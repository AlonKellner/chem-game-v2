// The invariant kernel witnesses what the primitives guarantee (GAME_SPEC §18).
// Each check is shown to fire on a trace corrupted in the way it exists to catch,
// and to stay silent on the corruptions that are another check's.
#include "chem/invariants.hpp"

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
	t.ledger.push_back(LedgerEntry{ LedgerEntry::Kind::Transform, EdgeId{}, A, CellId{}, { { X, Amount{ -1 } }, { X_PLUS, Amount{ 1 } } }, Energy{} });
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
	t.ledger.push_back(LedgerEntry{ LedgerEntry::Kind::Transform, EdgeId{}, A, CellId{}, { { X, Amount{ -1 } } }, Energy{} });
	amount(t.after, A, X) -= 1;
	const Violations v = check_all(table(), t);
	CHECK(has(v, "C8"));
	CHECK(has(v, "C1"));
	CHECK_FALSE(has(v, "C5")); // the ledger and the state agree; the entry itself is wrong
}

TEST_CASE("invariants: a negative amount is detected: C6 (Stage 1)") {
	StepTrace t = clean();
	// 200 x from A to B, recorded, though A holds 95: the totals and the ledger agree
	t.ledger.push_back(LedgerEntry{ LedgerEntry::Kind::Transfer, EdgeId{ 0 }, A, B, { { X, Amount{ 200 } } }, Energy{} });
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
