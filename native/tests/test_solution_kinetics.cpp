// The solution law (GAME_SPEC §7, Stage 87): a reaction among dissolved species, its rate in their molalities to their
// orders, k(T) prod_j m_j^n_j f(A) per kg of solvent, times Brønsted and Bjerrum's activity factor; any dissolved species may have an order, a catalyst one in none of
// its terms; a step exact in its first reactant, the others held. Held here on copper's first ammine given this law.
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

const Reaction &ammine() {
	return builtin_reactions()[static_cast<size_t>(builtin::reaction::copper_ammine_1_formation)];
}

// copper's first ammine, Cu2+ + NH3 -> CuNH3 2+, with the solution law
Reaction ammine_with(double prefactor, double activation, std::initializer_list<RateOrder> orders) {
	std::string why;
	auto r = Reaction::make(builtin_species(), "ammine", ammine().terms(), &why, solution(builtin::copper_ion, prefactor, activation, orders));
	REQUIRE_MESSAGE(r.has_value(), why);
	return *r;
}

// a kg of water at 25 C held there, copper sulfate, ammonia and (a catalyst's stand-in) sodium sulfate
struct Beaker {
	CellId cell;
	World world;
};
Beaker beaker(double copper, double ammonia, double sodium = 0.0, double scale = 1.0) {
	World::Builder b(builtin_species());
	const CellId cell = b.add_cell("beaker");
	const CellId bath = b.add_reservoir("bath", 298.15);
	b.add_edge(cell, bath, PHASES_NONE, 1e6 * scale);
	b.set_amount(cell, builtin::water, mol(55.508 * scale));
	b.set_amount(cell, builtin::copper_ion, mol(copper * scale)).set_amount(cell, builtin::sulfate_ion, mol((copper + sodium / 2.0) * scale));
	b.set_amount(cell, builtin::ammonia_aq, mol(ammonia * scale));
	if (sodium > 0.0) {
		b.set_amount(cell, builtin::sodium_ion, mol(sodium * scale));
	}
	b.set_temperature(cell, 298.15);
	std::string why;
	auto world = b.build(&why);
	REQUIRE_MESSAGE(world.has_value(), why);
	return Beaker{ cell, std::move(*world) };
}

double molality_of(const World &world, CellId cell, SpeciesId s) {
	return molality(builtin_species(), world.snapshot_cell(cell), s);
}

} // namespace

TEST_CASE("solution kinetics: the rate is k(T) times each species' molality to its order, a catalyst's too (Stage 87)") {
	const Beaker b = beaker(0.01, 0.2, 0.5);
	const CellState &cell = b.world.snapshot_cell(b.cell);
	const SpeciesTable &table = builtin_species();
	const double cu = molality(table, cell, builtin::copper_ion), nh3 = molality(table, cell, builtin::ammonia_aq), na = molality(table, cell, builtin::sodium_ion);
	CHECK(cu == doctest::Approx(0.01).epsilon(1e-3));
	const double k = 2.0 * std::exp(-50e3 / (GAS_CONSTANT * 298.15));
	const auto activity = [&](const Kinetics &kinetics) { return std::exp(rate_activity(table, cell, kinetics)); };
	const Kinetics first = solution(builtin::copper_ion, 2.0, 50e3, { { builtin::copper_ion, 1.0 }, { builtin::ammonia_aq, 1.0 } });
	CHECK(solution_rate(table, cell, first, 298.15) == doctest::Approx(k * cu * nh3 * activity(first)).epsilon(1e-12));
	const Kinetics second = solution(builtin::copper_ion, 2.0, 50e3, { { builtin::copper_ion, 2.0 }, { builtin::ammonia_aq, 0.5 } });
	CHECK(solution_rate(table, cell, second, 298.15) == doctest::Approx(k * cu * cu * std::sqrt(nh3) * activity(second)).epsilon(1e-12));
	const Kinetics catalysed = solution(builtin::copper_ion, 2.0, 50e3, { { builtin::copper_ion, 1.0 }, { builtin::ammonia_aq, 1.0 }, { builtin::sodium_ion, 1.0 } });
	CHECK(solution_rate(table, cell, catalysed, 298.15) == doctest::Approx(k * cu * nh3 * na * activity(catalysed)).epsilon(1e-12));
	// Arrhenius: ten kelvin up, e^(Ea/R (1/T1 - 1/T2)) faster (the molalities as they are)
	CHECK(solution_rate(table, cell, first, 308.15) / solution_rate(table, cell, first, 298.15)
			== doctest::Approx(std::exp(50e3 / GAS_CONSTANT * (1.0 / 298.15 - 1.0 / 308.15))).epsilon(1e-12));
}

TEST_CASE("solution kinetics: a step's extent is the decay's own solution in its first reactant, at any rate (Stage 87)") {
	const Beaker b = beaker(0.01, 0.2);
	const CellState &cell = b.world.snapshot_cell(b.cell);
	const SpeciesTable &table = builtin_species();
	const double cu = molality(table, cell, builtin::copper_ion), nh3 = molality(table, cell, builtin::ammonia_aq);
	const double per = static_cast<double>(cell.amount[static_cast<size_t>(builtin::copper_ion.index)]) / cu; // quanta per mol/kg
	const double rt = GAS_CONSTANT * 298.15, h = 1.0 / 60.0;
	const auto activity = [&](const Kinetics &kinetics) { return std::exp(rate_activity(table, cell, kinetics)); };
	// order 1 in copper: m (1 - e^(-k' h)), k' = k [NH3] f (and the activities, held over the step)
	const Kinetics first = solution(builtin::copper_ion, 3.0, 0.0, { { builtin::copper_ion, 1.0 }, { builtin::ammonia_aq, 1.0 } });
	const double g1 = activity(first);
	CHECK(solution_extent(table, cell, first, 1, rt, 0.5, true, h) == doctest::Approx(cu * (1.0 - std::exp(-3.0 * g1 * nh3 * 0.5 * h)) * per).epsilon(1e-12));
	// order 2: m - (m^-1 + k' h)^-1
	const Kinetics second = solution(builtin::copper_ion, 3.0, 0.0, { { builtin::copper_ion, 2.0 }, { builtin::ammonia_aq, 1.0 } });
	const double g2 = activity(second);
	CHECK(solution_extent(table, cell, second, 1, rt, 1.0, true, h) == doctest::Approx((cu - 1.0 / (1.0 / cu + 3.0 * g2 * nh3 * h)) * per).epsilon(1e-9)); // (a difference of near numbers)
	// a rate that could finish a million times over in a step: all of it, and no more
	const Kinetics fast = solution(builtin::copper_ion, 1e12, 0.0, { { builtin::copper_ion, 1.0 }, { builtin::ammonia_aq, 1.0 } });
	CHECK(solution_extent(table, cell, fast, 1, rt, 1.0, true, h) == doctest::Approx(cu * per).epsilon(1e-12));
	// an order under 1 runs out in a finite time: all of it, not past it
	const Kinetics half = solution(builtin::copper_ion, 1e9, 0.0, { { builtin::copper_ion, 0.5 }, { builtin::ammonia_aq, 1.0 } });
	CHECK(solution_extent(table, cell, half, 1, rt, 1.0, true, h) == doctest::Approx(cu * per).epsilon(1e-12));
	// backwards: the rate as it is at the step's start
	CHECK(solution_extent(table, cell, first, 1, rt, 0.5, false, h) == doctest::Approx(3.0 * g1 * cu * nh3 * 0.5 * h * per).epsilon(1e-12));
}

TEST_CASE("solution kinetics: far from its equilibrium, copper in excess ammonia decays at its pseudo-first-order rate (Stage 87)") {
	Beaker b = beaker(1e-3, 1.0);
	const double k0 = 0.02; // kg/mol/s
	Simulation sim(std::move(b.world), { ammine_with(k0, 0.0, { { builtin::copper_ion, 1.0 }, { builtin::ammonia_aq, 1.0 } }) });
	const double m0 = molality_of(sim.world(), b.cell, builtin::copper_ion), nh3 = molality_of(sim.world(), b.cell, builtin::ammonia_aq);
	// the activities as they are (the ionic strength hardly moves: CuNH3 2+ takes copper's charge)
	const double k = k0 * std::exp(rate_activity(builtin_species(), sim.world().snapshot_cell(b.cell), sim.rated()[0].kinetics()));
	double worst = 0.0;
	StepTrace trace;
	for (int step = 1; step <= 6000; ++step) { // 100 s: two time constants of k [NH3]
		sim.step(trace);
		if (step % 600 == 0) {
			const Violations v = check_all(builtin_species(), trace, sim.equilibria());
			REQUIRE_MESSAGE(v.empty(), v.front());
			const double t = step * STEP_SECONDS;
			// [NH3] falls by what the copper took, a thousandth of it: the closed form of the second order's pseudo-first
			const double expected = m0 * (nh3 - m0) / (nh3 * std::exp((nh3 - m0) * k * t) - m0);
			worst = std::max(worst, std::abs(molality_of(sim.world(), b.cell, builtin::copper_ion) - expected) / m0);
		}
	}
	MESSAGE("pseudo-first order at k [NH3] = " << k * nh3 << " /s: worst " << worst << " of the start");
	// (the affinity factor's 1 - Q/K, the step's splitting, and copper's activity as its sulfate's pairing gives way to the
	// ammine's: k held at its start's)
	CHECK(worst < 3e-3);
}

TEST_CASE("solution kinetics: a catalyst is unchanged, speeds the reaction, and leaves its equilibrium where it was (Stage 87)") {
	// the same reaction, the same k, catalysed by sodium at 2 mol/kg (order 1): twice as fast; ammonia scarce, so its
	// equilibrium leaves copper free
	const auto run = [](bool catalysed, double seconds, double &copper, double &sodium) {
		Beaker b = beaker(0.01, 0.004, 4.0);
		const Reaction r = catalysed ? ammine_with(5.0, 0.0, { { builtin::copper_ion, 1.0 }, { builtin::ammonia_aq, 1.0 }, { builtin::sodium_ion, 1.0 } })
									 : ammine_with(5.0, 0.0, { { builtin::copper_ion, 1.0 }, { builtin::ammonia_aq, 1.0 } });
		Simulation sim(std::move(b.world), { r });
		const int64_t na = sim.world().amount(b.cell, builtin::sodium_ion).quanta;
		for (int step = 0; step < static_cast<int>(seconds * 60.0); ++step) {
			sim.advance();
		}
		CHECK(sim.world().amount(b.cell, builtin::sodium_ion).quanta == na); // in none of its terms: never moved
		copper = molality_of(sim.world(), b.cell, builtin::copper_ion);
		sodium = molality_of(sim.world(), b.cell, builtin::sodium_ion);
	};
	double plain = 0.0, catalysed = 0.0, na = 0.0;
	run(false, 2.0, plain, na);
	run(true, 2.0, catalysed, na);
	CHECK(na == doctest::Approx(4.0).epsilon(1e-3));
	MESSAGE("copper left after 2 s: " << plain << " mol/kg alone, " << catalysed << " catalysed");
	CHECK(catalysed < plain - 1e-5);
	double plain_end = 0.0, catalysed_end = 0.0;
	run(false, 400.0, plain_end, na); // (twenty of its time constants, 1 / k [Cu])
	run(true, 400.0, catalysed_end, na);
	MESSAGE("at its equilibrium: " << plain_end << " alone, " << catalysed_end << " catalysed");
	CHECK(plain_end == doctest::Approx(catalysed_end).epsilon(1e-6));
	CHECK(plain_end > 0.005); // (copper still free: ammonia ran out)
}

TEST_CASE("solution kinetics: twice the solution goes the same way, molality for molality (Stage 87)") {
	const auto run = [](double scale) {
		Beaker b = beaker(0.01, 0.2, 0.0, scale);
		Simulation sim(std::move(b.world), { ammine_with(0.3, 20e3, { { builtin::copper_ion, 1.0 }, { builtin::ammonia_aq, 1.0 } }) });
		for (int step = 0; step < 3000; ++step) {
			sim.advance();
		}
		return molality_of(sim.world(), b.cell, builtin::copper_ion);
	};
	const double one = run(1.0), two = run(2.0);
	MESSAGE("copper after 50 s: " << one << " and, twice the solution, " << two);
	CHECK(one == doctest::Approx(two).epsilon(1e-9));
}

TEST_CASE("solution kinetics: malformed solution laws are refused (Stage 87)") {
	const auto refused = [](const Kinetics &k) {
		std::string why;
		const bool made = Reaction::make(builtin_species(), "ammine", ammine().terms(), &why, k).has_value();
		MESSAGE(why);
		return !made && !why.empty();
	};
	CHECK(refused(solution(builtin::copper_ion, 1.0, 0.0, {}))); // no order
	CHECK(refused(solution(builtin::copper_ion, 1.0, 0.0, { { builtin::glass, 1.0 }, { builtin::copper_ion, 1.0 } }))); // a solid's
	CHECK(refused(solution(builtin::copper_ion, 1.0, 0.0, { { builtin::copper_ion, -1.0 } }))); // negative
	CHECK(refused(solution(builtin::copper_ion, 1.0, 0.0, { { builtin::copper_ion, 1.0 }, { builtin::copper_ion, 1.0 } }))); // twice
	CHECK(refused(solution(builtin::copper_ammine_1, 1.0, 0.0, { { builtin::copper_ammine_1, 1.0 } }))); // exact in a product
	CHECK(refused(solution(builtin::copper_ion, 1.0, 0.0, { { builtin::ammonia_aq, 1.0 } }))); // its reactant with no order
	CHECK(refused(solution(builtin::copper_ion, 0.0, 0.0, { { builtin::copper_ion, 1.0 } }))); // no prefactor
	CHECK(refused(solution(builtin::copper_ion, 1.0, -1.0, { { builtin::copper_ion, 1.0 } }))); // a negative activation
	CHECK_FALSE(refused(solution(builtin::copper_ion, 1.0, 0.0, { { builtin::copper_ion, 1.0 }, { builtin::sodium_ion, 1.0 } }))); // a catalyst
}

TEST_CASE("solution kinetics: the primary salt effect is Brønsted and Bjerrum's, the Debye-Hückel limiting law in dilute solution (Stage 87)") {
	// a trace of copper in 1e-4 mol/kg sodium chloride (I = 1e-4): ln (k / k0) = 2 A z_A z_B sqrt(I), each ion's
	// ln gamma -A z^2 sqrt(I) with A = 3 A_phi in the limit (Pitzer's Debye-Hückel part): 2 A = 2.352 at 25 C, 1.02 in log10
	World::Builder builder(builtin_species());
	const CellId cell = builder.add_cell("beaker");
	builder.set_amount(cell, builtin::water, mol(55.508)).set_amount(cell, builtin::sodium_ion, mol(1e-4)).set_amount(cell, builtin::chloride_ion, mol(1e-4));
	builder.set_amount(cell, builtin::copper_ion, mol(1e-9)).set_amount(cell, builtin::chloride_ion, mol(1e-4 + 2e-9)).set_amount(cell, builtin::ammonia_aq, mol(1e-6));
	builder.set_temperature(cell, 298.15);
	std::string why;
	auto world = builder.build(&why);
	REQUIRE_MESSAGE(world.has_value(), why);
	const CellState &state = world->snapshot_cell(cell);
	const double root = 0.01, slope = 6.0 * builtin_species().solution().a_phi;
	const auto ln_factor = [&](std::initializer_list<RateOrder> orders) { return rate_activity(builtin_species(), state, solution(builtin::copper_ion, 1.0, 0.0, orders)); };
	// +1 with -1: slower, as -2.352 sqrt(I)
	CHECK(ln_factor({ { builtin::sodium_ion, 1.0 }, { builtin::chloride_ion, 1.0 } }) / root == doctest::Approx(-slope).epsilon(0.03));
	// +2 with +2 (order 2 in copper): faster, as 4 times 2.352 sqrt(I)
	CHECK(ln_factor({ { builtin::copper_ion, 2.0 } }) / root == doctest::Approx(4.0 * slope).epsilon(0.03));
	// a neutral species with an ion: no salt effect to first order
	CHECK(std::abs(ln_factor({ { builtin::copper_ion, 1.0 }, { builtin::ammonia_aq, 1.0 } })) < 0.03 * slope * root);
}
