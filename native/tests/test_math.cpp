// The core's own exp and log (chem/math.hpp) against the C library's: the
// tests may call the library, the core may not (ctest core_no_libm).
#include "chem/math.hpp"
#include "rng.hpp"

#include <doctest/doctest.h>

#include <bit>
#include <cmath>
#include <cstdint>
#include <limits>

namespace {

// the distance between two finite doubles of the same sign, in units in the last place
int64_t ulps(double a, double b) {
	const auto ia = static_cast<int64_t>(std::bit_cast<uint64_t>(a < 0 ? -a : a));
	const auto ib = static_cast<int64_t>(std::bit_cast<uint64_t>(b < 0 ? -b : b));
	return ia > ib ? ia - ib : ib - ia;
}

double uniform(Rng &rng, double lo, double hi) {
	return lo + (hi - lo) * (static_cast<double>(rng.next() >> 11) * 0x1p-53);
}

} // namespace

TEST_CASE("math: exp agrees with the C library within one unit in the last place (Stage 2)") {
	Rng rng(2);
	int64_t worst = 0;
	for (int i = 0; i < 400000; ++i) {
		const double x = i % 4 == 0 ? uniform(rng, -700.0, 700.0) : i % 4 == 1 ? uniform(rng, -40.0, 40.0) : i % 4 == 2 ? uniform(rng, -1.0, 1.0) : uniform(rng, -1e-6, 1e-6);
		const int64_t d = ulps(chem::exp(x), std::exp(x));
		worst = d > worst ? d : worst;
	}
	CHECK(worst <= 1);
	CHECK(chem::exp(0.0) == 1.0);
	CHECK(chem::exp(1.0) == doctest::Approx(2.718281828459045).epsilon(1e-15));
	CHECK(chem::exp(710.0) == std::numeric_limits<double>::infinity());
	CHECK(chem::exp(-746.0) == 0.0);
	CHECK(chem::exp(-740.0) > 0.0); // a subnormal result
	CHECK(std::isnan(chem::exp(std::numeric_limits<double>::quiet_NaN())));
}

TEST_CASE("math: log agrees with the C library within one unit in the last place (Stage 2)") {
	Rng rng(3);
	int64_t worst = 0;
	for (int i = 0; i < 400000; ++i) {
		double x;
		if (i % 3 == 0) {
			x = std::bit_cast<double>(rng.next() & 0x7fefffffffffffffull); // any positive finite double
			if (x == 0.0) {
				continue;
			}
		} else if (i % 3 == 1) {
			x = uniform(rng, 0.5, 2.0);
		} else {
			x = uniform(rng, 1e-3, 1e4);
		}
		const int64_t d = ulps(chem::log(x), std::log(x));
		worst = d > worst ? d : worst;
	}
	CHECK(worst <= 1);
	CHECK(chem::log(1.0) == 0.0);
	CHECK(chem::log(2.0) == doctest::Approx(0.6931471805599453).epsilon(1e-15));
	CHECK(chem::log(0.0) == -std::numeric_limits<double>::infinity());
	CHECK(std::isnan(chem::log(-1.0)));
	CHECK(chem::log(std::numeric_limits<double>::infinity()) == std::numeric_limits<double>::infinity());
	CHECK(chem::log(5e-324) == doctest::Approx(std::log(5e-324)).epsilon(1e-15)); // the smallest subnormal
}

TEST_CASE("math: the values are pinned, bit for bit, on every platform (Stage 2)") {
	// The replays' bit-identity (GAME_SPEC §3) rests on these being the same
	// everywhere. The bits are this implementation's (within one unit in the
	// last place of the true value, not always the nearest: exp(1) is one above).
	const auto bits = [](double v) { return std::bit_cast<uint64_t>(v); };
	CHECK(bits(chem::exp(1.0)) == 0x4005bf0a8b14576aull);
	CHECK(bits(chem::exp(-0.5)) == 0x3fe368b2fc6f960aull);
	CHECK(bits(chem::exp(700.0)) == 0x7f0d945df4f8ec8eull);
	CHECK(bits(chem::exp(-700.0)) == 0x00d14f2b0fb9307full);
	CHECK(bits(chem::exp(1e-9)) == 0x3ff000000044b830ull);
	CHECK(bits(chem::log(10.0)) == 0x40026bb1bbb55516ull);
	CHECK(bits(chem::log(353.15 / 298.15)) == 0x3fc5ab7f4ad5289bull);
	CHECK(bits(chem::log(1e-300)) == 0xc085963447f87fb5ull);
	CHECK(bits(chem::log(0.999999)) == 0xbeb0c6f82d74d230ull);
}
