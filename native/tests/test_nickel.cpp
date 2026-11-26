// Nickel sulfate in solution (Stage 47): the green of the two coloured liquids a column can hold apart. Its data
// held to its sources at their conditions (docs/DATA_SOURCES.md, "Nickel sulfate (Stage 47)").
#include "chem/builtin_ids.hpp"
#include "chem/optics.hpp"
#include "chem/thermo.hpp"

#include <doctest/doctest.h>

#include <cmath>
#include <vector>

using namespace chem;

namespace {

int64_t quanta(double n) {
	bool ok = false;
	const Amount a = amount_from_mol(n, ok);
	REQUIRE(ok);
	return a.quanta;
}

CellState kilogram_with(double molal) {
	CellState c{ std::vector<int64_t>(static_cast<size_t>(builtin_species().species_count()), 0), 0,
		std::vector<int64_t>(static_cast<size_t>(builtin_species().species_count()), 0) };
	c.amount[static_cast<size_t>(builtin::water.index)] = quanta(1.0 / 0.018015);
	put_amount(builtin_species(), c, builtin::nickel_sulfate_aq, quanta(molal)); // as its ions (Stage 63)
	return c;
}

} // namespace

TEST_CASE("measured: nickel sulfate's mean activity coefficient is the CRC's, through Pitzer and Mayorga's parameters (Stage 47)") {
	// the CRC's 25 C values (as a Tsinghua table reproduces them): 0.150, 0.105, 0.063, 0.043 at 0.1, 0.2, 0.5, 1.0;
	// Pitzer and Mayorga's own fit runs about 3 % above them. a = (m gamma)^2
	for (const auto [m, gamma] : { std::pair{ 0.1, 0.150 }, std::pair{ 0.2, 0.105 }, std::pair{ 0.5, 0.063 }, std::pair{ 1.0, 0.043 } }) {
		const double model = std::sqrt(activity(builtin_species(), kilogram_with(m), builtin::nickel_sulfate_aq)) / m;
		INFO(m << " mol/kg: " << model << " against " << gamma);
		CHECK(std::abs(model / gamma - 1.0) < 0.05);
	}
}

TEST_CASE("measured: a mol/kg of nickel sulfate is as dense as Krumgalz, Pogorelsky and Pitzer give it (Stage 47)") {
	// JPCRD 25, 663 (1996), Table 5: V0 -15.22 cm3/mol and its Pitzer volume terms: 1.1504 g/cm3 at 1 mol/kg (25 C)
	double d = 0.0;
	REQUIRE(liquid_density(builtin_species(), kilogram_with(1.0), 298.15, d));
	CHECK(d / 1000.0 == doctest::Approx(1.1504).epsilon(0.003));
}

TEST_CASE("measured: nickel sulfate's absorption peaks are Shigematsu's, and its 394 nm Burke's in sulfate (Stage 47)") {
	// Shigematsu et al. 1960, Table 1: 394 nm 4.72, 650 nm 1.80, 718 nm 2.06 L/mol/cm (Ni2+ in water, 0.05 M); Burke,
	// Deardorff and Menis 1972: 5.15 at 394 nm in sulfate, 25 C. The model's decadic molar absorptivity there
	const optics::Spectrum &s = optics::spectrum(builtin::nickel_ion);
	const auto at = [&](int nm) { return s[static_cast<size_t>(nm - 380)]; };
	CHECK(at(394) == doctest::Approx(4.72).epsilon(0.03));
	CHECK(at(650) == doctest::Approx(1.80).epsilon(0.05));
	CHECK(at(718) == doctest::Approx(2.06).epsilon(0.05));
	CHECK(std::abs(at(394) / 5.15 - 1.0) < 0.1);
	CHECK(at(520) < 0.1); // its window, green
}
