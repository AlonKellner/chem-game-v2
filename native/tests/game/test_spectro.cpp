// A spectrophotometer (GAME_SPEC §13, Stage 55): Beer and Lambert's law where they hold, a real instrument's
// departures where they do not.
#include "lab/spectro.hpp"

#include "chem/builtin_ids.hpp"

#include <doctest/doctest.h>

#include <cmath>

namespace {

using namespace lab::spectro;

Sample copper(double molar, double path_cm = 1.0) {
	return Sample{ { { chem::builtin::water, 55.3 }, { chem::builtin::copper_sulfate_aq, molar } }, path_cm };
}

double read(double nm, const Sample &s, double blank_nm, const Sample &blank) {
	const Model &m = model();
	const std::optional<double> a = reading(m, signal(m, nm, s), signal(m, blank_nm, blank));
	REQUIRE(a);
	return *a;
}

// what Beer and Lambert say: epsilon c l at the wavelength, the solvent's own taken off by the blank
double beer(double nm, double molar) {
	const Sample s{ { { chem::builtin::copper_sulfate_aq, molar } }, 1.0 };
	return absorbance_at(s, absorbance(s), nm);
}

} // namespace

TEST_CASE("spectro: a solution reads Beer and Lambert's epsilon c l, in a line through zero (Stage 55)") {
	const Sample water = copper(0.0);
	// the solvent blanked: copper sulfate at 750 nm, epsilon 10.2 L/mol/cm (NBS's)
	CHECK(beer(750, 1.0) == doctest::Approx(0.817 / 0.07986).epsilon(0.002));
	double sx = 0, sy = 0, sxx = 0, sxy = 0;
	const double cs[] = { 0.01, 0.02, 0.04, 0.06, 0.08, 0.10 };
	for (const double c : cs) {
		const double a = read(750, copper(c), 750, water);
		CHECK(a == doctest::Approx(beer(750, c)).epsilon(0.005)); // its band and stray light hardly touch it here
		sx += c;
		sy += a;
		sxx += c * c;
		sxy += c * a;
	}
	const double n = 6, slope = (n * sxy - sx * sy) / (n * sxx - sx * sx), intercept = (sy - slope * sx) / n;
	CHECK(slope == doctest::Approx(beer(750, 1.0)).epsilon(0.005));
	CHECK(std::fabs(intercept) < 0.003);
	// twice the path, twice the absorbance
	CHECK(read(750, copper(0.05, 2.0), 750, copper(0.0, 2.0)) == doctest::Approx(2.0 * read(750, copper(0.05), 750, water)).epsilon(0.01));
	// the blank itself reads zero
	CHECK(read(750, water, 750, water) == 0.0);
}

TEST_CASE("spectro: stray light flattens a high absorbance; a wavelength changed after its blank reads the lamp's slope (Stage 55)") {
	const Model &m = model();
	const Sample water = copper(0.0);
	// at an ideal 0.5 it reads within its resolution's few steps; at an ideal 2.5, noticeably low
	const double low = read(750, copper(0.5 / beer(750, 1.0)), 750, water);
	CHECK(low == doctest::Approx(0.5).epsilon(0.008));
	const double high = read(750, copper(2.5 / beer(750, 1.0)), 750, water);
	MESSAGE("an ideal 0.5 reads " << low << ", an ideal 2.5 reads " << high);
	CHECK(high < 2.5 - 0.02);
	CHECK(high > 2.0);
	// far past it, no reading: "OL"
	CHECK_FALSE(reading(m, signal(m, 750, copper(1.0)), signal(m, 750, water)));
	CHECK(display_text(std::nullopt, true, true) == "OL");
	CHECK(display_text(0.532, true, false) == "0.532");
	CHECK(display_text(std::nullopt, false, false) == "----");
	CHECK(wavelength_text(750.0) == "750");
	// blanked at 750 and set to 600 without a new blank: water reads the lamp's fall from 750 to 600 nm
	const double slope = read(600, water, 750, water);
	MESSAGE("water blanked at 750 nm, read at 600: " << slope);
	CHECK(slope > 0.05);
	CHECK(slope == doctest::Approx(-std::log10(chem::optics::planck(600, m.lamp_k) / chem::optics::planck(750, m.lamp_k))).epsilon(0.05));
	// a steep side of a band read through the slit's band: within a little of the ideal, not exactly it
	const double steep = read(640, copper(0.1), 640, water);
	CHECK(steep == doctest::Approx(beer(640, 0.1)).epsilon(0.02));
}
