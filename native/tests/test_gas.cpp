// Gas volumes and flow (GAME_SPEC §6, the demo's subset): rigid cells stay
// exactly full at one atmosphere; elastic cells take what they are given.
#include "chem/builtin_ids.hpp"
#include "chem/invariants.hpp"
#include "chem/sim.hpp"
#include "chem/thermo.hpp"

#include <doctest/doctest.h>

#include <cmath>
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

double temperature(const World &world, CellId cell) {
	double kelvin = 0.0;
	REQUIRE(world.temperature(cell, kelvin));
	return kelvin;
}

World build(World::Builder &b) {
	std::string why;
	auto world = b.build(&why);
	REQUIRE_MESSAGE(world.has_value(), why);
	return std::move(*world);
}

// the moles of gas that fill a volume at a temperature and one atmosphere
double fills(double volume, double kelvin) {
	return P_ATMOSPHERE * volume / (GAS_CONSTANT * kelvin);
}

double gas_mol(const World &world, CellId cell) {
	double total = 0.0;
	for (int s = 0; s < world.species().species_count(); ++s) {
		if (world.species().species(SpeciesId{ s }).phase == Phase::Gas) {
			total += world.amount(cell, SpeciesId{ s }).mol();
		}
	}
	return total;
}

double volume(const World &world, CellId cell) {
	const Fill fill = fill_of(world.species(), world.snapshot_cell(cell), temperature(world, cell));
	REQUIRE(fill.defined);
	return fill.gas + fill.condensed;
}

void add_air(World::Builder &b, CellId cell, double n) {
	b.set_amount(cell, builtin::nitrogen, mol(0.79 * n)).set_amount(cell, builtin::oxygen, mol(0.21 * n));
}

std::vector<Reaction> shipped() {
	return std::vector<Reaction>(builtin_reactions().begin(), builtin_reactions().end());
}

// A flask of air open to the room's air, with a hot and a cold reservoir to switch between.
struct Open {
	CellId flask, air, hot, cold;
	EdgeId mouth, heater, cooler;
	World world;
};
Open open_flask(double hot_kelvin) {
	World::Builder b(builtin_species());
	const CellId flask = b.add_rigid_cell("flask", FLASK);
	const CellId air = b.add_cell("air");
	const CellId hot = b.add_reservoir("hot", hot_kelvin);
	const CellId cold = b.add_reservoir("cold", 293.15);
	const EdgeId mouth = b.add_edge(flask, air, PHASES_GAS);
	const EdgeId heater = b.add_edge(flask, hot, PHASES_NONE, 50.0);
	const EdgeId cooler = b.add_edge(flask, cold, PHASES_NONE, 0.0);
	b.add_edge(air, cold, PHASES_NONE, 1000.0);
	add_air(b, flask, fills(FLASK, 293.15));
	b.set_amount(flask, builtin::glass, mol(1.0)).set_temperature(flask, 293.15);
	add_air(b, air, 100.0);
	b.set_temperature(air, 293.15);
	return Open{ flask, air, hot, cold, mouth, heater, cooler, build(b) };
}

} // namespace

TEST_CASE("gas: a volume is the ideal gas law at one atmosphere plus the solids and liquids (Stage 4)") {
	World::Builder b(builtin_species());
	const CellId cell = b.add_cell("cell");
	b.set_amount(cell, builtin::nitrogen, mol(1.0)).set_amount(cell, builtin::water, mol(1.0)).set_amount(cell, builtin::glass, mol(3.0));
	b.set_temperature(cell, 273.15);
	const World world = build(b);
	const Fill fill = fill_of(builtin_species(), world.snapshot_cell(cell), temperature(world, cell));
	REQUIRE(fill.defined);
	CHECK(fill.gas == doctest::Approx(22.414e-3).epsilon(1e-4)); // the molar volume of a gas at 0 C
	CHECK(fill.condensed == doctest::Approx(18.015 / 0.997048 * 1e-6).epsilon(1e-9)); // 18.07 mL of water; the glass takes no room
}

TEST_CASE("gas: heated air leaves an open flask: what stays is T0/T of it (Stage 4)") {
	for (const double hot : { 400.0, 600.0, 900.0 }) {
		Open t = open_flask(hot);
		Simulation sim(std::move(t.world));
		sim.set_checking(true);
		const double start = gas_mol(sim.world(), t.flask);
		const Totals genesis = totals(builtin_species(), sim.world().snapshot());
		for (int step = 0; step < 6000; ++step) {
			sim.step();
		}
		const double kelvin = temperature(sim.world(), t.flask);
		CAPTURE(hot);
		CHECK(kelvin == doctest::Approx(hot).epsilon(1e-6));
		CHECK(gas_mol(sim.world(), t.flask) / start == doctest::Approx(293.15 / kelvin).epsilon(1e-7)); // expelled: 1 - T0/T
		CHECK(volume(sim.world(), t.flask) == doctest::Approx(FLASK).epsilon(1e-9));
		CHECK(totals(builtin_species(), sim.world().snapshot()) == genesis);
	}
}

TEST_CASE("gas: a cooling flask draws the air back, and a settled flask is static (Stage 4)") {
	Open t = open_flask(700.0);
	Simulation sim(std::move(t.world));
	sim.set_checking(true);
	const double start = gas_mol(sim.world(), t.flask);
	for (int step = 0; step < 6000; ++step) {
		sim.step();
	}
	CHECK(gas_mol(sim.world(), t.flask) < 0.5 * start);
	REQUIRE(sim.world().set_conductance(t.heater, 0.0) == Refusal::None);
	REQUIRE(sim.world().set_conductance(t.cooler, 50.0) == Refusal::None);
	for (int step = 0; step < 12000; ++step) {
		sim.step();
	}
	CHECK(gas_mol(sim.world(), t.flask) == doctest::Approx(start).epsilon(1e-7));
	CHECK(temperature(sim.world(), t.flask) == doctest::Approx(293.15).epsilon(1e-8));
	// D3: at rest nothing moves
	const Snapshot rest = sim.world().snapshot();
	for (int step = 0; step < 20000; ++step) {
		REQUIRE(sim.step().ledger.empty());
	}
	CHECK(sim.world().snapshot() == rest);
}

TEST_CASE("gas: a chain of rigid cells settles in one step, each exactly full (Stage 4)") {
	// flask -> condenser tube -> open air; the flask suddenly hot
	World::Builder b(builtin_species());
	const CellId flask = b.add_rigid_cell("flask", FLASK);
	const CellId tube = b.add_rigid_cell("tube", 50e-6);
	const CellId air = b.add_cell("air");
	b.add_edge(flask, tube, PHASES_GAS);
	b.add_edge(tube, air, PHASES_GAS);
	add_air(b, flask, fills(FLASK, 293.15)); // the amount for 20 C, but the flask is at 500 K: over-full
	b.set_amount(flask, builtin::glass, mol(1.0)).set_temperature(flask, 500.0);
	add_air(b, tube, fills(50e-6, 293.15));
	b.set_amount(tube, builtin::glass, mol(0.3)).set_temperature(tube, 293.15);
	add_air(b, air, 100.0);
	b.set_temperature(air, 293.15);
	Simulation sim(build(b));
	sim.set_checking(true);
	const StepTrace trace = sim.step();
	CHECK(trace.ledger.size() == 2); // flask -> tube, tube -> air
	CHECK(volume(sim.world(), flask) == doctest::Approx(FLASK).epsilon(1e-9));
	CHECK(volume(sim.world(), tube) == doctest::Approx(50e-6).epsilon(1e-9));
	CHECK(temperature(sim.world(), flask) == doctest::Approx(500.0).epsilon(1e-12)); // expelling does not change it
	CHECK(temperature(sim.world(), tube) > 293.15); // hot gas came through
	CHECK(sim.step().ledger.empty());
}

TEST_CASE("gas: malachite over a flame fills a balloon with its gases and the flask's air (Stage 4)") {
	World::Builder b(builtin_species());
	const CellId flask = b.add_rigid_cell("flask", FLASK);
	const CellId balloon = b.add_cell("balloon");
	const CellId flame = b.add_reservoir("flame", 2200.0);
	const CellId room = b.add_reservoir("room", 293.15);
	b.add_edge(flask, balloon, PHASES_GAS);
	const EdgeId burner = b.add_edge(flask, flame, PHASES_NONE, 0.2);
	b.add_edge(flask, room, PHASES_NONE, 0.9);
	b.add_edge(balloon, room, PHASES_NONE, 0.5);
	const double decomposes = 5.0 / 221.114;
	b.set_amount(flask, builtin::malachite, mol(decomposes)).set_amount(flask, builtin::glass, mol(100.0 / 60.083));
	add_air(b, flask, fills(FLASK - decomposes * 221.114 / 3.8 * 1e-6, 293.15));
	b.set_temperature(flask, 293.15);
	b.set_amount(balloon, builtin::latex, mol(2.0 / 68.119)).set_temperature(balloon, 293.15);
	Simulation sim(build(b), shipped());
	sim.set_checking(true);
	const Totals genesis = totals(builtin_species(), sim.world().snapshot());
	const double air = gas_mol(sim.world(), flask);
	CHECK(volume(sim.world(), balloon) == 0.0); // deflated

	for (int step = 0; step < 72000; ++step) { // twenty minutes over the flame
		sim.step();
	}
	CHECK(sim.world().amount(flask, builtin::malachite).mol() < 1e-4);
	// every mole of gas is in the flask or the balloon: the air, and two per mole decomposed
	const double made = 2.0 * (decomposes - sim.world().amount(flask, builtin::malachite).mol());
	CHECK(gas_mol(sim.world(), flask) + gas_mol(sim.world(), balloon) == doctest::Approx(air + made).epsilon(1e-9));
	CHECK(volume(sim.world(), flask) == doctest::Approx(FLASK).epsilon(1e-9));
	CHECK(sim.world().amount(balloon, builtin::carbon_dioxide).mol() > 0.85 * decomposes); // most of the CO2 was pushed out
	CHECK(sim.world().amount(balloon, builtin::nitrogen).mol() > 0.0); // and the flask's air before it
	const double inflated = volume(sim.world(), balloon);
	CHECK(inflated > 0.5e-3); // over half a litre

	// burner off: the flask cools and draws some of the mixture back
	REQUIRE(sim.world().set_conductance(burner, 0.0) == Refusal::None);
	for (int step = 0; step < 72000; ++step) {
		sim.step();
	}
	CHECK(temperature(sim.world(), flask) < 294.0);
	CHECK(volume(sim.world(), balloon) < inflated);
	CHECK(sim.world().amount(flask, builtin::carbon_dioxide).mol() > 0.0);
	CHECK(volume(sim.world(), flask) == doctest::Approx(FLASK).epsilon(1e-9));
	CHECK(totals(builtin_species(), sim.world().snapshot()) == genesis);
	MESSAGE("balloon: " << inflated * 1e3 << " L hot, " << volume(sim.world(), balloon) * 1e3 << " L after the flask cooled, "
						<< 100.0 * sim.world().amount(balloon, builtin::carbon_dioxide).mol() / gas_mol(sim.world(), balloon) << " mol% CO2");
}

TEST_CASE("gas: a flask that cannot draw enough is left under-full and marked starved (Stage 4)") {
	// a flask at 20 C on an empty balloon, then cooled: there is no gas to draw
	World::Builder b(builtin_species());
	const CellId flask = b.add_rigid_cell("flask", FLASK);
	const CellId balloon = b.add_cell("balloon");
	const CellId ice = b.add_reservoir("ice", 250.0);
	b.add_edge(flask, balloon, PHASES_GAS);
	b.add_edge(flask, ice, PHASES_NONE, 5.0);
	add_air(b, flask, fills(FLASK, 293.15));
	b.set_amount(flask, builtin::glass, mol(1.0)).set_temperature(flask, 293.15);
	b.set_amount(balloon, builtin::latex, mol(0.03)).set_temperature(balloon, 293.15);
	Simulation sim(build(b));
	sim.set_checking(true); // V1 accepts the starved cell
	StepTrace trace;
	for (int step = 0; step < 600; ++step) {
		trace = sim.step();
	}
	REQUIRE(trace.starved.size() == 3);
	CHECK(trace.starved[static_cast<size_t>(flask.index)] == 1);
	CHECK(volume(sim.world(), flask) < FLASK * 0.99);
	CHECK(gas_mol(sim.world(), balloon) == 0.0);
}

TEST_CASE("gas: a rigid cell is never sealed, and a wall never moves or reacts (Stage 4)") {
	std::string why;
	{
		World::Builder b(builtin_species()); // no gas path at all
		const CellId flask = b.add_rigid_cell("flask", FLASK);
		b.set_amount(flask, builtin::nitrogen, mol(0.01));
		CHECK_FALSE(b.build(&why));
		CHECK(why.find("gas path") != std::string::npos);
	}
	{
		World::Builder b(builtin_species()); // a liquid edge is not a gas path
		const CellId flask = b.add_rigid_cell("flask", FLASK);
		const CellId air = b.add_cell("air");
		b.add_edge(flask, air, PHASES_LIQUID);
		CHECK_FALSE(b.build(&why));
	}
	{
		World::Builder b(builtin_species()); // more water than the flask holds
		const CellId flask = b.add_rigid_cell("flask", FLASK);
		const CellId air = b.add_cell("air");
		b.add_edge(flask, air, PHASES_GAS);
		b.set_amount(flask, builtin::water, mol(20.0));
		CHECK_FALSE(b.build(&why));
		CHECK(why.find("do not fit") != std::string::npos);
	}
	{
		World::Builder b(builtin_species());
		b.add_rigid_cell("flask", -1.0);
		CHECK_FALSE(b.build(&why));
	}
	{
		World::Builder b(builtin_species()); // a solid whose density is not known yet
		const CellId flask = b.add_rigid_cell("flask", FLASK);
		const CellId air = b.add_cell("air");
		b.add_edge(flask, air, PHASES_GAS);
		b.set_amount(flask, builtin::copper_sulfate_pentahydrate, mol(0.01));
		CHECK_FALSE(b.build(&why));
		CHECK(why.find("molar volume") != std::string::npos);
	}
	World::Builder b(builtin_species());
	const CellId flask = b.add_rigid_cell("flask", FLASK);
	const CellId tube = b.add_rigid_cell("tube", 50e-6);
	const CellId air = b.add_cell("air");
	const EdgeId neck = b.add_edge(flask, tube, PHASES_ALL);
	const EdgeId vent = b.add_edge(tube, air, PHASES_GAS);
	b.set_amount(flask, builtin::glass, mol(1.0));
	World world = build(b);
	CHECK(world.disconnect(vent) == Refusal::WouldSeal); // both cells would be sealed
	CHECK(world.disconnect(neck) == Refusal::WouldSeal); // the flask would be
	EdgeId mouth;
	REQUIRE(world.connect(flask, air, PHASES_GAS, &mouth) == Refusal::None);
	CHECK(world.disconnect(neck) == Refusal::None); // now it has its own way out
	CHECK(world.disconnect(mouth) == Refusal::WouldSeal);
	const SpeciesAmount glass[] = { { builtin::glass, Amount{ 10 } } };
	REQUIRE(world.connect(flask, tube, PHASES_ALL) == Refusal::None);
	CHECK(world.transfer(EdgeId{ 3 }, flask, glass, Energy{}) == Refusal::Wall);
	const Term melt[] = { { builtin::glass, -1 }, { builtin::copper_oxide, 1 } };
	CHECK_FALSE(Reaction::make(builtin_species(), "melt", melt, &why));
	CHECK(why.find("wall") != std::string::npos);
}
