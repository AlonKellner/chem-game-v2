// Boiling and condensation (GAME_SPEC §8): water's liquid and vapour taken to
// equilibrium in every cell each step, at fixed enthalpy; liquid down a drain.
#include "chem/builtin_ids.hpp"
#include "chem/invariants.hpp"
#include "chem/sim.hpp"
#include "chem/thermo.hpp"

#include <doctest/doctest.h>

#include <cmath>
#include <vector>

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

std::vector<Reaction> shipped() {
	return std::vector<Reaction>(builtin_reactions().begin(), builtin_reactions().end());
}

const SpeciesDef &water() {
	return builtin_species().species(builtin::water);
}
const SpeciesDef &steam() {
	return builtin_species().species(builtin::steam);
}

// The saturation pressure the species table implies, in Pa: the vapour pressure
// at which liquid and vapour have the same chemical potential.
double table_saturation(double t) {
	const double dh = (steam().hf - water().hf) + (steam().cp - water().cp) * (t - T0);
	const double ds = (steam().s0 - water().s0) + (steam().cp - water().cp) * std::log(t / T0);
	return P_STANDARD * std::exp(-(dh - t * ds) / (GAS_CONSTANT * t));
}

double table_boiling_point() {
	double lo = 300.0, hi = 450.0;
	for (int i = 0; i < 100; ++i) {
		const double middle = 0.5 * (lo + hi);
		(table_saturation(middle) < P_ATMOSPHERE ? lo : hi) = middle;
	}
	return lo;
}

double heat_of_vaporisation(double t) {
	return (steam().hf - water().hf) + (steam().cp - water().cp) * (t - T0);
}

double vapour_fraction(const World &world, CellId cell) {
	double gas = 0.0;
	for (int s = 0; s < world.species().species_count(); ++s) {
		if (world.species().species(SpeciesId{ s }).phase == Phase::Gas) {
			gas += world.amount(cell, SpeciesId{ s }).mol();
		}
	}
	return gas > 0.0 ? world.amount(cell, builtin::steam).mol() / gas : 0.0;
}

void add_air(World::Builder &b, CellId cell, double n) {
	b.set_amount(cell, builtin::nitrogen, mol(0.79 * n)).set_amount(cell, builtin::oxygen, mol(0.21 * n));
}

} // namespace

TEST_CASE("phase: the data oracle: the table's saturation pressure and boiling point against NIST (Stage 5)") {
	// K1. NIST Chemistry WebBook, saturation properties of water (IAPWS-95)
	const struct {
		double kelvin, pascal, vaporisation;
	} nist[] = { { 298.15, 3169.93, 43987.5 }, { 323.15, 12351.9, 42911.5 }, { 348.15, 38595.4, 41805.8 }, { 373.15, 101418.0, 40649.7 } };
	for (const auto &row : nist) {
		CAPTURE(row.kelvin);
		CHECK(table_saturation(row.kelvin) == doctest::Approx(row.pascal).epsilon(0.02));
		CHECK(heat_of_vaporisation(row.kelvin) == doctest::Approx(row.vaporisation).epsilon(0.006));
	}
	// the table boils water within half a kelvin of 373.124 K (steam as an ideal gas is the difference)
	CHECK(std::abs(table_boiling_point() - 373.1243) < 0.5);
	MESSAGE("table: boiling point " << table_boiling_point() - 273.15 << " C; saturation at 25 C " << table_saturation(298.15) << " Pa (NIST 3169.93)");
}

TEST_CASE("phase: water under dry air evaporates to saturation and cools doing it (Stage 5)") {
	World::Builder b(builtin_species());
	const CellId cell = b.add_cell("cell");
	b.set_amount(cell, builtin::water, mol(1.0)).set_amount(cell, builtin::nitrogen, mol(1.0)).set_temperature(cell, 298.15);
	Simulation sim(build(b), shipped());
	sim.set_checking(true);
	const Totals genesis = totals(builtin_species(), sim.world().snapshot());
	const StepTrace trace = sim.step();
	// projections only: the evaporation, and water's own ionisation into its 1e-7 mol/kg of ions (Stage 63)
	REQUIRE(!trace.ledger.empty());
	for (const LedgerEntry &e : trace.ledger) {
		REQUIRE(e.kind == LedgerEntry::Kind::Transform);
	}
	const double t = temperature(sim.world(), cell);
	CHECK(t < 298.15); // the vapour's latent heat came from the cell
	// (to 1e-8: water's own ions lower its activity by a few parts in a billion, Stage 63)
	CHECK(vapour_fraction(sim.world(), cell) * P_ATMOSPHERE == doctest::Approx(table_saturation(t)).epsilon(1e-8));
	CHECK(entropy_change(builtin_species(), trace) > 0.0);
	CHECK(totals(builtin_species(), sim.world().snapshot()) == genesis);
	// D3: at equilibrium nothing moves
	const Snapshot rest = sim.world().snapshot();
	for (int i = 0; i < 1000; ++i) {
		REQUIRE(sim.step().ledger.empty());
	}
	CHECK(sim.world().snapshot() == rest);
}

TEST_CASE("phase: supersaturated vapour condenses, and warms the cell (Stage 5)") {
	World::Builder b(builtin_species());
	const CellId cell = b.add_cell("cell");
	b.set_amount(cell, builtin::steam, mol(0.5)).set_amount(cell, builtin::nitrogen, mol(1.0)).set_amount(cell, builtin::glass, mol(20.0));
	b.set_temperature(cell, 300.0);
	Simulation sim(build(b), shipped());
	sim.set_checking(true);
	sim.step();
	const double t = temperature(sim.world(), cell);
	CHECK(t > 300.0);
	CHECK(sim.world().amount(cell, builtin::water).mol() > 0.4);
	// (to 1e-8: water's own ions lower its activity by a few parts in a billion, Stage 63)
	CHECK(vapour_fraction(sim.world(), cell) * P_ATMOSPHERE == doctest::Approx(table_saturation(t)).epsilon(1e-8));
}

TEST_CASE("phase: a little water in a hot cell boils dry, and the vapour stays below saturation (Stage 5)") {
	World::Builder b(builtin_species());
	const CellId cell = b.add_cell("cell");
	b.set_amount(cell, builtin::water, mol(0.01)).set_amount(cell, builtin::nitrogen, mol(1.0)).set_amount(cell, builtin::glass, mol(5.0));
	b.set_temperature(cell, 500.0);
	Simulation sim(build(b), shipped());
	sim.set_checking(true); // P1: liquid absent, at or below saturation
	sim.step();
	CHECK(sim.world().amount(cell, builtin::water).quanta == 0);
	CHECK(sim.world().amount(cell, builtin::steam) == mol(0.01));
	const double t = temperature(sim.world(), cell);
	CHECK(t < 500.0);
	CHECK(vapour_fraction(sim.world(), cell) * P_ATMOSPHERE < table_saturation(t));
	CHECK(sim.step().ledger.empty());
}

TEST_CASE("phase: an open flask boils at the table's boiling point, as fast as the heat arrives (Stage 5)") {
	// 100 g of water over a flame; once the air is flushed out the flask holds
	// steam alone and sits at the boiling point: every joule in makes vapour
	World::Builder b(builtin_species());
	const CellId flask = b.add_rigid_cell("flask", 250e-6);
	const CellId air = b.add_cell("air");
	const CellId flame = b.add_reservoir("flame", 2200.0);
	const CellId room = b.add_reservoir("room", 293.15);
	b.add_edge(flask, air, PHASES_GAS);
	b.add_edge(flask, flame, PHASES_NONE, 0.2);
	b.add_edge(air, room, PHASES_NONE, 1000.0);
	const double liquid = 100.0 / 18.015;
	b.set_amount(flask, builtin::water, mol(liquid)).set_amount(flask, builtin::glass, mol(100.0 / 60.083));
	add_air(b, flask, P_ATMOSPHERE * (250e-6 - liquid * water().molar_volume) / (GAS_CONSTANT * 293.15));
	b.set_temperature(flask, 293.15);
	add_air(b, air, 1000.0);
	b.set_temperature(air, 293.15);
	Simulation sim(build(b), shipped());
	sim.set_checking(true);
	const Totals genesis = totals(builtin_species(), sim.world().snapshot());
	const double boils = table_boiling_point();
	int steps = 0;
	while (temperature(sim.world(), flask) < boils - 1e-6 && steps < 60 * 600) {
		sim.step();
		++steps;
	}
	CHECK(steps < 60 * 300); // it reaches the boil within five minutes
	for (int i = 0; i < 60 * 60; ++i) { // a minute: the air is flushed out
		sim.step();
	}
	CHECK(vapour_fraction(sim.world(), flask) > 1.0 - 1e-9);
	const double before = sim.world().amount(flask, builtin::water).mol();
	const double flame_before = sim.world().energy(flame).joule();
	for (int i = 0; i < 60 * 60; ++i) { // a minute of steady boiling
		sim.step();
		CHECK(std::abs(temperature(sim.world(), flask) - boils) < 1e-6);
	}
	const double boiled = before - sim.world().amount(flask, builtin::water).mol();
	const double heat = flame_before - sim.world().energy(flame).joule();
	CHECK(heat == doctest::Approx(0.2 * (2200.0 - boils) * 60.0).epsilon(1e-5)); // about 365 W
	CHECK(boiled == doctest::Approx(heat / heat_of_vaporisation(boils)).epsilon(1e-5)); // power over latent heat
	CHECK(totals(builtin_species(), sim.world().snapshot()) == genesis);
	MESSAGE("boiling at " << boils - 273.15 << " C: " << heat / 60.0 << " W in, " << boiled * 18.015 << " g of water a minute");
}

TEST_CASE("phase: distilled water runs into the beaker, and it is water alone (Stage 5)") {
	World::Builder b(builtin_species());
	const CellId flask = b.add_rigid_cell("flask", 250e-6);
	const CellId condenser = b.add_rigid_cell("condenser", 50e-6);
	const CellId beaker = b.add_rigid_cell("beaker", 250e-6);
	const CellId air = b.add_cell("air");
	const CellId flame = b.add_reservoir("flame", 2200.0);
	const CellId coolant = b.add_reservoir("coolant", 288.15);
	const CellId room = b.add_reservoir("room", 293.15);
	b.add_edge(flask, condenser, PHASES_GAS);
	b.add_edge(condenser, air, PHASES_GAS);
	b.add_edge(beaker, air, PHASES_GAS);
	b.add_drain(condenser, beaker);
	b.add_edge(flask, flame, PHASES_NONE, 0.25);
	b.add_edge(flask, room, PHASES_NONE, 0.9);
	b.add_edge(condenser, coolant, PHASES_NONE, 10.0);
	b.add_edge(beaker, room, PHASES_NONE, 0.5);
	b.add_edge(air, room, PHASES_NONE, 1000.0);
	const double liquid = 100.0 / 18.015;
	const auto fills = [](double volume) { return P_ATMOSPHERE * volume / (GAS_CONSTANT * 293.15); };
	b.set_amount(flask, builtin::water, mol(liquid)).set_amount(flask, builtin::glass, mol(100.0 / 60.083));
	add_air(b, flask, fills(250e-6 - liquid * water().molar_volume));
	b.set_temperature(flask, 293.15);
	b.set_amount(condenser, builtin::glass, mol(50.0 / 60.083));
	add_air(b, condenser, fills(50e-6));
	b.set_temperature(condenser, 293.15);
	b.set_amount(beaker, builtin::glass, mol(80.0 / 60.083));
	add_air(b, beaker, fills(250e-6));
	b.set_temperature(beaker, 293.15);
	add_air(b, air, 1000.0);
	b.set_temperature(air, 293.15);
	Simulation sim(build(b), shipped());
	sim.set_checking(true);
	const Totals genesis = totals(builtin_species(), sim.world().snapshot());
	for (int step = 0; step < 60 * 600; ++step) { // ten minutes
		sim.step();
	}
	const double left = sim.world().amount(flask, builtin::water).mol();
	const double collected = sim.world().amount(beaker, builtin::water).mol();
	CHECK(left < 0.6 * liquid); // it boiled
	CHECK(collected > 0.95 * (liquid - left)); // nearly all of it reached the beaker
	// the beaker holds water, its own ions (Stage 63), its own vapour and air: no other species at all
	for (int s = 0; s < builtin_species().species_count(); ++s) {
		const SpeciesId id{ s };
		const bool expected = id == builtin::water || id == builtin::steam || id == builtin::nitrogen || id == builtin::oxygen || id == builtin::glass ||
				id == builtin::hydrogen_ion || id == builtin::hydroxide_ion;
		CAPTURE(builtin_species().species(id).id);
		CHECK((expected || sim.world().amount(beaker, id).quanta == 0));
	}
	const double t = temperature(sim.world(), condenser);
	CHECK(t > 288.15); // between the coolant
	CHECK(t < table_boiling_point()); // and the steam
	CHECK(sim.world().energy(coolant).quanta > 0); // the coolant took the latent heat
	CHECK(totals(builtin_species(), sim.world().snapshot()) == genesis);
	MESSAGE("distilled " << (liquid - left) * 18.015 << " g in ten minutes; " << collected * 18.015 << " g in the beaker; condenser at " << t - 273.15 << " C");
}

TEST_CASE("phase: a drain with a rate runs at most its rate's volume a step, its solutes in proportion, and its solids only where it passes them (Stage 27)") {
	// 2 mol of water (36 mL) with a little copper sulfate dissolved and malachite at rest, over an open cell
	World::Builder b(builtin_species());
	const CellId upper = b.add_cell("upper");
	const CellId lower = b.add_cell("lower");
	b.add_edge(upper, lower, PHASES_GAS);
	const EdgeId drain = b.add_drain(upper, lower);
	b.set_amount(upper, builtin::water, mol(2.0)).set_amount(upper, builtin::copper_sulfate_aq, mol(0.01)).set_amount(upper, builtin::malachite, mol(0.02));
	b.set_amount(upper, builtin::nitrogen, mol(0.01)).set_temperature(upper, 293.15);
	b.set_amount(lower, builtin::nitrogen, mol(10.0)).set_temperature(lower, 293.15);
	Simulation sim(build(b), shipped());
	sim.set_checking(true);
	const double rate = 6e-6; // 6 mL a second: 0.1 mL a step
	REQUIRE(sim.world().set_drain(drain, rate, false) == Refusal::None);
	sim.step();
	// its share of the liquid: the rate's volume of the 36 mL; its solute in the same share (the water that ran
	// partly evaporates into the dry gas below; what is dissolved does not)
	const double share_of_liquid = rate * STEP_SECONDS / (2.0 * water().molar_volume);
	CHECK(sim.world().amount(lower, builtin::copper_sulfate_aq).mol() == doctest::Approx(0.01 * share_of_liquid).epsilon(2e-2));
	CHECK(sim.world().amount(upper, builtin::water).mol() == doctest::Approx(2.0 * (1.0 - share_of_liquid)).epsilon(1e-4));
	CHECK(sim.world().amount(lower, builtin::malachite).mol() == 0.0);
	// passing solids: the same share of them
	REQUIRE(sim.world().set_drain(drain, rate, true) == Refusal::None);
	const double water_before = sim.world().amount(upper, builtin::water).mol(), malachite_before = sim.world().amount(upper, builtin::malachite).mol();
	sim.step();
	const double share = (water_before - sim.world().amount(upper, builtin::water).mol()) / water_before;
	CHECK(share > 0.0);
	CHECK(sim.world().amount(lower, builtin::malachite).mol() == doctest::Approx(share * malachite_before).epsilon(1e-3));
	// all of it again, below 0
	REQUIRE(sim.world().set_drain(drain, -1.0, false) == Refusal::None);
	sim.step();
	CHECK(sim.world().amount(upper, builtin::water).mol() == 0.0);
	// only a drain has a rate
	CHECK(sim.world().set_drain(EdgeId{ 0 }, rate, false) == Refusal::UnknownEdge);
}

TEST_CASE("phase: a drain carries a share of the solids with its liquid, and solids run down it by themselves at a rate of their own, liquid or none (Stage 31)") {
	World::Builder b(builtin_species());
	const CellId upper = b.add_cell("upper");
	const CellId lower = b.add_cell("lower");
	b.add_edge(upper, lower, PHASES_GAS);
	const EdgeId drain = b.add_drain(upper, lower);
	b.set_amount(upper, builtin::water, mol(2.0)).set_amount(upper, builtin::malachite, mol(0.02));
	b.set_amount(upper, builtin::nitrogen, mol(0.01)).set_temperature(upper, 293.15);
	b.set_amount(lower, builtin::nitrogen, mol(10.0)).set_temperature(lower, 293.15);
	Simulation sim(build(b), shipped());
	sim.set_checking(true);
	const SpeciesDef &malachite = builtin_species().species(builtin::malachite);
	REQUIRE(malachite.molar_volume > 0.0);
	// a quarter of the solids is suspended: of them, the liquid's own share goes with it
	const double rate = 6e-6;
	REQUIRE(sim.world().set_drain(drain, rate, 0.25) == Refusal::None);
	double water_before = sim.world().amount(upper, builtin::water).mol(), solid_before = sim.world().amount(upper, builtin::malachite).mol();
	sim.step();
	const double share = rate * STEP_SECONDS / (water_before * water().molar_volume);
	CHECK(sim.world().amount(lower, builtin::malachite).mol() == doctest::Approx(0.25 * share * solid_before).epsilon(1e-6));
	// sediment by itself: its rate's volume a step, whatever the liquid does (here none runs)
	const double solids_rate = 3e-7; // 0.3 mL of malachite a second
	REQUIRE(sim.world().set_drain(drain, 0.0, 0.0, solids_rate) == Refusal::None);
	water_before = sim.world().amount(upper, builtin::water).mol();
	solid_before = sim.world().amount(upper, builtin::malachite).mol();
	const double below_before = sim.world().amount(lower, builtin::malachite).mol();
	sim.step();
	CHECK(sim.world().amount(lower, builtin::malachite).mol() - below_before == doctest::Approx(solids_rate * STEP_SECONDS / malachite.molar_volume).epsilon(1e-6));
	CHECK(sim.world().amount(upper, builtin::water).mol() == doctest::Approx(water_before).epsilon(1e-3)); // (but for what evaporates)
	// both: what is carried and what runs by itself add up, and never more than there is
	REQUIRE(sim.world().set_drain(drain, -1.0, 1.0, 1.0) == Refusal::None);
	sim.step();
	CHECK(sim.world().amount(upper, builtin::malachite).mol() == 0.0);
	CHECK(sim.world().amount(upper, builtin::water).mol() == 0.0);
}

TEST_CASE("phase: dry solids run down a drain at their rate (Stage 31)") {
	World::Builder b(builtin_species());
	const CellId upper = b.add_cell("upper");
	const CellId lower = b.add_cell("lower");
	b.add_edge(upper, lower, PHASES_GAS);
	const EdgeId drain = b.add_drain(upper, lower);
	b.set_amount(upper, builtin::malachite, mol(0.02)).set_amount(upper, builtin::nitrogen, mol(0.01)).set_temperature(upper, 293.15);
	b.set_amount(lower, builtin::nitrogen, mol(10.0)).set_temperature(lower, 293.15);
	Simulation sim(build(b), shipped());
	sim.set_checking(true);
	const SpeciesDef &malachite = builtin_species().species(builtin::malachite);
	// no rate of their own: a drain is for liquid, and there is none
	sim.step();
	CHECK(sim.world().amount(lower, builtin::malachite).mol() == 0.0);
	const double solids_rate = 3e-7;
	REQUIRE(sim.world().set_drain(drain, 0.0, 0.0, solids_rate) == Refusal::None);
	for (int k = 0; k < 10; ++k) {
		sim.step();
	}
	CHECK(sim.world().amount(lower, builtin::malachite).mol() == doctest::Approx(10.0 * solids_rate * STEP_SECONDS / malachite.molar_volume).epsilon(1e-6));
}

TEST_CASE("phase: a drain fills a rigid cell and no further (Stage 5)") {
	// 2 mol of water (36 mL) above a 20 mL vessel
	World::Builder b(builtin_species());
	const CellId upper = b.add_cell("upper");
	const CellId lower = b.add_rigid_cell("lower", 20e-6);
	const CellId air = b.add_cell("air");
	b.add_drain(upper, lower);
	b.add_edge(lower, air, PHASES_GAS);
	b.set_amount(upper, builtin::water, mol(2.0)).set_temperature(upper, 330.0);
	b.set_amount(lower, builtin::glass, mol(0.5)).set_amount(lower, builtin::nitrogen, mol(P_ATMOSPHERE * 20e-6 / (GAS_CONSTANT * 293.15)));
	b.set_temperature(lower, 293.15);
	b.set_amount(air, builtin::nitrogen, mol(10.0)).set_temperature(air, 293.15);
	Simulation sim(build(b), shipped());
	sim.set_checking(true); // V1, V2: full of liquid, not over-full
	for (int i = 0; i < 10; ++i) {
		sim.step();
	}
	const double held = sim.world().amount(lower, builtin::water).mol() * water().molar_volume;
	CHECK(held == doctest::Approx(20e-6).epsilon(1e-6));
	CHECK(sim.world().amount(upper, builtin::water).mol() > 0.8); // the rest stayed above
	CHECK(temperature(sim.world(), lower) > 293.15); // warm water came down
	EdgeId made;
	World::Builder c(builtin_species());
	const CellId x = c.add_cell("x");
	const CellId y = c.add_cell("y");
	World world = build(c);
	CHECK(world.connect_drain(x, y, &made) == Refusal::None);
	CHECK(world.edges()[static_cast<size_t>(made.index)].drain);
	CHECK(world.connect_drain(x, x) == Refusal::SameCell);
}
