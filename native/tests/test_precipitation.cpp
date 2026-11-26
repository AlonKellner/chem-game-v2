// Precipitation and gas (GAME_SPEC §9, Stage 44): copper hydroxide out of a copper sulfate solution and a base,
// back into an acid, into copper oxide when heated; malachite in an acid, its carbonate leaving as a gas. The
// tables held to the evaluated solubility constants, the rates to their measurements.
#include "tables.hpp"
#include "chem/builtin_ids.hpp"
#include "chem/invariants.hpp"
#include "chem/sim.hpp"
#include "chem/thermo.hpp"

#include <doctest/doctest.h>

#include <cmath>
#include <vector>

using namespace chem;

namespace {

int64_t quanta(double n) {
	bool ok = false;
	const Amount a = amount_from_mol(n, ok);
	REQUIRE(ok);
	return a.quanta;
}

const Reaction &reaction(int index) {
	return builtin_reactions()[static_cast<size_t>(index)];
}

bool near(double a, double b, double rel) {
	return b != 0.0 && std::abs(a / b - 1.0) < rel;
}

// a kilogram of water and what is put in it
struct Cell {
	CellState state{ std::vector<int64_t>(static_cast<size_t>(builtin_species().species_count()), 0), 0,
		std::vector<int64_t>(static_cast<size_t>(builtin_species().species_count()), 0) };
	Cell() { put(builtin::water, 1.0 / 0.018015); }
	Cell &put(SpeciesId s, double mol, int64_t particles = 0) {
		testing::put_amount(builtin_species(), state, s, quanta(mol));
		state.particles[static_cast<size_t>(s.index)] = particles;
		return *this;
	}
};

double affinity_of(const Cell &c, const Reaction &r, double kelvin) {
	bool defined = false;
	const double a = affinity(builtin_species(), c.state, r.terms(), kelvin, defined);
	REQUIRE(defined);
	return a;
}

// a reaction's standard Gibbs energy at 25 C from the tables, J/mol
double standard_g(const Reaction &r) {
	double g = 0.0;
	for (const Term &t : r.terms()) {
		const SpeciesDef &s = builtin_species().species(t.species);
		g += t.nu * (s.hf - 298.15 * s.s0);
	}
	return g;
}

// log10 of a solid's *Ks, M(OH)2 or MO + 2 H+ -> Cu2+ + water, from the tables: the solid with the acid to the salt
double log_star_ks(const Reaction &with_acid) {
	return -standard_g(with_acid) / (GAS_CONSTANT * 298.15 * std::log(10.0));
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

double mol(const CellState &c, SpeciesId s) {
	return static_cast<double>(c.amount[static_cast<size_t>(s.index)]) * MOL_PER_QUANTUM;
}

// the hydroxide a state holds bound and free, less the hydrogen ions freed (Stage 63): 2 Cu(OH)2 + CuOH+ + OH- -
// H+ - HSO4-, the base put in less the acid's hydrogen, by its balance of protons
double bound_hydroxide(const CellState &c) {
	return 2.0 * mol(c, builtin::copper_hydroxide) + mol(c, builtin::copper_hydroxide_ion) +
			mol(c, builtin::hydroxide_ion) - mol(c, builtin::hydrogen_ion) - mol(c, builtin::hydrogen_sulfate_ion);
}

} // namespace

TEST_CASE("measured: the tables' solubility constants of copper hydroxide and copper oxide are the evaluated ones (Stage 44)") {
	// log *Ks at 25 C, I = 0: Cu(OH)2 8.674 (minteq.v4, from NIST SRD 46), 8.64 (wateq4f, Ball and Nordstrom 1991);
	// CuO 7.644 (minteq.v4), 7.62 (wateq4f), 7.656 (llnl.dat). Through the demo's own CuSO4(aq) and H2SO4(aq)
	const double hydroxide = log_star_ks(reaction(builtin::reaction::hydroxide_dissolution));
	const double oxide = log_star_ks(reaction(builtin::reaction::copper_oxide_dissolution));
	INFO("log *Ks: Cu(OH)2 " << hydroxide << ", CuO " << oxide);
	for (const double evaluated : { 8.674, 8.64 }) {
		CHECK(std::abs(hydroxide - evaluated) < 0.15);
	}
	for (const double evaluated : { 7.644, 7.62, 7.656 }) {
		CHECK(std::abs(oxide - evaluated) < 0.1);
	}
	// so the hydroxide is metastable: Cu(OH)2 -> CuO + H2O(l) at -5.51 kJ/mol (Puigdomenech and Taxen 2000,
	// SKB TR-00-13, Table 2: -359.92 and -128.29 kJ/mol), -5.88 by minteq's constants, -5.82 by wateq4f's
	const SpeciesTable &t = builtin_species();
	const auto g = [&](SpeciesId s) { return t.species(s).hf - 298.15 * t.species(s).s0; };
	const double turning = g(builtin::copper_oxide) + g(builtin::water) - g(builtin::copper_hydroxide);
	CHECK(turning == doctest::Approx(-5.51e3).epsilon(0.01));
	// its solubility product, Cu2+ + 2 OH-: 10^-19.32 from 8.674 and pKw 13.995
	CHECK(std::abs(hydroxide - 2.0 * -std::log10(water_ionisation(t, 298.15)) - (8.674 - 2.0 * 13.995)) < 0.15);
}

TEST_CASE("measured: copper hydroxide turns to oxide at Fukuda's first-order rate, dry; it lasts years in water (Stage 44)") {
	// Fukuda, Favergeon and Koga 2019 (J. Phys. Chem. C 123, 20903), quoting their 2018 fit in dry N2: the surface
	// reaction step, Ea 127.0 kJ/mol, ln(A / s^-1) 32.9; held at 400 K in dry nitrogen
	const Kinetics &k = reaction(builtin::reaction::hydroxide_dehydration).kinetics();
	CHECK(k.activation == doctest::Approx(127.0e3));
	CHECK(std::log(k.prefactor) == doctest::Approx(32.9).epsilon(1e-4));
	World::Builder b(builtin_species());
	const CellId cell = b.add_cell("pan");
	const CellId bath = b.add_reservoir("furnace", 400.0);
	b.add_edge(cell, bath, PHASES_NONE, 1e6);
	b.set_amount(cell, builtin::copper_hydroxide, Amount{ quanta(1e-6) }).set_amount(cell, builtin::glass, Amount{ quanta(100.0) });
	b.set_amount(cell, builtin::nitrogen, Amount{ quanta(1.0) }).set_temperature(cell, 400.0);
	std::string why;
	auto world = b.build(&why);
	REQUIRE_MESSAGE(world.has_value(), why);
	Simulation sim(std::move(*world), std::vector<Reaction>(builtin_reactions().begin(), builtin_reactions().end()));
	sim.set_step_seconds(1.0);
	const double n0 = sim.world().amount(cell, builtin::copper_hydroxide).mol();
	for (int s = 0; s < 300; ++s) {
		sim.step();
	}
	const double alpha = 1.0 - sim.world().amount(cell, builtin::copper_hydroxide).mol() / n0;
	const double rate = std::exp(32.9) * std::exp(-127.0e3 / (GAS_CONSTANT * 400.0));
	CHECK(near(alpha, 1.0 - std::exp(-rate * 300.0), 2e-3));
	MESSAGE("alpha after 300 s at 400 K: " << alpha << " (first order at " << rate << " /s)");
	// Cudennec and Lecerf 2003: pure Cu(OH)2 stays several months in pure water at room temperature. Under water,
	// steam at its saturation over it
	Cell wet;
	wet.put(builtin::copper_hydroxide, 0.01, 1);
	bool defined = false;
	const double pull = affinity(builtin_species(), wet.state, reaction(builtin::reaction::hydroxide_dehydration).terms(), 298.15, defined);
	REQUIRE(defined); // (a cell with no gas: steam at unit activity, the most it holds it back)
	const double factor = pull > 0.0 ? 1.0 - std::exp(-pull / (GAS_CONSTANT * 298.15)) : 0.0;
	const double half_life_days = std::log(2.0) / (k.prefactor * std::exp(-k.activation / (GAS_CONSTANT * 298.15)) * std::max(factor, 1e-9)) / 86400.0;
	CHECK(half_life_days > 90.0);
	MESSAGE("half-life in water at 25 C: " << half_life_days << " days");
}

TEST_CASE("measured: malachite dissolves in sulfuric acid at Nicol's surface rate, order 0.65, 53.6 kJ/mol (Stage 44)") {
	// Nicol 2018 (Hydrometallurgy 177, 214), eq. 4: dR/dt = A exp(-6447 / T) C^0.65, A 1.44e7 cm/s, C in mol/cm^3,
	// a shrinking particle controlled at its surface; 4.1e-7 mol/cm2/s in 0.2 mol/L acid at 25 C with his density,
	// and Kopylov and Orlov 1964: 3.6e-7. (Stage 63: the model's law is in the free hydrogen ions, refitted at his
	// conditions to give his in the acid: docs/DATA_SOURCES.md)
	const Reaction &r = reaction(builtin::reaction::malachite_dissolution);
	const SpeciesDef &malachite = builtin_species().species(builtin::malachite);
	const auto steps_at = [&](double acid, double kelvin, double dissipation) {
		Cell c;
		c.put(builtin::sulfuric_acid_aq, acid).put(builtin::malachite, 1e-3, 1000000); // grains of 44 um
		c.state = testing::settled(c.state, kelvin); // the acid at its bisulfate (Stage 63: the rate is the free ions')
		return surface_steps(builtin_species(), c.state, r, kelvin, affinity_of(c, r, kelvin), dissipation);
	};
	const auto nicol = [&](double acid, double kelvin) { // his shrinking rate times the model's own moles a m^3
		const double shrink = 1.44e7 * std::exp(-6447.0 / kelvin) * std::pow(acid * 1e-3, 0.65) * 1e-2; // m/s
		return shrink / malachite.molar_volume;
	};
	const SurfaceSteps at25 = steps_at(0.2, 298.15, 0.0);
	CHECK(near(at25.surface, nicol(0.2, 298.15), 0.01)); // (mol/kg here, mol/L there: a 0.2 % difference)
	CHECK(near(at25.surface, 4.1e-3, 0.05));
	CHECK(near(at25.surface, 3.6e-3, 0.15));
	CHECK(near(steps_at(0.1, 298.15, 0.0).surface, nicol(0.1, 298.15), 0.01));
	CHECK(near(steps_at(0.1, 318.15, 0.0).surface, nicol(0.1, 318.15), 0.01)); // his range: 25 to 45 C
	REQUIRE(at25.transported);
	MESSAGE("in 0.2 M acid at 25 C, grains of 44 um: surface " << at25.surface << ", transport " << at25.transport << " mol/m2/s");
}

TEST_CASE("precipitation: a base into copper sulfate brings the hydroxide out in the step, as grains, every invariant holding (Stage 44)") {
	const SpeciesTable &table = builtin_species();
	bool ok = false;
	for (const double base : { 0.04, 0.10, 0.14 }) { // short of the copper's 0.05 mol, at it (2 to 1), past it
		World::Builder b(table);
		const CellId beaker = b.add_cell("beaker");
		b.set_amount(beaker, builtin::water, amount_from_mol(100.0 / 18.015, ok)).set_amount(beaker, builtin::copper_sulfate_aq, amount_from_mol(0.05, ok));
		b.set_amount(beaker, builtin::sodium_hydroxide_aq, amount_from_mol(base, ok)).set_temperature(beaker, 293.15);
		std::string why;
		auto world = b.build(&why);
		REQUIRE_MESSAGE(world, why);
		Simulation sim(std::move(*world), std::vector<Reaction>(builtin_reactions().begin(), builtin_reactions().end()));
		sim.set_checking(true);
		const Totals genesis = totals(table, sim.world().snapshot());
		const StepTrace trace = sim.step();
		CHECK(totals(table, sim.world().snapshot()) == genesis);
		const Violations violations = check_all(table, trace, equilibria());
		CHECK_MESSAGE(violations.empty(), (violations.empty() ? std::string() : violations.front()));
		const CellState &end = sim.world().snapshot_cell(beaker);
		const double made = std::min(0.05, base / 2.0);
		const double copper_left = testing::element_mol(table, end, "Cu", Phase::Aqueous); // in Cu2+ and its hydrolysis's ions
		INFO("base " << base << ": hydroxide " << mol(end, builtin::copper_hydroxide) << ", copper left " << copper_left << ", base left " << mol(end, builtin::hydroxide_ion));
		// the base all taken: by the solid, by copper's hydroxo ions (Stage 63) and what is left free, less the hydrogen
		// ions its hydrolysis freed (a balance of protons, exact)
		CHECK(bound_hydroxide(end) == doctest::Approx(base).epsilon(1e-9));
		// with copper left over, a few percent of it held by the hydroxo ions at the hydroxide's saturation, not the solid
		CHECK(mol(end, builtin::copper_hydroxide) / made > (base < 0.1 ? 0.95 : 0.9999));
		CHECK(mol(end, builtin::copper_hydroxide) / made < 1.0 + 1e-9);
		if (base > 0.1) {
			CHECK(copper_left < 1e-11); // Ksp 5e-20 over the base left
		}
		// as grains of its own size (grain_um), not one lump
		const double d = particle_diameter(table, end, builtin::copper_hydroxide);
		CHECK(near(d, table.species(builtin::copper_hydroxide).grain, 0.01));
		CHECK(end.particles[static_cast<size_t>(builtin::copper_hydroxide.index)] > 1000000000);
		// the pH: copper's acid left, base left
		double p = 0.0, kelvin = 0.0;
		REQUIRE(sim.world().temperature(beaker, kelvin));
		REQUIRE(ph(table, end, kelvin, p));
		MESSAGE("base " << base << " mol: pH " << p);
		CHECK((base > 0.11 ? p > 12.0 : base < 0.09 ? p < 7.0 : p > 6.0 && p < 10.0));
	}
}

TEST_CASE("precipitation: an acid takes the base first, then dissolves the hydroxide again; copper sulfate alone holds a trace of it and is acid (Stage 44)") {
	const SpeciesTable &table = builtin_species();
	bool ok = false;
	const auto settle = [&](double acid, double base, double hydroxide) {
		World::Builder b(table);
		const CellId beaker = b.add_cell("beaker");
		b.set_amount(beaker, builtin::water, amount_from_mol(100.0 / 18.015, ok)).set_amount(beaker, builtin::copper_sulfate_aq, amount_from_mol(0.05, ok));
		if (acid > 0.0) {
			b.set_amount(beaker, builtin::sulfuric_acid_aq, amount_from_mol(acid, ok));
		}
		if (base > 0.0) {
			b.set_amount(beaker, builtin::sodium_hydroxide_aq, amount_from_mol(base, ok));
		}
		if (hydroxide > 0.0) {
			b.set_amount(beaker, builtin::copper_hydroxide, amount_from_mol(hydroxide, ok));
		}
		b.set_temperature(beaker, 293.15);
		std::string why;
		auto world = b.build(&why);
		REQUIRE_MESSAGE(world, why);
		Simulation sim(std::move(*world), std::vector<Reaction>(builtin_reactions().begin(), builtin_reactions().end()));
		sim.set_checking(true);
		const StepTrace trace = sim.step();
		const Violations violations = check_all(table, trace, equilibria());
		CHECK_MESSAGE(violations.empty(), (violations.empty() ? std::string() : violations.front()));
		return sim.world().snapshot_cell(beaker);
	};
	// 0.02 mol of acid and 0.06 of base: 0.04 of it neutralised, 0.02 left to bring out 0.01 of the hydroxide, less
	// what copper's hydroxo ions hold at its saturation (Stage 63: in 0.4 mol/kg of copper left, about a tenth)
	const CellState mixed = settle(0.02, 0.06, 0.0);
	CHECK(bound_hydroxide(mixed) == doctest::Approx(0.06 - 0.04).epsilon(1e-9));
	CHECK(mol(mixed, builtin::copper_hydroxide) > 0.0085);
	CHECK(mol(mixed, builtin::copper_hydroxide) < 0.01);
	// the hydroxide into acid: dissolved, a mole of acid a mole
	const CellState dissolved = settle(0.03, 0.0, 0.02);
	CHECK(mol(dissolved, builtin::copper_hydroxide) < 1e-12);
	// (the acid left, 0.02 mol of hydrogen: free and in the bisulfate, Stage 63)
	CHECK(mol(dissolved, builtin::hydrogen_ion) + mol(dissolved, builtin::hydrogen_sulfate_ion) == doctest::Approx(0.02).epsilon(1e-4));
	CHECK(testing::element_mol(table, dissolved, "Cu", Phase::Aqueous) == doctest::Approx(0.07).epsilon(1e-6));
	// copper sulfate alone: a trace hydrolyses, Cu2+ + H2O -> CuOH+ + H+, and the solution is acid:
	// a 0.5 mol/kg copper sulfate solution reads about pH 4 (Stage 63: the ion, no longer a trace of the solid; its
	// dimer left out, GAME_SPEC §20, so the model's is under 5)
	const CellState alone = settle(0.0, 0.0, 0.0);
	double p = 0.0;
	REQUIRE(ph(table, alone, 293.15, p));
	MESSAGE("copper sulfate 0.5 mol/kg: pH " << p << ", CuOH+ " << mol(alone, builtin::copper_hydroxide_ion) << " mol");
	CHECK(mol(alone, builtin::copper_hydroxide) == 0.0);
	CHECK(mol(alone, builtin::copper_hydroxide_ion) > 0.0);
	CHECK(p > 3.5);
	CHECK(p < 5.0);
}
