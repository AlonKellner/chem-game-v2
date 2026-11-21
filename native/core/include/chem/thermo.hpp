#pragma once

#include "chem/species.hpp"
#include "chem/world.hpp"

#include <array>
#include <cstdint>
#include <span>

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
// take their apparent molar volume (GAME_SPEC §10), or none if they have none.
Fill fill_of(const SpeciesTable &table, const CellState &cell, double temperature);

// How far a rigid cell is from exactly full, in m^3: gas + condensed - volume.
// Positive: over-full, gas must leave. Negative: under-full, gas must enter.
// `defined` is false when the cell has no temperature or no defined fill.
double gas_excess(const SpeciesTable &table, const CellMeta &meta, const CellState &cell, bool &defined);

// The diameter, in m, of one particle of a solid in the cell: its volume
// shared equally among its particles, as spheres. 0 when there is none.
double particle_diameter(const SpeciesTable &table, const CellState &cell, SpeciesId species);

// The solvent's viscosity at T, Pa s (GAME_SPEC §7): the table's, read between
// its points in the logarithm and held beyond its ends. 0 with no table.
double solvent_viscosity(const SpeciesTable &table, double temperature);

// A dissolved species' diffusion coefficient in the solvent at T, m^2/s
// (GAME_SPEC §7): its particles moving together, sum(nu) / sum(nu / D_i) over
// them (Nernst-Hartley, for a cation and an anion), each D_i measured at 25 C
// and scaled as T / viscosity(T). 0 when it is not dissolved or a particle of
// it has no coefficient: not known.
double diffusion(const SpeciesTable &table, SpeciesId species, double temperature);

// true when the cell holds a dissolved species and no liquid to hold it: not a
// state a solution can be in (GAME_SPEC §9).
bool dry_solute(const SpeciesTable &table, const CellState &cell);

// The most a change can advance (forward) or retreat (!forward) before a
// species it consumes runs out, in quanta of extent.
int64_t feasible_extent(const CellState &cell, std::span<const Term> terms, bool forward);

// A reaction's affinity along its own extent, in one cell at fixed enthalpy:
// what the affinity would be after `extent` quanta of it (negative: backwards).
// The step and the invariants both ask this, with the same arithmetic.
class AffinityProbe {
public:
	AffinityProbe(const SpeciesTable &table, const CellState &cell, std::span<const Term> terms);
	// the same, given the cell's thermal state (as thermal_state() gives it)
	AffinityProbe(const SpeciesTable &table, const CellState &cell, std::span<const Term> terms, const Thermal &thermal);
	// false when the cell has no temperature or a species named has no thermodynamic data
	bool defined() const { return defined_; }
	// the cell's temperature before any of the reaction, K
	double temperature() const { return temperature_; }
	// The affinity, J/mol, after `extent` quanta (see affinity() for its
	// infinities). false when the cell would then have no positive temperature.
	bool at(double extent, double &a) const;

private:
	struct Part {
		double nu = 0.0;
		double quanta = 0.0; // of the species, before
		const SpeciesDef *def = nullptr;
		double hf = 0.0, cp = 0.0, s0 = 0.0;
	};
	std::array<Part, MAX_TERMS> parts_{};
	size_t count_ = 0;
	bool defined_ = false;
	double temperature_ = 0.0; // before
	double capacity_ = 0.0; // J/K, before
	double capacity_step_ = 0.0; // J/K per quantum of extent
	double formation_step_ = 0.0; // J per quantum of extent
	// the cell's mixtures before, and what a quantum of extent adds to each
	double gas_ = 0.0, gas_step_ = 0.0; // quanta of gas
	double liquid_ = 0.0, liquid_step_ = 0.0; // quanta of liquid (the solvent)
	std::array<double, MAX_PARTICLES> particles_{}, particles_step_{}; // quanta of each particle dissolved
	const SpeciesTable *table_ = nullptr; // its solution model
};

// The activity of a species in the cell (GAME_SPEC §4.6). A gas: its partial
// pressure over the standard pressure, y_i * P / P°, y_i its fraction of the
// cell's gas, P one atmosphere. A solid: 1. A dissolved species: the product
// over its particles of (m_j gamma_j)^nu_j, m_j the particle's molality (mol
// per 55.51 mol of the cell's liquid: per kg of water) and gamma_j its
// activity coefficient (GAME_SPEC §10). A liquid: the solvent, ln a = ln x_i -
// phi (dissolved particles) / (liquid), which is 1 when nothing is dissolved
// in it. 0 for a gas or a solute that is absent.
double activity(const SpeciesTable &table, const CellState &cell, SpeciesId species);

// A dissolved species' molality: its amount per 55.51 mol of the cell's
// liquid (per kg of water). 0 when the cell has no liquid.
double molality(const SpeciesTable &table, const CellState &cell, SpeciesId species);

// The enthalpy, in quanta, that these amounts carry at temperature T: what a
// transfer of matter takes with it (GAME_SPEC §6).
Energy enthalpy_carried(const SpeciesTable &table, std::span<const SpeciesAmount> amounts, double temperature);

// The entropy, in J/K, of the cell at temperature T > 0:
// sum of n_i (S0_i + Cp_i ln(T / T0) - R ln a_i), a_i the activity above (the
// mixing terms of its gas and of its solution).
double entropy(const SpeciesTable &table, const CellState &cell, double temperature);

// The affinity of a change in the cell at temperature T, in J per mole of
// extent: A = -sum of nu_i mu_i, with mu_i = h_i(T) - T s_i(T, a_i). Positive
// when the change is spontaneous as written, zero at equilibrium, negative
// when its reverse is spontaneous. +infinity when a gas or a solute it makes
// is absent, -infinity when one it consumes is absent, 0 when both. `defined`
// is false when a species it names has no thermodynamic data, or is dissolved
// in a cell with no liquid.
double affinity(const SpeciesTable &table, const CellState &cell, std::span<const Term> terms, double temperature, bool &defined);

} // namespace chem
