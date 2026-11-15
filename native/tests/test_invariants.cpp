// The invariant kernel witnesses what the primitives guarantee (GAME_SPEC §18).
// Each check is shown to fire on a trace corrupted in the way it exists to catch,
// and to stay silent on the corruptions that are another check's.
#include "chem/builtin_ids.hpp"
#include "chem/invariants.hpp"
#include "chem/sim.hpp"

#include <doctest/doctest.h>

#include <limits>

using namespace chem;

namespace {

// elements X; x(s), x2(g), x+(aq)
const SpeciesTable &table() {
	static const SpeciesTable t = [] {
		auto made = SpeciesTable::make({ { "X", 1000000 } },
				{
						{ "x", "X", Phase::Solid, 0, Atoms{ { 1 } } },
						{ "x2", "X2", Phase::Gas, 0, Atoms{ { 2 } } },
						{ "x_plus", "X", Phase::Aqueous, 1, Atoms{ { 1 } } },
				});
		REQUIRE(made.has_value());
		return *made;
	}();
	return t;
}
constexpr SpeciesId X{ 0 }, X2{ 1 }, X_PLUS{ 2 };
constexpr CellId A{ 0 }, B{ 1 };

// One honest step: 5 x and 7 quanta of energy from A to B, then 2 (2 x -> x2) in B.
StepTrace clean() {
	World::Builder b(table());
	const CellId a = b.add_cell("a");
	const CellId c = b.add_cell("b");
	const EdgeId edge = b.add_edge(a, c, PHASES_ALL);
	b.set_amount(a, X, Amount{ 100 }).set_amount(c, X2, Amount{ 3 }).set_energy(a, Energy{ 50 });
	auto world = b.build();
	REQUIRE(world.has_value());
	const Term dimerise[] = { { X, -2 }, { X2, 1 } };
	const auto reaction = Reaction::make(table(), "dimerise", dimerise);
	REQUIRE(reaction.has_value());
	const SpeciesAmount five[] = { { X, Amount{ 5 } } };
	REQUIRE(world->transfer(edge, a, five, Energy{ 7 }) == Refusal::None);
	REQUIRE(world->transform(c, *reaction, 2) == Refusal::None);
	return world->close_step();
}

bool has(const Violations &v, const char *id) {
	const std::string prefix = std::string(id) + ":";
	for (const std::string &m : v) {
		if (m.rfind(prefix, 0) == 0) {
			return true;
		}
	}
	return false;
}

int64_t &amount(Snapshot &s, CellId c, SpeciesId sp) {
	return s.cells[static_cast<size_t>(c.index)].amount[static_cast<size_t>(sp.index)];
}

} // namespace

TEST_CASE("invariants: an honest step violates nothing (Stage 1)") {
	const StepTrace t = clean();
	REQUIRE(t.ledger.size() == 2);
	const Violations v = check_all(table(), t);
	CHECK_MESSAGE(v.empty(), (v.empty() ? "" : v[0]));
	CHECK(totals(table(), t.before) == totals(table(), t.after));
}

TEST_CASE("invariants: a created atom is detected: C1, C2, C5 (Stage 1)") {
	StepTrace t = clean();
	amount(t.after, A, X) += 1;
	const Violations v = check_all(table(), t);
	CHECK(has(v, "C1"));
	CHECK(has(v, "C2"));
	CHECK(has(v, "C5"));
	CHECK_FALSE(has(v, "C3"));
	CHECK_FALSE(has(v, "C4"));
}

TEST_CASE("invariants: created energy is detected: C3, C5 (Stage 1)") {
	StepTrace t = clean();
	t.after.cells[1].energy += 1;
	const Violations v = check_all(table(), t);
	CHECK(has(v, "C3"));
	CHECK(has(v, "C5"));
	CHECK_FALSE(has(v, "C1"));
	CHECK_FALSE(has(v, "C2"));
}

TEST_CASE("invariants: matter moved outside the primitives is detected by the ledger alone: C5 (Stage 1)") {
	StepTrace t = clean();
	amount(t.after, A, X) -= 1; // the totals still agree
	amount(t.after, B, X) += 1;
	const Violations v = check_all(table(), t);
	CHECK(has(v, "C5"));
	CHECK_FALSE(has(v, "C1"));
	CHECK_FALSE(has(v, "C2"));
	CHECK_FALSE(has(v, "C3"));
}

TEST_CASE("invariants: a created charge is detected though every atom balances: C4, C8 (Stage 1)") {
	StepTrace t = clean();
	// x -> x+ in A, recorded in the ledger and in the state: no atom is created
	t.ledger.push_back(LedgerEntry{ LedgerEntry::Kind::Transform, EdgeId{}, A, CellId{}, { { X, Amount{ -1 } }, { X_PLUS, Amount{ 1 } } }, Energy{}, 1 });
	amount(t.after, A, X) -= 1;
	amount(t.after, A, X_PLUS) += 1;
	const Violations v = check_all(table(), t);
	CHECK(has(v, "C4"));
	CHECK(has(v, "C8"));
	CHECK_FALSE(has(v, "C1"));
	CHECK_FALSE(has(v, "C5"));
}

TEST_CASE("invariants: an unbalanced transform is detected even when recorded: C8, C1 (Stage 1)") {
	StepTrace t = clean();
	t.ledger.push_back(LedgerEntry{ LedgerEntry::Kind::Transform, EdgeId{}, A, CellId{}, { { X, Amount{ -1 } } }, Energy{}, 1 });
	amount(t.after, A, X) -= 1;
	const Violations v = check_all(table(), t);
	CHECK(has(v, "C8"));
	CHECK(has(v, "C1"));
	CHECK_FALSE(has(v, "C5")); // the ledger and the state agree; the entry itself is wrong
}

TEST_CASE("invariants: a negative amount is detected: C6 (Stage 1)") {
	StepTrace t = clean();
	// 200 x from A to B, recorded, though A holds 95: the totals and the ledger agree
	t.ledger.push_back(LedgerEntry{ LedgerEntry::Kind::Transfer, EdgeId{ 0 }, A, B, { { X, Amount{ 200 } } }, Energy{}, 0 });
	amount(t.after, A, X) -= 200;
	amount(t.after, B, X) += 200;
	const Violations v = check_all(table(), t);
	CHECK(has(v, "C6"));
	CHECK_FALSE(has(v, "C1"));
	CHECK_FALSE(has(v, "C5"));
}

TEST_CASE("invariants: a transfer off the topology is detected: C7 (Stage 1)") {
	const StepTrace honest = clean();
	{
		StepTrace t = honest;
		t.ledger[0].edge = EdgeId{ 9 }; // no such edge
		CHECK(has(check_all(table(), t), "C7"));
	}
	{
		StepTrace t = honest;
		t.edges[0].open = false; // a disconnected edge
		CHECK(has(check_all(table(), t), "C7"));
	}
	{
		StepTrace t = honest;
		t.edges[0].phases = PHASES_GAS; // a solid through a gas edge
		CHECK(has(check_all(table(), t), "C7"));
	}
	{
		StepTrace t = honest;
		t.edges[0].b = A; // the edge does not join the two cells
		CHECK(has(check_all(table(), t), "C7"));
	}
	{
		StepTrace t = honest;
		t.ledger[0].delta[0].amount = Amount{ -5 }; // a negative amount moved
		CHECK(has(check_all(table(), t), "C7"));
	}
	CHECK_FALSE(has(check_all(table(), honest), "C7"));
}

TEST_CASE("invariants: a trace of the wrong shape is one SHAPE violation; corrupt ids do not crash (Stage 1)") {
	{
		StepTrace t = clean();
		t.after.cells.pop_back();
		const Violations v = check_all(table(), t);
		REQUIRE(v.size() == 1);
		CHECK(has(v, "SHAPE"));
	}
	{
		StepTrace t = clean();
		t.after.cells[0].amount.push_back(0);
		CHECK(has(check_all(table(), t), "SHAPE"));
	}
	{
		StepTrace t = clean();
		t.ledger[0].from = CellId{ 40 };
		t.ledger[1].delta[0].species = SpeciesId{ 40 };
		const Violations v = check_all(table(), t);
		CHECK(has(v, "C5"));
		CHECK(has(v, "C8"));
	}
	{
		// extreme values wrap, they are not undefined (the sanitizer build runs this too)
		StepTrace t = clean();
		amount(t.after, A, X) = std::numeric_limits<int64_t>::max();
		amount(t.after, B, X2) = std::numeric_limits<int64_t>::max();
		t.after.cells[0].energy = std::numeric_limits<int64_t>::min();
		CHECK(has(check_all(table(), t), "C1"));
	}
}

TEST_CASE("invariants: the mass total is exact past 64 bits (Stage 1)") {
	// 4e18 quanta of a 1000 g/mol species: 4e27 units, far past an int64
	ExactMass big;
	big.add(4000000000000000000, 1000000000);
	ExactMass parts;
	for (int i = 0; i < 4; ++i) {
		parts.add(1000000000000000000, 1000000000);
	}
	CHECK(big == parts);
	CHECK(big.lo >= 0);
	CHECK(big.lo < (int64_t{ 1 } << 32));
	CHECK(big.grams() == doctest::Approx(4e9));
	ExactMass off = parts;
	off.add(1, 1); // one quantum of the lightest thing is seen
	CHECK_FALSE(off == big);
}

// ------------------------------------------------------------------ thermal

namespace {

// One honest thermal step: hot water (cell 0) warms cold water (cell 1) and the room (cell 2).
StepTrace clean_thermal() {
	bool ok = false;
	World::Builder b(builtin_species());
	const CellId hot = b.add_cell("hot");
	const CellId cold = b.add_cell("cold");
	const CellId room = b.add_reservoir("room", 293.15);
	b.add_edge(hot, cold, PHASES_NONE, 5.0);
	b.add_edge(hot, room, PHASES_NONE, 1.0);
	b.set_amount(hot, builtin::water, amount_from_mol(1.0, ok)).set_temperature(hot, 360.0);
	b.set_amount(cold, builtin::water, amount_from_mol(1.0, ok)).set_temperature(cold, 300.0);
	auto world = b.build();
	REQUIRE(world.has_value());
	Simulation sim(std::move(*world));
	return sim.step();
}

} // namespace

TEST_CASE("invariants: an honest thermal step violates nothing (Stage 2)") {
	const StepTrace t = clean_thermal();
	REQUIRE(t.thermal);
	REQUIRE(t.ledger.size() == 2);
	const Violations v = check_all(builtin_species(), t);
	CHECK_MESSAGE(v.empty(), (v.empty() ? "" : v[0]));
	CHECK(entropy_change(builtin_species(), t) > 0.0);
}

TEST_CASE("invariants: heat flowing uphill is detected though energy is conserved: T1, T2 (Stage 2)") {
	StepTrace t = clean_thermal();
	// the first entry reversed, in the ledger and in the state: cold water heats hot water
	const int64_t q = t.ledger[0].energy.quanta;
	REQUIRE(q > 0);
	t.ledger[0].energy.quanta = -q;
	t.after.cells[0].energy += 2 * q;
	t.after.cells[1].energy -= 2 * q;
	const Violations v = check_all(builtin_species(), t);
	CHECK(has(v, "T1"));
	CHECK(has(v, "T2"));
	CHECK_FALSE(has(v, "C3"));
	CHECK_FALSE(has(v, "C5"));
}

TEST_CASE("invariants: entropy destroyed outside the ledger is detected: T2 (Stage 2)") {
	StepTrace t = clean_thermal();
	t.after.cells[0].energy += 500000000000; // 500 J from the cold cell to the hot one, unrecorded
	t.after.cells[1].energy -= 500000000000;
	const Violations v = check_all(builtin_species(), t);
	CHECK(has(v, "T2"));
	CHECK(has(v, "C5"));
	CHECK_FALSE(has(v, "T1")); // every recorded transfer ran downhill
	CHECK_FALSE(has(v, "C3"));
}

TEST_CASE("invariants: a cell without a temperature is detected: T3 (Stage 2)") {
	{
		StepTrace t = clean_thermal(); // a species without thermodynamic data appears
		t.after.cells[1].amount[static_cast<size_t>(builtin::copper_sulfate_trihydrate.index)] = 1000;
		CHECK(has(check_all(builtin_species(), t), "T3"));
	}
	{
		StepTrace t = clean_thermal(); // a cell emptied: no heat capacity
		t.after.cells[1].amount[static_cast<size_t>(builtin::water.index)] = 0;
		CHECK(has(check_all(builtin_species(), t), "T3"));
	}
	{
		StepTrace t = clean_thermal(); // far below absolute zero
		t.after.cells[1].energy -= 1000000000000000;
		CHECK(has(check_all(builtin_species(), t), "T3"));
	}
}

TEST_CASE("invariants: a value that is not finite is detected: S1 (Stage 2)") {
	StepTrace t = clean_thermal();
	t.meta[2].reservoir_temperature = std::numeric_limits<double>::infinity();
	CHECK(has(check_all(builtin_species(), t), "S1"));
	StepTrace u = clean_thermal();
	u.meta.pop_back(); // and a thermal trace without its cells' kinds is malformed
	CHECK(has(check_all(builtin_species(), u), "SHAPE"));
}

// ---------------------------------------------------------------------- gas

namespace {

// One honest gas step: a flask of air, suddenly at 500 K, pushes air into the open air.
// cell 0: the flask (rigid), cell 1: the air (elastic)
StepTrace clean_gas() {
	bool ok = false;
	World::Builder b(builtin_species());
	const CellId flask = b.add_rigid_cell("flask", 250e-6);
	const CellId air = b.add_cell("air");
	b.add_edge(flask, air, PHASES_GAS);
	b.set_amount(flask, builtin::nitrogen, amount_from_mol(0.0104, ok)).set_amount(flask, builtin::glass, amount_from_mol(1.0, ok));
	b.set_temperature(flask, 500.0);
	b.set_amount(air, builtin::nitrogen, amount_from_mol(10.0, ok)).set_temperature(air, 293.15);
	auto world = b.build();
	REQUIRE(world.has_value());
	Simulation sim(std::move(*world));
	return sim.step();
}

} // namespace

TEST_CASE("invariants: an honest gas step violates nothing (Stage 4)") {
	const StepTrace t = clean_gas();
	REQUIRE(t.ledger.size() == 1);
	CHECK(t.ledger[0].from == CellId{ 0 });
	const Violations v = check_all(builtin_species(), t);
	CHECK_MESSAGE(v.empty(), (v.empty() ? "" : v[0]));
}

TEST_CASE("invariants: a rigid cell that is not exactly full is detected: V1 (Stage 4)") {
	const auto nitrogen = static_cast<size_t>(builtin::nitrogen.index);
	{
		StepTrace t = clean_gas(); // 1% more gas than fits
		t.after.cells[0].amount[nitrogen] += t.after.cells[0].amount[nitrogen] / 100;
		CHECK(has(check_all(builtin_species(), t), "V1"));
	}
	{
		StepTrace t = clean_gas(); // 1% less, and nothing says it was starved
		t.after.cells[0].amount[nitrogen] -= t.after.cells[0].amount[nitrogen] / 100;
		CHECK(has(check_all(builtin_species(), t), "V1"));
		t.starved[0] = 1; // marked starved, under-full is the model's declared limit
		CHECK_FALSE(has(check_all(builtin_species(), t), "V1"));
	}
}

TEST_CASE("invariants: a sealed rigid cell is detected: V3 (Stage 4)") {
	StepTrace t = clean_gas();
	t.edges[0].open = false;
	CHECK(has(check_all(builtin_species(), t), "V3"));
	StepTrace u = clean_gas();
	u.edges[0].phases = PHASES_LIQUID; // open, but not to gas
	CHECK(has(check_all(builtin_species(), u), "V3"));
}

TEST_CASE("invariants: gas flowing into a full flask is detected though everything is conserved: T1 (Stage 4)") {
	StepTrace t = clean_gas();
	// the transfer reversed, in the ledger and in the state: air pushed into the over-full flask
	LedgerEntry &entry = t.ledger[0];
	const auto nitrogen = static_cast<size_t>(builtin::nitrogen.index);
	const int64_t n = entry.delta[0].amount.quanta;
	const int64_t e = entry.energy.quanta;
	std::swap(entry.from, entry.to);
	t.after.cells[0].amount[nitrogen] += 2 * n;
	t.after.cells[1].amount[nitrogen] -= 2 * n;
	t.after.cells[0].energy += 2 * e;
	t.after.cells[1].energy -= 2 * e;
	const Violations v = check_all(builtin_species(), t);
	CHECK(has(v, "T1"));
	CHECK(has(v, "V1")); // and the flask is over-full
	CHECK_FALSE(has(v, "C1"));
	CHECK_FALSE(has(v, "C5"));
}

// ------------------------------------------------------------------- phases

namespace {

std::vector<Reaction> shipped_reactions() {
	return std::vector<Reaction>(builtin_reactions().begin(), builtin_reactions().end());
}

// One honest step of water under dry air: it evaporates to saturation.
StepTrace clean_phase(std::vector<Reaction> &equilibria) {
	bool ok = false;
	World::Builder b(builtin_species());
	const CellId cell = b.add_cell("cell");
	b.set_amount(cell, builtin::water, amount_from_mol(1.0, ok)).set_amount(cell, builtin::nitrogen, amount_from_mol(1.0, ok));
	b.set_temperature(cell, 298.15);
	auto world = b.build();
	REQUIRE(world.has_value());
	Simulation sim(std::move(*world), shipped_reactions());
	equilibria = sim.equilibria();
	return sim.step();
}

} // namespace

TEST_CASE("invariants: a cell away from its phase equilibrium is detected: P1 (Stage 5)") {
	std::vector<Reaction> equilibria;
	const StepTrace honest = clean_phase(equilibria);
	REQUIRE(equilibria.size() == 1);
	CHECK(check_all(builtin_species(), honest, equilibria).empty());
	const auto water = static_cast<size_t>(builtin::water.index);
	const auto steam = static_cast<size_t>(builtin::steam.index);
	{
		StepTrace t = honest; // a tenth of the vapour back in the liquid: the air is too dry
		const int64_t n = t.after.cells[0].amount[steam] / 10;
		t.after.cells[0].amount[steam] -= n;
		t.after.cells[0].amount[water] += n;
		const Violations v = check_all(builtin_species(), t, equilibria);
		CHECK(has(v, "P1"));
		CHECK_FALSE(has(v, "C1"));
	}
	{
		StepTrace t = honest; // more vapour than the air can hold, over the liquid
		const int64_t n = t.after.cells[0].amount[steam] / 10;
		t.after.cells[0].amount[steam] += n;
		t.after.cells[0].amount[water] -= n;
		CHECK(has(check_all(builtin_species(), t, equilibria), "P1"));
	}
	{
		StepTrace t = honest; // the step undone: liquid under dry air
		t.after = t.before;
		t.ledger.clear();
		CHECK(has(check_all(builtin_species(), t, equilibria), "P1"));
	}
	CHECK_FALSE(has(check_all(builtin_species(), honest), "P1")); // not asked: no reactions given
}

TEST_CASE("invariants: more liquid than the vessel holds is detected: V2 (Stage 5)") {
	StepTrace t = clean_gas();
	t.after.cells[0].amount[static_cast<size_t>(builtin::water.index)] = 20000000000000; // 20 mol: 361 mL in 250
	const Violations v = check_all(builtin_species(), t);
	CHECK(has(v, "V2"));
	CHECK(has(v, "V1"));
}

// ---------------------------------------------------------------- solutions

TEST_CASE("invariants: a solid without particles, or particles without a solid, is detected: X1 (Stage 6)") {
	const auto oxide = static_cast<size_t>(builtin::copper_oxide.index);
	const auto water = static_cast<size_t>(builtin::water.index);
	StepTrace honest = clean_thermal();
	REQUIRE(honest.after.cells[0].particles.size() == honest.after.cells[0].amount.size());
	CHECK_FALSE(has(check_all(builtin_species(), honest), "X1"));
	{
		StepTrace t = honest; // particles of a solid that is not there
		t.after.cells[0].particles[oxide] = 3;
		CHECK(has(check_all(builtin_species(), t), "X1"));
	}
	{
		StepTrace t = honest; // a solid in no particles
		t.after.cells[0].amount[oxide] = 1000;
		CHECK(has(check_all(builtin_species(), t), "X1"));
		t.after.cells[0].particles[oxide] = 1;
		CHECK_FALSE(has(check_all(builtin_species(), t), "X1"));
	}
	{
		StepTrace t = honest; // particles of a liquid
		t.after.cells[0].particles[water] = 1;
		CHECK(has(check_all(builtin_species(), t), "X1"));
	}
}

TEST_CASE("invariants: a dissolved species with no liquid is detected: X2 (Stage 6)") {
	StepTrace t = clean_thermal();
	t.after.cells[1].amount[static_cast<size_t>(builtin::copper_sulfate_aq.index)] = 1000000;
	CHECK_FALSE(has(check_all(builtin_species(), t), "X2")); // it has water to be in
	t.after.cells[1].amount[static_cast<size_t>(builtin::water.index)] = 0;
	t.after.cells[1].amount[static_cast<size_t>(builtin::glass.index)] = 1000000000000; // still a temperature
	CHECK(has(check_all(builtin_species(), t), "X2"));
}
