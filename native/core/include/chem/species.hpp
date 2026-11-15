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

struct ReactionDef {
	std::string_view id;
	std::array<Term, MAX_TERMS> terms{};
	int32_t term_count = 0;
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
	static std::optional<SpeciesTable> make(std::vector<ElementDef> elements, std::vector<SpeciesDef> species, std::string *why = nullptr);

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

private:
	SpeciesTable() = default;
	std::vector<ElementDef> elements_;
	std::vector<SpeciesDef> species_;
	std::vector<int64_t> molar_mass_;
};

// A balanced reaction over one species table. There is no other kind: make()
// refuses an unbalanced one, so `transform` (chem/world.hpp) cannot create or
// destroy an atom or a charge.
class Reaction {
public:
	// Refuses (nullopt, the reason in *why): no terms or more than MAX_TERMS, an
	// unknown or repeated species, a zero stoichiometric number, no reactant or
	// no product, an element or the charge out of balance.
	static std::optional<Reaction> make(const SpeciesTable &table, std::string_view id, std::span<const Term> terms, std::string *why = nullptr);

	std::string_view id() const { return id_; }
	std::span<const Term> terms() const { return std::span<const Term>(terms_.data(), static_cast<size_t>(term_count_)); }
	const SpeciesTable *table() const { return table_; }

private:
	Reaction() = default;
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

} // namespace chem
