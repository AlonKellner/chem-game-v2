// Ice (GAME_SPEC §8, §9, Stage 71; docs/CURRICULUM.md, C8): H2O(s), its melting and freezing as a surface law limited
// by the heat its change carries, its first crystal by a nucleation barrier (water supercools), its growth into a
// supercooled liquid as a mush of grains, and its floating on water. Held to IAPWS R10-06, the International Critical
// Tables' freezing points and Wilson and Haymet's supercooling (docs/DATA_SOURCES.md, Ice).
#include "chem/builtin_ids.hpp"
#include "chem/invariants.hpp"
#include "chem/sim.hpp"
#include "chem/thermo.hpp"

#include <doctest/doctest.h>

#include <cmath>
#include <memory>
#include <string>
#include <vector>

using namespace chem;

namespace {

Amount mol(double n) {
	bool ok = false;
	const Amount a = amount_from_mol(n, ok);
	REQUIRE(ok);
	return a;
}

const Reaction &freezing() {
	return builtin_reactions()[static_cast<size_t>(builtin::reaction::water_freezing)];
}

std::vector<Reaction> shipped() {
	return std::vector<Reaction>(builtin_reactions().begin(), builtin_reactions().end());
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

// the affinity of water freezing in a kilogram of water with `molality` of a salt, at `kelvin`
double affinity_of(SpeciesId salt, double molality, double kelvin) {
	const SpeciesTable &table = builtin_species();
	CellState state;
	state.amount.assign(static_cast<size_t>(table.species_count()), 0);
	state.particles.assign(static_cast<size_t>(table.species_count()), 0);
	state.amount[static_cast<size_t>(builtin::water.index)] = mol(1000.0 / 18.015).quanta;
	if (molality > 0.0) {
		put_amount(table, state, salt, mol(molality).quanta);
	}
	bool defined = false;
	const double a = affinity(table, state, freezing().terms(), kelvin, defined);
	REQUIRE(defined);
	return a;
}

// where ice and the solution's water have one potential
double freezing_point(SpeciesId salt, double molality) {
	double lo = 230.0, hi = 274.0;
	for (int i = 0; i < 60; ++i) {
		const double mid = 0.5 * (lo + hi);
		(affinity_of(salt, molality, mid) > 0.0 ? lo : hi) = mid;
	}
	return 0.5 * (lo + hi);
}

// a glass tube of 0.3 g of water at 5 C, cooled through `conductance` from a -40 C bath (about 4.4 K/min at 0.005 W/K
// as it passes -10 C, Wilson and Haymet's tubes' rate)
struct Tube {
	std::unique_ptr<Simulation> sim;
	CellId cell;
	double kelvin() const {
		double t = 0.0;
		sim->world().temperature(cell, t);
		return t;
	}
	double ice_g() const { return sim->world().amount(cell, builtin::ice).mol() * 18.015; }
};

Tube tube(double conductance, double water_g = 0.3, bool checking = false) {
	const SpeciesTable &table = builtin_species();
	World::Builder b(table);
	const CellId cell = b.add_rigid_cell("tube", 2e-6);
	const CellId bath = b.add_reservoir("bath", 233.15);
	b.add_edge(cell, bath, PHASES_NONE, conductance);
	b.set_amount(cell, builtin::water, mol(water_g / 18.015)).set_amount(cell, builtin::glass, mol(1.0 / 60.083));
	b.set_amount(cell, builtin::nitrogen, mol(P_ATMOSPHERE * (2e-6 - water_g * 1e-6) / (GAS_CONSTANT * 293.15)));
	b.set_temperature(cell, 278.15);
	std::string why;
	auto world = b.build(&why);
	REQUIRE_MESSAGE(world, why);
	Tube out;
	out.sim = std::make_unique<Simulation>(std::move(*world), shipped());
	out.sim->set_checking(checking);
	out.cell = cell;
	return out;
}

// cooled until its first ice: the temperature then, K, and the rate it was cooling at, K/min
std::pair<double, double> first_ice(Tube &t) {
	double before = t.kelvin();
	for (int step = 0; step < 60 * 3600; ++step) {
		const double now = t.kelvin();
		if (t.ice_g() > 0.0) {
			return { now, (before - now) * 3600.0 };
		}
		before = now;
		t.sim->step();
	}
	FAIL("no ice in an hour");
	return { 0.0, 0.0 };
}

} // namespace

TEST_CASE("measured: water freezes at IAPWS's point with its heat of fusion, ice its entropy there (Stage 71)") {
	// IAPWS R10-06(2009), Table 6 and section 5: 273.152519 K at 101 325 Pa, 333.427 kJ/kg (6.0068 kJ/mol) with the
	// liquid's (IAPWS-95, NIST WebBook); ice's absolute entropy at it, 41.36 J/(mol K)
	const double tm = freezing_point(builtin::water, 0.0);
	const SpeciesDef &w = builtin_species().species(builtin::water), &i = builtin_species().species(builtin::ice);
	const double dh = (w.hf - i.hf) + (w.cp - i.cp) * (tm - T0);
	MESSAGE("freezes at " << tm << " K, " << dh << " J/mol");
	CHECK(tm == doctest::Approx(273.152519).epsilon(2e-6)); // half a millikelvin
	CHECK(dh == doctest::Approx(6006.8).epsilon(1e-4));
	CHECK(i.s0 - i.cp * std::log(T0 / tm) == doctest::Approx(41.36).epsilon(5e-4));
	CHECK(i.cp == doctest::Approx(2096.71 * 0.0180153).epsilon(1e-4));
	CHECK(i.molar_volume == doctest::Approx(0.0180153 / 916.721).epsilon(1e-4));
	CHECK(i.conductivity == doctest::Approx(612.0 / 273.15).epsilon(2e-3)); // Carnahan et al. 2021: k = 612 / T
}

TEST_CASE("measured: solutions freeze at the International Critical Tables' points (Stage 71)") {
	// ICT IV (1928), Hall and Sherrill, pp. 256 and 258: the lowering at each molality, Nw (Δt/N)
	const double tm = freezing_point(builtin::water, 0.0);
	const struct {
		SpeciesId salt;
		double m, lowering, within;
	} rows[] = {
		{ builtin::copper_sulfate_aq, 0.01, 0.0270, 0.02 }, { builtin::copper_sulfate_aq, 0.05, 0.1133, 0.02 }, { builtin::copper_sulfate_aq, 0.10, 0.2085, 0.02 },
		{ builtin::copper_sulfate_aq, 0.20, 0.3824, 0.02 }, { builtin::copper_sulfate_aq, 0.50, 0.861, 0.02 }, { builtin::copper_sulfate_aq, 0.80, 1.352, 0.02 },
		{ builtin::sodium_chloride_aq, 0.01, 0.0360, 0.02 }, { builtin::sodium_chloride_aq, 0.10, 0.348, 0.02 }, { builtin::sodium_chloride_aq, 0.40, 1.352, 0.02 },
		// Pitzer's 25 C parameters at 0 C: the lowering 3 % high at 1 mol/kg, 9 % at 4
		{ builtin::sodium_chloride_aq, 1.0, 3.37, 0.04 }, { builtin::sodium_chloride_aq, 2.0, 6.90, 0.07 }, { builtin::sodium_chloride_aq, 4.0, 15.14, 0.10 },
	};
	for (const auto &r : rows) {
		const double lowering = tm - freezing_point(r.salt, r.m);
		INFO(builtin_species().species(r.salt).id << " at " << r.m << " mol/kg: " << lowering << " K against " << r.lowering);
		CHECK(lowering == doctest::Approx(r.lowering).epsilon(r.within));
	}
}

TEST_CASE("measured: water supercools in a glass tube to Wilson and Haymet's median, colder as it cools faster (Stage 71)") {
	// Wilson and Haymet (2016), Table 1: 300 uL in borosilicate tubes at 4.5 K/min, median -9.4 to -12.0 C over six
	// tubes, -10.45 C their mean; Vali (2008): 0.4 K colder at six times the rate
	Tube slow = tube(0.005);
	const auto [at, rate] = first_ice(slow);
	MESSAGE("first ice at " << at - 273.15 << " C, cooling at " << rate << " K/min");
	CHECK(rate == doctest::Approx(4.5).epsilon(0.1));
	CHECK(at - 273.15 == doctest::Approx(-10.45).epsilon(0.03));
	Tube fast = tube(0.04);
	const auto [colder, faster] = first_ice(fast);
	MESSAGE("at " << faster << " K/min: " << colder - 273.15 << " C");
	CHECK(faster / rate > 4.0);
	CHECK(at - colder > 0.2);
	CHECK(at - colder < 0.8);
}

TEST_CASE("ice: a supercooled tube jumps back to its freezing point and holds there while it freezes, every invariant held (Stage 71)") {
	Tube t = tube(0.005, 0.3, true);
	const auto start = first_ice(t);
	const double tm = freezing_point(builtin::water, 0.0);
	for (int step = 0; step < 60 * 20; ++step) {
		t.sim->step();
	}
	MESSAGE("from " << start.first - 273.15 << " C to " << t.kelvin() - 273.15 << " C in 20 s, " << t.ice_g() << " g of ice");
	CHECK(t.kelvin() > tm - 0.05); // recalescence: back within 50 mK in 20 s
	CHECK(t.ice_g() > 0.03); // the heat of what froze warmed it: about c dT / L of it
	// the plateau: held at its freezing point while the bath draws the heat away, until it is ice
	double least = 1e9, most = 0.0;
	while (t.ice_g() < 0.27) {
		least = std::min(least, t.kelvin());
		most = std::max(most, t.kelvin());
		t.sim->step();
	}
	MESSAGE("the plateau from " << least - 273.15 << " to " << most - 273.15 << " C");
	CHECK(least > tm - 0.05);
	CHECK(most < tm + 1e-3);
	CHECK(t.sim->world().particles(t.cell, builtin::ice) > 1000); // a mush of grains, not one crystal
}

TEST_CASE("ice: melting in water is limited by the heat the water carries to it, Nu 2 at rest and more stirred (Stage 71)") {
	const SpeciesTable &table = builtin_species();
	// 5 g of ice in one 1 cm lump in 100 g of water at 10 C
	const auto steps_of = [&](double dissipation) {
		CellState state;
		state.amount.assign(static_cast<size_t>(table.species_count()), 0);
		state.particles.assign(static_cast<size_t>(table.species_count()), 0);
		state.amount[static_cast<size_t>(builtin::water.index)] = mol(100.0 / 18.015).quanta;
		state.amount[static_cast<size_t>(builtin::ice.index)] = mol(5.0 / 18.015).quanta;
		state.particles[static_cast<size_t>(builtin::ice.index)] = 1;
		bool defined = false;
		const double a = affinity(table, state, freezing().terms(), 283.15, defined);
		REQUIRE(defined);
		REQUIRE(a < 0.0); // it melts
		return std::pair{ surface_steps(table, state, freezing(), 283.15, a, dissipation), a };
	};
	const auto [still, a] = steps_of(0.0);
	REQUIRE(still.transported);
	const double d = particle_diameter(table, [&] {
		CellState s;
		s.amount.assign(static_cast<size_t>(table.species_count()), 0);
		s.particles.assign(static_cast<size_t>(table.species_count()), 0);
		s.amount[static_cast<size_t>(builtin::ice.index)] = mol(5.0 / 18.015).quanta;
		s.particles[static_cast<size_t>(builtin::ice.index)] = 1;
		return s;
	}(), builtin::ice);
	const SolventPoint w = solvent_at(table, 283.15);
	const SpeciesDef &water = table.species(builtin::water), &ice = table.species(builtin::ice);
	const double dh = (water.hf - ice.hf) + (water.cp - ice.cp) * (283.15 - T0);
	const double h = still.transport * dh * dh / (-a * 283.15); // W/m^2 K: the heat the step carries per kelvin
	MESSAGE("a " << d * 1e3 << " mm lump: h " << h << " W/m2K at rest");
	CHECK(h == doctest::Approx(2.0 * w.k / d).epsilon(1e-9));
	CHECK(still.both < still.surface); // the heat limits, not ice's own surface
	const auto [stirred, unused] = steps_of(1.0);
	(void)unused;
	const double h_stirred = stirred.transport * dh * dh / (-a * 283.15);
	MESSAGE("stirred at 1 W/kg: h " << h_stirred << " W/m2K (Machicoane et al.'s 18 mm ball in turbulent water: 5380)");
	CHECK(h_stirred > 20.0 * h);
	CHECK(h_stirred > 1000.0);
	CHECK(h_stirred < 20000.0);
}

TEST_CASE("ice: floats up a column through a denser solution to its topmost liquid layer, a denser solid still sinks (Stage 71)") {
	const SpeciesTable &table = builtin_species();
	World::Builder b(table);
	const CellId air = b.add_cell("air");
	b.set_amount(air, builtin::nitrogen, mol(10.0)).set_amount(air, builtin::oxygen, mol(2.6)).set_temperature(air, 273.25);
	const CellId top = b.add_rigid_cell("beaker", 250e-6);
	b.add_edge(top, air, PHASES_GAS);
	b.set_amount(top, builtin::glass, mol(80.0 / 60.08)).set_temperature(top, 273.25);
	std::vector<CellId> layers;
	CellId over = top;
	for (int i = 0; i < 4; ++i) {
		const CellId layer = b.add_layer_cell("layer " + std::to_string(i + 1));
		b.add_column_edge(layer, over);
		b.set_amount(layer, builtin::water, mol(40.0 / 18.015)).set_temperature(layer, 273.25);
		if (i >= 2) { // the lower two a denser solution, 1.2 mol/kg: the column keeps it under the water
			b.set_amount(layer, builtin::copper_sulfate_aq, mol(1.2 * 0.040));
		}
		layers.push_back(layer);
		over = layer;
	}
	b.set_column(top, 3.14159265358979 * 0.033 * 0.033);
	b.set_amount(layers.back(), builtin::ice, mol(2.0 / 18.015)).set_particles(layers.back(), builtin::ice, 1);
	b.set_amount(layers.front(), builtin::copper_oxide, mol(0.5 / 79.545)).set_particles(layers.front(), builtin::copper_oxide, 1000);
	std::string why;
	auto world = b.build(&why);
	REQUIRE_MESSAGE(world, why);
	Simulation sim(std::move(*world), shipped());
	sim.set_checking(false);
	const std::vector<Reaction> eq = equilibria();
	for (int s = 0; s < 120; ++s) {
		const StepTrace trace = sim.step();
		const Violations v = check_all(table, trace, eq);
		REQUIRE_MESSAGE(v.empty(), v.front());
	}
	// the topmost layer with liquid holds the ice, the lowest the oxide
	CellId uppermost{}, lowest{};
	if (sim.world().amount(top, builtin::water).quanta > 0) {
		uppermost = lowest = top;
	}
	for (CellId at = top;;) {
		CellId next{};
		for (const Edge &e : sim.world().edges()) {
			if (e.open && e.column && e.b == at) {
				next = e.a;
			}
		}
		if (next.index < 0) {
			break;
		}
		if (sim.world().amount(next, builtin::water).quanta > 0) {
			uppermost = uppermost.index < 0 ? next : uppermost;
			lowest = next;
		}
		at = next;
	}
	REQUIRE(uppermost.index >= 0);
	CHECK(sim.world().amount(uppermost, builtin::ice).quanta > 0);
	CHECK(sim.world().amount(lowest, builtin::copper_oxide).quanta > 0);
	CHECK(sim.world().amount(lowest, builtin::ice).quanta == 0);
}
