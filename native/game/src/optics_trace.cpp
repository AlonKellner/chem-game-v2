// The tracer and the refraction maps (GAME_SPEC §12.3, Stage 20).
#include "lab/optics_trace.hpp"

#include <algorithm>
#include <cmath>
#include <atomic>
#include <map>
#include <memory>
#include <mutex>
#include <thread>
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

Landing trace(const Shape &shape, Vec3 o, Vec3 u, double wall, const std::array<double, MAX_MEDIA> &index) {
	Landing out;
	int here = 0;
	constexpr int MOST = 32;
	constexpr double NEAR = 1.0e-9; // a surface just left is not met again
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
		if (hit < 0) {
			break;
		}
		const Patch &p = shape.patches[static_cast<size_t>(hit)];
		out.chord[static_cast<size_t>(here)] += best;
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
		const int there = rising ? p.above : p.below;
		if (there == here) {
			out.mismatched += std::fabs(dot(n, u)) > 1.0e-6 ? 1 : 0;
			continue; // grazing, or a surface between two parts of one medium
		}
		if (rising) {
			n = Vec3{ -n.x, -n.y, -n.z }; // against the ray
		}
		const double n1 = index[static_cast<size_t>(here)], n2 = index[static_cast<size_t>(there)];
		const double cos_i = -dot(n, u), eta = n1 / n2, sin2 = eta * eta * (1.0 - cos_i * cos_i);
		if (sin2 >= 1.0) {
			// turned back, all of it
			u = Vec3{ u.x + 2.0 * cos_i * n.x, u.y + 2.0 * cos_i * n.y, u.z + 2.0 * cos_i * n.z };
			out.route = out.route * 31u + static_cast<uint32_t>(2 * (hit + 1) + 1);
			continue;
		}
		const double cos_t = std::sqrt(1.0 - sin2);
		const double rs = (n1 * cos_i - n2 * cos_t) / (n1 * cos_i + n2 * cos_t), rp = (n1 * cos_t - n2 * cos_i) / (n1 * cos_t + n2 * cos_i);
		out.pass *= 1.0 - 0.5 * (rs * rs + rp * rp);
		const double k = eta * cos_i - cos_t;
		u = Vec3{ eta * u.x + k * n.x, eta * u.y + k * n.y, eta * u.z + k * n.z };
		here = there;
		out.route = out.route * 31u + static_cast<uint32_t>(2 * (hit + 1));
	}
	out.at = o;
	out.toward = u;
	if (here != 0) {
		out.pass = 0.0; // still inside after every meeting allowed: nothing of the wall comes this way
		out.route = out.route * 31u + 7u;
		out.left = false;
	}
	out.lands = here == 0 && u.z > 0.05;
	if (out.lands) {
		const double t = (wall - o.z) / u.z;
		out.x = o.x + t * u.x;
		out.y = o.y + t * u.y;
	} else {
		// it leaves sideways or back toward the eye: far off the wall, on the side it goes
		out.x = u.x < 0.0 ? -1.0e5 : 1.0e5;
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

// ---- maps

namespace {

double index_at(const float *row, double nm) {
	if (row == nullptr) {
		return 1.0;
	}
	const double x = std::min(std::max(nm - 380.0, 0.0), 400.0);
	const int i = std::min(static_cast<int>(x), 399);
	const double f = x - i;
	return row[i] * (1.0 - f) + row[i + 1] * f;
}

// whether the core is liquid: with two media the first is the liquid's, with one it is air's
bool liquid(const MapSpec &spec, int medium) {
	return spec.media >= 2 && medium == 0;
}

} // namespace

int Map::samples() const {
	return coarse * MAP_FINE + 1;
}

MapRay map_ray(const MapSpec &spec, double x, double nm, int medium) {
	// its core: its liquid, the air over it, or (a third core: a vessel's floor) glass all through
	const double glass = index_at(spec.glass_index, nm);
	std::array<double, MAX_MEDIA> index{ 1.0, glass, liquid(spec, medium) ? index_at(spec.liquid_index, nm) : medium == 2 ? glass : 1.0, 1.0 };
	const Landing l = trace(spec.shape, Vec3{ x * spec.cm_per_pixel, spec.y_cm, -1000.0 }, Vec3{ 0.0, 0.0, 1.0 }, spec.wall_cm, index);
	MapRay out;
	out.landing = l.lands ? l.x / spec.cm_per_pixel : l.x;
	out.pass = l.pass;
	out.chord = liquid(spec, medium) ? l.chord[CORE] : 0.0;
	out.route = l.route;
	return out;
}

double map_place(double nm) {
	const double r = 380.0 / std::min(std::max(nm, 380.0), 780.0);
	return std::min(std::max((1.0 - r * r) / MAP_PLACE_SPAN, 0.0), 1.0);
}

double map_nm(double place) {
	return 380.0 / std::sqrt(1.0 - MAP_PLACE_SPAN * std::min(std::max(place, 0.0), 1.0));
}

namespace {

// the cubic through four values at the thirds of a piece, at g of the way through it
double thirds(double v0, double v1, double v2, double v3, double g) {
	const double s = 3.0 * g;
	return -(s - 1.0) * (s - 2.0) * (s - 3.0) / 6.0 * v0 + s * (s - 2.0) * (s - 3.0) / 2.0 * v1 - s * (s - 1.0) * (s - 3.0) / 2.0 * v2 + s * (s - 1.0) * (s - 2.0) / 6.0 * v3;
}

} // namespace

Map build_map(const MapSpec &spec) {
	Map map;
	map.media = spec.media;
	map.left = std::floor(spec.left - spec.grid) + spec.grid;
	map.coarse = std::max(1, static_cast<int>(std::ceil(spec.right - map.left)));
	const int n = map.samples();

	// The pieces: the fewest, even in 1 / nm^2, in each of which the cubic through a ray's four nodes is the
	// ray: tried on rays across the body, in the middle of each third of each piece. A landing may be off by
	// MAP_LANDING_TOLERANCE as a shift of the image on the screen, or by MAP_NM_TOLERANCE as a shift in
	// wavelength (where a ray leaves a face nearly along it, its landing runs away with the wavelength, and no
	// cubic follows it: but the light there is spread as far, and a nanometre's twentieth of it is nothing).
	map.pieces = std::max(1, spec.pieces_at_least);
	{
		constexpr int PROBES = 33;
		for (int p = 0; p < PROBES; ++p) {
			const double x = spec.left + (spec.right - spec.left) * (p + 0.5) / PROBES;
			for (int medium = 0; medium < spec.media; ++medium) {
				// how fast the landing moves across the body here: an error on the wall shifts the image by
				// that much less on the screen
				const MapRay here = map_ray(spec, x, 580.0, medium), beside = map_ray(spec, x + 1.0 / MAP_FINE, 580.0, medium);
				map.rays += 2;
				const double slope = std::max(1.0, std::fabs(beside.landing - here.landing) * MAP_FINE);
				for (; map.pieces < spec.pieces_most;) {
					bool true_to_it = true;
					for (int piece = 0; piece < map.pieces && true_to_it; ++piece) {
						MapRay node[4];
						double nm[4];
						for (int k = 0; k < 4; ++k) {
							nm[k] = map_nm((piece + k / 3.0) / map.pieces);
							node[k] = map_ray(spec, x, nm[k], medium);
							++map.rays;
						}
						for (int third = 0; third < 3 && true_to_it; ++third) {
							const double g = (third + 0.5) / 3.0;
							const MapRay exact = map_ray(spec, x, map_nm((piece + g) / map.pieces), medium);
							++map.rays;
							if (exact.route != node[0].route || node[1].route != node[0].route || node[2].route != node[0].route || node[3].route != node[0].route || exact.pass < 0.02) {
								continue; // a ray that changes its way in the piece is read at the piece's nearer end
							}
							const double per_nm = std::fabs(node[third + 1].landing - node[third].landing) / std::max(nm[third + 1] - nm[third], 1e-9);
							const auto sq = [](const MapRay &r) { return r.chord * r.chord; };
							true_to_it = std::fabs(thirds(node[0].landing, node[1].landing, node[2].landing, node[3].landing, g) - exact.landing) <=
												 std::max(MAP_LANDING_TOLERANCE * slope, MAP_NM_TOLERANCE * per_nm) &&
										 std::fabs(thirds(node[0].pass, node[1].pass, node[2].pass, node[3].pass, g) - exact.pass) <= MAP_PASS_TOLERANCE &&
										 std::fabs(thirds(sq(node[0]), sq(node[1]), sq(node[2]), sq(node[3]), g) - sq(exact)) <= 0.01 * std::max(1.0, sq(exact));
						}
					}
					if (true_to_it) {
						break;
					}
					map.pieces *= 2;
				}
			}
		}
	}

	for (;;) {
	// the cells: traced where the map bends across the body, filled along the straight line where it does not
	const int nodes = 3 * map.pieces + 1, per = map.media * nodes; // rays a sample
	std::vector<double> node_nm(static_cast<size_t>(nodes));
	for (int j = 0; j < nodes; ++j) {
		node_nm[static_cast<size_t>(j)] = map_nm(static_cast<double>(j) / (nodes - 1));
	}
	std::vector<MapRay> rays(static_cast<size_t>(n) * static_cast<size_t>(per));
	std::vector<char> known(static_cast<size_t>(n), 0);
	const auto shoot = [&](int i) {
		if (known[static_cast<size_t>(i)]) {
			return;
		}
		for (int medium = 0; medium < map.media; ++medium) {
			for (int j = 0; j < nodes; ++j) {
				rays[static_cast<size_t>(i) * static_cast<size_t>(per) + static_cast<size_t>(medium * nodes + j)] =
						map_ray(spec, map.left + static_cast<double>(i) / MAP_FINE, node_nm[static_cast<size_t>(j)], medium);
				++map.rays;
			}
		}
		known[static_cast<size_t>(i)] = 1;
	};
	const auto ray = [&](int i, int k) -> MapRay & { return rays[static_cast<size_t>(i) * static_cast<size_t>(per) + static_cast<size_t>(k)]; };
	map.level.assign(static_cast<size_t>(map.coarse), 1.0f);
	// [i0, i1] in samples, a power of two wide: straight if the middle ray is where the ends put it
	const auto refine = [&](const auto &self, int i0, int i1, int cell) -> void {
		shoot(i0);
		shoot(i1);
		const int width = i1 - i0;
		if (width == 1) {
			map.level[static_cast<size_t>(cell)] = static_cast<float>(MAP_FINE);
			return;
		}
		const int mid = i0 + width / 2;
		shoot(mid);
		bool straight = true;
		for (int k = 0; k < per && straight; ++k) {
			const MapRay &a = ray(i0, k), &m = ray(mid, k), &b = ray(i1, k);
			// Straight: the middle ray lands where the ends put it. Within MAP_LANDING_TOLERANCE as a shift of the
			// image on the screen (the error on the wall over the map's slope across the part); and so that the
			// part's two halves have slopes alike within MAP_SLOPE_TOLERANCE, for a thin line's image is as bright
			// as the slope where it lands is small. (Where rays cross, the slope passes through nothing: there
			// the first alone, down to the finest part.)
			const double spread = std::fabs(b.landing - a.landing), slope = std::max(1.0, spread * MAP_FINE / width);
			const double allowed = std::min(MAP_LANDING_TOLERANCE * slope, std::max(0.25 * MAP_SLOPE_TOLERANCE * spread, 0.0005));
			straight = a.route == m.route && b.route == m.route && std::fabs(m.landing - 0.5 * (a.landing + b.landing)) <= allowed &&
					   std::fabs(m.pass - 0.5 * (a.pass + b.pass)) <= MAP_PASS_TOLERANCE &&
					   std::fabs(m.chord * m.chord - 0.5 * (a.chord * a.chord + b.chord * b.chord)) <= 0.01 * std::max(1.0, m.chord * m.chord);
		}
		if (!straight) {
			self(self, i0, mid, cell);
			self(self, mid, i1, cell);
			return;
		}
		map.level[static_cast<size_t>(cell)] = std::max(map.level[static_cast<size_t>(cell)], static_cast<float>(MAP_FINE / width));
		// the samples between, along the two straight halves
		for (int half = 0; half < 2; ++half) {
			const int from = half == 0 ? i0 : mid, to = half == 0 ? mid : i1;
			for (int i = from + 1; i < to; ++i) {
				const double f = static_cast<double>(i - from) / (to - from);
				for (int k = 0; k < per; ++k) {
					const MapRay &a = ray(from, k), &b = ray(to, k);
					MapRay &out = ray(i, k);
					out.landing = a.landing + (b.landing - a.landing) * f;
					out.pass = a.pass + (b.pass - a.pass) * f;
					out.chord = std::sqrt(a.chord * a.chord + (b.chord * b.chord - a.chord * a.chord) * f);
					out.route = a.route;
				}
				known[static_cast<size_t>(i)] = 1;
			}
		}
	};
	for (int c = 0; c < map.coarse; ++c) {
		refine(refine, c * MAP_FINE, (c + 1) * MAP_FINE, c);
	}

	// how fast each whole pixel's landing bends in wavelength, at most: the second difference between a
	// ray's nodes, over the map's slope across (as a shift on the screen), in pixels per nm^2. A band's sweep
	// is taken in as many straight steps as keep it true (the renderer: map_steps).
	map.bend.assign(static_cast<size_t>(map.coarse), 0.0f);
	for (int c = 0; c < map.coarse; ++c) {
		double most = 0.0;
		for (int i = c * MAP_FINE; i < (c + 1) * MAP_FINE; ++i) {
			for (int medium = 0; medium < map.media; ++medium) {
				for (int j = 1; j + 1 < nodes; ++j) {
					const int k = medium * nodes + j;
					const MapRay &before = ray(i, k - 1), &here = ray(i, k), &after = ray(i, k + 1), &beside = ray(i + 1, k);
					if (before.route != here.route || after.route != here.route || beside.route != here.route || here.pass < 0.02) {
						continue;
					}
					const double slope = std::max(1.0, std::fabs(beside.landing - here.landing) * MAP_FINE);
					const double nm0 = node_nm[static_cast<size_t>(j - 1)], nm1 = node_nm[static_cast<size_t>(j)], nm2 = node_nm[static_cast<size_t>(j + 1)];
					const double second = 2.0 * ((after.landing - here.landing) / (nm2 - nm1) - (here.landing - before.landing) / (nm1 - nm0)) / (nm2 - nm0);
					most = std::max(most, std::fabs(second) / slope);
				}
			}
		}
		map.bend[static_cast<size_t>(c)] = static_cast<float>(most);
	}

	// Routes as small whole numbers, in the order met. And where the route changes between a sample and the
	// next, how far along (found by halving, twelve times): in the route's fraction, half of it, so that a part
	// of a pixel across the change is shared out truly (a rim: none of it to the ray that misses).
	std::map<uint32_t, int> ids;
	std::vector<float> routes(static_cast<size_t>(n) * static_cast<size_t>(per));
	// Where a route's boundary across the body moves with the wavelength, a ray near it changes its way within
	// a piece, and is read there as its nearest node's ray: wrongly over at most half the way the boundary
	// moves between two nodes. That is kept to MAP_LANDING_TOLERANCE of the screen, by more pieces.
	double moved = 0.0;
	std::vector<std::array<double, 3>> before, here; // a node's boundaries: where, and the routes on its two sides
	for (int medium = 0; medium < map.media; ++medium) {
		before.clear();
		for (int j = 0; j < nodes; ++j) {
			const int k = medium * nodes + j;
			here.clear();
			for (int i = 0; i < n; ++i) {
				const MapRay &r = ray(i, k);
				const auto found = ids.emplace(r.route, static_cast<int>(ids.size())).first;
				double along_cell = 0.0;
				if (i + 1 < n && ray(i + 1, k).route != r.route) {
					double lo = 0.0, hi = 1.0;
					for (int it = 0; it < 12; ++it) {
						const double mid = 0.5 * (lo + hi);
						(map_ray(spec, map.left + (i + mid) / MAP_FINE, node_nm[static_cast<size_t>(j)], medium).route == r.route ? lo : hi) = mid;
						++map.rays;
					}
					along_cell = 0.5 * (lo + hi);
					here.push_back({ (i + along_cell) / MAP_FINE, static_cast<double>(r.route), static_cast<double>(ray(i + 1, k).route) });
				}
				routes[static_cast<size_t>(i) * static_cast<size_t>(per) + static_cast<size_t>(k)] = static_cast<float>(found->second + 0.5 * along_cell);
			}
			if (j > 0) {
				for (const auto &now : here) {
					double nearest = -1.0;
					for (const auto &was : before) {
						if (was[1] == now[1] && was[2] == now[2] && (nearest < 0.0 || std::fabs(was[0] - now[0]) < nearest)) {
							nearest = std::fabs(was[0] - now[0]);
						}
					}
					moved = std::max(moved, nearest); // one that was not there before: not counted
				}
			}
			before = here;
		}
	}
	int wanted = map.pieces;
	while (wanted < spec.pieces_most && 0.5 * moved * map.pieces / wanted > MAP_LANDING_TOLERANCE) {
		wanted *= 2;
	}
	if (wanted > map.pieces) {
		map.pieces = wanted;
		continue; // again, finer
	}
	map.cells.resize(static_cast<size_t>(map.media) * static_cast<size_t>(n) * static_cast<size_t>(map.pieces) * MAP_CELL);
	for (int medium = 0; medium < map.media; ++medium) {
		for (int i = 0; i < n; ++i) {
			for (int piece = 0; piece < map.pieces; ++piece) {
				float *cell = &map.cells[((static_cast<size_t>(medium) * static_cast<size_t>(n) + static_cast<size_t>(i)) * static_cast<size_t>(map.pieces) + static_cast<size_t>(piece)) * MAP_CELL];
				for (int k = 0; k < 4; ++k) {
					const MapRay &r = ray(i, medium * nodes + 3 * piece + k);
					cell[k] = static_cast<float>(r.landing);
					cell[4 + k] = static_cast<float>(r.pass);
					cell[8 + k] = static_cast<float>(r.chord * r.chord); // its square: straight across, where the chord rises as a root at a rim
					cell[12 + k] = routes[static_cast<size_t>(i) * static_cast<size_t>(per) + static_cast<size_t>(medium * nodes + 3 * piece + k)];
				}
			}
		}
	}
	return map;
	}
}

MapRay map_read(const Map &map, double x, double nm, int medium) {
	const int n = map.samples();
	const double s = std::min(std::max((x - map.left) * MAP_FINE, 0.0), static_cast<double>(n - 1));
	const int i = std::min(static_cast<int>(s), n - 2);
	double f = s - i;
	const double place = map_place(nm) * map.pieces;
	const int piece = std::min(static_cast<int>(place), map.pieces - 1);
	const double g = place - piece;
	const float *a = map.cell(medium, piece, i), *b = map.cell(medium, piece, i + 1);
	const auto whole = [](float route) { return std::floor(route); };
	for (int k = 0; k < 4; ++k) {
		if (whole(a[12 + k]) != whole(b[12 + k])) {
			// the route changes between the samples: each side of the change its own sample's ray
			f = f < 2.0 * (a[12 + k] - whole(a[12 + k])) ? 0.0 : 1.0;
			break;
		}
	}
	const float *near = f < 0.5 ? a : b;
	const auto one_way = [&](const float *c) { return whole(c[13]) == whole(c[12]) && whole(c[14]) == whole(c[12]) && whole(c[15]) == whole(c[12]); };
	const bool cubic = (f <= 0.0 || one_way(b)) && (f >= 1.0 || one_way(a));
	const int node = std::min(static_cast<int>(3.0 * g + 0.5), 3);
	const auto across = [&](int k) { return a[k] * (1.0 - f) + b[k] * f; };
	// the cubic through the piece's four nodes, where they all go one way; if not, the nearest node's ray
	const auto read = [&](int from) { return cubic ? thirds(across(from), across(from + 1), across(from + 2), across(from + 3), g) : across(from + node); };
	MapRay out;
	out.landing = read(0);
	out.pass = std::min(std::max(read(4), 0.0), 1.0);
	out.chord = std::sqrt(std::max(read(8), 0.0));
	out.route = static_cast<uint32_t>(whole(near[12 + (cubic ? 0 : node)]));
	return out;
}

// ---- a shaped vessel's maps, a map for each band of its rows (Stage 24)

RowMap build_rows(const MapSpec &spec, const glass::Vessel &vessel, double height) {
	RowMap out;
	const double cm = spec.cm_per_pixel;
	// the bands: cut where any piece of the profile begins or ends
	std::vector<double> cuts{ 0.0, height };
	for (const glass::Chain *chain : { &vessel.outside, &vessel.inside }) {
		for (const glass::Piece &p : *chain) {
			for (const double y : { p.a.y, p.b.y }) {
				const double row = height - y / cm;
				if (row > 0.0 && row < height) {
					cuts.push_back(row);
				}
			}
		}
	}
	std::sort(cuts.begin(), cuts.end());
	cuts.erase(std::unique(cuts.begin(), cuts.end(), [](double a, double b) { return std::fabs(a - b) < 1e-9; }), cuts.end());
	// a band is upright if every piece reaching into it is a vertical line: one map serves all its rows
	const auto upright = [&](double top, double foot) {
		const double y0 = (height - foot) * cm, y1 = (height - top) * cm;
		for (const glass::Chain *chain : { &vessel.outside, &vessel.inside }) {
			for (const glass::Piece &p : *chain) {
				const double lo = std::min(p.a.y, p.b.y), hi = std::max(p.a.y, p.b.y);
				if (hi > y0 + 1e-12 && lo < y1 - 1e-12 && (p.arc || std::fabs(p.a.r - p.b.r) > 1e-12)) {
					return false;
				}
			}
		}
		return true;
	};
	// the rows' bands: an upright band whole, a curved one in steps of at most ROW_STEP pixels
	for (size_t b = 0; b + 1 < cuts.size(); ++b) {
		const double top = cuts[b], foot = cuts[b + 1];
		const int steps = upright(top, foot) ? 1 : std::max(1, static_cast<int>(std::ceil((foot - top) / ROW_STEP)));
		for (int k = 1; k <= steps; ++k) {
			out.ends.push_back(top + (foot - top) * k / steps);
		}
	}
	// each band's map, traced at its middle row, the bands on the threads at once (none depends on another)
	out.maps.resize(out.ends.size());
	std::atomic<size_t> next{ 0 };
	std::vector<std::thread> workers;
	const unsigned threads = std::max(1u, std::min(static_cast<unsigned>(out.ends.size()), std::thread::hardware_concurrency()));
	for (unsigned t = 0; t < threads; ++t) {
		workers.emplace_back([&] {
			for (size_t k = next.fetch_add(1); k < out.ends.size(); k = next.fetch_add(1)) {
				const double from = k == 0 ? 0.0 : out.ends[k - 1];
				MapSpec row = spec;
				row.y_cm = (height - 0.5 * (from + out.ends[k])) * cm;
				row.pieces_most = std::min(spec.pieces_most, ROW_PIECES_MOST);
				out.maps[k] = build_map(row);
			}
		});
	}
	for (std::thread &w : workers) {
		w.join();
	}
	for (const Map &m : out.maps) {
		out.rays += m.rays;
	}
	// the rims: where each band's map was traced, and in each whole row
	const auto outer_at = [&](double y) { return vessel.radius_outside((height - y) * cm) / cm; };
	const auto inner_at = [&](double y) { return vessel.radius_inside((height - y) * cm) / cm; };
	for (size_t k = 0; k < out.ends.size(); ++k) {
		const double mid = 0.5 * ((k == 0 ? 0.0 : out.ends[k - 1]) + out.ends[k]);
		out.band_outer.push_back(outer_at(mid));
		out.band_inner.push_back(inner_at(mid));
	}
	const size_t whole = static_cast<size_t>(std::ceil(height)) + 1;
	for (size_t r = 0; r < whole; ++r) {
		const double mid = std::min(static_cast<double>(r) + 0.5, height);
		out.row_outer.push_back(outer_at(mid));
		out.row_inner.push_back(inner_at(mid));
	}
	return out;
}

MapRay rows_ray(const MapSpec &spec, double height, double x, double y, double nm, int medium) {
	MapSpec row = spec;
	row.y_cm = (height - y) * spec.cm_per_pixel;
	return map_ray(row, x, nm, medium);
}

namespace {

// a place across a row in a measure both its rims keep: inside the inner rim from 0 to 1, through the glass from
// 1 to 2, past the outer rim 2 and the pixels beyond (no inner rim: 0 to 2 from the axis to the outer)
double rim_measure(double x, double outer, double inner) {
	const double a = std::fabs(x), sign = x < 0.0 ? -1.0 : 1.0;
	if (inner < 1.0e-6) {
		return sign * (a <= outer ? 2.0 * a / outer : 2.0 + (a - outer));
	}
	return sign * (a <= inner ? a / inner : a <= outer ? 1.0 + (a - inner) / (outer - inner) : 2.0 + (a - outer));
}
double rim_place(double c, double outer, double inner) {
	const double a = std::fabs(c), sign = c < 0.0 ? -1.0 : 1.0;
	if (inner < 1.0e-6) {
		return sign * (a <= 2.0 ? 0.5 * a * outer : outer + (a - 2.0));
	}
	return sign * (a <= 1.0 ? a * inner : a <= 2.0 ? inner + (a - 1.0) * (outer - inner) : outer + (a - 2.0));
}

} // namespace

MapRay rows_read(const RowMap &rows, double x, double y, double nm, int medium) {
	size_t k = 0;
	while (k + 1 < rows.ends.size() && y >= rows.ends[k]) {
		++k;
	}
	// the row's rims (straight between the middles of its whole rows), and the band's
	const double d = std::max(y - 0.5, 0.0);
	const size_t r = std::min(static_cast<size_t>(d), rows.row_outer.size() - 1), r1 = std::min(r + 1, rows.row_outer.size() - 1);
	const double f = d - static_cast<double>(r);
	const double outer = rows.row_outer[r] + (rows.row_outer[r1] - rows.row_outer[r]) * f, inner = rows.row_inner[r] + (rows.row_inner[r1] - rows.row_inner[r]) * f;
	const double xb = outer > 0.0 && rows.band_outer[k] > 0.0 ? rim_place(rim_measure(x, outer, inner), rows.band_outer[k], rows.band_inner[k]) : x;
	MapRay out = map_read(rows.maps[k], xb, nm, medium);
	out.landing += x - xb;
	return out;
}

std::vector<float> rows_block(const RowMap &rows) {
	// [0] the rows' bands K, the whole rows R; [1 .. K] each band: where its rows end (pixels down from the
	// body's top), where its map begins (vec4s from the table's start), its pieces in wavelength, its whole
	// pixels across; [1 + K .. K + R] for each whole row, the band its top is in; then the maps
	const size_t k = rows.ends.size(), whole = static_cast<size_t>(std::ceil(rows.ends.back())) + 1;
	std::vector<float> out((1 + k + whole + k) * 4, 0.0f);
	out[0] = static_cast<float>(k);
	out[1] = static_cast<float>(whole);
	size_t band = 0;
	for (size_t r = 0; r < whole; ++r) {
		while (band + 1 < k && static_cast<double>(r) >= rows.ends[band]) {
			++band;
		}
		out[(1 + k + r) * 4] = static_cast<float>(band);
		out[(1 + k + r) * 4 + 1] = static_cast<float>(r < rows.row_outer.size() ? rows.row_outer[r] : 0.0);
		out[(1 + k + r) * 4 + 2] = static_cast<float>(r < rows.row_inner.size() ? rows.row_inner[r] : 0.0);
	}
	for (size_t i = 0; i < k; ++i) {
		out[(1 + k + whole + i) * 4] = static_cast<float>(rows.band_outer[i]);
		out[(1 + k + whole + i) * 4 + 1] = static_cast<float>(rows.band_inner[i]);
	}
	size_t at = 1 + k + whole + k;
	for (size_t i = 0; i < k; ++i) {
		const std::vector<float> block = map_block(rows.maps[i]);
		out[(1 + i) * 4] = static_cast<float>(rows.ends[i]);
		out[(1 + i) * 4 + 1] = static_cast<float>(at);
		out[(1 + i) * 4 + 2] = static_cast<float>(rows.maps[i].pieces);
		out[(1 + i) * 4 + 3] = static_cast<float>(rows.maps[i].coarse);
		out.insert(out.end(), block.begin(), block.end());
		at += block.size() / 4;
	}
	return out;
}

std::vector<float> map_block(const Map &map) {
	std::vector<float> out(static_cast<size_t>(1 + map.coarse) * 4 + map.cells.size(), 0.0f);
	out[0] = static_cast<float>(map.coarse);
	out[1] = static_cast<float>(map.media);
	out[2] = static_cast<float>(map.samples());
	out[3] = static_cast<float>(map.pieces);
	for (int c = 0; c < map.coarse; ++c) {
		out[static_cast<size_t>(1 + c) * 4] = map.level[static_cast<size_t>(c)];
		out[static_cast<size_t>(1 + c) * 4 + 1] = map.bend[static_cast<size_t>(c)];
	}
	std::copy(map.cells.begin(), map.cells.end(), out.begin() + static_cast<std::ptrdiff_t>(1 + map.coarse) * 4);
	return out;
}

} // namespace lab::trace
