#pragma once
// Tables for the tests (GAME_SPEC §4.1): the built-in one with a species' data
// taken away, for the refusals that need a species the game does not know.
#include "chem/builtin_ids.hpp"
#include "chem/species.hpp"

#include <doctest/doctest.h>

#include <vector>

namespace chem::testing {

// The built-in species and solution model, with the trihydrate's
// thermodynamic data and molar volume taken away.
inline const SpeciesTable &unverified_table() {
	static const SpeciesTable table = [] {
		const SpeciesTable &base = builtin_species();
		std::vector<ElementDef> elements;
		for (int e = 0; e < base.element_count(); ++e) {
			elements.push_back(base.element(e));
		}
		std::vector<SpeciesDef> species(base.all_species().begin(), base.all_species().end());
		SpeciesDef &unknown = species[static_cast<size_t>(builtin::copper_sulfate_trihydrate.index)];
		unknown.has_thermo = false;
		unknown.molar_volume = 0.0;
		std::string why;
		auto made = SpeciesTable::make(elements, species, &why, base.solution());
		REQUIRE_MESSAGE(made.has_value(), why);
		return *made;
	}();
	return table;
}

// The built-in species and solution model with one more: urea, CH4N2O(aq),
// a neutral solute (it names no ions, so it is one particle of its own).
// Its numbers are the test's, not data.
inline const SpeciesTable &with_neutral_solute() {
	static const SpeciesTable table = [] {
		const SpeciesTable &base = builtin_species();
		std::vector<ElementDef> elements;
		for (int e = 0; e < base.element_count(); ++e) {
			elements.push_back(base.element(e));
		}
		std::vector<SpeciesDef> species(base.all_species().begin(), base.all_species().end());
		SpeciesDef urea;
		urea.id = "urea_aq";
		urea.formula = "CH4N2O";
		urea.phase = Phase::Aqueous;
		const int counts[] = { 4, 1, 2, 1 }; // H, C, N, O: the table's first four elements
		for (size_t e = 0; e < 4; ++e) {
			REQUIRE(base.element(static_cast<int>(e)).symbol == std::string_view("HCNO").substr(e, 1));
			urea.atoms[e] = counts[e];
		}
		urea.has_thermo = true;
		urea.hf = -320000.0;
		urea.cp = 90.0;
		urea.s0 = 170.0;
		species.push_back(urea);
		std::string why;
		auto made = SpeciesTable::make(elements, species, &why, base.solution());
		REQUIRE_MESSAGE(made.has_value(), why);
		return *made;
	}();
	return table;
}

} // namespace chem::testing
