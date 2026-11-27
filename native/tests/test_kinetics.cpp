// Reactions in the step loop (GAME_SPEC §7): rate laws with an affinity
// factor, stable at any rate, stopping at their own equilibrium.
#include "chem/builtin_ids.hpp"
#include "tables.hpp"
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

const Reaction &decomposition() {
	return builtin_reactions()[static_cast<size_t>(builtin::reaction::malachite_decomposition)];
}

std::vector<Reaction> shipped() {
	return std::vector<Reaction>(builtin_reactions().begin(), builtin_reactions().end());
}

// the decomposition with other kinetics
Reaction decomposition_with(double prefactor, double activation) {
	std::string why;
	auto r = Reaction::make(builtin_species(), "decomposition", decomposition().terms(), &why,
			first_order(builtin::malachite, prefactor, activation));
	REQUIRE_MESSAGE(r.has_value(), why);
	return *r;
}

// malachite in a flask of glass and air, a flame and a room
struct Flask {
	CellId flask, flame, room;
	World world;
};
Flask heated_flask(double malachite, double scale = 1.0) {
	World::Builder b(builtin_species());
	const CellId flask = b.add_cell("flask");
	const CellId flame = b.add_reservoir("flame", 2200.0);
	const CellId room = b.add_reservoir("room", 293.15);
	b.add_edge(flask, flame, PHASES_NONE, 0.2 * scale);
	b.add_edge(flask, room, PHASES_NONE, 0.9 * scale);
	b.set_amount(flask, builtin::malachite, mol(malachite * scale)).set_amount(flask, builtin::glass, mol(0.5 * scale));
	b.set_amount(flask, builtin::nitrogen, mol(0.008 * scale)).set_amount(flask, builtin::oxygen, mol(0.002 * scale));
	b.set_temperature(flask, 293.15);
	return Flask{ flask, flame, room, build(b) };
}

} // namespace

TEST_CASE("kinetics: the data oracle: malachite's decomposition is endothermic and favoured by heat (Stage 3)") {
	// K1. From the table: dH = 2 Hf(CuO) + Hf(CO2) + Hf(H2O g) - Hf(malachite)
	const SpeciesTable &t = builtin_species();
	const auto hf = [&](SpeciesId s) { return t.species(s).hf; };
	const auto s0 = [&](SpeciesId s) { return t.species(s).s0; };
	const double dh = 2 * hf(builtin::copper_oxide) + hf(builtin::carbon_dioxide) + hf(builtin::steam) - hf(builtin::malachite);
	const double ds = 2 * s0(builtin::copper_oxide) + s0(builtin::carbon_dioxide) + s0(builtin::steam) - s0(builtin::malachite);
	CHECK(dh == doctest::Approx(106494.0).epsilon(1e-6)); // J/mol: endothermic
	CHECK(ds == doctest::Approx(321.5).epsilon(1e-3)); // J/mol/K: two gases made
	// spontaneous above about 331 K with both gases at 1 bar; it is known to be
	// stable at room temperature and to decompose well below red heat
	CHECK(dh / ds > 298.15);
	CHECK(dh / ds < 400.0);
	CHECK(decomposition().kinetics().law == Kinetics::Law::FirstOrder);
}

TEST_CASE("kinetics: affinity has the sign of the spontaneous direction (Stage 3)") {
	const SpeciesTable &t = builtin_species();
	CellState cell{ std::vector<int64_t>(static_cast<size_t>(t.species_count()), 0), 0, {} };
	const auto at = [&](SpeciesId s) -> int64_t & { return cell.amount[static_cast<size_t>(s.index)]; };
	bool defined = false;
	at(builtin::malachite) = 1000000;
	// no gas yet: nothing opposes the first quantum
	CHECK(affinity(t, cell, decomposition().terms(), 300.0, defined) == std::numeric_limits<double>::infinity());
	CHECK(defined);
	// the products at one atmosphere between them: stable cold, unstable hot
	at(builtin::copper_oxide) = 2000000;
	at(builtin::carbon_dioxide) = 1000000;
	at(builtin::steam) = 1000000;
	CHECK(affinity(t, cell, decomposition().terms(), 300.0, defined) < 0.0);
	CHECK(affinity(t, cell, decomposition().terms(), 600.0, defined) > 0.0);
	// diluting the gases in air favours the decomposition (Le Chatelier)
	const double crowded = affinity(t, cell, decomposition().terms(), 320.0, defined);
	at(builtin::nitrogen) = 1000000000;
	CHECK(affinity(t, cell, decomposition().terms(), 320.0, defined) > crowded);
	CHECK(activity(t, cell, builtin::malachite) == 1.0);
	CHECK(activity(t, cell, builtin::carbon_dioxide) == doctest::Approx(1e6 / (1e9 + 2e6) * 1.01325));
	// a species without verified data: no affinity, so no reaction
	const Reaction &unverified = builtin_reactions()[static_cast<size_t>(builtin::reaction::trihydrate_dehydration)];
	affinity(testing::unverified_table(), cell, unverified.terms(), 300.0, defined);
	CHECK_FALSE(defined);
}

TEST_CASE("kinetics: malformed kinetics are refused (Stage 3)") {
	std::string why;
	const auto terms = decomposition().terms();
	CHECK_FALSE(Reaction::make(builtin_species(), "r", terms, &why, first_order(builtin::copper_oxide, 1.0, 0.0))); // a product
	CHECK_FALSE(Reaction::make(builtin_species(), "r", terms, &why, first_order(builtin::malachite, 0.0, 0.0)));
	CHECK_FALSE(Reaction::make(builtin_species(), "r", terms, &why, first_order(builtin::malachite, 1.0, -5.0)));
	CHECK_FALSE(Reaction::make(builtin_species(), "r", terms, &why, first_order(builtin::malachite, std::nan(""), 1.0)));
	CHECK(Reaction::make(builtin_species(), "r", terms, &why, first_order(builtin::malachite, 1.0, 0.0)).has_value());
}

TEST_CASE("kinetics: at a held temperature malachite decays exponentially at its Arrhenius rate (Stage 3)") {
	// a trace of malachite in a large glass ballast held at 550 K: the heat of
	// the reaction moves the temperature by microkelvins, far from equilibrium
	World::Builder b(builtin_species());
	const CellId cell = b.add_cell("cell");
	const CellId bath = b.add_reservoir("bath", 550.0);
	b.add_edge(cell, bath, PHASES_NONE, 1e6);
	b.set_amount(cell, builtin::malachite, mol(1e-6)).set_amount(cell, builtin::glass, mol(100.0)).set_amount(cell, builtin::nitrogen, mol(1.0));
	b.set_temperature(cell, 550.0);
	Simulation sim(build(b), shipped());
	sim.set_checking(true);
	const Kinetics &law = decomposition().kinetics();
	const double k = law.prefactor * std::exp(-law.activation / (GAS_CONSTANT * 550.0)); // about 6e-3 /s
	const double n0 = sim.world().amount(cell, builtin::malachite).mol();
	const Totals genesis = totals(builtin_species(), sim.world().snapshot());
	double worst = 0.0;
	for (int step = 1; step <= 60000; ++step) { // 1000 s: six time constants
		sim.step();
		if (step % 3000 == 0) {
			const double expected = n0 * std::exp(-k * step * STEP_SECONDS);
			worst = std::max(worst, std::abs(sim.world().amount(cell, builtin::malachite).mol() - expected) / n0);
		}
	}
	CHECK(worst < 1e-6);
	CHECK(sim.world().amount(cell, builtin::copper_oxide).quanta == 2 * (mol(1e-6).quanta - sim.world().amount(cell, builtin::malachite).quanta));
	CHECK(totals(builtin_species(), sim.world().snapshot()) == genesis);
	MESSAGE("first-order decay at 550 K: k = " << k << " /s, worst error " << worst << " of the initial amount");
}

TEST_CASE("kinetics: a reaction that could finish a million times over in one step simply finishes (Stage 3)") {
	World::Builder b(builtin_species());
	const CellId cell = b.add_cell("cell");
	b.set_amount(cell, builtin::malachite, mol(0.01)).set_amount(cell, builtin::glass, mol(5.0)).set_amount(cell, builtin::nitrogen, mol(1.0));
	b.set_temperature(cell, 900.0);
	Simulation sim(build(b), { decomposition_with(1e8, 0.0) });
	sim.set_checking(true);
	const Totals genesis = totals(builtin_species(), sim.world().snapshot());
	sim.step();
	CHECK(sim.world().amount(cell, builtin::malachite).quanta == 0);
	CHECK(sim.world().amount(cell, builtin::carbon_dioxide) == mol(0.01));
	CHECK(temperature(sim.world(), cell) < 900.0); // endothermic: the cell paid for it
	CHECK(totals(builtin_species(), sim.world().snapshot()) == genesis);
	for (int i = 0; i < 100; ++i) {
		CHECK(sim.step().ledger.empty());
	}
}

TEST_CASE("kinetics: a sealed cell stops at its equilibrium and stays there, bit for bit (Stage 3)") {
	// malachite alone with little ballast: decomposing cools the cell until the
	// affinity is zero, with malachite left over. A fast rate, to get there.
	World::Builder b(builtin_species());
	const CellId cell = b.add_cell("cell");
	b.set_amount(cell, builtin::malachite, mol(0.1)).set_amount(cell, builtin::glass, mol(0.5));
	b.set_temperature(cell, 500.0);
	Simulation sim(build(b), { decomposition_with(50.0, 0.0) });
	sim.set_checking(true);
	const Totals genesis = totals(builtin_species(), sim.world().snapshot());
	double made = 0.0;
	for (int step = 0; step < 20000; ++step) {
		made += entropy_change(builtin_species(), sim.step());
	}
	const Snapshot rest = sim.world().snapshot();
	const std::vector<double> carried = sim.reaction_remainders();
	const double left = sim.world().amount(cell, builtin::malachite).mol();
	const double t = temperature(sim.world(), cell);
	CHECK(left > 0.01); // it did not run out
	CHECK(left < 0.09); // and it did react
	CHECK(t < 500.0);
	CHECK(t > 300.0);
	CHECK(made > 0.0);
	// at rest the affinity is zero to within what one quantum of extent changes
	bool defined = false;
	const double a = affinity(builtin_species(), sim.world().snapshot_cell(cell), decomposition().terms(), t, defined);
	CHECK(std::abs(a) < 1e-3); // J/mol, against a reaction enthalpy of 1e5
	for (int step = 0; step < 50000; ++step) {
		REQUIRE(sim.step().ledger.empty());
	}
	CHECK(sim.world().snapshot() == rest);
	CHECK(sim.reaction_remainders() == carried);
	CHECK(totals(builtin_species(), rest) == genesis);
	MESSAGE("equilibrium: " << left << " mol of 0.1 left at " << t << " K, affinity " << a << " J/mol, entropy made " << made << " J/K");
}

TEST_CASE("kinetics: past its equilibrium the reaction runs backwards (Stage 3)") {
	// the products with a little malachite, cold: malachite reforms and the cell warms
	World::Builder b(builtin_species());
	const CellId cell = b.add_cell("cell");
	b.set_amount(cell, builtin::malachite, mol(0.01)).set_amount(cell, builtin::copper_oxide, mol(0.2));
	b.set_amount(cell, builtin::carbon_dioxide, mol(0.1)).set_amount(cell, builtin::steam, mol(0.1)).set_amount(cell, builtin::glass, mol(2.0));
	b.set_temperature(cell, 300.0);
	Simulation sim(build(b), { decomposition_with(50.0, 0.0) });
	sim.set_checking(true);
	for (int step = 0; step < 20000; ++step) {
		sim.step();
	}
	CHECK(sim.world().amount(cell, builtin::malachite).mol() > 0.01);
	CHECK(temperature(sim.world(), cell) > 300.0);
}

TEST_CASE("kinetics: heated by a flame, malachite decomposes and the flask pays the reaction's heat (Stage 3)") {
	Flask f = heated_flask(5.0 / 221.114);
	Simulation sim(std::move(f.world), shipped());
	sim.set_checking(true);
	const Totals genesis = totals(builtin_species(), sim.world().snapshot());
	const Amount start = sim.world().amount(f.flask, builtin::malachite);
	double made = 0.0;
	for (int step = 0; step < 72000; ++step) { // twenty minutes
		made += entropy_change(builtin_species(), sim.step());
	}
	CHECK(sim.world().amount(f.flask, builtin::malachite).quanta < start.quanta / 100); // over 99% gone
	CHECK(sim.world().amount(f.flask, builtin::copper_oxide).quanta == 2 * (start.quanta - sim.world().amount(f.flask, builtin::malachite).quanta));
	CHECK(temperature(sim.world(), f.flask) > 600.0);
	CHECK(totals(builtin_species(), sim.world().snapshot()) == genesis);
	// energy: what the flame gave is in the flask and the room
	CHECK(sim.world().energy(f.flame).quanta < 0);
	CHECK(made > 0.0);
}

TEST_CASE("kinetics: shifting every formation enthalpy by its elements changes nothing (Stage 3)") {
	// M1. Enthalpies are relative to the elements: adding c_e per atom of element
	// e to every Hf leaves every reaction enthalpy, so every trajectory, the
	// same. A reaction's heat counted twice, or not at all, would show here.
	const SpeciesTable &base = builtin_species();
	std::vector<ElementDef> elements;
	for (int e = 0; e < base.element_count(); ++e) {
		elements.push_back(base.element(e));
	}
	std::vector<SpeciesDef> species(base.all_species().begin(), base.all_species().end());
	const double per_atom[] = { 12345.0, -67890.0, 4321.0, -98765.0, 55555.0, -22222.0, 77777.0, -33333.0, 44444.0, -11111.0, 66666.0, -55555.0, 23456.0, 31415.0, -27182.0, 16180.0, -14142.0,
		17320.0, -22360.0, 26457.0, -30000.0 }; // an element each (Na: Stage 43; Ni: Stage 47; Cl, K, P, Br: Stage 64; Ca, Mg, Ba, Ag, Fe, Zn, Al, Co: Stage 76)
	REQUIRE(std::size(per_atom) == static_cast<size_t>(base.element_count()));
	for (SpeciesDef &def : species) {
		for (int e = 0; e < base.element_count(); ++e) {
			def.hf += per_atom[e] * def.atoms[static_cast<size_t>(e)];
		}
	}
	const auto shifted = SpeciesTable::make(elements, species, nullptr, base.solution());
	REQUIRE(shifted.has_value());

	const auto run = [](const SpeciesTable &table, std::vector<double> &temperatures, std::vector<double> &left) {
		World::Builder b(table);
		const CellId flask = b.add_cell("flask");
		const CellId flame = b.add_reservoir("flame", 2200.0);
		b.add_edge(flask, flame, PHASES_NONE, 0.3);
		bool ok = false;
		b.set_amount(flask, builtin::malachite, amount_from_mol(0.02, ok)).set_amount(flask, builtin::glass, amount_from_mol(0.5, ok));
		b.set_amount(flask, builtin::nitrogen, amount_from_mol(0.01, ok)).set_temperature(flask, 293.15);
		auto world = b.build();
		REQUIRE(world.has_value());
		auto reaction = Reaction::make(table, "decomposition", decomposition().terms(), nullptr, decomposition().kinetics());
		REQUIRE(reaction.has_value());
		Simulation sim(std::move(*world), { *reaction });
		sim.set_checking(true);
		for (int step = 1; step <= 30000; ++step) {
			sim.step();
			if (step % 3000 == 0) {
				double t = 0.0;
				sim.world().temperature(flask, t);
				temperatures.push_back(t);
				left.push_back(sim.world().amount(flask, builtin::malachite).mol());
			}
		}
	};
	std::vector<double> t_base, n_base, t_shifted, n_shifted;
	run(base, t_base, n_base);
	run(*shifted, t_shifted, n_shifted);
	REQUIRE(t_base.size() == t_shifted.size());
	CHECK(n_base.back() < 0.01); // the scenario does react
	for (size_t i = 0; i < t_base.size(); ++i) {
		CHECK(t_shifted[i] == doctest::Approx(t_base[i]).epsilon(1e-9));
		CHECK(n_shifted[i] == doctest::Approx(n_base[i]).epsilon(1e-7));
	}
}

TEST_CASE("kinetics: twice the world behaves the same (Stage 3)") {
	// M2. Every amount and conductance doubled: the same temperatures, twice the amounts.
	const auto run = [](double scale, std::vector<double> &temperatures, std::vector<double> &left) {
		Flask f = heated_flask(0.02, scale);
		Simulation sim(std::move(f.world), shipped());
		sim.set_checking(true);
		for (int step = 1; step <= 40000; ++step) {
			sim.step();
			if (step % 4000 == 0) {
				double t = 0.0;
				sim.world().temperature(f.flask, t);
				temperatures.push_back(t);
				left.push_back(sim.world().amount(f.flask, builtin::malachite).mol());
			}
		}
	};
	std::vector<double> t1, n1, t2, n2;
	run(1.0, t1, n1);
	run(2.0, t2, n2);
	CHECK(n1.back() < 0.019);
	for (size_t i = 0; i < t1.size(); ++i) {
		CHECK(t2[i] == doctest::Approx(t1[i]).epsilon(1e-10));
		CHECK(n2[i] == doctest::Approx(2.0 * n1[i]).epsilon(1e-9));
	}
}

TEST_CASE("kinetics: halving the step halves the error (Stage 3)") {
	// M3. The processes are applied in sequence, each exact on its own: the
	// splitting is first order in the step.
	const auto left_after = [](double h) {
		Flask f = heated_flask(0.02);
		Simulation sim(std::move(f.world), shipped());
		sim.set_step_seconds(h);
		const int steps = static_cast<int>(std::lround(64.0 / h));
		for (int i = 0; i < steps; ++i) {
			sim.step();
		}
		return sim.world().amount(f.flask, builtin::malachite).mol();
	};
	const double reference = left_after(1.0 / 256.0);
	const double coarse = std::abs(left_after(4.0) - reference);
	const double medium = std::abs(left_after(2.0) - reference);
	const double fine = std::abs(left_after(1.0) - reference);
	CHECK(reference > 0.002); // mid-reaction: the comparison means something
	CHECK(reference < 0.018);
	CHECK(coarse / medium == doctest::Approx(2.0).epsilon(0.25));
	CHECK(medium / fine == doctest::Approx(2.0).epsilon(0.25));
	// at the game's step the splitting error is that of a time shift of about one step
	const double game = std::abs(left_after(STEP_SECONDS) - reference) / reference;
	CHECK(game < 3e-3);
	MESSAGE("convergence at 64 s: " << reference << " mol left; errors at h = 4, 2, 1 s: " << coarse << ", " << medium << ", " << fine
										   << "; relative error at the game's step: " << game);
}
