// The tracer (GAME_SPEC §12.3, Stage 20).
#include "lab/optics_trace.hpp"

#include "chem/optics.hpp"

#include <algorithm>
#include <cmath>
#include <map>
#include <memory>
#include <mutex>
#include <numbers>

namespace lab::trace {

namespace {

double dot(const Vec3 &a, const Vec3 &b) {
	return a.x * b.x + a.y * b.y + a.z * b.z;
}

Vec3 along(const Vec3 &o, const Vec3 &u, double t) {
	return Vec3{ o.x + t * u.x, o.y + t * u.y, o.z + t * u.z };
}

bool within(const Patch &p, const Vec3 &at) {
	constexpr double SLACK = 1.0e-9;
	if (!(at.x >= p.lo.x - SLACK && at.x <= p.hi.x + SLACK && at.y >= p.lo.y - SLACK && at.y <= p.hi.y + SLACK && at.z >= p.lo.z - SLACK && at.z <= p.hi.z + SLACK)) {
		return false;
	}
	if (p.rho_lo > 0.0 || p.rho_hi < 1.0e299 || p.torus) {
		const double rho = std::sqrt(at.x * at.x + at.z * at.z);
		if (rho < p.rho_lo - SLACK || rho > p.rho_hi + SLACK) {
			return false;
		}
		if (p.torus) {
			// on its own arc of the circle, not the rest of it
			// how far round from the arc's start, in [0, 2 pi): within the arc's own sweep
			const double turn = 2.0 * std::numbers::pi;
			double d = std::fmod(std::atan2(at.y - p.cy, rho - p.major) - p.t_lo, turn);
			d = d < 0.0 ? d + turn : d;
			d = d > turn - 1.0e-9 ? d - turn : d;
			return d <= p.t_hi - p.t_lo + 1.0e-9;
		}
	}
	return true;
}

// where t along the ray keeps rho^2 = |o + t u|^2 (across) within [r0^2, r1^2]: up to two intervals
int rho_spans(const Vec3 &o, const Vec3 &u, double r0, double r1, double span[2][2]) {
	const double a = u.x * u.x + u.z * u.z, b = 2.0 * (o.x * u.x + o.z * u.z), c = o.x * o.x + o.z * o.z;
	if (a < 1.0e-18) {
		if (c < r0 * r0 || c > r1 * r1) {
			return 0;
		}
		span[0][0] = -1.0e300;
		span[0][1] = 1.0e300;
		return 1;
	}
	// rho^2(t) = a t^2 + b t + c: within the outer circle, and outside the inner one
	const double disc1 = b * b - 4.0 * a * (c - r1 * r1);
	if (disc1 <= 0.0) {
		return 0;
	}
	const double t1a = (-b - std::sqrt(disc1)) / (2.0 * a), t1b = (-b + std::sqrt(disc1)) / (2.0 * a);
	const double disc0 = b * b - 4.0 * a * (c - r0 * r0);
	if (r0 <= 0.0 || disc0 <= 0.0) {
		span[0][0] = t1a;
		span[0][1] = t1b;
		return 1;
	}
	const double t0a = (-b - std::sqrt(disc0)) / (2.0 * a), t0b = (-b + std::sqrt(disc0)) / (2.0 * a);
	span[0][0] = t1a;
	span[0][1] = t0a;
	span[1][0] = t0b;
	span[1][1] = t1b;
	return 2;
}

// a torus patch's nearest crossing after `after`, within its bounds: stepping along each span of the ray
// that is within its rings and its heights, halving where the quartic changes sign
double torus_hit(const Patch &p, const Vec3 &o, const Vec3 &u, double after) {
	double span[2][2];
	const int spans = rho_spans(o, u, std::max(0.0, p.rho_lo - 1.0e-9), p.rho_hi + 1.0e-9, span);
	for (int k = 0; k < spans; ++k) {
		double t0 = std::max(span[k][0], after), t1 = span[k][1];
		// and within its heights
		if (std::fabs(u.y) > 1.0e-18) {
			const double ya = (p.lo.y - 1.0e-9 - o.y) / u.y, yb = (p.hi.y + 1.0e-9 - o.y) / u.y;
			t0 = std::max(t0, std::min(ya, yb));
			t1 = std::min(t1, std::max(ya, yb));
		} else if (o.y < p.lo.y - 1.0e-9 || o.y > p.hi.y + 1.0e-9) {
			continue;
		}
		if (!(t1 > t0)) {
			continue;
		}
		// the quartic's coefficients along the ray from the span's start (no cancellation from a far origin):
		// S = rho^2 + (y - cy)^2 + major^2 - minor^2, P = S^2 - 4 major^2 rho^2
		const Vec3 s0 = along(o, u, t0);
		const double dy = s0.y - p.cy, m2 = p.major * p.major;
		const double a2 = u.x * u.x + u.z * u.z, a1 = 2.0 * (s0.x * u.x + s0.z * u.z), a0 = s0.x * s0.x + s0.z * s0.z;
		const double q2 = a2 + u.y * u.y, q1 = a1 + 2.0 * dy * u.y, q0 = a0 + dy * dy + m2 - p.minor * p.minor;
		const double c4 = q2 * q2, c3 = 2.0 * q2 * q1, c2 = q1 * q1 + 2.0 * q2 * q0 - 4.0 * m2 * a2, c1 = 2.0 * q1 * q0 - 4.0 * m2 * a1, c0 = q0 * q0 - 4.0 * m2 * a0;
		const auto f = [&](double tau) { return (((c4 * tau + c3) * tau + c2) * tau + c1) * tau + c0; };
		constexpr int STEPS = 48;
		const double length = t1 - t0;
		double ta = 0.0, fa = f(ta);
		for (int i = 1; i <= STEPS; ++i) {
			const double tb = length * i / STEPS, fb = f(tb);
			if ((fa < 0.0) != (fb < 0.0)) {
				double lo = ta, hi = tb;
				const bool rising = fb >= 0.0;
				while (hi - lo > 1.0e-13 * std::max(1.0, std::fabs(t0 + hi))) {
					const double mid = 0.5 * (lo + hi);
					((f(mid) >= 0.0) == rising ? hi : lo) = mid;
				}
				const double t = t0 + 0.5 * (lo + hi);
				if (t > after && within(p, along(o, u, t))) {
					return t;
				}
			}
			ta = tb;
			fa = fb;
		}
	}
	return -1.0;
}

} // namespace

namespace {

// what is at a place in a core: its liquid (CORE), the air over it (CORE_AIR), or its sediment (4)
constexpr int SEDIMENT = 4;
int medium_at(const Inside &in, const Vec3 &p) {
	const double level = in.up_x * p.x + in.up_y * p.y;
	// (how far across from a cone's middle, along the level's line: the room's up turned a quarter round)
	const auto in_cone = [&](double x, double y, double across, double deep, double narrowing) {
		if (!(narrowing >= 0.0) || !(across > 0.0) || !(deep > 0.0)) {
			return false;
		}
		const double q = ((p.x - x) * in.up_y - (p.y - y) * in.up_x) / across, w = p.z / deep;
		return q * q + w * w <= narrowing * narrowing;
	};
	if (in.solid && (level <= in.solid_level || (in.pile > 0.0 && level - in.solid_level <= in.pile && in_cone(in.pile_x, in.pile_y, in.pile_across, in.pile_deep, 1.0 - (level - in.solid_level) / in.pile)))) {
		return SEDIMENT;
	}
	if (in.liquid && level <= in.liquid_level &&
			!(in.vortex > 0.0 && in.liquid_level - level <= in.vortex && in_cone(in.vortex_x, in.vortex_y, in.vortex_across, in.vortex_deep, 1.0 - (in.liquid_level - level) / in.vortex))) {
		return CORE;
	}
	return CORE_AIR;
}

// the surfaces within a core that a ray from o along u may cross (up to six places along it), and at one of
// them the surface's normal: 0 the liquid's level, 1 the sediment's, 2 the vortex's cone, 3 the pile's
int inside_crossings(const Inside &in, const Vec3 &o, const Vec3 &u, double t[6], int which[6]) {
	int count = 0;
	const double level = in.up_x * o.x + in.up_y * o.y, rising = in.up_x * u.x + in.up_y * u.y;
	if (in.liquid && std::fabs(rising) > 1.0e-14) {
		t[count] = (in.liquid_level - level) / rising;
		which[count++] = 0;
	}
	if (in.solid && std::fabs(rising) > 1.0e-14) {
		t[count] = (in.solid_level - level) / rising;
		which[count++] = 1;
	}
	const auto cone = [&](int kind, double x, double y, double across, double deep, double h0, double h1) {
		if (!(across > 0.0) || !(deep > 0.0)) {
			return;
		}
		// q^2 + w^2 = h^2, each straight along the ray
		const double q0 = ((o.x - x) * in.up_y - (o.y - y) * in.up_x) / across, q1 = (u.x * in.up_y - u.y * in.up_x) / across, w0 = o.z / deep, w1 = u.z / deep;
		const double a = q1 * q1 + w1 * w1 - h1 * h1, b = 2.0 * (q0 * q1 + w0 * w1 - h0 * h1), c = q0 * q0 + w0 * w0 - h0 * h0;
		if (std::fabs(a) < 1.0e-14) {
			if (std::fabs(b) > 1.0e-14) {
				t[count] = -c / b;
				which[count++] = kind;
			}
			return;
		}
		const double disc = b * b - 4.0 * a * c;
		if (disc >= 0.0) {
			const double q = -0.5 * (b + (b < 0.0 ? -1.0 : 1.0) * std::sqrt(disc));
			t[count] = q / a;
			which[count++] = kind;
			if (q != 0.0) {
				t[count] = c / q;
				which[count++] = kind;
			}
		}
	};
	if (in.liquid && in.vortex > 0.0) {
		cone(2, in.vortex_x, in.vortex_y, in.vortex_across, in.vortex_deep, 1.0 - (in.liquid_level - level) / in.vortex, rising / in.vortex);
	}
	if (in.solid && in.pile > 0.0) {
		cone(3, in.pile_x, in.pile_y, in.pile_across, in.pile_deep, 1.0 - (level - in.solid_level) / in.pile, -rising / in.pile);
	}
	return count;
}

Vec3 inside_normal(const Inside &in, int which, const Vec3 &p) {
	if (which < 2) {
		return Vec3{ in.up_x, in.up_y, 0.0 };
	}
	const bool vortex = which == 2;
	const double x = vortex ? in.vortex_x : in.pile_x, y = vortex ? in.vortex_y : in.pile_y, across = vortex ? in.vortex_across : in.pile_across, deep = vortex ? in.vortex_deep : in.pile_deep;
	const double level = in.up_x * p.x + in.up_y * p.y;
	const double q = ((p.x - x) * in.up_y - (p.y - y) * in.up_x) / across, w = p.z / deep;
	const double h = vortex ? 1.0 - (in.liquid_level - level) / in.vortex : 1.0 - (level - in.solid_level) / in.pile, slope = vortex ? 1.0 / in.vortex : -1.0 / in.pile;
	// the gradient of q^2 + w^2 - h^2
	return Vec3{ 2.0 * q * in.up_y / across - 2.0 * h * slope * in.up_x, -2.0 * q * in.up_x / across - 2.0 * h * slope * in.up_y, 2.0 * w / deep };
}

} // namespace

Landing trace(const Shape &shape, Vec3 o, Vec3 u, double wall, const std::array<double, MAX_MEDIA> &index, const Inside *inside, const std::array<double, MAX_MEDIA> *absorb,
		const std::array<double, MAX_MEDIA> *extinction, Branching *branching) {
	// a clear surface's two shares (Stage 78): as before, what it turns away the room's; or a branch
	const auto clear = [&](Landing &out, Vec3 &dir, const Vec3 &n, double cos_i, double eta, double cos_t, double squares, uint32_t mark) {
		const double r = 0.5 * squares; // (rs^2 + rp^2: the turned share as it always was reckoned)
		if (branching != nullptr) {
			const int i = branching->events++;
			branching->reflectance.push_back(r);
			if (branching->back(i, r)) {
				out.pass *= branching->weigh ? r : 1.0;
				dir = Vec3{ dir.x + 2.0 * cos_i * n.x, dir.y + 2.0 * cos_i * n.y, dir.z + 2.0 * cos_i * n.z };
				out.route = out.route * 31u + mark + 700u;
				return true; // turned back: it stays where it is
			}
			out.pass *= branching->weigh ? 1.0 - r : 1.0;
		} else {
			out.turned += out.pass * 0.5 * squares;
			out.pass *= 1.0 - 0.5 * squares;
		}
		const double k = eta * cos_i - cos_t;
		dir = Vec3{ eta * dir.x + k * n.x, eta * dir.y + k * n.y, eta * dir.z + k * n.z };
		return false;
	};
	// a metal's k (Stage 78): nought for a clear medium
	const auto k_of = [&](int medium) { return extinction != nullptr && medium >= 0 && medium < MAX_MEDIA ? (*extinction)[static_cast<size_t>(medium)] : 0.0; };
	// at a metal (Stage 78): turned back by its complex Fresnel reflectance (Born and Wolf), the rest absorbed in its skin
	const auto mirror = [&](Landing &out, Vec3 &dir, const Vec3 &n, double n1, int there, uint32_t mark) {
		const double cos_i = -dot(n, dir);
		const double r = chem::optics::fresnel_reflectance(n1, 0.0, index[static_cast<size_t>(there)], k_of(there), cos_i);
		out.absorbed += out.pass * (1.0 - r);
		out.pass *= r;
		dir = Vec3{ dir.x + 2.0 * cos_i * n.x, dir.y + 2.0 * cos_i * n.y, dir.z + 2.0 * cos_i * n.z };
		out.route = out.route * 31u + mark;
	};
	// what a medium takes in over a stretch of the way (Stage 46)
	const auto take = [&](Landing &out, int medium, double cm) {
		if (absorb != nullptr && medium >= 0 && medium < MAX_MEDIA) {
			const double kept = std::exp(-(*absorb)[static_cast<size_t>(medium)] * cm);
			out.absorbed += out.pass * (1.0 - kept);
			out.pass *= kept;
		}
	};
	Landing out;
	int here = 0;
	const int MOST = MEETINGS_MOST;
	int turned = 0; // how often it has been turned back
	constexpr double NEAR = 1.0e-9; // a surface just left is not met again
	constexpr double NUDGE = 1.0e-7; // how far to each side of a surface within the core the medium is asked
	for (; out.meetings < MOST;) {
		double best = 1.0e300;
		int hit = -1;
		for (size_t i = 0; i < shape.patches.size(); ++i) {
			const Patch &p = shape.patches[i];
			if (p.torus) {
				const double t = torus_hit(p, o, u, NEAR);
				if (t > NEAR && t < best) {
					best = t;
					hit = static_cast<int>(i);
				}
				continue;
			}
			// f(o + t u) = A t^2 + B t + C
			const double a = p.a.x * u.x * u.x + p.a.y * u.y * u.y + p.a.z * u.z * u.z;
			const double b = 2.0 * (p.a.x * o.x * u.x + p.a.y * o.y * u.y + p.a.z * o.z * u.z) + dot(p.l, u);
			const double c = p.a.x * o.x * o.x + p.a.y * o.y * o.y + p.a.z * o.z * o.z + dot(p.l, o) + p.c;
			double roots[2];
			int count = 0;
			if (std::fabs(a) < 1.0e-14) {
				if (std::fabs(b) > 1.0e-14) {
					roots[count++] = -c / b;
				}
			} else {
				const double disc = b * b - 4.0 * a * c;
				if (disc >= 0.0) {
					// the stable pair: no cancellation in the smaller root
					const double q = -0.5 * (b + (b < 0.0 ? -1.0 : 1.0) * std::sqrt(disc));
					roots[count++] = q / a;
					if (q != 0.0) {
						roots[count++] = c / q;
					}
				}
			}
			for (int k = 0; k < count; ++k) {
				if (roots[k] > NEAR && roots[k] < best && within(p, along(o, u, roots[k]))) {
					best = roots[k];
					hit = static_cast<int>(i);
				}
			}
		}
		if (inside != nullptr && (here == CORE || here == CORE_AIR)) {
			// within the core (Stage 30): the nearest place before the glass where what the ray is in changes
			double places[6];
			int kinds[6];
			const int count = inside_crossings(*inside, o, u, places, kinds);
			double nearest = best;
			int which = -1;
			for (int k = 0; k < count; ++k) {
				if (places[k] > NEAR && places[k] < nearest && medium_at(*inside, along(o, u, places[k] - NUDGE)) != medium_at(*inside, along(o, u, places[k] + NUDGE))) {
					nearest = places[k];
					which = kinds[k];
				}
			}
			if (which >= 0) {
				out.chord[static_cast<size_t>(here)] += nearest;
				take(out, here, nearest);
				o = along(o, u, nearest);
				++out.meetings;
				const int there = medium_at(*inside, along(o, u, NUDGE));
				if (there == SEDIMENT) {
					out.solid = true;
					out.route = out.route * 31u + static_cast<uint32_t>(200 + which);
					break;
				}
				Vec3 n = inside_normal(*inside, which, o);
				const double length = std::sqrt(dot(n, n));
				if (!(length > 0.0)) {
					continue;
				}
				n = Vec3{ n.x / length, n.y / length, n.z / length };
				if (dot(n, u) > 0.0) {
					n = Vec3{ -n.x, -n.y, -n.z };
				}
				const double n1 = index[static_cast<size_t>(here)], n2 = index[static_cast<size_t>(there)];
				if (k_of(there) > 0.0) {
					mirror(out, u, n, n1, there, static_cast<uint32_t>(2 * (100 + which)) + 600u);
					continue;
				}
				const double cos_i = -dot(n, u), eta = n1 / n2, sin2 = eta * eta * (1.0 - cos_i * cos_i);
				if (sin2 >= 1.0) {
					if (++turned > TURNS_MOST) {
						break; // turned back once too often: it goes on as it is (below)
					}
					u = Vec3{ u.x + 2.0 * cos_i * n.x, u.y + 2.0 * cos_i * n.y, u.z + 2.0 * cos_i * n.z };
					out.route = out.route * 31u + static_cast<uint32_t>(2 * (100 + which) + 1);
					continue;
				}
				const double cos_t = std::sqrt(1.0 - sin2);
				const double rs = (n1 * cos_i - n2 * cos_t) / (n1 * cos_i + n2 * cos_t), rp = (n1 * cos_t - n2 * cos_i) / (n1 * cos_t + n2 * cos_i);
				if (clear(out, u, n, cos_i, eta, cos_t, rs * rs + rp * rp, static_cast<uint32_t>(2 * (100 + which)))) {
					continue;
				}
				here = there;
				out.route = out.route * 31u + static_cast<uint32_t>(2 * (100 + which));
				continue;
			}
		}
		if (hit < 0) {
			break;
		}
		const Patch &p = shape.patches[static_cast<size_t>(hit)];
		out.chord[static_cast<size_t>(here)] += best;
		take(out, here, best);
		o = along(o, u, best);
		++out.meetings;
		// the surface's normal, and which medium is beyond it
		Vec3 n{ 2.0 * p.a.x * o.x + p.l.x, 2.0 * p.a.y * o.y + p.l.y, 2.0 * p.a.z * o.z + p.l.z };
		if (p.torus) {
			const double rho = std::sqrt(o.x * o.x + o.z * o.z);
			const double k = rho > 0.0 ? 2.0 * (rho - p.major) / rho : 0.0;
			n = Vec3{ k * o.x, 2.0 * (o.y - p.cy), k * o.z };
		}
		const double length = std::sqrt(dot(n, n));
		if (!(length > 0.0)) {
			continue;
		}
		n = Vec3{ n.x / length, n.y / length, n.z / length };
		const bool rising = dot(n, u) > 0.0; // from f < 0 to f > 0
		int there = rising ? p.above : p.below;
		if (there == (here == CORE_AIR ? CORE : here)) {
			out.mismatched += std::fabs(dot(n, u)) > 1.0e-6 ? 1 : 0;
			continue; // grazing, or a surface between two parts of one medium
		}
		if (inside != nullptr && there == CORE) {
			// into the core (Stage 30): its liquid, the air over it, or the sediment lying against the glass
			there = medium_at(*inside, along(o, u, NUDGE));
			if (there == SEDIMENT) {
				out.solid = true;
				out.route = out.route * 31u + static_cast<uint32_t>(2 * (hit + 1)) + 300u;
				break;
			}
		}
		if (rising) {
			n = Vec3{ -n.x, -n.y, -n.z }; // against the ray
		}
		const double n1 = index[static_cast<size_t>(here)], n2 = index[static_cast<size_t>(there)];
		if (k_of(there) > 0.0) {
			mirror(out, u, n, n1, there, static_cast<uint32_t>(2 * (hit + 1)) + 600u);
			continue;
		}
		const double cos_i = -dot(n, u), eta = n1 / n2, sin2 = eta * eta * (1.0 - cos_i * cos_i);
		if (sin2 >= 1.0) {
			// turned back, all of it
			if (++turned > TURNS_MOST) {
				break; // once too often: it goes on as it is (below)
			}
			u = Vec3{ u.x + 2.0 * cos_i * n.x, u.y + 2.0 * cos_i * n.y, u.z + 2.0 * cos_i * n.z };
			out.route = out.route * 31u + static_cast<uint32_t>(2 * (hit + 1) + 1);
			continue;
		}
		const double cos_t = std::sqrt(1.0 - sin2);
		const double rs = (n1 * cos_i - n2 * cos_t) / (n1 * cos_i + n2 * cos_t), rp = (n1 * cos_t - n2 * cos_i) / (n1 * cos_t + n2 * cos_i);
		if (clear(out, u, n, cos_i, eta, cos_t, rs * rs + rp * rp, static_cast<uint32_t>(2 * (hit + 1)))) {
			continue;
		}
		here = there;
		out.route = out.route * 31u + static_cast<uint32_t>(2 * (hit + 1));
	}
	out.at = o;
	out.toward = u;
	if (out.solid) {
		// it ended in the sediment: it reaches no wall, and what its surfaces passed is what lights the sediment
		out.left = false;
		out.lands = false;
		out.x = u.x < 0.0 ? -1.0e5 : 1.0e5;
		out.y = o.y;
		return out;
	}
	if (here != 0) {
		// Still inside, turned back more often than is allowed or after every meeting allowed: it goes straight on
		// from where it is, as if the rest of the body were not there, with what its surfaces have passed so far
		// (Stage 33, the user's call: light held in the glass is not followed to its end, and is not darkness)
		out.route = out.route * 31u + 7u;
	}
	out.lands = u.z > 0.05;
	if (out.lands) {
		const double t = (wall - o.z) / u.z;
		out.x = o.x + t * u.x;
		out.y = o.y + t * u.y;
	} else {
		// it leaves sideways or back toward the eye: far off the wall, on the side it goes
		out.x = 1.0e5; // (one place for all of them: what they show is the room in front, whichever way they go; Stage 33)
		out.y = o.y;
		out.route = out.route * 31u + 5u;
	}
	return out;
}

Shape cylinder(double outer, double glass) {
	Shape s;
	Patch shell;
	shell.a = Vec3{ 1.0, 0.0, 1.0 };
	shell.c = -outer * outer;
	shell.below = glass > 0.0 ? GLASS : CORE;
	shell.above = 0;
	s.patches.push_back(shell);
	if (glass > 0.0 && glass < outer) {
		Patch inner;
		inner.a = Vec3{ 1.0, 0.0, 1.0 };
		inner.c = -(outer - glass) * (outer - glass);
		inner.below = CORE;
		inner.above = GLASS;
		s.patches.push_back(inner);
	}
	return s;
}

Shape prism(const double corners[3][2]) {
	Shape s;
	const double cx = (corners[0][0] + corners[1][0] + corners[2][0]) / 3.0, cz = (corners[0][1] + corners[1][1] + corners[2][1]) / 3.0;
	for (int e = 0; e < 3; ++e) {
		const int f = (e + 1) % 3;
		const double ax = corners[e][0], az = corners[e][1], bx = corners[f][0], bz = corners[f][1];
		// the face's plane: n . (p - a) = 0, n across the edge
		Patch face;
		face.l = Vec3{ bz - az, 0.0, -(bx - ax) };
		face.c = -(face.l.x * ax + face.l.z * az);
		const bool centre_below = face.l.x * cx + face.l.z * cz + face.c < 0.0;
		face.below = centre_below ? GLASS : 0;
		face.above = centre_below ? 0 : GLASS;
		face.lo = Vec3{ std::min(ax, bx), -1.0e300, std::min(az, bz) };
		face.hi = Vec3{ std::max(ax, bx), 1.0e300, std::max(az, bz) };
		s.patches.push_back(face);
	}
	return s;
}

Shape ball(double radius) {
	Shape s;
	Patch skin;
	skin.a = Vec3{ 1.0, 1.0, 1.0 };
	skin.c = -radius * radius;
	skin.below = GLASS;
	skin.above = 0;
	s.patches.push_back(skin);
	return s;
}

Shape revolved(const glass::Vessel &vessel) {
	Shape s;
	// a piece of a chain whose left side (as it runs) is `left` and right side `right`
	const auto add = [&](const glass::Piece &piece, int left, int right) {
		Patch p;
		if (piece.arc) {
			const double t_lo = std::min(piece.t0, piece.t1), t_hi = std::max(piece.t0, piece.t1);
			double rlo = std::min(piece.a.r, piece.b.r), rhi = std::max(piece.a.r, piece.b.r);
			double ylo = std::min(piece.a.y, piece.b.y), yhi = std::max(piece.a.y, piece.b.y);
			for (int k = -4; k <= 4; ++k) { // the circle's extremes within the arc
				const double t = k * 0.5 * std::numbers::pi;
				if (t > t_lo && t < t_hi) {
					rlo = std::min(rlo, piece.centre.r + piece.radius * std::cos(t));
					rhi = std::max(rhi, piece.centre.r + piece.radius * std::cos(t));
					ylo = std::min(ylo, piece.centre.y + piece.radius * std::sin(t));
					yhi = std::max(yhi, piece.centre.y + piece.radius * std::sin(t));
				}
			}
			p.lo.y = ylo;
			p.hi.y = yhi;
			p.rho_lo = std::max(0.0, rlo);
			p.rho_hi = rhi;
			// the centre is on the left of an arc that turns anticlockwise
			const bool centre_left = piece.t1 > piece.t0;
			const int inner = centre_left ? left : right, outer = centre_left ? right : left;
			if (std::fabs(piece.centre.r) < 1.0e-12) {
				// centred on the axis: a sphere
				p.a = Vec3{ 1.0, 1.0, 1.0 };
				p.l = Vec3{ 0.0, -2.0 * piece.centre.y, 0.0 };
				p.c = piece.centre.y * piece.centre.y - piece.radius * piece.radius;
			} else {
				p.torus = true;
				p.major = piece.centre.r;
				p.cy = piece.centre.y;
				p.minor = piece.radius;
				p.t_lo = t_lo;
				p.t_hi = t_hi;
			}
			p.below = inner;
			p.above = outer;
		} else if (std::fabs(piece.b.y - piece.a.y) < 1.0e-15) {
			// a flat ring: f = y - y0; running outward its left is above
			p.l = Vec3{ 0.0, 1.0, 0.0 };
			p.c = -piece.a.y;
			p.rho_lo = std::min(piece.a.r, piece.b.r);
			p.rho_hi = std::max(piece.a.r, piece.b.r);
			const bool outward = piece.b.r > piece.a.r;
			p.below = outward ? right : left;
			p.above = outward ? left : right;
		} else {
			// a cone: rho = m + k y, f = rho^2 - (m + k y)^2, negative toward the axis; running up its left is the axis's side
			const double k = (piece.b.r - piece.a.r) / (piece.b.y - piece.a.y), m = piece.a.r - k * piece.a.y;
			p.a = Vec3{ 1.0, -k * k, 1.0 };
			p.l = Vec3{ 0.0, -2.0 * m * k, 0.0 };
			p.c = -m * m;
			p.lo.y = std::min(piece.a.y, piece.b.y);
			p.hi.y = std::max(piece.a.y, piece.b.y);
			const bool up = piece.b.y > piece.a.y;
			p.below = up ? left : right;
			p.above = up ? right : left;
		}
		s.patches.push_back(p);
	};
	for (const glass::Piece &piece : vessel.outside) {
		add(piece, GLASS, 0);
	}
	for (const glass::Piece &piece : vessel.inside) {
		add(piece, CORE, GLASS);
	}
	// the top of the wall: a flat ring from the outside's end to the inside's, running inward (glass on its left, below)
	if (!vessel.outside.empty() && !vessel.inside.empty()) {
		glass::Piece ring;
		ring.a = vessel.outside.back().b;
		ring.b = vessel.inside.back().b;
		if (std::fabs(ring.a.r - ring.b.r) > 1.0e-12) {
			add(ring, GLASS, 0);
		}
	}
	return s;
}

const Shape &revolved_of(const glass::Vessel &vessel) {
	static std::mutex lock;
	static std::map<const glass::Vessel *, std::unique_ptr<Shape>> made;
	const std::lock_guard<std::mutex> hold(lock);
	std::unique_ptr<Shape> &s = made[&vessel];
	if (!s) {
		s = std::make_unique<Shape>(revolved(vessel));
	}
	return *s;
}

} // namespace lab::trace
