#include "chem/math.hpp"

#include <cmath>
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

// (Stage 95) The sine, the cosine and the arctangent, from +, -, *, / alone (the fdlibm algorithms: a kernel on
// [-pi/4, pi/4] after Cody and Waite's reduction by pi/2 in parts; atan by its four breakpoints): under 1 ulp, the
// same everywhere. A path's arcs (chem/path.hpp) are all that ask.


namespace {

// sin(x + y) for |x| <= pi/4, y the tail of x
double kernel_sin(double x, double y, bool tail) {
	constexpr double S1 = -1.66666666666666324348e-01, S2 = 8.33333333332248946124e-03, S3 = -1.98412698298579493134e-04,
					 S4 = 2.75573137070700676789e-06, S5 = -2.50507602534068634195e-08, S6 = 1.58969099521155010221e-10;
	const double z = x * x, v = z * x;
	const double r = S2 + z * (S3 + z * (S4 + z * (S5 + z * S6)));
	if (!tail) {
		return x + v * (S1 + z * r);
	}
	return x - ((z * (0.5 * y - v * r) - y) - v * S1);
}

// cos(x + y) for |x| <= pi/4
double kernel_cos(double x, double y) {
	constexpr double C1 = 4.16666666666666019037e-02, C2 = -1.38888888888741095749e-03, C3 = 2.48015872894767294178e-05,
					 C4 = -2.75573143513906633035e-07, C5 = 2.08757232129817482790e-09, C6 = -1.13596475577881948265e-11;
	const double z = x * x;
	const double r = z * (C1 + z * (C2 + z * (C3 + z * (C4 + z * (C5 + z * C6)))));
	const double ax = std::fabs(x);
	if (ax < 0.3) {
		return 1.0 - (0.5 * z - (z * r - x * y));
	}
	// 1 - qx exact (qx a few bits of x / 4), so the leading terms cancel without loss
	const double qx = ax > 0.78125 ? 0.28125 : std::floor(ax * 0.25 * 1048576.0) / 1048576.0;
	const double hz = 0.5 * z - qx, a = 1.0 - qx;
	return a - (hz - (z * r - x * y));
}

// x = n pi/2 + (y0 + y1), |y0 + y1| <= pi/4; n mod 4 returned. |x| under 2^19 pi/2.
int reduce(double x, double &y0, double &y1) {
	constexpr double INV_PIO2 = 6.36619772367581382433e-01;
	constexpr double PIO2_1 = 1.57079632673412561417e+00; // the first 33 bits of pi/2
	constexpr double PIO2_2 = 6.07710050630396597660e-11; // the next 33
	constexpr double PIO2_2T = 2.02226624879595063154e-21; // pi/2 - (PIO2_1 + PIO2_2)
	const double fn = std::floor(x * INV_PIO2 + 0.5);
	const double t = x - fn * PIO2_1; // (exact: fn has under 20 bits)
	double w = fn * PIO2_2;
	const double r = t - w;
	w = fn * PIO2_2T - ((t - r) - w);
	y0 = r - w;
	y1 = (r - y0) - w;
	const double quarter = fn - 4.0 * std::floor(fn * 0.25);
	return static_cast<int>(quarter);
}

constexpr double TRIG_MOST = 8.0e5; // (under 2^19 pi/2)

} // namespace

double sin(double x) {
	if (!(std::fabs(x) <= TRIG_MOST)) {
		return std::numeric_limits<double>::quiet_NaN();
	}
	if (std::fabs(x) <= 0.78539816339744830962) {
		return kernel_sin(x, 0.0, false);
	}
	double y0 = 0.0, y1 = 0.0;
	switch (reduce(x, y0, y1)) {
		case 0:
			return kernel_sin(y0, y1, true);
		case 1:
			return kernel_cos(y0, y1);
		case 2:
			return -kernel_sin(y0, y1, true);
		default:
			return -kernel_cos(y0, y1);
	}
}

double cos(double x) {
	if (!(std::fabs(x) <= TRIG_MOST)) {
		return std::numeric_limits<double>::quiet_NaN();
	}
	if (std::fabs(x) <= 0.78539816339744830962) {
		return kernel_cos(x, 0.0);
	}
	double y0 = 0.0, y1 = 0.0;
	switch (reduce(x, y0, y1)) {
		case 0:
			return kernel_cos(y0, y1);
		case 1:
			return -kernel_sin(y0, y1, true);
		case 2:
			return -kernel_cos(y0, y1);
		default:
			return kernel_sin(y0, y1, true);
	}
}

double atan(double x) {
	constexpr double HI[4] = { 4.63647609000806093515e-01, 7.85398163397448278999e-01, 9.82793723247329054082e-01, 1.57079632679489655800e+00 };
	constexpr double LO[4] = { 2.26987774529616870924e-17, 3.06161699786838301793e-17, 1.39033110312309984516e-17, 6.12323399573676603587e-17 };
	constexpr double T[11] = { 3.33333333333329318027e-01, -1.99999999998764832476e-01, 1.42857142725034663711e-01, -1.11111104054623557880e-01,
		9.09088713343650656196e-02, -7.69187620504482999495e-02, 6.66107313738753120669e-02, -5.83357013379057348645e-02,
		4.97687799461593236017e-02, -3.65315727442169155270e-02, 1.62858201153657823623e-02 };
	if (x != x) {
		return x;
	}
	const bool negative = x < 0.0;
	double a = std::fabs(x);
	if (a >= 7.378697629483821e19) { // 2^66
		return negative ? -(HI[3] + LO[3]) : HI[3] + LO[3];
	}
	int id = -1;
	if (a >= 0.4375) {
		if (a < 1.1875) {
			if (a < 0.6875) {
				id = 0;
				a = (2.0 * a - 1.0) / (2.0 + a);
			} else {
				id = 1;
				a = (a - 1.0) / (a + 1.0);
			}
		} else if (a < 2.4375) {
			id = 2;
			a = (a - 1.5) / (1.0 + 1.5 * a);
		} else {
			id = 3;
			a = -1.0 / a;
		}
	} else if (a < 3.7252902984619141e-09) { // 2^-28
		return x;
	}
	const double z = a * a, w = z * z;
	const double s1 = z * (T[0] + w * (T[2] + w * (T[4] + w * (T[6] + w * (T[8] + w * T[10])))));
	const double s2 = w * (T[1] + w * (T[3] + w * (T[5] + w * (T[7] + w * T[9]))));
	if (id < 0) {
		const double r = a - a * (s1 + s2);
		return negative ? -r : r;
	}
	const double r = HI[id] - ((a * (s1 + s2) - LO[id]) - a);
	return negative ? -r : r;
}

double atan2(double y, double x) {
	constexpr double PI = 3.14159265358979311600e+00, PI_LO = 1.2246467991473531772e-16;
	if (x != x || y != y) {
		return x + y;
	}
	if (y == 0.0) {
		return x < 0.0 || (x == 0.0 && std::signbit(x)) ? (std::signbit(y) ? -PI : PI) : y;
	}
	if (x == 0.0) {
		return y < 0.0 ? -0.5 * PI : 0.5 * PI;
	}
	const double z = atan(std::fabs(y / x));
	if (x > 0.0) {
		return y < 0.0 ? -z : z;
	}
	const double r = PI - (z - PI_LO);
	return y < 0.0 ? -r : r;
}

} // namespace chem
