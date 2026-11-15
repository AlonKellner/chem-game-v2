// The world's two primitives (GAME_SPEC §4.2, §4.3): they move and convert,
// exactly, and a refused call changes nothing.
#include "chem/builtin_ids.hpp"
#include "chem/invariants.hpp"
#include "chem/world.hpp"

#include <doctest/doctest.h>

#include <limits>

using namespace chem;

namespace {

struct Bench {
	World world;
	CellId flask, balloon, room;
	EdgeId mouth; // flask - balloon, gas
	EdgeId wall; // flask - room, heat only
};

Bench bench() {
	World::Builder b(builtin_species());
	const CellId flask = b.add_cell("flask");
	const CellId balloon = b.add_cell("balloon");
	const CellId room = b.add_cell("room");
	const EdgeId mouth = b.add_edge(flask, balloon, PHASES_GAS);
	const EdgeId wall = b.add_edge(flask, room, PHASES_NONE);
	b.set_amount(flask, builtin::malachite, Amount{ 1000 });
	b.set_amount(flask, builtin::nitrogen, Amount{ 500 });
	b.set_energy(flask, Energy{ -4000 });
	b.set_energy(room, Energy{ 90000 });
	std::string why;
	auto world = b.build(&why);
	REQUIRE_MESSAGE(world.has_value(), why);
	return Bench{ std::move(*world), flask, balloon, room, mouth, wall };
}

const Reaction &decomposition() {
	return builtin_reactions()[static_cast<size_t>(builtin::reaction::malachite_decomposition)];
}

} // namespace

TEST_CASE("world: a transfer takes from one cell exactly what it gives the other (Stage 1)") {
	Bench t = bench();
	const SpeciesAmount gas[] = { { builtin::nitrogen, Amount{ 120 } } };
	CHECK(t.world.transfer(t.mouth, t.flask, gas, Energy{ 35 }) == Refusal::None);
	CHECK(t.world.amount(t.flask, builtin::nitrogen) == Amount{ 380 });
	CHECK(t.world.amount(t.balloon, builtin::nitrogen) == Amount{ 120 });
	CHECK(t.world.energy(t.flask) == Energy{ -4035 });
	CHECK(t.world.energy(t.balloon) == Energy{ 35 });
	// heat alone, against the direction named: the room warms the flask
	CHECK(t.world.transfer(t.wall, t.flask, {}, Energy{ -500 }) == Refusal::None);
	CHECK(t.world.energy(t.flask) == Energy{ -3535 });
	CHECK(t.world.energy(t.room) == Energy{ 89500 });
	// a species listed twice adds up
	const SpeciesAmount twice[] = { { builtin::nitrogen, Amount{ 100 } }, { builtin::nitrogen, Amount{ 80 } } };
	CHECK(t.world.transfer(t.mouth, t.flask, twice, Energy{}) == Refusal::None);
	CHECK(t.world.amount(t.flask, builtin::nitrogen) == Amount{ 200 });
	CHECK(check_all(builtin_species(), t.world.close_step()).empty());
}

TEST_CASE("world: a transform converts by the reaction's numbers, energy unchanged (Stage 1)") {
	Bench t = bench();
	CHECK(t.world.transform(t.flask, decomposition(), 300) == Refusal::None);
	CHECK(t.world.amount(t.flask, builtin::malachite) == Amount{ 700 });
	CHECK(t.world.amount(t.flask, builtin::copper_oxide) == Amount{ 600 });
	CHECK(t.world.amount(t.flask, builtin::carbon_dioxide) == Amount{ 300 });
	CHECK(t.world.amount(t.flask, builtin::steam) == Amount{ 300 });
	CHECK(t.world.energy(t.flask) == Energy{ -4000 });
	// and backwards
	CHECK(t.world.transform(t.flask, decomposition(), -300) == Refusal::None);
	CHECK(t.world.amount(t.flask, builtin::malachite) == Amount{ 1000 });
	CHECK(t.world.amount(t.flask, builtin::copper_oxide) == Amount{ 0 });
	CHECK(check_all(builtin_species(), t.world.close_step()).empty());
}

TEST_CASE("world: a refused call names its reason and changes nothing (Stage 1)") {
	Bench t = bench();
	const Snapshot before = t.world.snapshot();
	const SpeciesAmount n2[] = { { builtin::nitrogen, Amount{ 10 } } };
	const SpeciesAmount too_much[] = { { builtin::nitrogen, Amount{ 501 } } };
	const SpeciesAmount split_too_much[] = { { builtin::nitrogen, Amount{ 300 } }, { builtin::nitrogen, Amount{ 300 } } };
	const SpeciesAmount solid[] = { { builtin::malachite, Amount{ 10 } } };
	const SpeciesAmount negative[] = { { builtin::nitrogen, Amount{ -1 } } };
	const SpeciesAmount unknown[] = { { SpeciesId{ 999 }, Amount{ 1 } } };

	CHECK(t.world.transfer(EdgeId{ 7 }, t.flask, n2, Energy{}) == Refusal::UnknownEdge);
	CHECK(t.world.transfer(t.mouth, t.room, n2, Energy{}) == Refusal::NotOnEdge);
	CHECK(t.world.transfer(t.mouth, t.flask, too_much, Energy{}) == Refusal::Insufficient);
	CHECK(t.world.transfer(t.mouth, t.flask, split_too_much, Energy{}) == Refusal::Insufficient);
	CHECK(t.world.transfer(t.mouth, t.flask, solid, Energy{}) == Refusal::PhaseNotAllowed); // a gas edge
	CHECK(t.world.transfer(t.wall, t.flask, n2, Energy{}) == Refusal::PhaseNotAllowed); // a heat-only edge
	CHECK(t.world.transfer(t.mouth, t.flask, negative, Energy{}) == Refusal::NegativeAmount);
	CHECK(t.world.transfer(t.mouth, t.flask, unknown, Energy{}) == Refusal::UnknownSpecies);
	CHECK(t.world.transfer(t.wall, t.flask, {}, Energy{ std::numeric_limits<int64_t>::max() }) == Refusal::Overflow);
	CHECK(t.world.transfer(t.wall, t.flask, {}, Energy{ std::numeric_limits<int64_t>::min() }) == Refusal::Overflow);

	CHECK(t.world.transform(CellId{ 9 }, decomposition(), 1) == Refusal::UnknownCell);
	CHECK(t.world.transform(t.flask, decomposition(), 1001) == Refusal::Insufficient);
	CHECK(t.world.transform(t.flask, decomposition(), -1) == Refusal::Insufficient); // no products to run it backwards
	CHECK(t.world.transform(t.flask, decomposition(), std::numeric_limits<int64_t>::max()) == Refusal::Insufficient);
	// backwards by the largest extent: the malachite made would not fit an int64
	CHECK(t.world.transform(t.flask, decomposition(), -std::numeric_limits<int64_t>::max()) == Refusal::Overflow);

	CHECK(t.world.snapshot() == before);
	const StepTrace trace = t.world.close_step();
	CHECK(trace.ledger.empty());
	CHECK(check_all(builtin_species(), trace).empty());
	for (int r = 0; r <= static_cast<int>(Refusal::WouldSeal); ++r) {
		CHECK(std::string(refusal_text(static_cast<Refusal>(r))) != "unknown refusal");
	}
}

TEST_CASE("world: a reaction of another table is refused (Stage 1)") {
	Bench t = bench();
	std::string why;
	const auto other = SpeciesTable::make({ { "X", 1000000 } },
			{ { "x", "X", Phase::Solid, 0, Atoms{ { 1 } } }, { "x2", "X2", Phase::Gas, 0, Atoms{ { 2 } } } }, &why);
	REQUIRE(other.has_value());
	const Term terms[] = { { SpeciesId{ 0 }, -2 }, { SpeciesId{ 1 }, 1 } };
	const auto foreign = Reaction::make(*other, "dimerise", terms, &why);
	REQUIRE(foreign.has_value());
	CHECK(t.world.transform(t.flask, *foreign, 1) == Refusal::ForeignReaction);
}

TEST_CASE("world: the topology changes between steps only (Stage 1)") {
	Bench t = bench();
	EdgeId vent;
	CHECK(t.world.connect(t.balloon, t.room, PHASES_GAS, &vent) == Refusal::None);
	CHECK(t.world.connect(t.flask, t.flask, PHASES_GAS) == Refusal::SameCell);
	CHECK(t.world.connect(t.flask, CellId{ 9 }, PHASES_GAS) == Refusal::UnknownCell);
	const SpeciesAmount n2[] = { { builtin::nitrogen, Amount{ 10 } } };
	CHECK(t.world.transfer(t.mouth, t.flask, n2, Energy{}) == Refusal::None);
	CHECK(t.world.disconnect(t.mouth) == Refusal::MidStep);
	CHECK(t.world.connect(t.flask, t.room, PHASES_GAS) == Refusal::MidStep);
	CHECK(check_all(builtin_species(), t.world.close_step()).empty());
	CHECK(t.world.disconnect(t.mouth) == Refusal::None);
	CHECK(t.world.disconnect(t.mouth) == Refusal::UnknownEdge);
	CHECK(t.world.transfer(t.mouth, t.flask, n2, Energy{}) == Refusal::UnknownEdge);
	CHECK(t.world.transfer(vent, t.balloon, n2, Energy{}) == Refusal::None);
	CHECK(t.world.amount(t.room, builtin::nitrogen) == Amount{ 10 });
	CHECK(t.world.step() == 1);
}

TEST_CASE("world: malformed initial conditions are refused (Stage 1)") {
	std::string why;
	{
		World::Builder b(builtin_species());
		const CellId a = b.add_cell("a");
		b.set_amount(a, builtin::water, Amount{ -1 });
		CHECK_FALSE(b.build(&why));
	}
	{
		World::Builder b(builtin_species());
		const CellId a = b.add_cell("a");
		b.add_edge(a, a, PHASES_ALL);
		CHECK_FALSE(b.build(&why));
	}
	{
		World::Builder b(builtin_species());
		b.set_amount(CellId{ 3 }, builtin::water, Amount{ 1 });
		CHECK_FALSE(b.build(&why));
	}
	{
		// two cells whose atoms together overflow the totals
		World::Builder b(builtin_species());
		const CellId a = b.add_cell("a");
		const CellId c = b.add_cell("c");
		b.set_amount(a, builtin::water, Amount{ std::numeric_limits<int64_t>::max() / 2 });
		b.set_amount(c, builtin::water, Amount{ std::numeric_limits<int64_t>::max() / 2 });
		CHECK_FALSE(b.build(&why));
		CHECK(why.find("integer range") != std::string::npos);
	}
}

TEST_CASE("world: 5 g of malachite decomposes into the masses its formula fixes (Stage 1)") {
	// GAME_SPEC §17, stage 1: the demo's numbers follow from the atoms, not from a script.
	const SpeciesTable &table = builtin_species();
	bool ok = false;
	const Amount n = amount_from_mol(5.0 / 221.114, ok);
	REQUIRE(ok);
	World::Builder b(table);
	const CellId flask = b.add_cell("flask");
	b.set_amount(flask, builtin::malachite, n);
	auto world = b.build();
	REQUIRE(world.has_value());
	const Totals genesis = totals(table, world->snapshot());
	CHECK(genesis.mass.grams() == doctest::Approx(5.0).epsilon(1e-9));

	REQUIRE(world->transform(flask, decomposition(), n.quanta) == Refusal::None);
	const StepTrace trace = world->close_step();
	CHECK(check_all(table, trace).empty());
	CHECK(totals(table, trace.after) == genesis); // every atom, the mass and the energy: the same integers

	const auto grams = [&](SpeciesId s) { return world->amount(flask, s).mol() * static_cast<double>(table.molar_mass_ug(s)) * 1e-6; };
	CHECK(world->amount(flask, builtin::malachite) == Amount{ 0 });
	CHECK(grams(builtin::carbon_dioxide) == doctest::Approx(0.995).epsilon(0.001)); // not the 2 g of the original spec
	CHECK(grams(builtin::copper_oxide) == doctest::Approx(3.597).epsilon(0.001));
	CHECK(grams(builtin::steam) == doctest::Approx(0.407).epsilon(0.002));
	CHECK(grams(builtin::carbon_dioxide) + grams(builtin::copper_oxide) + grams(builtin::steam) == doctest::Approx(5.0).epsilon(1e-9));
}
