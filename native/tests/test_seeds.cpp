// Seeds (GAME_SPEC §9, Stage 66; docs/CURRICULUM.md, C16 and experiment 35): a stirred solution's crystals breed.
// Fragments the stirring chips off them grow as crystals of their own (secondary nucleation), secondary * (W per kg
// of the liquid) * (kg of the crystals) * (S - 1)^secondary_order a second; a still solution's seed only grows.
#include "chem/builtin_ids.hpp"
#include "chem/invariants.hpp"
#include "chem/sim.hpp"
#include "chem/thermo.hpp"

#include <doctest/doctest.h>

#include <cmath>
#include <optional>
#include <string>
#include <vector>

using namespace chem;

namespace {

constexpr double ROOM = 298.15, WATER_G = 100.0, PENTAHYDRATE = 249.677; // K, g, g/mol

Amount mol(double n) {
	bool ok = false;
	const Amount a = amount_from_mol(n, ok);
	REQUIRE(ok);
	return a;
}

const Reaction &growth() {
	return builtin_reactions()[static_cast<size_t>(builtin::reaction::pentahydrate_crystallization)];
}

double affinity_of(const CellState &state, double kelvin) {
	bool defined = false;
	const double a = affinity(builtin_species(), state, growth().terms(), kelvin, defined);
	REQUIRE(defined);
	return a;
}

// the model's own solubility at 25 C, mol/kg: where the pentahydrate neither grows nor dissolves
double saturated() {
	const SpeciesTable &table = builtin_species();
	const auto at = [&](double m) {
		CellState state;
		state.amount.assign(static_cast<size_t>(table.species_count()), 0);
		state.particles.assign(static_cast<size_t>(table.species_count()), 0);
		state.amount[static_cast<size_t>(builtin::water.index)] = mol(1000.0 / 18.015).quanta;
		put_amount(table, state, builtin::copper_sulfate_aq, mol(m).quanta);
		return affinity_of(state, ROOM);
	};
	double lo = 0.5, hi = 3.0;
	for (int i = 0; i < 60; ++i) {
		const double mid = 0.5 * (lo + hi);
		(at(mid) > 0.0 ? hi : lo) = mid;
	}
	return 0.5 * (lo + hi);
}

// 100 g of water held at 25 C, copper sulfate at `over` times its saturation, a seed of the pentahydrate in one
// crystal, stirred by `watts` (the lab's stirrer turns 0.01 W into a beaker)
struct Beaker {
	std::optional<Simulation> sim;
	CellId cell;
};

Beaker beaker(double over, double seed_mg, double watts, bool checking) {
	const SpeciesTable &table = builtin_species();
	World::Builder b(table);
	const CellId cell = b.add_rigid_cell("beaker", 250e-6);
	const CellId room = b.add_reservoir("room", ROOM);
	b.add_edge(cell, room, PHASES_NONE, 50.0);
	b.set_amount(cell, builtin::water, mol(WATER_G / 18.015)).set_amount(cell, builtin::copper_sulfate_aq, mol(saturated() * over * WATER_G * 1e-3));
	if (seed_mg > 0.0) {
		b.set_amount(cell, builtin::copper_sulfate_pentahydrate, mol(seed_mg * 1e-3 / PENTAHYDRATE)).set_particles(cell, builtin::copper_sulfate_pentahydrate, 1);
	}
	const double n = P_ATMOSPHERE * (250e-6 - 120e-6) / (GAS_CONSTANT * ROOM);
	b.set_amount(cell, builtin::nitrogen, mol(0.79 * n)).set_amount(cell, builtin::oxygen, mol(0.21 * n));
	b.set_temperature(cell, ROOM);
	std::string why;
	auto world = b.build(&why);
	REQUIRE_MESSAGE(world.has_value(), why);
	Beaker out;
	out.sim.emplace(std::move(*world), std::vector<Reaction>(builtin_reactions().begin(), builtin_reactions().end()));
	out.sim->set_checking(checking);
	out.cell = cell;
	REQUIRE(out.sim->world().set_stirring(cell, watts) == Refusal::None);
	return out;
}

double crystals_g(const Beaker &x) {
	return x.sim->world().amount(x.cell, builtin::copper_sulfate_pentahydrate).mol() * PENTAHYDRATE;
}

int64_t crystals(const Beaker &x) {
	return x.sim->world().particles(x.cell, builtin::copper_sulfate_pentahydrate);
}

double supersaturation(const Beaker &x) {
	double kelvin = 0.0;
	REQUIRE(x.sim->world().temperature(x.cell, kelvin));
	return std::exp(affinity_of(x.sim->world().snapshot_cell(x.cell), kelvin) / (GAS_CONSTANT * kelvin));
}

void run(Beaker &x, double seconds) {
	for (int i = 0; i < static_cast<int>(seconds / STEP_SECONDS + 0.5); ++i) {
		x.sim->step();
	}
}

} // namespace

TEST_CASE("seeds: a still solution's seed only grows, one crystal (Stage 66)") {
	Beaker x = beaker(1.05, 10.0, 0.0, true);
	run(x, 1800.0);
	CHECK(crystals(x) == 1);
	CHECK(crystals_g(x) > 0.0101);
	CHECK(crystals_g(x) < 0.0110);
	CHECK(supersaturation(x) > 1.05);
}

TEST_CASE("seeds: stirred, a seed breeds and sets out the solution's excess (Stage 66)") {
	// the calibration (docs/DATA_SOURCES.md, Seeds): half the excess out in about a quarter of an hour, the rest within
	// half an hour, in crystals under 100 um (Justel et al. 2019: 95-98 % of a stirred crop under 100 um)
	Beaker x = beaker(1.05, 10.0, 0.01, true);
	const double s0 = supersaturation(x);
	run(x, 600.0);
	CHECK(crystals(x) > 1000);
	CHECK(crystals_g(x) < 0.6); // still mostly in the solution at ten minutes
	run(x, 600.0);
	const double out_20 = crystals_g(x);
	run(x, 1200.0);
	const double out_40 = crystals_g(x);
	MESSAGE("S " << s0 << " -> " << supersaturation(x) << "; crystals " << out_20 << " g at 20 min, " << out_40 << " g in " << crystals(x) << " at 40 min");
	CHECK(out_20 > 0.5 * out_40);
	CHECK(supersaturation(x) < 1.001);
	CHECK(supersaturation(x) > 0.999);
	const double diameter = particle_diameter(builtin_species(), x.sim->world().snapshot_cell(x.cell), builtin::copper_sulfate_pentahydrate);
	CHECK(diameter > 30e-6);
	CHECK(diameter < 100e-6);
}

TEST_CASE("seeds: the crystals bred go with the stirring, the crystals' mass and (S - 1)^1.7 (Stage 66)") {
	const Kinetics &k = growth().kinetics();
	REQUIRE(k.secondary > 0.0);
	CHECK(k.secondary_order == doctest::Approx(1.7));
	// ten seconds: the crystals bred, while the mass and the supersaturation have hardly moved
	const auto bred = [](double over, double seed_mg, double watts) {
		Beaker x = beaker(over, seed_mg, watts, false);
		run(x, 10.0);
		return static_cast<double>(crystals(x) - 1);
	};
	const double base = bred(1.05, 10.0, 0.01);
	MESSAGE("crystals bred in 10 s: " << base);
	REQUIRE(base > 300.0);
	CHECK(bred(1.05, 10.0, 0.02) / base == doctest::Approx(2.0).epsilon(0.03));
	CHECK(bred(1.05, 20.0, 0.01) / base == doctest::Approx(2.0).epsilon(0.03));
	const double s1 = supersaturation(beaker(1.05, 10.0, 0.01, false)), s2 = supersaturation(beaker(1.10, 10.0, 0.01, false));
	CHECK(bred(1.10, 10.0, 0.01) / base == doctest::Approx(std::pow((s2 - 1.0) / (s1 - 1.0), 1.7)).epsilon(0.03));
}

TEST_CASE("seeds: none are bred unstirred, without crystals, or where they dissolve (Stage 66)") {
	CHECK(crystals([] {
		Beaker x = beaker(1.05, 10.0, 0.0, false);
		run(x, 60.0);
		return x;
	}()) == 1);
	// stirred without a crystal: only the solution's own nucleation, none in a minute this near saturation
	{
		Beaker x = beaker(1.05, 0.0, 0.01, false);
		run(x, 60.0);
		CHECK(crystals(x) == 0);
	}
	// stirred under saturation: the seed dissolves, still one crystal
	{
		Beaker x = beaker(0.95, 10.0, 0.01, true);
		run(x, 60.0);
		CHECK(crystals(x) == 1);
		CHECK(crystals_g(x) < 0.0099);
	}
}
