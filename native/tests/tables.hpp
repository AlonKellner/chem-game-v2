#pragma once
// Tables for the tests (GAME_SPEC §4.1): the built-in one with a species' data
// taken away, for the refusals that need a species the game does not know.
#include "chem/builtin_ids.hpp"
#include "chem/species.hpp"
#include "chem/invariants.hpp"
#include "chem/sim.hpp"
#include "chem/world.hpp"

#include <doctest/doctest.h>

#include <string>
#include <vector>

namespace chem::testing {

using chem::put_amount;

// The table's reactions of the equilibrium law.
inline std::vector<Reaction> fast_equilibria() {
	std::vector<Reaction> out;
	for (const Reaction &r : builtin_reactions()) {
		if (r.kinetics().law == Kinetics::Law::Equilibrium) {
			out.push_back(r);
		}
	}
	return out;
}

// A state put by hand taken to its fast equilibria at a temperature (Stage 63: water's ionisation, the bisulfate,
// copper's hydrolysis): one step of them alone in a cell of its own, every invariant held; its solids' grains as put.
inline CellState settled(const CellState &state, double kelvin) {
	World::Builder b(builtin_species());
	const CellId cell = b.add_cell("cell");
	for (size_t s = 0; s < state.amount.size(); ++s) {
		if (state.amount[s] > 0) {
			b.set_amount(cell, SpeciesId{ static_cast<int32_t>(s) }, Amount{ state.amount[s] });
			if (s < state.particles.size() && state.particles[s] > 0) {
				b.set_particles(cell, SpeciesId{ static_cast<int32_t>(s) }, state.particles[s]);
			}
		}
	}
	b.set_temperature(cell, kelvin);
	std::string why;
	auto world = b.build(&why);
	REQUIRE_MESSAGE(world.has_value(), why);
	Simulation sim(std::move(*world), fast_equilibria());
	const StepTrace trace = sim.step();
	const Violations v = check_all(builtin_species(), trace, sim.equilibria());
	REQUIRE_MESSAGE(v.empty(), (v.empty() ? std::string() : v.front()));
	return sim.world().snapshot_cell(cell);
}

// Moles of a species in a state; of a retired formula unit (Stage 63), as many whole ones as its ions make (the
// fewest of its ions over their counts): exact where nothing else holds them.
inline double formula_mol(const SpeciesTable &table, const CellState &state, SpeciesId s) {
	if (!table.species(s).retired) {
		return static_cast<double>(state.amount[static_cast<size_t>(s.index)]) * 1e-12;
	}
	double fewest = -1.0;
	for (const auto &[ion, count] : table.as_species(s)) {
		const double whole = static_cast<double>(state.amount[static_cast<size_t>(ion.index)]) / static_cast<double>(count);
		fewest = fewest < 0.0 || whole < fewest ? whole : fewest;
	}
	return fewest < 0.0 ? 0.0 : fewest * 1e-12;
}

// Moles of an element in a state's species of one phase (Stage 63: the copper dissolved is in Cu2+, CuOH+ and
// Cu2(OH)2 2+ alike).
inline double element_mol(const SpeciesTable &table, const CellState &state, std::string_view symbol, Phase phase) {
	int element = -1;
	for (int e = 0; e < table.element_count(); ++e) {
		if (table.element(e).symbol == symbol) {
			element = e;
		}
	}
	REQUIRE(element >= 0);
	double total = 0.0;
	for (size_t s = 0; s < state.amount.size(); ++s) {
		const SpeciesDef &def = table.species(SpeciesId{ static_cast<int32_t>(s) });
		if (def.phase == phase) {
			total += static_cast<double>(state.amount[s]) * static_cast<double>(def.atoms[static_cast<size_t>(element)]) * 1e-12;
		}
	}
	return total;
}

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
