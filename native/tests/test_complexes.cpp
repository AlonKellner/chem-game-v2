// Complexes (GAME_SPEC §9, Stage 81): each complex's constant against its source at 25 C, and what they do: copper's
// deep blue ammine, silver chloride dissolving in ammonia, zinc's and aluminium's hydroxides redissolving in excess
// base, iron(III)'s thiocyanate at Below, Connick and Coppel's quotient, cobalt turning blue in strong hydrochloric
// acid and on warming, chromate turning to dichromate in acid. docs/DATA_SOURCES.md, "Complexes (Stage 81)".
#include "tables.hpp"
#include "chem/builtin_ids.hpp"
#include "chem/optics.hpp"
#include "chem/thermo.hpp"

#include <doctest/doctest.h>

#include <cmath>
#include <tuple>
#include <string>
#include <vector>

using namespace chem;

namespace {

int64_t quanta(double n) {
	bool ok = false;
	const Amount a = amount_from_mol(n, ok);
	REQUIRE(ok);
	return a.quanta;
}

// a kilogram of water and what is put in it
struct Cell {
	CellState state{ std::vector<int64_t>(static_cast<size_t>(builtin_species().species_count()), 0), 0,
		std::vector<int64_t>(static_cast<size_t>(builtin_species().species_count()), 0) };
	explicit Cell(double water_kg = 1.0) { put(builtin::water, water_kg / 0.018015); }
	Cell &put(SpeciesId s, double mol) {
		testing::put_amount(builtin_species(), state, s, quanta(mol));
		return *this;
	}
};

double mol_of(const CellState &c, SpeciesId s) {
	return static_cast<double>(c.amount[static_cast<size_t>(s.index)]) * MOL_PER_QUANTUM;
}

// a reaction's log10 K from the tables at temperature T, as the model's affinity has it (constant heat capacities)
double log_k(const std::vector<std::pair<SpeciesId, int>> &terms, double kelvin) {
	double g = 0.0;
	for (const auto &[s, nu] : terms) {
		const SpeciesDef &d = builtin_species().species(s);
		g += nu * (d.hf + d.cp * (kelvin - T0) - kelvin * (d.s0 + d.cp * std::log(kelvin / T0)));
	}
	return -g / (GAS_CONSTANT * kelvin * std::log(10.0));
}

double reaction_heat(const std::vector<std::pair<SpeciesId, int>> &terms) {
	double h = 0.0;
	for (const auto &[s, nu] : terms) {
		h += nu * builtin_species().species(s).hf;
	}
	return h * 1e-3;
}

} // namespace

TEST_CASE("measured: each complex's constant is its source's at 25 C (Stage 81)") {
	using namespace builtin;
	struct Row {
		const char *name;
		std::vector<std::pair<SpeciesId, int>> terms;
		double log_k, tolerance, heat; // heat: kJ, NAN where none is checked
	};
	// copper's ammines by NBS 1982's free energies (p. 2-156; Cu2+ p. 2-154, NH3 p. 2-64) and enthalpies; MINTEQ v4's
	// first (4.010), LLNL's second and third (7.451, 10.272) beside them within 0.1
	const std::vector<Row> rows{
		{ "CuNH3 2+", { { copper_ion, -1 }, { ammonia_aq, -1 }, { copper_ammine_1, 1 } }, 4.0978, 0.001, -23.38 },
		{ "Cu(NH3)2 2+", { { copper_ion, -1 }, { ammonia_aq, -2 }, { copper_ammine_2, 1 } }, 7.5070, 0.001, -46.49 },
		{ "Cu(NH3)3 2+", { { copper_ion, -1 }, { ammonia_aq, -3 }, { copper_ammine_3, 1 } }, 10.3293, 0.001, -69.50 },
		{ "Cu(NH3)4 2+", { { copper_ion, -1 }, { ammonia_aq, -4 }, { copper_ammine_4, 1 } }, 12.3616, 0.001, -92.11 },
		// silver: MINTEQ v4 (lines 3424, 3432), NH4+ = NH3 + H+ at its 9.244 (line 3416); Ag(NH3)2+ is NBS's row (p. 2-162)
		{ "AgNH3+", { { silver_ion, -1 }, { ammonia_aq, -1 }, { silver_ammine, 1 } }, 3.310, 0.001, NAN },
		{ "Ag(NH3)2+", { { silver_ion, -1 }, { ammonia_aq, -2 }, { silver_diammine, 1 } }, 7.220, 0.03, NAN },
		// hydroxo complexes: MINTEQ v4 (lines 766, 774, 710; NIST 46.3)
		{ "Zn(OH)3-", { { zinc_ion, -1 }, { water, -3 }, { zinc_trihydroxo, 1 }, { hydrogen_ion, 3 } }, -28.091, 0.001, NAN },
		{ "Zn(OH)4 2-", { { zinc_ion, -1 }, { water, -4 }, { zinc_tetrahydroxo, 1 }, { hydrogen_ion, 4 } }, -40.488, 0.001, NAN },
		{ "Al(OH)4-", { { aluminium_ion, -1 }, { water, -4 }, { aluminium_tetrahydroxo, 1 }, { hydrogen_ion, 4 } }, -22.688, 0.001, 173.24 },
		// iron(III) thiocyanate: calibrated in the model's own activities to Below, Connick and Coppel 1958's Q1 = 146 at
		// 0.40 ionic strength (3.5085; by Davies's equation the constant at nought would be 2.981) and Lister and
		// Rivington's Q2 = 15.5 at 0.5 (the next test holds them); their heats Below et al.'s and Ozutsumi et al.'s
		{ "FeSCN 2+", { { iron_iii_ion, -1 }, { thiocyanate_ion, -1 }, { iron_thiocyanate, 1 } }, 3.5085, 0.001, -6.694 },
		{ "Fe(SCN)2+", { { iron_iii_ion, -1 }, { thiocyanate_ion, -2 }, { iron_dithiocyanate, 1 } }, 5.6655, 0.001, -11.0 },
		// cobalt's tetrachloro: calibrated in the model's own activities to Bjerrum, Halonin and Skibsted 1975's alpha4
		// = 0.496 in 8.23 M HCl (ThermoChimie's -2.09 at nought, sit.dat 2971, with the model's activities that far
		// past Pitzer's range, put all of it blue); its heat ThermoChimie's
		{ "CoCl4 2-", { { cobalt_ion, -1 }, { chloride_ion, -4 }, { cobalt_tetrachloro, 1 } }, -13.256, 0.001, 22.57 },
		// chromium(VI): NBS's rows (p. 2-197) against MINTEQ v4's constants (lines 4984, 5000; NIST 46.4)
		{ "HCrO4-", { { chromate_ion, -1 }, { hydrogen_ion, -1 }, { hydrogen_chromate_ion, 1 } }, 6.51, 0.05, 2.0 },
		{ "Cr2O7 2-", { { chromate_ion, -2 }, { hydrogen_ion, -2 }, { dichromate_ion, 1 }, { water, 1 } }, 14.56, 0.06, -15.0 },
	};
	for (const Row &row : rows) {
		const double lk = log_k(row.terms, T0);
		INFO(row.name << ": log K " << lk << " against " << row.log_k);
		CHECK(std::abs(lk - row.log_k) < row.tolerance);
		if (!std::isnan(row.heat)) {
			CHECK(std::abs(reaction_heat(row.terms) - row.heat) < 1.5);
		}
	}
}

TEST_CASE("complexes: ammonia turns copper deep blue and dissolves silver chloride; excess base redissolves zinc's and aluminium's hydroxides (Stage 81)") {
	using namespace builtin;
	// copper sulfate in 1 mol/kg ammonia: nearly all of it the tetrammine
	const CellState blue = testing::settled(Cell().put(copper_sulfate_aq, 0.01).put(ammonia_aq, 1.0).state, T0);
	MESSAGE("copper in ammonia: tetrammine " << mol_of(blue, copper_ammine_4) << ", free " << mol_of(blue, copper_ion));
	CHECK(mol_of(blue, copper_ammine_4) > 0.8 * 0.01);
	CHECK(mol_of(blue, copper_ion) < 1e-8);
	// silver chloride: in water a hundredth of a millimole; in 1 mol/kg ammonia all of a millimole
	const CellState water_only = testing::settled(Cell().put(silver_chloride, 0.001).state, T0);
	const CellState in_ammonia = testing::settled(Cell().put(silver_chloride, 0.001).put(ammonia_aq, 1.0).state, T0);
	MESSAGE("silver chloride left: in water " << mol_of(water_only, silver_chloride) << ", in ammonia " << mol_of(in_ammonia, silver_chloride));
	CHECK(mol_of(water_only, silver_chloride) > 0.00098);
	CHECK(mol_of(in_ammonia, silver_chloride) < 1e-6);
	CHECK(mol_of(in_ammonia, silver_diammine) == doctest::Approx(0.001).epsilon(0.02));
	// zinc and aluminium hydroxides: in water they stand; in 1 mol/kg sodium hydroxide they dissolve
	for (const auto &[solid, complex] : { std::pair{ zinc_hydroxide, zinc_tetrahydroxo }, std::pair{ aluminium_hydroxide, aluminium_tetrahydroxo } }) {
		const CellState still = testing::settled(Cell().put(solid, 0.001).state, T0);
		const CellState gone = testing::settled(Cell().put(solid, 0.001).put(sodium_hydroxide_aq, 1.0).state, T0);
		MESSAGE(builtin_species().species(solid).id << ": left in water " << mol_of(still, solid) << ", in the base " << mol_of(gone, solid));
		CHECK(mol_of(still, solid) > 0.00099);
		CHECK(mol_of(gone, solid) < 1e-6);
		CHECK(mol_of(gone, complex) > 0.0005);
	}
}

TEST_CASE("measured: iron(III) thiocyanate at Below, Connick and Coppel's and Lister and Rivington's quotients; cobalt blue at Bjerrum's chloride, and warm; dichromate in acid (Stage 81)") {
	using namespace builtin;
	const auto litres = [](const CellState &e) { return fill_of(builtin_species(), e, T0).condensed * 1000.0; };
	// Below et al.'s solutions: iron(III) in excess, 0.1 M acid, the ionic strength by sodium (perchlorate's there,
	// nitrate's here): Q1 = 146 at 0.40, a molar quotient; at 1.00 they measured 123, where the model's iron(III), with
	// no pair of its own in Pitzer's model, gives about 45 (declared)
	{
		const double fe = 0.002, scn = 0.0001, h = 0.1, na = 0.40 - 6.0 * fe - h - scn;
		const CellState e = testing::settled(Cell().put(iron_iii_nitrate_aq, fe).put(potassium_thiocyanate_aq, scn).put(nitric_acid_aq, h).put(sodium_nitrate_aq, na).state, T0);
		const double L = litres(e), q = (mol_of(e, iron_thiocyanate) / L) / ((mol_of(e, iron_iii_ion) / L) * (mol_of(e, thiocyanate_ion) / L));
		MESSAGE("FeSCN2+ quotient at 0.40 ionic strength: " << q << " (Below et al.: 146)");
		CHECK(q == doctest::Approx(146.0).epsilon(0.02));
	}
	// Lister and Rivington (Below et al.'s Table I): Q2 = 15.5 at 0.5
	{
		const double fe = 0.0001, scn = 0.05, h = 0.1, na = 0.5 - 0.0006 - h - scn;
		const CellState e = testing::settled(Cell().put(iron_iii_nitrate_aq, fe).put(potassium_thiocyanate_aq, scn).put(nitric_acid_aq, h).put(sodium_nitrate_aq, na).state, T0);
		const double L = litres(e), q2 = (mol_of(e, iron_dithiocyanate) / L) / ((mol_of(e, iron_thiocyanate) / L) * (mol_of(e, thiocyanate_ion) / L));
		MESSAGE("Fe(SCN)2+ step quotient at 0.5: " << q2 << " (Lister and Rivington: 15.5)");
		CHECK(q2 == doctest::Approx(15.5).epsilon(0.02));
	}
	// cobalt: Bjerrum, Halonin and Skibsted's alpha4 against HCl molarity (Table 1): 0.166 at 7.12 M, 0.496 at 8.23 M,
	// 0.765 at 9.17 M; the model's held at 8.23 M, steeper about it (declared), and in water none of it
	double last = 0.0;
	for (const auto &[molar, alpha] : { std::pair{ 7.12, 0.166 }, std::pair{ 8.23, 0.496 }, std::pair{ 9.17, 0.765 } }) {
		double lo = 1.0, hi = 20.0, m = 0.0;
		for (int k = 0; k < 40; ++k) {
			m = 0.5 * (lo + hi);
			(m / litres(Cell().put(hydrochloric_acid_aq, m).state) < molar ? lo : hi) = m;
		}
		const CellState e = testing::settled(Cell().put(hydrochloric_acid_aq, m).put(cobalt_chloride_aq, 0.0004).state, T0);
		const double a = mol_of(e, cobalt_tetrachloro) / (mol_of(e, cobalt_tetrachloro) + mol_of(e, cobalt_ion));
		MESSAGE("cobalt as CoCl4 2- in " << molar << " M HCl: " << a << " (Bjerrum et al.: " << alpha << ")");
		CHECK(a > last);
		if (molar == 8.23) {
			CHECK(a == doctest::Approx(alpha).epsilon(0.03));
			// warmer, bluer (its formation takes in heat: 22.57 kJ)
			const CellState warm = testing::settled(Cell().put(hydrochloric_acid_aq, m).put(cobalt_chloride_aq, 0.0004).state, T0 + 40.0);
			CHECK(mol_of(warm, cobalt_tetrachloro) > 1.4 * mol_of(e, cobalt_tetrachloro)); // (0.50 to 0.74 of it, 40 K warmer)
		}
		last = a;
	}
	const CellState pink = testing::settled(Cell().put(cobalt_chloride_aq, 0.01).state, T0);
	CHECK(mol_of(pink, cobalt_tetrachloro) < 1e-9);
	// chromate: in water yellow chromate; with acid most of the chromium as dichromate
	const CellState yellow = testing::settled(Cell().put(potassium_chromate_aq, 0.05).state, T0);
	const CellState orange = testing::settled(Cell().put(potassium_chromate_aq, 0.05).put(nitric_acid_aq, 0.2).state, T0);
	MESSAGE("chromium as dichromate: in water " << 2.0 * mol_of(yellow, dichromate_ion) / 0.05 << ", in acid " << 2.0 * mol_of(orange, dichromate_ion) / 0.05);
	CHECK(2.0 * mol_of(yellow, dichromate_ion) < 0.01 * 0.05);
	CHECK(2.0 * mol_of(orange, dichromate_ion) > 0.6 * 0.05);
}

TEST_CASE("measured: the complexes' absorption is their sources' (Stage 81)") {
	using namespace builtin;
	const auto at = [](SpeciesId s, int nm) { return optics::spectrum(s)[static_cast<size_t>(nm - 380)]; };
	// chromate: Haupt 1952, Table 1 (a_M)
	CHECK(at(chromate_ion, 380) == doctest::Approx(4526.0).epsilon(1e-9));
	CHECK(at(chromate_ion, 400) == doctest::Approx(1922.0).epsilon(1e-9));
	CHECK(at(chromate_ion, 450) == doctest::Approx(160.0).epsilon(1e-9));
	// the tetrammine: Bjerrum 1932, Table 14; its maximum near 600 nm
	CHECK(at(copper_ammine_4, 600) == doctest::Approx(51.6).epsilon(1e-9));
	CHECK(at(copper_ammine_4, 546) == doctest::Approx(40.3).epsilon(1e-9));
	// CoCl4 2-: Bjerrum, Halonin and Skibsted 1975, Table 1 (LiCl)
	CHECK(at(cobalt_tetrachloro, 690) == doctest::Approx(600.0).epsilon(1e-9));
	CHECK(at(cobalt_tetrachloro, 610) == doctest::Approx(279.0).epsilon(1e-9));
	// FeSCN2+: Betts and Dainton's 5000 at 460 nm, Ozutsumi et al.'s shape
	CHECK(at(iron_thiocyanate, 460) == doctest::Approx(5000.0).epsilon(1e-9));
	// Eriochrome Black T: Lindstrom 1959's figures (HIn2- 33,100 at 610 nm; MgIn- 33,200 at 560; CaIn- 29,300 at 540;
	// In3- 20,900 at 500)
	CHECK(at(ebt_h1, 610) == doctest::Approx(33100.0).epsilon(1e-9));
	CHECK(at(magnesium_ebt, 560) == doctest::Approx(33200.0).epsilon(1e-9));
	CHECK(at(calcium_ebt, 540) == doctest::Approx(29300.0).epsilon(1e-9));
	CHECK(at(ebt_ion, 500) == doctest::Approx(20900.0).epsilon(1e-9));
	// colourless: the ammines of silver, the hydroxo complexes, thiocyanate
	for (const SpeciesId s : { silver_diammine, zinc_tetrahydroxo, aluminium_tetrahydroxo, thiocyanate_ion }) {
		CHECK(at(s, 550) == 0.0);
	}
	// the colours they make: chromate yellow (absorbs the blue, passes the red), dichromate orange (absorbs further into
	// the green), the tetrammine deep blue (absorbs the red and yellow), FeSCN2+ red (absorbs the blue-green)
	CHECK(at(chromate_ion, 420) > 10.0 * at(chromate_ion, 550));
	CHECK(at(dichromate_ion, 480) > 5.0 * at(chromate_ion, 480));
	CHECK(at(copper_ammine_4, 600) > 5.0 * at(copper_ammine_4, 470));
	CHECK(at(iron_thiocyanate, 470) > 5.0 * at(iron_thiocyanate, 650));
}

TEST_CASE("measured: chromate's, the chromates' and thiocyanate's heat capacities and volumes are OBIGT's on the model's footing (Stage 81)") {
	using namespace builtin;
	// OBIGT (CHNOSZ inorganic_aq.csv; Shock, Sassani, Willis and Sverjensky 1997), cal and cm3, then Cp - 216.35 z, V - 16.74 z
	struct Row {
		SpeciesId s;
		double cp_cal, v, z;
	};
	for (const Row &r : { Row{ chromate_ion, -62.6, 19.8, -2 }, Row{ hydrogen_chromate_ion, 1.5, 44.9, -1 }, Row{ dichromate_ion, -29.1, 72.7, -2 }, Row{ thiocyanate_ion, -9.6, 35.7, -1 } }) {
		const SpeciesDef &d = builtin_species().species(r.s);
		CAPTURE(d.id);
		CHECK(d.cp == doctest::Approx(r.cp_cal * 4.184 - 216.35 * r.z).epsilon(1e-4));
		CHECK(d.molar_volume * 1e6 == doctest::Approx(r.v - 16.74 * r.z).epsilon(1e-4));
	}
	// and SCN-'s against NBS's own Cp, -40.2 J (p. 2-92), on the same footing
	CHECK(builtin_species().species(thiocyanate_ion).cp == doctest::Approx(-40.2 + 216.35).epsilon(1e-3));
}

TEST_CASE("measured: EDTA's constants are MINTEQ v4's, Eriochrome Black T's Lindstrom's at 0.1 ionic strength; a hardness titration turns the dye from wine red to blue at its end point (Stage 81)") {
	using namespace builtin;
	// EDTA: MINTEQ v4 (NIST 46.2), cumulative from Y4- (lines 7224 to 7248, 7664, 7648, 7408, 7352, 7552)
	const std::vector<std::tuple<const char *, std::vector<std::pair<SpeciesId, int>>, double>> rows{
		{ "HY3-", { { edta_ion, -1 }, { hydrogen_ion, -1 }, { edta_h1, 1 } }, 10.948 },
		{ "H2Y2-", { { edta_ion, -1 }, { hydrogen_ion, -2 }, { edta_h2, 1 } }, 17.221 },
		{ "H3Y-", { { edta_ion, -1 }, { hydrogen_ion, -3 }, { edta_h3, 1 } }, 20.34 },
		{ "H4Y", { { edta_ion, -1 }, { hydrogen_ion, -4 }, { edta_h4, 1 } }, 22.5 },
		{ "CaY2-", { { calcium_ion, -1 }, { edta_ion, -1 }, { calcium_edta, 1 } }, 12.42 },
		{ "MgY2-", { { magnesium_ion, -1 }, { edta_ion, -1 }, { magnesium_edta, 1 } }, 10.57 },
		{ "CuY2-", { { copper_ion, -1 }, { edta_ion, -1 }, { copper_edta, 1 } }, 20.5 },
		{ "ZnY2-", { { zinc_ion, -1 }, { edta_ion, -1 }, { zinc_edta, 1 } }, 18.0 },
		{ "FeY-", { { iron_iii_ion, -1 }, { edta_ion, -1 }, { iron_iii_edta, 1 } }, 27.7 },
	};
	for (const auto &[name, terms, target] : rows) {
		INFO(name);
		CHECK(log_k(terms, T0) == doctest::Approx(target).epsilon(1e-4));
	}
	const auto ph_of = [](const CellState &e) {
		double p = 0.0;
		ph(builtin_species(), e, T0, p);
		return p;
	};
	// Eriochrome Black T in 0.1 ionic strength (KCl), as Lindstrom 1959 measured it: pKa3 11.50, pKa2 6.91 (mixed: the pH
	// meter's activity of hydrogen ions, the dye's forms by concentration), log K MgIn- 7.26 and CaIn- 5.23
	{
		const CellState e = testing::settled(Cell().put(potassium_chloride_aq, 0.097).put(eriochrome_black_t_aq, 0.0001).put(sodium_hydroxide_aq, 0.003).state, T0);
		CHECK(ph_of(e) + std::log10(mol_of(e, ebt_h1) / mol_of(e, ebt_ion)) == doctest::Approx(11.50).epsilon(0.002));
	}
	{
		const CellState e = testing::settled(Cell().put(potassium_chloride_aq, 0.1).put(eriochrome_black_t_aq, 0.0001).put(sodium_hydroxide_aq, 0.00005).state, T0);
		CHECK(ph_of(e) + std::log10(mol_of(e, ebt_h2) / mol_of(e, ebt_h1)) == doctest::Approx(6.91).epsilon(0.002));
	}
	for (const auto &[metal, free, complex, target] : { std::tuple{ magnesium_chloride_aq, magnesium_ion, magnesium_ebt, 7.26 }, std::tuple{ calcium_chloride_aq, calcium_ion, calcium_ebt, 5.23 } }) {
		const CellState e = testing::settled(Cell().put(potassium_chloride_aq, 0.085).put(ammonia_aq, 0.02).put(ammonium_chloride_aq, 0.004).put(eriochrome_black_t_aq, 0.00005).put(metal, 0.0002).state, T0);
		CHECK(std::log10(mol_of(e, complex) / (mol_of(e, free) * mol_of(e, ebt_ion))) == doctest::Approx(target).epsilon(0.002));
	}
	// a hardness titration at pH 10: 2 mmol calcium and 0.5 mmol magnesium with the dye; short of the EDTA they take the
	// dye is mostly wine red MgIn-, past it mostly blue HIn2-
	const auto titrated = [&](double edta) {
		return testing::settled(Cell().put(ammonia_aq, 0.1).put(ammonium_chloride_aq, 0.02).put(calcium_chloride_aq, 0.002).put(magnesium_chloride_aq, 0.0005).put(eriochrome_black_t_aq, 0.00002).put(disodium_edta_aq, edta).state, T0);
	};
	const CellState before = titrated(0.9 * 0.0025), after = titrated(1.1 * 0.0025);
	MESSAGE("the dye before the end point: MgIn- " << mol_of(before, magnesium_ebt) << ", HIn2- " << mol_of(before, ebt_h1) << "; after: MgIn- " << mol_of(after, magnesium_ebt) << ", HIn2- " << mol_of(after, ebt_h1));
	CHECK(mol_of(before, magnesium_ebt) > mol_of(before, ebt_h1));
	CHECK(mol_of(after, ebt_h1) > 5.0 * mol_of(after, magnesium_ebt));
}
