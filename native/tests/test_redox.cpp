// Redox in solution (GAME_SPEC §7 and §8, Stage 91): persulfate oxidising iodide at the solution law (the iodine clock's
// rate step), triiodide, iodine with thiosulfate and permanganate with iron(II) as fast equilibria; their rows and
// rates held to their sources (docs/DATA_SOURCES.md, Redox in solution (Stage 91)).
#include "chem/builtin_ids.hpp"
#include "tables.hpp"
#include "chem/invariants.hpp"
#include "chem/optics.hpp"
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

double mol_of(const World &world, CellId cell, SpeciesId s) {
	return static_cast<double>(world.snapshot_cell(cell).amount[static_cast<size_t>(s.index)]) * MOL_PER_QUANTUM;
}

const Reaction &shipped(int id) {
	return builtin_reactions()[static_cast<size_t>(id)];
}

std::vector<Reaction> all_reactions() {
	return std::vector<Reaction>(builtin_reactions().begin(), builtin_reactions().end());
}

// a kg of water at `celsius` with these dissolved amounts (mol)
World::Builder kg_of_water(double celsius, std::initializer_list<std::pair<SpeciesId, double>> dissolved, CellId &cell, bool bath = false) {
	World::Builder b(builtin_species());
	cell = b.add_cell("beaker");
	if (bath) {
		const CellId reservoir = b.add_reservoir("bath", celsius + 273.15);
		b.add_edge(cell, reservoir, PHASES_NONE, 1e6);
	}
	b.set_amount(cell, builtin::water, mol(55.508));
	for (const auto &[species, n] : dissolved) {
		b.set_amount(cell, species, mol(n));
	}
	b.set_temperature(cell, celsius + 273.15);
	return b;
}

// the model's rate constant: its rate over the product of its ordered species' molalities
double rate_constant(const Reaction &reaction, double celsius, std::initializer_list<std::pair<SpeciesId, double>> dissolved) {
	CellId cell;
	World::Builder b = kg_of_water(celsius, dissolved, cell);
	std::string why;
	auto world = b.build(&why);
	REQUIRE_MESSAGE(world.has_value(), why);
	const CellState &state = world->snapshot_cell(cell);
	const Kinetics &k = reaction.kinetics();
	double product = 1.0;
	for (int32_t o = 0; o < k.order_count; ++o) {
		product *= molality(builtin_species(), state, k.orders[static_cast<size_t>(o)].species);
	}
	return solution_rate(builtin_species(), state, k, celsius + 273.15) / product;
}

// log10 K at 25 C from the rows
double log_k(const Reaction &reaction) {
	const SpeciesTable &t = builtin_species();
	double g = 0.0;
	for (const Term &term : reaction.terms()) {
		const SpeciesDef &d = t.species(term.species);
		g += term.nu * (d.hf - 298.15 * d.s0);
	}
	return -g / (GAS_CONSTANT * 298.15 * std::log(10.0));
}

} // namespace

TEST_CASE("measured: persulfate oxidises iodide at Jette and King's rate at 25 C, faster with salt and toward King and Jacobs' at none (Stage 91)") {
	using namespace builtin;
	const Reaction &oxidation = shipped(reaction::persulfate_iodide);
	// Jette and King 1929 (JACS 51, 1034), p. 1042 and Table II: k = 0.125 L/mol/min at 25.00 C, 0.005 M K2S2O8 with
	// 0.01 M KI (ionic strength 0.025), extrapolated to the start
	const double k = rate_constant(oxidation, 25.0, { { potassium_ion, 0.02 }, { persulfate_ion, 0.005 }, { iodide_ion, 0.01 } });
	MESSAGE("k at Jette and King's: " << k << " kg/mol/s (theirs 0.125 / 60 = " << 0.125 / 60.0 << ")");
	CHECK(k == doctest::Approx(0.125 / 60.0).epsilon(0.005));
	// like charges: added salt speeds it (Bronsted and Bjerrum), and at no salt it is King and Jacobs 1931's (JACS 53,
	// 1704; log k0 -1.075, read as minutes as Jette and King's, as Burns 1966 quotes it): within 25 %, the ion pairing
	// KS2O8- (not modelled) slowing the measured one
	const double salted = rate_constant(oxidation, 25.0, { { potassium_ion, 0.22 }, { persulfate_ion, 0.005 }, { iodide_ion, 0.01 }, { nitrate_ion, 0.2 } });
	const double bare = rate_constant(oxidation, 25.0, { { potassium_ion, 2e-6 }, { persulfate_ion, 5e-7 }, { iodide_ion, 1e-6 } });
	MESSAGE("k with 0.2 M KNO3: " << salted << "; at no salt: " << bare << " (King and Jacobs " << std::pow(10.0, -1.075) / 60.0 << ")");
	CHECK(salted > 1.5 * k);
	CHECK(bare == doctest::Approx(std::pow(10.0, -1.075) / 60.0).epsilon(0.25));
	// its temperature: Ahmed, Uddin and Jafri 1979 (Pakistan J. Sci. Ind. Res. 22, 128), Table 3: 51.39 kJ/mol at an
	// ionic strength of 0.215, 25 to 40 C
	const auto at = [&](double celsius) { return rate_constant(oxidation, celsius, { { potassium_ion, 0.215 }, { persulfate_ion, 0.0075 }, { iodide_ion, 0.2 } }); };
	const double ea = GAS_CONSTANT * std::log(at(40.0) / at(25.0)) / (1.0 / 298.15 - 1.0 / 313.15);
	CHECK(ea == doctest::Approx(51.39e3).epsilon(0.02));
}

TEST_CASE("measured: triiodide's balance is Palmer, Ramette and Mesmer's, iodine with thiosulfate and permanganate with iron(II) as their potentials put them (Stage 91)") {
	using namespace builtin;
	// Palmer, Ramette and Mesmer 1984 (J. Solution Chem. 13, 673), the abstract: K = 698 at 25 C (molal)
	CHECK(std::pow(10.0, log_k(shipped(reaction::triiodide_formation))) == doctest::Approx(698.0).epsilon(0.002));
	// MnO4- + 8 H+ + 5 e -> Mn2+ 1.51 V, Fe3+ + e -> Fe2+ 0.771 V (Bard, Parsons and Jordan 1985, as LibreTexts'
	// table gives them): log K = 5 (1.51 - 0.771) / 0.05916, 62.46
	CHECK(log_k(shipped(reaction::permanganate_iron)) == doctest::Approx(62.46).epsilon(0.01));
	// iodine and thiosulfate: NBS's rows give 21.4 (their own Gibbs energies 20.1; the potentials 0.621 and 0.08 V,
	// 18.3: thiosulfate's row is NBS's least sure, its entropy to a unit), all of them to its end
	CHECK(log_k(shipped(reaction::iodine_thiosulfate)) > 15.0);
	CHECK(log_k(shipped(reaction::persulfate_iodide)) > 30.0);
}

TEST_CASE("measured: the new rows are NBS's, the ions' heat capacities and volumes OBIGT's on the model's footing (Stage 91)") {
	using namespace builtin;
	const SpeciesTable &t = builtin_species();
	struct Row {
		SpeciesId s;
		double hf_kj, s0, cp_conventional, v_conventional;
		int z;
	};
	// NBS 1982 pp. 2-52, 2-57, 2-191; OBIGT (CHNOSZ inorganic_aq.csv: Shock and Helgeson 1988; Shock, Sassani, Willis and
	// Sverjensky 1997), Cp in cal x 4.184; the footing Cp - 216.35 z, V - 16.74 z
	for (const Row &r : { Row{ thiosulfate_ion, -648.5, 67.0, -57.3 * 4.184, 28.5, -2 }, Row{ tetrathionate_ion, -1224.2, 257.3, -26.4 * 4.184, 56.5, -2 },
				 Row{ persulfate_ion, -1344.7, 244.3, -25.0 * 4.184, 79.0, -2 }, Row{ permanganate_ion, -541.4, 191.2, -1.8 * 4.184, 42.0, -1 },
				 Row{ manganese_ion, -220.75, -73.6, -4.1 * 4.184, -17.1, 2 } }) {
		const SpeciesDef &d = t.species(r.s);
		CHECK(d.hf == doctest::Approx(r.hf_kj * 1e3).epsilon(1e-12));
		CHECK(d.s0 == doctest::Approx(r.s0).epsilon(1e-12));
		CHECK(d.cp == doctest::Approx(r.cp_conventional - 216.35 * r.z).epsilon(1e-9));
		CHECK(d.molar_volume == doctest::Approx((r.v_conventional - 16.74 * r.z) * 1e-6).epsilon(1e-9));
	}
	// p. 2-52: I2(ao) 22.6 kJ, S 137.2; I3-(ao) -51.5, its S 239.3 calibrated to 239.52 (Palmer's K, above)
	CHECK(t.species(iodine_aq).hf == doctest::Approx(22.6e3).epsilon(1e-12));
	CHECK(t.species(iodine_aq).s0 == doctest::Approx(137.2).epsilon(1e-12));
	CHECK(t.species(triiodide_ion).hf == doctest::Approx(-51.5e3).epsilon(1e-12));
	CHECK(t.species(triiodide_ion).s0 == doctest::Approx(239.3).epsilon(0.001));
	// the salts, their ions' sums, against NBS's own (ai) rows: K2S2O8 -1849.3 (p. 2-336), KMnO4 -793.7 (p. 2-349),
	// MnSO4 -1130.1 (p. 2-192); Na2S2O3's -1132.40 (p. 2-307) is 3.7 kJ off its own ions', the sodium table's of 1980
	CHECK(t.species(potassium_persulfate_aq).hf == doctest::Approx(-1849.3e3).epsilon(2e-4));
	CHECK(t.species(potassium_permanganate_aq).hf == doctest::Approx(-793.7e3).epsilon(2e-4));
	CHECK(t.species(manganese_sulfate_aq).hf == doctest::Approx(-1130.1e3).epsilon(2e-4));
}

TEST_CASE("measured: permanganate's absorption is McBeath, Wilkinson and Graham's, its peaks where they are; triiodide and iodine Pursell and Pursell's; manganese(II), thiosulfate and persulfate colourless (Stage 91)") {
	using namespace builtin;
	const auto at = [](SpeciesId s, int nm) { return optics::spectrum(s)[static_cast<size_t>(nm - 380)]; };
	// their figure's spectra fitted against concentration; their calibration line's 3340 at 525 nm
	CHECK(at(permanganate_ion, 525) == doctest::Approx(3340.0).epsilon(0.03));
	CHECK(at(permanganate_ion, 545) == doctest::Approx(3220.0).epsilon(0.03));
	int peak = 380;
	for (int nm = 380; nm <= 780; ++nm) {
		peak = at(permanganate_ion, nm) > at(permanganate_ion, peak) ? nm : peak;
	}
	CHECK(peak == 525);
	// its bands at 507 and 565 nm: local maxima
	for (const int band : { 507, 545, 566 }) {
		CHECK(at(permanganate_ion, band) > at(permanganate_ion, band - 4));
		CHECK(at(permanganate_ion, band) > at(permanganate_ion, band + 4));
	}
	for (const SpeciesId s : { manganese_ion, thiosulfate_ion, persulfate_ion, tetrathionate_ion }) {
		CHECK(at(s, 525) == 0.0);
	}
	// triiodide (Pursell and Pursell 2016's drawing) against two others: Chia 1958's (UCRL-8311, Table IV) 5990 at 400.6
	// nm at no iodide; Pai, Li and Stephens 2024's (ACS Omega, Table 4) 2376 at 430 nm, 1127 at 456, 791 at 466, in
	// 0.038 M iodide at 25 C; and the SI's own 225 at 500 nm
	CHECK(at(triiodide_ion, 400) == doctest::Approx(5990.0).epsilon(0.03));
	CHECK(at(triiodide_ion, 430) == doctest::Approx(2376.0).epsilon(0.04));
	CHECK(at(triiodide_ion, 456) == doctest::Approx(1127.0).epsilon(0.06));
	CHECK(at(triiodide_ion, 466) == doctest::Approx(791.0).epsilon(0.06));
	CHECK(at(triiodide_ion, 500) == doctest::Approx(225.0).epsilon(0.06));
	// iodine: the SI's Table S1, 725 at 459 nm and 491 at 500; Pai et al.'s Table 3 at 25 C, 540 at 430 and 717 at 456
	CHECK(at(iodine_aq, 459) == doctest::Approx(725.0).epsilon(0.02));
	CHECK(at(iodine_aq, 500) == doctest::Approx(491.0).epsilon(0.02));
	CHECK(at(iodine_aq, 430) == doctest::Approx(540.0).epsilon(0.03));
	CHECK(at(iodine_aq, 456) == doctest::Approx(717.0).epsilon(0.03));
}

TEST_CASE("redox: an iodine clock's iodine comes when its thiosulfate is spent, at t = [S2O3]0 / (2 k [S2O8] [I-]), every invariant held (Stage 91)") {
	using namespace builtin;
	// 0.020 M persulfate, 0.050 M iodide, 0.50 mM thiosulfate, at 25 C
	const std::initializer_list<std::pair<SpeciesId, double>> mix = { { potassium_ion, 0.09 }, { persulfate_ion, 0.02 }, { iodide_ion, 0.05 },
		{ sodium_ion, 1e-3 }, { thiosulfate_ion, 5e-4 } };
	const double k = rate_constant(shipped(reaction::persulfate_iodide), 25.0, mix);
	const double expected = 5e-4 / (2.0 * k * 0.02 * 0.05);
	CellId cell;
	World::Builder b = kg_of_water(25.0, mix, cell, true);
	std::string why;
	auto world = b.build(&why);
	REQUIRE_MESSAGE(world.has_value(), why);
	Simulation sim(std::move(*world), all_reactions());
	StepTrace trace;
	double clock = -1.0;
	const int most = static_cast<int>(2.0 * expected / STEP_SECONDS);
	for (int step = 1; step <= most && clock < 0.0; ++step) {
		sim.step(trace);
		if (step % 300 == 0) {
			const Violations v = check_all(builtin_species(), trace, sim.equilibria());
			REQUIRE_MESSAGE(v.empty(), v.front());
		}
		// the iodine stays nought while thiosulfate is left
		const double iodine = mol_of(sim.world(), cell, iodine_aq) + mol_of(sim.world(), cell, triiodide_ion);
		if (iodine > 1e-6) {
			clock = step * STEP_SECONDS;
		} else if (step * STEP_SECONDS < 0.9 * expected) {
			REQUIRE(iodine < 1e-9);
		}
	}
	MESSAGE("the clock: " << clock << " s against " << expected << " s (k " << k << " kg/mol/s)");
	// a micromole of iodine after it, the persulfate a sixtieth spent: within 3 %
	CHECK(clock == doctest::Approx(expected).epsilon(0.03));
	CHECK(mol_of(sim.world(), cell, thiosulfate_ion) < 1e-6);
}

TEST_CASE("redox: permanganate takes five iron(II) each, to the end point and no further; thiosulfate two iodines (Stage 91)") {
	using namespace builtin;
	// 0.0100 mol iron(II) sulfate in 0.05 mol sulfuric acid; permanganate a little short of 0.0020, and a little past
	for (const double mn : { 0.0019, 0.0021 }) {
		CellId cell;
		World::Builder b = kg_of_water(25.0, { { iron_ii_ion, 0.01 }, { sulfate_ion, 0.06 }, { hydrogen_ion, 0.1 }, { potassium_ion, mn }, { permanganate_ion, mn } }, cell);
		std::string why;
		auto world = build_settled(b, builtin_reactions(), &why);
		REQUIRE_MESSAGE(world.has_value(), why);
		const double left = mol_of(*world, cell, permanganate_ion), ferrous = mol_of(*world, cell, iron_ii_ion);
		MESSAGE(mn << " mol permanganate: " << left << " left, iron(II) " << ferrous);
		if (mn < 0.002) {
			CHECK(left < 1e-12);
			CHECK(ferrous == doctest::Approx(0.01 - 5.0 * mn).epsilon(1e-6));
		} else {
			CHECK(left == doctest::Approx(mn - 0.002).epsilon(1e-6));
			CHECK(ferrous < 1e-12);
		}
		CHECK(mol_of(*world, cell, manganese_ion) == doctest::Approx(std::min(mn, 0.002)).epsilon(1e-6));
	}
	// 1.0 mmol iodine in iodide: thiosulfate a little short of 2.0 mmol, and a little past
	for (const double s : { 0.0019, 0.0021 }) {
		CellId cell;
		World::Builder b = kg_of_water(25.0, { { iodine_aq, 0.001 }, { potassium_ion, 0.1 + 2.0 * s }, { iodide_ion, 0.1 }, { thiosulfate_ion, s } }, cell);
		std::string why;
		auto world = build_settled(b, builtin_reactions(), &why);
		REQUIRE_MESSAGE(world.has_value(), why);
		const double iodine = mol_of(*world, cell, iodine_aq) + mol_of(*world, cell, triiodide_ion);
		MESSAGE(s << " mol thiosulfate: iodine " << iodine << ", triiodide's share " << mol_of(*world, cell, triiodide_ion) / std::max(iodine, 1e-30));
		if (s < 0.002) {
			CHECK(iodine == doctest::Approx(0.001 - s / 2.0).epsilon(1e-6));
			// at 0.1 M iodide, 698 x 0.1: nearly all of it triiodide
			CHECK(mol_of(*world, cell, triiodide_ion) / iodine == doctest::Approx(69.8 / 70.8).epsilon(0.02));
		} else {
			// (a few dozen quanta, the settling's last rounds' remainder: 0.05 nM, nothing a cell's colour shows)
			CHECK(iodine < 1e-9);
			CHECK(mol_of(*world, cell, thiosulfate_ion) == doctest::Approx(s - 0.002).epsilon(1e-6));
		}
	}
}
