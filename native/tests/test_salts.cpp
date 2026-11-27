// The salts (GAME_SPEC §9, §10, Stage 64): each crystal saturates its solution at its measured solubility, read as
// the saturation index there (the crystallisation's affinity over RT ln 10, nought at saturation); potassium hydrogen
// phthalate, KHP, the acidimetric standard: its solubility, its pH standard, and its acid's constants.
#include "tables.hpp"
#include "chem/builtin_ids.hpp"
#include "chem/sim.hpp"
#include "chem/thermo.hpp"

#include <doctest/doctest.h>

#include <cmath>
#include <initializer_list>
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
	Cell() { put(builtin::water, 1.0 / 0.018015); }
	Cell &put(SpeciesId s, double mol) {
		testing::put_amount(builtin_species(), state, s, quanta(mol));
		return *this;
	}
};

const Reaction &rx_of(size_t r) {
	return builtin_reactions()[r];
}

// the saturation index of a crystallisation in a solution of `m` mol/kg of the salt (its retired unit) at T: the
// solution settled at its fast equilibria, the crystal's affinity to grow there over RT ln 10
double saturation_index(size_t crystallisation, SpeciesId unit, double m, double kelvin) {
	const CellState settled = testing::settled(Cell().put(unit, m).state, kelvin);
	bool defined = false;
	const double a = affinity(builtin_species(), settled, rx_of(crystallisation).terms(), kelvin, defined);
	REQUIRE(defined);
	return a / (GAS_CONSTANT * kelvin * std::log(10.0));
}

double ph_of(const Cell &c, double kelvin = 298.15) {
	double p = 0.0;
	REQUIRE(ph(builtin_species(), testing::settled(c.state, kelvin), kelvin, p));
	return p;
}

} // namespace

TEST_CASE("measured: each salt saturates its solution at its measured solubility (Stage 64)") {
	using namespace builtin;
	namespace rx = builtin::reaction;
	const struct {
		size_t crystallisation;
		SpeciesId unit;
		double celsius, m, tolerance;
		const char *source;
	} rows[] = {
		// potassium hydrogen phthalate, Hendrixson 1920 (Seidell 1940): 10.23 g in 100 g of the solution at 25 C, 12.67 at 35
		{ rx::potassium_hydrogen_phthalate_crystallization, potassium_hydrogen_phthalate_aq, 25.0, 0.558, 0.02, "Hendrixson 1920" },
		{ rx::potassium_hydrogen_phthalate_crystallization, potassium_hydrogen_phthalate_aq, 35.0, 0.710, 0.02, "Hendrixson 1920" },
		// IUPAC-NIST Solubility Data Series 47 (Cohen-Adad and Lorimer 1991), the evaluators' smoothed values
		{ rx::sodium_chloride_crystallization, sodium_chloride_aq, 25.0, 6.157, 0.05, "SDS 47" },
		{ rx::sodium_chloride_crystallization, sodium_chloride_aq, 80.0, 6.490, 0.1, "SDS 47 (Pitzer's 25 C parameters at 80 C)" },
		{ rx::potassium_chloride_crystallization, potassium_chloride_aq, 25.0, 4.769, 0.05, "SDS 47" },
		{ rx::potassium_chloride_crystallization, potassium_chloride_aq, 60.0, 6.139, 0.08, "SDS 47" },
		{ rx::ammonium_chloride_crystallization, ammonium_chloride_aq, 25.0, 7.393, 0.12, "SDS 47 (past Pitzer and Mayorga's 6 mol/kg)" },
		// Zhou et al. 2017 (ThermoML), and Seidell 1919 (Dibbits; Fedotieff)
		{ rx::sodium_bicarbonate_crystallization, sodium_bicarbonate_aq, 25.0, 1.2207, 0.05, "Zhou et al. 2017" },
		{ rx::sodium_bicarbonate_crystallization, sodium_bicarbonate_aq, 50.0, 1.720, 0.08, "Seidell 1919" },
		// Na2CO3: the decahydrate is the solid at 25 C (Wells and McAdam 1907; de Paepe 1911: 2.67 to 2.82 mol/kg)
		{ rx::sodium_carbonate_decahydrate_crystallization, sodium_carbonate_aq, 25.0, 2.79, 0.08, "Wells and McAdam 1907" },
		{ rx::sodium_carbonate_decahydrate_crystallization, sodium_carbonate_aq, 10.0, 1.179, 0.08, "Wells and McAdam 1907" },
	};
	for (const auto &row : rows) {
		const double si = saturation_index(row.crystallisation, row.unit, row.m, row.celsius + 273.15);
		MESSAGE(rx_of(row.crystallisation).id() << " at " << row.celsius << " C, " << row.m << " mol/kg (" << std::string(row.source) << "): saturation index " << si);
		CHECK(std::abs(si) < row.tolerance);
	}
	// the anhydrous carbonate and the anhydrous acetate dissolve past what the decahydrate and the trihydrate hold: at
	// 25 C a solution saturated in the decahydrate is under-saturated in the anhydrous salt (Green 1908: the anhydrous
	// acetate "unstable" below 58 C, 123.5 g in 100 g of water at 20 C)
	CHECK(saturation_index(rx::sodium_carbonate_crystallization, sodium_carbonate_aq, 2.79, 298.15) < 0.0);
	// (the anhydrous acetate's own solubility, 15 mol/kg, lies past the activity model's range: no gate)
}

TEST_CASE("measured: phthalic acid's constants are Hamer, Pinching and Acree's; 0.05 mol/kg KHP reads IUPAC's 4.005 (Stage 64)") {
	using namespace builtin;
	const auto log_k = [](std::initializer_list<std::pair<SpeciesId, int>> terms, double kelvin) {
		double g = 0.0;
		for (const auto &[s, nu] : terms) {
			const SpeciesDef &d = builtin_species().species(s);
			g += nu * (d.hf + d.cp * (kelvin - T0) - kelvin * (d.s0 + d.cp * std::log(kelvin / T0)));
		}
		return -g / (GAS_CONSTANT * kelvin * std::log(10.0));
	};
	// J. Res. NBS 35 (1945): log K1 = -561.57 / T + 1.2843 - 0.0078833 T (539) and pK2 = 2175.83 / T - 9.55075 + 0.025694 T (381)
	for (const double celsius : { 0.0, 25.0, 50.0 }) {
		const double t = celsius + 273.15;
		const double pk1 = -log_k({ { phthalic_acid_aq, -1 }, { hydrogen_phthalate_ion, 1 }, { hydrogen_ion, 1 } }, t);
		const double pk2 = -log_k({ { hydrogen_phthalate_ion, -1 }, { phthalate_ion, 1 }, { hydrogen_ion, 1 } }, t);
		const double source1 = -(-561.57 / t + 1.2843 - 0.0078833 * t), source2 = 2175.83 / t - 9.55075 + 0.025694 * t;
		INFO(celsius << " C: pK1 " << pk1 << " against " << source1 << ", pK2 " << pk2 << " against " << source2);
		CHECK(std::abs(pk1 - source1) < 0.01);
		CHECK(std::abs(pk2 - source2) < 0.01);
	}
	// Buck et al. 2002 (Pure Appl. Chem. 74, 2169), Table 2: 0.05 mol/kg KHP, 4.005 at 25 C, 4.000 at 0 and 20, 4.027 at 40
	for (const auto [celsius, value] : { std::pair{ 25.0, 4.005 }, std::pair{ 10.0, 3.997 }, std::pair{ 40.0, 4.027 } }) {
		const double p = ph_of(Cell().put(potassium_hydrogen_phthalate_aq, 0.05), celsius + 273.15);
		MESSAGE("KHP 0.05 mol/kg at " << celsius << " C: pH " << p << " against " << value);
		CHECK(std::abs(p - value) < 0.03);
	}
}
