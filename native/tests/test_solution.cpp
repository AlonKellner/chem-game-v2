// Solutions and crystals (GAME_SPEC §9): dissolved species, the surface rate
// law, nucleation and particle counts, the pentahydrate and its dehydration.
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

const Reaction &reaction(int index) {
	return builtin_reactions()[static_cast<size_t>(index)];
}

// mol of a dissolved species per kg of the cell's water
double molality(const World &world, CellId cell, SpeciesId solute) {
	return world.amount(cell, solute).mol() / (world.amount(cell, builtin::water).mol() * 0.018015);
}

// the crystallisation with a fast surface, to reach equilibrium quickly
Reaction fast_crystallisation() {
	std::string why;
	auto r = Reaction::make(builtin_species(), "crystallisation", reaction(builtin::reaction::pentahydrate_crystallization).terms(), &why,
			surface(builtin::copper_sulfate_pentahydrate, 10.0, 0.0, SpeciesId{}, 1e9, 3.0));
	REQUIRE_MESSAGE(r.has_value(), why);
	return *r;
}

// a beaker of solution with an excess of crystals, held at a temperature
struct Saturated {
	CellId cell;
	Simulation sim;
};
Saturated saturated(double kelvin, double dissolved) {
	World::Builder b(builtin_species());
	const CellId cell = b.add_cell("beaker");
	const CellId bath = b.add_reservoir("bath", kelvin);
	b.add_edge(cell, bath, PHASES_NONE, 500.0);
	b.set_amount(cell, builtin::water, mol(100.0 / 18.015)).set_amount(cell, builtin::copper_sulfate_pentahydrate, mol(1.0));
	b.set_particles(cell, builtin::copper_sulfate_pentahydrate, 1000000);
	if (dissolved > 0.0) {
		b.set_amount(cell, builtin::copper_sulfate_aq, mol(dissolved));
	}
	b.set_amount(cell, builtin::glass, mol(1.0)).set_temperature(cell, kelvin);
	Simulation sim(build(b), { fast_crystallisation() });
	sim.set_checking(true);
	return Saturated{ cell, std::move(sim) };
}

} // namespace

TEST_CASE("solution: the data oracle: saturated copper sulfate against the handbook's solubility (Stage 6)") {
	// K1. g of CuSO4 per 100 g of water (CRC Handbook), from an excess of the
	// pentahydrate dissolving. The dissolved salt's enthalpy and entropy were
	// fitted to this curve (docs/DATA_SOURCES.md): two numbers for five points.
	const struct {
		double celsius, grams;
	} handbook[] = { { 0.0, 14.3 }, { 10.0, 17.2 }, { 20.0, 20.5 }, { 60.0, 39.5 }, { 100.0, 75.4 } };
	for (const auto &row : handbook) {
		Saturated s = saturated(row.celsius + 273.15, 0.0);
		for (int step = 0; step < 60 * 600; ++step) {
			s.sim.step();
		}
		const double grams = molality(s.sim.world(), s.cell, builtin::copper_sulfate_aq) * 159.602 / 10.0;
		CAPTURE(row.celsius);
		CHECK(grams == doctest::Approx(row.grams).epsilon(0.015));
		CHECK(std::abs(temperature(s.sim.world(), s.cell) - (row.celsius + 273.15)) < 1e-6);
		// D3: saturated, nothing moves
		const Snapshot rest = s.sim.world().snapshot();
		for (int step = 0; step < 2000; ++step) {
			REQUIRE(s.sim.step().ledger.empty());
		}
		CHECK(s.sim.world().snapshot() == rest);
	}
}

TEST_CASE("solution: saturation is the same from above and from below (Stage 6)") {
	Saturated under = saturated(293.15, 0.0);
	Saturated over = saturated(293.15, 0.36); // 3.6 mol/kg, nearly three times saturation
	for (int step = 0; step < 60 * 600; ++step) {
		under.sim.step();
		over.sim.step();
	}
	const double from_below = molality(under.sim.world(), under.cell, builtin::copper_sulfate_aq);
	const double from_above = molality(over.sim.world(), over.cell, builtin::copper_sulfate_aq);
	CHECK(from_above == doctest::Approx(from_below).epsilon(1e-9));
	CHECK(over.sim.world().amount(over.cell, builtin::copper_sulfate_pentahydrate).mol() > 1.0); // it crystallised
	CHECK(under.sim.world().amount(under.cell, builtin::copper_sulfate_pentahydrate).mol() < 1.0); // it dissolved
}

TEST_CASE("solution: dissolving the pentahydrate cools the solution (Stage 6)") {
	// adiabatic: the heat of solution, +11.2 kJ/mol, comes out of the water
	World::Builder b(builtin_species());
	const CellId cell = b.add_cell("beaker");
	b.set_amount(cell, builtin::water, mol(100.0 / 18.015)).set_amount(cell, builtin::copper_sulfate_pentahydrate, mol(0.05));
	b.set_particles(cell, builtin::copper_sulfate_pentahydrate, 100000).set_temperature(cell, 298.15);
	Simulation sim(build(b), { fast_crystallisation() });
	sim.set_checking(true);
	const Totals genesis = totals(builtin_species(), sim.world().snapshot());
	for (int step = 0; step < 6000; ++step) {
		sim.step();
	}
	CHECK(sim.world().amount(cell, builtin::copper_sulfate_pentahydrate).quanta == 0); // all of it: 0.47 mol/kg is under saturation
	CHECK(sim.world().particles(cell, builtin::copper_sulfate_pentahydrate) == 0); // X1
	CHECK(sim.world().amount(cell, builtin::copper_sulfate_aq) == mol(0.05));
	// 0.05 mol * 11.17 kJ/mol over the solution's heat capacity
	const double capacity = thermal_state(builtin_species(), sim.world().snapshot_cell(cell)).heat_capacity;
	CHECK(298.15 - temperature(sim.world(), cell) == doctest::Approx(0.05 * 11168.0 / capacity).epsilon(0.01));
	CHECK(totals(builtin_species(), sim.world().snapshot()) == genesis);
}

TEST_CASE("solution: a solute lowers water's vapour pressure, so the solution boils hotter (Stage 6)") {
	const SpeciesTable &t = builtin_species();
	CellState cell{ std::vector<int64_t>(static_cast<size_t>(t.species_count()), 0), 0, {} };
	const auto at = [&](SpeciesId s) -> int64_t & { return cell.amount[static_cast<size_t>(s.index)]; };
	at(builtin::water) = mol(1.0 / 0.018015).quanta; // a kilogram
	CHECK(activity(t, cell, builtin::water) == 1.0);
	at(builtin::copper_sulfate_aq) = mol(1.0).quanta;
	CHECK(activity(t, cell, builtin::copper_sulfate_aq) == doctest::Approx(1.0).epsilon(1e-9)); // one molal
	CHECK(activity(t, cell, builtin::water) == doctest::Approx(std::exp(-0.018015)).epsilon(1e-9));
	// the vaporisation's affinity at the pure water's boiling point is now negative
	at(builtin::steam) = 1000000;
	bool defined = false;
	const auto vaporise = reaction(builtin::reaction::water_vaporization).terms();
	double lo = 373.0, hi = 376.0; // where a one-molal solution boils into pure steam
	for (int i = 0; i < 60; ++i) {
		const double middle = 0.5 * (lo + hi);
		(affinity(t, cell, vaporise, middle, defined) < 0.0 ? lo : hi) = middle;
	}
	at(builtin::copper_sulfate_aq) = 0;
	double pure_lo = 373.0, pure_hi = 376.0;
	for (int i = 0; i < 60; ++i) {
		const double middle = 0.5 * (pure_lo + pure_hi);
		(affinity(t, cell, vaporise, middle, defined) < 0.0 ? pure_lo : pure_hi) = middle;
	}
	// the ebullioscopic constant of water, 0.512 K kg/mol
	CHECK(lo - pure_lo == doctest::Approx(0.512).epsilon(0.03));
}

TEST_CASE("solution: copper oxide dissolves in the acid, faster stirred and faster warm (Stage 6)") {
	const auto time_to_dissolve = [](double kelvin, double mixing, double fraction) {
		World::Builder b(builtin_species());
		const CellId cell = b.add_cell("flask");
		const CellId bath = b.add_reservoir("bath", kelvin);
		b.add_edge(cell, bath, PHASES_NONE, 500.0);
		const double oxide = 3.597 / 79.545;
		b.set_amount(cell, builtin::copper_oxide, mol(oxide)).set_particles(cell, builtin::copper_oxide, 2500000);
		b.set_amount(cell, builtin::water, mol(96.2 / 18.015)).set_amount(cell, builtin::sulfuric_acid_aq, mol(0.1));
		b.set_amount(cell, builtin::glass, mol(1.0)).set_temperature(cell, kelvin);
		World world = build(b);
		REQUIRE(world.set_mixing(cell, mixing) == Refusal::None);
		Simulation sim(std::move(world), shipped());
		sim.set_checking(true);
		const Totals genesis = totals(builtin_species(), sim.world().snapshot());
		int step = 0;
		while (sim.world().amount(cell, builtin::copper_oxide).mol() > fraction * oxide && step < 60 * 7200) {
			sim.step();
			++step;
		}
		// what dissolved is in solution, mole for mole, and the acid paid for it
		const double gone = oxide - sim.world().amount(cell, builtin::copper_oxide).mol();
		CHECK(sim.world().amount(cell, builtin::copper_sulfate_aq).mol() == doctest::Approx(gone).epsilon(1e-9));
		CHECK(sim.world().amount(cell, builtin::sulfuric_acid_aq).mol() == doctest::Approx(0.1 - gone).epsilon(1e-9));
		CHECK(sim.world().energy(bath).quanta > 0); // exothermic: the bath took the heat
		CHECK(totals(builtin_species(), sim.world().snapshot()) == genesis);
		return step * STEP_SECONDS;
	};
	const double still = time_to_dissolve(293.15, 1.0, 0.5);
	const double stirred = time_to_dissolve(293.15, 3.0, 0.5);
	const double warm = time_to_dissolve(353.15, 1.0, 0.5);
	CHECK(still > 300.0); // minutes, cold and still
	CHECK(still / stirred == doctest::Approx(3.0).epsilon(0.02)); // the rate is proportional to the stirring
	CHECK(still / warm == doctest::Approx(std::exp(50000.0 / GAS_CONSTANT * (1.0 / 293.15 - 1.0 / 353.15))).epsilon(0.05)); // Arrhenius, 50 kJ/mol
	MESSAGE("half of the oxide gone: " << still << " s still, " << stirred << " s stirred, " << warm << " s at 80 C");
}

TEST_CASE("solution: with no acid left the oxide stops, and never runs backwards in pure water (Stage 6)") {
	World::Builder b(builtin_species());
	const CellId cell = b.add_cell("flask");
	b.set_amount(cell, builtin::copper_oxide, mol(0.05)).set_particles(cell, builtin::copper_oxide, 1000);
	b.set_amount(cell, builtin::water, mol(5.0)).set_amount(cell, builtin::sulfuric_acid_aq, mol(0.01)).set_temperature(cell, 350.0);
	Simulation sim(build(b), shipped());
	sim.set_checking(true);
	for (int step = 0; step < 60 * 3600; ++step) {
		sim.step();
	}
	// the acid is the limiting reagent: all but a trace of it is used
	CHECK(sim.world().amount(cell, builtin::sulfuric_acid_aq).mol() < 1e-4);
	CHECK(sim.world().amount(cell, builtin::copper_oxide).mol() == doctest::Approx(0.04).epsilon(0.01));
	CHECK(sim.world().particles(cell, builtin::copper_oxide) == 1000); // the grains shrank, none vanished
}

TEST_CASE("solution: slow cooling grows fewer, larger crystals than fast cooling (Stage 6)") {
	// the same hot solution, 3 mol/kg, taken to 20 C through a weak and a strong link
	const auto cool = [](double conductance, int64_t &count, double &diameter, double &crop) {
		World::Builder b(builtin_species());
		const CellId cell = b.add_cell("flask");
		const CellId room = b.add_reservoir("room", 293.15);
		b.add_edge(cell, room, PHASES_NONE, conductance);
		b.set_amount(cell, builtin::water, mol(20.0 / 18.015)).set_amount(cell, builtin::copper_sulfate_aq, mol(0.06));
		b.set_amount(cell, builtin::glass, mol(1.5)).set_temperature(cell, 363.15);
		Simulation sim(build(b), shipped());
		sim.set_checking(true);
		const Totals genesis = totals(builtin_species(), sim.world().snapshot());
		for (int step = 0; step < 60 * 7200; ++step) {
			sim.step();
		}
		count = sim.world().particles(cell, builtin::copper_sulfate_pentahydrate);
		diameter = particle_diameter(builtin_species(), sim.world().snapshot_cell(cell), builtin::copper_sulfate_pentahydrate);
		crop = sim.world().amount(cell, builtin::copper_sulfate_pentahydrate).mol();
		CHECK(std::abs(temperature(sim.world(), cell) - 293.15) < 0.2);
		CHECK(totals(builtin_species(), sim.world().snapshot()) == genesis);
	};
	int64_t slow_count = 0, fast_count = 0;
	double slow_size = 0.0, fast_size = 0.0, slow_crop = 0.0, fast_crop = 0.0;
	cool(0.15, slow_count, slow_size, slow_crop); // a time constant of 17 minutes
	cool(5.0, fast_count, fast_size, fast_crop); // of 25 seconds
	CHECK(slow_count > 0);
	CHECK(fast_count > 3 * slow_count);
	CHECK(slow_size > 1.4 * fast_size);
	CHECK(slow_crop == doctest::Approx(fast_crop).epsilon(0.01)); // the same amount comes out: the same equilibrium
	MESSAGE("slow: " << slow_count << " crystals of " << slow_size * 1e6 << " um; fast: " << fast_count << " of " << fast_size * 1e6 << " um; crop " << slow_crop * 249.677 << " g");
}

TEST_CASE("solution: overheated crystals lose their water and turn to the anhydrous salt (Stage 6)") {
	// dry crystals in a flask open to the air, at 200 C
	World::Builder b(builtin_species());
	const CellId flask = b.add_rigid_cell("flask", 250e-6);
	const CellId air = b.add_cell("air");
	const CellId hot = b.add_reservoir("hot", 473.15);
	b.add_edge(flask, air, PHASES_GAS);
	b.add_edge(flask, hot, PHASES_NONE, 5.0);
	b.set_amount(flask, builtin::copper_sulfate_pentahydrate, mol(0.02)).set_particles(flask, builtin::copper_sulfate_pentahydrate, 5000);
	b.set_amount(flask, builtin::glass, mol(1.5));
	b.set_amount(flask, builtin::nitrogen, mol(P_ATMOSPHERE * 248e-6 / (GAS_CONSTANT * 293.15)));
	b.set_temperature(flask, 293.15);
	b.set_amount(air, builtin::nitrogen, mol(100.0)).set_temperature(air, 293.15);
	Simulation sim(build(b), shipped());
	sim.set_checking(true);
	const Totals genesis = totals(builtin_species(), sim.world().snapshot());
	for (int step = 0; step < 60 * 1200; ++step) {
		sim.step();
	}
	CHECK(sim.world().amount(flask, builtin::copper_sulfate_pentahydrate).mol() < 0.0002); // over 99% gone
	CHECK(sim.world().amount(flask, builtin::copper_sulfate).mol() > 0.0198);
	CHECK(sim.world().particles(flask, builtin::copper_sulfate) == 5000); // the crystals' ghosts: the same grains
	CHECK(totals(builtin_species(), sim.world().snapshot()) == genesis);
}

TEST_CASE("solution: crystals in ordinary room air keep their water (Stage 6)") {
	// 20 C, air at half its saturation with water vapour, a day
	World::Builder b(builtin_species());
	const CellId dish = b.add_cell("dish");
	const CellId room = b.add_reservoir("room", 293.15);
	b.add_edge(dish, room, PHASES_NONE, 1.0);
	b.set_amount(dish, builtin::copper_sulfate_pentahydrate, mol(0.02)).set_amount(dish, builtin::nitrogen, mol(1.0)).set_amount(dish, builtin::steam, mol(0.0115));
	b.set_temperature(dish, 293.15);
	Simulation sim(build(b), shipped());
	sim.set_checking(true);
	sim.set_step_seconds(60.0);
	for (int step = 0; step < 1440; ++step) {
		sim.step();
	}
	CHECK(sim.world().amount(dish, builtin::copper_sulfate_pentahydrate).mol() > 0.01999);
}

TEST_CASE("solution: malformed surface laws, particle counts and stirring are refused (Stage 6)") {
	std::string why;
	const auto terms = reaction(builtin::reaction::pentahydrate_crystallization).terms();
	CHECK_FALSE(Reaction::make(builtin_species(), "r", terms, &why, surface(builtin::water, 1.0, 0.0))); // not a solid
	CHECK_FALSE(Reaction::make(builtin_species(), "r", terms, &why, surface(builtin::copper_oxide, 1.0, 0.0))); // not in the reaction
	CHECK_FALSE(Reaction::make(builtin_species(), "r", terms, &why, surface(builtin::copper_sulfate_pentahydrate, 0.0, 0.0)));
	CHECK_FALSE(Reaction::make(builtin_species(), "r", terms, &why, surface(builtin::copper_sulfate_pentahydrate, 1.0, 0.0, builtin::water))); // a driver that is not dissolved
	CHECK_FALSE(Reaction::make(builtin_species(), "r", terms, &why, surface(builtin::copper_sulfate_pentahydrate, 1.0, 0.0, SpeciesId{}, -1.0, 3.0)));
	CHECK(Reaction::make(builtin_species(), "r", terms, &why, surface(builtin::copper_sulfate_pentahydrate, 1.0, 0.0, builtin::copper_sulfate_aq)).has_value());
	{
		World::Builder b(builtin_species()); // a dissolved species with nothing to be dissolved in
		const CellId c = b.add_cell("c");
		b.set_amount(c, builtin::copper_sulfate_aq, mol(0.1)).set_amount(c, builtin::glass, mol(1.0)).set_temperature(c, 300.0);
		CHECK_FALSE(b.build(&why));
		CHECK(why.find("no liquid") != std::string::npos);
	}
	{
		World::Builder b(builtin_species()); // particles of what is not there
		const CellId c = b.add_cell("c");
		b.set_amount(c, builtin::water, mol(1.0)).set_particles(c, builtin::copper_oxide, 5);
		CHECK_FALSE(b.build(&why));
	}
	{
		World::Builder b(builtin_species()); // particles of a liquid
		const CellId c = b.add_cell("c");
		b.set_amount(c, builtin::water, mol(1.0)).set_particles(c, builtin::water, 5);
		CHECK_FALSE(b.build(&why));
	}
	World::Builder b(builtin_species());
	const CellId c = b.add_cell("c");
	b.set_amount(c, builtin::copper_oxide, mol(1.0));
	World world = build(b);
	CHECK(world.particles(c, builtin::copper_oxide) == 1); // a lump, by default
	CHECK(world.set_particles(c, builtin::copper_oxide, 40) == Refusal::None);
	CHECK(world.set_particles(c, builtin::copper_oxide, 0) == Refusal::BadValue); // it is there
	CHECK(world.set_particles(c, builtin::malachite, 3) == Refusal::BadValue); // it is not
	CHECK(world.set_particles(c, builtin::water, 3) == Refusal::BadValue);
	CHECK(world.set_mixing(c, 3.0) == Refusal::None);
	CHECK(world.meta(c).mixing == 3.0);
	CHECK(world.set_mixing(c, -1.0) == Refusal::BadValue);
	CHECK(world.set_mixing(CellId{ 7 }, 1.0) == Refusal::UnknownCell);
}
