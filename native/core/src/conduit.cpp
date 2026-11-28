// (Stage 89, GAME_SPEC §6, Flow between vessels) A conduit's flow law: chem/conduit.hpp.
#include "chem/conduit.hpp"

#include "chem/math.hpp"
#include "chem/thermo.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace chem {
namespace {

constexpr double PI = 3.14159265358979323846;

double area(const Piece &p) {
	return 0.25 * PI * p.bore * p.bore;
}

// x^n for a whole n >= 0, by squaring
double power(double x, int n) {
	double out = 1.0;
	for (double square = x; n > 0; n >>= 1, square *= square) {
		if ((n & 1) != 0) {
			out *= square;
		}
	}
	return out;
}

// (a^n + b^n)^(1/n) for a, b >= 0: the larger, a little more where they are near
double joined(double a, double b, double n) {
	const double high = std::max(a, b), low = std::min(a, b);
	if (!(high > 0.0)) {
		return 0.0;
	}
	const double ratio = low / high;
	return ratio < 1e-3 ? high : high * pow(1.0 + pow(ratio, n), 1.0 / n);
}

} // namespace

bool good(const Conduit &conduit) {
	if (conduit.count < 0 || conduit.count > MAX_PIECES) {
		return false;
	}
	for (int i = 0; i < conduit.count; ++i) {
		const Piece &p = conduit.pieces[static_cast<size_t>(i)];
		if (!(p.bore > 0.0) || !(p.length > 0.0) || p.bore - p.bore != 0.0 || p.length - p.length != 0.0) {
			return false;
		}
	}
	return true;
}

double friction_factor(double reynolds) {
	const FlowLaw &law = builtin_flow();
	if (!(reynolds > 0.0)) {
		return std::numeric_limits<double>::infinity();
	}
	const double laminar = law.laminar * law.laminar / reynolds; // 64 / Re
	if (reynolds < 100.0) {
		return laminar; // (Churchill's other terms are nothing here: under 1e-13 of it)
	}
	const int n = static_cast<int>(law.power), join = static_cast<int>(law.join);
	const double a = power(law.log * log(1.0 / pow(law.smooth / reynolds, law.smooth_power)), n);
	const double b = power(law.transition / reynolds, n);
	const double sum = a + b; // (A + B)^(-3/2) below
	return law.laminar * pow(power(law.laminar / reynolds, join) + 1.0 / (sum * std::sqrt(sum)), 1.0 / law.join);
}

double entrance_drop(double x) {
	const FlowLaw &law = builtin_flow();
	return x > 0.0 ? 4.0 * law.developed * x + entrance_excess(x) : 0.0;
}

double entrance_excess(double x) {
	const FlowLaw &law = builtin_flow();
	if (!(x > 0.0)) {
		return 0.0;
	}
	const double thin = 4.0 * law.entrance * std::sqrt(x); // the boundary layer's: 13.76 sqrt(x)
	const double developed = 4.0 * law.developed * x; // 64 x
	const double short_share = law.c / (x * x + law.c); // (c / x^2) / (1 + c / x^2)
	return (thin - developed) * short_share + law.excess * (1.0 - short_share);
}

double conduit_drive(const Conduit &conduit, double rate, double viscosity, bool back) {
	if (conduit.count <= 0 || !(rate > 0.0) || !(viscosity > 0.0)) {
		return 0.0;
	}
	const FlowLaw &law = builtin_flow();
	const int n = conduit.count;
	const auto piece = [&](int i) -> const Piece & { return conduit.pieces[static_cast<size_t>(back ? n - 1 - i : i)]; };
	double sum = 0.0;
	for (int i = 0; i < n; ++i) {
		const Piece &p = piece(i);
		const double a = area(p), head = rate * rate / (2.0 * a * a);
		const double reynolds = rate * p.bore / (a * viscosity), slender = p.length / p.bore;
		// entered through a contraction (from a vessel: all of it), left through an expansion (into a vessel: all)
		const double before = i > 0 ? area(piece(i - 1)) : std::numeric_limits<double>::infinity();
		const double after = i + 1 < n ? area(piece(i + 1)) : std::numeric_limits<double>::infinity();
		const double contraction = before > a ? law.contraction * (1.0 - a / before) : 0.0;
		const double widening = after > a ? (1.0 - a / after) * (1.0 - a / after) : 0.0;
		// Poiseuille's drop, and over it the entrance: attached (Shah's excess) or separated (the contraction's loss, and
		// what the friction has over Poiseuille's once it is turbulent)
		const double poiseuille = law.laminar * law.laminar / reynolds;
		const double attached = entrance_excess(slender / reynolds);
		const double separated = contraction + std::max(friction_factor(reynolds) - poiseuille, 0.0) * slender;
		sum += head * (poiseuille * slender + joined(attached, separated, law.join) + widening);
	}
	return sum;
}

double conduit_rate(const Conduit &conduit, double drive, double viscosity, bool back, double give) {
	if (conduit.count <= 0 || !(drive > 0.0) || !(viscosity > 0.0) || drive - drive != 0.0 || !(give >= 0.0)) {
		return 0.0;
	}
	const auto takes = [&](double rate) { return conduit_drive(conduit, rate, viscosity, back) + give * rate; };
	// no more than Poiseuille's through every piece (the law's least drop at a rate: 32 m L mu / (A D^2) a piece)
	double poiseuille = 0.0;
	for (int i = 0; i < conduit.count; ++i) {
		const Piece &p = conduit.pieces[static_cast<size_t>(i)];
		poiseuille += 32.0 * p.length * viscosity / (area(p) * p.bore * p.bore);
	}
	double hi = drive / (poiseuille + give), f_hi = takes(hi) - drive;
	if (!(f_hi > 0.0)) {
		return hi;
	}
	double lo = hi, f_lo = f_hi;
	for (int i = 0; i < 400 && f_lo > 0.0; ++i) {
		hi = lo;
		f_hi = f_lo;
		lo *= 0.25;
		f_lo = takes(lo) - drive;
	}
	if (f_lo > 0.0) {
		return 0.0;
	}
	// regula falsi (Illinois') between them
	int side = 0;
	for (int i = 0; i < 200 && hi - lo > 1e-14 * hi; ++i) {
		double middle = (lo * f_hi - hi * f_lo) / (f_hi - f_lo);
		if (!(middle > lo && middle < hi)) {
			middle = 0.5 * (lo + hi);
		}
		const double f = takes(middle) - drive;
		if (f == 0.0) {
			return middle;
		}
		if (f > 0.0) {
			hi = middle;
			f_hi = f;
			f_lo *= side == 1 ? 0.5 : 1.0;
			side = 1;
		} else {
			lo = middle;
			f_lo = f;
			f_hi *= side == -1 ? 0.5 : 1.0;
			side = -1;
		}
	}
	return lo; // (from below: never more than the law passes)
}

double liquid_rate(const Conduit &conduit, double from, double to, double density, double viscosity, bool back, double give) {
	if (!(from > to) || !(density > 0.0)) {
		return 0.0;
	}
	return conduit_rate(conduit, density * (from - to), viscosity, back, give);
}

double gas_rate(const Conduit &conduit, double from, double to, double kelvin, double molar_mass, double gamma, double viscosity, bool back) {
	if (!(from > to) || !(to >= 0.0) || !(kelvin > 0.0) || !(molar_mass > 0.0) || !(gamma > 1.0) || conduit.count <= 0) {
		return 0.0;
	}
	const double rate = conduit_rate(conduit, molar_mass * (from * from - to * to) / (2.0 * GAS_CONSTANT * kelvin), viscosity, back);
	double narrowest = std::numeric_limits<double>::infinity();
	for (int i = 0; i < conduit.count; ++i) {
		narrowest = std::min(narrowest, area(conduit.pieces[static_cast<size_t>(i)]));
	}
	const double choked = narrowest * from * std::sqrt(gamma * molar_mass / (GAS_CONSTANT * kelvin)) * pow(2.0 / (gamma + 1.0), (gamma + 1.0) / (2.0 * (gamma - 1.0)));
	return std::min(rate, choked);
}

} // namespace chem
