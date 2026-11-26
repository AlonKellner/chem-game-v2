// The world open at its boundary (GAME_SPEC §4, Stage 61): a cell added and supplied from outside, a cell frozen
// (taking part in nothing, its state standing) and thawed (as it was), a cell retired (all it held leaving the
// world); C9, the ledger of what crossed the boundary, and its self-test.
#include "chem/builtin_ids.hpp"
#include "chem/invariants.hpp"
#include "chem/sim.hpp"
#include "chem/thermo.hpp"
#include "tables.hpp"

#include <doctest/doctest.h>

#include <vector>

using namespace chem;

namespace {

constexpr double FLASK = 250e-6; // m^3

Amount mol(double n) {
	bool ok = false;
	const Amount a = amount_from_mol(n, ok);
	REQUIRE(ok);
	return a;
}

// a room's air and a hot flask of water open to it, both losing to the room; and a template world holding one
// more flask of water at 20 C, built as the bench would (what a supply is made from)
struct Room {
	CellId flask, air, room;
	EdgeId mouth;
	Simulation sim;
};

Room make_room() {
	World::Builder b(builtin_species());
	const CellId flask = b.add_rigid_cell("flask", FLASK);
	const CellId air = b.add_cell("air");
	const CellId room = b.add_reservoir("room", 293.15);
	const EdgeId mouth = b.add_edge(flask, air, PHASES_GAS);
	b.add_edge(flask, room, PHASES_NONE, 0.5);
	b.add_edge(air, room, PHASES_NONE, 1000.0);
	b.set_amount(flask, builtin::water, mol(5.0)).set_amount(flask, builtin::glass, mol(1.0));
	b.set_amount(flask, builtin::nitrogen, mol(0.79 * 0.006)).set_amount(flask, builtin::oxygen, mol(0.21 * 0.006)).set_temperature(flask, 353.15);
	b.set_amount(air, builtin::nitrogen, mol(79.0)).set_amount(air, builtin::oxygen, mol(21.0)).set_temperature(air, 293.15);
	std::string why;
	auto world = b.build(&why);
	REQUIRE_MESSAGE(world.has_value(), why);
	Simulation sim(std::move(*world), std::vector<Reaction>(builtin_reactions().begin(), builtin_reactions().end()));
	sim.set_checking(true);
	return Room{ flask, air, room, mouth, std::move(sim) };
}

CellState template_flask() {
	World::Builder b(builtin_species());
	const CellId flask = b.add_rigid_cell("flask", FLASK);
	const CellId air = b.add_cell("air");
	b.add_edge(flask, air, PHASES_GAS);
	b.set_amount(flask, builtin::water, mol(3.0)).set_amount(flask, builtin::glass, mol(1.0));
	b.set_amount(flask, builtin::nitrogen, mol(0.79 * 0.008)).set_amount(flask, builtin::oxygen, mol(0.21 * 0.008)).set_temperature(flask, 293.15);
	b.set_amount(air, builtin::nitrogen, mol(1.0)).set_temperature(air, 293.15);
	std::string why;
	auto world = b.build(&why);
	REQUIRE_MESSAGE(world.has_value(), why);
	return world->snapshot_cell(flask);
}

double celsius(const World &w, CellId c) {
	double t = 0.0;
	REQUIRE(w.temperature(c, t));
	return t - 273.15;
}

} // namespace

TEST_CASE("boundary: a cell added and supplied joins the world: the ledger counts it, and the world steps on with every invariant (C9, Stage 61)") {
	Room r = make_room();
	World &w = r.sim.world();
	const SpeciesTable &table = builtin_species();
	const Totals start = totals(table, w.snapshot());
	REQUIRE(r.sim.step().step == 0);
	CellMeta rigid;
	rigid.rigid = true;
	rigid.volume = FLASK;
	Refusal why = Refusal::None;
	const CellId made = w.add_cell("flask2", rigid, &why);
	REQUIRE(why == Refusal::None);
	const CellState supplied = template_flask();
	REQUIRE(w.supply(made, supplied) == Refusal::None);
	CHECK(w.supply(made, supplied) == Refusal::BadValue); // (into an empty cell only)
	EdgeId mouth;
	REQUIRE(w.connect(made, r.air, PHASES_GAS, &mouth) == Refusal::None);
	REQUIRE(w.connect(made, r.room, PHASES_NONE, nullptr, 0.5) == Refusal::None);
	CHECK(celsius(w, made) == doctest::Approx(20.0).epsilon(1e-6));
	Snapshot one;
	one.cells.push_back(supplied);
	const Totals in = totals(table, one), none{};
	for (int k = 0; k < 120; ++k) {
		const StepTrace trace = r.sim.step();
		CHECK(check_all(table, trace, {}).empty());
		CHECK(check_C9(start, in, none, totals(table, w.snapshot())).empty());
	}
	CHECK(check_C9(start, none, none, totals(table, w.snapshot())).size() > 0); // (without what was supplied, it fires)
}

TEST_CASE("boundary: a frozen cell takes part in nothing, its state standing; thawed, its edges open as they were (Stage 61)") {
	Room r = make_room();
	World &w = r.sim.world();
	const SpeciesTable &table = builtin_species();
	r.sim.step();
	const CellState before = w.snapshot_cell(r.flask);
	const double hot = celsius(w, r.flask);
	REQUIRE(hot > 70.0);
	REQUIRE(w.freeze(r.flask, true) == Refusal::None);
	CHECK(w.meta(r.flask).frozen);
	CHECK_FALSE(w.edges()[static_cast<size_t>(r.mouth.index)].open);
	CHECK(w.edges()[static_cast<size_t>(r.mouth.index)].frozen);
	for (int k = 0; k < 3600; ++k) {
		const StepTrace trace = r.sim.step();
		CHECK(check_all(table, trace, {}).empty());
	}
	CHECK(w.snapshot_cell(r.flask) == before); // a minute: not a quantum of it changed
	REQUIRE(w.freeze(r.flask, false) == Refusal::None);
	CHECK(w.edges()[static_cast<size_t>(r.mouth.index)].open);
	CHECK_FALSE(w.edges()[static_cast<size_t>(r.mouth.index)].frozen);
	for (int k = 0; k < 3600; ++k) {
		CHECK(check_all(table, r.sim.step(), {}).empty());
	}
	CHECK(celsius(w, r.flask) < hot - 1.0); // thawed, it cools (a time constant of some thirteen minutes)
}

TEST_CASE("boundary: a retired cell's matter and energy leave the world, its edges closed for good; C9 counts it (Stage 61)") {
	Room r = make_room();
	World &w = r.sim.world();
	const SpeciesTable &table = builtin_species();
	const Totals start = totals(table, w.snapshot());
	r.sim.step();
	REQUIRE(w.freeze(r.flask, true) == Refusal::None);
	CellState left;
	REQUIRE(w.retire(r.flask, &left) == Refusal::None);
	CHECK(w.meta(r.flask).retired);
	CHECK(w.amount(r.flask, builtin::water).mol() == 0.0);
	CHECK(w.freeze(r.flask, false) == Refusal::BadValue); // (for good)
	CHECK_FALSE(w.edges()[static_cast<size_t>(r.mouth.index)].open);
	Snapshot out;
	out.cells.push_back(left);
	const Totals gone = totals(table, out), none{};
	for (int k = 0; k < 60; ++k) {
		CHECK(check_all(table, r.sim.step(), {}).empty());
		CHECK(check_C9(start, none, gone, totals(table, w.snapshot())).empty());
	}
}

TEST_CASE("boundary: the boundary moves between steps only, as a connection does (Stage 61)") {
	Room r = make_room();
	World &w = r.sim.world();
	r.sim.step();
	// a transfer begins a step's ledger: until it closes, nothing crosses the boundary
	const SpeciesAmount gas{ builtin::nitrogen, mol(1e-6) };
	REQUIRE(w.transfer(r.mouth, r.air, std::span<const SpeciesAmount>(&gas, 1), Energy{}) == Refusal::None);
	CellMeta rigid;
	rigid.rigid = true;
	rigid.volume = FLASK;
	Refusal why = Refusal::None;
	CHECK_FALSE(w.add_cell("late", rigid, &why).index >= 0);
	CHECK(why == Refusal::MidStep);
	CHECK(w.freeze(r.flask, true) == Refusal::MidStep);
	CHECK(w.retire(r.flask) == Refusal::MidStep);
}

TEST_CASE("invariants: C9 fires on a ledger off by one atom, a gram, a charge or a quantum of energy (Stage 61)") {
	const SpeciesTable &table = builtin_species();
	Snapshot a, b;
	a.cells.push_back(template_flask());
	b.cells.push_back(template_flask());
	const Totals start = totals(table, a), supplied = totals(table, b), now = plus(start, supplied), none{};
	CHECK(check_C9(start, supplied, none, now).empty());
	CHECK(check_C9(now, none, supplied, start).empty());
	CHECK(minus(plus(start, supplied), supplied) == start);
	Totals off = now;
	off.atoms[0] += 1;
	CHECK(check_C9(start, supplied, none, off).size() == 1);
	off = now;
	off.energy += 1;
	CHECK(check_C9(start, supplied, none, off).size() == 1);
	off = now;
	off.charge -= 1;
	CHECK(check_C9(start, supplied, none, off).size() == 1);
	off = now;
	off.mass.add(1, 1);
	CHECK(check_C9(start, supplied, none, off).size() == 1);
}
