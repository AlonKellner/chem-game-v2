// Fresnel's reflectance with a complex index (GAME_SPEC §12.3, Stage 78): the core's, which the tests of both tracers
// and the scene's rows are held to.
#include "chem/builtin_ids.hpp"
#include "chem/optics.hpp"
#include <doctest/doctest.h>
#include <cmath>

using namespace chem;

namespace {
// the textbook's real Fresnel (Hecht, Optics, §4.6): for a clear medium onto a clear one, below the critical angle
double real_fresnel(double n1, double n2, double cos_i) {
	const double s = n1 / n2 * std::sqrt(1.0 - cos_i * cos_i), ct = std::sqrt(1.0 - s * s);
	const double rs = (n1 * cos_i - n2 * ct) / (n1 * cos_i + n2 * ct), rp = (n2 * cos_i - n1 * ct) / (n2 * cos_i + n1 * ct);
	return (rs * rs + rp * rp) / 2.0;
}
} // namespace

TEST_CASE("fresnel: a clear surface's reflectance is the textbook's, all of it past the critical angle (Stage 78)") {
	for (const double n2 : { 1.333, 1.47, 1.79 }) {
		for (double c = 0.02; c <= 1.0; c += 0.07) {
			CAPTURE(n2);
			CAPTURE(c);
			CHECK(optics::fresnel_reflectance(1.0, 0.0, n2, 0.0, c) == doctest::Approx(real_fresnel(1.0, n2, c)).epsilon(1e-12));
			CHECK(optics::fresnel_reflectance(1.0, 0.0, n2, 0.0, -c) == doctest::Approx(real_fresnel(1.0, n2, c)).epsilon(1e-12)); // either side
		}
	}
	// glass at 45 degrees: Rs 0.0920, Rp 0.0085 (n 1.5)
	CHECK(optics::fresnel_reflectance(1.0, 0.0, 1.5, 0.0, std::sqrt(0.5)) == doctest::Approx((0.092013 + 0.008466) / 2.0).epsilon(1e-4));
	// at Brewster's angle the p wave passes whole: R is the s wave's half
	const double brewster = 1.0 / std::sqrt(1.0 + 1.5 * 1.5);
	const double rs = (brewster - 1.5 * std::sqrt(1.0 - (1.0 - brewster * brewster) / 2.25)) / (brewster + 1.5 * std::sqrt(1.0 - (1.0 - brewster * brewster) / 2.25));
	CHECK(optics::fresnel_reflectance(1.0, 0.0, 1.5, 0.0, brewster) == doctest::Approx(rs * rs / 2.0).epsilon(1e-9));
	// from glass into air, past the critical angle (41.8 degrees): all of it
	CHECK(optics::fresnel_reflectance(1.5, 0.0, 1.0, 0.0, std::cos(50.0 * 3.14159265358979 / 180.0)) == doctest::Approx(1.0).epsilon(1e-12));
	CHECK(optics::fresnel_reflectance(1.5, 0.0, 1.0, 0.0, std::cos(30.0 * 3.14159265358979 / 180.0)) == doctest::Approx(real_fresnel(1.5, 1.0, std::cos(30.0 * 3.14159265358979 / 180.0))).epsilon(1e-12));
}

TEST_CASE("fresnel: a metal's reflectance at normal incidence is its index's, rises to all at grazing, and never exceeds it (Stage 78)") {
	const optics::Spectrum &n = optics::refractive_index(builtin::mercury), &k = optics::extinction_index(builtin::mercury);
	for (size_t i = 0; i < n.size(); i += 20) {
		CAPTURE(380 + i);
		CHECK(optics::fresnel_reflectance(1.0, 0.0, n[i], k[i], 1.0) == doctest::Approx(optics::normal_reflectance(n[i], k[i])).epsilon(1e-12));
		for (double c = 1.0; c > 0.0; c -= 0.05) {
			const double r = optics::fresnel_reflectance(1.0, 0.0, n[i], k[i], c);
			CHECK(r > 0.65); // (the p wave's dip at the principal angle, near 80 degrees: to 0.41 in the red, the mean 0.68)
			CHECK(r <= 1.0);
		}
		CHECK(optics::fresnel_reflectance(1.0, 0.0, n[i], k[i], 1e-6) > 0.9999);
		// under water a metal turns back less: the index step is smaller
		CHECK(optics::fresnel_reflectance(1.333, 0.0, n[i], k[i], 1.0) < optics::fresnel_reflectance(1.0, 0.0, n[i], k[i], 1.0));
	}
	// at 80 degrees in the red (774.9 nm: n 2.746, k 6.054) the s wave turns back 0.953 and the p wave 0.423 (an
	// independent evaluation, Python's complex arithmetic)
	CHECK(optics::fresnel_reflectance(1.0, 0.0, 2.74611, 6.05402, 0.2) == doctest::Approx((0.953 + 0.423) / 2.0).epsilon(2e-3));
	// Born and Wolf's normal reflectance from a medium n1: ((n - n1)^2 + k^2) / ((n + n1)^2 + k^2)
	CHECK(optics::fresnel_reflectance(1.333, 0.0, 1.6, 4.75, 1.0) == doctest::Approx((0.267 * 0.267 + 4.75 * 4.75) / (2.933 * 2.933 + 4.75 * 4.75)).epsilon(1e-12));
}
