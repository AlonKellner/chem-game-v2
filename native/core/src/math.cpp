#include "chem/math.hpp"

#include <bit>
#include <cstdint>
#include <limits>

// The algorithms and constants of fdlibm's e_exp.c and e_log.c (Sun
// Microsystems, 1993; freely distributable), written without the C library.
namespace chem {

namespace {

constexpr double LN2_HI = 6.93147180369123816490e-01; // the high 33 bits of ln 2
constexpr double LN2_LO = 1.90821492927058770002e-10; // ln 2 - LN2_HI
constexpr double INV_LN2 = 1.44269504088896338700e+00;

// y * 2^k
double scale(double y, int k) {
	const auto pow2 = [](int e) { return std::bit_cast<double>(static_cast<uint64_t>(e + 1023) << 52); }; // -1022 <= e <= 1023
	if (k > 1023) {
		return y * pow2(1023) * pow2(k - 1023 > 1023 ? 1023 : k - 1023);
	}
	if (k < -1022) {
		return y * pow2(-1022) * pow2(k + 1022 < -1022 ? -1022 : k + 1022);
	}
	return y * pow2(k);
}

} // namespace

double exp(double x) {
	constexpr double P1 = 1.66666666666666019037e-01;
	constexpr double P2 = -2.77777777770155933842e-03;
	constexpr double P3 = 6.61375632143793436117e-05;
	constexpr double P4 = -1.65339022054652515390e-06;
	constexpr double P5 = 4.13813679705723846039e-08;
	if (x != x) {
		return x;
	}
	if (x > 7.09782712893383973096e+02) {
		return std::numeric_limits<double>::infinity();
	}
	if (x < -7.45133219101941108420e+02) {
		return 0.0;
	}
	// x = k ln 2 + r, |r| <= ln 2 / 2
	const int k = static_cast<int>(x * INV_LN2 + (x < 0.0 ? -0.5 : 0.5));
	const double hi = x - static_cast<double>(k) * LN2_HI;
	const double lo = static_cast<double>(k) * LN2_LO;
	const double r = hi - lo;
	const double t = r * r;
	const double c = r - t * (P1 + t * (P2 + t * (P3 + t * (P4 + t * P5))));
	const double y = 1.0 - ((lo - (r * c) / (2.0 - c)) - hi);
	return k == 0 ? y : scale(y, k);
}

double log(double x) {
	constexpr double LG1 = 6.666666666666735130e-01;
	constexpr double LG2 = 3.999999999940941908e-01;
	constexpr double LG3 = 2.857142874366239149e-01;
	constexpr double LG4 = 2.222219843214978396e-01;
	constexpr double LG5 = 1.818357216161805012e-01;
	constexpr double LG6 = 1.531383769920937332e-01;
	constexpr double LG7 = 1.479819860511658591e-01;
	if (x != x) {
		return x;
	}
	if (x < 0.0) {
		return std::numeric_limits<double>::quiet_NaN();
	}
	if (x == 0.0) {
		return -std::numeric_limits<double>::infinity();
	}
	if (x == std::numeric_limits<double>::infinity()) {
		return x;
	}
	int k = 0;
	uint64_t bits = std::bit_cast<uint64_t>(x);
	if ((bits >> 52) == 0) { // subnormal: scale up by 2^54
		x *= 18014398509481984.0;
		k = -54;
		bits = std::bit_cast<uint64_t>(x);
	}
	// x = 2^k * m, sqrt(2)/2 <= m < sqrt(2)
	k += static_cast<int>(bits >> 52) - 1023;
	const uint64_t mantissa = bits & 0x000fffffffffffffull;
	double m = std::bit_cast<double>(mantissa | 0x3ff0000000000000ull);
	if (mantissa >= 0x0006a09e667f3bcdull) { // m >= sqrt(2)
		m *= 0.5;
		k += 1;
	}
	const double f = m - 1.0;
	const double s = f / (2.0 + f);
	const double z = s * s;
	const double w = z * z;
	const double t1 = w * (LG2 + w * (LG4 + w * LG6));
	const double t2 = z * (LG1 + w * (LG3 + w * (LG5 + w * LG7)));
	const double r = t2 + t1;
	const double hfsq = 0.5 * f * f;
	const double dk = static_cast<double>(k);
	return dk * LN2_HI - ((hfsq - (s * (hfsq + r) + dk * LN2_LO)) - f);
}

double cbrt(double x) {
	return x > 0.0 ? exp(log(x) / 3.0) : 0.0;
}

double pow(double x, double p) {
	return x > 0.0 ? exp(p * log(x)) : 0.0;
}

} // namespace chem
