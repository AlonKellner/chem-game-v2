// The vertical dimension (GAME_SPEC §6, Stage 47): a vessel's column of layers. Mixed it is one cell, as every
// vessel was; layered it keeps a denser liquid under a lighter one, passes heat and what is dissolved between them,
// overturns where the lower is the lighter, lets gas rise and solids sink, and resolves a steep step into thinner
// layers. Every invariant holds at every step.
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
	out.sim->set_checking(true);
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
	const auto copper = [&](CellId c) { return mol(b, c, builtin::copper_sulfate_aq) + mol(b, c, builtin::copper_hydroxide); };
	double rose = copper(b.top);
	for (const CellId c : column(b)) {
		rose += c == lowest(b) ? 0.0 : copper(c);
	}
	MESSAGE("the first step moves " << rose << " mol up; Fick: " << expected);
	CHECK(rose == doctest::Approx(expected).epsilon(0.02));
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
	CHECK(mol(dense_on_top, dense_on_top.top, builtin::copper_sulfate_aq) == doctest::Approx(0.075));
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

TEST_CASE("column: gas in a layer rises, solids in the top sink into the liquid under them, and stirring makes the column one (Stage 47)") {
	Bench b = beaker([](World::Builder &w, Bench &x) {
		water(w, x.top, 100.0, 293.15);
		w.set_amount(x.top, builtin::copper_oxide, amount_of(0.01));
		w.set_amount(x.layers[0], builtin::water, amount_of(50.0 / 18.015)).set_amount(x.layers[0], builtin::copper_sulfate_aq, amount_of(0.075));
		w.set_amount(x.layers[0], builtin::carbon_dioxide, amount_of(0.001)).set_temperature(x.layers[0], 293.15);
	});
	run(b, 2);
	CHECK(mol(b, b.layers[0], builtin::carbon_dioxide) == 0.0);
	CHECK(mol(b, b.top, builtin::copper_oxide) == 0.0);
	CHECK(mol(b, lowest(b), builtin::copper_oxide) == doctest::Approx(0.01).epsilon(1e-3));
	REQUIRE(b.sim->world().set_mixing(b.top, 3.0) == Refusal::None);
	run(b, 1);
	CHECK(used(b) == 0);
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
	run(b, 600);
	CHECK(column(b).size() > 1 + COLUMN_BLOCK); // and more, as the gradient is resolved
	CHECK((column(b).size() - 1) % COLUMN_BLOCK == 0);
	MESSAGE("after ten seconds the column has " << column(b).size() << " layers, " << used(b) << " in use");
	// a mixed vessel: none
	Bench mixed = beaker([](World::Builder &w, Bench &x) { water(w, x.top, 150.0, 330.0); }, true, 0);
	run(mixed, 600);
	CHECK(column(mixed).empty());
}

