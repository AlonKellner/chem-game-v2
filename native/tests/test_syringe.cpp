// Pistons (GAME_SPEC §6, Stage 79): a syringe's plunger moved by the forces on it, overdamped, its space at one
// pressure; a hand that pulls it toward a volume with at most its force; the hand's work along an edge from a
// reservoir of work, the only work (T1, T2 held each step).
#include "chem/builtin_ids.hpp"
#include "chem/invariants.hpp"
#include "chem/sim.hpp"
#include "chem/thermo.hpp"
#include "tables.hpp"

#include <doctest/doctest.h>

#include <cmath>
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

void run(Simulation &sim, int steps) {
	sim.set_checking(false);
	for (int i = 0; i < steps; ++i) {
		const StepTrace trace = sim.step();
		const Violations v = check_all(sim.world().species(), trace, sim.equilibria());
		REQUIRE_MESSAGE(v.empty(), "step " << i << ": " << v[0]);
	}
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

// a syringe of `air` mL of air and `water` mL of water at 25 C, its nozzle capped, its plunger free; in contact with
// the room (a reservoir at 25 C) through `conductance` W/K
Syringe add_syringe(World::Builder &b, CellId mains, CellId room, double air, double water, double conductance, const char *name = "syringe") {
	// (water alone: a hundredth of a millilitre over it, its vapour's)
	const CellId cell = b.add_rigid_cell(name, (air + water + (air > 0.0 ? 0.0 : 0.01)) * ML);
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
	const EdgeId work = b.add_edge(mains, cell, PHASES_NONE, 0.0);
	return { cell, work };
}

Piston plunger(EdgeId work, double held_ml = 0.0) {
	Piston p;
	p.area = AREA;
	p.friction = FRICTION;
	p.drag = DRAG;
	p.least = 0.5 * ML;
	p.most = 100.0 * ML;
	p.hand_most = HAND;
	p.work = work;
	p.held = held_ml > 0.0;
	p.target = held_ml * ML;
	return p;
}

Simulation build(World::Builder &b) {
	std::string why;
	auto built = b.build(&why);
	REQUIRE_MESSAGE(built.has_value(), why);
	return Simulation(std::move(*built), equilibria());
}

void hold(Simulation &sim, const Syringe &s, double ml) {
	REQUIRE(sim.world().set_piston(s.cell, plunger(s.work, ml)) == Refusal::None);
}

double ml_of(const Simulation &sim, const Syringe &s) {
	return sim.world().piston_volume(s.cell) / ML;
}

} // namespace

TEST_CASE("syringe: a sealed syringe of air held and pushed in slowly, its pressure times its volume stands (Boyle), the hand's force what the pressure over the room's takes on its area (Stage 79)") {
	World::Builder b(builtin_species());
	const CellId mains = b.add_reservoir("mains", WORK_KELVIN);
	const CellId room = b.add_reservoir("room", T0);
	const Syringe s = add_syringe(b, mains, room, 100.0, 0.0, 5.0);
	Simulation sim = build(b);
	REQUIRE(sim.world().meta(s.cell).sealed);
	for (const double ml : { 90.0, 80.0, 70.0, 60.0 }) {
		CAPTURE(ml);
		hold(sim, s, ml);
		run(sim, 60 * 30);
		const Thermal t = state(sim.world(), s.cell);
		CHECK(ml_of(sim, s) == doctest::Approx(ml).epsilon(1e-9));
		CHECK(t.temperature == doctest::Approx(T0).epsilon(1e-6));
		CHECK(t.pressure * ml == doctest::Approx(P_ATMOSPHERE * 100.0).epsilon(1e-5));
		// at rest, the hand holds what friction does not
		const Piston &p = sim.world().meta(s.cell).piston;
		CHECK(-p.force == doctest::Approx((t.pressure - P_ATMOSPHERE) * AREA - FRICTION).epsilon(1e-3));
		MESSAGE(ml << " mL: " << t.pressure / 1000.0 << " kPa, the hand " << -p.force << " N");
	}
}

TEST_CASE("syringe: a hand pushes with at most its force: held toward 30 mL, the plunger stops where the air's pressure over the room's on its area is the hand's most, within its friction (Stage 79)") {
	World::Builder b(builtin_species());
	const CellId mains = b.add_reservoir("mains", WORK_KELVIN);
	const CellId room = b.add_reservoir("room", T0);
	const Syringe s = add_syringe(b, mains, room, 100.0, 0.0, 5.0);
	Simulation sim = build(b);
	hold(sim, s, 30.0);
	run(sim, 60 * 30);
	const Thermal t = state(sim.world(), s.cell);
	const double over = (t.pressure - P_ATMOSPHERE) * AREA;
	CHECK(over >= HAND - FRICTION - 1e-6);
	CHECK(over <= HAND + FRICTION + 1e-6);
	CHECK(-sim.world().meta(s.cell).piston.force == doctest::Approx(HAND));
	CHECK(ml_of(sim, s) > 50.0);
	MESSAGE("the hand's most: " << ml_of(sim, s) << " mL at " << t.pressure / 1000.0 << " kPa");
}

TEST_CASE("syringe: a sealed syringe of air pushed in and let go springs back to about where it was, short of it by what its friction holds (Stage 79)") {
	World::Builder b(builtin_species());
	const CellId mains = b.add_reservoir("mains", WORK_KELVIN);
	const CellId room = b.add_reservoir("room", T0);
	const Syringe s = add_syringe(b, mains, room, 100.0, 0.0, 5.0);
	Simulation sim = build(b);
	hold(sim, s, 60.0);
	run(sim, 60 * 5);
	CHECK(ml_of(sim, s) == doctest::Approx(60.0).epsilon(1e-9));
	hold(sim, s, 0.0); // let go
	run(sim, 60 * 30);
	const Thermal t = state(sim.world(), s.cell);
	const double band = FRICTION / AREA; // Pa
	CHECK(std::fabs(t.pressure - P_ATMOSPHERE) <= band * (1.0 + 1e-6));
	CHECK(ml_of(sim, s) < 100.0);
	CHECK(ml_of(sim, s) > 100.0 * P_ATMOSPHERE / (P_ATMOSPHERE + band) - 1e-6);
	MESSAGE("let go: back to " << ml_of(sim, s) << " mL");
}

TEST_CASE("syringe: a fast push heats the air as a compression does; a free plunger does no work: its cell's energy stands (Stage 79)") {
	World::Builder b(builtin_species());
	const CellId mains = b.add_reservoir("mains", WORK_KELVIN);
	const CellId room = b.add_reservoir("room", T0);
	const Syringe s = add_syringe(b, mains, room, 100.0, 0.0, 0.0); // (no contact: adiabatic)
	Simulation sim = build(b);
	hold(sim, s, 70.0);
	run(sim, 60 * 2);
	const Thermal t = state(sim.world(), s.cell);
	// at least the reversible adiabat's temperature (T V^(R/Cv) constant), hotter by what drag and friction gave
	const double n = P_ATMOSPHERE * 100.0 * ML / (GAS_CONSTANT * T0);
	const double cv = (0.79 * builtin_species().species(builtin::nitrogen).cp + 0.21 * builtin_species().species(builtin::oxygen).cp) - GAS_CONSTANT;
	const double adiabat = T0 * std::pow(100.0 / 70.0, GAS_CONSTANT / cv);
	CHECK(t.temperature >= adiabat);
	// and at most what the hand's most over the stroke and the room's work on the plunger's back give its gas
	const double most = (HAND * 30.0 * ML / AREA + P_ATMOSPHERE * 30.0 * ML) / (n * cv);
	CHECK(t.temperature <= T0 + most);
	MESSAGE("pushed to 70 mL in a second: " << t.temperature - 273.15 << " C (the adiabat's " << adiabat - 273.15 << " C), n " << n);
	hold(sim, s, 0.0);
	const int64_t e0 = sim.world().snapshot_cell(s.cell).energy;
	run(sim, 60 * 2);
	CHECK(sim.world().snapshot_cell(s.cell).energy == e0);
	CHECK(ml_of(sim, s) > 70.0);
}

TEST_CASE("syringe: a sealed syringe of water hardly moves: its plunger stands on the liquid however the hand pushes, and the room holds it there against the hand's pull (Stage 79)") {
	World::Builder b(builtin_species());
	const CellId mains = b.add_reservoir("mains", WORK_KELVIN);
	const CellId room = b.add_reservoir("room", T0);
	const Syringe s = add_syringe(b, mains, room, 0.0, 50.0, 5.0);
	Simulation sim = build(b);
	REQUIRE(sim.world().set_piston(s.cell, plunger(s.work)) == Refusal::None);
	run(sim, 60 * 2);
	// free, the room pushes it onto the water (its vapour, 3 kPa, holds nothing up), a ten-thousandth of its volume
	// left to its gas (the clearance its water grows into as it warms)
	const double rest = ml_of(sim, s);
	CHECK(rest == doctest::Approx(50.0).epsilon(2e-4));
	hold(sim, s, 30.0);
	run(sim, 60 * 5);
	const double pushed = ml_of(sim, s);
	CHECK(pushed == doctest::Approx(rest).epsilon(1e-6));
	// pulled: the room's pressure less the vapour's on its area, with its friction, is more than a hand's most
	hold(sim, s, 80.0);
	run(sim, 60 * 5);
	const double pulled = ml_of(sim, s);
	CHECK(pulled == doctest::Approx(rest).epsilon(1e-6));
	MESSAGE("water: at rest " << rest << " mL, pushed " << pushed << " mL, pulled " << pulled << " mL, at " << state(sim.world(), s.cell).pressure / 1000.0 << " kPa");
}

TEST_CASE("syringe: two sealed syringes of air joined: one pushed in, the other moves out, behind it by what the air between gives to its friction (Stage 79)") {
	World::Builder b(builtin_species());
	const CellId mains = b.add_reservoir("mains", WORK_KELVIN);
	const CellId room = b.add_reservoir("room", T0);
	const Syringe one = add_syringe(b, mains, room, 50.0, 0.0, 5.0, "one");
	const Syringe two = add_syringe(b, mains, room, 50.0, 0.0, 5.0, "two");
	b.add_edge(one.cell, two.cell, PHASES_GAS, 0.0);
	Simulation sim = build(b);
	REQUIRE(sim.world().set_piston(two.cell, plunger(two.work)) == Refusal::None);
	hold(sim, one, 30.0);
	run(sim, 60 * 10);
	CHECK(ml_of(sim, one) == doctest::Approx(30.0).epsilon(1e-9));
	const double moved = ml_of(sim, two) - 50.0;
	MESSAGE("one in by 20 mL, two out by " << moved << " mL");
	CHECK(moved > 15.0);
	CHECK(moved < 20.0);
}

TEST_CASE("syringe: two sealed syringes of water joined: one pushed in, the water runs into the other, which moves out with it (Stage 79)") {
	World::Builder b(builtin_species());
	const CellId mains = b.add_reservoir("mains", WORK_KELVIN);
	const CellId room = b.add_reservoir("room", T0);
	const Syringe one = add_syringe(b, mains, room, 0.05, 50.0, 5.0, "one");
	const Syringe two = add_syringe(b, mains, room, 0.05, 50.0, 5.0, "two");
	const EdgeId tube = b.add_edge(one.cell, two.cell, phase_bit(Phase::Liquid) | phase_bit(Phase::Aqueous), 0.0);
	Simulation sim = build(b);
	REQUIRE(sim.world().set_flow(tube, 0.0, 3) == Refusal::None); // (nozzle to nozzle: each one's foot, nothing held back)
	REQUIRE(sim.world().set_piston(two.cell, plunger(two.work)) == Refusal::None);
	// (both free first: the water's vapour comes into the bubbles)
	REQUIRE(sim.world().set_piston(one.cell, plunger(one.work)) == Refusal::None);
	run(sim, 60 * 2);
	const double start_one = ml_of(sim, one), start_two = ml_of(sim, two);
	hold(sim, one, 30.0);
	run(sim, 60 * 10);
	const double in = start_one - ml_of(sim, one), out = ml_of(sim, two) - start_two;
	MESSAGE("one in by " << in << " mL, two out by " << out << " mL");
	CHECK(ml_of(sim, one) == doctest::Approx(30.0).epsilon(1e-9));
	// the water gives nothing: only its bubbles (0.1 mL) do, within what its friction lets the pressure stand at either
	// side of the room's (both plungers free, then the one held: a band of 2 f / A)
	CHECK(std::fabs(out - in) <= 0.1 * 2.0 * FRICTION / AREA / P_ATMOSPHERE * 1.1);
}

TEST_CASE("syringe: a piston is refused on a cell that is not rigid, with no drag, with stops that do not hold it, or a work edge that is not from a reservoir (Stage 79)") {
	World::Builder b(builtin_species());
	const CellId mains = b.add_reservoir("mains", WORK_KELVIN);
	const CellId room = b.add_reservoir("room", T0);
	const Syringe s = add_syringe(b, mains, room, 100.0, 0.0, 5.0);
	const CellId bag = b.add_cell("bag");
	Simulation sim = build(b);
	World &w = sim.world();
	CHECK(w.set_piston(bag, plunger(s.work)) == Refusal::BadValue);
	Piston p = plunger(s.work);
	p.drag = 0.0;
	CHECK(w.set_piston(s.cell, p) == Refusal::BadValue);
	p = plunger(s.work);
	p.most = 90.0 * ML;
	CHECK(w.set_piston(s.cell, p) == Refusal::BadValue);
	CHECK(w.set_piston(s.cell, plunger(EdgeId{ 999 })) == Refusal::BadValue);
	CHECK(w.set_piston(s.cell, plunger(s.work)) == Refusal::None);
}

TEST_CASE("invariant W1: a piston within its stops, its hand within its most, the work along its edge its force over its move; it fires on a trace corrupted in each (Stage 79)") {
	World::Builder b(builtin_species());
	const CellId mains = b.add_reservoir("mains", WORK_KELVIN);
	const CellId room = b.add_reservoir("room", T0);
	const Syringe s = add_syringe(b, mains, room, 100.0, 0.0, 5.0);
	Simulation sim = build(b);
	hold(sim, s, 60.0);
	sim.set_checking(false);
	const StepTrace trace = sim.step();
	const auto c = static_cast<size_t>(s.cell.index);
	REQUIRE(trace.meta[c].piston.moved < 0.0);
	REQUIRE(trace.meta[c].piston.force < 0.0);
	CHECK(check_W1(builtin_species(), trace).empty());
	StepTrace strong = trace;
	strong.meta[c].piston.force = -1.01 * HAND;
	CHECK_FALSE(check_W1(builtin_species(), strong).empty());
	StepTrace past = trace;
	past.meta[c].volume = 101.0 * ML;
	CHECK_FALSE(check_W1(builtin_species(), past).empty());
	StepTrace free = trace;
	free.meta[c].piston.held = false;
	CHECK_FALSE(check_W1(builtin_species(), free).empty());
	StepTrace more = trace;
	bool found = false;
	for (LedgerEntry &e : more.ledger) {
		if (e.kind == LedgerEntry::Kind::Transfer && e.edge == s.work) {
			e.energy.quanta += 2;
			found = true;
		}
	}
	REQUIRE(found);
	CHECK_FALSE(check_W1(builtin_species(), more).empty());
}

TEST_CASE("invariant T1 with a reservoir of work: work given back to it, a gas pushing the hand, is no heat; heat into it from a colder cell still fires (Stage 79)") {
	World::Builder b(builtin_species());
	const CellId mains = b.add_reservoir("mains", WORK_KELVIN);
	const CellId hot = b.add_reservoir("hot", 1e6);
	const CellId room = b.add_reservoir("room", T0);
	const Syringe s = add_syringe(b, mains, room, 100.0, 0.0, 5.0);
	const EdgeId heat = b.add_edge(hot, s.cell, PHASES_NONE, 0.0); // (no heat runs: an edge for the corrupted entry below)
	Simulation sim = build(b);
	hold(sim, s, 60.0);
	run(sim, 60 * 3);
	sim.set_checking(false);
	hold(sim, s, 60.5); // (the air pushes the plunger out against the hand, which holds it back: it gives the mains work)
	StepTrace trace = sim.step();
	CHECK(check_T1(builtin_species(), trace).empty());
	bool given = false;
	for (const LedgerEntry &e : trace.ledger) {
		given = given || (e.edge == s.work && e.to == mains && e.energy.quanta > 0) || (e.edge == s.work && e.from == mains && e.energy.quanta < 0);
	}
	CHECK(given);
	// heat from the cell into a hotter reservoir still fires
	LedgerEntry wrong;
	wrong.kind = LedgerEntry::Kind::Transfer;
	wrong.edge = heat;
	wrong.from = s.cell;
	wrong.to = hot;
	wrong.energy = Energy{ 1000 };
	trace.ledger.push_back(wrong);
	CHECK_FALSE(check_T1(builtin_species(), trace).empty());
}

TEST_CASE("syringe: a tube holds back what crosses it: water pushed through a narrow one at a steady rate stands Poiseuille's pressure across it, viscosity times the flow over the tube's G (Stage 79)") {
	World::Builder b(builtin_species());
	const CellId mains = b.add_reservoir("mains", WORK_KELVIN);
	const CellId room = b.add_reservoir("room", T0);
	const Syringe one = add_syringe(b, mains, room, 1.0, 50.0, 5.0, "one");
	const Syringe two = add_syringe(b, mains, room, 1.0, 50.0, 5.0, "two");
	const EdgeId tube = b.add_edge(one.cell, two.cell, phase_bit(Phase::Liquid) | phase_bit(Phase::Aqueous), 0.0);
	Simulation sim = build(b);
	const double g = 2e-12; // m^3: a narrow tube's pi r^4 / (8 L)
	REQUIRE(sim.world().set_flow(tube, g, 3) == Refusal::None);
	REQUIRE(sim.world().set_piston(two.cell, plunger(two.work)) == Refusal::None);
	run(sim, 60);
	// one pushed in at a hand's steady 1 cm/s
	Piston p = plunger(one.work, 20.0);
	p.hand_speed = 0.01;
	REQUIRE(sim.world().set_piston(one.cell, p) == Refusal::None);
	run(sim, 60);
	const double q = 0.01 * AREA; // m^3/s
	const double mu = solvent_viscosity(builtin_species(), state(sim.world(), one.cell).temperature);
	const double across = state(sim.world(), one.cell).pressure - state(sim.world(), two.cell).pressure;
	MESSAGE("across the tube: " << across / 1000.0 << " kPa, Poiseuille's " << mu * q / g / 1000.0 << " kPa");
	// (each sub-step's flow driven by the difference at its start, the difference read at the step's end, after its last
	// flow eased it: under Poiseuille's by what a sub-step's flow eases)
	CHECK(across < mu * q / g);
	CHECK(across > 0.8 * mu * q / g);
}

TEST_CASE("measured: air's viscosity is the table's at its rows, within 1 % of Lemmon and Jacobsen's (Stage 79)") {
	CHECK(gas_viscosity(builtin_species(), 273.15) == doctest::Approx(17.15e-6).epsilon(1e-9));
	CHECK(gas_viscosity(builtin_species(), 298.15) == doctest::Approx(18.37e-6).epsilon(1e-9));
	// between rows, as the solvent's is read; Lemmon and Jacobsen (2004) give 18.54 uPa s at 300 K, 0.1 MPa
	CHECK(gas_viscosity(builtin_species(), 300.0) == doctest::Approx(18.54e-6).epsilon(0.01));
}
