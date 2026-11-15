#include "chem/thermo.hpp"

#include "chem/math.hpp"

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
