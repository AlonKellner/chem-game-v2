// The vertical dimension (GAME_SPEC §6, Stage 47): a vessel's column of layers. Mixed it is one cell, as every
// vessel was; layered it keeps a denser liquid under a lighter one, passes heat and what is dissolved between them,
// overturns where the lower is the lighter, lets gas rise and solids sink, and resolves a steep step into thinner
// layers. Every invariant holds at every step.
#include "tables.hpp"
#include "chem/builtin_ids.hpp"
#include "chem/invariants.hpp"
#include "chem/sim.hpp"
#include "chem/thermo.hpp"

#include <doctest/doctest.h>

#include <cmath>
#include <string>
#include <vector>

using namespace chem;

namespace {

constexpr double PI = 3.14159265358979323846;
constexpr double RADIUS = 0.033; // m: a 250 mL beaker's inside
constexpr int LAYERS = 7;

Amount amount_of(double mol) {
	bool ok = false;
	const Amount a = amount_from_mol(mol, ok);
	REQUIRE(ok);
	return a;
}

struct Bench {
	CellId air, top;
	std::vector<CellId> layers;
	std::unique_ptr<Simulation> sim;
};

// a beaker of 250 mL open to the room, its column of seven layers; `fill` puts what each holds (0 the top)
template <typename Fill>
Bench beaker(Fill fill, bool column = true, int layers = LAYERS) {
	const SpeciesTable &table = builtin_species();
	World::Builder b(table);
	Bench out;
	out.air = b.add_cell("air");
	b.set_amount(out.air, builtin::nitrogen, amount_of(10.0)).set_amount(out.air, builtin::oxygen, amount_of(2.6)).set_temperature(out.air, 293.15);
	out.top = b.add_rigid_cell("beaker", 250e-6);
	b.add_edge(out.top, out.air, PHASES_GAS);
	b.set_amount(out.top, builtin::glass, amount_of(80.0 / 60.08));
	CellId over = out.top;
	if (column) {
		for (int i = 0; i < layers; ++i) {
			const CellId layer = b.add_layer_cell("beaker layer " + std::to_string(i + 1));
			b.add_column_edge(layer, over);
			out.layers.push_back(layer);
			over = layer;
		}
		b.set_column(out.top, PI * RADIUS * RADIUS);
	}
	fill(b, out);
	std::string why;
	auto world = b.build(&why);
	REQUIRE_MESSAGE(world, why);
	out.sim = std::make_unique<Simulation>(std::move(*world), std::vector<Reaction>(builtin_reactions().begin(), builtin_reactions().end()));
	// (Stage 70: the simulation's own checking off; run() holds every step to every invariant, and fails the test where
	// the simulation's would only report: each step was checked twice)
	out.sim->set_checking(false);
	return out;
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

// steps, each held to every invariant
void run(Bench &b, int steps) {
	const std::vector<Reaction> eq = equilibria();
	for (int s = 0; s < steps; ++s) {
		const StepTrace trace = b.sim->step();
		const Violations v = check_all(builtin_species(), trace, eq);
		REQUIRE_MESSAGE(v.empty(), v.front());
	}
}

// what is dissolved of an element (Stage 63: in each of its ions)
double dissolved(const Bench &b, CellId c, std::string_view element) {
	return testing::element_mol(builtin_species(), b.sim->world().snapshot_cell(c), element, Phase::Aqueous);
}

double mol(const Bench &b, CellId c, SpeciesId s) {
	return static_cast<double>(b.sim->world().snapshot_cell(c).amount[static_cast<size_t>(s.index)]) * MOL_PER_QUANTUM;
}

bool empty(const Bench &b, CellId c) {
	for (const int64_t a : b.sim->world().snapshot_cell(c).amount) {
		if (a != 0) {
			return false;
		}
	}
	return true;
}

// the column's layers as it is now (it grows as it needs: Stage 47), from the top down
std::vector<CellId> column(const Bench &b) {
	std::vector<CellId> out;
	for (CellId at = b.top;;) {
		CellId next{};
		for (const Edge &e : b.sim->world().edges()) {
			if (e.open && e.column && e.b == at) {
				next = e.a;
			}
		}
		if (next.index < 0) {
			return out;
		}
		out.push_back(next);
		at = next;
	}
}

// the lowest layer that holds anything (the top when none does)
CellId lowest(const Bench &b) {
	CellId out = b.top;
	for (const CellId c : column(b)) {
		if (!empty(b, c)) {
			out = c;
		}
	}
	return out;
}

int used(const Bench &b) {
	int n = 0;
	for (const CellId c : column(b)) {
		n += empty(b, c) ? 0 : 1;
	}
	return n;
}

double kelvin(const Bench &b, CellId c) {
	double t = 0.0;
	REQUIRE(b.sim->world().temperature(c, t));
	return t;
}

double density(const Bench &b, CellId c) {
	double d = 0.0;
	REQUIRE(liquid_density(builtin_species(), b.sim->world().snapshot_cell(c), kelvin(b, c), d));
	return d;
}

void water(World::Builder &w, CellId c, double grams, double kelvin) {
	w.set_amount(c, builtin::water, amount_of(grams / 18.015)).set_temperature(c, kelvin);
}

} // namespace

TEST_CASE("column: mixed, a vessel is one cell as before: its layers stay empty, and its water warms as one without them (Stage 47)") {
	const auto fill = [](World::Builder &w, Bench &b) { water(w, b.top, 150.0, 330.0); };
	Bench with = beaker(fill), without = beaker(fill, false);
	run(with, 600);
	run(without, 600);
	CHECK(used(with) == 0);
	CHECK(kelvin(with, with.top) == kelvin(without, without.top));
	CHECK(with.sim->world().meta(with.top).volume == 250e-6);
}

TEST_CASE("column: a denser solution under water stays under it, and what is dissolved crosses at Fick's rate (Stage 47)") {
	// 50 g of 1.5 mol/kg copper sulfate under 100 g of water, both at 20 C
	Bench b = beaker([](World::Builder &w, Bench &x) {
		water(w, x.top, 100.0, 293.15);
		w.set_amount(x.layers[0], builtin::water, amount_of(50.0 / 18.015)).set_amount(x.layers[0], builtin::copper_sulfate_aq, amount_of(0.075)).set_temperature(x.layers[0], 293.15);
	});
	const SpeciesTable &table = builtin_species();
	CHECK(density(b, b.layers[0]) > density(b, b.top) * 1.1);
	// the first step: D A (c_lower - c_upper) / dz of the copper sulfate rises
	const CellState &top = b.sim->world().snapshot_cell(b.top), &low = b.sim->world().snapshot_cell(b.layers[0]);
	const auto volume = [&](const CellState &c) {
		double v = 0.0;
		for (size_t s = 0; s < c.amount.size(); ++s) {
			const SpeciesDef &d = table.species(SpeciesId{ static_cast<int32_t>(s) });
			if (c.amount[s] > 0 && !d.wall && (d.phase == Phase::Liquid || d.phase == Phase::Aqueous)) {
				v += static_cast<double>(c.amount[s]) * MOL_PER_QUANTUM * d.molar_volume;
			}
		}
		return v;
	};
	const double area = PI * RADIUS * RADIUS, vu = volume(top), vl = volume(low), dz = 0.5 * (vu + vl) / area;
	const double expected = diffusion(table, builtin::copper_sulfate_aq, 293.15) * area * (0.075 / vl) / dz * STEP_SECONDS;
	run(b, 1);
	// (the step may split the water over it: what rose is in every cell over the dense layer)
	// (and some of it the trace of hydroxide its hydrolysis makes in pure water, Stage 44)
	const auto copper = [&](CellId c) { return dissolved(b, c, "Cu") + mol(b, c, builtin::copper_hydroxide); };
	double rose = copper(b.top);
	for (const CellId c : column(b)) {
		rose += c == lowest(b) ? 0.0 : copper(c);
	}
	MESSAGE("the first step moves " << rose << " mol up; Fick: " << expected);
	CHECK(rose / expected == doctest::Approx(1.0).epsilon(0.02)); // (a ratio: Approx's tolerance is absolute below 1)
	// an hour on: still layered, the denser under, the total the same, every layer in order of density
	run(b, 60 * 3600);
	CHECK(used(b) >= 1);
	// all the copper still there
	double total = copper(b.top);
	for (const CellId c : column(b)) {
		total += copper(c);
	}
	CHECK(total == doctest::Approx(0.075).epsilon(1e-9));
	double over = density(b, b.top);
	for (const CellId c : column(b)) {
		if (!empty(b, c)) {
			const double d = density(b, c);
			CHECK(d >= over * (1.0 - 1e-6));
			over = d;
		}
	}
	MESSAGE("after an hour " << used(b) << " layers under the top hold the gradient");
}

TEST_CASE("column: a denser liquid over a lighter overturns at once; warm over cold stays, cold over warm overturns (Stage 47)") {
	Bench dense_on_top = beaker([](World::Builder &w, Bench &x) {
		water(w, x.layers[0], 100.0, 293.15);
		w.set_amount(x.top, builtin::water, amount_of(50.0 / 18.015)).set_amount(x.top, builtin::copper_sulfate_aq, amount_of(0.075)).set_temperature(x.top, 293.15);
	});
	run(dense_on_top, 1);
	CHECK(used(dense_on_top) == 0);
	CHECK(dissolved(dense_on_top, dense_on_top.top, "Cu") == doctest::Approx(0.075));
	Bench warm_on_cold = beaker([](World::Builder &w, Bench &x) {
		water(w, x.top, 60.0, 333.15);
		water(w, x.layers[0], 60.0, 288.15);
	});
	run(warm_on_cold, 60);
	CHECK(used(warm_on_cold) >= 1);
	CHECK(kelvin(warm_on_cold, warm_on_cold.top) > kelvin(warm_on_cold, lowest(warm_on_cold)) + 10.0);
	Bench cold_on_warm = beaker([](World::Builder &w, Bench &x) {
		water(w, x.top, 60.0, 288.15);
		water(w, x.layers[0], 60.0, 333.15);
	});
	run(cold_on_warm, 1);
	CHECK(used(cold_on_warm) == 0);
}

TEST_CASE("column: gas in a layer rises, solids in the top sink into the liquid under them, and stirring makes the column one (Stage 47, Stage 49)") {
	Bench b = beaker([](World::Builder &w, Bench &x) {
		water(w, x.top, 100.0, 293.15);
		w.set_amount(x.top, builtin::copper_oxide, amount_of(0.01));
		w.set_amount(x.layers[0], builtin::water, amount_of(50.0 / 18.015)).set_amount(x.layers[0], builtin::copper_sulfate_aq, amount_of(0.075));
		w.set_amount(x.layers[0], builtin::carbon_dioxide, amount_of(0.001)).set_temperature(x.layers[0], 293.15);
	});
	run(b, 2);
	CHECK(mol(b, b.layers[0], builtin::carbon_dioxide) == 0.0);
	CHECK(mol(b, b.top, builtin::copper_oxide) == 0.0);
	CHECK(mol(b, lowest(b), builtin::copper_oxide) / 0.01 == doctest::Approx(1.0).epsilon(1e-3));
	REQUIRE(b.sim->world().set_mixing(b.top, 3.0) == Refusal::None);
	REQUIRE(b.sim->world().set_stirring(b.top, 0.01) == Refusal::None); // (Stage 49: as far as the turning pays)
	run(b, 600);
	CHECK(used(b) == 0);
}

// ---- stirred (Stage 49)

namespace {

// the column's potential energy, J: each layer's liquid mass (as dense as it is, over its volume) at the height of
// its middle, from the bottom up
double potential(const Bench &b) {
	const SpeciesTable &table = builtin_species();
	const double area = b.sim->world().meta(b.top).column_area;
	std::vector<CellId> cells{ b.top };
	for (const CellId c : column(b)) {
		cells.push_back(c);
	}
	double z = 0.0, out = 0.0;
	for (size_t k = cells.size(); k-- > 0;) {
		const CellState &c = b.sim->world().snapshot_cell(cells[k]);
		double v = 0.0;
		for (size_t s = 0; s < c.amount.size(); ++s) {
			const SpeciesDef &d = table.species(SpeciesId{ static_cast<int32_t>(s) });
			if (c.amount[s] > 0 && !d.wall && (d.phase == Phase::Liquid || d.phase == Phase::Aqueous)) {
				v += static_cast<double>(c.amount[s]) * MOL_PER_QUANTUM * d.molar_volume;
			}
		}
		if (!(v > 0.0)) {
			continue;
		}
		const double h = v / area;
		out += density(b, cells[k]) * v * 9.80665 * (z + 0.5 * h);
		z += h;
	}
	return out;
}

Bench two_liquids() {
	return beaker([](World::Builder &w, Bench &x) {
		water(w, x.top, 80.0, 293.15);
		w.set_amount(x.top, builtin::nickel_sulfate_aq, amount_of(0.064));
		w.set_amount(x.layers[0], builtin::water, amount_of(80.0 / 18.015)).set_amount(x.layers[0], builtin::copper_sulfate_aq, amount_of(0.112)).set_temperature(x.layers[0], 293.15);
	});
}

} // namespace

TEST_CASE("measured: stirred, a layered column's potential energy rises at Linden's 0.2 of what the turning dissipates (Stage 49)") {
	// Linden 1979 (Geophys. Astrophys. Fluid Dyn. 13, 3-23): of the work that stirs a stable stratification, at most
	// 0.2 +- 0.05 goes to its potential energy; the column takes the most (docs/DATA_SOURCES.md). Two layers, the
	// green nickel sulfate over the blue copper sulfate, 80 g of water each: still and stirred side by side, the
	// difference is the stirring's.
	Bench still = two_liquids(), stirred = two_liquids();
	run(still, 2);
	run(stirred, 2);
	const double watts = 1e-3;
	REQUIRE(stirred.sim->world().set_stirring(stirred.top, watts) == Refusal::None);
	const double before_still = potential(still), before_stirred = potential(stirred);
	const int steps = 6;
	run(still, steps);
	run(stirred, steps);
	const double gained = (potential(stirred) - before_stirred) - (potential(still) - before_still);
	const double paid = 0.2 * watts * steps * STEP_SECONDS;
	MESSAGE("gained " << gained * 1e6 << " uJ of " << paid * 1e6 << " uJ paid");
	CHECK(used(stirred) >= 1); // (not yet one: it is paying for the exchange)
	CHECK(gained / paid == doctest::Approx(1.0).epsilon(0.05)); // (a ratio: Approx's own tolerance is absolute below 1)
}

TEST_CASE("column: stirred, two layers become one in the time the turning pays for; a stir stopped short leaves them part mixed (Stage 49)") {
	// the user's test: stirred, the layers merged at once. The stirrer stands on the floor: the layer it stirs deepens
	// up the column, through the gradient and into the green.
	Bench b = two_liquids();
	run(b, 2);
	const double start = potential(b);
	REQUIRE(b.sim->world().set_stirring(b.top, 0.002) == Refusal::None); // a fifth of a stirrer's full turning
	run(b, 30); // half a second: 0.2 mJ of the 0.7 that make them one
	const double partly = potential(b);
	CHECK((partly - start) / (0.2 * 0.002 * 0.5) == doctest::Approx(1.0).epsilon(0.05));
	CHECK(used(b) >= 1);
	CHECK(dissolved(b, lowest(b), "Ni") > 1e-3); // the green down in the blue
	REQUIRE(b.sim->world().set_stirring(b.top, 0.0) == Refusal::None);
	run(b, 60); // stopped: it stays as it was mixed (diffusion's micro-joules aside)
	CHECK(std::fabs(potential(b) - partly) < 0.01 * (partly - start));
	CHECK(used(b) >= 1);
	REQUIRE(b.sim->world().set_stirring(b.top, 0.01) == Refusal::None);
	run(b, 600); // ten seconds at a stirrer's full turning: one
	CHECK(used(b) == 0);
	CHECK(dissolved(b, b.top, "Ni") == doctest::Approx(0.064).epsilon(1e-6));
	CHECK(dissolved(b, b.top, "Cu") > 0.11);
	MESSAGE("mixed: " << (potential(b) - start) * 1e3 << " mJ over the layered column's");
}

TEST_CASE("column: a steep step is resolved into thinner layers, each homogeneous, in order of density (Stage 47)") {
	Bench b = beaker([](World::Builder &w, Bench &x) {
		water(w, x.top, 100.0, 293.15);
		w.set_amount(x.layers[0], builtin::water, amount_of(100.0 / 18.015)).set_amount(x.layers[0], builtin::copper_sulfate_aq, amount_of(0.15)).set_temperature(x.layers[0], 293.15);
	});
	run(b, 60 * 600);
	CHECK(used(b) >= 6); // the step resolved by thinner layers next to it (half-millimetre ones, a smooth profile)
	MESSAGE("after ten minutes " << used(b) << " layers under the top");
	double over = density(b, b.top);
	for (const CellId c : column(b)) {
		if (!empty(b, c)) {
			CHECK(density(b, c) >= over * (1.0 - 1e-6));
			over = density(b, c);
		}
	}
}

TEST_CASE("invariants: L1 fires on a layer holding something under an empty one, and on a column holding more than its vessel; T3 on an empty cell holding energy (Stage 47)") {
	Bench b = beaker([](World::Builder &w, Bench &x) {
		water(w, x.top, 100.0, 293.15);
		w.set_amount(x.layers[0], builtin::water, amount_of(50.0 / 18.015)).set_amount(x.layers[0], builtin::copper_sulfate_aq, amount_of(0.075)).set_temperature(x.layers[0], 293.15);
	});
	const StepTrace good = b.sim->step();
	REQUIRE(check_L1(builtin_species(), good).empty());
	StepTrace gap = good;
	std::swap(gap.after.cells[static_cast<size_t>(b.layers[0].index)], gap.after.cells[static_cast<size_t>(b.layers[2].index)]);
	CHECK_FALSE(check_L1(builtin_species(), gap).empty());
	StepTrace full = good;
	full.after.cells[static_cast<size_t>(b.layers[3].index)] = full.after.cells[static_cast<size_t>(b.top.index)];
	full.after.cells[static_cast<size_t>(b.layers[1].index)] = full.after.cells[static_cast<size_t>(b.top.index)];
	full.after.cells[static_cast<size_t>(b.layers[2].index)] = full.after.cells[static_cast<size_t>(b.top.index)];
	CHECK_FALSE(check_L1(builtin_species(), full).empty());
	StepTrace warm = good;
	warm.after.cells[static_cast<size_t>(b.layers[6].index)].energy = 5;
	CHECK_FALSE(check_T3(builtin_species(), warm).empty());
}

TEST_CASE("column: a column grows as it needs, a block of four layers at a time, and a mixed one never does (Stage 47)") {
	// built with one layer: a dense solution under water
	const auto fill = [](World::Builder &w, Bench &x) {
		water(w, x.top, 100.0, 293.15);
		w.set_amount(x.layers[0], builtin::water, amount_of(100.0 / 18.015)).set_amount(x.layers[0], builtin::copper_sulfate_aq, amount_of(0.15)).set_temperature(x.layers[0], 293.15);
	};
	Bench b = beaker(fill, true, 1);
	CHECK(column(b).size() == 1);
	run(b, 1);
	CHECK(column(b).size() == 1 + COLUMN_BLOCK); // the step wanted a spare layer to split into
	// and more, as the gradient is resolved (Stage 63: within the minute; the step resolved first by the thinnest
	// layers beside it, then the next steepest)
	int seconds = 0;
	for (; seconds < 60 && column(b).size() == 1 + COLUMN_BLOCK; ++seconds) {
		run(b, 60);
	}
	CHECK(column(b).size() > 1 + COLUMN_BLOCK);
	CHECK((column(b).size() - 1) % COLUMN_BLOCK == 0);
	MESSAGE("after " << seconds << " seconds the column has " << column(b).size() << " layers, " << used(b) << " in use");
	// a mixed vessel: none
	Bench mixed = beaker([](World::Builder &w, Bench &x) { water(w, x.top, 150.0, 330.0); }, true, 0);
	run(mixed, 600);
	CHECK(column(mixed).empty());
}


// ---- a pour's stream (Stage 51)

TEST_CASE("measured: a round jet entrains at Ricou and Spalding's 0.32 Q0 x / d0; one driven into a denser liquid plunges to Turner's 2.46 M^(3/4) B^(-1/2) (Stage 51)") {
	// Ricou and Spalding 1961 (J. Fluid Mech. 11, 21-32): a jet of volume flux Q0 from a nozzle of diameter d0 carries
	// 0.32 Q0 x / d0 at x diameters down (of the same density as what it passes). Turner 1966 (J. Fluid Mech. 26,
	// 779-792): a forced fountain rises to 2.46 M^(3/4) B^(-1/2). Each at its source's own form, for a few jets
	// (docs/DATA_SOURCES.md).
	for (const double d0 : { 0.002, 0.005, 0.01 }) {
		for (const double u0 : { 0.3, 1.2, 3.0 }) {
			const double q0 = 3.14159265358979323846 * d0 * d0 / 4.0 * u0;
			const double x = 20.0 * d0;
			CHECK(jet_entrains(q0, u0) * x / (0.32 * q0 * x / d0) == doctest::Approx(1.0).epsilon(1e-12));
			const double buoyancy = 9.80665 * 0.08 * q0, momentum = q0 * u0;
			CHECK(fountain_reach(momentum, buoyancy) / (2.46 * std::pow(momentum, 0.75) / std::sqrt(buoyancy)) == doctest::Approx(1.0).epsilon(1e-12));
		}
	}
	CHECK(jet_entrains(0.0, 1.0) == 0.0);
	CHECK(fountain_reach(1e-5, 0.0) == 0.0);
}

TEST_CASE("measured: a stream landing on a layered column raises its potential energy by Linden's 0.2 of its kinetic energy (Stage 51)") {
	// The stream's kinetic energy, half its mass times its landing speed squared, is the work it does on the column;
	// 0.2 of it (Linden 1979, as a stirrer's, Stage 49) goes to the column's potential energy, its entrainment first.
	// Water at 20 mL/s landing at 1.2 m/s (a 7 cm fall) on water over copper sulfate, a step; against a copy with the
	// same pour landing at no speed.
	const auto bench = [](double speed) {
		EdgeId into;
		Bench b = beaker([&](World::Builder &w, Bench &x) {
			water(w, x.top, 60.0, 293.15);
			w.set_amount(x.layers[0], builtin::water, amount_of(80.0 / 18.015)).set_amount(x.layers[0], builtin::copper_sulfate_aq, amount_of(0.112)).set_temperature(x.layers[0], 293.15);
			const CellId bottle = w.add_cell("bottle");
			water(w, bottle, 200.0, 293.15);
			into = w.add_drain(bottle, x.top);
		});
		REQUIRE(b.sim->world().set_drain(into, 20e-6, 0.0) == Refusal::None);
		REQUIRE(b.sim->world().set_landing(b.top, speed) == Refusal::None);
		return b;
	};
	Bench jet = bench(1.2), still = bench(0.0);
	const double before = potential(jet);
	REQUIRE(potential(still) == doctest::Approx(before).epsilon(1e-12));
	run(jet, 1);
	run(still, 1);
	const double gained = potential(jet) - potential(still);
	const double landed_kg = 20e-6 * STEP_SECONDS * 18.015e-3 / builtin_species().species(builtin::water).molar_volume;
	const double paid = 0.2 * 0.5 * landed_kg * 1.2 * 1.2;
	MESSAGE("gained " << gained * 1e6 << " uJ of " << paid * 1e6 << " uJ paid; lifted " << dissolved(jet, jet.top, "Cu") << " mol of copper sulfate");
	CHECK(gained / paid == doctest::Approx(1.0).epsilon(0.05)); // (a ratio: Approx's own tolerance is absolute below 1)
	CHECK(dissolved(jet, jet.top, "Cu") > 0.0);
}

TEST_CASE("column: a stream landing slowly, or on a liquid much denser, plunges less far: Turner's fountain (Stage 51)") {
	// a trickle at 2 mL/s landing at 0.3 m/s into the water over the blue: the fountain's reach, 2.46 M^(3/4) B^(-1/2),
	// is under the blue's depth; it lifts less than the whole depth's share
	EdgeId into;
	Bench b = beaker([&](World::Builder &w, Bench &x) {
		water(w, x.top, 60.0, 293.15);
		w.set_amount(x.layers[0], builtin::water, amount_of(80.0 / 18.015)).set_amount(x.layers[0], builtin::copper_sulfate_aq, amount_of(0.112)).set_temperature(x.layers[0], 293.15);
		const CellId bottle = w.add_cell("bottle");
		water(w, bottle, 200.0, 293.15);
		into = w.add_drain(bottle, x.top);
	});
	REQUIRE(b.sim->world().set_drain(into, 2e-6, 0.0) == Refusal::None);
	REQUIRE(b.sim->world().set_landing(b.top, 0.3) == Refusal::None);
	run(b, 1);
	const double area = b.sim->world().meta(b.top).column_area;
	const double whole = 0.112 * 0.32 * 0.8862269254527580 * std::sqrt(2e-6 * 0.3) * STEP_SECONDS / area;
	const double lifted = dissolved(b, b.top, "Cu");
	MESSAGE("a trickle lifts " << lifted << " mol; through the whole blue it would " << whole);
	CHECK(lifted > 0.0);
	CHECK(lifted < 0.8 * whole);
}

// ---- a column's shape (Stage 51)

TEST_CASE("column: a column that narrows takes its layers' heights and its interfaces' areas from its shape: diffusion across a narrow neck is slower (Stage 51)") {
	// A column of the floor's area A0 for 2 cm, then of half of it. Copper sulfate fills it to 3 cm (2 cm of the wide,
	// 1 cm of the narrow), water over it 2 cm more. By its shape the interface is in the neck (A0 / 2) and the middles
	// 2.75 cm apart (1.25 cm and 4 cm); as a cylinder of A0 the same volumes stand 1.75 cm apart across A0. Fick's
	// first step moves D A dc / dz: the narrowing's (1/2 / 2.75) / (1 / 1.75) of the cylinder's.
	const double a0 = PI * RADIUS * RADIUS, rise = 0.001;
	const double lower = a0 * 0.02 + 0.5 * a0 * 0.01, upper = 0.5 * a0 * 0.02; // m^3
	const auto bench = [&](bool shaped) {
		return beaker([&](World::Builder &w, Bench &x) {
			const double water_l = lower / builtin_species().species(builtin::water).molar_volume, water_u = upper / builtin_species().species(builtin::water).molar_volume;
			w.set_amount(x.top, builtin::water, amount_of(water_u)).set_temperature(x.top, 293.15);
			w.set_amount(x.layers[0], builtin::water, amount_of(water_l)).set_amount(x.layers[0], builtin::copper_sulfate_aq, amount_of(0.05)).set_temperature(x.layers[0], 293.15);
			if (shaped) {
				std::vector<double> volumes;
				for (int k = 0; k <= 80; ++k) {
					const double z = k * rise;
					volumes.push_back(z <= 0.02 ? a0 * z : a0 * 0.02 + 0.5 * a0 * (z - 0.02));
				}
				w.set_column_shape(x.top, rise, volumes);
			}
		});
	};
	Bench narrow = bench(true), straight = bench(false);
	// the layers' own volumes (the solute's adds to the water's), stacked by each shape
	const auto volume_of = [&](CellId c) {
		double v = 0.0;
		const CellState &state = narrow.sim->world().snapshot_cell(c);
		for (size_t k = 0; k < state.amount.size(); ++k) {
			const SpeciesDef &d = builtin_species().species(SpeciesId{ static_cast<int32_t>(k) });
			if (state.amount[k] > 0 && (d.phase == Phase::Liquid || d.phase == Phase::Aqueous)) {
				v += static_cast<double>(state.amount[k]) * MOL_PER_QUANTUM * d.molar_volume;
			}
		}
		return v;
	};
	const double vl = volume_of(narrow.layers[0]), vu = volume_of(narrow.top);
	const auto height = [&](double v) { return v <= a0 * 0.02 ? v / a0 : 0.02 + (v - a0 * 0.02) / (0.5 * a0); };
	const double dz_narrow = height(vl + 0.5 * vu) - height(0.5 * vl), across = height(vl) <= 0.02 ? a0 : 0.5 * a0;
	const double dz_straight = (vl + 0.5 * vu) / a0 - 0.5 * vl / a0;
	const double expected = (across / dz_narrow) / (a0 / dz_straight);
	run(narrow, 1);
	run(straight, 1);
	// (the copper that crosses, in both its forms: some hydrolyses in the water it reaches, Stage 47)
	const auto copper = [&](const Bench &b) { return dissolved(b, b.top, "Cu") + mol(b, b.top, builtin::copper_hydroxide); };
	const double ratio = copper(narrow) / copper(straight);
	MESSAGE("through the neck " << ratio << " of the cylinder's; by the shapes " << expected << " (" << dz_narrow * 100.0 << " cm against " << dz_straight * 100.0 << " cm)");
	CHECK(expected < 0.4); // (the neck's)
	CHECK(ratio / expected == doctest::Approx(1.0).epsilon(0.01));
}
