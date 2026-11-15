// The conservation sweep (GAME_SPEC §18): random worlds over random species
// tables, driven by random calls of the two primitives, honest and dishonest.
// After every step every invariant holds and the world's totals are the
// integers it was built with; a refused call leaves the state bit-identical.
#include "chem/invariants.hpp"
#include "rng.hpp"

#include <doctest/doctest.h>

#include <cstdlib>
#include <deque>
#include <string>
#include <vector>

using namespace chem;

namespace {

struct RandomChemistry {
	std::deque<std::string> names; // the table's views point here
	std::optional<SpeciesTable> table;
	std::vector<Reaction> reactions;
};

// Base species with random atoms and charges; composites that are sums of
// bases, each with the reaction composite <-> its parts (balanced by
// construction, and Reaction::make agrees).
void make_chemistry(Rng &rng, RandomChemistry &out) {
	const int elements = static_cast<int>(rng.range(1, 5));
	std::vector<ElementDef> element_defs;
	for (int e = 0; e < elements; ++e) {
		out.names.push_back("E" + std::to_string(e));
		element_defs.push_back(ElementDef{ out.names.back(), rng.range(1000000, 5000000) });
	}
	const Phase phases[] = { Phase::Solid, Phase::Liquid, Phase::Gas, Phase::Aqueous };
	std::vector<SpeciesDef> species;
	const int bases = static_cast<int>(rng.range(2, 6));
	for (int i = 0; i < bases; ++i) {
		SpeciesDef def;
		out.names.push_back("base" + std::to_string(i));
		def.id = out.names.back();
		def.formula = def.id;
		def.phase = phases[rng.range(0, 3)];
		def.charge = rng.chance(30) ? static_cast<int32_t>(rng.range(-2, 2)) : 0;
		for (int e = 0; e < elements; ++e) {
			def.atoms[static_cast<size_t>(e)] = static_cast<int32_t>(rng.range(0, 3));
		}
		def.atoms[static_cast<size_t>(rng.range(0, elements - 1))] += 1; // at least one atom
		species.push_back(def);
	}
	struct Recipe {
		int composite;
		std::vector<Term> parts;
	};
	std::vector<Recipe> recipes;
	const int composites = static_cast<int>(rng.range(1, 4));
	for (int i = 0; i < composites; ++i) {
		SpeciesDef def;
		out.names.push_back("mix" + std::to_string(i));
		def.id = out.names.back();
		def.formula = def.id;
		def.phase = phases[rng.range(0, 3)];
		Recipe recipe{ bases + i, {} };
		const int parts = static_cast<int>(rng.range(1, 3));
		for (int p = 0; p < parts; ++p) {
			const SpeciesId base{ static_cast<int32_t>(rng.range(0, bases - 1)) };
			const auto k = static_cast<int32_t>(rng.range(1, 3));
			bool merged = false;
			for (Term &t : recipe.parts) {
				if (t.species == base) {
					t.nu += k;
					merged = true;
				}
			}
			if (!merged) {
				recipe.parts.push_back(Term{ base, k });
			}
			const SpeciesDef &b = species[static_cast<size_t>(base.index)];
			for (size_t e = 0; e < def.atoms.size(); ++e) {
				def.atoms[e] += k * b.atoms[e];
			}
			def.charge += k * b.charge;
		}
		species.push_back(def);
		recipes.push_back(recipe);
	}
	std::string why;
	out.table = SpeciesTable::make(element_defs, species, &why);
	REQUIRE_MESSAGE(out.table.has_value(), why);
	for (const Recipe &recipe : recipes) {
		std::vector<Term> terms = recipe.parts;
		terms.push_back(Term{ SpeciesId{ recipe.composite }, -1 });
		auto reaction = Reaction::make(*out.table, "split", terms, &why);
		REQUIRE_MESSAGE(reaction.has_value(), why);
		out.reactions.push_back(*reaction);
	}
}

struct Counts {
	long long accepted = 0;
	long long refused = 0;
	long long steps = 0;
};

void run_world(uint64_t seed, Counts &counts) {
	Rng rng(seed);
	RandomChemistry chem;
	make_chemistry(rng, chem);
	const SpeciesTable &table = *chem.table;
	const int species = table.species_count();

	World::Builder builder(table);
	const int cells = static_cast<int>(rng.range(2, 6));
	for (int c = 0; c < cells; ++c) {
		const CellId cell = builder.add_cell("cell" + std::to_string(c));
		for (int s = 0; s < species; ++s) {
			if (rng.chance(60)) {
				builder.set_amount(cell, SpeciesId{ s }, Amount{ rng.range(0, 1000000000000) });
			}
		}
		builder.set_energy(cell, Energy{ rng.range(-1000000000000, 1000000000000) });
	}
	const PhaseMask masks[] = { PHASES_NONE, PHASES_GAS, PHASES_LIQUID, PHASES_ALL, phase_bit(Phase::Solid) };
	const int edges = static_cast<int>(rng.range(1, 8));
	for (int e = 0; e < edges; ++e) {
		const auto a = static_cast<int32_t>(rng.range(0, cells - 1));
		const auto b = static_cast<int32_t>((a + rng.range(1, cells - 1)) % cells);
		builder.add_edge(CellId{ a }, CellId{ b }, masks[rng.range(0, 4)]);
	}
	std::string why;
	auto built = builder.build(&why);
	REQUIRE_MESSAGE(built.has_value(), why);
	World &world = *built;
	const Totals genesis = totals(table, world.snapshot());
	Snapshot previous = world.snapshot();

	for (int step = 0; step < 40; ++step) {
		// the topology, at the step boundary
		if (rng.chance(10)) {
			const auto a = static_cast<int32_t>(rng.range(0, cells - 1));
			const auto b = static_cast<int32_t>(rng.range(0, cells - 1)); // sometimes the same cell: refused
			world.connect(CellId{ a }, CellId{ b }, masks[rng.range(0, 4)]);
		}
		if (rng.chance(5)) {
			world.disconnect(EdgeId{ static_cast<int32_t>(rng.range(0, static_cast<int64_t>(world.edges().size()))) });
		}
		const int ops = static_cast<int>(rng.range(0, 12));
		for (int op = 0; op < ops; ++op) {
			const Snapshot before = world.snapshot();
			Refusal result;
			if (rng.chance(60)) {
				// a transfer: mostly along a real edge from one of its ends, mostly within what the cell holds
				const auto edge_count = static_cast<int64_t>(world.edges().size());
				const EdgeId edge{ static_cast<int32_t>(rng.range(0, rng.chance(5) ? edge_count : edge_count - 1)) };
				CellId from{ static_cast<int32_t>(rng.range(0, cells - 1)) };
				if (edge.index < edge_count && rng.chance(90)) {
					const Edge &e = world.edges()[static_cast<size_t>(edge.index)];
					from = rng.chance(50) ? e.a : e.b;
				}
				std::vector<SpeciesAmount> amounts;
				const int items = static_cast<int>(rng.range(0, 3));
				for (int i = 0; i < items; ++i) {
					const SpeciesId s{ static_cast<int32_t>(rng.range(0, rng.chance(3) ? species : species - 1)) };
					const int64_t have = world.amount(from, s).quanta;
					const int64_t want = rng.chance(85) ? rng.range(0, have) : rng.range(-5, 2 * have + 5);
					amounts.push_back(SpeciesAmount{ s, Amount{ want } });
				}
				result = world.transfer(edge, from, amounts, Energy{ rng.range(-1000000, 1000000) });
			} else {
				// a transform: either direction, sometimes more than the cell can give
				const Reaction &reaction = chem.reactions[static_cast<size_t>(rng.range(0, static_cast<int64_t>(chem.reactions.size()) - 1))];
				const CellId cell{ static_cast<int32_t>(rng.range(0, rng.chance(3) ? cells : cells - 1)) };
				const int64_t extent = rng.chance(80) ? rng.range(-1000000, 1000000) : rng.range(-4000000000000, 4000000000000);
				result = world.transform(cell, reaction, extent);
			}
			if (result == Refusal::None) {
				++counts.accepted;
			} else {
				++counts.refused;
				REQUIRE_MESSAGE(world.snapshot() == before, "seed " << seed << ": a refused call (" << refusal_text(result) << ") changed the state");
			}
		}
		const StepTrace trace = world.close_step();
		++counts.steps;
		const Violations violations = check_all(table, trace);
		REQUIRE_MESSAGE(violations.empty(), "seed " << seed << ", step " << step << ": " << violations[0]);
		REQUIRE_MESSAGE(totals(table, trace.after) == genesis, "seed " << seed << ", step " << step << ": the world's totals moved");
		REQUIRE_MESSAGE(trace.before == previous, "seed " << seed << ", step " << step << ": the trace does not start where the last ended");
		previous = trace.after;
	}
}

} // namespace

TEST_CASE("sweeps: conservation holds on every step of every random world (Stage 1)") {
	// CHEM_SWEEP_WORLDS scales the sweep (the sanitizer build runs fewer)
	const char *env = std::getenv("CHEM_SWEEP_WORLDS");
	const long long worlds = env != nullptr ? std::atoll(env) : 2000;
	Counts counts;
	for (long long w = 0; w < worlds; ++w) {
		run_world(0xc0ffee00ull + static_cast<uint64_t>(w), counts);
	}
	MESSAGE(worlds << " worlds, " << counts.steps << " steps, " << counts.accepted << " calls applied, " << counts.refused << " refused");
	// the sweep exercises both paths
	CHECK(counts.accepted > worlds);
	CHECK(counts.refused > worlds);
}
