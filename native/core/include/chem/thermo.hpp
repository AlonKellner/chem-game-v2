#pragma once

#include "chem/species.hpp"
#include "chem/world.hpp"

// What a cell's conserved state implies (GAME_SPEC §4.1, §4.2, §4.6): its heat
// capacity, its temperature, its entropy. Derived, never stored.
namespace chem {

inline constexpr double T0 = 298.15; // K: the reference temperature of the species table
inline constexpr double GAS_CONSTANT = 8.314462618; // J/mol/K (CODATA 2018)

struct Thermal {
	// false when the cell holds a species without thermodynamic data, or its
	// heat capacity is not positive: it then has no temperature
	bool defined = false;
	double heat_capacity = 0.0; // J/K: sum of n_i Cp_i
	double temperature = 0.0; // K: T0 + (H - sum of n_i Hf_i) / heat capacity
};

Thermal thermal_state(const SpeciesTable &table, const CellState &cell);

// The enthalpy, in J, of these amounts at temperature T: sum of n_i (Hf_i + Cp_i (T - T0)).
// `ok` is false when a species present has no thermodynamic data.
double enthalpy_at(const SpeciesTable &table, const CellState &cell, double temperature, bool &ok);

// The entropy, in J/K, of the cell at temperature T > 0: sum of n_i (S0_i + Cp_i ln(T / T0)).
// Stage 2: the thermal part alone (the mixing terms of §4.6 enter with the
// phases that mix, Stages 4-6).
double entropy(const SpeciesTable &table, const CellState &cell, double temperature);

} // namespace chem
