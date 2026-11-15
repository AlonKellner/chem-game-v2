#pragma once

#include "chem/species.hpp"
#include "chem/world.hpp"

// What a cell's conserved state implies (GAME_SPEC §4.1, §4.2, §4.6): its heat
// capacity, its temperature, its entropy. Derived, never stored.
namespace chem {

inline constexpr double T0 = 298.15; // K: the reference temperature of the species table
inline constexpr double GAS_CONSTANT = 8.314462618; // J/mol/K (CODATA 2018)
inline constexpr double P_ATMOSPHERE = 101325.0; // Pa: every cell's pressure in the demo (GAME_SPEC §6)
inline constexpr double P_STANDARD = 100000.0; // Pa: the species table's standard pressure (1 bar)

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

struct Fill {
	// false when the cell holds a condensed species (not a wall, not a solute)
	// whose molar volume is unknown
	bool defined = false;
	double gas = 0.0; // m^3: n_gas R T / P, at one atmosphere
	double condensed = 0.0; // m^3: sum of n_i v_i over its solids and liquids (walls take no room)
};

// The volume the cell's contents take at temperature T (GAME_SPEC §6). Solutes
// take no room of their own until Stage 6.
Fill fill_of(const SpeciesTable &table, const CellState &cell, double temperature);

// How far a rigid cell is from exactly full, in m^3: gas + condensed - volume.
// Positive: over-full, gas must leave. Negative: under-full, gas must enter.
// `defined` is false when the cell has no temperature or no defined fill.
double gas_excess(const SpeciesTable &table, const CellMeta &meta, const CellState &cell, bool &defined);

// The activity of a species in the cell (GAME_SPEC §4.6): for a gas, its
// partial pressure over the standard pressure, y_i * P / P°, with y_i its mole
// fraction among the cell's gases and P one atmosphere; 1 for a solid or a
// liquid. (Solutes: Stage 6; until then 1.) 0 for a gas that is absent.
double activity(const SpeciesTable &table, const CellState &cell, SpeciesId species);

// The entropy, in J/K, of the cell at temperature T > 0:
// sum of n_i (S0_i + Cp_i ln(T / T0) - R ln a_i), a_i the activity above (the
// gases' mixing and pressure terms).
double entropy(const SpeciesTable &table, const CellState &cell, double temperature);

// The affinity of a change in the cell at temperature T, in J per mole of
// extent: A = -sum of nu_i mu_i, with mu_i = h_i(T) - T s_i(T, a_i). Positive
// when the change is spontaneous as written, zero at equilibrium, negative
// when its reverse is spontaneous. +infinity when a gas it makes is absent,
// -infinity when a gas it consumes is absent, 0 when both. `defined` is false
// when a species it names has no thermodynamic data.
double affinity(const SpeciesTable &table, const CellState &cell, std::span<const Term> terms, double temperature, bool &defined);

} // namespace chem
