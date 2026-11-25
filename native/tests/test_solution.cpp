// Solutions and crystals (GAME_SPEC §9): dissolved species, the surface rate
// law, nucleation and particle counts, the pentahydrate and its dehydration.
#include "chem/builtin_ids.hpp"
#include "tables.hpp"
#include "chem/invariants.hpp"
#include "chem/sim.hpp"
#include "chem/thermo.hpp"

#include <doctest/doctest.h>

#include <cmath>
#include <utility>
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

std::vector<Reaction> shipped() {
	return std::vector<Reaction>(builtin_reactions().begin(), builtin_reactions().end());
}

const Reaction &reaction(int index) {
	return builtin_reactions()[static_cast<size_t>(index)];
}

// mol of a dissolved species per kg of the cell's water
double molality(const World &world, CellId cell, SpeciesId solute) {
	return world.amount(cell, solute).mol() / (world.amount(cell, builtin::water).mol() * 0.018015);
}

// the crystallisation with a fast surface, to reach equilibrium quickly (stirred hard too: GAME_SPEC §7's transport)
Reaction fast_crystallisation() {
	std::string why;
	auto r = Reaction::make(builtin_species(), "crystallisation", reaction(builtin::reaction::pentahydrate_crystallization).terms(), &why,
			surface(builtin::copper_sulfate_pentahydrate, 10.0, 0.0, SpeciesId{}, 1e9, 3.0));
	REQUIRE_MESSAGE(r.has_value(), why);
	return *r;
}

// a beaker of solution with an excess of crystals, held at a temperature
struct Saturated {
	CellId cell;
	Simulation sim;
};
Saturated saturated(double kelvin, double dissolved) {
	World::Builder b(builtin_species());
	const CellId cell = b.add_cell("beaker");
	const CellId bath = b.add_reservoir("bath", kelvin);
	b.add_edge(cell, bath, PHASES_NONE, 500.0);
	b.set_amount(cell, builtin::water, mol(100.0 / 18.015)).set_amount(cell, builtin::copper_sulfate_pentahydrate, mol(1.0));
	b.set_particles(cell, builtin::copper_sulfate_pentahydrate, 1000000);
	if (dissolved > 0.0) {
		b.set_amount(cell, builtin::copper_sulfate_aq, mol(dissolved));
	}
	b.set_amount(cell, builtin::glass, mol(1.0)).set_temperature(cell, kelvin);
	World world = build(b);
	REQUIRE(world.set_stirring(cell, 1.0) == Refusal::None); // stirred hard (Stage 57: 10 W/kg; until then a mixing of 1000)
	Simulation sim(std::move(world), { fast_crystallisation() });
	sim.set_checking(true);
	return Saturated{ cell, std::move(sim) };
}

} // namespace

TEST_CASE("solution: the data oracle: saturated copper sulfate against the handbook's solubility (Stage 19)") {
	// K1. g of CuSO4 per 100 g of water (CRC Handbook, mass % converted), from
	// an excess of the pentahydrate dissolving. The dissolved salt's enthalpy,
	// entropy and heat capacity were fitted to the points from 0 to 90 C
	// (docs/DATA_SOURCES.md): three numbers for eleven points; 100 C is beyond
	// the fit, where the pentahydrate is metastable.
	const struct {
		double celsius, grams;
	} handbook[] = { { 0.0, 14.16 }, { 10.0, 16.82 }, { 20.0, 20.05 }, { 25.0, 21.95 }, { 30.0, 23.92 }, { 40.0, 28.53 }, { 50.0, 34.05 },
		{ 60.0, 40.45 }, { 70.0, 47.93 }, { 80.0, 56.99 }, { 90.0, 67.50 }, { 100.0, 76.99 } };
	for (const auto &row : handbook) {
		Saturated s = saturated(row.celsius + 273.15, 0.0);
		for (int step = 0; step < 60 * 600; ++step) {
			s.sim.step();
		}
		const double grams = molality(s.sim.world(), s.cell, builtin::copper_sulfate_aq) * 159.602 / 10.0;
		CAPTURE(row.celsius);
		CHECK(grams == doctest::Approx(row.grams).epsilon(0.03));
		CHECK(std::abs(temperature(s.sim.world(), s.cell) - (row.celsius + 273.15)) < 1e-6);
		// D3: saturated, nothing moves
		const Snapshot rest = s.sim.world().snapshot();
		for (int step = 0; step < 2000; ++step) {
			REQUIRE(s.sim.step().ledger.empty());
		}
		CHECK(s.sim.world().snapshot() == rest);
	}
}

TEST_CASE("solution: saturation is the same from above and from below (Stage 6)") {
	Saturated under = saturated(293.15, 0.0);
	Saturated over = saturated(293.15, 0.36); // 3.6 mol/kg, nearly three times saturation
	for (int step = 0; step < 60 * 600; ++step) {
		under.sim.step();
		over.sim.step();
	}
	const double from_below = molality(under.sim.world(), under.cell, builtin::copper_sulfate_aq);
	const double from_above = molality(over.sim.world(), over.cell, builtin::copper_sulfate_aq);
	CHECK(from_above == doctest::Approx(from_below).epsilon(1e-9));
	CHECK(over.sim.world().amount(over.cell, builtin::copper_sulfate_pentahydrate).mol() > 1.0); // it crystallised
	CHECK(under.sim.world().amount(under.cell, builtin::copper_sulfate_pentahydrate).mol() < 1.0); // it dissolved
}

TEST_CASE("solution: dissolving the pentahydrate cools the solution (Stage 6)") {
	// adiabatic: the heat of solution, +14.56 kJ/mol (Stage 19's fit), comes out of the water
	World::Builder b(builtin_species());
	const CellId cell = b.add_cell("beaker");
	b.set_amount(cell, builtin::water, mol(100.0 / 18.015)).set_amount(cell, builtin::copper_sulfate_pentahydrate, mol(0.05));
	b.set_particles(cell, builtin::copper_sulfate_pentahydrate, 100000).set_temperature(cell, 298.15);
	World world = build(b);
	REQUIRE(world.set_stirring(cell, 1.0) == Refusal::None); // stirred hard (Stage 57: 10 W/kg; until then a mixing of 1000)
	Simulation sim(std::move(world), { fast_crystallisation() });
	sim.set_checking(true);
	const Totals genesis = totals(builtin_species(), sim.world().snapshot());
	for (int step = 0; step < 6000; ++step) {
		sim.step();
	}
	CHECK(sim.world().amount(cell, builtin::copper_sulfate_pentahydrate).quanta == 0); // all of it: 0.47 mol/kg is under saturation
	CHECK(sim.world().particles(cell, builtin::copper_sulfate_pentahydrate) == 0); // X1
	CHECK(sim.world().amount(cell, builtin::copper_sulfate_aq) == mol(0.05));
	// 0.05 mol * 14.56 kJ/mol over the solution's heat capacity
	const double heat = -835990.0 + 5.0 * -285830.0 - -2279700.0;
	CHECK(heat == doctest::Approx(14560.0));
	const double capacity = thermal_state(builtin_species(), sim.world().snapshot_cell(cell)).heat_capacity;
	CHECK(298.15 - temperature(sim.world(), cell) == doctest::Approx(0.05 * heat / capacity).epsilon(0.01));
	CHECK(totals(builtin_species(), sim.world().snapshot()) == genesis);
}

TEST_CASE("solution: a neutral solute lowers water's vapour pressure, so the solution boils hotter (Stage 6)") {
	// K1: a neutral solute (one particle of its own, no ions) is ideal-dilute
	const SpeciesTable &t = testing::with_neutral_solute();
	const SpeciesId urea = t.find("urea_aq");
	CellState cell{ std::vector<int64_t>(static_cast<size_t>(t.species_count()), 0), 0, {} };
	const auto at = [&](SpeciesId s) -> int64_t & { return cell.amount[static_cast<size_t>(s.index)]; };
	at(builtin::water) = mol(1.0 / 0.018015).quanta; // a kilogram
	CHECK(activity(t, cell, builtin::water) == 1.0);
	at(urea) = mol(1.0).quanta;
	CHECK(activity(t, cell, urea) == doctest::Approx(1.0).epsilon(1e-9)); // one molal
	CHECK(molality(t, cell, urea) == doctest::Approx(1.0).epsilon(1e-9));
	CHECK(activity(t, cell, builtin::water) == doctest::Approx(std::exp(-0.018015)).epsilon(1e-9));
	// the vaporisation's affinity at the pure water's boiling point is now negative
	at(builtin::steam) = 1000000;
	bool defined = false;
	const auto vaporise = reaction(builtin::reaction::water_vaporization).terms();
	double lo = 373.0, hi = 376.0; // where a one-molal solution boils into pure steam
	for (int i = 0; i < 60; ++i) {
		const double middle = 0.5 * (lo + hi);
		(affinity(t, cell, vaporise, middle, defined) < 0.0 ? lo : hi) = middle;
	}
	at(urea) = 0;
	double pure_lo = 373.0, pure_hi = 376.0;
	for (int i = 0; i < 60; ++i) {
		const double middle = 0.5 * (pure_lo + pure_hi);
		(affinity(t, cell, vaporise, middle, defined) < 0.0 ? pure_lo : pure_hi) = middle;
	}
	// the ebullioscopic constant of water, 0.512 K kg/mol
	CHECK(lo - pure_lo == doctest::Approx(0.512).epsilon(0.03));
}

TEST_CASE("solution: copper oxide dissolves in the acid, much faster warm, and hardly faster stirred (Stage 6; Stage 23: controlled at its surface)") {
	const auto time_to_dissolve = [](double kelvin, double stirring, double fraction) {
		World::Builder b(builtin_species());
		const CellId cell = b.add_cell("flask");
		const CellId bath = b.add_reservoir("bath", kelvin);
		b.add_edge(cell, bath, PHASES_NONE, 500.0);
		const double oxide = 3.597 / 79.545;
		b.set_amount(cell, builtin::copper_oxide, mol(oxide)).set_particles(cell, builtin::copper_oxide, 2500000);
		b.set_amount(cell, builtin::water, mol(96.2 / 18.015)).set_amount(cell, builtin::sulfuric_acid_aq, mol(0.1));
		b.set_amount(cell, builtin::glass, mol(1.0)).set_temperature(cell, kelvin);
		World world = build(b);
		REQUIRE(world.set_stirring(cell, stirring) == Refusal::None); // (Stage 57: the stirring's watts, Sano's transport)
		Simulation sim(std::move(world), shipped());
		sim.set_checking(true);
		const Totals genesis = totals(builtin_species(), sim.world().snapshot());
		int step = 0;
		while (sim.world().amount(cell, builtin::copper_oxide).mol() > fraction * oxide && step < 60 * 7200) {
			sim.step();
			++step;
		}
		// what dissolved is in solution, mole for mole, and the acid paid for it
		const double gone = oxide - sim.world().amount(cell, builtin::copper_oxide).mol();
		CHECK(sim.world().amount(cell, builtin::copper_sulfate_aq).mol() == doctest::Approx(gone).epsilon(1e-9));
		CHECK(sim.world().amount(cell, builtin::sulfuric_acid_aq).mol() == doctest::Approx(0.1 - gone).epsilon(1e-9));
		CHECK(sim.world().energy(bath).quanta > 0); // exothermic: the bath took the heat
		CHECK(totals(builtin_species(), sim.world().snapshot()) == genesis);
		return step * STEP_SECONDS;
	};
	const double cold = time_to_dissolve(333.15, 0.0, 0.5);
	const double still = time_to_dissolve(353.15, 0.0, 0.5);
	const double stirred = time_to_dissolve(353.15, 0.1, 0.5); // 1 W/kg
	CHECK(still > 60.0); // a minute or more, even hot
	// Majima et al. 1980: the rate does not depend on agitation; the acid reaches the 8 um grains far faster than they use it
	CHECK(still / stirred == doctest::Approx(1.0).epsilon(0.03));
	CHECK(still / stirred >= 1.0);
	const Kinetics &law = reaction(builtin::reaction::copper_oxide_dissolution).kinetics();
	CHECK(cold / still == doctest::Approx(std::exp(law.activation / GAS_CONSTANT * (1.0 / 333.15 - 1.0 / 353.15))).epsilon(0.05)); // Arrhenius
	MESSAGE("half of the oxide gone: " << cold << " s at 60 C, " << still << " s at 80 C, " << stirred << " s stirred");
}

TEST_CASE("solution: with no acid left the oxide stops, and never runs backwards in pure water (Stage 6)") {
	World::Builder b(builtin_species());
	const CellId cell = b.add_cell("flask");
	b.set_amount(cell, builtin::copper_oxide, mol(0.05)).set_particles(cell, builtin::copper_oxide, 100000);
	b.set_amount(cell, builtin::water, mol(5.0)).set_amount(cell, builtin::sulfuric_acid_aq, mol(0.01)).set_temperature(cell, 350.0);
	Simulation sim(build(b), shipped());
	sim.set_checking(true);
	for (int step = 0; step < 60 * 3600; ++step) {
		sim.step();
	}
	// the acid is the limiting reagent: all but a trace of it is used
	CHECK(sim.world().amount(cell, builtin::sulfuric_acid_aq).mol() < 1e-4);
	CHECK(sim.world().amount(cell, builtin::copper_oxide).mol() == doctest::Approx(0.04).epsilon(0.01));
	CHECK(sim.world().particles(cell, builtin::copper_oxide) == 100000); // the grains shrank, none vanished
}

TEST_CASE("solution: slow cooling grows fewer, larger crystals than fast cooling, if only a little (Stage 6; Stage 23)") {
	// the same hot solution, 3 mol/kg, taken to 20 C through a weak and a strong link. With measured nucleation
	// (Stage 23) crystals start rarely and keep starting for hours, while the salt diffuses to the few there are:
	// fast cooling's head start in supersaturation gives only some more of them. Twelve hours, at quarter-second
	// steps (the counts are the same at the game's step), for the whole crop.
	const auto cool = [](double conductance, int64_t &count, double &diameter, double &crop) {
		World::Builder b(builtin_species());
		const CellId cell = b.add_cell("flask");
		const CellId room = b.add_reservoir("room", 293.15);
		b.add_edge(cell, room, PHASES_NONE, conductance);
		b.set_amount(cell, builtin::water, mol(20.0 / 18.015)).set_amount(cell, builtin::copper_sulfate_aq, mol(0.06));
		b.set_amount(cell, builtin::glass, mol(1.5)).set_temperature(cell, 363.15);
		Simulation sim(build(b), shipped());
		sim.set_step_seconds(0.25);
		sim.set_checking(true);
		const Totals genesis = totals(builtin_species(), sim.world().snapshot());
		for (int step = 0; step < 4 * 12 * 3600; ++step) {
			sim.step();
		}
		count = sim.world().particles(cell, builtin::copper_sulfate_pentahydrate);
		diameter = particle_diameter(builtin_species(), sim.world().snapshot_cell(cell), builtin::copper_sulfate_pentahydrate);
		crop = sim.world().amount(cell, builtin::copper_sulfate_pentahydrate).mol();
		CHECK(std::abs(temperature(sim.world(), cell) - 293.15) < 0.2);
		CHECK(totals(builtin_species(), sim.world().snapshot()) == genesis);
	};
	int64_t slow_count = 0, fast_count = 0;
	double slow_size = 0.0, fast_size = 0.0, slow_crop = 0.0, fast_crop = 0.0;
	cool(0.15, slow_count, slow_size, slow_crop); // a time constant of 17 minutes
	cool(5.0, fast_count, fast_size, fast_crop); // of 25 seconds
	CHECK(slow_count > 0);
	CHECK(fast_count > 1.1 * static_cast<double>(slow_count));
	CHECK(slow_size > 1.03 * fast_size);
	CHECK(slow_crop == doctest::Approx(fast_crop).epsilon(0.01)); // the same amount comes out: the same equilibrium
	MESSAGE("slow: " << slow_count << " crystals of " << slow_size * 1e6 << " um; fast: " << fast_count << " of " << fast_size * 1e6 << " um; crop " << slow_crop * 249.677 << " g");
}

TEST_CASE("solution: overheated crystals lose their water step by step, to the white anhydrous salt (Stage 19)") {
	// dry crystals in a flask open to the air, held hot for twenty minutes:
	// at 150 C the first four waters go (the monohydrate), at 300 C the last
	const auto heat = [](double kelvin, double mol_of[4]) {
		World::Builder b(builtin_species());
		const CellId flask = b.add_rigid_cell("flask", 250e-6);
		const CellId air = b.add_cell("air");
		const CellId hot = b.add_reservoir("hot", kelvin);
		b.add_edge(flask, air, PHASES_GAS);
		b.add_edge(flask, hot, PHASES_NONE, 5.0);
		b.set_amount(flask, builtin::copper_sulfate_pentahydrate, mol(0.02)).set_particles(flask, builtin::copper_sulfate_pentahydrate, 5000);
		b.set_amount(flask, builtin::glass, mol(1.5));
		b.set_amount(flask, builtin::nitrogen, mol(P_ATMOSPHERE * 248e-6 / (GAS_CONSTANT * 293.15)));
		b.set_temperature(flask, 293.15);
		b.set_amount(air, builtin::nitrogen, mol(100.0)).set_temperature(air, 293.15);
		Simulation sim(build(b), shipped());
		sim.set_checking(true);
		const Totals genesis = totals(builtin_species(), sim.world().snapshot());
		for (int step = 0; step < 60 * 1200; ++step) {
			sim.step();
		}
		const SpeciesId forms[] = { builtin::copper_sulfate_pentahydrate, builtin::copper_sulfate_trihydrate, builtin::copper_sulfate_monohydrate,
			builtin::copper_sulfate };
		for (int i = 0; i < 4; ++i) {
			mol_of[i] = sim.world().amount(flask, forms[i]).mol();
			// a solid made from a solid keeps its grains
			CHECK(sim.world().particles(flask, forms[i]) == (mol_of[i] > 0.0 ? 5000 : 0));
		}
		CHECK(totals(builtin_species(), sim.world().snapshot()) == genesis);
	};
	double warm[4] = {}, hot[4] = {};
	heat(423.15, warm);
	CHECK(warm[0] < 0.0002); // the pentahydrate is gone
	CHECK(warm[2] > 0.015); // most is the monohydrate
	heat(573.15, hot);
	CHECK(hot[3] > 0.0198); // over 99% anhydrous
	MESSAGE("at 150 C: " << warm[0] << ", " << warm[1] << ", " << warm[2] << ", " << warm[3] << " mol of the 5, 3, 1 and 0 hydrates; at 300 C " << hot[3]
						 << " mol anhydrous");
}

namespace {

// a kilogram of water holding m mol/kg of the salt and `acid` of the acid
CellState solution_of(double m, double acid) {
	CellState cell{ std::vector<int64_t>(static_cast<size_t>(builtin_species().species_count()), 0), 0, {} };
	cell.amount[static_cast<size_t>(builtin::water.index)] = mol(1.0 / 0.018015).quanta;
	if (m > 0.0) {
		cell.amount[static_cast<size_t>(builtin::copper_sulfate_aq.index)] = mol(m).quanta;
	}
	if (acid > 0.0) {
		cell.amount[static_cast<size_t>(builtin::sulfuric_acid_aq.index)] = mol(acid).quanta;
	}
	return cell;
}

// where a hydrate's crystallisation stops: its saturation, mol/kg, by bisection
// on the sign of its affinity (positive: supersaturated)
double saturation(int crystallisation, double kelvin, double acid) {
	double lo = 0.05, hi = 8.0;
	for (int i = 0; i < 60; ++i) {
		const double middle = 0.5 * (lo + hi);
		bool defined = false;
		const double a = affinity(builtin_species(), solution_of(middle, acid), reaction(crystallisation).terms(), kelvin, defined);
		REQUIRE(defined);
		(a > 0.0 ? hi : lo) = middle;
	}
	return 0.5 * (lo + hi);
}

} // namespace

TEST_CASE("solution: the data oracle: copper sulfate and the acid as electrolytes (Stage 19)") {
	const SpeciesTable &t = builtin_species();
	// K1: CuSO4's mean activity coefficient at saturation, 25 C: 0.0369 and
	// 0.0360 measured at 1.413 mol/kg; a = (m gamma)^2
	const double salt = std::sqrt(activity(t, solution_of(1.413, 0.0), builtin::copper_sulfate_aq)) / 1.413;
	CHECK(salt == doctest::Approx(0.0369).epsilon(0.04));
	CHECK(salt == doctest::Approx(0.0360).epsilon(0.04));
	// a 2-2 salt is far from ideal: a tenth of a mole per kg already has gamma 0.15
	CHECK(std::sqrt(activity(t, solution_of(0.1, 0.0), builtin::copper_sulfate_aq)) / 0.1 == doctest::Approx(0.150).epsilon(0.03));
	// the CRC's mean activity coefficients at 0.2, 0.5 and 1.0 mol/kg (low confidence in their unit; docs/DATA_SOURCES.md)
	for (const auto [m, gamma] : { std::pair{ 0.2, 0.104 }, std::pair{ 0.5, 0.062 }, std::pair{ 1.0, 0.042 } }) {
		CAPTURE(m);
		CHECK(std::sqrt(activity(t, solution_of(m, 0.0), builtin::copper_sulfate_aq)) / m == doctest::Approx(gamma).epsilon(0.03));
	}
	// K1: the acid against Staples (1981): its osmotic coefficient (from the
	// water's activity, three particles) and mean activity coefficient
	// (a = (2m)^2 m gamma^3), within 2% to 5 mol/kg
	const struct {
		double m, gamma, phi;
	} staples[] = { { 0.5, 0.1463, 0.6760 }, { 1.0, 0.1247, 0.7255 }, { 2.0, 0.1195, 0.8400 }, { 3.0, 0.1337, 0.9897 }, { 5.0, 0.1973, 1.3090 } };
	for (const auto &row : staples) {
		CAPTURE(row.m);
		const CellState cell = solution_of(0.0, row.m);
		const double phi = -std::log(activity(t, cell, builtin::water)) / (3.0 * row.m * 0.018015);
		const double gamma = std::cbrt(activity(t, cell, builtin::sulfuric_acid_aq) / 4.0) / row.m;
		CHECK(phi == doctest::Approx(row.phi).epsilon(0.02));
		CHECK(gamma == doctest::Approx(row.gamma).epsilon(0.02));
	}
	CHECK(activity(t, solution_of(0.0, 1.0), builtin::water) == doctest::Approx(0.96155).epsilon(0.001));
}

TEST_CASE("solution: stable at every strength: the water's activity falls as water leaves, to 400 mol/kg (Stage 19)") {
	// A solution boiled nearly dry passes any strength. Were the water's activity
	// to rise again as water left, evaporating would drive itself and the fast
	// equilibrium would have no last quantum (P1). CuSO4's published C^phi did
	// that beyond 65 mol/kg; it is left out (docs/DATA_SOURCES.md).
	const SpeciesTable &t = builtin_species();
	for (const double share : { 0.0, 0.1, 0.5, 1.0 }) { // of the solute that is acid
		CAPTURE(share);
		double last = 1e300;
		for (double m = 0.1; m < 400.0; m *= 1.05) {
			CellState cell{ std::vector<int64_t>(static_cast<size_t>(t.species_count()), 0), 0, {} };
			cell.amount[static_cast<size_t>(builtin::water.index)] = mol(1.0 / (0.018015 * m)).quanta; // one mol of solute in 1/m kg
			if (share < 1.0) {
				cell.amount[static_cast<size_t>(builtin::copper_sulfate_aq.index)] = mol(1.0 - share).quanta;
			}
			if (share > 0.0) {
				cell.amount[static_cast<size_t>(builtin::sulfuric_acid_aq.index)] = mol(share).quanta;
			}
			// the evaporation's affinity carries ln a of the water, finite where a underflows
			cell.amount[static_cast<size_t>(builtin::steam.index)] = mol(1.0).quanta;
			bool defined = false;
			const double a = affinity(t, cell, reaction(builtin::reaction::water_vaporization).terms(), 373.15, defined);
			REQUIRE(defined);
			CAPTURE(m);
			REQUIRE(a < last);
			last = a;
		}
	}
}

TEST_CASE("solution: every activity is the derivative of one Gibbs energy (Gibbs-Duhem) (Stage 19)") {
	// G_mix / RT = sum of n_i ln a_i. Its change along a reaction must be the
	// reaction's sum of nu_i ln a_i, or the entropy is not a function of state
	// (§4.6). Checked by central differences in a mixed salt and acid solution.
	const SpeciesTable &t = builtin_species();
	const auto mixing = [&](const CellState &cell) {
		double g = 0.0;
		for (const SpeciesId s : { builtin::water, builtin::copper_sulfate_aq, builtin::sulfuric_acid_aq }) {
			const int64_t n = cell.amount[static_cast<size_t>(s.index)];
			g += n > 0 ? static_cast<double>(n) * MOL_PER_QUANTUM * std::log(activity(t, cell, s)) : 0.0; // n ln a -> 0 as n -> 0
		}
		return g;
	};
	for (const double m : { 0.3, 1.4, 4.5, 9.0 }) {
		for (const double acid : { 0.0, 0.5, 3.0 }) {
			CAPTURE(m);
			CAPTURE(acid);
			const CellState base = solution_of(m, acid);
			for (const int r : { builtin::reaction::pentahydrate_crystallization, builtin::reaction::water_vaporization,
					 builtin::reaction::copper_oxide_dissolution }) {
				const double step = 1e-5; // mol of extent
				CellState plus = base, minus = base;
				double expected = 0.0;
				for (const Term &term : reaction(r).terms()) {
					const SpeciesDef &def = t.species(term.species);
					if (def.phase != Phase::Liquid && def.phase != Phase::Aqueous) {
						continue; // solids and gases are not in the solution
					}
					const auto q = static_cast<int64_t>(step / MOL_PER_QUANTUM) * term.nu;
					plus.amount[static_cast<size_t>(term.species.index)] += q;
					minus.amount[static_cast<size_t>(term.species.index)] -= q;
					if (base.amount[static_cast<size_t>(term.species.index)] > 0) {
						expected += term.nu * std::log(activity(t, base, term.species));
					}
				}
				if (acid == 0.0 && r == builtin::reaction::copper_oxide_dissolution) {
					continue; // no acid to take
				}
				CHECK((mixing(plus) - mixing(minus)) / (2.0 * step) == doctest::Approx(expected).epsilon(1e-6));
			}
		}
	}
}

TEST_CASE("solution: the data oracle: the pentahydrate gives way to the trihydrate under its solution near 96 C (Stage 19)") {
	// K1: where the two are saturated at the same molality. Miles and Menzies
	// (1937) measured it at 369.05 K, 95.9 C.
	using namespace builtin::reaction;
	double lo = 355.0, hi = 380.0;
	CHECK(saturation(pentahydrate_crystallization, lo, 0.0) < saturation(trihydrate_crystallization, lo, 0.0));
	CHECK(saturation(pentahydrate_crystallization, hi, 0.0) > saturation(trihydrate_crystallization, hi, 0.0));
	for (int i = 0; i < 40; ++i) {
		const double middle = 0.5 * (lo + hi);
		(saturation(pentahydrate_crystallization, middle, 0.0) < saturation(trihydrate_crystallization, middle, 0.0) ? lo : hi) = middle;
	}
	CHECK(std::abs(lo - 369.05) < 3.0);
	// at room temperature the pentahydrate is the least soluble, so the stable
	// form; the monohydrate and the anhydrous salt are far more soluble
	const double room = 298.15;
	CHECK(saturation(pentahydrate_crystallization, room, 0.0) < saturation(trihydrate_crystallization, room, 0.0));
	CHECK(saturation(trihydrate_crystallization, room, 0.0) < saturation(monohydrate_crystallization, room, 0.0));
	MESSAGE("the transition: " << lo - 273.15 << " C (measured 95.9)");
}

TEST_CASE("solution: the acid lowers the salt's solubility: the common-ion effect (Stage 19)") {
	// K1: both carry sulfate. Holler and Peffer (1916) at 15 C: about 0.54 of
	// it with 100 g/L of acid (about 1 mol/kg), on an unclear basis.
	using namespace builtin::reaction;
	const double water = saturation(pentahydrate_crystallization, 288.15, 0.0);
	double last = water;
	for (const double acid : { 0.25, 0.5, 1.0, 2.0 }) {
		const double m = saturation(pentahydrate_crystallization, 288.15, acid);
		CHECK(m < last);
		last = m;
	}
	const double ratio = saturation(pentahydrate_crystallization, 288.15, 1.0) / water;
	CHECK(ratio > 0.5);
	CHECK(ratio < 0.85);
	MESSAGE("15 C: " << water << " mol/kg in water, " << ratio << " of it with 1 mol/kg of acid");
}

TEST_CASE("solution: boiled down, crystals start once well past saturation, near 7 g of water; boiled dry and heated on, the salt turns white (Stage 19; Stage 23)") {
	// the demo's copper, 0.0452 mol of CuSO4 in 50 g of water, in a flask over
	// a flame, its steam leaving to the room's air
	World::Builder b(builtin_species());
	const CellId flask = b.add_rigid_cell("flask", 250e-6);
	const CellId air = b.add_cell("air");
	const CellId flame = b.add_reservoir("flame", 700.0);
	b.add_edge(flask, air, PHASES_GAS);
	b.add_edge(flask, flame, PHASES_NONE, 0.3);
	b.set_amount(flask, builtin::water, mol(50.0 / 18.015)).set_amount(flask, builtin::copper_sulfate_aq, mol(0.0452));
	b.set_amount(flask, builtin::glass, mol(100.0 / 60.083));
	b.set_amount(flask, builtin::nitrogen, mol(P_ATMOSPHERE * 200e-6 / (GAS_CONSTANT * 293.15)));
	b.set_temperature(flask, 293.15);
	b.set_amount(air, builtin::nitrogen, mol(1000.0)).set_temperature(air, 293.15);
	Simulation sim(build(b), shipped());
	sim.set_checking(true);
	const Totals genesis = totals(builtin_species(), sim.world().snapshot());
	const SpeciesId forms[] = { builtin::copper_sulfate_pentahydrate, builtin::copper_sulfate_trihydrate, builtin::copper_sulfate_monohydrate,
		builtin::copper_sulfate };
	double first = -1.0; // g of water when the first crystal appears
	int step = 0;
	for (; step < 60 * 3600; ++step) {
		sim.step();
		if (first < 0.0) {
			for (const SpeciesId f : forms) {
				first = sim.world().amount(flask, f).quanta > 0 ? sim.world().amount(flask, builtin::water).mol() * 18.015 : first;
			}
		}
		if (sim.world().amount(flask, builtin::copper_sulfate).mol() > 0.0451) {
			break;
		}
	}
	// saturated near 100 C at 9.4 g of water (77 g of CuSO4 per 100 g); measured nucleation needs it well past that
	CHECK(first > 6.0);
	CHECK(first < 9.0);
	CHECK(sim.world().amount(flask, builtin::copper_sulfate).mol() > 0.0451); // white: the anhydrous salt
	CHECK(totals(builtin_species(), sim.world().snapshot()) == genesis);
	MESSAGE("crystals start with " << first << " g of water; all anhydrous after " << step / 60 << " s at "
								   << temperature(sim.world(), flask) - 273.15 << " C");
}

TEST_CASE("solution: crystals in ordinary room air keep their water (Stage 6)") {
	// 20 C, air at half its saturation with water vapour, a day
	World::Builder b(builtin_species());
	const CellId dish = b.add_cell("dish");
	const CellId room = b.add_reservoir("room", 293.15);
	b.add_edge(dish, room, PHASES_NONE, 1.0);
	b.set_amount(dish, builtin::copper_sulfate_pentahydrate, mol(0.02)).set_amount(dish, builtin::nitrogen, mol(1.0)).set_amount(dish, builtin::steam, mol(0.0115));
	b.set_temperature(dish, 293.15);
	Simulation sim(build(b), shipped());
	sim.set_checking(true);
	sim.set_step_seconds(60.0);
	for (int step = 0; step < 1440; ++step) {
		sim.step();
	}
	CHECK(sim.world().amount(dish, builtin::copper_sulfate_pentahydrate).mol() > 0.01999);
}

TEST_CASE("solution: malformed surface laws, particle counts and stirring are refused (Stage 6)") {
	std::string why;
	const auto terms = reaction(builtin::reaction::pentahydrate_crystallization).terms();
	CHECK_FALSE(Reaction::make(builtin_species(), "r", terms, &why, surface(builtin::water, 1.0, 0.0))); // not a solid
	CHECK_FALSE(Reaction::make(builtin_species(), "r", terms, &why, surface(builtin::copper_oxide, 1.0, 0.0))); // not in the reaction
	CHECK_FALSE(Reaction::make(builtin_species(), "r", terms, &why, surface(builtin::copper_sulfate_pentahydrate, 0.0, 0.0)));
	CHECK_FALSE(Reaction::make(builtin_species(), "r", terms, &why, surface(builtin::copper_sulfate_pentahydrate, 1.0, 0.0, builtin::water))); // a driver that is not dissolved
	CHECK_FALSE(Reaction::make(builtin_species(), "r", terms, &why, surface(builtin::copper_sulfate_pentahydrate, 1.0, 0.0, SpeciesId{}, -1.0, 3.0)));
	CHECK(Reaction::make(builtin_species(), "r", terms, &why, surface(builtin::copper_sulfate_pentahydrate, 1.0, 0.0, builtin::copper_sulfate_aq)).has_value());
	{
		World::Builder b(builtin_species()); // a dissolved species with nothing to be dissolved in
		const CellId c = b.add_cell("c");
		b.set_amount(c, builtin::copper_sulfate_aq, mol(0.1)).set_amount(c, builtin::glass, mol(1.0)).set_temperature(c, 300.0);
		CHECK_FALSE(b.build(&why));
		CHECK(why.find("no liquid") != std::string::npos);
	}
	{
		World::Builder b(builtin_species()); // particles of what is not there
		const CellId c = b.add_cell("c");
		b.set_amount(c, builtin::water, mol(1.0)).set_particles(c, builtin::copper_oxide, 5);
		CHECK_FALSE(b.build(&why));
	}
	{
		World::Builder b(builtin_species()); // particles of a liquid
		const CellId c = b.add_cell("c");
		b.set_amount(c, builtin::water, mol(1.0)).set_particles(c, builtin::water, 5);
		CHECK_FALSE(b.build(&why));
	}
	World::Builder b(builtin_species());
	const CellId c = b.add_cell("c");
	b.set_amount(c, builtin::copper_oxide, mol(1.0));
	World world = build(b);
	CHECK(world.particles(c, builtin::copper_oxide) == 1); // a lump, by default
	CHECK(world.set_particles(c, builtin::copper_oxide, 40) == Refusal::None);
	CHECK(world.set_particles(c, builtin::copper_oxide, 0) == Refusal::BadValue); // it is there
	CHECK(world.set_particles(c, builtin::malachite, 3) == Refusal::BadValue); // it is not
	CHECK(world.set_particles(c, builtin::water, 3) == Refusal::BadValue);
	CHECK(world.set_mixing(c, 3.0) == Refusal::None);
	CHECK(world.meta(c).mixing == 3.0);
	CHECK(world.set_mixing(c, -1.0) == Refusal::BadValue);
	CHECK(world.set_mixing(CellId{ 7 }, 1.0) == Refusal::UnknownCell);
}
