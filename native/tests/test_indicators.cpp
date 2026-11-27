// The indicators (GAME_SPEC §9, §12, Stage 64): phenolphthalein, bromothymol blue and methyl red, each a weak acid at
// its measured constant whose forms absorb as measured; the colour a solution takes across the pH.
#include "tables.hpp"
#include "chem/builtin_ids.hpp"
#include "chem/optics.hpp"
#include "chem/thermo.hpp"

#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <initializer_list>
#include <vector>

using namespace chem;

namespace {

int64_t quanta(double n) {
	bool ok = false;
	const Amount a = amount_from_mol(n, ok);
	REQUIRE(ok);
	return a.quanta;
}

struct Cell {
	CellState state{ std::vector<int64_t>(static_cast<size_t>(builtin_species().species_count()), 0), 0,
		std::vector<int64_t>(static_cast<size_t>(builtin_species().species_count()), 0) };
	Cell() { put(builtin::water, 1.0 / 0.018015); }
	Cell &put(SpeciesId s, double mol) {
		testing::put_amount(builtin_species(), state, s, quanta(mol));
		return *this;
	}
};

double mol_of(const CellState &c, SpeciesId s) {
	return static_cast<double>(c.amount[static_cast<size_t>(s.index)]) * MOL_PER_QUANTUM;
}

double log_k(std::initializer_list<std::pair<SpeciesId, int>> terms, double kelvin) {
	double g = 0.0;
	for (const auto &[s, nu] : terms) {
		const SpeciesDef &d = builtin_species().species(s);
		g += nu * (d.hf + d.cp * (kelvin - T0) - kelvin * (d.s0 + d.cp * std::log(kelvin / T0)));
	}
	return -g / (GAS_CONSTANT * kelvin * std::log(10.0));
}

// a spectrum's peak above 380 nm and its value there
std::pair<int, double> peak(SpeciesId s, int from = 380, int to = 780) {
	const optics::Spectrum &sp = optics::spectrum(s);
	int best = from;
	for (int nm = from; nm <= to; ++nm) {
		if (sp[static_cast<size_t>(nm - 380)] > sp[static_cast<size_t>(best - 380)]) {
			best = nm;
		}
	}
	return { best, sp[static_cast<size_t>(best - 380)] };
}

} // namespace

TEST_CASE("measured: the indicators' constants are Tamura's, Shimada and Hasegawa's and Ramette's (Stage 64)") {
	using namespace builtin;
	const double t = 298.15;
	// phenolphthalein: Tamura, Abe, Ito and Maeda 1996 (Anal. Sci. 12, 927): pK1 9.05, pK2 9.50
	CHECK(-log_k({ { phenolphthalein_aq, -1 }, { phenolphthalein_ion, 1 }, { hydrogen_ion, 1 } }, t) == doctest::Approx(9.05).epsilon(1e-4));
	CHECK(-log_k({ { phenolphthalein_ion, -1 }, { phenolphthalein_dianion, 1 }, { hydrogen_ion, 1 } }, t) == doctest::Approx(9.50).epsilon(1e-4));
	// bromothymol blue: Shimada and Hasegawa 2017 (Spectrochim. Acta A 185, 104), thermodynamic, 7.5
	CHECK(-log_k({ { bromothymol_blue_ion, -1 }, { bromothymol_blue_dianion, 1 }, { hydrogen_ion, 1 } }, t) == doctest::Approx(7.5).epsilon(1e-4));
	// methyl red: Ramette, Dratz and Kelly 1962 (the IUPAC dataset), 4.82 at 25 C in acetate buffers
	CHECK(-log_k({ { methyl_red_aq, -1 }, { methyl_red_ion, 1 }, { hydrogen_ion, 1 } }, t) == doctest::Approx(4.82).epsilon(1e-4));
	// bromothymol blue's apparent constant at an ionic strength of 0.1 (its dianion's activity coefficient, the model's
	// Debye-Huckel term alone): Schill's 7.12 at 0.1 M, quoted by Avdeef 2023; Hoang et al.'s 7.29 to 7.36 in 0.1 M KCl
	for (const double ph_target : { 7.0, 7.3 }) {
		// a phosphate buffer at 0.1 mol/kg of KCl with a trace of the indicator
		const double ratio = std::pow(10.0, ph_target - 6.86);
		Cell c;
		// (the chloride makes the phosphate's strength up to 0.1)
		c.put(potassium_chloride_aq, 0.1 - 0.01 - 0.03 * ratio).put(potassium_dihydrogen_phosphate_aq, 0.01).put(disodium_hydrogen_phosphate_aq, 0.01 * ratio).put(sodium_bromothymol_blue_aq, 1e-5);
		const CellState s = testing::settled(c.state, t);
		double p = 0.0;
		REQUIRE(ph(builtin_species(), s, t, p));
		const double apparent = p - std::log10(mol_of(s, bromothymol_blue_dianion) / mol_of(s, bromothymol_blue_ion));
		MESSAGE("bromothymol blue at pH " << p << ", ionic strength " << ionic_strength(builtin_species(), s) << ": its apparent pKa " << apparent);
		CHECK(apparent > 7.0);
		CHECK(apparent < 7.4);
	}
}

TEST_CASE("measured: the indicators' forms absorb at their measured peaks (Stage 64)") {
	using namespace builtin;
	// phenolphthalein's dianion: 552.8 nm, about 3.7e4 L/mol/cm (Tamura et al.); its colourless forms absorb nothing
	const auto pp = peak(phenolphthalein_dianion, 450);
	CHECK(std::abs(pp.first - 553) <= 2);
	CHECK(pp.second == doctest::Approx(37110.0).epsilon(0.01));
	CHECK(peak(phenolphthalein_aq).second == 0.0);
	CHECK(peak(phenolphthalein_ion).second == 0.0);
	// bromothymol blue: yellow 430 nm, 16,600; blue 620 nm, 38,000 (the IChO 2017's absorptivities; Hoang et al.'s 432, 616)
	const auto yellow = peak(bromothymol_blue_ion), blue = peak(bromothymol_blue_dianion, 500);
	CHECK(std::abs(yellow.first - 432) <= 3);
	CHECK(yellow.second == doctest::Approx(16600.0).epsilon(0.01));
	CHECK(std::abs(blue.first - 616) <= 3);
	CHECK(optics::spectrum(bromothymol_blue_dianion)[620 - 380] == doctest::Approx(38000.0).epsilon(0.001));
	// methyl red: the red acid at 520 nm (4.4e4), the yellow base at 425 nm (1.8e4), Tobey 1958
	const auto red = peak(methyl_red_aq, 450), base = peak(methyl_red_ion);
	CHECK(std::abs(red.first - 520) <= 2);
	CHECK(red.second == doctest::Approx(44200.0).epsilon(0.01));
	CHECK(std::abs(base.first - 425) <= 2);
	CHECK(base.second == doctest::Approx(18410.0).epsilon(0.01));
}

TEST_CASE("indicators: each turns across its range: phenolphthalein pink only in base, bromothymol blue green at neutral, methyl red red in acid (Stage 64)") {
	using namespace builtin;
	const double t = 298.15;
	const auto settle = [&](Cell c) { return testing::settled(c.state, t); };
	// a drop of each in acid (0.001 mol/kg HCl), at neutral and in base (0.01 mol/kg NaOH)
	const auto share = [&](const CellState &s, SpeciesId coloured, std::initializer_list<SpeciesId> all) {
		double total = 0.0;
		for (const SpeciesId f : all) {
			total += mol_of(s, f);
		}
		return mol_of(s, coloured) / total;
	};
	const double drop = 1e-5;
	{
		const auto forms = { phenolphthalein_aq, phenolphthalein_ion, phenolphthalein_dianion };
		CHECK(share(settle(Cell().put(hydrochloric_acid_aq, 1e-3).put(phenolphthalein_aq, drop)), phenolphthalein_dianion, forms) < 1e-6);
		CHECK(share(settle(Cell().put(sodium_chloride_aq, 1e-3).put(phenolphthalein_aq, drop)), phenolphthalein_dianion, forms) < 1e-3);
		CHECK(share(settle(Cell().put(sodium_hydroxide_aq, 1e-2).put(phenolphthalein_aq, drop)), phenolphthalein_dianion, forms) > 0.9);
	}
	{
		const auto forms = { bromothymol_blue_ion, bromothymol_blue_dianion };
		CHECK(share(settle(Cell().put(hydrochloric_acid_aq, 1e-3).put(sodium_bromothymol_blue_aq, drop)), bromothymol_blue_dianion, forms) < 1e-3);
		const double neutral = share(settle(Cell().put(potassium_dihydrogen_phosphate_aq, 0.025).put(disodium_hydrogen_phosphate_aq, 0.025).put(sodium_bromothymol_blue_aq, drop)), bromothymol_blue_dianion, forms);
		MESSAGE("bromothymol blue in the 6.86 phosphate: " << neutral << " blue");
		CHECK(neutral > 0.2);
		CHECK(neutral < 0.5);
		CHECK(share(settle(Cell().put(sodium_hydroxide_aq, 1e-2).put(sodium_bromothymol_blue_aq, drop)), bromothymol_blue_dianion, forms) > 0.999);
	}
	{
		const auto forms = { methyl_red_aq, methyl_red_ion };
		CHECK(share(settle(Cell().put(hydrochloric_acid_aq, 1e-2).put(methyl_red_aq, drop)), methyl_red_aq, forms) > 0.99);
		// in a neutral buffer yellow; in pure water a trace of it is the acid that sets the pH, near 5, and it is part red
		CHECK(share(settle(Cell().put(potassium_dihydrogen_phosphate_aq, 0.025).put(disodium_hydrogen_phosphate_aq, 0.025).put(methyl_red_aq, drop)), methyl_red_ion, forms) > 0.99);
		const double alone = share(settle(Cell().put(methyl_red_aq, drop)), methyl_red_ion, forms);
		CHECK(alone > 0.5);
		CHECK(alone < 0.9);
	}
}
