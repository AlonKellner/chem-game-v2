#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

// Species and reactions (GAME_SPEC §4.1, §4.3). A species is a (formula, phase)
// pair with its element counts and charge. A Reaction value is balanced in
// every element and in charge: an unbalanced one cannot be constructed.
namespace chem {

// The elements a table may hold. A fixed bound keeps element vectors plain
// arrays; raise it when the game needs more.
inline constexpr int MAX_ELEMENTS = 16;
// The species one reaction may name.
inline constexpr int MAX_TERMS = 8;

enum class Phase : uint8_t { Solid = 0, Liquid = 1, Gas = 2, Aqueous = 3 };

// A set of phases: what an edge lets through (chem/world.hpp).
using PhaseMask = uint8_t;
constexpr PhaseMask phase_bit(Phase p) {
	return static_cast<PhaseMask>(1u << static_cast<unsigned>(p));
}
inline constexpr PhaseMask PHASES_NONE = 0; // heat only
inline constexpr PhaseMask PHASES_GAS = phase_bit(Phase::Gas);
inline constexpr PhaseMask PHASES_LIQUID = static_cast<PhaseMask>(phase_bit(Phase::Liquid) | phase_bit(Phase::Aqueous));
inline constexpr PhaseMask PHASES_ALL = 0x0f;

using Atoms = std::array<int32_t, MAX_ELEMENTS>;

struct ElementDef {
	std::string_view symbol;
	int64_t mass_ug_per_mol = 0; // micrograms per mole: 1.008 g/mol is 1'008'000
};

// The particles in solution (GAME_SPEC §4.6, §10): the ions of a table's
// dissolved species, and a neutral particle for each dissolved species that
// names none.
inline constexpr int MAX_IONS = 3; // per species
inline constexpr int MAX_PARTICLES = 16; // per table

struct IonCount {
	int32_t ion = -1;
	int32_t count = 0;
};

struct IonDef {
	std::string_view name; // "SO4-2"; a neutral particle: its species' id
	int32_t charge = 0;
	double diffusion = 0.0; // its diffusion coefficient in water at 25 C, m^2/s; 0: not known (GAME_SPEC §7)
};

// The solvent's viscosity at one temperature (GAME_SPEC §7): what scales the
// ions' diffusion away from 25 C.
struct ViscosityPoint {
	double kelvin = 0.0;
	double pascal_seconds = 0.0;
};

// The solvent's properties at one temperature (Stage 47: saturated liquid water, Lienhard's Table A.3): its
// density (kg/m^3), heat capacity (J/kg K), conductivity (W/m K), thermal diffusivity and kinematic viscosity
// (m^2/s), Prandtl number and thermal expansion (1/K).
struct SolventPoint {
	double kelvin = 0.0;
	double rho = 0.0, cp = 0.0, k = 0.0, alpha = 0.0, nu = 0.0, pr = 0.0, beta = 0.0;
};

// Pitzer's parameters of one cation and one anion (GAME_SPEC §10).
struct PairDef {
	int32_t cation = -1, anion = -1; // ions
	double beta0 = 0.0, beta1 = 0.0, beta2 = 0.0, c_phi = 0.0;
	double alpha1 = 2.0, alpha2 = 12.0;
};

// The electrolyte model of a table (GAME_SPEC §10): the Debye-Hückel term and
// the pairs, at 25 C, used at every temperature.
struct SolutionDef {
	double a_phi = 0.392;
	double b = 1.2;
	std::vector<IonDef> ions;
	std::vector<PairDef> pairs;
	std::vector<ViscosityPoint> viscosity; // the solvent's, by rising temperature; empty: diffusion is not scaled
	// water's own ionisation, H2O(l) -> H+(aq) + OH-(aq), at 25 C (GAME_SPEC §9, Stage 43): its enthalpy and
	// Gibbs energy (J/mol) and its change of heat capacity (J/mol/K, held at every temperature); Kw follows.
	// ionisation_g 0: none, and no pH
	double ionisation_h = 0.0, ionisation_g = 0.0, ionisation_cp = 0.0;
	std::vector<SolventPoint> solvent; // by rising temperature; empty: none known (no column's buoyancy, Stage 47)
};

struct SpeciesDef {
	std::string_view id; // "water"
	std::string_view formula; // "H2O"
	Phase phase = Phase::Solid;
	int32_t charge = 0;
	Atoms atoms{}; // count of each of the table's elements
	// Thermodynamic data at 298.15 K and 1 bar (GAME_SPEC §4.1), present only
	// once verified (docs/DATA_SOURCES.md). Without it the species cannot be
	// in a cell that has a temperature.
	bool has_thermo = false;
	double hf = 0.0; // standard enthalpy of formation, J/mol
	double cp = 0.0; // molar heat capacity, J/mol/K (constant)
	double s0 = 0.0; // standard entropy, J/mol/K
	// A vessel's own material (glass, rubber): it holds heat, takes no room
	// inside the vessel, never moves along an edge and never reacts.
	bool wall = false;
	// The volume of a mole of a condensed species, m^3 (derived from its
	// density by the build); 0 when unknown. A condensed species without it
	// cannot be in a rigid cell. A dissolved species: its apparent molar
	// volume (GAME_SPEC §10); without it, it takes no room.
	double molar_volume = 0.0;
	// A dissolved species: the particles it is made of in solution (GAME_SPEC
	// §4.6), each an index into the table's solution ions with its count; a
	// count of 0 ends the list. One with none is a neutral particle of its own
	// (the table gives it one).
	std::array<IonCount, MAX_IONS> ions{};
	// A solid: the diameter of its grains as they come out of a solution by a
	// fast equilibrium (a precipitate, GAME_SPEC §9, Stage 44), m; 0: one lump
	double grain = 0.0;
	// A gas: how fast it diffuses through the room's air, m^2/s at 298.15 K and 1 atm, and the exponent of its
	// temperature (D = D298 (T / 298.15)^n; GAME_SPEC §6, Stage 50: an open vessel's gas exchanged at its mouth);
	// 0: it does not diffuse
	double air_diffusion = 0.0, air_exponent = 0.0;
};

struct SpeciesId {
	int32_t index = -1;
	constexpr bool operator==(const SpeciesId &) const = default;
};

// One species of a reaction with its stoichiometric number: negative for a
// reactant, positive for a product.
struct Term {
	SpeciesId species;
	int32_t nu = 0;
};

// How a reaction runs on its own. Equilibrium (GAME_SPEC §8): it is fast
// against the step, so each step takes it to its equilibrium in every cell
// (boiling, condensation). FirstOrder (GAME_SPEC §7):
//
//   rate = prefactor * e^(-activation / RT) * n_in * affinity factor   [mol/s of `in`]
//
// where `in` is one of the reactants. The affinity factor (chem/sim.hpp) is in
// [-1, 1], zero at equilibrium, and has the sign of the reaction's affinity, so
// the reaction stops at its equilibrium and runs backwards past it.
//
// Surface (GAME_SPEC §7, §9): the reaction happens at the surface of one of
// its solids, `in`, a reactant (it dissolves) or a product (it grows):
//
//   at the surface:  prefactor * e^(-activation / RT) * area * m_driver^driver_order * affinity factor
//   by transport:    area * (Sh D / d) * (C - C_eq) / nu   (Sh = 2 * mixing)
//   rate: the two in series, 1 / rate = 1 / surface + 1 / transport   [mol/s]
//
// The area is that of the solid's particles, equal spheres:
// (36 pi)^(1/3) N^(1/3) (n v)^(2/3), d their diameter. `driver`, if any, is a dissolved reactant
// whose molality (its concentration, not its activity) the surface rate goes with; it is what transport
// brings, or else the reaction's dissolved species (chem/sim.hpp). Where `in` is a product, new
// particles form in a supersaturated solution, nucleation * (kg of solvent) *
// (S - 1)^nucleation_order per second, S = e^(A/RT); each begins as one quantum.
struct Kinetics {
	enum class Law : uint8_t { None, FirstOrder, Equilibrium, Surface };
	Law law = Law::None;
	SpeciesId in;
	double prefactor = 0.0; // FirstOrder: 1/s. Surface: mol/m^2/s
	double activation = 0.0; // J/mol
	SpeciesId driver; // Surface: a dissolved reactant, or none
	double nucleation = 0.0; // Surface: 1/kg/s
	double nucleation_order = 0.0;
	double driver_order = 1.0; // Surface: the surface rate goes with m_driver to this power
	// Surface (Stage 57): its solid dissolves into its solution at transport's pace, its surface's step a thousand
	// times its growth's (a crystal gives up its ions; only its growth builds a lattice)
	bool dissolves_by_transport = false;
};

constexpr Kinetics first_order(SpeciesId in, double prefactor, double activation) {
	return Kinetics{ Kinetics::Law::FirstOrder, in, prefactor, activation, SpeciesId{}, 0.0, 0.0 };
}
constexpr Kinetics equilibrium() {
	return Kinetics{ Kinetics::Law::Equilibrium, SpeciesId{}, 0.0, 0.0, SpeciesId{}, 0.0, 0.0 };
}
constexpr Kinetics surface(SpeciesId in, double prefactor, double activation, SpeciesId driver = {}, double nucleation = 0.0, double nucleation_order = 0.0,
		double driver_order = 1.0) {
	return Kinetics{ Kinetics::Law::Surface, in, prefactor, activation, driver, nucleation, nucleation_order, driver_order };
}

struct ReactionDef {
	std::string_view id;
	std::array<Term, MAX_TERMS> terms{};
	int32_t term_count = 0;
	Kinetics kinetics{};
};

// Sum of nu * atoms over the terms is zero for every element, and the sum of
// nu * charge is zero. constexpr: the build's tables are checked by the
// compiler (static_assert in the generated tables).
constexpr bool balanced(std::span<const SpeciesDef> species, std::span<const Term> terms) {
	std::array<int64_t, MAX_ELEMENTS> sum{};
	int64_t charge = 0;
	for (const Term &t : terms) {
		if (t.species.index < 0 || static_cast<size_t>(t.species.index) >= species.size()) {
			return false;
		}
		const SpeciesDef &s = species[static_cast<size_t>(t.species.index)];
		for (size_t e = 0; e < sum.size(); ++e) {
			sum[e] += static_cast<int64_t>(t.nu) * s.atoms[e];
		}
		charge += static_cast<int64_t>(t.nu) * s.charge;
	}
	for (const int64_t v : sum) {
		if (v != 0) {
			return false;
		}
	}
	return charge == 0;
}

constexpr bool all_balanced(std::span<const SpeciesDef> species, std::span<const ReactionDef> reactions) {
	for (const ReactionDef &r : reactions) {
		if (r.term_count <= 0 || r.term_count > MAX_TERMS) {
			return false;
		}
		if (!balanced(species, std::span<const Term>(r.terms.data(), static_cast<size_t>(r.term_count)))) {
			return false;
		}
	}
	return true;
}

// The elements and species of a world. Immutable once made. The string views
// of its definitions must outlive it (the build's tables are static).
class SpeciesTable {
public:
	// Refuses (nullopt, the reason in *why): no or too many elements, a repeated
	// symbol or id, a non-positive atomic mass, a negative atom count, a count
	// for an element the table does not have, a species with no atoms, a molar
	// mass of 2^30 ug/mol (about 1074 g/mol) or more, thermodynamic data that
	// is not finite.
	// The solution model (GAME_SPEC §10) is refused when: an ion is named
	// twice or has no name; a species names ions without being dissolved, an
	// ion out of range or a count that is not positive, or ions whose charges
	// do not sum to its own; a pair is not one cation and one anion, or a
	// parameter is not finite, or an alpha or b not positive; there are more
	// than MAX_PARTICLES particles; an ion's diffusion coefficient is negative
	// or not finite; the solvent's viscosity is not positive at rising temperatures. A dissolved species that names no ions is
	// given a neutral particle of its own.
	static std::optional<SpeciesTable> make(std::vector<ElementDef> elements, std::vector<SpeciesDef> species, std::string *why = nullptr,
			SolutionDef solution = {});

	int element_count() const { return static_cast<int>(elements_.size()); }
	int species_count() const { return static_cast<int>(species_.size()); }
	const ElementDef &element(int e) const { return elements_[static_cast<size_t>(e)]; }
	const SpeciesDef &species(SpeciesId s) const { return species_[static_cast<size_t>(s.index)]; }
	std::span<const SpeciesDef> all_species() const { return species_; }
	// derived from the element counts, never entered
	int64_t molar_mass_ug(SpeciesId s) const { return molar_mass_[static_cast<size_t>(s.index)]; }
	bool has(SpeciesId s) const { return s.index >= 0 && s.index < species_count(); }
	// the species with this id, or an invalid id
	SpeciesId find(std::string_view id) const;
	// the particles in solution and their interactions (GAME_SPEC §10)
	const SolutionDef &solution() const { return solution_; }
	int particle_count() const { return static_cast<int>(solution_.ions.size()); }

private:
	SpeciesTable() = default;
	std::vector<ElementDef> elements_;
	std::vector<SpeciesDef> species_;
	std::vector<int64_t> molar_mass_;
	SolutionDef solution_;
};

// A balanced reaction over one species table. There is no other kind: make()
// refuses an unbalanced one, so `transform` (chem/world.hpp) cannot create or
// destroy an atom or a charge.
class Reaction {
public:
	// Refuses (nullopt, the reason in *why): no terms or more than MAX_TERMS, an
	// unknown or repeated species, a zero stoichiometric number, no reactant or
	// no product, an element or the charge out of balance, a wall species;
	// kinetics that are malformed: a first-order `in` that is not a reactant; a
	// surface `in` that is not one of its solids with a molar volume; a driver
	// that is not a dissolved reactant; a number that is negative or not finite.
	static std::optional<Reaction> make(const SpeciesTable &table, std::string_view id, std::span<const Term> terms, std::string *why = nullptr,
			Kinetics kinetics = {});

	std::string_view id() const { return id_; }
	std::span<const Term> terms() const { return std::span<const Term>(terms_.data(), static_cast<size_t>(term_count_)); }
	const SpeciesTable *table() const { return table_; }
	const Kinetics &kinetics() const { return kinetics_; }
	// the stoichiometric number of a species (0 when the reaction does not name it)
	int32_t nu(SpeciesId s) const;

private:
	Reaction() = default;
	Kinetics kinetics_{};
	const SpeciesTable *table_ = nullptr;
	std::string id_;
	std::array<Term, MAX_TERMS> terms_{};
	int32_t term_count_ = 0;
};

// The build's tables (data/elements.json, data/species.json, data/reactions.json,
// embedded and checked by native/datagen): the species the game ships and its
// reactions, in the files' order. chem/builtin_ids.hpp names their indices.
const SpeciesTable &builtin_species();
std::span<const Reaction> builtin_reactions();

// What the game says of a built-in reaction (data/reactions.json "notice",
// GAME_SPEC §13.4): the level of the notice when it starts in an item (0
// detail, 1 event, 2 warning, 3 danger), and what it says then. Text for the
// player, not physics: one per reaction, in builtin_reactions()'s order.
struct ReactionNote {
	std::string_view id;
	int level = 0;
	std::string_view says;
	std::string_view explain; // why it happens (GAME_SPEC §13.6, "explain")
};
std::span<const ReactionNote> builtin_reaction_notes();

} // namespace chem
