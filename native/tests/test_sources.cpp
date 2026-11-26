// Every value fetched from a source, held to it (docs/DATA_SOURCES.md): the table the build embeds read back against
// the cited rows, typed here apart from data/, and what follows from them against what was measured apart.
#include "chem/builtin_ids.hpp"
#include "chem/quantity.hpp"
#include "chem/species.hpp"
#include "chem/thermo.hpp"

#include <doctest/doctest.h>

#include <cmath>

using namespace chem;

namespace {

bool near(double a, double b, double rel) {
	return b != 0.0 && std::abs(a / b - 1.0) < rel;
}

// IUPAC's abridged standard atomic weights (CIAAW), g/mol
constexpr double H = 1.008, C = 12.011, N = 14.007, O = 15.999, SI = 28.085, S = 32.06, CU = 63.546, NA = 22.990, NI = 58.693;

double grams_per_mol(SpeciesId s) {
	return static_cast<double>(builtin_species().molar_mass_ug(s)) / 1e6;
}

// a reaction's standard Gibbs energy at 25 C from the table, J/mol
double gibbs(const Reaction &r) {
	double g = 0.0;
	for (const Term &t : r.terms()) {
		const SpeciesDef &d = builtin_species().species(t.species);
		g += t.nu * (d.hf - 298.15 * d.s0);
	}
	return g;
}

} // namespace

TEST_CASE("sources: every species' thermodynamic row is the one cited (NIST, CODATA, Chase, Atkins, NBS, Kiseleva, ISO 3585, NETZSCH) (Stage 23)") {
	const struct {
		SpeciesId id;
		double hf_kj, s0, cp;
	} rows[] = {
		{ builtin::water, -285.830, 69.95, 75.375 }, // NIST: CODATA; Chase's fit
		{ builtin::steam, -241.826, 188.835, 33.590 },
		{ builtin::carbon_dioxide, -393.51, 213.785, 37.130 },
		{ builtin::nitrogen, 0.0, 191.609, 29.124 },
		{ builtin::oxygen, 0.0, 205.152, 29.383 },
		{ builtin::copper_oxide, -156.06, 42.59, 42.246 }, // NIST: Chase
		{ builtin::copper_sulfate, -771.4, 109.2, 98.531 }, // NIST: CODATA; Chase's Cp
		{ builtin::malachite, -1053.95, 166.3, 154.3 }, // Richardson and Brown 1974; Kiseleva et al. 1992
		{ builtin::glass, -910.86, 41.46, 48.066 }, // quartz (Chase), a stand-in; Cp: ISO 3585's 0.8 J/g/K
		{ builtin::copper_sulfate_pentahydrate, -2279.7, 300.4, 280.0 }, // Atkins (NBS 1982 rounded)
		{ builtin::copper_sulfate_trihydrate, -1684.31, 221.3, 205.0 }, // NBS 1982, p. 2-156
		{ builtin::copper_sulfate_monohydrate, -1085.83, 146.0, 134.0 },
		{ builtin::sulfuric_acid_aq, -909.27, 20.1, 138.9 }, // Atkins's SO4 2-(aq); Cp a stand-in
		{ builtin::copper_sulfate_aq, -835.990, -51.520, 211.81 }, // fitted to the CRC's solubility (Stage 19)
		{ builtin::latex, 0.0, 0.0, 130.1 }, // NETZSCH's 1.91 J/g/K
		{ builtin::sodium_hydroxide_aq, -470.114, 48.1, -102.1 }, // NBS 1982, p. 2-299, NaOH "ai" (Stage 43)
		{ builtin::sodium_sulfate_aq, -1389.51, 138.1, -201.0 }, // NBS 1982, p. 2-307, Na2SO4 "ai"
		// Puigdomenech and Taxen 2000, SKB TR-00-13, Table 2: dG -359.92, S 87.0 and its Cp equation at 25 C;
		// dH with CODATA's elements (Stage 44: JANAF 1966's row makes it more stable than CuO and water, as no
		// solubility measured does)
		{ builtin::copper_hydroxide, -443.99, 87.0, 87.85 },
		// NBS 1982, p. 2-167, NiSO4 "ai" (Stage 47); NBS gives no heat capacity: CuSO4(aq)'s, a stand-in
		{ builtin::nickel_sulfate_aq, -963.2, -108.8, 211.81 },
		// NIST's polystyrene foam correlation near 293 K, 1.17 J/g/K, the styrene unit (Stage 52); a wall's formation terms 0
		{ builtin::polystyrene_foam, 0.0, 0.0, 121.86 },
	};
	for (const auto &row : rows) {
		const SpeciesDef &d = builtin_species().species(row.id);
		CAPTURE(d.id);
		REQUIRE(d.has_thermo);
		CHECK(std::abs(d.hf - row.hf_kj * 1000.0) < 1e-6);
		CHECK(std::abs(d.s0 - row.s0) < 1e-9);
		CHECK(std::abs(d.cp - row.cp) < 1e-9);
	}
	// the Shomate fits' and ISO 3585's figures behind two of them
	CHECK(near(0.8 * (SI + 2 * O), 48.066, 1e-4)); // glass: 0.8 J/g/K times SiO2's molar mass
	CHECK(near(1.91 * (5 * C + 8 * H), 130.1, 1e-3)); // latex: the isoprene unit
	CHECK(near(1.17 * (8 * C + 8 * H), 121.86, 1e-4)); // polystyrene foam: the styrene unit
	const double t = 298.15 / 1000.0; // water's liquid fit (Chase 1998, 298-500 K)
	CHECK(near(-203.6060 + 1523.290 * t - 3196.413 * t * t + 2474.455 * t * t * t + 3.855326 / (t * t), 75.375, 1e-4));
}

TEST_CASE("sources: molar masses are IUPAC's atomic weights, and molar volumes the cited densities (Stage 23)") {
	const struct {
		SpeciesId id;
		double grams, density; // g/cm^3; 0: none
	} rows[] = {
		{ builtin::water, 2 * H + O, 0.997048 }, // IAPWS-95 at 25 C (NIST)
		{ builtin::copper_sulfate, CU + S + 4 * O, 3.603 }, // Merck 102791
		{ builtin::copper_oxide, CU + O, 6.315 }, // medium confidence
		{ builtin::malachite, 2 * CU + C + 5 * O + 2 * H, 3.8 }, // the middle of 3.6-4.0
		{ builtin::copper_sulfate_pentahydrate, CU + S + 4 * O + 5 * (2 * H + O), 2.286 }, // CRC 92nd ed.
		{ builtin::copper_sulfate_trihydrate, CU + S + 4 * O + 3 * (2 * H + O), 2.663 }, // synthetic bonattite
		{ builtin::copper_sulfate_monohydrate, CU + S + 4 * O + (2 * H + O), 3.382 }, // its structure at 100 K
		{ builtin::carbon_dioxide, C + 2 * O, 0.0 },
		{ builtin::nitrogen, 2 * N, 0.0 },
		{ builtin::sodium_hydroxide_aq, NA + O + H, 0.0 }, // Stage 43
		{ builtin::sodium_sulfate_aq, 2 * NA + S + 4 * O, 0.0 },
		{ builtin::nickel_sulfate_aq, NI + S + 4 * O, 0.0 }, // Stage 47
		{ builtin::copper_hydroxide, CU + 2 * O + 2 * H, 3.93 }, // spertiniite, measured: Handbook of Mineralogy (Stage 44)
	};
	for (const auto &row : rows) {
		const SpeciesDef &d = builtin_species().species(row.id);
		CAPTURE(d.id);
		CHECK(near(grams_per_mol(row.id), row.grams, 1e-9));
		if (row.density > 0.0) {
			CHECK(near(d.molar_volume, row.grams / row.density * 1e-6, 1e-9));
		}
	}
	CHECK(near(grams_per_mol(builtin::water), 18.015, 1e-5));
	CHECK(near(grams_per_mol(builtin::copper_sulfate_pentahydrate), 249.677, 1e-5));
}

TEST_CASE("sources: the solutes' apparent molar volumes follow from the densities of their 10 % solutions at 20 C (Stage 23)") {
	// Engineering Toolbox (low confidence): CuSO4 10 % 1.107, H2SO4 10 % 1.0661 g/cm^3; water 0.99820 at 20 C
	const auto apparent = [](double density, double grams_per_mol) {
		return (100.0 / density - 90.0 / 0.99820) / (10.0 / grams_per_mol); // cm^3/mol
	};
	const SpeciesTable &t = builtin_species();
	CHECK(near(t.species(builtin::copper_sulfate_aq).molar_volume * 1e6, 2.7, 1e-9));
	CHECK(near(t.species(builtin::sulfuric_acid_aq).molar_volume * 1e6, 35.7, 1e-9));
	CHECK(near(apparent(1.107, CU + S + 4 * O), 2.7, 0.03));
	CHECK(near(apparent(1.0661, 2 * H + S + 4 * O), 35.7, 0.01));
}

TEST_CASE("sources: the hydrates' dissociation pressures at 25 C follow from the table, as NBS's free energies give them and near Cotti's measurement (Stage 23)") {
	// p = e^(-dG / (nu R T)) bar, nu waters of steam. NBS 1982's free energies give 7.80, 5.14 and 0.0104 torr;
	// Cotti et al. 2024 measured 10 and 6 mbar for the first two
	const double torr = 750.0617, mbar = 1000.0;
	const struct {
		int reaction;
		double waters, nbs_torr, measured_mbar, to_nbs, to_measured;
	} rows[] = {
		{ builtin::reaction::pentahydrate_dehydration, 2.0, 7.80, 10.0, 0.02, 0.05 },
		{ builtin::reaction::trihydrate_dehydration, 2.0, 5.14, 6.0, 0.02, 0.2 },
		{ builtin::reaction::monohydrate_dehydration, 1.0, 0.0104, 0.0, 0.25, 0.0 }, // the table keeps CODATA's anhydrous salt
	};
	for (const auto &row : rows) {
		CAPTURE(row.nbs_torr);
		const double bar = std::exp(-gibbs(builtin_reactions()[static_cast<size_t>(row.reaction)]) / (row.waters * GAS_CONSTANT * 298.15));
		CHECK(near(bar * torr, row.nbs_torr, row.to_nbs));
		if (row.measured_mbar > 0.0) {
			CHECK(near(bar * mbar, row.measured_mbar, row.to_measured));
		}
		MESSAGE((bar * torr) << " torr, " << (bar * mbar) << " mbar");
	}
}

TEST_CASE("sources: malachite's activation energy lies in Seguin's range, and the constants are CODATA 2018's (Stage 23)") {
	const double ea = builtin_reactions()[static_cast<size_t>(builtin::reaction::malachite_decomposition)].kinetics().activation;
	CHECK(ea >= 131e3);
	CHECK(ea <= 159e3);
	CHECK(GAS_CONSTANT == 8.314462618);
	CHECK(P_ATMOSPHERE == 101325.0);
}

TEST_CASE("sources: each ion's data is its formula units' split exactly, the hydrogen ion's at nought (Stage 63)") {
	// the ions' formation enthalpies, entropies, heat capacities and apparent molar volumes are the retired formula
	// units' (each cited above) split by the convention that H+(aq) has none: so every solution keeps its energy,
	// its entropy, its volume and so its solubility (docs/TDD_63_IONS.md)
	const SpeciesTable &t = builtin_species();
	const SpeciesDef &h = t.species(builtin::hydrogen_ion);
	CHECK(h.hf == 0.0);
	CHECK(h.s0 == 0.0);
	CHECK(h.cp == 0.0);
	CHECK(h.molar_volume == 0.0);
	int retired = 0;
	for (int k = 0; k < t.species_count(); ++k) {
		const SpeciesDef &unit = t.species(SpeciesId{ k });
		if (!unit.retired) {
			continue;
		}
		++retired;
		CAPTURE(unit.id);
		double hf = 0.0, s0 = 0.0, cp = 0.0, volume = 0.0;
		for (const auto &[ion, count] : t.as_species(SpeciesId{ k })) {
			const SpeciesDef &d = t.species(ion);
			hf += count * d.hf;
			s0 += count * d.s0;
			cp += count * d.cp;
			volume += count * d.molar_volume;
		}
		CHECK(hf == doctest::Approx(unit.hf).epsilon(1e-9));
		CHECK(s0 == doctest::Approx(unit.s0).epsilon(1e-9));
		CHECK(cp == doctest::Approx(unit.cp).epsilon(1e-9));
		CHECK(volume == doctest::Approx(unit.molar_volume).epsilon(1e-9));
	}
	CHECK(retired == 5);
}
