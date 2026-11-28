// Metals, their salts and precipitates (GAME_SPEC §9, Stage 76): each precipitate's solubility product against MINTEQ
// v4's, the hydrolysis that makes iron(III) and aluminium acid against NIST 46.3's, a saturated calcium hydroxide
// against NIST SRM 2193b's certified pH from 5 to 50 C, silver chloride's solubility and its common ion.
// docs/DATA_SOURCES.md, "Metals and precipitates (Stage 76)".
#include "tables.hpp"
#include "chem/builtin_ids.hpp"
#include "chem/optics.hpp"
#include "chem/thermo.hpp"

#include <doctest/doctest.h>

#include <array>
#include <cmath>
#include <string>
#include <initializer_list>
#include <tuple>
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
	Cell() { put(builtin::water, 1.0 / 0.018015); }
	Cell &put(SpeciesId s, double mol) {
		testing::put_amount(builtin_species(), state, s, quanta(mol));
		return *this;
	}
};

double mol_of(const CellState &c, SpeciesId s) {
	return static_cast<double>(c.amount[static_cast<size_t>(s.index)]) * MOL_PER_QUANTUM;
}

// a reaction's log10 K from the tables at temperature T, as the model's affinity has it (constant heat capacities)
double log_k(std::initializer_list<std::pair<SpeciesId, int>> terms, double kelvin) {
	double g = 0.0;
	for (const auto &[s, nu] : terms) {
		const SpeciesDef &d = builtin_species().species(s);
		g += nu * (d.hf + d.cp * (kelvin - T0) - kelvin * (d.s0 + d.cp * std::log(kelvin / T0)));
	}
	return -g / (GAS_CONSTANT * kelvin * std::log(10.0));
}

double reaction_heat(std::initializer_list<std::pair<SpeciesId, int>> terms) {
	double h = 0.0;
	for (const auto &[s, nu] : terms) {
		h += nu * builtin_species().species(s).hf;
	}
	return h * 1e-3;
}

} // namespace

TEST_CASE("measured: each precipitate's solubility product is MINTEQ v4's at 25 C, and its heat near MINTEQ's (Stage 76)") {
	// minteq.v4.dat's PHASES, as written there: log_k and delta_h (kJ; none where it gives "-0", none given)
	using namespace builtin;
	struct Row {
		const char *name;
		std::vector<std::pair<SpeciesId, int>> terms;
		double log_k, delta_h; // delta_h NAN: none given
	};
	const double none = NAN;
	const std::vector<Row> rows = {
		{ "AgCl", { { silver_chloride, -1 }, { silver_ion, 1 }, { chloride_ion, 1 } }, -9.75, 65.2 },
		{ "Ag2O", { { silver_oxide, -1 }, { hydrogen_ion, -2 }, { silver_ion, 2 }, { water, 1 } }, 12.574, -45.62 },
		{ "BaSO4", { { barium_sulfate, -1 }, { barium_ion, 1 }, { sulfate_ion, 1 } }, -9.98, 23.0 },
		{ "BaCO3", { { barium_carbonate, -1 }, { barium_ion, 1 }, { carbonate_ion, 1 } }, -8.57, 4.0 },
		{ "CaCO3", { { calcium_carbonate, -1 }, { calcium_ion, 1 }, { carbonate_ion, 1 } }, -8.48, -8.0 },
		{ "CaSO4.2H2O", { { gypsum, -1 }, { calcium_ion, 1 }, { sulfate_ion, 1 }, { water, 2 } }, -4.61, 1.0 },
		{ "Ca(OH)2", { { calcium_hydroxide, -1 }, { hydrogen_ion, -2 }, { calcium_ion, 1 }, { water, 2 } }, 22.804, -128.62 },
		{ "Mg(OH)2", { { magnesium_hydroxide, -1 }, { hydrogen_ion, -2 }, { magnesium_ion, 1 }, { water, 2 } }, 16.844, -113.996 },
		{ "Fe(OH)2", { { iron_ii_hydroxide, -1 }, { hydrogen_ion, -2 }, { iron_ii_ion, 1 }, { water, 2 } }, 13.564, none },
		{ "Fe(OH)3", { { iron_iii_hydroxide, -1 }, { hydrogen_ion, -3 }, { iron_iii_ion, 1 }, { water, 3 } }, 3.191, -73.374 },
		{ "Zn(OH)2", { { zinc_hydroxide, -1 }, { hydrogen_ion, -2 }, { zinc_ion, 1 }, { water, 2 } }, 11.534, -81.8 },
		{ "Al(OH)3", { { aluminium_hydroxide, -1 }, { hydrogen_ion, -3 }, { aluminium_ion, 1 }, { water, 3 } }, 10.8, -111.0 },
		{ "Co(OH)2", { { cobalt_hydroxide, -1 }, { hydrogen_ion, -2 }, { cobalt_ion, 1 }, { water, 2 } }, 13.094, none },
	};
	for (const Row &row : rows) {
		double lk = 0.0, heat = 0.0;
		{
			double g = 0.0, h = 0.0;
			for (const auto &[s, nu] : row.terms) {
				const SpeciesDef &d = builtin_species().species(s);
				g += nu * (d.hf - T0 * d.s0);
				h += nu * d.hf;
			}
			lk = -g / (GAS_CONSTANT * T0 * std::log(10.0));
			heat = h * 1e-3;
		}
		INFO(row.name << ": log K " << lk << " against " << row.log_k << ", heat " << heat << " kJ against " << row.delta_h);
		CHECK(std::abs(lk - row.log_k) < 0.005);
		// the heat is NBS's formation enthalpies' (each S0 set to the measured K): within 6 kJ of MINTEQ's but iron(III)
		// hydroxide's (NBS's precipitate, 10 kJ from the ferrihydrite MINTEQ's K is of)
		if (!std::isnan(row.delta_h)) {
			CHECK(std::abs(heat - row.delta_h) < (std::string(row.name) == "Fe(OH)3" ? 10.0 : 6.0));
		}
	}
}

TEST_CASE("measured: calcite's, gypsum's, barite's and witherite's solubility products follow wateq4f's from 10 to 80 C (Stage 76)") {
	// wateq4f.dat's -analytical: log K = A1 + A2 T + A3 / T + A4 log10 T + A5 / T^2; the model's by NBS's heat capacities
	// held constant (GAME_SPEC §20): within 0.08 to 50 C, 0.17 at 80
	using namespace builtin;
	struct Row {
		const char *name;
		std::vector<std::pair<SpeciesId, int>> terms;
		std::array<double, 5> a;
	};
	const std::vector<Row> rows = {
		{ "calcite", { { calcium_carbonate, -1 }, { calcium_ion, 1 }, { carbonate_ion, 1 } }, { -171.9065, -0.077993, 2839.319, 71.595, 0 } },
		{ "gypsum", { { gypsum, -1 }, { calcium_ion, 1 }, { sulfate_ion, 1 }, { water, 2 } }, { 68.2401, 0, -3221.51, -25.0627, 0 } },
		{ "barite", { { barium_sulfate, -1 }, { barium_ion, 1 }, { sulfate_ion, 1 } }, { 136.035, 0, -7680.41, -48.595, 0 } },
		{ "witherite", { { barium_carbonate, -1 }, { barium_ion, 1 }, { carbonate_ion, 1 } }, { 607.642, 0.121098, -20011.25, -236.4948, 0 } },
	};
	for (const Row &row : rows) {
		for (const double kelvin : { 283.15, 298.15, 323.15, 353.15 }) {
			double g = 0.0;
			for (const auto &[sid, nu] : row.terms) {
				const SpeciesDef &d = builtin_species().species(sid);
				g += nu * (d.hf + d.cp * (kelvin - T0) - kelvin * (d.s0 + d.cp * std::log(kelvin / T0)));
			}
			const double model = -g / (GAS_CONSTANT * kelvin * std::log(10.0));
			const double wateq = row.a[0] + row.a[1] * kelvin + row.a[2] / kelvin + row.a[3] * std::log10(kelvin) + row.a[4] / (kelvin * kelvin);
			INFO(row.name << " at " << kelvin - 273.15 << " C: " << model << " against " << wateq);
			CHECK(std::abs(model - wateq) < (kelvin < 330.0 ? 0.08 : 0.17));
		}
	}
}

TEST_CASE("measured: iron(III)'s and aluminium's hydrolysis are NIST 46.3's, as MINTEQ v4 has them (Stage 76)") {
	using namespace builtin;
	const double fe = log_k({ { iron_iii_ion, -1 }, { water, -1 }, { iron_hydroxo_ion, 1 }, { hydrogen_ion, 1 } }, T0);
	const double al = log_k({ { aluminium_ion, -1 }, { water, -1 }, { aluminium_hydroxo_ion, 1 }, { hydrogen_ion, 1 } }, T0);
	MESSAGE("FeOH2+: log K " << fe << " (NIST 46.3 -2.187); AlOH2+: " << al << " (-4.997)");
	CHECK(fe == doctest::Approx(-2.187).epsilon(0.01)); // NBS's own rows, not fitted
	CHECK(al == doctest::Approx(-4.997).epsilon(0.002));
	CHECK(reaction_heat({ { iron_iii_ion, -1 }, { water, -1 }, { iron_hydroxo_ion, 1 }, { hydrogen_ion, 1 } }) == doctest::Approx(41.81).epsilon(0.05));
	CHECK(reaction_heat({ { aluminium_ion, -1 }, { water, -1 }, { aluminium_hydroxo_ion, 1 }, { hydrogen_ion, 1 } }) == doctest::Approx(47.81).epsilon(0.001));
	// their salts' solutions are acid: 0.1 mol/kg iron(III) chloride about pH 1.8 (ideally sqrt(K c): 1.6), aluminium
	// chloride about 3 (3.0), each the higher by its triply charged ion's activity
	double feph = 0.0, alph = 0.0;
	REQUIRE(ph(builtin_species(), testing::settled(Cell().put(iron_iii_chloride_aq, 0.1).state, T0), T0, feph));
	REQUIRE(ph(builtin_species(), testing::settled(Cell().put(aluminium_chloride_aq, 0.1).state, T0), T0, alph));
	MESSAGE("0.1 mol/kg FeCl3: pH " << feph << "; AlCl3: pH " << alph);
	CHECK(feph > 1.4);
	CHECK(feph < 2.2);
	CHECK(alph > 2.6);
	CHECK(alph < 3.6);
}

TEST_CASE("measured: a saturated calcium hydroxide solution reads NIST SRM 2193b's certified pH from 5 to 50 C (Stage 76)") {
	// SRM 2193b, Certificate of Analysis (23 December 2024): pH(S), the Bates-Guggenheim convention, CO2-free water
	using namespace builtin;
	for (const auto &[celsius, certified] : { std::pair{ 5.0, 13.218 }, std::pair{ 15.0, 12.815 }, std::pair{ 25.0, 12.453 }, std::pair{ 35.0, 12.129 },
				 std::pair{ 50.0, 11.709 } }) {
		const double kelvin = celsius + 273.15;
		const CellState c = testing::settled(Cell().put(calcium_hydroxide, 0.1).state, kelvin);
		double p = 0.0;
		REQUIRE(ph(builtin_species(), c, kelvin, p));
		const double dissolved = mol_of(c, calcium_ion);
		INFO(celsius << " C: pH " << p << " against " << certified << ", " << dissolved << " mol/kg dissolved");
		CHECK(mol_of(c, calcium_hydroxide) > 0.05); // saturated: the solid stays
		// within 0.05 to 35 C; at 50 C 0.07 low, the activity model's parameters 25 C's at every temperature (GAME_SPEC §20)
		CHECK(std::abs(p - certified) < (celsius < 40.0 ? 0.05 : 0.08));
	}
}

TEST_CASE("solubility: silver chloride and barium sulfate dissolve to their solubility products, and a common ion holds them back (Stage 76)") {
	using namespace builtin;
	// in water: s = sqrt(Ksp) / gamma; AgCl's 1.33e-5 mol/kg, BaSO4's about 1.1e-5
	const CellState agcl = testing::settled(Cell().put(silver_chloride, 0.001).state, T0);
	const CellState baso4 = testing::settled(Cell().put(barium_sulfate, 0.001).state, T0);
	const double s_ag = mol_of(agcl, silver_ion), s_ba = mol_of(baso4, barium_ion);
	MESSAGE("in water: AgCl " << s_ag << " mol/kg, BaSO4 " << s_ba);
	CHECK(s_ag == doctest::Approx(std::sqrt(std::pow(10.0, -9.75))).epsilon(0.03));
	CHECK(s_ba == doctest::Approx(std::sqrt(std::pow(10.0, -9.98))).epsilon(0.06));
	// in 0.01 mol/kg sodium chloride: Ksp / (0.01 gamma^2), about 2e-8
	const CellState common = testing::settled(Cell().put(sodium_chloride_aq, 0.01).put(silver_chloride, 0.001).state, T0);
	const double held = mol_of(common, silver_ion);
	MESSAGE("in 0.01 mol/kg NaCl: AgCl " << held << " mol/kg");
	CHECK(held < s_ag / 100.0);
	CHECK(held > 1.5e-8);
	CHECK(held < 3.5e-8);
	// mixed, silver nitrate and sodium chloride: the precipitate takes nearly all of the silver
	const CellState mixed = testing::settled(Cell().put(silver_nitrate_aq, 0.01).put(sodium_chloride_aq, 0.01).state, T0);
	CHECK(mol_of(mixed, silver_chloride) == doctest::Approx(0.01).epsilon(0.002));
}

TEST_CASE("measured: cobalt(II)'s and FeOH2+'s absorption are their sources' (Stage 76)") {
	// Ballhausen and Jorgensen 1955, Fig. 2: [Co(H2O)6]2+ 4.8 at 512 nm, 3.3 at 470, 2.2 at 550, 0.65 at 420 (read from
	// the figure); FeOH2+ (Byrne and Kester 1978; King 1988): 380 nm 300 (the visible's first row of the pass), its tail
	// past it the band's. The model's decadic molar absorptivity at 1 nm from 380
	const optics::Spectrum &co = optics::spectrum(builtin::cobalt_ion);
	const optics::Spectrum &fe = optics::spectrum(builtin::iron_hydroxo_ion);
	const auto at = [](const optics::Spectrum &s, int nm) { return s[static_cast<size_t>(nm - 380)]; };
	CHECK(at(co, 512) == doctest::Approx(4.8).epsilon(0.03));
	CHECK(std::abs(at(co, 470) - 3.3) < 0.3);
	CHECK(std::abs(at(co, 550) - 2.2) < 0.3);
	CHECK(std::abs(at(co, 420) - 0.65) < 0.3);
	CHECK(at(fe, 380) == doctest::Approx(300.0).epsilon(0.05));
	CHECK(at(fe, 450) < at(fe, 400) / 4.0); // yellow: it absorbs the violet and blue, not the green
	CHECK(at(fe, 550) < 1.0);
}
