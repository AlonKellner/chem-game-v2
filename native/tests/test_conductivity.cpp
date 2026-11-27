// A solution's conductivity (GAME_SPEC §10, Stage 65): each ion's Nernst-Einstein conductivity from its diffusion,
// lowered by Debye, Hückel and Onsager's atmosphere over an ion size, against what was measured at the sources' own
// conditions: the KCl standards (Jones and Bradshaw), KCl and NaCl (Shedlovsky), HCl from 0 to 45 C (Hamer and DeWane,
// NSRDS-NBS 33), acetic acid (MacInnes and Shedlovsky). docs/DATA_SOURCES.md, "Conductivity (Stage 65)".
#include "tables.hpp"
#include "chem/builtin_ids.hpp"
#include "chem/invariants.hpp"
#include "chem/thermo.hpp"

#include <doctest/doctest.h>

#include <cmath>
#include <initializer_list>
#include <string_view>
#include <utility>
#include <vector>

using namespace chem;

namespace {

int64_t quanta(double n) {
	bool ok = false;
	const Amount a = amount_from_mol(n, ok);
	REQUIRE(ok);
	return a.quanta;
}

const SpeciesTable &table() {
	return builtin_species();
}

// a kilogram of water and `mol` of a solute, settled at T
CellState solution(SpeciesId solute, double mol, double kelvin) {
	CellState state{ std::vector<int64_t>(static_cast<size_t>(table().species_count()), 0), 0, std::vector<int64_t>(static_cast<size_t>(table().species_count()), 0) };
	testing::put_amount(table(), state, builtin::water, quanta(1.0 / 0.018015));
	if (mol > 0.0) {
		testing::put_amount(table(), state, solute, quanta(mol));
	}
	return testing::settled(state, kelvin);
}

// the liquid's volume, m^3, as the model has it
double volume(const CellState &c, double kelvin) {
	double v = 0.0;
	for (size_t s = 0; s < c.amount.size(); ++s) {
		const SpeciesDef &d = table().species(SpeciesId{ static_cast<int32_t>(s) });
		if (c.amount[s] > 0 && !d.wall && (d.phase == Phase::Liquid || d.phase == Phase::Aqueous)) {
			v += static_cast<double>(c.amount[s]) * MOL_PER_QUANTUM * molar_volume_at(table(), d, kelvin);
		}
	}
	return v;
}

// the molar conductivity, S cm^2/mol, of `solute` at `molar` mol/L (its molality found so that the model's own
// volume gives that concentration); `limit` Kohlrausch's
// (a salt's less the water's own, as its measurers subtracted their solvent's; an acid's whole, its own ions holding
// back the water's)
double molar_conductivity(SpeciesId solute, double molar, double kelvin, double *limit = nullptr) {
	double mol = molar;
	CellState c;
	for (int i = 0; i < 4; ++i) {
		c = solution(solute, mol, kelvin);
		mol *= molar / (mol / volume(c, kelvin) * 1e-3);
	}
	c = solution(solute, mol, kelvin);
	const double per_m3 = mol / volume(c, kelvin);
	double kappa = 0.0, kohlrausch = 0.0;
	REQUIRE(conductivity(table(), c, kelvin, kappa, &kohlrausch));
	const SpeciesDef &def = table().species(solute);
	const bool acid = def.id == std::string_view("hydrochloric_acid_aq") || def.id == std::string_view("acetic_acid_aq");
	double water = 0.0, water_limit = 0.0;
	if (!acid) {
		REQUIRE(conductivity(table(), solution(builtin::water, 0.0, kelvin), kelvin, water, &water_limit));
	}
	if (limit != nullptr) {
		*limit = (kohlrausch - water_limit) / per_m3 * 1e4;
	}
	return (kappa - water) / per_m3 * 1e4;
}

void against(const char *name, SpeciesId solute, double kelvin, std::initializer_list<std::pair<double, double>> rows, double tolerance) {
	for (const auto &[c, measured] : rows) {
		const double model = molar_conductivity(solute, c, kelvin);
		INFO(name << " " << c << " mol/L at " << kelvin - 273.15 << " C: " << model << " against " << measured);
		CHECK(std::abs(model / measured - 1.0) < tolerance);
	}
}

} // namespace

TEST_CASE("measured: Debye, Hückel and Onsager's constants are NSRDS-NBS 33's from 0 to 100 C (Stage 65)") {
	// Hamer and DeWane 1970, Table 3: B1 and B2 for equivalent conductances of 1-1 aqueous solutions
	for (const auto &[celsius, b1, b2] : { std::tuple{ 0.0, 0.2211, 29.817 }, std::tuple{ 25.0, 0.2300, 60.639 }, std::tuple{ 50.0, 0.2416, 100.31 },
				 std::tuple{ 100.0, 0.2737, 202.71 } }) {
		double m1 = 0.0, m2 = 0.0, b = 0.0;
		REQUIRE(onsager_constants(table(), celsius + 273.15, m1, m2, b));
		INFO(celsius << " C: B1 " << m1 << " against " << b1 << ", B2 " << m2 * 1e4 << " against " << b2);
		// the viscosity is IAPWS 2008's at 0.5 MPa where the table's was Swindells', within 0.3 %
		CHECK(std::abs(m1 / b1 - 1.0) < 0.002);
		CHECK(std::abs(m2 * 1e4 / b2 - 1.0) < 0.004);
	}
}

TEST_CASE("measured: KCl's and NaCl's molar conductivity is Shedlovsky's from 0.0001 to 0.12 mol/L at 25 C (Stage 65)") {
	// Shedlovsky 1932, J. Am. Chem. Soc. 54, 1405, as MacInnes 1939 (The Principles of Electrochemistry) Table 18-II
	// gives it, observed; S cm^2/mol
	using namespace builtin;
	against("KCl", potassium_chloride_aq, 298.15,
			{ { 0.0001, 148.94 }, { 0.0005, 147.81 }, { 0.001, 146.95 }, { 0.005, 143.55 }, { 0.01, 141.27 }, { 0.02, 138.34 }, { 0.05, 133.37 }, { 0.1, 128.96 },
					{ 0.12, 127.69 } },
			0.005);
	against("NaCl", sodium_chloride_aq, 298.15,
			{ { 0.0001, 125.59 }, { 0.0005, 124.50 }, { 0.001, 123.74 }, { 0.005, 120.65 }, { 0.01, 118.51 }, { 0.02, 115.76 }, { 0.05, 111.06 }, { 0.1, 106.74 },
					{ 0.12, 105.48 } },
			0.005);
}

TEST_CASE("measured: HCl's molar conductivity is Hamer and DeWane's at 25 C, to 0.1 mol/L within half a percent (Stage 65)") {
	// NSRDS-NBS 33 (1970), Table 11; past 0.1 mol/L the model's one ion size and no viscosity of the solution read it
	// high (declared, GAME_SPEC §20): 2.5 % at 0.5 mol/L, 8 % at 1
	using namespace builtin;
	against("HCl", hydrochloric_acid_aq, 298.15, { { 0.0001, 424.5 }, { 0.0005, 422.6 }, { 0.001, 421.2 }, { 0.005, 415.7 }, { 0.01, 411.9 }, { 0.05, 398.9 }, { 0.1, 391.1 } }, 0.005);
	against("HCl", hydrochloric_acid_aq, 298.15, { { 0.5, 360.7 } }, 0.03);
	against("HCl", hydrochloric_acid_aq, 298.15, { { 1.0, 332.2 } }, 0.085);
}

TEST_CASE("measured: HCl's limiting conductivity follows Hamer and DeWane's from 0 to 45 C, the protons by Walden's exponent of their own (Stage 65)") {
	// NSRDS-NBS 33, Table 12: Lambda0 of HCl; Stokes and Einstein (an exponent of 1) put it 16 % low at 5 C
	using namespace builtin;
	for (const auto &[celsius, measured] : { std::pair{ 0.0, 265.03 }, std::pair{ 5.0, 297.42 }, std::pair{ 10.0, 329.78 }, std::pair{ 15.0, 362.05 },
				 std::pair{ 20.0, 394.16 }, std::pair{ 25.0, 426.06 }, std::pair{ 30.0, 457.65 }, std::pair{ 35.0, 488.91 }, std::pair{ 40.0, 519.75 },
				 std::pair{ 45.0, 550.12 } }) {
		double limit = 0.0;
		molar_conductivity(hydrochloric_acid_aq, 1e-5, celsius + 273.15, &limit);
		INFO(celsius << " C: Lambda0 " << limit << " against " << measured);
		CHECK(std::abs(limit / measured - 1.0) < (celsius < 2.0 ? 0.025 : 0.01));
	}
	// and at a finite concentration, 0.01 mol/L, from 5 to 45 C (Table 11)
	for (const auto &[celsius, measured] : { std::pair{ 5.0, 288.6 }, std::pair{ 15.0, 350.3 }, std::pair{ 35.0, 472.2 }, std::pair{ 45.0, 530.3 } }) {
		const double model = molar_conductivity(hydrochloric_acid_aq, 0.01, celsius + 273.15);
		INFO(celsius << " C, 0.01 mol/L: " << model << " against " << measured);
		CHECK(std::abs(model / measured - 1.0) < 0.012);
	}
}

TEST_CASE("measured: the KCl conductivity standards read Jones and Bradshaw's at 0, 18 and 25 C (Stage 65)") {
	// Jones and Bradshaw 1933, J. Am. Chem. Soc. 55, 1780, as MacInnes 1939 Table 3-I gives them: grams of KCl per
	// 1000 g of solution (in vacuum) and the specific conductance, S/cm
	using namespace builtin;
	const double molar_mass = 74.5513; // g/mol (the elements' weights)
	struct Standard {
		double grams, s0, s18, s25;
	};
	for (const Standard &s : { Standard{ 0.745263, 0.00077364, 0.0012205, 0.00140877 }, Standard{ 7.41913, 0.0071379, 0.0111667, 0.012856 },
				 Standard{ 71.1352, 0.065176, 0.09783, 0.111342 } }) {
		const double mol = s.grams / molar_mass / ((1000.0 - s.grams) / 1000.0);
		for (const auto &[celsius, measured] : { std::pair{ 0.0, s.s0 }, std::pair{ 18.0, s.s18 }, std::pair{ 25.0, s.s25 } }) {
			const double kelvin = celsius + 273.15;
			double kappa = 0.0;
			REQUIRE(conductivity(table(), solution(potassium_chloride_aq, mol, kelvin), kelvin, kappa));
			const double s_cm = kappa * 1e-2;
			INFO(s.grams << " g/kg at " << celsius << " C: " << s_cm << " S/cm against " << measured);
			// the demal 1 standard is 1 mol/L, past the model's range (declared, GAME_SPEC §20): 1.5 % low at 25 C, 2.7 %
			// at 18, 5.9 % at 0
			const double tolerance = s.grams < 50.0 ? 0.01 : celsius > 20.0 ? 0.02 : celsius > 10.0 ? 0.03 : 0.065;
			CHECK(std::abs(s_cm / measured - 1.0) < tolerance);
		}
	}
}

TEST_CASE("measured: acetic acid's molar conductivity is MacInnes and Shedlovsky's, its ionisation and its ions' together (Stage 65)") {
	// MacInnes and Shedlovsky 1932, J. Am. Chem. Soc. 54, 1429, as MacInnes 1939 Table 3-II gives it: equivalents per
	// litre and S cm^2/mol at 25 C (no carbon dioxide)
	using namespace builtin;
	against("acetic acid", acetic_acid_aq, 298.15,
			{ { 0.028014e-3, 210.38 }, { 0.15321e-3, 112.05 }, { 1.02831e-3, 48.146 }, { 2.41400e-3, 32.217 }, { 5.91153e-3, 20.962 }, { 12.829e-3, 14.375 },
					{ 50.000e-3, 7.358 }, { 52.303e-3, 7.202 } },
			0.02);
}

TEST_CASE("conductivity: pure water reads its own ions', 0.055 uS/cm at 25 C, and a solution conducts no more than Kohlrausch's sum (Stage 65)") {
	using namespace builtin;
	double kappa = 0.0, limit = 0.0;
	REQUIRE(conductivity(table(), solution(water, 0.0, 298.15), 298.15, kappa, &limit));
	MESSAGE("pure water: " << kappa * 1e4 << " uS/cm");
	// ASTM D1193's Type I water, 18.2 MOhm cm: 0.0550 uS/cm
	CHECK(kappa * 1e4 == doctest::Approx(0.0550).epsilon(0.03));
	for (const double c : { 0.001, 0.01, 0.1 }) {
		REQUIRE(conductivity(table(), solution(sodium_chloride_aq, c, 298.15), 298.15, kappa, &limit));
		CHECK(kappa > 0.0);
		CHECK(kappa < limit);
	}
	// no liquid, no conductivity
	CellState dry{ std::vector<int64_t>(static_cast<size_t>(table().species_count()), 0), 0, std::vector<int64_t>(static_cast<size_t>(table().species_count()), 0) };
	testing::put_amount(table(), dry, sodium_chloride, quanta(0.1));
	CHECK_FALSE(conductivity(table(), dry, 298.15, kappa));
}

TEST_CASE("invariant Q1: a solution's conductivity lies between nought and Kohlrausch's limit; it fires on one past it, below nought, or with no ion (Stage 65)") {
	using namespace builtin;
	// solutions of every kind the experiments make, at 0 to 80 C
	for (const double kelvin : { 273.15, 298.15, 353.15 }) {
		for (const auto &[solute, mol] : { std::pair{ hydrochloric_acid_aq, 0.1 }, std::pair{ sodium_hydroxide_aq, 0.05 }, std::pair{ acetic_acid_aq, 0.8 },
					 std::pair{ sodium_carbonate_aq, 0.05 }, std::pair{ ammonia_aq, 0.1 }, std::pair{ copper_sulfate_aq, 0.5 }, std::pair{ water, 0.0 } }) {
			const CellState c = solution(solute, mol, kelvin);
			double kappa = 0.0, limit = 0.0;
			REQUIRE(conductivity(table(), c, kelvin, kappa, &limit));
			INFO(table().species(solute).id << " at " << kelvin << " K: " << kappa << " S/m, its limit " << limit);
			CHECK(check_Q1(table(), c, kappa, limit).empty());
		}
	}
	// corrupted readings
	const CellState salt = solution(sodium_chloride_aq, 0.01, 298.15);
	double kappa = 0.0, limit = 0.0;
	REQUIRE(conductivity(table(), salt, 298.15, kappa, &limit));
	CHECK_FALSE(check_Q1(table(), salt, limit * 1.01, limit).empty());
	CHECK_FALSE(check_Q1(table(), salt, -kappa, limit).empty());
	CellState dry{ std::vector<int64_t>(static_cast<size_t>(table().species_count()), 0), 0, std::vector<int64_t>(static_cast<size_t>(table().species_count()), 0) };
	testing::put_amount(table(), dry, glass, quanta(1.0));
	CHECK(check_Q1(table(), dry, 0.0, 0.0).empty());
	CHECK_FALSE(check_Q1(table(), dry, 1e-6, 1e-3).empty());
}
