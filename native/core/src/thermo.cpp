#include "chem/thermo.hpp"

#include "chem/math.hpp"

#include <cstdint>
#include <limits>

namespace chem {

Thermal thermal_state(const SpeciesTable &table, const CellState &cell) {
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

// What a cell's mixtures hold, in quanta: its gas; its liquid (the solvent)
// and what is dissolved in it. Walls are in neither.
struct Mix {
	double gas = 0.0;
	double liquid = 0.0;
	double solute = 0.0;
};

// A solute's activity is its amount per 55.51 mol of the cell's liquid: its
// molality when the liquid is water (18.015 g/mol). Counting the solvent in
// moles, not kilograms, is what keeps a mixed solvent consistent with its
// solutes (Gibbs-Duhem).
constexpr double SOLVENT_MOL_PER_UNIT = 1.0 / 0.018015;

Mix mix_of(const SpeciesTable &table, const CellState &cell) {
	Mix mix;
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
			mix.solute += quanta;
		}
	}
	return mix;
}

enum class Presence { Present, Absent, Undefined };

// The logarithm of a species' activity (GAME_SPEC §4.6), given how much of it
// there is and the cell's mixtures.
//   gas:    y_i P / P°, y_i its fraction of the cell's gas. Absent when there is none.
//   liquid: ln a = ln x_i - (dissolved) / (liquid), x_i its fraction of the
//           cell's liquids: the solvent of an ideal-dilute solution. This form,
//           not ln of the mole fraction, is the one consistent with the
//           solutes' (Gibbs-Duhem), so the entropy is a state function. Where
//           the cell has no liquid at all, one would form pure, at activity 1.
//           Absent from a liquid that is there, its activity is 0: liquids mix.
//   solute: its molality (mol per 55.51 mol of the cell's liquid: per kg of
//           water). Absent when there is none; undefined without a solvent.
//   solid:  1.
Presence log_activity(Phase phase, double quanta, const Mix &mix, double &ln) {
	ln = 0.0;
	switch (phase) {
		case Phase::Gas:
			if (!(quanta > 0.0) || !(mix.gas > 0.0)) {
				return Presence::Absent;
			}
			ln = log(quanta / mix.gas * (P_ATMOSPHERE / P_STANDARD));
			return Presence::Present;
		case Phase::Liquid:
			if (!(mix.liquid > 0.0)) {
				ln = mix.solute > 0.0 ? -INFINITE : 0.0;
				return Presence::Present;
			}
			if (!(quanta > 0.0)) {
				return Presence::Absent; // it would mix into the liquid that is there
			}
			ln = (quanta < mix.liquid ? log(quanta / mix.liquid) : 0.0) - mix.solute / mix.liquid;
			return Presence::Present;
		case Phase::Aqueous:
			if (!(mix.liquid > 0.0)) {
				return Presence::Undefined;
			}
			if (!(quanta > 0.0)) {
				return Presence::Absent;
			}
			ln = log(quanta / mix.liquid * SOLVENT_MOL_PER_UNIT);
			return Presence::Present;
		case Phase::Solid:
			return Presence::Present;
	}
	return Presence::Present;
}

} // namespace

Fill fill_of(const SpeciesTable &table, const CellState &cell, double temperature) {
	Fill out;
	double gas = 0.0;
	for (size_t s = 0; s < cell.amount.size(); ++s) {
		if (cell.amount[s] == 0) {
			continue;
		}
		const SpeciesDef &def = table.species(SpeciesId{ static_cast<int32_t>(s) });
		if (def.wall || def.phase == Phase::Aqueous) {
			continue;
		}
		if (def.phase == Phase::Gas) {
			gas += static_cast<double>(cell.amount[s]);
			continue;
		}
		if (!(def.molar_volume > 0.0)) {
			return out;
		}
		out.condensed += static_cast<double>(cell.amount[s]) * MOL_PER_QUANTUM * def.molar_volume;
	}
	out.gas = gas * MOL_PER_QUANTUM * GAS_CONSTANT * temperature / P_ATMOSPHERE;
	out.defined = true;
	return out;
}

double gas_excess(const SpeciesTable &table, const CellMeta &meta, const CellState &cell, bool &defined) {
	defined = false;
	const Thermal thermal = thermal_state(table, cell);
	if (!thermal.defined || !(thermal.temperature > 0.0)) {
		return 0.0;
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

bool dry_solute(const SpeciesTable &table, const CellState &cell) {
	const Mix mix = mix_of(table, cell);
	return mix.solute > 0.0 && !(mix.liquid > 0.0);
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

double activity(const SpeciesTable &table, const CellState &cell, SpeciesId species) {
	double ln = 0.0;
	const Presence presence = log_activity(table.species(species).phase, static_cast<double>(cell.amount[static_cast<size_t>(species.index)]), mix_of(table, cell), ln);
	return presence == Presence::Present ? exp(ln) : 0.0;
}

double entropy(const SpeciesTable &table, const CellState &cell, double temperature) {
	const double ln_t = log(temperature / T0);
	const Mix mix = mix_of(table, cell);
	double total = 0.0;
	for (size_t s = 0; s < cell.amount.size(); ++s) {
		if (cell.amount[s] <= 0) {
			continue;
		}
		const SpeciesDef &def = table.species(SpeciesId{ static_cast<int32_t>(s) });
		double molar = def.s0 + def.cp * ln_t;
		double ln = 0.0;
		if (!def.wall && log_activity(def.phase, static_cast<double>(cell.amount[s]), mix, ln) == Presence::Present) {
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

double affinity(const SpeciesTable &table, const CellState &cell, std::span<const Term> terms, double temperature, bool &defined) {
	defined = false;
	const double ln_t = log(temperature / T0);
	const Mix mix = mix_of(table, cell);
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
		switch (log_activity(def.phase, static_cast<double>(cell.amount[static_cast<size_t>(t.species.index)]), mix, ln)) {
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

AffinityProbe::AffinityProbe(const SpeciesTable &table, const CellState &cell, std::span<const Term> terms) {
	const Thermal thermal = thermal_state(table, cell);
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
		part.phase = def.phase;
		part.hf = def.hf;
		part.cp = def.cp;
		part.s0 = def.s0;
		capacity_step_ += part.nu * def.cp * MOL_PER_QUANTUM;
		formation_step_ += part.nu * def.hf * MOL_PER_QUANTUM;
		if (def.phase == Phase::Gas) {
			gas_step_ += part.nu;
		} else if (def.phase == Phase::Liquid) {
			liquid_step_ += part.nu;
		} else if (def.phase == Phase::Aqueous) {
			solute_step_ += part.nu;
		}
	}
	const Mix mix = mix_of(table, cell);
	gas_ = mix.gas;
	liquid_ = mix.liquid;
	solute_ = mix.solute;
	temperature_ = thermal.temperature;
	capacity_ = thermal.heat_capacity;
	defined_ = true;
}

bool AffinityProbe::at(double extent, double &a) const {
	if (!defined_) {
		return false;
	}
	// at fixed enthalpy, what the reaction takes as formation enthalpy it takes
	// from the heat: C (T - T0) falls by extent * formation_step
	double temperature = temperature_;
	if (extent != 0.0) {
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
	Mix mix;
	mix.gas = clamp(gas_ + extent * gas_step_);
	mix.liquid = clamp(liquid_ + extent * liquid_step_);
	mix.solute = clamp(solute_ + extent * solute_step_);
	const double ln_t = log(temperature / T0);
	double standard = 0.0;
	double mixing = 0.0;
	bool made_absent = false;
	bool consumed_absent = false;
	for (size_t i = 0; i < count_; ++i) {
		const Part &part = parts_[i];
		standard -= part.nu * (part.hf + part.cp * (temperature - T0) - temperature * (part.s0 + part.cp * ln_t));
		double ln = 0.0;
		switch (log_activity(part.phase, part.quanta + part.nu * extent, mix, ln)) {
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
