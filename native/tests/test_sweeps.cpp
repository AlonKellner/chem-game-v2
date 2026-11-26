// The conservation sweep (GAME_SPEC §18): random worlds over random species
// tables, driven by random calls of the two primitives, honest and dishonest.
// After every step every invariant holds and the world's totals are the
// integers it was built with; a refused call leaves the state bit-identical.
#include "chem/invariants.hpp"
#include "chem/sim.hpp"
#include "rng.hpp"

#include <doctest/doctest.h>

#include <cstdio>
#include <cstdlib>
#include <deque>
#include <functional>
#include <span>
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

// ------------------------------------------------------------------ thermal

namespace {

// Random thermal worlds: cells of random matter at random temperatures and
// reservoirs, joined by edges of random conductance (zero, ordinary, enormous),
// with the topology and the conductances changing between steps.
// With `reactive`, the chemistry's reactions run too, with random rates: random
// formation enthalpies and entropies make them spontaneous in either direction.
// With `volumes`, about half the cells are rigid, the edges let gas through,
// and gas flows to keep the rigid cells full.
// every gas exchanged by diffusion in the sweeps' steps (Stage 50)
long long exchanges_seen = 0;
// called with every step's trace when set (Stage 68: check_all against the checks one by one)
std::function<void(const SpeciesTable &, const StepTrace &, std::span<const Reaction>)> each_trace;

void run_thermal_world(uint64_t seed, long long &transfers, bool reactive = false, long long *transforms = nullptr, bool volumes = false,
		long long *gas_moves = nullptr) {
	Rng rng(seed);
	RandomChemistry chem;
	make_chemistry(rng, chem);
	// give every species thermodynamic data
	std::vector<ElementDef> elements;
	for (int e = 0; e < chem.table->element_count(); ++e) {
		elements.push_back(chem.table->element(e));
	}
	std::vector<SpeciesDef> species(chem.table->all_species().begin(), chem.table->all_species().end());
	for (SpeciesDef &def : species) {
		def.has_thermo = true;
		def.hf = static_cast<double>(rng.range(-2000000, 500000));
		def.cp = static_cast<double>(rng.range(10, 300));
		if (def.phase == Phase::Gas && def.cp < 21.0) {
			def.cp += 21.0; // an ideal gas's Cp is at least 5/2 R: its Cv at least 3/2 R (Stage 28: a sealed cell's heat capacity is Cv)
		}
		def.s0 = static_cast<double>(rng.range(1, 400));
		if (volumes && (def.phase == Phase::Solid || def.phase == Phase::Liquid)) {
			def.molar_volume = static_cast<double>(rng.range(5, 100)) * 1e-6;
		}
		if (volumes && def.phase == Phase::Gas && rng.chance(80)) { // (Stage 50) a gas that diffuses at an exchanging edge
			def.air_diffusion = static_cast<double>(rng.range(1, 100)) * 1e-6;
			def.air_exponent = static_cast<double>(rng.range(0, 200)) / 100.0;
		}
	}
	if (volumes) {
		// every vessel has a wall: a cell drained of its gas still has a temperature
		SpeciesDef wall;
		wall.id = "wall";
		wall.formula = "wall";
		wall.atoms[0] = 1;
		wall.has_thermo = true;
		wall.cp = static_cast<double>(rng.range(10, 300));
		wall.wall = true;
		species.push_back(wall);
	}
	std::string why;
	const auto table = SpeciesTable::make(elements, species, &why, chem.table->solution());
	REQUIRE_MESSAGE(table.has_value(), why);
	const int contents = table->species_count() - (volumes ? 1 : 0); // the species a cell may be filled with

	World::Builder builder(*table);
	const int cells = static_cast<int>(rng.range(1, 6));
	std::vector<int> rigid_cells;
	for (int c = 0; c < cells; ++c) {
		// what goes in it, decided first: a rigid cell's volume depends on it
		const int kinds = static_cast<int>(rng.range(1, 3));
		std::vector<SpeciesAmount> holds;
		double gas_mol = 0.0, all_mol = 0.0;
		for (int k = 0; k < kinds; ++k) {
			// from a speck to hundreds of moles
			const int64_t quanta = rng.chance(20) ? rng.range(1000, 1000000) : rng.range(1000000000, 300000000000000);
			const SpeciesId id{ static_cast<int32_t>(rng.range(0, contents - 1)) };
			holds.push_back(SpeciesAmount{ id, Amount{ quanta } }); // a species drawn twice: the last amount stands
		}
		// a dissolved species needs a liquid to be dissolved in: without one, it is left out
		bool has_liquid = false;
		for (const SpeciesAmount &h : holds) {
			has_liquid = has_liquid || table->species(h.species).phase == Phase::Liquid;
		}
		if (!has_liquid) {
			std::erase_if(holds, [&](const SpeciesAmount &h) { return table->species(h.species).phase == Phase::Aqueous; });
			if (holds.empty()) {
				for (int s = 0; s < contents && holds.empty(); ++s) {
					if (table->species(SpeciesId{ s }).phase != Phase::Aqueous) {
						holds.push_back(SpeciesAmount{ SpeciesId{ s }, Amount{ rng.range(1000000000, 300000000000000) } });
					}
				}
				if (holds.empty()) {
					return; // a chemistry of dissolved species alone: no world to build
				}
			}
		}
		for (size_t k = 0; k < holds.size(); ++k) {
			bool last = true;
			for (size_t later = k + 1; later < holds.size(); ++later) {
				last = last && !(holds[later].species == holds[k].species);
			}
			if (last) {
				all_mol += holds[k].amount.mol();
				gas_mol += table->species(holds[k].species).phase == Phase::Gas ? holds[k].amount.mol() : 0.0;
			}
		}
		const double kelvin = static_cast<double>(rng.range(80, 3000));
		CellId cell;
		if (volumes && c > 0 && rng.chance(50)) {
			// from half to twice what its gas needs, with room for whatever its solids may become
			const double volume = gas_mol * 8.314462618 * kelvin / 101325.0 * static_cast<double>(rng.range(50, 200)) / 100.0 + all_mol * 2e-3 + 1e-9;
			cell = builder.add_rigid_cell("cell" + std::to_string(c), volume);
			rigid_cells.push_back(c);
		} else {
			cell = builder.add_cell("cell" + std::to_string(c));
		}
		for (const SpeciesAmount &h : holds) {
			builder.set_amount(cell, h.species, h.amount);
		}
		if (volumes) {
			builder.set_amount(cell, SpeciesId{ contents }, Amount{ rng.range(1000000, 1000000000000) });
		}
		builder.set_temperature(cell, kelvin);
	}
	// every rigid cell has a gas path to cell 0, which is elastic
	for (const int c : rigid_cells) {
		builder.add_edge(CellId{ c }, CellId{ 0 }, rng.chance(50) ? PHASES_GAS : PHASES_ALL, rng.chance(50) ? 0.0 : 1.0);
	}
	const int reservoirs = static_cast<int>(rng.range(0, 2));
	for (int r = 0; r < reservoirs; ++r) {
		builder.add_reservoir("reservoir" + std::to_string(r), static_cast<double>(rng.range(80, 3000)));
	}
	const int all = cells + reservoirs;
	const auto conductance = [&rng] {
		return rng.chance(15) ? 0.0 : rng.chance(15) ? 1e9 : static_cast<double>(rng.range(1, 50000)) / 1000.0;
	};
	if (all > 1) {
		const int edges = static_cast<int>(rng.range(1, 8));
		for (int e = 0; e < edges; ++e) {
			const auto a = static_cast<int32_t>(rng.range(0, all - 1));
			const auto b = static_cast<int32_t>((a + rng.range(1, all - 1)) % all);
			const PhaseMask mask = volumes && a < cells && b < cells && rng.chance(50) ? PHASES_GAS : PHASES_NONE;
			builder.add_edge(CellId{ a }, CellId{ b }, mask, conductance());
		}
	}
	auto built = builder.build(&why);
	REQUIRE_MESSAGE(built.has_value(), why);
	for (size_t e = 0; volumes && e < built->edges().size(); ++e) { // (Stage 50) gas exchanged by diffusion, weak to strong
		if ((built->edges()[e].phases & PHASES_GAS) != 0 && rng.chance(50)) {
			REQUIRE(built->set_exchange(EdgeId{ static_cast<int32_t>(e) }, static_cast<double>(rng.range(1, 100000)) * 1e-5) == Refusal::None);
		}
	}
	std::vector<Reaction> reactions;
	if (reactive) {
		for (const Reaction &r : chem.reactions) {
			SpeciesId composite;
			for (const Term &t : r.terms()) {
				if (t.nu < 0) {
					composite = t.species;
				}
			}
			// one reaction of a world in three is fast: taken to equilibrium each step
			Kinetics kinetics = first_order(composite, static_cast<double>(rng.range(1, 1000000)) / 1000.0, static_cast<double>(rng.range(0, 60000)));
			if (reactions.empty() && rng.chance(33)) {
				kinetics = equilibrium();
			}
			auto made = Reaction::make(*table, "split", r.terms(), &why, kinetics);
			REQUIRE_MESSAGE(made.has_value(), why);
			reactions.push_back(*made);
		}
	}
	Simulation sim(std::move(*built), reactions);
	sim.set_checking(false); // checked here, with the seed in the message
	const Totals genesis = totals(*table, sim.world().snapshot());

	const auto extremes = [&](double &lo, double &hi) {
		lo = 1e300;
		hi = -1e300;
		for (int c = 0; c < all; ++c) {
			double t = 0.0;
			REQUIRE(sim.world().temperature(CellId{ c }, t));
			lo = t < lo ? t : lo;
			hi = t > hi ? t : hi;
		}
	};
	for (int step = 0; step < 60; ++step) {
		if (all > 1 && rng.chance(10)) {
			const auto a = static_cast<int32_t>(rng.range(0, all - 1));
			const auto b = static_cast<int32_t>((a + rng.range(1, all - 1)) % all);
			sim.world().connect(CellId{ a }, CellId{ b }, PHASES_NONE, nullptr, conductance());
		}
		if (rng.chance(10)) {
			const EdgeId edge{ static_cast<int32_t>(rng.range(0, static_cast<int64_t>(sim.world().edges().size()))) };
			if (rng.chance(50)) {
				sim.world().disconnect(edge);
			} else {
				sim.world().set_conductance(edge, conductance());
			}
		}
		double lo_before = 0.0, hi_before = 0.0, lo_after = 0.0, hi_after = 0.0;
		extremes(lo_before, hi_before);
		const StepTrace trace = sim.step();
		exchanges_seen += static_cast<long long>(trace.exchanged.size());
		extremes(lo_after, hi_after);
		for (const LedgerEntry &entry : trace.ledger) {
			if (entry.kind == LedgerEntry::Kind::Transfer) {
				++transfers;
				if (!entry.delta.empty() && gas_moves != nullptr) {
					++*gas_moves;
				}
			} else if (transforms != nullptr) {
				++*transforms;
			}
		}
		const Violations violations = check_all(*table, trace, sim.equilibria());
		if (each_trace) {
			each_trace(*table, trace, sim.equilibria());
		}
		if (!violations.empty() && std::getenv("CHEM_SWEEP_DUMP") != nullptr) {
			// CHEM_SWEEP_DUMP=1: the failing step's ledger and cells, to stderr
			for (const LedgerEntry &e : trace.ledger) {
				std::fprintf(stderr, "  %s cell %d -> %d, %zu species, energy %lld, extent %lld\n", e.kind == LedgerEntry::Kind::Transfer ? "transfer" : "transform",
						e.from.index, e.to.index, e.delta.size(), static_cast<long long>(e.energy.quanta), static_cast<long long>(e.extent));
			}
			for (size_t c = 0; c < trace.meta.size(); ++c) {
				double t = 0.0;
				sim.world().temperature(CellId{ static_cast<int32_t>(c) }, t);
				std::fprintf(stderr, "  cell %zu: %s volume %g, T %g, starved %d\n", c, trace.meta[c].reservoir ? "reservoir" : trace.meta[c].rigid ? "rigid" : "elastic",
						trace.meta[c].volume, t, c < trace.starved.size() ? trace.starved[c] : 0);
			}
			for (const Edge &e : trace.edges) {
				std::fprintf(stderr, "  edge %d - %d, phases %d, open %d\n", e.a.index, e.b.index, e.phases, e.open);
			}
		}
		REQUIRE_MESSAGE(violations.empty(), "seed " << seed << ", step " << step << ": " << violations[0]);
		REQUIRE_MESSAGE(totals(*table, trace.after) == genesis, "seed " << seed << ", step " << step << ": the world's totals moved");
		if (reactive || volumes) {
			continue; // a reaction's heat, or gas arriving, may take a cell past the others
		}
		// UX6: with no reaction, nothing gets hotter than the hottest or colder than the coldest
		const double slack = 1e-9 * hi_before;
		REQUIRE_MESSAGE(hi_after <= hi_before + slack, "seed " << seed << ", step " << step << ": the hottest cell warmed from " << hi_before << " to " << hi_after);
		REQUIRE_MESSAGE(lo_after >= lo_before - slack, "seed " << seed << ", step " << step << ": the coldest cell cooled from " << lo_before << " to " << lo_after);
	}
}

} // namespace

TEST_CASE("sweeps: heat flows downhill and entropy grows on every step of every random thermal world (Stage 2)") {
	const char *env = std::getenv("CHEM_SWEEP_WORLDS");
	const long long worlds = env != nullptr ? std::atoll(env) : 2000;
	long long transfers = 0;
	for (long long w = 0; w < worlds; ++w) {
		run_thermal_world(0x7e47000000ull + static_cast<uint64_t>(w), transfers);
	}
	MESSAGE(worlds << " thermal worlds, " << worlds * 60 << " steps, " << transfers << " heat transfers");
	CHECK(transfers > worlds);
}

TEST_CASE("sweeps: reactions run down their affinity and entropy grows on every step of every random reacting world (Stage 3)") {
	const char *env = std::getenv("CHEM_SWEEP_WORLDS");
	const long long worlds = env != nullptr ? std::atoll(env) : 2000;
	long long transfers = 0;
	long long transforms = 0;
	for (long long w = 0; w < worlds; ++w) {
		run_thermal_world(0x4eac7000000ull + static_cast<uint64_t>(w), transfers, true, &transforms);
	}
	MESSAGE(worlds << " reacting worlds, " << worlds * 60 << " steps, " << transforms << " reaction steps, " << transfers << " heat transfers");
	CHECK(transforms > worlds);
}

TEST_CASE("sweeps: rigid cells stay exactly full on every step of every random world with volumes (Stage 4)") {
	const char *env = std::getenv("CHEM_SWEEP_WORLDS");
	const long long worlds = env != nullptr ? std::atoll(env) : 2000;
	long long transfers = 0, transforms = 0, gas_moves = 0;
	for (long long w = 0; w < worlds; ++w) {
		run_thermal_world(0x9a5000000ull + static_cast<uint64_t>(w), transfers, true, &transforms, true, &gas_moves);
	}
	MESSAGE(worlds << " worlds with volumes, " << worlds * 60 << " steps, " << gas_moves << " gas flows, " << transforms << " reaction steps, "
				   << transfers - gas_moves << " heat transfers, " << exchanges_seen << " gases exchanged by diffusion");
	CHECK(gas_moves > worlds);
	CHECK(exchanges_seen > worlds); // (Stage 50) the sweep exchanges gas at its edges
}

// (Stage 68) check_all shares its work among the checks (the totals once, a replay's buffers kept): what it reports is
// still every check's report, one by one, in its order, on honest traces and on traces corrupted every way a check
// looks at
TEST_CASE("sweeps: check_all reports exactly what the checks report one by one, honest or corrupted (Stage 68)") {
	const auto one_by_one = [](const SpeciesTable &table, const StepTrace &trace, std::span<const Reaction> equilibria) {
		Violations out = check_P1(table, trace, equilibria);
		const auto add = [&](const Violations &v) { out.insert(out.end(), v.begin(), v.end()); };
		add(check_V2(table, trace));
		for (const auto check : { check_C1, check_C2, check_C3, check_C4, check_C5, check_C6, check_C7, check_C8, check_T1, check_T2, check_T3, check_X1, check_X2,
					 check_S1, check_V1, check_V3, check_L1, check_E1 }) {
			add(check(table, trace));
		}
		return out;
	};
	long long traces = 0, reported = 0;
	Rng pick(0x68c4ec);
	each_trace = [&](const SpeciesTable &table, const StepTrace &trace, std::span<const Reaction> equilibria) {
		std::vector<StepTrace> cases{ trace };
		const size_t cells = trace.after.cells.size();
		const size_t species = static_cast<size_t>(table.species_count());
		if (cells > 0 && species > 0) {
			const auto c = static_cast<size_t>(pick.range(0, static_cast<int64_t>(cells) - 1));
			const auto sp = static_cast<size_t>(pick.range(0, static_cast<int64_t>(species) - 1));
			cases.push_back(trace);
			cases.back().after.cells[c].amount[sp] += 1; // an atom from nowhere (C1, C2, C5)
			cases.push_back(trace);
			cases.back().after.cells[c].energy += 1000; // energy from nowhere (C3, C5, T2)
			cases.push_back(trace);
			cases.back().before.cells[c].amount[sp] -= 7; // the start not the last step's end (C1, C5, C6)
			cases.push_back(trace);
			cases.back().meta[c].sealed = !cases.back().meta[c].sealed; // the wrong kind (V3, T1)
		}
		if (!trace.ledger.empty()) {
			cases.push_back(trace);
			LedgerEntry &e = cases.back().ledger[static_cast<size_t>(pick.range(0, static_cast<int64_t>(trace.ledger.size()) - 1))];
			for (SpeciesAmount &d : e.delta) {
				d.amount.quanta = -d.amount.quanta; // an entry run backwards (C5, C7, C8, T1)
			}
			e.energy.quanta = -e.energy.quanta;
		}
		if (!trace.edges.empty()) {
			cases.push_back(trace);
			cases.back().edges[static_cast<size_t>(pick.range(0, static_cast<int64_t>(trace.edges.size()) - 1))].open = false; // (C7, V3)
		}
		for (const StepTrace &t : cases) {
			const Violations all = check_all(table, t, equilibria), each = one_by_one(table, t, equilibria);
			REQUIRE(all == each);
			++traces;
			reported += all.empty() ? 0 : 1;
		}
	};
	long long transfers = 0, transforms = 0, gas_moves = 0;
	for (uint64_t w = 0; w < 40; ++w) {
		run_thermal_world(0x68a11000ull + w, transfers, true, &transforms, true, &gas_moves);
	}
	each_trace = nullptr;
	MESSAGE(traces << " traces, " << reported << " with violations");
	CHECK(reported > traces / 4); // the corrupted ones are reported
	CHECK(reported < traces); // the honest ones are not
}
