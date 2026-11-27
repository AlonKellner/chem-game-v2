#include "chem/thermo.hpp"

#include <algorithm>

#include "chem/math.hpp"

#include <cmath>
#include <cstdint>
#include <limits>

namespace chem {

namespace {

// A cell's solids' and liquids' volume in two parts (Stage 67): what is fixed (its solids), and what grows with the
// solvent (its liquids, and its solutes' apparent volumes), at 25 C; m^3. false when a condensed species' molar
// volume is unknown.
struct Volumes {
	bool defined = false;
	double fixed = 0.0, expanding = 0.0;
};

Volumes volumes_of(const SpeciesTable &table, const CellState &cell) {
	Volumes out;
	for (size_t s = 0; s < cell.amount.size(); ++s) {
		if (cell.amount[s] == 0) {
			continue;
		}
		const SpeciesDef &def = table.species(SpeciesId{ static_cast<int32_t>(s) });
		if (def.wall || def.phase == Phase::Gas) {
			continue;
		}
		const double v = static_cast<double>(cell.amount[s]) * MOL_PER_QUANTUM * def.molar_volume;
		if (def.phase == Phase::Aqueous) { // its apparent molar volume, if it has one (GAME_SPEC §10)
			out.expanding += v;
			continue;
		}
		if (!(def.molar_volume > 0.0)) {
			return out;
		}
		(def.phase == Phase::Liquid ? out.expanding : out.fixed) += v;
	}
	out.defined = true;
	return out;
}

// the cell's gas, mol (walls are never gas)
double gas_mol(const SpeciesTable &table, const CellState &cell) {
	double gas = 0.0;
	for (size_t s = 0; s < cell.amount.size(); ++s) {
		if (cell.amount[s] > 0 && table.species(SpeciesId{ static_cast<int32_t>(s) }).phase == Phase::Gas) {
			gas += static_cast<double>(cell.amount[s]);
		}
	}
	return gas * MOL_PER_QUANTUM;
}

bool is_sealed(const CellMeta *meta) {
	return meta != nullptr && meta->sealed && meta->rigid && !meta->reservoir;
}

// A sealed cell's energy by its temperature (GAME_SPEC §6, Stage 67). Its solids and liquids incompressible but
// growing with temperature, V_c(T) = fixed + expanding e(T): their Gibbs energy G0(T) + V_c(T) (P - P_atm), so
// that at the room's pressure they are the table's, and E = U + P_atm V is
// E = H(T) - n_gas R T + P_atm V_free(T) - T V_c'(T) (P - P_atm), P = n_gas R T / V_free,
// its entropy's part S0(T) + the gas's - V_c'(T) (P - P_atm): one potential's, so dE = T dS (T2).
struct Sealed {
	double formation = 0.0, capacity = 0.0; // J, J/K: sum of n_i Hf_i, of n_i Cp_i
	double gas = 0.0; // mol
	double volume = 0.0, fixed = 0.0, expanding = 0.0; // m^3

	struct State {
		double temperature = 0.0, energy = 0.0, slope = 0.0; // K, J, dE/dT (J/K)
		double free = 0.0, pressure = 0.0; // m^3, Pa
	};

	// false where its solids and liquids leave its gas no room
	bool at(const SpeciesTable &table, double temperature, State &out) const {
		double de = 0.0, dde = 0.0;
		const double e = expansion(table, temperature, &de, &dde);
		out.temperature = temperature;
		out.free = volume - fixed - expanding * e;
		if (!(out.free > 0.0) || !(temperature > 0.0)) {
			return false;
		}
		const double grows = expanding * de, bends = expanding * dde; // dV_c/dT, d2V_c/dT2
		out.pressure = gas * GAS_CONSTANT * temperature / out.free;
		const double over = out.pressure - P_ATMOSPHERE;
		const double rises = out.pressure / temperature * (1.0 + temperature * grows / out.free); // dP/dT
		out.energy = formation + capacity * (temperature - T0) - gas * GAS_CONSTANT * temperature + P_ATMOSPHERE * out.free - temperature * grows * over;
		out.slope = capacity - gas * GAS_CONSTANT - P_ATMOSPHERE * grows - grows * over - temperature * bends * over - temperature * grows * rises;
		return true;
	}

	// The temperature at which it holds `energy` J, from `guess` K (0: none), by Newton's steps: the linear model's
	// (Stage 28's, nothing growing) first, then each by the slope, halved while it would leave the gas no room
	bool solve(const SpeciesTable &table, double energy, double guess, State &out) const {
		const double cv = capacity - gas * GAS_CONSTANT;
		if (!(cv > 0.0)) {
			return false;
		}
		double t = guess > 0.0 ? guess : (energy - P_ATMOSPHERE * (volume - fixed - expanding) - formation + capacity * T0) / cv;
		if (!(t > 0.0) || !at(table, t, out)) {
			t = T0;
			if (!at(table, t, out)) {
				return false;
			}
		}
		for (int i = 0; i < 64; ++i) {
			if (!(out.slope > 0.0)) {
				return false;
			}
			double step = (energy - out.energy) / out.slope;
			State next;
			int halved = 0;
			while (!at(table, t + step, next) && halved < 60) {
				step *= 0.5;
				++halved;
			}
			if (halved == 60) {
				return false;
			}
			t += step;
			out = next;
			if (std::fabs(step) <= 1e-13 * t) {
				return true;
			}
		}
		return std::fabs(energy - out.energy) <= 1e-9 * (std::fabs(energy) + 1.0);
	}
};

} // namespace

Thermal thermal_state(const SpeciesTable &table, const CellState &cell, const CellMeta *meta) {
	Thermal out;
	double formation = 0.0;
	double capacity = 0.0;
	for (size_t s = 0; s < cell.amount.size(); ++s) {
		if (cell.amount[s] == 0) {
			continue;
		}
		const SpeciesDef &def = table.species(SpeciesId{ static_cast<int32_t>(s) });
		if (!def.has_thermo) {
			return out;
		}
		const double mol = static_cast<double>(cell.amount[s]) * MOL_PER_QUANTUM;
		formation += mol * def.hf;
		capacity += mol * def.cp;
	}
	if (!(capacity > 0.0)) {
		return out;
	}
	if (is_sealed(meta)) {
		const Volumes volumes = volumes_of(table, cell);
		if (!volumes.defined) {
			return out;
		}
		const Sealed model{ formation, capacity, gas_mol(table, cell), meta->volume, volumes.fixed, volumes.expanding };
		Sealed::State at;
		if (!model.solve(table, static_cast<double>(cell.energy) * JOULE_PER_QUANTUM, 0.0, at)) {
			return out; // no room for its gas, or no temperature that holds its energy: no pressure (V3)
		}
		out.sealed = true;
		out.free_volume = at.free;
		out.heat_capacity = at.slope;
		out.temperature = at.temperature;
		out.pressure = at.pressure;
		out.defined = true;
		return out;
	}
	out.defined = true;
	out.heat_capacity = capacity;
	out.temperature = T0 + (static_cast<double>(cell.energy) * JOULE_PER_QUANTUM - formation) / capacity;
	return out;
}

double enthalpy_at(const SpeciesTable &table, const CellState &cell, double temperature, bool &ok) {
	ok = true;
	double h = 0.0;
	for (size_t s = 0; s < cell.amount.size(); ++s) {
		if (cell.amount[s] == 0) {
			continue;
		}
		const SpeciesDef &def = table.species(SpeciesId{ static_cast<int32_t>(s) });
		if (!def.has_thermo) {
			ok = false;
			return 0.0;
		}
		h += static_cast<double>(cell.amount[s]) * MOL_PER_QUANTUM * (def.hf + def.cp * (temperature - T0));
	}
	return h;
}

Energy enthalpy_carried(const SpeciesTable &table, std::span<const SpeciesAmount> amounts, double temperature) {
	double joule = 0.0;
	for (const SpeciesAmount &a : amounts) {
		const SpeciesDef &def = table.species(a.species);
		joule += static_cast<double>(a.amount.quanta) * MOL_PER_QUANTUM * (def.hf + def.cp * (temperature - T0));
	}
	bool ok = false;
	return energy_from_joule(joule, ok);
}

namespace {

constexpr double INFINITE = std::numeric_limits<double>::infinity();

// A solute's molality is its amount per 55.51 mol of the cell's liquid: per kg
// when the liquid is water (18.015 g/mol). Counting the solvent in moles, not
// kilograms, is what keeps a mixed solvent consistent with its solutes
// (Gibbs-Duhem).
constexpr double SOLVENT_MOL_PER_UNIT = 1.0 / 0.018015;

// What a cell's mixtures hold, in quanta: its gas; its liquid (the solvent)
// and the particles dissolved in it (GAME_SPEC §4.6). Walls are in neither.
// With them, the excess of the solution (GAME_SPEC §10): each particle's
// ln gamma, and the solvent's share of it.
struct Mix {
	// (Stage 64) the table's own particles only are set and read: n of them zeroed, not the whole bound's
	explicit Mix(size_t n) {
		std::fill_n(particles.begin(), n, 0.0);
		std::fill_n(ln_gamma.begin(), n, 0.0);
	}
	// (Stage 64) the listed particles only: no other is read
	explicit Mix(const uint8_t *ions, size_t count) {
		for (size_t k = 0; k < count; ++k) {
			particles[ions[k]] = 0.0;
			ln_gamma[ions[k]] = 0.0;
		}
	}
	double gas = 0.0;
	double liquid = 0.0;
	std::array<double, MAX_PARTICLES> particles;
	double dissolved = 0.0; // the particles' sum
	std::array<double, MAX_PARTICLES> ln_gamma;
	double solvent_excess = 0.0; // the excess part of ln a of the solvent
	double pressure_ratio = P_ATMOSPHERE / P_STANDARD; // the cell's pressure over the standard's (Stage 28: a sealed cell's own)
};

// g(x) = 2 (1 - (1 + x) e^-x) / x^2 and h(x) = e^-x - g(x), by their series
// where they cancel (GAME_SPEC §10)
void pitzer_g(double x, double &g, double &h) {
	if (x < 1e-3) {
		g = 1.0 - x * (2.0 / 3.0 - x * (0.25 - x / 15.0));
		h = -x * (1.0 / 3.0 - x * (0.25 - x * 0.1));
		return;
	}
	const double e = exp(-x);
	g = 2.0 * (1.0 - (1.0 + x) * e) / (x * x);
	h = e - g;
}

// Pitzer's excess (GAME_SPEC §10): F = G_ex / (w R T) per kg of water, as a
// function of the molalities, and its derivatives ln gamma_j = dF/dm_j. The
// solvent's share follows: w d(wF)/dw = F - sum m_j F_j, per kg.
// (Stage 64) `subset`: only the particles listed (ascending), which hold every one the mixture has, and only the pairs
// listed (in the table's order), which hold every pair of them: the same sums in the same order, less the terms that are
// nought, so the same values bit for bit for the particles listed; another's ln gamma is left as it was
struct Subset {
	const uint8_t *ions = nullptr;
	size_t ion_count = 0;
	const uint16_t *pairs = nullptr;
	size_t pair_count = 0;
};

void excess_of(const SolutionDef &model, Mix &mix, const Subset *subset = nullptr) {
	const size_t n = subset ? subset->ion_count : model.ions.size();
	const auto ion = [&](size_t k) { return subset ? static_cast<size_t>(subset->ions[k]) : k; };
	std::array<double, MAX_PARTICLES> m; // (Stage 64: the table's own particles only are set and read)
	double ionic = 0.0, z_sum = 0.0;
	for (size_t k = 0; k < n; ++k) {
		const size_t j = ion(k);
		m[j] = mix.particles[j] / mix.liquid * SOLVENT_MOL_PER_UNIT;
		const double z = static_cast<double>(model.ions[j].charge);
		ionic += 0.5 * m[j] * z * z;
		z_sum += m[j] * (z < 0.0 ? -z : z);
	}
	if (!(ionic > 0.0)) {
		return; // only neutral particles: ideal
	}
	const double s = std::sqrt(ionic);
	const double lnp = log(1.0 + model.b * s);
	double f = -model.a_phi * 4.0 * ionic / model.b * lnp;
	const double f_ionic = -model.a_phi * (4.0 / model.b * lnp + 2.0 * s / (1.0 + model.b * s)); // df/dI
	std::array<double, MAX_PARTICLES> d;
	for (size_t k = 0; k < n; ++k) {
		d[ion(k)] = 0.0;
	}
	double d_ionic = f_ionic; // dF/dI
	double d_z = 0.0; // dF/dZ
	// g and h of each exponent once: pairs share them (alpha2 is 12 for most)
	struct Known {
		double alpha = 0.0, g = 0.0, h = 0.0;
	};
	std::array<Known, 4> known{};
	size_t kept = 0;
	const auto of = [&](double alpha, double &g, double &h) {
		for (size_t k = 0; k < kept; ++k) {
			if (known[k].alpha == alpha) {
				g = known[k].g;
				h = known[k].h;
				return;
			}
		}
		pitzer_g(alpha * s, g, h);
		if (kept < known.size()) {
			known[kept++] = Known{ alpha, g, h };
		}
	};
	const size_t pair_count = subset ? subset->pair_count : model.pairs.size();
	for (size_t k = 0; k < pair_count; ++k) {
		const PairDef &p = model.pairs[subset ? static_cast<size_t>(subset->pairs[k]) : k];
		const auto c = static_cast<size_t>(p.cation), a = static_cast<size_t>(p.anion);
		const double mm = m[c] * m[a];
		if (!(mm > 0.0)) {
			continue;
		}
		double g1 = 0.0, h1 = 0.0, g2 = 0.0, h2 = 0.0;
		of(p.alpha1, g1, h1);
		of(p.alpha2, g2, h2);
		const double b_ca = p.beta0 + p.beta1 * g1 + p.beta2 * g2;
		const double b_ionic = (p.beta1 * h1 + p.beta2 * h2) / ionic; // dB/dI
		const double zc = static_cast<double>(model.ions[c].charge), za = static_cast<double>(-model.ions[a].charge);
		const double c_ca = p.c_phi / (2.0 * std::sqrt(zc * za));
		// m_c m_a (2 B + Z C), Z the sum over every ion of m |z| (Pitzer 1991; until Stage 47 this read 2 m_c m_a (B + Z C),
		// which counts C twice: no pair with a C^phi was gated where it shows, 2:2 salts above 0.5 mol/kg)
		f += mm * (2.0 * b_ca + z_sum * c_ca);
		d[c] += m[a] * (2.0 * b_ca + z_sum * c_ca);
		d[a] += m[c] * (2.0 * b_ca + z_sum * c_ca);
		d_ionic += 2.0 * mm * b_ionic;
		d_z += mm * c_ca;
	}
	double weighted = 0.0;
	for (size_t k = 0; k < n; ++k) {
		const size_t j = ion(k);
		const double z = static_cast<double>(model.ions[j].charge);
		mix.ln_gamma[j] = d[j] + d_ionic * 0.5 * z * z + d_z * (z < 0.0 ? -z : z);
		weighted += m[j] * mix.ln_gamma[j];
	}
	mix.solvent_excess = (f - weighted) / SOLVENT_MOL_PER_UNIT;
}

// the particles a dissolved species adds to the cell, per quantum of it
void add_particles(const SpeciesDef &def, double quanta, Mix &mix) {
	for (const IonCount &ion : def.ions) {
		if (ion.count <= 0) {
			break;
		}
		const double q = quanta * static_cast<double>(ion.count);
		mix.particles[static_cast<size_t>(ion.ion)] += q;
		mix.dissolved += q;
	}
}

// the excess, once the mixture's amounts are known
void complete(const SpeciesTable &table, Mix &mix, const Subset *subset = nullptr) {
	if (mix.liquid > 0.0 && mix.dissolved > 0.0) {
		excess_of(table.solution(), mix, subset);
	}
}

// The cell's mixtures; with `excess`, the solution's excess too (GAME_SPEC §10)
Mix mix_of(const SpeciesTable &table, const CellState &cell, bool excess = true) {
	Mix mix(table.solution().ions.size());
	for (size_t s = 0; s < cell.amount.size(); ++s) {
		if (cell.amount[s] <= 0) {
			continue;
		}
		const SpeciesDef &def = table.species(SpeciesId{ static_cast<int32_t>(s) });
		const auto quanta = static_cast<double>(cell.amount[s]);
		if (def.wall) {
			continue;
		}
		if (def.phase == Phase::Gas) {
			mix.gas += quanta;
		} else if (def.phase == Phase::Liquid) {
			mix.liquid += quanta;
		} else if (def.phase == Phase::Aqueous) {
			add_particles(def, quanta, mix);
		}
	}
	if (excess) {
		complete(table, mix);
	}
	return mix;
}

enum class Presence { Present, Absent, Undefined };

// The logarithm of a species' activity (GAME_SPEC §4.6), given how much of it
// there is and the cell's mixtures.
//   gas:    y_i P / P°, y_i its fraction of the cell's gas. Absent when there is none.
//   liquid: ln a = ln x_i - phi (dissolved) / (liquid), x_i its fraction of
//           the cell's liquids, phi the osmotic coefficient: the solvent's
//           share of the solution's Gibbs energy, consistent with the
//           solutes' (Gibbs-Duhem), so the entropy is a state function. Where
//           the cell has no liquid at all, one would form pure, at activity 1.
//           Absent from a liquid that is there, its activity is 0: liquids mix.
//   solute: the sum over its particles of nu_j ln(m_j gamma_j), m_j the
//           particle's molality (mol per 55.51 mol of the cell's liquid: per
//           kg of water). Absent when one of its particles is; undefined
//           without a solvent.
//   solid:  1.
Presence log_activity(const SpeciesDef &def, double quanta, const Mix &mix, double &ln) {
	ln = 0.0;
	switch (def.phase) {
		case Phase::Gas:
			if (!(quanta > 0.0) || !(mix.gas > 0.0)) {
				return Presence::Absent;
			}
			ln = log(quanta / mix.gas * mix.pressure_ratio);
			return Presence::Present;
		case Phase::Liquid:
			if (!(mix.liquid > 0.0)) {
				ln = mix.dissolved > 0.0 ? -INFINITE : 0.0;
				return Presence::Present;
			}
			if (!(quanta > 0.0)) {
				return Presence::Absent; // it would mix into the liquid that is there
			}
			ln = (quanta < mix.liquid ? log(quanta / mix.liquid) : 0.0) - mix.dissolved / mix.liquid + mix.solvent_excess;
			return Presence::Present;
		case Phase::Aqueous:
			if (!(mix.liquid > 0.0)) {
				return Presence::Undefined;
			}
			for (const IonCount &ion : def.ions) {
				if (ion.count <= 0) {
					break;
				}
				const auto j = static_cast<size_t>(ion.ion);
				if (!(mix.particles[j] > 0.0)) {
					return Presence::Absent;
				}
				ln += static_cast<double>(ion.count) * (log(mix.particles[j] / mix.liquid * SOLVENT_MOL_PER_UNIT) + mix.ln_gamma[j]);
			}
			return Presence::Present;
		case Phase::Solid:
			return Presence::Present;
	}
	return Presence::Present;
}

} // namespace

Fill fill_of(const SpeciesTable &table, const CellState &cell, double temperature, const CellMeta *meta) {
	Fill out;
	const Volumes volumes = volumes_of(table, cell);
	if (!volumes.defined) {
		return out;
	}
	out.condensed = volumes.fixed + volumes.expanding * (volumes.expanding != 0.0 ? expansion(table, temperature) : 1.0);
	out.gas = is_sealed(meta) ? meta->volume - out.condensed : gas_mol(table, cell) * GAS_CONSTANT * temperature / P_ATMOSPHERE;
	out.defined = true;
	return out;
}

namespace {

// the rational form and its first two derivatives at t C
void kell(const SolutionDef &s, double t, double &rho, double &slope, double &curve) {
	double n = 0.0, dn = 0.0, ddn = 0.0;
	for (size_t k = s.density.size(); k-- > 0;) {
		ddn = ddn * t + 2.0 * dn;
		dn = dn * t + n;
		n = n * t + s.density[k];
	}
	const double d = 1.0 + s.density_b * t;
	rho = n / d;
	slope = (dn - rho * s.density_b) / d;
	curve = (ddn - 2.0 * slope * s.density_b) / d;
}

} // namespace

double solvent_density(const SpeciesTable &table, double temperature, double *slope, double *curve) {
	const SolutionDef &s = table.solution();
	double rho = 0.0, d = 0.0, dd = 0.0;
	if (s.density[0] != 0.0 || s.density_b != 0.0) {
		const double t = temperature - 273.15;
		const double end = t < s.density_low ? s.density_low : t > s.density_high ? s.density_high : t;
		kell(s, end, rho, d, dd);
		if (end != t) { // beyond its range: its value and slope at the end
			rho += d * (t - end);
			dd = 0.0;
		}
	}
	if (slope != nullptr) {
		*slope = d;
	}
	if (curve != nullptr) {
		*curve = dd;
	}
	return rho;
}

double expansion(const SpeciesTable &table, double temperature, double *slope, double *curve) {
	double d = 0.0, dd = 0.0;
	const double rho = solvent_density(table, temperature, &d, &dd), at25 = solvent_density(table, T0);
	const bool known = rho > 0.0 && at25 > 0.0;
	if (slope != nullptr) {
		*slope = known ? -at25 * d / (rho * rho) : 0.0;
	}
	if (curve != nullptr) {
		*curve = known ? -at25 * (dd * rho - 2.0 * d * d) / (rho * rho * rho) : 0.0;
	}
	return known ? at25 / rho : 1.0;
}

double molar_volume_at(const SpeciesTable &table, const SpeciesDef &def, double temperature) {
	return def.phase == Phase::Liquid || def.phase == Phase::Aqueous ? def.molar_volume * expansion(table, temperature) : def.molar_volume;
}

bool energy_at(const SpeciesTable &table, const CellState &cell, const CellMeta *meta, double temperature, double &joule) {
	bool ok = false;
	joule = enthalpy_at(table, cell, temperature, ok);
	if (!ok || !is_sealed(meta)) {
		return ok;
	}
	const Volumes volumes = volumes_of(table, cell);
	double formation = 0.0, capacity = 0.0;
	for (size_t s = 0; s < cell.amount.size(); ++s) {
		const SpeciesDef &def = table.species(SpeciesId{ static_cast<int32_t>(s) });
		formation += static_cast<double>(cell.amount[s]) * MOL_PER_QUANTUM * def.hf;
		capacity += static_cast<double>(cell.amount[s]) * MOL_PER_QUANTUM * def.cp;
	}
	const Sealed model{ formation, capacity, gas_mol(table, cell), meta->volume, volumes.fixed, volumes.expanding };
	Sealed::State at;
	if (!volumes.defined || !model.at(table, temperature, at)) {
		return false;
	}
	joule = at.energy;
	return true;
}

double gas_excess(const SpeciesTable &table, const CellMeta &meta, const CellState &cell, bool &defined) {
	defined = false;
	const Thermal thermal = thermal_state(table, cell, &meta);
	if (!thermal.defined || !(thermal.temperature > 0.0)) {
		return 0.0;
	}
	if (thermal.sealed) {
		defined = true;
		return 0.0; // full by definition: its pressure follows
	}
	const Fill fill = fill_of(table, cell, thermal.temperature);
	if (!fill.defined) {
		return 0.0;
	}
	defined = true;
	return fill.gas + fill.condensed - meta.volume;
}

double particle_diameter(const SpeciesTable &table, const CellState &cell, SpeciesId species) {
	const auto s = static_cast<size_t>(species.index);
	if (s >= cell.particles.size() || cell.particles[s] <= 0 || cell.amount[s] <= 0) {
		return 0.0;
	}
	const double each = static_cast<double>(cell.amount[s]) * MOL_PER_QUANTUM * table.species(species).molar_volume / static_cast<double>(cell.particles[s]);
	return cbrt(each * 6.0 / 3.14159265358979323846);
}

double solvent_viscosity(const SpeciesTable &table, double temperature) {
	const std::vector<ViscosityPoint> &points = table.solution().viscosity;
	if (points.empty()) {
		return 0.0;
	}
	if (!(temperature > points.front().kelvin)) {
		return points.front().pascal_seconds;
	}
	for (size_t i = 1; i < points.size(); ++i) {
		if (temperature <= points[i].kelvin) {
			const ViscosityPoint &a = points[i - 1], &b = points[i];
			const double along = (temperature - a.kelvin) / (b.kelvin - a.kelvin);
			return a.pascal_seconds * exp(along * log(b.pascal_seconds / a.pascal_seconds));
		}
	}
	return points.back().pascal_seconds;
}

SolventPoint solvent_at(const SpeciesTable &table, double temperature) {
	const std::vector<SolventPoint> &rows = table.solution().solvent;
	if (rows.empty()) {
		return SolventPoint{};
	}
	if (!(temperature > rows.front().kelvin)) {
		return rows.front();
	}
	if (!(temperature < rows.back().kelvin)) {
		return rows.back();
	}
	size_t i = 1;
	while (rows[i].kelvin < temperature) {
		++i;
	}
	const SolventPoint &a = rows[i - 1], &b = rows[i];
	const double f = (temperature - a.kelvin) / (b.kelvin - a.kelvin);
	const auto mix = [f](double x, double y) { return x + (y - x) * f; };
	return SolventPoint{ temperature, mix(a.rho, b.rho), mix(a.cp, b.cp), mix(a.k, b.k), mix(a.alpha, b.alpha), mix(a.nu, b.nu), mix(a.pr, b.pr), mix(a.beta, b.beta) };
}

bool liquid_density(const SpeciesTable &table, const CellState &cell, double temperature, double &density) {
	double grams = 0.0, volume = 0.0, liquid = 0.0; // g, m^3 at 25 C, quanta of liquid
	for (size_t s = 0; s < cell.amount.size(); ++s) {
		const SpeciesId id{ static_cast<int32_t>(s) };
		const SpeciesDef &def = table.species(id);
		if (cell.amount[s] <= 0 || def.wall || (def.phase != Phase::Liquid && def.phase != Phase::Aqueous)) {
			continue;
		}
		const double mol = static_cast<double>(cell.amount[s]) * MOL_PER_QUANTUM;
		grams += mol * static_cast<double>(table.molar_mass_ug(id)) * 1e-6;
		volume += mol * def.molar_volume;
		liquid += def.phase == Phase::Liquid ? static_cast<double>(cell.amount[s]) : 0.0;
	}
	if (!(liquid > 0.0) || !(volume > 0.0)) {
		return false;
	}
	density = grams * 1e-3 / (volume * expansion(table, temperature));
	return true;
}

double gas_diffusion(const SpeciesTable &table, SpeciesId species, double temperature) {
	const SpeciesDef &def = table.species(species);
	if (def.phase != Phase::Gas || !(def.air_diffusion > 0.0) || !(temperature > 0.0)) {
		return 0.0;
	}
	return def.air_diffusion * chem::pow(temperature / 298.15, def.air_exponent);
}

double liquid_density(const SpeciesTable &table, const CellState &cell) {
	double kg = 0.0, m3 = 0.0;
	for (size_t s = 0; s < cell.amount.size(); ++s) {
		const SpeciesId id{ static_cast<int32_t>(s) };
		const SpeciesDef &def = table.species(id);
		if (cell.amount[s] > 0 && !def.wall && (def.phase == Phase::Liquid || def.phase == Phase::Aqueous)) {
			const double mol = static_cast<double>(cell.amount[s]) * MOL_PER_QUANTUM;
			kg += mol * static_cast<double>(table.molar_mass_ug(id)) * 1e-9;
			m3 += mol * def.molar_volume;
		}
	}
	return m3 > 0.0 ? kg / m3 : 0.0;
}

double dissipation_per_kg(const SpeciesTable &table, const CellState &cell, double watts) {
	if (!(watts > 0.0)) {
		return 0.0;
	}
	double kg = 0.0;
	for (size_t s = 0; s < cell.amount.size(); ++s) {
		const SpeciesId id{ static_cast<int32_t>(s) };
		const SpeciesDef &def = table.species(id);
		if (cell.amount[s] > 0 && !def.wall && (def.phase == Phase::Liquid || def.phase == Phase::Aqueous)) {
			kg += static_cast<double>(cell.amount[s]) * MOL_PER_QUANTUM * static_cast<double>(table.molar_mass_ug(id)) * 1e-9;
		}
	}
	return kg > 0.0 ? watts / kg : 0.0;
}

double diffusion(const SpeciesTable &table, SpeciesId species, double temperature) {
	const SpeciesDef &def = table.species(species);
	if (def.phase != Phase::Aqueous || !(temperature > 0.0)) {
		return 0.0;
	}
	double count = 0.0, resistance = 0.0;
	for (const IonCount &ion : def.ions) {
		if (ion.count <= 0) {
			break;
		}
		const double d = ion_diffusion(table, static_cast<size_t>(ion.ion), temperature);
		if (!(d > 0.0)) {
			return 0.0;
		}
		count += ion.count;
		resistance += ion.count / d;
	}
	if (!(count > 0.0)) {
		return 0.0;
	}
	return count / resistance;
}

double ion_diffusion(const SpeciesTable &table, size_t ion, double temperature) {
	const IonDef &def = table.solution().ions[ion];
	if (!(def.diffusion > 0.0) || !(temperature > 0.0)) {
		return 0.0;
	}
	// Stokes and Einstein, D as T / viscosity from its value at 25 C; Stage 65: Walden's rule with the ion's own
	// exponent, T / viscosity^p (1: Stokes and Einstein)
	const double at = solvent_viscosity(table, temperature), at25 = solvent_viscosity(table, 298.15);
	if (!(at > 0.0 && at25 > 0.0)) {
		return def.diffusion;
	}
	const double ratio = at25 / at;
	return def.diffusion * temperature / 298.15 * (def.walden == 1.0 ? ratio : exp(def.walden * log(ratio)));
}

bool dry_solute(const SpeciesTable &table, const CellState &cell) {
	const Mix mix = mix_of(table, cell);
	return mix.dissolved > 0.0 && !(mix.liquid > 0.0);
}

int64_t feasible_extent(const CellState &cell, std::span<const Term> terms, bool forward) {
	int64_t most = INT64_MAX;
	for (const Term &t : terms) {
		const int64_t per = forward ? -t.nu : t.nu; // > 0 for the species this direction consumes
		if (per > 0) {
			const int64_t can = cell.amount[static_cast<size_t>(t.species.index)] / per;
			most = can < most ? can : most;
		}
	}
	return most;
}

namespace {

// a sealed cell's pressure over the standard's, at temperature T (its gas of `gas` quanta), in the room its solids and
// liquids leave at T
double sealed_ratio(const SpeciesTable &table, const CellState &cell, const CellMeta &meta, double temperature, double gas) {
	const Fill fill = fill_of(table, cell, temperature, &meta);
	if (!fill.defined || !(fill.gas > 0.0)) {
		return INFINITE;
	}
	return gas * MOL_PER_QUANTUM * GAS_CONSTANT * temperature / (fill.gas * P_STANDARD);
}

// what a sealed cell's pressure off the room's adds to its condensed species' chemical potentials, J/mol for each
// m^3/mol of molar volume at T (G0 + V_c (P - P_atm), Stage 67); 0 open, or with no pressure
double over_room(const CellMeta *meta, double ratio) {
	return is_sealed(meta) && ratio < INFINITE ? ratio * P_STANDARD - P_ATMOSPHERE : 0.0;
}

} // namespace

double activity(const SpeciesTable &table, const CellState &cell, SpeciesId species, const CellMeta *meta) {
	double ln = 0.0;
	Mix mix = mix_of(table, cell);
	if (is_sealed(meta)) {
		mix.pressure_ratio = thermal_state(table, cell, meta).pressure / P_STANDARD;
	}
	const Presence presence = log_activity(table.species(species), static_cast<double>(cell.amount[static_cast<size_t>(species.index)]), mix, ln);
	return presence == Presence::Present ? exp(ln) : 0.0;
}

double water_ionisation(const SpeciesTable &table, double temperature) {
	const SolutionDef &s = table.solution();
	if (!(s.ionisation_g > 0.0) || !(temperature > 0.0)) {
		return 0.0;
	}
	const double t0 = T0;
	const double entropy = (s.ionisation_h - s.ionisation_g) / t0;
	const double g = s.ionisation_h + s.ionisation_cp * (temperature - t0) - temperature * (entropy + s.ionisation_cp * log(temperature / t0));
	return exp(-g / (GAS_CONSTANT * temperature));
}

namespace {

// The free hydrogen and hydroxide ions (Stages 43 and 63): their molalities at their exact balance with water, from
// their difference d = m_H - m_OH, which the ionisation leaves unchanged; false without Kw or a liquid
struct WaterIons {
	int h = -1, oh = -1; // their particles, or -1
	double d = 0.0, m_h = 0.0, m_oh = 0.0, gamma_h = 1.0;
};

bool water_ions(const SpeciesTable &table, const Mix &mix, double temperature, WaterIons &w) {
	const double kw = water_ionisation(table, temperature);
	if (!(kw > 0.0) || !(mix.liquid > 0.0)) {
		return false;
	}
	const std::vector<IonDef> &ions = table.solution().ions;
	const auto index_of = [&](std::string_view name) {
		for (size_t j = 0; j < ions.size(); ++j) {
			if (ions[j].name == name) {
				return static_cast<int>(j);
			}
		}
		return -1;
	};
	w.h = index_of("H+1");
	w.oh = index_of("OH-1");
	const auto molal = [&](int j) { return j >= 0 ? mix.particles[static_cast<size_t>(j)] / mix.liquid * SOLVENT_MOL_PER_UNIT : 0.0; };
	const auto gamma = [&](int j) { return j >= 0 ? exp(mix.ln_gamma[static_cast<size_t>(j)]) : 1.0; };
	// the solvent's activity: the liquid is water, ln a = -dissolved / liquid + its excess (as log_activity has it)
	const double water = exp(-mix.dissolved / mix.liquid + mix.solvent_excess);
	w.gamma_h = gamma(w.h);
	const double k = kw * water / (w.gamma_h * gamma(w.oh));
	w.d = molal(w.h) - molal(w.oh);
	// without the difference of near equals: m_H = d/2 + sqrt(d^2/4 + k), m_OH = m_H - d
	const double root = std::sqrt(0.25 * w.d * w.d + k);
	w.m_h = w.d >= 0.0 ? 0.5 * w.d + root : k / (root - 0.5 * w.d);
	w.m_oh = w.d >= 0.0 ? k / (root + 0.5 * w.d) : root - 0.5 * w.d;
	return true;
}

} // namespace

bool ph(const SpeciesTable &table, const CellState &cell, double temperature, double &out) {
	const Mix mix = mix_of(table, cell);
	WaterIons w;
	if (!water_ions(table, mix, temperature, w)) {
		return false;
	}
	// beyond the activity model's range (Pitzer and Mayorga's fits hold to 6 mol/kg) a solution has no pH the game
	// can read: a base or an acid boiled nearly dry, with no solid for it to come out as (Stage 44)
	if (w.d > PH_MOST_EXCESS || w.d < -PH_MOST_EXCESS) {
		return false;
	}
	out = -log(w.gamma_h * w.m_h) / log(10.0);
	return true;
}

double solvent_dielectric(const SpeciesTable &table, double temperature) {
	const std::vector<DielectricPoint> &points = table.solution().dielectric;
	if (points.empty()) {
		return 0.0;
	}
	if (!(temperature > points.front().kelvin)) {
		return points.front().epsilon;
	}
	for (size_t i = 1; i < points.size(); ++i) {
		if (temperature <= points[i].kelvin) {
			const DielectricPoint &a = points[i - 1], &b = points[i];
			return a.epsilon + (b.epsilon - a.epsilon) * (temperature - a.kelvin) / (b.kelvin - a.kelvin);
		}
	}
	return points.back().epsilon;
}

bool onsager_constants(const SpeciesTable &table, double temperature, double &b1, double &b2, double &b) {
	const double epsilon = solvent_dielectric(table, temperature), eta = solvent_viscosity(table, temperature);
	if (!(epsilon > 0.0) || !(eta > 0.0) || !(temperature > 0.0)) {
		return false;
	}
	// (Stage 65) Robinson and Stokes' numerical forms (from CODATA's constants of their day; NSRDS-NBS 33, Table 3,
	// agrees to its last digit): B1 = 8.2046e5 / (eps T)^1.5, B2 = 82.487 / (eta[P] (eps T)^0.5) S cm^2/mol,
	// B = 50.29 / (eps T)^0.5 per Angstrom, each with (L/mol)^1/2
	const double et = epsilon * temperature, root = std::sqrt(et);
	b1 = 8.2046e5 / (et * root);
	b2 = 82.487 / (eta * 10.0 * root) * 1e-4;
	b = 50.29 / root * 1e10;
	return true;
}

bool conductivity(const SpeciesTable &table, const CellState &cell, double temperature, double &kappa, double *limit) {
	const SolutionDef &solution = table.solution();
	double b1 = 0.0, b2 = 0.0, b = 0.0;
	if (!(solution.ion_size > 0.0) || !onsager_constants(table, temperature, b1, b2, b)) {
		return false;
	}
	const Mix mix = mix_of(table, cell);
	if (!(mix.liquid > 0.0)) {
		return false;
	}
	// the liquid's volume, m^3: its liquids' and what is dissolved in them, at T (Stage 67)
	double volume = 0.0;
	for (size_t s = 0; s < cell.amount.size(); ++s) {
		if (cell.amount[s] <= 0) {
			continue;
		}
		const SpeciesDef &def = table.species(SpeciesId{ static_cast<int32_t>(s) });
		if (!def.wall && (def.phase == Phase::Liquid || def.phase == Phase::Aqueous)) {
			volume += static_cast<double>(cell.amount[s]) * MOL_PER_QUANTUM * molar_volume_at(table, def, temperature);
		}
	}
	if (!(volume > 0.0)) {
		return false;
	}
	// each ion's concentration, mol/m^3; the hydrogen and hydroxide ions' at their exact balance with water
	WaterIons w;
	const bool balanced = water_ions(table, mix, temperature, w);
	const double per_quantum = MOL_PER_QUANTUM / volume, liquid_kg = mix.liquid * MOL_PER_QUANTUM / SOLVENT_MOL_PER_UNIT;
	const auto concentration = [&](size_t j) {
		if (balanced && static_cast<int>(j) == w.h) {
			return w.m_h * liquid_kg / volume;
		}
		if (balanced && static_cast<int>(j) == w.oh) {
			return w.m_oh * liquid_kg / volume;
		}
		return mix.particles[j] * per_quantum;
	};
	double ionic = 0.0; // mol/L
	for (size_t j = 0; j < solution.ions.size(); ++j) {
		const double z = static_cast<double>(solution.ions[j].charge);
		if (z != 0.0) {
			ionic += 0.5 * concentration(j) * z * z * 1e-3;
		}
	}
	const double root = std::sqrt(ionic), x = root / (1.0 + b * solution.ion_size * root);
	constexpr double FARADAY = 96485.33212; // C/mol (CODATA 2018)
	double sum = 0.0, kohlrausch = 0.0;
	for (size_t j = 0; j < solution.ions.size(); ++j) {
		const double z = static_cast<double>(solution.ions[j].charge);
		if (z == 0.0) {
			continue;
		}
		const double c = concentration(j);
		if (!(c > 0.0)) {
			continue;
		}
		// Nernst and Einstein, S m^2/mol; its atmosphere's relaxation and electrophoresis (exact for a 1:1
		// electrolyte's pair, z^2 for an ion of higher charge an approximation)
		const double lambda0 = z * z * FARADAY * FARADAY * ion_diffusion(table, j, temperature) / (GAS_CONSTANT * temperature);
		const double lambda = lambda0 * (1.0 - b1 * z * z * x) - 0.5 * b2 * z * z * x;
		sum += c * (lambda > 0.0 ? lambda : 0.0);
		kohlrausch += c * lambda0;
	}
	kappa = sum;
	if (limit != nullptr) {
		*limit = kohlrausch;
	}
	return true;
}

double ionic_strength(const SpeciesTable &table, const CellState &cell) {
	const Mix mix = mix_of(table, cell, false);
	if (!(mix.liquid > 0.0)) {
		return 0.0;
	}
	double ionic = 0.0;
	const std::vector<IonDef> &ions = table.solution().ions;
	for (size_t j = 0; j < ions.size(); ++j) {
		const double z = static_cast<double>(ions[j].charge);
		ionic += 0.5 * mix.particles[j] / mix.liquid * SOLVENT_MOL_PER_UNIT * z * z;
	}
	return ionic;
}

bool among_dissolved(const SpeciesTable &table, std::span<const Term> terms) {
	bool dissolved = false;
	for (const Term &t : terms) {
		const Phase phase = table.species(t.species).phase;
		if (phase != Phase::Aqueous && phase != Phase::Liquid) {
			return false;
		}
		dissolved = dissolved || phase == Phase::Aqueous;
	}
	return dissolved;
}

bool stands(const SpeciesTable &table, const CellState &cell, std::span<const Term> terms) {
	return among_dissolved(table, terms) && ionic_strength(table, cell) > MOST_IONIC_STRENGTH;
}

double molality(const SpeciesTable &table, const CellState &cell, SpeciesId species) {
	const Mix mix = mix_of(table, cell);
	return mix.liquid > 0.0 ? static_cast<double>(cell.amount[static_cast<size_t>(species.index)]) / mix.liquid * SOLVENT_MOL_PER_UNIT : 0.0;
}

double entropy(const SpeciesTable &table, const CellState &cell, double temperature, const CellMeta *meta) {
	const double ln_t = log(temperature / T0);
	Mix mix = mix_of(table, cell);
	if (is_sealed(meta)) {
		mix.pressure_ratio = sealed_ratio(table, cell, *meta, temperature, mix.gas);
	}
	double total = 0.0;
	for (size_t s = 0; s < cell.amount.size(); ++s) {
		if (cell.amount[s] <= 0) {
			continue;
		}
		const SpeciesDef &def = table.species(SpeciesId{ static_cast<int32_t>(s) });
		double molar = def.s0 + def.cp * ln_t;
		double ln = 0.0;
		if (!def.wall && log_activity(def, static_cast<double>(cell.amount[s]), mix, ln) == Presence::Present) {
			molar -= GAS_CONSTANT * ln;
		}
		total += static_cast<double>(cell.amount[s]) * MOL_PER_QUANTUM * molar;
	}
	const double over = over_room(meta, mix.pressure_ratio);
	if (over != 0.0) { // - dV_c/dT (P - P_atm): its liquids' growth at a pressure off the room's (Stage 67)
		double grows = 0.0;
		expansion(table, temperature, &grows);
		total -= volumes_of(table, cell).expanding * grows * over;
	}
	return total;
}

namespace {

// -sum of nu_i mu_i from its two parts, with the absent species' infinities
double combine(double standard, double mixing, double temperature, bool made_absent, bool consumed_absent) {
	if (made_absent || consumed_absent) {
		return made_absent && consumed_absent ? 0.0 : made_absent ? INFINITE : -INFINITE;
	}
	return standard - GAS_CONSTANT * temperature * mixing;
}

} // namespace

double affinity(const SpeciesTable &table, const CellState &cell, std::span<const Term> terms, double temperature, bool &defined, const CellMeta *meta) {
	defined = false;
	const double ln_t = log(temperature / T0);
	Mix mix = mix_of(table, cell);
	if (is_sealed(meta)) {
		mix.pressure_ratio = sealed_ratio(table, cell, *meta, temperature, mix.gas);
	}
	double standard = 0.0; // -sum of nu_i (h_i - T s_i) at unit activity
	double mixing = 0.0; // sum of nu_i ln a_i
	bool made_absent = false;
	bool consumed_absent = false;
	const double over = over_room(meta, mix.pressure_ratio);
	for (const Term &t : terms) {
		if (!table.has(t.species) || !table.species(t.species).has_thermo) {
			return 0.0;
		}
		const SpeciesDef &def = table.species(t.species);
		standard -= static_cast<double>(t.nu) * (def.hf + def.cp * (temperature - T0) - temperature * (def.s0 + def.cp * ln_t));
		if (over != 0.0 && def.phase != Phase::Gas && !def.wall) {
			standard -= static_cast<double>(t.nu) * molar_volume_at(table, def, temperature) * over; // its volume at the cell's pressure (Stage 67)
		}
		double ln = 0.0;
		switch (log_activity(def, static_cast<double>(cell.amount[static_cast<size_t>(t.species.index)]), mix, ln)) {
			case Presence::Present:
				mixing += static_cast<double>(t.nu) * ln;
				break;
			case Presence::Absent:
				(t.nu > 0 ? made_absent : consumed_absent) = true;
				break;
			case Presence::Undefined:
				return 0.0;
		}
	}
	defined = true;
	return combine(standard, mixing, temperature, made_absent, consumed_absent);
}

AffinityProbe::AffinityProbe(const SpeciesTable &table, const CellState &cell, std::span<const Term> terms, const CellMeta *meta) :
		AffinityProbe(table, cell, terms, thermal_state(table, cell, meta), meta) {}

AffinityProbe::AffinityProbe(const SpeciesTable &table, const CellState &cell, std::span<const Term> terms, const Thermal &thermal, const CellMeta *meta) {
	if (!thermal.defined || !(thermal.temperature > 0.0) || terms.size() > parts_.size()) {
		return;
	}
	for (const Term &t : terms) {
		if (!table.has(t.species) || !table.species(t.species).has_thermo) {
			return;
		}
		const SpeciesDef &def = table.species(t.species);
		Part &part = parts_[count_++];
		part.nu = static_cast<double>(t.nu);
		part.quanta = static_cast<double>(cell.amount[static_cast<size_t>(t.species.index)]);
		part.def = &def;
		part.hf = def.hf;
		part.cp = def.cp;
		part.s0 = def.s0;
		capacity_step_ += part.nu * def.cp * MOL_PER_QUANTUM;
		formation_step_ += part.nu * def.hf * MOL_PER_QUANTUM;
		if (def.phase != Phase::Gas && !def.wall) { // what it makes takes room from the gas (Stage 28), growing with temperature or not (Stage 67)
			(def.phase == Phase::Solid ? fixed_step_ : expanding_step_) += part.nu * def.molar_volume * MOL_PER_QUANTUM;
		}
		if (def.phase == Phase::Gas) {
			gas_step_ += part.nu;
		} else if (def.phase == Phase::Liquid) {
			liquid_step_ += part.nu;
		} else if (def.phase == Phase::Aqueous) {
			for (const IonCount &ion : def.ions) {
				if (ion.count <= 0) {
					break;
				}
				particles_step_[static_cast<size_t>(ion.ion)] += part.nu * static_cast<double>(ion.count);
			}
		}
	}
	const Mix mix = mix_of(table, cell, false); // the amounts: at() works out the excess at each extent
	gas_ = mix.gas;
	liquid_ = mix.liquid;
	const SolutionDef &solution = table.solution();
	std::copy_n(mix.particles.begin(), solution.ions.size(), particles_.begin());
	// (Stage 64) the particles the cell has or the reaction makes or uses, and the pairs among them: at() reads no other
	std::array<bool, MAX_PARTICLES> listed{};
	for (size_t i = 0; i < count_; ++i) {
		for (const IonCount &ion : parts_[i].def->ions) {
			if (ion.count <= 0) {
				break;
			}
			listed[static_cast<size_t>(ion.ion)] = true;
		}
	}
	for (size_t j = 0; j < solution.ions.size(); ++j) {
		if (listed[j] || particles_[j] != 0.0 || particles_step_[j] != 0.0) {
			ions_[ion_count_++] = static_cast<uint8_t>(j);
			listed[j] = true;
		}
	}
	all_pairs_ = solution.pairs.size() > pairs_.size();
	for (size_t k = 0; k < solution.pairs.size() && !all_pairs_; ++k) {
		const PairDef &p = solution.pairs[k];
		if (listed[static_cast<size_t>(p.cation)] && listed[static_cast<size_t>(p.anion)]) {
			pairs_[pair_count_++] = static_cast<uint16_t>(k);
		}
	}
	table_ = &table;
	temperature_ = thermal.temperature;
	capacity_ = thermal.heat_capacity;
	sealed_ = thermal.sealed && is_sealed(meta);
	if (sealed_) {
		const Volumes volumes = volumes_of(table, cell);
		for (size_t s = 0; s < cell.amount.size(); ++s) {
			const SpeciesDef &def = table.species(SpeciesId{ static_cast<int32_t>(s) });
			formation_ += static_cast<double>(cell.amount[s]) * MOL_PER_QUANTUM * def.hf;
			cp_ += static_cast<double>(cell.amount[s]) * MOL_PER_QUANTUM * def.cp;
		}
		energy_ = static_cast<double>(cell.energy) * JOULE_PER_QUANTUM;
		gas_mol_ = gas_mol(table, cell);
		volume_ = meta->volume;
		fixed_ = volumes.fixed;
		expanding_ = volumes.expanding;
	}
	defined_ = true;
}

bool AffinityProbe::at(double extent, double &a) const {
	if (!defined_) {
		return false;
	}
	// at fixed enthalpy, what the reaction takes as formation enthalpy it takes
	// from the heat: C (T - T0) falls by extent * formation_step
	double temperature = temperature_;
	double ratio = P_ATMOSPHERE / P_STANDARD;
	double over = 0.0; // Pa: a sealed cell's pressure off the room's
	if (sealed_) {
		// at fixed E (Sealed): what the extent makes, at the temperature that holds E with it
		const double gas = gas_mol_ + extent * gas_step_ * MOL_PER_QUANTUM;
		const Sealed model{ formation_ + extent * formation_step_, cp_ + extent * capacity_step_, gas > 0.0 ? gas : 0.0, volume_, fixed_ + extent * fixed_step_,
			expanding_ + extent * expanding_step_ };
		Sealed::State state;
		if (!(extent == 0.0 ? model.at(*table_, temperature_, state) : model.solve(*table_, energy_, temperature_, state)) || !(state.temperature > 0.0)) {
			return false;
		}
		temperature = state.temperature;
		ratio = state.pressure / P_STANDARD;
		over = state.pressure - P_ATMOSPHERE;
	} else if (extent != 0.0) {
		const double capacity = capacity_ + extent * capacity_step_;
		if (!(capacity > 0.0)) {
			return false;
		}
		temperature = T0 + (capacity_ * (temperature_ - T0) - extent * formation_step_) / capacity;
		if (!(temperature > 0.0)) {
			return false;
		}
	}
	const auto clamp = [](double v) { return v > 0.0 ? v : 0.0; };
	// (Stage 64) the listed particles alone: every other is nought before and after, and adds nought
	Mix mix(ions_.data(), ion_count_);
	mix.gas = clamp(gas_ + extent * gas_step_);
	mix.liquid = clamp(liquid_ + extent * liquid_step_);
	for (size_t k = 0; k < ion_count_; ++k) {
		const size_t j = ions_[k];
		mix.particles[j] = clamp(particles_[j] + extent * particles_step_[j]);
		mix.dissolved += mix.particles[j];
	}
	const Subset subset{ ions_.data(), ion_count_, all_pairs_ ? nullptr : pairs_.data(), pair_count_ };
	if (all_pairs_) {
		// (more pairs than a probe lists: every pair, each of an absent particle adding nought; their m read as nought)
		Mix full(table_->solution().ions.size());
		full.gas = mix.gas;
		full.liquid = mix.liquid;
		full.dissolved = mix.dissolved;
		for (size_t k = 0; k < ion_count_; ++k) {
			full.particles[ions_[k]] = mix.particles[ions_[k]];
		}
		mix = full;
		complete(*table_, mix);
	} else {
		complete(*table_, mix, &subset);
	}
	mix.pressure_ratio = ratio;
	const double ln_t = log(temperature / T0);
	double standard = 0.0;
	double mixing = 0.0;
	bool made_absent = false;
	bool consumed_absent = false;
	for (size_t i = 0; i < count_; ++i) {
		const Part &part = parts_[i];
		standard -= part.nu * (part.hf + part.cp * (temperature - T0) - temperature * (part.s0 + part.cp * ln_t));
		if (over != 0.0 && part.def->phase != Phase::Gas && !part.def->wall) {
			standard -= part.nu * molar_volume_at(*table_, *part.def, temperature) * over; // its volume at the cell's pressure (Stage 67)
		}
		double ln = 0.0;
		switch (log_activity(*part.def, part.quanta + part.nu * extent, mix, ln)) {
			case Presence::Present:
				mixing += part.nu * ln;
				break;
			case Presence::Absent:
				(part.nu > 0.0 ? made_absent : consumed_absent) = true;
				break;
			case Presence::Undefined:
				return false;
		}
	}
	a = combine(standard, mixing, temperature, made_absent, consumed_absent);
	return true;
}

} // namespace chem
