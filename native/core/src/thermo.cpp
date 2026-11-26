#include "chem/thermo.hpp"

#include <algorithm>

#include "chem/math.hpp"

#include <cmath>
#include <cstdint>
#include <limits>

namespace chem {

namespace {

// A sealed cell's free volume, m^3: what its solids and liquids leave (false
// when a condensed species' molar volume is unknown)
bool free_volume(const SpeciesTable &table, const CellState &cell, const CellMeta &meta, double &free) {
	const Fill fill = fill_of(table, cell, T0);
	free = meta.volume - fill.condensed;
	return fill.defined;
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
		// E = H(T) - n_gas R T + P_atm V_free: linear in T
		double free = 0.0;
		if (!free_volume(table, cell, *meta, free)) {
			return out;
		}
		const double gas = gas_mol(table, cell);
		const double cv = capacity - gas * GAS_CONSTANT;
		if (!(free > 0.0) || !(cv > 0.0)) {
			return out; // no room for its gas: no pressure (V3)
		}
		out.sealed = true;
		out.free_volume = free;
		out.heat_capacity = cv;
		out.temperature = (static_cast<double>(cell.energy) * JOULE_PER_QUANTUM - P_ATMOSPHERE * free - formation + capacity * T0) / cv;
		out.pressure = gas * GAS_CONSTANT * out.temperature / free;
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
void excess_of(const SolutionDef &model, Mix &mix) {
	const size_t n = model.ions.size();
	std::array<double, MAX_PARTICLES> m; // (Stage 64: the table's own particles only are set and read)
	double ionic = 0.0, z_sum = 0.0;
	for (size_t j = 0; j < n; ++j) {
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
	std::fill_n(d.begin(), n, 0.0);
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
	for (const PairDef &p : model.pairs) {
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
	for (size_t j = 0; j < n; ++j) {
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
void complete(const SpeciesTable &table, Mix &mix) {
	if (mix.liquid > 0.0 && mix.dissolved > 0.0) {
		excess_of(table.solution(), mix);
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
	double gas = 0.0;
	for (size_t s = 0; s < cell.amount.size(); ++s) {
		if (cell.amount[s] == 0) {
			continue;
		}
		const SpeciesDef &def = table.species(SpeciesId{ static_cast<int32_t>(s) });
		if (def.wall) {
			continue;
		}
		if (def.phase == Phase::Gas) {
			gas += static_cast<double>(cell.amount[s]);
			continue;
		}
		if (def.phase == Phase::Aqueous) { // its apparent molar volume, if it has one (GAME_SPEC §10)
			out.condensed += static_cast<double>(cell.amount[s]) * MOL_PER_QUANTUM * def.molar_volume;
			continue;
		}
		if (!(def.molar_volume > 0.0)) {
			return out;
		}
		out.condensed += static_cast<double>(cell.amount[s]) * MOL_PER_QUANTUM * def.molar_volume;
	}
	out.gas = is_sealed(meta) ? meta->volume - out.condensed : gas * MOL_PER_QUANTUM * GAS_CONSTANT * temperature / P_ATMOSPHERE;
	out.defined = true;
	return out;
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
	const double at25 = solvent_at(table, 298.15).rho, now = solvent_at(table, temperature).rho;
	const double expansion = at25 > 0.0 && now > 0.0 ? at25 / now : 1.0;
	density = grams * 1e-3 / (volume * expansion);
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
	const SolutionDef &solution = table.solution();
	double count = 0.0, resistance = 0.0;
	for (const IonCount &ion : def.ions) {
		if (ion.count <= 0) {
			break;
		}
		const double d = solution.ions[static_cast<size_t>(ion.ion)].diffusion;
		if (!(d > 0.0)) {
			return 0.0;
		}
		count += ion.count;
		resistance += ion.count / d;
	}
	if (!(count > 0.0)) {
		return 0.0;
	}
	// Stokes-Einstein: D goes as T / viscosity, from its value at 25 C
	const double at = solvent_viscosity(table, temperature), at25 = solvent_viscosity(table, 298.15);
	const double scale = at > 0.0 && at25 > 0.0 ? temperature / 298.15 * at25 / at : 1.0;
	return count / resistance * scale;
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

// a sealed cell's pressure over the standard's, at temperature T (its gas of `gas` quanta)
double sealed_ratio(const SpeciesTable &table, const CellState &cell, const CellMeta &meta, double temperature, double gas) {
	double free = 0.0;
	if (!free_volume(table, cell, meta, free) || !(free > 0.0)) {
		return INFINITE;
	}
	return gas * MOL_PER_QUANTUM * GAS_CONSTANT * temperature / (free * P_STANDARD);
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

bool ph(const SpeciesTable &table, const CellState &cell, double temperature, double &out) {
	const double kw = water_ionisation(table, temperature);
	const Mix mix = mix_of(table, cell);
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
	const int h = index_of("H+1"), oh = index_of("OH-1");
	const auto molal = [&](int j) { return j >= 0 ? mix.particles[static_cast<size_t>(j)] / mix.liquid * SOLVENT_MOL_PER_UNIT : 0.0; };
	const auto gamma = [&](int j) { return j >= 0 ? exp(mix.ln_gamma[static_cast<size_t>(j)]) : 1.0; };
	// the solvent's activity: the liquid is water, ln a = -dissolved / liquid + its excess (as log_activity has it)
	const double water = exp(-mix.dissolved / mix.liquid + mix.solvent_excess);
	const double k = kw * water / (gamma(h) * gamma(oh));
	const double d = molal(h) - molal(oh);
	// beyond the activity model's range (Pitzer and Mayorga's fits hold to 6 mol/kg) a solution has no pH the game
	// can read: a base or an acid boiled nearly dry, with no solid for it to come out as (Stage 44)
	if (d > PH_MOST_EXCESS || d < -PH_MOST_EXCESS) {
		return false;
	}
	// the free hydrogen ions, without the difference of near equals: m_H = d/2 + sqrt(d^2/4 + k)
	const double root = std::sqrt(0.25 * d * d + k);
	const double m_h = d >= 0.0 ? 0.5 * d + root : k / (root - 0.5 * d);
	out = -log(gamma(h) * m_h) / log(10.0);
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
	for (const Term &t : terms) {
		if (!table.has(t.species) || !table.species(t.species).has_thermo) {
			return 0.0;
		}
		const SpeciesDef &def = table.species(t.species);
		standard -= static_cast<double>(t.nu) * (def.hf + def.cp * (temperature - T0) - temperature * (def.s0 + def.cp * ln_t));
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
		if (def.phase != Phase::Gas && !def.wall) {
			free_step_ -= part.nu * def.molar_volume * MOL_PER_QUANTUM; // what it makes takes room from the gas (Stage 28)
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
	std::copy_n(mix.particles.begin(), table.solution().ions.size(), particles_.begin());
	table_ = &table;
	temperature_ = thermal.temperature;
	capacity_ = thermal.heat_capacity;
	sealed_ = thermal.sealed && is_sealed(meta);
	free_ = thermal.free_volume;
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
	if (sealed_) {
		// at fixed E = H(T) - n_gas R T + P_atm V_free: Cv(x) T(x) = Cv T - x (P_atm dV + dHf - dCp T0)
		const double cv = capacity_ + extent * (capacity_step_ - gas_step_ * MOL_PER_QUANTUM * GAS_CONSTANT);
		const double free = free_ + extent * free_step_;
		if (!(cv > 0.0) || !(free > 0.0)) {
			return false;
		}
		if (extent != 0.0) {
			temperature = (capacity_ * temperature_ - extent * (P_ATMOSPHERE * free_step_ + formation_step_ - capacity_step_ * T0)) / cv;
			if (!(temperature > 0.0)) {
				return false;
			}
		}
		const double gas = gas_ + extent * gas_step_;
		ratio = (gas > 0.0 ? gas : 0.0) * MOL_PER_QUANTUM * GAS_CONSTANT * temperature / (free * P_STANDARD);
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
	Mix mix(table_->solution().ions.size());
	mix.gas = clamp(gas_ + extent * gas_step_);
	mix.liquid = clamp(liquid_ + extent * liquid_step_);
	for (size_t j = 0, n = table_->solution().ions.size(); j < n; ++j) {
		mix.particles[j] = clamp(particles_[j] + extent * particles_step_[j]);
		mix.dissolved += mix.particles[j];
	}
	complete(*table_, mix);
	mix.pressure_ratio = ratio;
	const double ln_t = log(temperature / T0);
	double standard = 0.0;
	double mixing = 0.0;
	bool made_absent = false;
	bool consumed_absent = false;
	for (size_t i = 0; i < count_; ++i) {
		const Part &part = parts_[i];
		standard -= part.nu * (part.hf + part.cp * (temperature - T0) - temperature * (part.s0 + part.cp * ln_t));
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
