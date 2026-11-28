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
inline constexpr double P_ATMOSPHERE = 101325.0; // Pa: the room's, and every cell's open to it (GAME_SPEC §6)
inline constexpr double P_STANDARD = 100000.0; // Pa: the species table's standard pressure (1 bar)

// A cell's thermal state. Open (a cell with a gas path to an elastic one, at
// one atmosphere), its energy is its enthalpy H. Sealed (GAME_SPEC §6, Stage
// 28: a rigid cell with no gas path to an elastic one), its energy is
// E = U + P_atm V, its internal energy and the room's work on its volume: at
// one atmosphere and full that is its enthalpy, so a cell sealed or opened
// keeps its temperature there. With the condensed phases incompressible and
// the gas ideal, E = H(T) - n_gas R T + P_atm V_free, V_free the volume its
// solids and liquids leave, and its pressure is n_gas R T / V_free. Stage 67:
// its liquids grow with temperature (expansion()), their Gibbs energy
// G0(T) + V_c(T) (P - P_atm), so E gains - T V_c'(T) (P - P_atm), its entropy
// - V_c'(T) (P - P_atm), each condensed species' potential v_i(T) (P - P_atm);
// its temperature is then E's root (Newton's), V_free at it.
struct Thermal {
	// false when the cell holds a species without thermodynamic data, or its
	// heat capacity is not positive, or it is sealed and its solids and
	// liquids leave no room for its gas: it then has no temperature
	bool defined = false;
	// J/K: what heat raises its temperature by, dE/dT: sum of n_i Cp_i; a
	// sealed cell's at constant volume, less n_gas R
	double heat_capacity = 0.0;
	double temperature = 0.0; // K: T0 + (H - sum of n_i Hf_i) / heat capacity; sealed, from E as above
	double pressure = P_ATMOSPHERE; // Pa
	bool sealed = false;
	double free_volume = 0.0; // m^3: a sealed cell's V_free
};

// (Stage 75) What a cell's mixtures hold, in quanta (GAME_SPEC §4.6): its gas, its liquid (the solvent) and each
// particle dissolved in it, each summed over its species in order. Of its matter alone, so kept while only energy
// moves (World::matter_revision); what is read from them is what is read from the cell.
struct CellMixtures {
	double gas = 0.0, liquid = 0.0;
	std::array<double, MAX_PARTICLES> particles{};
	// (Stage 81) the particles it has, ascending, and its ionic strength: what a probe and stands() read, once a matter
	// change, not once a reaction
	std::array<uint8_t, MAX_PARTICLES> present{};
	size_t present_count = 0;
	double ionic = 0.0;
};
CellMixtures cell_mixtures(const SpeciesTable &table, const CellState &cell);

// `meta` is the cell's (null: open). A reservoir's is not asked for.
Thermal thermal_state(const SpeciesTable &table, const CellState &cell, const CellMeta *meta = nullptr);
// (Stage 75) What a cell's thermal state takes of its matter alone: sum of n_i Hf_i and of n_i Cp_i (J, J/K), each
// species in order; `known` false when it holds a species without thermodynamic data. The same for any energy, so
// kept while only energy moves (World::matter_revision); thermal_state from them is the value thermal_state gives.
struct MatterSums {
	bool known = true;
	double formation = 0.0, capacity = 0.0;
};
MatterSums matter_sums(const SpeciesTable &table, const CellState &cell);
Thermal thermal_state(const SpeciesTable &table, const CellState &cell, const CellMeta *meta, const MatterSums &sums);

// The enthalpy, in J, of these amounts at temperature T: sum of n_i (Hf_i + Cp_i (T - T0)).
// `ok` is false when a species present has no thermodynamic data.
double enthalpy_at(const SpeciesTable &table, const CellState &cell, double temperature, bool &ok);

struct Fill {
	// false when the cell holds a condensed species (not a wall, not a solute)
	// whose molar volume is unknown
	bool defined = false;
	double gas = 0.0; // m^3: n_gas R T / P, at one atmosphere; a sealed cell's, the room its solids and liquids leave
	double condensed = 0.0; // m^3: sum of n_i v_i over its solids and liquids (walls take no room)
};

// The volume the cell's contents take at temperature T (GAME_SPEC §6). Solutes
// take their apparent molar volume (GAME_SPEC §10), or none if they have none.
// Liquids and solutes grow with the solvent from 25 C (Stage 67: expansion());
// solids keep the table's.
Fill fill_of(const SpeciesTable &table, const CellState &cell, double temperature, const CellMeta *meta = nullptr);

// How far a rigid cell is from exactly full, in m^3: gas + condensed - volume.
// Positive: over-full, gas must leave. Negative: under-full, gas must enter.
// `defined` is false when the cell has no temperature or no defined fill. A
// sealed cell is full by definition: 0.
double gas_excess(const SpeciesTable &table, const CellMeta &meta, const CellState &cell, bool &defined);

// The diameter, in m, of one particle of a solid in the cell: its volume
// shared equally among its particles, as spheres. 0 when there is none.
double particle_diameter(const SpeciesTable &table, const CellState &cell, SpeciesId species);

// The solvent's viscosity at T, Pa s (GAME_SPEC §7): the table's, read between
// its points in the logarithm and held beyond its ends. 0 with no table.
double solvent_viscosity(const SpeciesTable &table, double temperature);
// (Stage 79) a gas's viscosity at T, Pa s (air's at 1 atm, GAME_SPEC §6): the table's, read between its rows as the
// solvent's is, held beyond them; 0 where none is known
double gas_viscosity(const SpeciesTable &table, double temperature);

// The solvent's properties at a temperature (Stage 47: saturated liquid water, data/solutions.json's solvent), read
// between rows, held beyond the ends; all 0 where the table has none.
SolventPoint solvent_at(const SpeciesTable &table, double temperature);

// Stage 67 (GAME_SPEC §6): the solvent's density at T at one atmosphere, kg/m^3 (Kell's form, data/solutions.json's
// density; beyond its range its value and slope at the end), and its first and second derivatives by T; 0 with none
double solvent_density(const SpeciesTable &table, double temperature, double *slope = nullptr, double *curve = nullptr);
// Stage 88 (GAME_SPEC §6, Liquids that give): the solvent's isothermal compressibility at T at one atmosphere, 1/Pa
// (Kell's form, data/solutions.json's compressibility; beyond its range its value and slope at the end), and its first
// and second derivatives by T; 0 with none (a sealed cell's liquids then do not give)
double compressibility(const SpeciesTable &table, double temperature, double *slope = nullptr, double *curve = nullptr);
// What a liquid's and a solute's volume at T is of the table's (at 25 C): rho(25 C) / rho(T), and its derivatives by
// T (1/K, 1/K^2); 1 where the solvent's density is not known.
double expansion(const SpeciesTable &table, double temperature, double *slope = nullptr, double *curve = nullptr);
// A species' molar volume at T, m^3/mol: a liquid's or a solute's the table's times the expansion, a solid's the table's.
double molar_volume_at(const SpeciesTable &table, const SpeciesDef &def, double temperature);
// The energy, in J, the cell's contents hold at temperature T: open, their enthalpy; sealed (`meta`), E as Thermal has
// it. false where it has none (a species without thermodynamic data, or sealed with no room for its gas).
bool energy_at(const SpeciesTable &table, const CellState &cell, const CellMeta *meta, double temperature, double &joule);

// A cell's liquid's density at a temperature, kg/m^3 (Stage 47: what floats on what in a column, GAME_SPEC §6): the
// mass of its liquid and of what is dissolved in it over their volume, the table's molar volumes (at 25 C) scaled by
// the solvent's own expansion from 25 C (Stage 67: expansion()). false, and `density` left, where it holds no liquid.
bool liquid_density(const SpeciesTable &table, const CellState &cell, double temperature, double &density);

// A dissolved species' diffusion coefficient in the solvent at T, m^2/s
// (GAME_SPEC §7): its particles moving together, sum(nu) / sum(nu / D_i) over
// them (Nernst-Hartley, for a cation and an anion), each D_i measured at 25 C
// and scaled as T / viscosity(T). 0 when it is not dissolved or a particle of
// it has no coefficient: not known. (Stage 65) Each D_i as T / viscosity(T)^p, Walden's rule with the ion's own
// exponent (1, Stokes and Einstein, for a neutral solute).
double diffusion(const SpeciesTable &table, SpeciesId species, double temperature);
// One particle's (the table's solution's `ions`), m^2/s at T; 0 where it has none.
double ion_diffusion(const SpeciesTable &table, size_t ion, double temperature);

// A solution's conductivity (GAME_SPEC §10, Stage 65), S/m, at temperature T: sum c_i lambda_i over its ions, each
// lambda_i Nernst and Einstein's z^2 F^2 D_i / (R T) lowered by Debye, Hückel and Onsager's relaxation and
// electrophoresis over 1 + B a sqrt(I) (Robinson and Stokes), c_i per m^3 of the liquid and I its ionic strength in
// mol/L; hydrogen and hydroxide ions at their exact balance with water, as the pH reads them. `limit`, if given:
// Kohlrausch's, sum c_i lambda0_i, what no atmosphere lowers. false, and both left, where the cell holds no liquid or
// the table no conductivity (no dielectric constant or no ions' size).
bool conductivity(const SpeciesTable &table, const CellState &cell, double temperature, double &kappa, double *limit = nullptr);
// The solvent's dielectric constant at T (Stage 65), read between the table's rows, held beyond its ends; 0: none.
double solvent_dielectric(const SpeciesTable &table, double temperature);
// Debye, Hückel and Onsager's constants for a 1:1 electrolyte at T (Stage 65): B1 (relaxation, (L/mol)^1/2), B2
// (electrophoresis, S m^2/mol (L/mol)^1/2) and B (the ions' size term, 1/m (L/mol)^1/2); false without a dielectric
// constant or a viscosity.
bool onsager_constants(const SpeciesTable &table, double temperature, double &b1, double &b2, double &b);
// (Stage 57) a cell's liquid's density from its liquid's own mass and volume, kg/m^3 (0 with no liquid); the power its
// stirring dissipates for each kg of its liquid and what is dissolved in it, W/kg
double liquid_density(const SpeciesTable &table, const CellState &cell);
double dissipation_per_kg(const SpeciesTable &table, const CellState &cell, double watts);
// How fast a gas diffuses through the room's air at 1 atm (GAME_SPEC §6, Stage 50), m^2/s: its coefficient at
// 298.15 K times (T / 298.15)^n. 0 for a species that is not a gas, or has none.
double gas_diffusion(const SpeciesTable &table, SpeciesId species, double temperature);

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
	// At fixed energy: an open cell's enthalpy; a sealed cell's E, at its
	// volume, its gas's pressure following what the extent makes and frees.
	AffinityProbe(const SpeciesTable &table, const CellState &cell, std::span<const Term> terms, const CellMeta *meta = nullptr);
	// the same, given the cell's thermal state (as thermal_state() gives it)
	AffinityProbe(const SpeciesTable &table, const CellState &cell, std::span<const Term> terms, const Thermal &thermal, const CellMeta *meta = nullptr);
	// (Stage 75) the same, the cell's mixtures given (cell_mixtures: kept by its matter revision)
	AffinityProbe(const SpeciesTable &table, const CellState &cell, std::span<const Term> terms, const Thermal &thermal, const CellMeta *meta, const CellMixtures &mixtures);
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
	// (Stage 81: only the table's own particles are set and read, as a Mix's: a probe is built tens of millions of times a
	// play, and filling all MAX_PARTICLES of each cost a fifth of a step)
	std::array<double, MAX_PARTICLES> particles_, particles_step_; // quanta of each particle dissolved
	// (Stage 64) the particles it has or the reaction touches, ascending, and the solution's pairs among them: at() reads
	// these alone (more pairs than fit: every pair)
	std::array<uint8_t, MAX_PARTICLES> ions_;
	size_t ion_count_ = 0;
	std::array<uint16_t, 64> pairs_{};
	size_t pair_count_ = 0;
	bool all_pairs_ = false;
	const SpeciesTable *table_ = nullptr; // its solution model
	// sealed (Stage 28; Stage 67, its solids and liquids growing with temperature): its energy (J), its sum of n_i Hf_i
	// (J) and of n_i Cp_i (J/K), its gas (mol), its volume, and its solids' and its liquids' (at 25 C) volumes (m^3);
	// what a quantum of extent adds to the last two
	bool sealed_ = false;
	// (Stage 88) a sealed vessel's layer, at its vessel's pressure over the room's: its enthalpy there stands
	bool pressed_ = false;
	double over_ = 0.0;
	double energy_ = 0.0, formation_ = 0.0, cp_ = 0.0, gas_mol_ = 0.0, volume_ = 0.0, fixed_ = 0.0, expanding_ = 0.0;
	double fixed_step_ = 0.0, expanding_step_ = 0.0;
};

// The activity of a species in the cell (GAME_SPEC §4.6). A gas: its partial
// pressure over the standard pressure, y_i * P / P°, y_i its fraction of the
// cell's gas, P the cell's pressure (one atmosphere, or a sealed cell's). A solid: 1. A dissolved species: the product
// over its particles of (m_j gamma_j)^nu_j, m_j the particle's molality (mol
// per 55.51 mol of the cell's liquid: per kg of water) and gamma_j its
// activity coefficient (GAME_SPEC §10). A liquid: the solvent, ln a = ln x_i -
// phi (dissolved particles) / (liquid), which is 1 when nothing is dissolved
// in it. 0 for a gas or a solute that is absent.
double activity(const SpeciesTable &table, const CellState &cell, SpeciesId species, const CellMeta *meta = nullptr);

// Water's ionisation constant at temperature T (GAME_SPEC §9, Stage 43): Kw = a(H+) a(OH-) / a(H2O), from the
// table's ionisation at 25 C and its heat capacity held constant. 0 for a table without it.
double water_ionisation(const SpeciesTable &table, double temperature);

// The cell's pH at temperature T (GAME_SPEC §9, Stages 43 and 63): -log10 a(H+). Hydrogen and hydroxide ions are
// species (Stage 63), at their fast equilibria with water, the bisulfate and copper's hydrolysis; their difference,
// m_H - m_OH, is what the ionisation of water leaves unchanged, so the free ions solve
// (m_H + x)(m_OH + x) gamma_H gamma_OH = Kw a(H2O) from it exactly, below a quantum of either as well. Each particle's
// activity coefficient is Pitzer's (§10).
// false, and `ph` left, where the cell holds no liquid or its table no ionisation of water, or where what is left
// over of the acid or the base is more than PH_MOST_EXCESS mol/kg, past the activity model's range (Stage 44).
inline constexpr double PH_MOST_EXCESS = 6.0;
bool ph(const SpeciesTable &table, const CellState &cell, double temperature, double &ph);

// The solution's ionic strength, I = 1/2 sum m_j z_j^2 over its particles, mol/kg (0 with no liquid).
double ionic_strength(const SpeciesTable &table, const CellState &cell);
double ionic_strength(const SpeciesTable &table, const CellMixtures &mixtures);
// (Stage 63) The activity model's range: Pitzer's fits hold to about 6 mol/kg of a salt; past an ionic strength of
// MOST_IONIC_STRENGTH (a 2:2 salt at 5 mol/kg, a 1:1 at 20) the game cannot say how a solution's ions share
// themselves, and a fast equilibrium among dissolved species alone (every species dissolved, or the solvent: water's
// ionisation, the bisulfate, copper's hydrolysis) stands as it was (`stands`). A solution boiled nearly dry keeps the
// traces its ions held, and what crystallizes out of it still does (its solid is not among the dissolved).
inline constexpr double MOST_IONIC_STRENGTH = 20.0;
bool among_dissolved(const SpeciesTable &table, std::span<const Term> terms);
bool stands(const SpeciesTable &table, const CellState &cell, std::span<const Term> terms);
bool stands(const SpeciesTable &table, const CellMixtures &mixtures, std::span<const Term> terms);

// A dissolved species' molality: its amount per 55.51 mol of the cell's
// liquid (per kg of water). 0 when the cell has no liquid.
double molality(const SpeciesTable &table, const CellState &cell, SpeciesId species);

// The enthalpy, in quanta, that these amounts carry at temperature T: what a
// transfer of matter takes with it (GAME_SPEC §6).
Energy enthalpy_carried(const SpeciesTable &table, std::span<const SpeciesAmount> amounts, double temperature);

// (Stage 88, GAME_SPEC §6, Liquids that give) What a pressure `over` the room's adds to the enthalpy these amounts
// carry at T, in J: what crosses carries its enthalpy at the giver's state. A solid's v_i p, a liquid's or a solute's
// v_i (p - k p^2 / 2) - T (v_i' (p - k p^2 / 2) - v_i k' p^2 / 2) (its potential's, less T times its slope), a gas's
// nothing. At the room's pressure, nought.
double enthalpy_over(const SpeciesTable &table, std::span<const SpeciesAmount> amounts, double temperature, double over);

// The entropy, in J/K, of the cell at temperature T > 0:
// sum of n_i (S0_i + Cp_i ln(T / T0) - R ln a_i), a_i the activity above (the
// mixing terms of its gas and of its solution).
double entropy(const SpeciesTable &table, const CellState &cell, double temperature, const CellMeta *meta = nullptr);

// The affinity of a change in the cell at temperature T, in J per mole of
// extent: A = -sum of nu_i mu_i, with mu_i = h_i(T) - T s_i(T, a_i). Positive
// when the change is spontaneous as written, zero at equilibrium, negative
// when its reverse is spontaneous. +infinity when a gas or a solute it makes
// is absent, -infinity when one it consumes is absent, 0 when both. `defined`
// is false when a species it names has no thermodynamic data, or is dissolved
// in a cell with no liquid.
double affinity(const SpeciesTable &table, const CellState &cell, std::span<const Term> terms, double temperature, bool &defined, const CellMeta *meta = nullptr);
// (Stage 75) several reactions' affinities in one cell at one temperature, each the value affinity() gives it: the
// cell's mixtures and the solution's excess worked out once for them all
void affinities(const SpeciesTable &table, const CellState &cell, std::span<const std::span<const Term>> reactions, double temperature, const CellMeta *meta, std::span<double> a,
		std::span<bool> defined);

} // namespace chem
