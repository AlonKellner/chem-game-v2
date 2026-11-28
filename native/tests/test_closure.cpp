// The closure (GAME_SPEC §6, Flow between vessels, Stage 89): sealed vessels joined by conduits and moved by plungers,
// each brought with its neighbours to the pressure at which it is exactly full as the step ends: what crosses a
// conduit by its law at those pressures, carrying its enthalpy at the giver's end state. Every invariant on every
// step, and no solve left short of its tolerance.
#include "chem/builtin_ids.hpp"
#include "chem/conduit.hpp"
#include "chem/invariants.hpp"
#include "chem/sim.hpp"
#include "chem/thermo.hpp"
#include "tables.hpp"

#include <doctest/doctest.h>

#include <cmath>
#include <cstdint>
#include <iterator>
#include <string>
#include <vector>

using namespace chem;

namespace {

constexpr double ML = 1e-6; // m^3
constexpr double AREA = 100.0 * ML / 0.12; // a 100 mL barrel's bore over its 120 mm scale
constexpr double FRICTION = 1.0, DRAG = 70.0, HAND = 79.8; // N, N s/m, N

Amount mol(double n) {
	bool ok = false;
	const Amount a = amount_from_mol(n, ok);
	REQUIRE(ok);
	return a;
}

std::vector<Reaction> equilibria() {
	std::vector<Reaction> out;
	for (const Reaction &r : builtin_reactions()) {
		if (r.kinetics().law == Kinetics::Law::Equilibrium) {
			out.push_back(r);
		}
	}
	return out;
}

// every invariant on every step; the last step's trace back
StepTrace run(Simulation &sim, int steps) {
	sim.set_checking(false);
	StepTrace trace;
	for (int i = 0; i < steps; ++i) {
		trace = sim.step();
		const Violations v = check_all(sim.world().species(), trace, sim.equilibria());
		REQUIRE_MESSAGE(v.empty(), "step " << i << ": " << v[0]);
	}
	return trace;
}

Thermal state(const World &world, CellId cell) {
	const Thermal t = thermal_state(world.species(), world.snapshot_cell(cell), &world.meta(cell));
	REQUIRE(t.defined);
	return t;
}

struct Syringe {
	CellId cell;
	EdgeId work;
};

// a vessel of `air` mL of air over `water` mL of water at 25 C and the room's pressure, full (with no air, a ten
// millionth of its volume to spare, which its plunger takes up), in contact with the room (a reservoir at 25 C)
// through `conductance` W/K
Syringe add_vessel(World::Builder &b, CellId mains, CellId room, double air, double water, double conductance, const std::string &name) {
	const CellId cell = b.add_rigid_cell(name, (air + water) * ML * (air > 0.0 ? 1.0 : 1.0 + 1e-7));
	const double n = P_ATMOSPHERE * air * ML / (GAS_CONSTANT * T0);
	if (air > 0.0) {
		b.set_amount(cell, builtin::nitrogen, mol(0.79 * n)).set_amount(cell, builtin::oxygen, mol(0.21 * n));
	}
	if (water > 0.0) {
		b.set_amount(cell, builtin::water, mol(water * ML / molar_volume_at(builtin_species(), builtin_species().species(builtin::water), T0)));
	}
	b.set_temperature(cell, T0);
	if (conductance > 0.0) {
		b.add_edge(cell, room, PHASES_NONE, conductance);
	}
	return { cell, b.add_edge(mains, cell, PHASES_NONE, 0.0) };
}

Piston plunger(EdgeId work, double held_ml = 0.0, double area = AREA) {
	Piston p;
	p.area = area;
	p.friction = FRICTION;
	p.drag = DRAG;
	p.least = 0.5 * ML;
	p.most = 400.0 * ML;
	p.hand_most = HAND;
	p.work = work;
	p.held = held_ml > 0.0;
	p.target = held_ml * ML;
	return p;
}

Conduit nozzle() { // a syringe's: 2 mm by 1 cm
	Conduit c;
	c.pieces[0] = Piece{ 2e-3, 10e-3 };
	c.count = 1;
	return c;
}

Conduit nozzles_on_a_tube() {
	Conduit c;
	c.pieces[0] = Piece{ 2e-3, 10e-3 };
	c.pieces[1] = Piece{ 4e-3, 300e-3 };
	c.pieces[2] = Piece{ 2e-3, 10e-3 };
	c.count = 3;
	return c;
}

constexpr PhaseMask FLUIDS = PHASES_GAS | phase_bit(Phase::Liquid) | phase_bit(Phase::Aqueous);

Simulation build(World::Builder &b) {
	std::string why;
	auto built = b.build(&why);
	REQUIRE_MESSAGE(built.has_value(), why);
	return Simulation(std::move(*built), equilibria());
}

double ml_of(const Simulation &sim, const Syringe &s) {
	return sim.world().piston_volume(s.cell) / ML;
}

double water_ml(const Simulation &sim, const Syringe &s) {
	const SpeciesTable &table = sim.world().species();
	return static_cast<double>(sim.world().snapshot_cell(s.cell).amount[static_cast<size_t>(builtin::water.index)]) * MOL_PER_QUANTUM * molar_volume_at(table, table.species(builtin::water), T0) / ML;
}

} // namespace

TEST_CASE("closure: a conduit is set on an open edge, refused where it is none; a vessel open to the room only through one is sealed from it (Stage 89)") {
	World::Builder b(builtin_species());
	const CellId mains = b.add_reservoir("mains", WORK_KELVIN);
	const CellId room = b.add_reservoir("room", T0);
	const CellId air = b.add_cell("air");
	b.set_amount(air, builtin::nitrogen, mol(0.79)).set_amount(air, builtin::oxygen, mol(0.21)).set_temperature(air, T0);
	const Syringe s = add_vessel(b, mains, room, 50.0, 0.0, 5.0, "syringe");
	const EdgeId mouth = b.add_edge(s.cell, air, PHASES_GAS, 0.0);
	Simulation sim = build(b);
	World &world = sim.world();
	CHECK_FALSE(world.meta(s.cell).sealed); // open to the air
	Conduit bad = nozzle();
	bad.pieces[0].bore = 0.0;
	CHECK(world.set_conduit(mouth, bad, 1) == Refusal::BadValue);
	bad = nozzle();
	bad.count = MAX_PIECES + 1;
	CHECK(world.set_conduit(mouth, bad, 1) == Refusal::BadValue);
	CHECK(world.set_conduit(EdgeId{ 9999 }, nozzle(), 1) == Refusal::UnknownEdge);
	CHECK_FALSE(world.meta(s.cell).sealed);
	const uint64_t version = world.topology_version();
	REQUIRE(world.set_conduit(mouth, nozzle(), 1) == Refusal::None);
	CHECK(world.meta(s.cell).sealed); // its nozzle holds back what crosses: at its own pressure
	CHECK(world.edges()[static_cast<size_t>(mouth.index)].conduit == nozzle());
	CHECK(world.edges()[static_cast<size_t>(mouth.index)].foot == 1);
	CHECK(world.topology_version() != version);
	REQUIRE(world.set_conduit(mouth, Conduit{}) == Refusal::None);
	CHECK_FALSE(world.meta(s.cell).sealed); // none: open again
	REQUIRE(world.disconnect(mouth) == Refusal::None);
	CHECK(world.set_conduit(mouth, nozzle(), 1) == Refusal::UnknownEdge); // (not on a closed edge)
}

TEST_CASE("closure: a syringe open to the air only through its nozzle draws air through it at the nozzle's pace: under the room's pressure while it is pulled, at it again at rest (Stage 89)") {
	World::Builder b(builtin_species());
	const CellId mains = b.add_reservoir("mains", WORK_KELVIN);
	const CellId room = b.add_reservoir("room", T0);
	const CellId air = b.add_cell("air");
	b.set_amount(air, builtin::nitrogen, mol(0.79)).set_amount(air, builtin::oxygen, mol(0.21)).set_temperature(air, T0);
	const Syringe s = add_vessel(b, mains, room, 20.0, 0.0, 5.0, "syringe");
	const EdgeId mouth = b.add_edge(s.cell, air, PHASES_GAS, 0.0);
	Simulation sim = build(b);
	REQUIRE(sim.world().set_conduit(mouth, nozzle(), 1) == Refusal::None);
	const int64_t before = sim.world().snapshot_cell(s.cell).amount[static_cast<size_t>(builtin::nitrogen.index)];
	Piston p = plunger(s.work, 80.0);
	p.hand_speed = 0.3; // a person's pace: 250 mL/s
	REQUIRE(sim.world().set_piston(s.cell, p) == Refusal::None);
	double least = P_ATMOSPHERE;
	for (int i = 0; i < 30; ++i) {
		const StepTrace trace = run(sim, 1);
		least = std::min(least, state(sim.world(), s.cell).pressure);
		for (const Crossed &x : trace.crossed) {
			CHECK(x.from == air); // into the syringe, from the air at the room's pressure
			CHECK(x.from_pa == P_ATMOSPHERE);
			CHECK_FALSE(x.liquid);
		}
	}
	MESSAGE("pulled 60 mL in a quarter of a second: " << least / 1000.0 << " kPa at the least");
	CHECK(least < P_ATMOSPHERE - 500.0); // the nozzle holds the air back
	CHECK(least > 0.5 * P_ATMOSPHERE);
	run(sim, 60 * 3);
	CHECK(ml_of(sim, s) == doctest::Approx(80.0).epsilon(1e-9));
	CHECK(state(sim.world(), s.cell).pressure == doctest::Approx(P_ATMOSPHERE).epsilon(1e-6));
	const int64_t after = sim.world().snapshot_cell(s.cell).amount[static_cast<size_t>(builtin::nitrogen.index)];
	CHECK(static_cast<double>(after) == doctest::Approx(4.0 * static_cast<double>(before)).epsilon(2e-3)); // 80 mL of it for 20, at 25 C
	CHECK(sim.closure_misses() == 0);
}

TEST_CASE("closure: two syringes of nothing but water on a tube move as one: no bubble between them, pushed and pulled with the same force (Stage 89)") {
	// (until this stage a vessel with no gas had no room to give, and a pair of pure water stood)
	World::Builder b(builtin_species());
	const CellId mains = b.add_reservoir("mains", WORK_KELVIN);
	const CellId room = b.add_reservoir("room", T0);
	const Syringe one = add_vessel(b, mains, room, 0.0, 50.0, 5.0, "one");
	const Syringe two = add_vessel(b, mains, room, 0.0, 50.0, 5.0, "two");
	const EdgeId tube = b.add_edge(one.cell, two.cell, FLUIDS, 0.0);
	Simulation sim = build(b);
	REQUIRE(sim.world().set_conduit(tube, nozzles_on_a_tube(), 3) == Refusal::None);
	REQUIRE(sim.world().set_piston(one.cell, plunger(one.work)) == Refusal::None);
	REQUIRE(sim.world().set_piston(two.cell, plunger(two.work)) == Refusal::None);
	run(sim, 60);
	const auto pushed = [&](double to_ml, double &force, double &other) {
		const double start = ml_of(sim, two);
		Piston p = plunger(one.work, to_ml);
		p.hand_speed = 0.01; // 1 cm/s: 8.3 mL/s
		REQUIRE(sim.world().set_piston(one.cell, p) == Refusal::None);
		run(sim, 30);
		force = sim.world().meta(one.cell).piston.force; // (as it moves, steadily)
		run(sim, 60 * 3);
		CHECK(ml_of(sim, one) == doctest::Approx(to_ml).epsilon(1e-9));
		other = ml_of(sim, two) - start;
	};
	double in = 0.0, out = 0.0, with_in = 0.0, with_out = 0.0;
	pushed(40.0, in, with_in);
	pushed(50.0, out, with_out);
	MESSAGE("in by 10 mL: " << in << " N, the other " << with_in << " mL; out by 10 mL: " << out << " N, the other " << with_out << " mL");
	CHECK(with_in == doctest::Approx(10.0).epsilon(1e-4)); // water gives nothing to speak of
	CHECK(with_out == doctest::Approx(-10.0).epsilon(1e-4));
	CHECK(in < -2.0 * FRICTION); // both plungers' friction, both drags and the tube's drop
	CHECK(-in == doctest::Approx(out).epsilon(1e-3)); // a push is the other's pull
	CHECK(sim.closure_misses() == 0);
}

TEST_CASE("closure: a hydraulic press: water between a narrow plunger and a wide one stands at one pressure, the wide hand holding the narrow one's force in the ratio of their areas (Stage 89)") {
	World::Builder b(builtin_species());
	const CellId mains = b.add_reservoir("mains", WORK_KELVIN);
	const CellId room = b.add_reservoir("room", T0);
	const Syringe narrow = add_vessel(b, mains, room, 0.0, 50.0, 5.0, "narrow");
	const Syringe wide = add_vessel(b, mains, room, 0.0, 200.0, 5.0, "wide");
	const EdgeId tube = b.add_edge(narrow.cell, wide.cell, FLUIDS, 0.0);
	Simulation sim = build(b);
	REQUIRE(sim.world().set_conduit(tube, nozzles_on_a_tube(), 3) == Refusal::None);
	// the wide one held where it is by a hand that can hold anything; the narrow one pushed by a thumb
	Piston held = plunger(wide.work, 200.0, 4.0 * AREA);
	held.hand_most = 1e6;
	REQUIRE(sim.world().set_piston(wide.cell, held) == Refusal::None);
	REQUIRE(sim.world().set_piston(narrow.cell, plunger(narrow.work, 30.0)) == Refusal::None);
	run(sim, 60 * 3);
	const double p_narrow = state(sim.world(), narrow.cell).pressure, p_wide = state(sim.world(), wide.cell).pressure;
	const double thumb = sim.world().meta(narrow.cell).piston.force, holds = sim.world().meta(wide.cell).piston.force;
	MESSAGE("the thumb " << thumb << " N at " << p_narrow / 1000.0 << " kPa; the wide hand " << holds << " N at " << p_wide / 1000.0 << " kPa");
	CHECK(thumb == doctest::Approx(-HAND).epsilon(1e-9)); // all it has
	CHECK(p_wide == doctest::Approx(p_narrow).epsilon(1e-7)); // at rest: one pressure
	CHECK(p_narrow == doctest::Approx(P_ATMOSPHERE + (HAND - FRICTION) / AREA).epsilon(1e-6));
	// four times the area: four times the force, less what each one's friction holds
	CHECK(std::fabs(holds) == doctest::Approx(4.0 * (HAND - FRICTION)).epsilon(FRICTION / (4.0 * (HAND - FRICTION)) * 1.01));
	CHECK(ml_of(sim, wide) == doctest::Approx(200.0).epsilon(1e-9));
	CHECK(sim.closure_misses() == 0);
}

TEST_CASE("closure: three syringes of water in a row: one pushed in, the other two take what it gives between them; pulled out, they give it back, the hand's force the same either way (Stage 89)") {
	World::Builder b(builtin_species());
	const CellId mains = b.add_reservoir("mains", WORK_KELVIN);
	const CellId room = b.add_reservoir("room", T0);
	const Syringe one = add_vessel(b, mains, room, 0.5, 50.0, 5.0, "one");
	const Syringe two = add_vessel(b, mains, room, 0.5, 50.0, 5.0, "two");
	const Syringe three = add_vessel(b, mains, room, 0.5, 50.0, 5.0, "three");
	const EdgeId first = b.add_edge(one.cell, two.cell, FLUIDS, 0.0), second = b.add_edge(two.cell, three.cell, FLUIDS, 0.0);
	Simulation sim = build(b);
	REQUIRE(sim.world().set_conduit(first, nozzles_on_a_tube(), 3) == Refusal::None);
	REQUIRE(sim.world().set_conduit(second, nozzles_on_a_tube(), 3) == Refusal::None);
	for (const Syringe &s : { one, two, three }) {
		REQUIRE(sim.world().set_piston(s.cell, plunger(s.work)) == Refusal::None);
	}
	run(sim, 60 * 2);
	const auto pushed = [&](double to_ml, double &force, double &others) {
		const double start = ml_of(sim, two) + ml_of(sim, three);
		Piston p = plunger(one.work, to_ml);
		p.hand_speed = 0.01;
		REQUIRE(sim.world().set_piston(one.cell, p) == Refusal::None);
		run(sim, 30);
		force = sim.world().meta(one.cell).piston.force;
		run(sim, 60 * 4);
		others = ml_of(sim, two) + ml_of(sim, three) - start;
	};
	const double rest = ml_of(sim, one);
	double in = 0.0, out = 0.0, with_in = 0.0, with_out = 0.0;
	pushed(rest - 10.0, in, with_in);
	pushed(rest, out, with_out);
	MESSAGE("in by 10 mL: " << in << " N, the others " << with_in << " mL; out: " << out << " N, the others " << with_out << " mL; two " << ml_of(sim, two) << " mL, three " << ml_of(sim, three));
	CHECK(with_in == doctest::Approx(10.0).epsilon(0.02)); // (their half millilitres of air give a little)
	CHECK(with_out == doctest::Approx(-10.0).epsilon(0.02));
	CHECK(-in == doctest::Approx(out).epsilon(0.05));
	CHECK(sim.closure_misses() == 0);
}

TEST_CASE("closure: air let through a nozzle into a vessel at a third of its pressure is choked at first, the two then come to one pressure; the energy stands, and once they are at one temperature again the entropy has risen by the two gases' mixing at it (Stage 89)") {
	const SpeciesTable &table = builtin_species();
	World::Builder b(table);
	// (two litres a side: a step's crossing is a hundredth of a gram of 2.3, so the first steps end under the critical ratio)
	const CellId full = b.add_rigid_cell("full", 2000.0 * ML), empty = b.add_rigid_cell("empty", 2000.0 * ML);
	const double n = P_ATMOSPHERE * 2000.0 * ML / (GAS_CONSTANT * T0);
	b.set_amount(full, builtin::nitrogen, mol(n)).set_temperature(full, T0);
	b.set_amount(empty, builtin::nitrogen, mol(0.3 * n)).set_temperature(empty, T0); // at 0.3 atm: under the critical ratio, 0.528
	Edge e;
	e.a = full;
	e.b = empty;
	e.phases = PHASES_GAS;
	e.open = true;
	e.conductance = 0.5; // W/K between them: they come to one temperature
	e.conduit = nozzle();
	b.add_edge_as(e);
	Simulation sim = build(b);
	const World &world = sim.world();
	REQUIRE(world.meta(full).sealed);
	REQUIRE(world.meta(empty).sealed);
	const auto entropy_of = [&]() {
		return entropy(table, world.snapshot_cell(full), state(world, full).temperature, &world.meta(full)) +
				entropy(table, world.snapshot_cell(empty), state(world, empty).temperature, &world.meta(empty));
	};
	const double s0 = entropy_of();
	const int64_t e0 = world.snapshot_cell(full).energy + world.snapshot_cell(empty).energy;
	const StepTrace first = run(sim, 1);
	REQUIRE(first.crossed.size() >= 1);
	const Crossed &x = first.crossed[0];
	const double area = 0.25 * 3.14159265358979323846 * 4e-6, cp = table.species(builtin::nitrogen).cp, gamma = cp / (cp - GAS_CONSTANT), molar = 28.0134e-3;
	const double choked = area * x.from_pa * std::sqrt(gamma * molar / (GAS_CONSTANT * x.read_kelvin)) * std::pow(2.0 / (gamma + 1.0), (gamma + 1.0) / (2.0 * (gamma - 1.0)));
	MESSAGE("the first step: " << x.rate * 1e3 << " g/s from " << x.from_pa / 1000.0 << " kPa into " << x.to_pa / 1000.0 << " kPa, the choked rate " << choked * 1e3 << " g/s");
	CHECK(x.rate == doctest::Approx(choked).epsilon(1e-3));
	CHECK(x.to_pa < 0.528 * x.from_pa);
	CHECK(x.from_pa < P_ATMOSPHERE); // (the pressure the step ends at, not the one it began at)
	CHECK(x.from_pa > 0.99 * P_ATMOSPHERE);
	double coldest = T0, hottest = T0;
	for (int i = 0; i < 60 * 40; ++i) {
		run(sim, 1);
		coldest = std::min(coldest, state(world, full).temperature);
		hottest = std::max(hottest, state(world, empty).temperature);
	}
	MESSAGE("the giver cooled to " << coldest - 273.15 << " C as it gave, the taker warmed to " << hottest - 273.15 << " C");
	CHECK(coldest < T0 - 10.0); // what stays behind expands
	CHECK(hottest > T0 + 10.0); // what arrives is compressed by what follows
	run(sim, 60 * 600);
	CHECK(world.snapshot_cell(full).energy + world.snapshot_cell(empty).energy == e0); // nothing in, nothing out
	const double p_full = state(world, full).pressure, p_empty = state(world, empty).pressure;
	CHECK(p_full == doctest::Approx(p_empty).epsilon(1e-5)); // (to where a step's crossing is under a quantum)
	CHECK(p_full == doctest::Approx(0.65 * P_ATMOSPHERE).epsilon(1e-6));
	CHECK(state(world, full).temperature == doctest::Approx(T0).epsilon(1e-6)); // an ideal gas's energy is its temperature's
	CHECK(state(world, empty).temperature == doctest::Approx(T0).epsilon(1e-6));
	// each gas from its own pressure to the one they share, at one temperature: n R ln(P / P_f) each (the fuller one
	// gains, the emptier loses less)
	const double rise = entropy_of() - s0;
	MESSAGE("the entropy rose by " << rise << " J/K; " << n * GAS_CONSTANT * (std::log(1.0 / 0.65) + 0.3 * std::log(0.3 / 0.65)) << " by the pressures");
	CHECK(rise == doctest::Approx(n * GAS_CONSTANT * (std::log(1.0 / 0.65) + 0.3 * std::log(0.3 / 0.65))).epsilon(1e-5));
	CHECK(sim.closure_misses() == 0);
}

TEST_CASE("closure: a port that runs dry within a step gives what stands there next: a syringe's last water through its foot, then its air (Stage 89)") {
	World::Builder b(builtin_species());
	const CellId mains = b.add_reservoir("mains", WORK_KELVIN);
	const CellId room = b.add_reservoir("room", T0);
	const Syringe one = add_vessel(b, mains, room, 48.0, 2.0, 5.0, "one");
	const Syringe two = add_vessel(b, mains, room, 20.0, 0.0, 5.0, "two");
	const EdgeId tube = b.add_edge(one.cell, two.cell, FLUIDS, 0.0);
	Simulation sim = build(b);
	REQUIRE(sim.world().set_conduit(tube, nozzles_on_a_tube(), 3) == Refusal::None);
	REQUIRE(sim.world().set_piston(two.cell, plunger(two.work)) == Refusal::None);
	Piston p = plunger(one.work, 20.0);
	p.hand_speed = 0.02;
	REQUIRE(sim.world().set_piston(one.cell, p) == Refusal::None);
	bool liquid_seen = false, gas_seen = false, both_in_a_step = false, liquid_after_gas = false;
	for (int i = 0; i < 60 * 6; ++i) {
		const StepTrace trace = run(sim, 1);
		bool liquid = false, gas = false;
		for (const Crossed &x : trace.crossed) {
			if (x.from == one.cell) {
				liquid = liquid || x.liquid;
				gas = gas || !x.liquid;
				liquid_after_gas = liquid_after_gas || (x.liquid && gas_seen && !liquid_seen);
			}
		}
		both_in_a_step = both_in_a_step || (liquid && gas);
		liquid_seen = liquid_seen || liquid;
		gas_seen = gas_seen || gas;
	}
	CHECK(liquid_seen);
	CHECK(gas_seen);
	CHECK(both_in_a_step); // the step its water ran out in
	CHECK_FALSE(liquid_after_gas); // its water first
	MESSAGE("one holds " << water_ml(sim, one) << " mL of water, two " << water_ml(sim, two) << " mL; one at " << ml_of(sim, one) << " mL, two at " << ml_of(sim, two));
	CHECK(water_ml(sim, one) < 0.01); // (what its vapour holds)
	CHECK(water_ml(sim, two) == doctest::Approx(2.0).epsilon(0.01));
	CHECK(ml_of(sim, one) == doctest::Approx(20.0).epsilon(1e-9));
	CHECK(ml_of(sim, two) > 45.0); // the 30 mL pushed across, less what the air gives
	CHECK(sim.closure_misses() == 0);
}

TEST_CASE("closure: the step's length hardly changes what it finds: water pushed through a tube for four tenths of a second, at a step, a half and a quarter of it, the taker's plunger within a hundredth of a millilitre (Stage 89)") {
	// (each step's moves and crossings are at the pressures it ends at, a steady push's the same at any step: what
	// differs is the first steps' start)
	const auto after = [&](double seconds) {
		World::Builder b(builtin_species());
		const CellId mains = b.add_reservoir("mains", WORK_KELVIN);
		const CellId room = b.add_reservoir("room", T0);
		const Syringe one = add_vessel(b, mains, room, 5.0, 50.0, 5.0, "one");
		const Syringe two = add_vessel(b, mains, room, 5.0, 50.0, 5.0, "two");
		const EdgeId tube = b.add_edge(one.cell, two.cell, FLUIDS, 0.0);
		Simulation sim = build(b);
		sim.set_step_seconds(seconds);
		REQUIRE(sim.world().set_conduit(tube, nozzles_on_a_tube(), 3) == Refusal::None);
		REQUIRE(sim.world().set_piston(two.cell, plunger(two.work)) == Refusal::None);
		REQUIRE(sim.world().set_piston(one.cell, plunger(one.work, 20.0)) == Refusal::None); // a thumb's whole push
		run(sim, static_cast<int>(std::lround(0.4 / seconds)));
		CHECK(sim.closure_misses() == 0);
		return ml_of(sim, two);
	};
	const double whole = after(STEP_SECONDS), half = after(STEP_SECONDS / 2.0), quarter = after(STEP_SECONDS / 4.0), coarse = after(4.0 * STEP_SECONDS);
	MESSAGE("the taker after 0.4 s: " << coarse << " mL at four steps' length, " << whole << " at a step, " << half << " at a half, " << quarter << " at a quarter");
	CHECK(whole > 58.0); // (it moved, and is still moving: 20 mL/s and more)
	CHECK(whole < 70.0);
	CHECK(std::fabs(whole - half) < 0.01);
	CHECK(std::fabs(half - quarter) < 0.01);
	CHECK(std::fabs(coarse - whole) < 0.5); // (and a step four times as long is stable, and near)
}

TEST_CASE("closure: hands jerked at random on three syringes in a row, of water, of air and of both, weak hands and strong: every invariant on every step, no solve left short (Stage 89)") {
	struct Fill {
		double air, water;
	};
	struct Setup {
		const char *name;
		Fill one, two, three;
	};
	const Setup setups[] = { { "water, a bubble each", { 0.5, 50.0 }, { 0.5, 50.0 }, { 0.5, 50.0 } },
		{ "nothing but water", { 0.0, 50.0 }, { 0.0, 50.0 }, { 0.0, 50.0 } },
		{ "air", { 50.0, 0.0 }, { 50.0, 0.0 }, { 50.0, 0.0 } },
		{ "water, air, water", { 0.0, 50.0 }, { 50.0, 0.0 }, { 0.0, 40.0 } },
		{ "air over water, each", { 20.0, 30.0 }, { 5.0, 45.0 }, { 40.0, 10.0 } },
		{ "air, water, a little of both", { 60.0, 0.0 }, { 0.0, 60.0 }, { 2.0, 2.0 } } };
	const int count = static_cast<int>(std::size(setups)) * 2;
	for (int k = 0; k < count; ++k) {
		const Setup &setup = setups[static_cast<size_t>(k) / 2];
		uint64_t seed = 0x9E3779B97F4A7C15ull * static_cast<uint64_t>(k + 1);
		const auto random = [&]() { // a number in [0, 1)
			seed = seed * 6364136223846793005ull + 1442695040888963407ull;
			return static_cast<double>(seed >> 11) / 9007199254740992.0;
		};
		const std::string name = setup.name;
		CAPTURE(name);
		CAPTURE(k);
		World::Builder b(builtin_species());
		const CellId mains = b.add_reservoir("mains", WORK_KELVIN);
		const CellId room = b.add_reservoir("room", T0);
		const Syringe syringes[3] = { add_vessel(b, mains, room, setup.one.air, setup.one.water, 5.0, "one"), add_vessel(b, mains, room, setup.two.air, setup.two.water, 5.0, "two"),
			add_vessel(b, mains, room, setup.three.air, setup.three.water, 5.0, "three") };
		const EdgeId first = b.add_edge(syringes[0].cell, syringes[1].cell, FLUIDS, 0.0), second = b.add_edge(syringes[1].cell, syringes[2].cell, FLUIDS, 0.0);
		Simulation sim = build(b);
		// (the second syringe's two conduits: one at its foot, one at its top, so that it gives its water one way and
		// its air the other)
		REQUIRE(sim.world().set_conduit(first, nozzles_on_a_tube(), 3) == Refusal::None);
		REQUIRE(sim.world().set_conduit(second, k % 2 == 0 ? nozzles_on_a_tube() : nozzle(), k % 2 == 0 ? 3 : 2) == Refusal::None);
		for (const Syringe &s : syringes) {
			REQUIRE(sim.world().set_piston(s.cell, plunger(s.work)) == Refusal::None);
		}
		sim.set_checking(false);
		int until = 0;
		for (int i = 0; i < 2400; ++i) {
			if (i >= until) {
				until = i + 5 + static_cast<int>(random() * 90.0);
				for (const Syringe &s : syringes) {
					const double choice = random();
					Piston p = plunger(s.work, choice < 0.3 ? 0.0 : 0.6 + random() * 140.0);
					p.hand_most = random() < 0.3 ? 600.0 : HAND; // (a strong hand now and then: two arms)
					p.hand_speed = random() < 0.5 ? 0.3 : 0.0; // a person's pace, or as fast as its forces let it
					p.most = 150.0 * ML;
					if (sim.world().piston_volume(s.cell) <= p.most) {
						REQUIRE(sim.world().set_piston(s.cell, p) == Refusal::None);
					}
				}
			}
			const StepTrace trace = sim.step();
			const Violations v = check_all(sim.world().species(), trace, sim.equilibria());
			REQUIRE_MESSAGE(v.empty(), "step " << i << ": " << v[0]);
		}
		MESSAGE(std::string(setup.name) << ", " << k % 2 << ": " << sim.closure_nested() << " of its solves by the nested one");
		CHECK(sim.closure_misses() == 0);
	}
}

TEST_CASE("invariant F1: a crossing down the pressures it was solved at, within its conduit's law and the step, carrying its amounts' enthalpy at the giver's state; it fires on a trace corrupted in each (Stage 89)") {
	const SpeciesTable &table = builtin_species();
	World::Builder b(table);
	const CellId mains = b.add_reservoir("mains", WORK_KELVIN);
	const CellId room = b.add_reservoir("room", T0);
	const Syringe one = add_vessel(b, mains, room, 5.0, 50.0, 5.0, "one");
	const Syringe two = add_vessel(b, mains, room, 5.0, 50.0, 5.0, "two");
	const EdgeId tube = b.add_edge(one.cell, two.cell, FLUIDS, 0.0);
	Simulation sim = build(b);
	REQUIRE(sim.world().set_conduit(tube, nozzles_on_a_tube(), 3) == Refusal::None);
	REQUIRE(sim.world().set_piston(two.cell, plunger(two.work)) == Refusal::None);
	REQUIRE(sim.world().set_piston(one.cell, plunger(one.work, 20.0)) == Refusal::None);
	const StepTrace trace = run(sim, 5);
	REQUIRE(trace.crossed.size() >= 1);
	REQUIRE(check_F1(table, trace).empty());
	const auto fires = [&](const StepTrace &bad) { return !check_F1(table, bad).empty(); };
	{
		StepTrace bad = trace; // up the pressure
		std::swap(bad.crossed[0].from_pa, bad.crossed[0].to_pa);
		CHECK(fires(bad));
	}
	{
		StepTrace bad = trace; // faster than its conduit's law
		bad.crossed[0].rate *= 1.01;
		CHECK(fires(bad));
	}
	{
		StepTrace bad = trace; // for longer than the step
		bad.crossed[0].seconds = 1.01 * bad.seconds;
		CHECK(fires(bad));
	}
	{
		StepTrace bad = trace; // more than its rate and its time pass
		bad.crossed[0].kg *= 1.01;
		CHECK(fires(bad));
	}
	{
		StepTrace bad = trace; // not at the giver's temperature
		bad.crossed[0].kelvin += 1.0;
		CHECK(fires(bad));
	}
	{
		StepTrace bad = trace; // gas said, where liquid crossed
		bad.crossed[0].liquid = !bad.crossed[0].liquid;
		CHECK(fires(bad));
	}
	{
		StepTrace bad = trace; // a crossing with no entry
		bad.crossed.push_back(bad.crossed[0]);
		CHECK(fires(bad));
	}
	{
		StepTrace bad = trace; // an entry marked a crossing that is none of the step's
		bad.crossed.pop_back();
		CHECK(fires(bad));
	}
	{
		StepTrace bad = trace; // along an edge with no conduit
		bad.edges[static_cast<size_t>(tube.index)].conduit = Conduit{};
		CHECK(fires(bad));
	}
	{
		StepTrace bad = trace; // its entry's energy not its enthalpy
		for (LedgerEntry &entry : bad.ledger) {
			if (entry.crossing) {
				entry.energy.quanta += 1000;
				break;
			}
		}
		CHECK(fires(bad));
	}
	// and T1 no longer asks a crossing the replay's pressures; an unmarked one between sealed cells it still does
	CHECK(check_T1(table, trace).empty());
}
