// Heights and heads (GAME_SPEC §6, Flow between vessels, Stage 94): a conduit's port is a height over its vessel's
// floor; what stands there is what the column holds at that height; its pressure is the vessel's and the weight of the
// liquid over it; a crossing is driven by the two ports' pressures less the weight of what crosses over the rise
// between them, and gravity's work on it is in the ledger (Z1). Every invariant on every step.
#include "chem/builtin_ids.hpp"
#include "chem/conduit.hpp"
#include "chem/invariants.hpp"
#include "chem/sim.hpp"
#include "chem/thermo.hpp"
#include "tables.hpp"

#include <doctest/doctest.h>

#include <cmath>
#include <string>
#include <vector>

using namespace chem;

namespace {

constexpr double ML = 1e-6, CM = 1e-2, CM2 = 1e-4; // m^3, m, m^2
constexpr double GRAVITY = 9.80665;
constexpr PhaseMask FLUIDS = PHASES_GAS | phase_bit(Phase::Liquid) | phase_bit(Phase::Aqueous);

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

double water_m3(double kelvin = T0) {
	return molar_volume_at(builtin_species(), builtin_species().species(builtin::water), kelvin);
}

// an upright cylinder of `area` and `tall`, water to `level` under air at the room's pressure; open to `air` (an
// elastic cell) where one is given, else closed; in contact with the room
CellId add_cylinder(World::Builder &b, CellId room, CellId air, double area, double tall, double level, const std::string &name) {
	const double volume = area * tall, water = area * level;
	const CellId cell = b.add_rigid_cell(name, volume);
	b.set_column(cell, area);
	const double n = 0.999 * P_ATMOSPHERE * (volume - water) / (GAS_CONSTANT * T0);
	b.set_amount(cell, builtin::nitrogen, mol(0.79 * n)).set_amount(cell, builtin::oxygen, mol(0.21 * n));
	if (water > 0.0) {
		b.set_amount(cell, builtin::water, mol(water / water_m3()));
	}
	b.set_temperature(cell, T0);
	b.add_edge(cell, room, PHASES_NONE, 2.0);
	if (air.index >= 0) {
		b.add_edge(cell, air, PHASES_GAS, 0.0);
	}
	return cell;
}

CellId add_air(World::Builder &b) {
	const CellId air = b.add_cell("air");
	b.set_amount(air, builtin::nitrogen, mol(79.0)).set_amount(air, builtin::oxygen, mol(21.0)).set_temperature(air, T0);
	return air;
}

Conduit pipe(double bore, double length) {
	Conduit c;
	c.pieces[0] = Piece{ bore, length };
	c.count = 1;
	return c;
}

Simulation build(World::Builder &b) {
	std::string why;
	auto built = b.build(&why);
	REQUIRE_MESSAGE(built.has_value(), why);
	return Simulation(std::move(*built), equilibria());
}

// the height of a cylinder's water over its floor, m
double level_of(const Simulation &sim, CellId cell, double area) {
	const World &world = sim.world();
	double kelvin = T0;
	world.temperature(cell, kelvin);
	return static_cast<double>(world.snapshot_cell(cell).amount[static_cast<size_t>(builtin::water.index)]) * MOL_PER_QUANTUM * water_m3(kelvin) / area;
}

} // namespace

TEST_CASE("heads: two open vessels joined at their feet come level, the wide and the narrow at one height, never past it at a step or at four steps' length (Stage 94)") {
	for (const double seconds : { STEP_SECONDS, 4.0 * STEP_SECONDS }) {
		CAPTURE(seconds);
		World::Builder b(builtin_species());
		const CellId room = b.add_reservoir("room", T0);
		const CellId air = add_air(b);
		const CellId wide = add_cylinder(b, room, air, 20.0 * CM2, 30.0 * CM, 20.0 * CM, "wide");
		const CellId narrow = add_cylinder(b, room, air, 5.0 * CM2, 30.0 * CM, 2.0 * CM, "narrow");
		const EdgeId join = b.add_edge(wide, narrow, FLUIDS, 0.0);
		Simulation sim = build(b);
		sim.set_step_seconds(seconds);
		REQUIRE(sim.world().set_conduit(join, pipe(6e-3, 0.10), 3) == Refusal::None);
		CHECK_FALSE(sim.world().meta(wide).sealed); // each open to the air by its own mouth
		double gap = level_of(sim, wide, 20.0 * CM2) - level_of(sim, narrow, 5.0 * CM2);
		const double began = gap;
		const int steps = static_cast<int>(std::lround(90.0 / seconds));
		for (int i = 0; i < steps; ++i) {
			run(sim, 1);
			const double now = level_of(sim, wide, 20.0 * CM2) - level_of(sim, narrow, 5.0 * CM2);
			REQUIRE(now <= gap + 1e-9); // falling
			REQUIRE(now >= -1e-7); // never past level
			gap = now;
		}
		const double level = level_of(sim, wide, 20.0 * CM2);
		MESSAGE("from " << began / CM << " cm apart to " << gap / CM << " cm, both at " << level / CM << " cm");
		CHECK(gap < 0.01 * CM);
		CHECK(level == doctest::Approx((20.0 * 20.0 + 5.0 * 2.0) / 25.0 * CM).epsilon(2e-3)); // what they hold, over both areas
		CHECK(sim.closure_misses() == 0);
	}
}

TEST_CASE("heads: three open vessels in a row, one raised on a block: their surfaces come to one height over the bench, not over their own floors (Stage 94)") {
	World::Builder b(builtin_species());
	const CellId room = b.add_reservoir("room", T0);
	const CellId air = add_air(b);
	const double area = 10.0 * CM2;
	const CellId one = add_cylinder(b, room, air, area, 40.0 * CM, 30.0 * CM, "one");
	const CellId two = add_cylinder(b, room, air, area, 40.0 * CM, 5.0 * CM, "two");
	const CellId three = add_cylinder(b, room, air, area, 40.0 * CM, 5.0 * CM, "three");
	const EdgeId first = b.add_edge(one, two, FLUIDS, 0.0), second = b.add_edge(two, three, FLUIDS, 0.0);
	Simulation sim = build(b);
	REQUIRE(sim.world().set_conduit(first, pipe(6e-3, 0.10), 3) == Refusal::None);
	REQUIRE(sim.world().set_conduit(second, pipe(6e-3, 0.10), 3) == Refusal::None);
	REQUIRE(sim.world().set_height(three, 6.0 * CM) == Refusal::None); // on a block of 6 cm
	run(sim, 60 * 120);
	const double a = level_of(sim, one, area), c = level_of(sim, two, area), e = level_of(sim, three, area) + 6.0 * CM;
	MESSAGE("their surfaces over the bench: " << a / CM << ", " << c / CM << ", " << e / CM << " cm");
	CHECK(a == doctest::Approx(c).epsilon(1e-3));
	CHECK(e == doctest::Approx(c).epsilon(1e-3));
	CHECK(a + c + (e - 6.0 * CM) == doctest::Approx(40.0 * CM).epsilon(1e-3)); // the water there was
	CHECK(sim.closure_misses() == 0);
}

TEST_CASE("heads: a port over the floor leaves a heel: the vessel drains to its port's height and no further, though the other stands far below (Stage 94)") {
	World::Builder b(builtin_species());
	const CellId room = b.add_reservoir("room", T0);
	const CellId air = add_air(b);
	const double area = 10.0 * CM2;
	const CellId upper = add_cylinder(b, room, air, area, 20.0 * CM, 12.0 * CM, "upper");
	const CellId lower = add_cylinder(b, room, air, area, 40.0 * CM, 1.0 * CM, "lower");
	const EdgeId join = b.add_edge(upper, lower, FLUIDS, 0.0);
	Simulation sim = build(b);
	REQUIRE(sim.world().set_conduit(join, pipe(4e-3, 0.30), 3) == Refusal::None);
	REQUIRE(sim.world().set_ports(join, 4.0 * CM, PORT_TOP) == Refusal::None); // 4 cm up the upper one; into the lower one's top
	REQUIRE(sim.world().set_height(lower, -50.0 * CM) == Refusal::None);
	run(sim, 60 * 60);
	const double left = level_of(sim, upper, area), fell = level_of(sim, lower, area);
	MESSAGE("the upper drained to " << left / CM << " cm, the lower stands at " << fell / CM << " cm");
	CHECK(left == doctest::Approx(4.0 * CM).epsilon(5e-3));
	CHECK(fell == doctest::Approx(9.0 * CM).epsilon(5e-3)); // the 8 cm that came, on its 1
	run(sim, 60 * 10);
	CHECK(level_of(sim, upper, area) == doctest::Approx(left).epsilon(1e-6)); // and stays
	CHECK(sim.closure_misses() == 0);
}

TEST_CASE("heads: a tall open column runs into a closed flask until the flask's air holds its weight up: P = P_atm + rho g (the column's surface over the flask's), the entropy never falling on the way (Stage 94)") {
	// (without gravity's work in the ledger the flask's air would be squeezed with nothing to pay for it, and the
	// world's entropy would fall by v dP / T as the water came in: T2 is checked on every step)
	World::Builder b(builtin_species());
	const CellId room = b.add_reservoir("room", T0);
	const CellId air = add_air(b);
	const double thin = 2.0 * CM2, wide = 20.0 * CM2;
	const CellId column = add_cylinder(b, room, air, thin, 120.0 * CM, 100.0 * CM, "column");
	const CellId flask = add_cylinder(b, room, CellId{}, wide, 5.0 * CM, 0.0, "flask"); // closed, of air
	const EdgeId join = b.add_edge(column, flask, FLUIDS, 0.0);
	Simulation sim = build(b);
	run(sim, 1); // (each full at the room's pressure first: the flask is sealed as it stands)
	REQUIRE(sim.world().set_conduit(join, pipe(4e-3, 0.20), 3) == Refusal::None);
	REQUIRE(sim.world().meta(flask).sealed);
	const World &world = sim.world();
	int64_t given = 0;
	bool crossed = false;
	for (int i = 0; i < 60 * 240; ++i) {
		const StepTrace trace = run(sim, 1);
		for (const Crossed &x : trace.crossed) {
			given += x.weight;
			crossed = crossed || x.weight != 0;
		}
	}
	const double up = level_of(sim, column, thin), in = level_of(sim, flask, wide);
	const Thermal t = thermal_state(world.species(), world.snapshot_cell(flask), &world.meta(flask));
	REQUIRE(t.defined);
	const double density = solvent_density(world.species(), t.temperature);
	MESSAGE("the column at " << up / CM << " cm, the flask's water " << in / CM << " cm deep under air at " << t.pressure / 1000.0 << " kPa; gravity gave " << static_cast<double>(given) * JOULE_PER_QUANTUM << " J");
	CHECK(t.pressure == doctest::Approx(P_ATMOSPHERE + density * GRAVITY * (up - in)).epsilon(2e-4));
	CHECK(t.pressure > P_ATMOSPHERE + 5000.0);
	CHECK(crossed);
	CHECK(given > 0); // it fell
	// gravity's account is what its reservoir gave: the whole world's energy stands with it
	const CellId gravity = world.gravity_cell();
	REQUIRE(gravity.index >= 0);
	CHECK(world.snapshot_cell(gravity).energy == -given);
	CHECK(sim.closure_misses() == 0);
}

TEST_CASE("heads: a syringe held over an open beaker with its nozzle under the water draws it up: its air a little under the room's pressure by the water it holds up (Stage 94)") {
	World::Builder b(builtin_species());
	const CellId mains = b.add_reservoir("mains", WORK_KELVIN);
	const CellId room = b.add_reservoir("room", T0);
	const CellId air = add_air(b);
	const double bore = 100.0 * ML / 0.12, wide = 30.0 * CM2;
	const CellId beaker = add_cylinder(b, room, air, wide, 10.0 * CM, 6.0 * CM, "beaker");
	const CellId syringe = add_cylinder(b, room, CellId{}, bore, 20.0 * ML / bore, 0.0, "syringe"); // 20 mL of air
	const EdgeId work = b.add_edge(mains, syringe, PHASES_NONE, 0.0);
	const EdgeId nozzle = b.add_edge(syringe, beaker, FLUIDS, 0.0);
	Simulation sim = build(b);
	run(sim, 1); // (full at the room's pressure first)
	REQUIRE(sim.world().set_conduit(nozzle, pipe(2e-3, 0.01), 1) == Refusal::None);
	REQUIRE(sim.world().set_ports(nozzle, 0.0, 1.0 * CM) == Refusal::None); // its foot; a centimetre over the beaker's floor
	REQUIRE(sim.world().set_height(syringe, 10.0 * CM) == Refusal::None); // held over it
	REQUIRE(sim.world().meta(syringe).sealed);
	CHECK_FALSE(sim.world().meta(beaker).sealed);
	Piston p;
	p.area = bore;
	p.friction = 1.0;
	p.drag = 70.0;
	p.least = 0.5 * ML;
	p.most = 100.0 * ML;
	p.hand_most = 79.8;
	p.hand_speed = 0.3;
	p.work = work;
	p.held = true;
	p.target = 60.0 * ML;
	REQUIRE(sim.world().set_piston(syringe, p) == Refusal::None);
	run(sim, 60 * 20);
	const World &world = sim.world();
	const double drawn = level_of(sim, syringe, bore) * bore / ML;
	const Thermal t = thermal_state(world.species(), world.snapshot_cell(syringe), &world.meta(syringe));
	REQUIRE(t.defined);
	const double held_up = (10.0 * CM + level_of(sim, syringe, bore)) - level_of(sim, beaker, wide); // its surface over the beaker's
	MESSAGE("pulled 40 mL: " << drawn << " mL of water drawn up, its surface " << held_up / CM << " cm over the beaker's, its air at " << t.pressure / 1000.0 << " kPa");
	CHECK(world.piston_volume(syringe) == doctest::Approx(60.0 * ML).epsilon(1e-9));
	CHECK(drawn > 39.0);
	CHECK(drawn < 40.0); // (its air has grown a little, holding the water up)
	CHECK(t.pressure == doctest::Approx(P_ATMOSPHERE - solvent_density(world.species(), t.temperature) * GRAVITY * held_up).epsilon(3e-4));
	CHECK(sim.closure_misses() == 0);
}

TEST_CASE("invariant Z1: what a cell takes from gravity is its crossings' weights, each its mass times g times its fall; it fires on a trace corrupted in each (Stage 94)") {
	const SpeciesTable &table = builtin_species();
	World::Builder b(table);
	const CellId room = b.add_reservoir("room", T0);
	const CellId air = add_air(b);
	const CellId upper = add_cylinder(b, room, air, 10.0 * CM2, 30.0 * CM, 25.0 * CM, "upper");
	const CellId lower = add_cylinder(b, room, air, 10.0 * CM2, 30.0 * CM, 1.0 * CM, "lower");
	const EdgeId join = b.add_edge(upper, lower, FLUIDS, 0.0);
	Simulation sim = build(b);
	REQUIRE(sim.world().set_conduit(join, pipe(4e-3, 0.10), 3) == Refusal::None);
	const StepTrace trace = run(sim, 3);
	REQUIRE(trace.crossed.size() >= 1);
	REQUIRE(trace.crossed[0].weight > 0);
	CHECK(trace.crossed[0].fall == doctest::Approx(0.24).epsilon(0.05)); // from 25 cm to 1 cm, a little run already
	REQUIRE(check_Z1(table, trace).empty());
	const auto fires = [&](const StepTrace &bad) { return !check_Z1(table, bad).empty(); };
	const CellId gravity = sim.world().gravity_cell();
	{
		StepTrace bad = trace; // more than its fall gives
		bad.crossed[0].weight += 5;
		CHECK(fires(bad));
	}
	{
		StepTrace bad = trace; // a fall it did not have
		bad.crossed[0].fall *= 2.0;
		CHECK(fires(bad));
	}
	{
		StepTrace bad = trace; // gravity's entry not the crossing's
		for (LedgerEntry &entry : bad.ledger) {
			if (entry.kind == LedgerEntry::Kind::Transfer && entry.from == gravity) {
				entry.energy.quanta += 3;
				break;
			}
		}
		CHECK(fires(bad));
	}
	{
		StepTrace bad = trace; // gravity's work with no crossing
		bad.crossed.clear();
		CHECK(fires(bad));
	}
	{
		StepTrace bad = trace; // matter along gravity's edge
		for (LedgerEntry &entry : bad.ledger) {
			if (entry.kind == LedgerEntry::Kind::Transfer && entry.from == gravity) {
				entry.delta.push_back(SpeciesAmount{ builtin::water, Amount{ 1 } });
				break;
			}
		}
		CHECK(fires(bad));
	}
	// and T1 asks no heat's direction of it: gravity's reservoir is one of work
	CHECK(check_T1(table, trace).empty());
}
