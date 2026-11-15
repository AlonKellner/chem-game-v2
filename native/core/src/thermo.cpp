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

namespace {

// the cell's gas, in quanta
double gas_quanta(const SpeciesTable &table, const CellState &cell) {
	double total = 0.0;
	for (size_t s = 0; s < cell.amount.size(); ++s) {
		if (cell.amount[s] != 0 && table.species(SpeciesId{ static_cast<int32_t>(s) }).phase == Phase::Gas) {
			total += static_cast<double>(cell.amount[s]);
		}
	}
	return total;
}

double activity_in(const SpeciesDef &def, int64_t quanta, double gas) {
	if (def.phase != Phase::Gas) {
		return 1.0;
	}
	return quanta > 0 ? static_cast<double>(quanta) / gas * (P_ATMOSPHERE / P_STANDARD) : 0.0;
}

} // namespace

Fill fill_of(const SpeciesTable &table, const CellState &cell, double temperature) {
	Fill out;
	out.gas = gas_quanta(table, cell) * MOL_PER_QUANTUM * GAS_CONSTANT * temperature / P_ATMOSPHERE;
	for (size_t s = 0; s < cell.amount.size(); ++s) {
		if (cell.amount[s] == 0) {
			continue;
		}
		const SpeciesDef &def = table.species(SpeciesId{ static_cast<int32_t>(s) });
		if (def.wall || def.phase == Phase::Gas || def.phase == Phase::Aqueous) {
			continue;
		}
		if (!(def.molar_volume > 0.0)) {
			return out;
		}
		out.condensed += static_cast<double>(cell.amount[s]) * MOL_PER_QUANTUM * def.molar_volume;
	}
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
		part.gas = def.phase == Phase::Gas;
		part.hf = def.hf;
		part.cp = def.cp;
		part.s0 = def.s0;
		capacity_step_ += part.nu * def.cp * MOL_PER_QUANTUM;
		formation_step_ += part.nu * def.hf * MOL_PER_QUANTUM;
		gas_step_ += part.gas ? part.nu : 0.0;
	}
	temperature_ = thermal.temperature;
	capacity_ = thermal.heat_capacity;
	gas_ = gas_quanta(table, cell);
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
	const double ln = log(temperature / T0);
	const double gas = gas_ + extent * gas_step_;
	double standard = 0.0;
	double mixing = 0.0;
	bool made_absent = false;
	bool consumed_absent = false;
	for (size_t i = 0; i < count_; ++i) {
		const Part &part = parts_[i];
		standard -= part.nu * (part.hf + part.cp * (temperature - T0) - temperature * (part.s0 + part.cp * ln));
		if (part.gas) {
			const double quanta = part.quanta + part.nu * extent;
			if (quanta > 0.0 && gas > 0.0) {
				mixing += part.nu * log(quanta / gas * (P_ATMOSPHERE / P_STANDARD));
			} else if (part.nu > 0.0) {
				made_absent = true;
			} else {
				consumed_absent = true;
			}
		}
	}
	if (made_absent || consumed_absent) {
		constexpr double infinity = std::numeric_limits<double>::infinity();
		a = made_absent && consumed_absent ? 0.0 : made_absent ? infinity : -infinity;
	} else {
		a = standard - GAS_CONSTANT * temperature * mixing;
	}
	return true;
}

double activity(const SpeciesTable &table, const CellState &cell, SpeciesId species) {
	return activity_in(table.species(species), cell.amount[static_cast<size_t>(species.index)], gas_quanta(table, cell));
}

double entropy(const SpeciesTable &table, const CellState &cell, double temperature) {
	const double ln = log(temperature / T0);
	const double gas = gas_quanta(table, cell);
	double s_total = 0.0;
	for (size_t s = 0; s < cell.amount.size(); ++s) {
		if (cell.amount[s] <= 0) {
			continue;
		}
		const SpeciesDef &def = table.species(SpeciesId{ static_cast<int32_t>(s) });
		double molar = def.s0 + def.cp * ln;
		if (def.phase == Phase::Gas) {
			molar -= GAS_CONSTANT * log(activity_in(def, cell.amount[s], gas));
		}
		s_total += static_cast<double>(cell.amount[s]) * MOL_PER_QUANTUM * molar;
	}
	return s_total;
}

double affinity(const SpeciesTable &table, const CellState &cell, std::span<const Term> terms, double temperature, bool &defined) {
	defined = false;
	const double ln = log(temperature / T0);
	const double gas = gas_quanta(table, cell);
	double standard = 0.0; // -sum of nu_i (h_i - T s_i) at unit activity
	double mixing = 0.0; // sum of nu_i ln a_i over the gases present
	bool made_absent = false;
	bool consumed_absent = false;
	for (const Term &t : terms) {
		if (!table.has(t.species)) {
			return 0.0;
		}
		const SpeciesDef &def = table.species(t.species);
		if (!def.has_thermo) {
			return 0.0;
		}
		const double mu = def.hf + def.cp * (temperature - T0) - temperature * (def.s0 + def.cp * ln);
		standard -= static_cast<double>(t.nu) * mu;
		if (def.phase == Phase::Gas) {
			const int64_t quanta = cell.amount[static_cast<size_t>(t.species.index)];
			if (quanta > 0) {
				mixing += static_cast<double>(t.nu) * log(activity_in(def, quanta, gas));
			} else if (t.nu > 0) {
				made_absent = true;
			} else {
				consumed_absent = true;
			}
		}
	}
	defined = true;
	if (made_absent || consumed_absent) {
		constexpr double infinity = std::numeric_limits<double>::infinity();
		return made_absent && consumed_absent ? 0.0 : made_absent ? infinity : -infinity;
	}
	return standard - GAS_CONSTANT * temperature * mixing;
}

} // namespace chem
