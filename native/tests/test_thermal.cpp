// Temperature, heat conduction and entropy (GAME_SPEC §4.2, §4.6, §5).
#include "chem/builtin_ids.hpp"
#include "chem/invariants.hpp"
#include "chem/sim.hpp"
#include "chem/thermo.hpp"

#include <doctest/doctest.h>

#include <cmath>
#include <limits>

using namespace chem;

namespace {

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

const SpeciesDef &def(SpeciesId s) {
	return builtin_species().species(s);
}

} // namespace

TEST_CASE("thermal: the data oracle: water's heat of vaporisation at 25 C follows from the table (Stage 2)") {
	// K1: Hf(steam) - Hf(water) against the CODATA value 43.99 +- 0.07 kJ/mol
	CHECK(def(builtin::steam).hf - def(builtin::water).hf == doctest::Approx(44004.0).epsilon(1e-6));
	CHECK(def(builtin::steam).hf - def(builtin::water).hf == doctest::Approx(43990.0).epsilon(0.002));
	// liquid water's heat capacity, 4.18 J/g/K
	CHECK(def(builtin::water).cp / 18.015 == doctest::Approx(4.184).epsilon(0.002));
	// the species the later stages still have to verify have no data yet
	CHECK_FALSE(def(builtin::copper_sulfate_pentahydrate).has_thermo);
	CHECK(def(builtin::copper_oxide).has_thermo);
}

TEST_CASE("thermal: a cell built at a temperature has it, and its heat capacity is the sum of its parts (Stage 2)") {
	World::Builder b(builtin_species());
	const CellId flask = b.add_cell("flask");
	b.set_amount(flask, builtin::water, mol(5.0)).set_amount(flask, builtin::glass, mol(1.5)).set_temperature(flask, 353.15);
	const World world = build(b);
	CHECK(world.thermal());
	const Thermal t = thermal_state(builtin_species(), world.snapshot_cell(flask));
	REQUIRE(t.defined);
	CHECK(t.heat_capacity == doctest::Approx(5.0 * 75.375 + 1.5 * 48.066).epsilon(1e-12));
	CHECK(std::abs(t.temperature - 353.15) < 1e-9); // the energy is whole quanta
	// the enthalpy is far from zero: formation enthalpies are in it
	CHECK(world.energy(flask).joule() == doctest::Approx(5.0 * (-285830.0 + 75.375 * 55.0) + 1.5 * (-910860.0 + 48.066 * 55.0)).epsilon(1e-12));
}

TEST_CASE("thermal: worlds that cannot have temperatures are refused (Stage 2)") {
	std::string why;
	{
		World::Builder b(builtin_species()); // a species without verified data
		const CellId c = b.add_cell("c");
		b.set_amount(c, builtin::copper_sulfate_pentahydrate, mol(0.02)).set_temperature(c, 300.0);
		CHECK_FALSE(b.build(&why));
		CHECK(why.find("thermodynamic data") != std::string::npos);
	}
	{
		World::Builder b(builtin_species()); // an empty cell beside a thermal one
		const CellId c = b.add_cell("c");
		b.add_cell("empty");
		b.set_amount(c, builtin::water, mol(1.0)).set_temperature(c, 300.0);
		CHECK_FALSE(b.build(&why));
	}
	{
		World::Builder b(builtin_species());
		const CellId c = b.add_cell("c");
		b.set_amount(c, builtin::water, mol(1.0)).set_temperature(c, -5.0);
		CHECK_FALSE(b.build(&why));
	}
	{
		World::Builder b(builtin_species());
		const CellId r = b.add_reservoir("room", 293.15);
		b.set_amount(r, builtin::nitrogen, mol(1.0));
		CHECK_FALSE(b.build(&why));
	}
	{
		World::Builder b(builtin_species());
		b.add_reservoir("room", std::numeric_limits<double>::infinity());
		CHECK_FALSE(b.build(&why));
	}
	{
		World::Builder b(builtin_species());
		const CellId c = b.add_cell("c");
		const CellId r = b.add_reservoir("room", 293.15);
		b.set_amount(c, builtin::water, mol(1.0)).set_temperature(c, 300.0);
		b.add_edge(c, r, PHASES_NONE, -1.0);
		CHECK_FALSE(b.build(&why));
	}
}

TEST_CASE("thermal: a reservoir takes and gives heat, never matter (Stage 2)") {
	World::Builder b(builtin_species());
	const CellId flask = b.add_cell("flask");
	const CellId room = b.add_reservoir("room", 293.15);
	const EdgeId wall = b.add_edge(flask, room, PHASES_ALL, 1.0);
	b.set_amount(flask, builtin::water, mol(1.0)).set_temperature(flask, 350.0);
	World world = build(b);
	const SpeciesAmount water[] = { { builtin::water, Amount{ 10 } } };
	CHECK(world.transfer(wall, flask, water, Energy{}) == Refusal::Reservoir);
	const Reaction &boil = builtin_reactions()[static_cast<size_t>(builtin::reaction::water_vaporization)];
	CHECK(world.transform(room, boil, 1) == Refusal::Reservoir);
	CHECK(world.transfer(wall, flask, {}, Energy{ 1000 }) == Refusal::None);
	CHECK(world.energy(room) == Energy{ 1000 });
	CHECK(temperature(world, room) == 293.15); // fixed
	CHECK(world.set_conductance(wall, 3.0) == Refusal::MidStep);
	world.close_step();
	CHECK(world.set_conductance(wall, 3.0) == Refusal::None);
	CHECK(world.edges()[0].conductance == 3.0);
	CHECK(world.set_conductance(wall, std::numeric_limits<double>::quiet_NaN()) == Refusal::BadValue);
	CHECK(world.set_conductance(EdgeId{ 5 }, 1.0) == Refusal::UnknownEdge);
	CHECK(world.connect(flask, room, PHASES_NONE, nullptr, -2.0) == Refusal::BadValue);
}

TEST_CASE("thermal: a flask cooling in a room follows Newton's law of cooling (Stage 2)") {
	// 100 g of water in 100 g of glass at 80 C, a 20 C room, UA = 2 W/K
	World::Builder b(builtin_species());
	const CellId flask = b.add_cell("flask");
	const CellId room = b.add_reservoir("room", 293.15);
	b.add_edge(flask, room, PHASES_NONE, 2.0);
	b.set_amount(flask, builtin::water, mol(100.0 / 18.015)).set_amount(flask, builtin::glass, mol(100.0 / 60.083)).set_temperature(flask, 353.15);
	Simulation sim(build(b));
	sim.set_checking(true);
	const double capacity = thermal_state(builtin_species(), sim.world().snapshot_cell(flask)).heat_capacity;
	const double start = temperature(sim.world(), flask);
	const Totals genesis = totals(builtin_species(), sim.world().snapshot());
	double worst = 0.0;
	double entropy_made = 0.0;
	for (int step = 1; step <= 36000; ++step) { // ten minutes
		const StepTrace trace = sim.step();
		entropy_made += entropy_change(builtin_species(), trace);
		if (step % 600 == 0) {
			const double t = step * STEP_SECONDS;
			const double analytic = 293.15 + (start - 293.15) * std::exp(-2.0 * t / capacity);
			worst = std::max(worst, std::abs(temperature(sim.world(), flask) - analytic));
		}
	}
	CHECK(worst < 1e-8); // kelvin, over 36000 steps
	CHECK(temperature(sim.world(), flask) < 300.0); // 600 s is 2.4 time constants
	CHECK(totals(builtin_species(), sim.world().snapshot()) == genesis);
	// what the flask lost is what the room holds, to the quantum
	CHECK(sim.world().energy(room).joule() == doctest::Approx(capacity * (start - temperature(sim.world(), flask))).epsilon(1e-9));
	CHECK(entropy_made > 0.0); // cooling into a colder room is irreversible
	MESSAGE("Newton cooling: worst error " << worst << " K over 600 s; entropy made " << entropy_made << " J/K");
}

TEST_CASE("thermal: two bodies meet at the temperature their capacities fix (Stage 2)") {
	// 1 mol of CuO at 500 K against 2 mol of water at 300 K, 0.5 W/K
	World::Builder b(builtin_species());
	const CellId hot = b.add_cell("hot");
	const CellId cold = b.add_cell("cold");
	b.add_edge(hot, cold, PHASES_NONE, 0.5);
	b.set_amount(hot, builtin::copper_oxide, mol(1.0)).set_temperature(hot, 500.0);
	b.set_amount(cold, builtin::water, mol(2.0)).set_temperature(cold, 300.0);
	Simulation sim(build(b));
	sim.set_checking(true);
	const double ca = 42.246, cb = 2.0 * 75.375;
	const double meet = (ca * 500.0 + cb * 300.0) / (ca + cb);
	const double rate = 0.5 * (1.0 / ca + 1.0 / cb);
	double worst = 0.0;
	for (int step = 1; step <= 18000; ++step) {
		sim.step();
		if (step % 300 == 0) {
			const double decay = std::exp(-rate * step * STEP_SECONDS);
			worst = std::max(worst, std::abs(temperature(sim.world(), hot) - (meet + (500.0 - meet) * decay)));
			worst = std::max(worst, std::abs(temperature(sim.world(), cold) - (meet + (300.0 - meet) * decay)));
		}
	}
	CHECK(worst < 1e-8);
}

TEST_CASE("thermal: any conductance and any number of edges are stable (Stage 2)") {
	// a tiny cell between a flame and a room through enormous conductances: an
	// explicit rate * dt would overshoot by orders of magnitude
	World::Builder b(builtin_species());
	const CellId speck = b.add_cell("speck");
	const CellId flame = b.add_reservoir("flame", 2200.0);
	const CellId room = b.add_reservoir("room", 293.15);
	b.add_edge(speck, flame, PHASES_NONE, 1e9);
	b.add_edge(speck, room, PHASES_NONE, 1e9);
	b.set_amount(speck, builtin::copper_oxide, mol(1e-6)).set_temperature(speck, 293.15);
	Simulation sim(build(b));
	sim.set_checking(true);
	for (int step = 0; step < 200; ++step) {
		sim.step();
		const double t = temperature(sim.world(), speck);
		CHECK(t >= 293.15 - 1e-9);
		CHECK(t <= 2200.0 + 1e-9);
	}
}

TEST_CASE("thermal: a world at rest stays bit-identical (Stage 2)") {
	// D3: three cells and a room, all joined; left until nothing moves
	World::Builder b(builtin_species());
	const CellId a = b.add_cell("a");
	const CellId c = b.add_cell("c");
	const CellId d = b.add_cell("d");
	const CellId room = b.add_reservoir("room", 293.15);
	b.add_edge(a, c, PHASES_NONE, 40.0);
	b.add_edge(c, d, PHASES_NONE, 25.0);
	b.add_edge(a, d, PHASES_NONE, 60.0);
	b.add_edge(d, room, PHASES_NONE, 30.0);
	b.set_amount(a, builtin::water, mol(0.5)).set_temperature(a, 370.0);
	b.set_amount(c, builtin::copper_oxide, mol(1.0)).set_temperature(c, 250.0);
	b.set_amount(d, builtin::glass, mol(0.8)).set_amount(d, builtin::nitrogen, mol(0.01)).set_temperature(d, 900.0);
	Simulation sim(build(b));
	sim.set_checking(true);
	for (int step = 0; step < 30000; ++step) {
		sim.step();
	}
	const Snapshot rest = sim.world().snapshot();
	const std::vector<double> carried = sim.heat_remainders();
	CHECK(std::abs(temperature(sim.world(), a) - 293.15) < 1e-9);
	CHECK(std::abs(temperature(sim.world(), d) - 293.15) < 1e-9);
	for (int step = 0; step < 100000; ++step) {
		const StepTrace trace = sim.step();
		REQUIRE(trace.ledger.empty());
	}
	CHECK(sim.world().snapshot() == rest);
	CHECK(sim.heat_remainders() == carried);
}

TEST_CASE("thermal: the frame's grouping of steps changes nothing (Stage 2)") {
	// the same world stepped twice gives the same bits (the full D1 comes with the lab layer)
	const auto run = [] {
		World::Builder b(builtin_species());
		const CellId a = b.add_cell("a");
		const CellId c = b.add_cell("c");
		const CellId room = b.add_reservoir("room", 280.0);
		b.add_edge(a, c, PHASES_NONE, 3.0);
		b.add_edge(c, room, PHASES_NONE, 0.7);
		b.set_amount(a, builtin::water, mol(2.0)).set_temperature(a, 360.0);
		b.set_amount(c, builtin::copper_sulfate, mol(0.3)).set_temperature(c, 300.0);
		Simulation sim(build(b));
		for (int step = 0; step < 5000; ++step) {
			sim.step();
		}
		return sim.world().snapshot();
	};
	CHECK(run() == run());
}
