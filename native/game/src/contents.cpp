// What lies in a vessel, at any turn (GAME_SPEC §13, §12.3, Stage 30).
#include "lab/contents.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <map>
#include <memory>
#include <mutex>
#include <vector>

namespace lab::contents {

namespace {

constexpr double PI = 3.14159265358979323846;

// the inside's radius at each height, sampled finely once for a vessel (a level cut reads it hundreds of times)
struct Section {
	double floor = 0.0, height = 0.0, step = 0.0;
	std::vector<double> radius;
	double at(double y) const {
		if (y < floor || y > height) {
			return 0.0;
		}
		const double f = (y - floor) / step;
		const size_t i = std::min(static_cast<size_t>(f), radius.size() - 2);
		const double t = f - static_cast<double>(i);
		return radius[i] * (1.0 - t) + radius[i + 1] * t;
	}
};

const Section &section_of(const glass::Vessel &vessel) {
	static std::mutex lock;
	static std::map<const glass::Vessel *, std::unique_ptr<Section>> made;
	const std::lock_guard<std::mutex> hold(lock);
	std::unique_ptr<Section> &s = made[&vessel];
	if (!s) {
		s = std::make_unique<Section>();
		constexpr int N = 1024;
		s->floor = vessel.floor;
		s->height = vessel.height;
		s->step = (vessel.height - vessel.floor) / N;
		s->radius.resize(N + 1);
		for (int i = 0; i <= N; ++i) {
			s->radius[static_cast<size_t>(i)] = vessel.radius_inside(std::min(vessel.floor + s->step * i, vessel.height - 1e-9));
		}
	}
	return *s;
}

// the part of a disc of radius r on the side x >= xc
double disc_from(double r, double xc) {
	if (xc >= r) {
		return 0.0;
	}
	if (xc <= -r) {
		return PI * r * r;
	}
	return r * r * std::acos(xc / r) - xc * std::sqrt(r * r - xc * xc);
}

// Gauss-Legendre, 4 points on [-1, 1]
constexpr double GX[4] = { -0.8611363115940526, -0.3399810435848563, 0.3399810435848563, 0.8611363115940526 };
constexpr double GW[4] = { 0.3478548451374538, 0.6521451548625461, 0.6521451548625461, 0.3478548451374538 };

struct Frame {
	double s, c; // sin and cos of the turn
	double up_x() const { return -s; }
	double up_y() const { return c; }
	double level(double x, double y) const { return -s * x + c * y; }
};

Frame frame(double turn) {
	return Frame{ std::sin(turn), std::cos(turn) };
}

// the inside's lowest and highest levels: over its outline
void reach(const glass::Vessel &vessel, const Frame &f, double &lowest, double &highest) {
	const Section &sec = section_of(vessel);
	lowest = 1e300;
	highest = -1e300;
	for (size_t i = 0; i < sec.radius.size(); ++i) {
		const double y = sec.floor + sec.step * static_cast<double>(i);
		for (const double x : { -sec.radius[i], sec.radius[i] }) {
			lowest = std::min(lowest, f.level(x, y));
			highest = std::max(highest, f.level(x, y));
		}
	}
}

} // namespace

double Cut::deep() const {
	return half > 0.0 ? area / (PI * half) : 0.0;
}

double Cone::base_area() const {
	return PI * across * deep;
}

double Cone::volume() const {
	return base_area() * height / 3.0;
}

namespace {
double below_made(const glass::Vessel &vessel, double turn, double level);
} // namespace

// (what was asked last is kept, on each thread, as lying's is: what a vessel holds below its lip is asked at
// every step of a pour by hand)
double below(const glass::Vessel &vessel, double turn, double level) {
	struct Asked {
		const glass::Vessel *vessel = nullptr;
		double turn = 0.0, level = 0.0, answer = 0.0;
	};
	// (the last few: a pour asks for the lip's and the surface's in turn, Stage 36)
	thread_local std::array<Asked, 4> kept;
	thread_local size_t next = 0;
	for (const Asked &a : kept) {
		if (a.vessel == &vessel && a.turn == turn && a.level == level) {
			return a.answer;
		}
	}
	kept[next] = Asked{ &vessel, turn, level, below_made(vessel, turn, level) };
	const double answer = kept[next].answer;
	next = (next + 1) % kept.size();
	return answer;
}

namespace {

double below_made(const glass::Vessel &vessel, double turn, double level) {
	const Frame f = frame(turn);
	if (std::fabs(f.s) < 1e-12) {
		// upright: what is under a height; or upside down: what is over one
		const double y = level / f.c;
		const double under = vessel.volume_below(std::min(std::max(y, vessel.floor), vessel.height));
		return f.c > 0.0 ? under : vessel.capacity() - under;
	}
	// At each height the inside is a disc, and the level cuts it along a line across: the part under the level is
	// a segment, in closed form; up the profile, piece by piece. The segment's area has a corner where the level
	// leaves the disc on either side: a piece is cut at those, and each stretch between them summed (8 stretches
	// of 4 points).
	double volume = 0.0;
	for (const glass::Piece &p : vessel.inside) {
		if (std::fabs(p.b.y - p.a.y) < 1e-15) {
			continue;
		}
		// where the level leaves the disc (xc = +r or -r), along the piece
		const auto leaves = [&](double sigma, double sign) {
			const glass::Point at = p.at(sigma);
			return (f.c * at.y - level) / f.s - sign * at.r;
		};
		double marks[8] = { 0.0 };
		int count = 1;
		constexpr int SCAN = 48;
		for (const double sign : { 1.0, -1.0 }) {
			double before = leaves(0.0, sign);
			for (int i = 1; i <= SCAN && count < 6; ++i) {
				const double now = leaves(static_cast<double>(i) / SCAN, sign);
				if ((before < 0.0) != (now < 0.0)) {
					double lo = static_cast<double>(i - 1) / SCAN, hi = static_cast<double>(i) / SCAN;
					const bool rising = now >= 0.0;
					for (int k = 0; k < 50; ++k) {
						const double mid = 0.5 * (lo + hi);
						((leaves(mid, sign) >= 0.0) == rising ? hi : lo) = mid;
					}
					marks[count++] = 0.5 * (lo + hi);
				}
				before = now;
			}
		}
		marks[count++] = 1.0;
		std::sort(marks, marks + count);
		for (int m = 0; m + 1 < count; ++m) {
			const double from = marks[m], to = marks[m + 1];
			if (!(to > from)) {
				continue;
			}
			constexpr int STRETCHES = 8;
			for (int k = 0; k < STRETCHES; ++k) {
				for (int g = 0; g < 4; ++g) {
					const double sigma = from + (to - from) * (k + 0.5 + 0.5 * GX[g]) / STRETCHES;
					const glass::Point at = p.at(sigma);
					// how fast the height goes with sigma
					const double dy = p.arc ? p.radius * std::cos(p.t0 + (p.t1 - p.t0) * sigma) * (p.t1 - p.t0) : p.b.y - p.a.y;
					const double xc = (f.c * at.y - level) / f.s;
					const double part = f.s > 0.0 ? disc_from(at.r, xc) : PI * at.r * at.r - disc_from(at.r, xc);
					volume += GW[g] * 0.5 * (to - from) / STRETCHES * part * dy;
				}
			}
		}
	}
	return volume;
}

} // namespace

double level_of(const glass::Vessel &vessel, double turn, double cm3) {
	const Frame f = frame(turn);
	double lo, hi;
	reach(vessel, f, lo, hi);
	if (cm3 <= 0.0) {
		return lo;
	}
	// The volume under a level rises with the level, smoothly. So it is known at 33 levels for a vessel at a turn
	// (kept for the last four asked: a pour by hand asks at every step of the lab's clock, 300 a frame at 600
	// times its speed), the level sought lies between two of them, and from there it is found by false position
	// with Illinois's guard, to a ten-million-millionth of the vessel's reach: some seven volumes, where halving
	// from the whole reach took sixty (Stage 33).
	constexpr int LEVELS = 32;
	struct Known {
		const glass::Vessel *vessel = nullptr;
		double turn = 0.0;
		double under[LEVELS + 1] = {};
	};
	thread_local Known known[4];
	thread_local int next = 0;
	const Known *table = nullptr;
	for (const Known &k : known) {
		if (k.vessel == &vessel && k.turn == turn) {
			table = &k;
		}
	}
	if (table == nullptr) {
		Known &k = known[next];
		next = (next + 1) % 4;
		k.vessel = &vessel;
		k.turn = turn;
		for (int i = 0; i <= LEVELS; ++i) {
			k.under[i] = below(vessel, turn, lo + (hi - lo) * i / LEVELS);
		}
		table = &k;
	}
	if (table->under[LEVELS] <= cm3) {
		return hi;
	}
	int at = 0;
	while (at + 1 < LEVELS && table->under[at + 1] < cm3) {
		++at;
	}
	const double span = hi - lo;
	double a = lo + span * at / LEVELS, b = lo + span * (at + 1) / LEVELS, fa = table->under[at] - cm3, fb = table->under[at + 1] - cm3;
	int side = 0;
	for (int i = 0; i < 80 && b - a > 1e-13 * span; ++i) {
		double c = fb != fa ? b - fb * (b - a) / (fb - fa) : 0.5 * (a + b);
		if (!(c > a && c < b)) {
			c = 0.5 * (a + b);
		}
		const double fc = below(vessel, turn, c) - cm3;
		if (fc < 0.0) {
			a = c;
			fa = fc;
			fb = side == -1 ? 0.5 * fb : fb;
			side = -1;
		} else {
			b = c;
			fb = fc;
			fa = side == 1 ? 0.5 * fa : fa;
			side = 1;
		}
		if (fc == 0.0) {
			return c;
		}
	}
	return 0.5 * (a + b);
}

Cut cut(const glass::Vessel &vessel, double turn, double level) {
	const Frame f = frame(turn);
	const Section &sec = section_of(vessel);
	Cut out;
	// along the level's line: from the point of it nearest the frame's origin, tau along (cos, sin)
	const auto place = [&](double tau, double &x, double &y) {
		x = level * f.up_x() + tau * f.c;
		y = level * f.up_y() + tau * f.s;
	};
	const auto inside = [&](double tau) {
		double x, y;
		place(tau, x, y);
		return y >= sec.floor && y <= sec.height && std::fabs(x) <= sec.at(y);
	};
	const double reach_of = std::hypot(vessel.diameter, vessel.height) + 1.0;
	constexpr int N = 384;
	// the longest stretch of the line inside the vessel
	double best0 = 0.0, best1 = -1.0;
	int run = -1;
	for (int i = 0; i <= N + 1; ++i) {
		const double tau = -reach_of + 2.0 * reach_of * i / N;
		const bool in = i <= N && inside(tau);
		if (in && run < 0) {
			run = i;
		} else if (!in && run >= 0) {
			const double t0 = -reach_of + 2.0 * reach_of * run / N, t1 = -reach_of + 2.0 * reach_of * (i - 1) / N;
			if (t1 - t0 > best1 - best0 || best1 < best0) {
				best0 = t0;
				best1 = t1;
			}
			run = -1;
		}
	}
	if (best1 < best0) {
		return out;
	}
	// its ends, to where the inside ends
	const double pace = 2.0 * reach_of / N;
	double a = best0 - pace, b = best0;
	for (int i = 0; i < 40; ++i) {
		const double mid = 0.5 * (a + b);
		(inside(mid) ? b : a) = mid;
	}
	const double tau0 = b;
	a = best1;
	b = best1 + pace;
	for (int i = 0; i < 40; ++i) {
		const double mid = 0.5 * (a + b);
		(inside(mid) ? a : b) = mid;
	}
	const double tau1 = a;
	if (!(tau1 > tau0)) {
		return out;
	}
	out.any = true;
	place(tau0, out.x0, out.y0);
	place(tau1, out.x1, out.y1);
	place(0.5 * (tau0 + tau1), out.mid_x, out.mid_y);
	out.half = 0.5 * (tau1 - tau0);
	// the level surface's area: along the line, as far into the depth as the vessel is round there (Simpson)
	constexpr int M = 96;
	double sum = 0.0;
	for (int i = 0; i <= M; ++i) {
		double x, y;
		place(tau0 + (tau1 - tau0) * i / M, x, y);
		const double r = sec.at(y), chord = 2.0 * std::sqrt(std::max(r * r - x * x, 0.0));
		sum += chord * (i == 0 || i == M ? 1.0 : i % 2 == 1 ? 4.0 : 2.0);
	}
	out.area = sum * (tau1 - tau0) / (3.0 * M);
	return out;
}

namespace {

glass::Point brim_inner(const glass::Vessel &vessel) {
	return vessel.inside.empty() ? glass::Point{} : vessel.inside.back().b;
}

glass::Point brim_outer(const glass::Vessel &vessel) {
	return vessel.outside.empty() ? glass::Point{} : vessel.outside.back().b;
}

// the sediment's bulk under a level: the flat section, and the part of the pile under it
double bulk_under(const glass::Vessel &vessel, const Lying &l, double level) {
	if (!l.solid) {
		return 0.0;
	}
	double bulk = below(vessel, l.turn, std::min(level, l.solid_level));
	if (level > l.solid_level && l.pile.height > 0.0) {
		const double up = std::min((level - l.solid_level) / l.pile.height, 1.0);
		bulk += l.pile.volume() * (1.0 - (1.0 - up) * (1.0 - up) * (1.0 - up));
	}
	return bulk;
}

// how high the sediment stands on the room's upright through a place across (measured along the level's line
// from the pile's middle): its flat top, or the pile over it
double sediment_top(const Lying &l, double across_from_middle) {
	if (!l.solid) {
		return -1e300;
	}
	if (l.pile.height > 0.0 && l.pile.across > 0.0 && std::fabs(across_from_middle) < l.pile.across) {
		return l.solid_level + l.pile.height * (1.0 - std::fabs(across_from_middle) / l.pile.across);
	}
	return l.solid_level;
}

// the depth of liquid under the middle of a cut: down the room's upright to the glass, or to the sediment
double depth_under(const glass::Vessel &vessel, const Lying &l, const Cut &c, double level) {
	if (!c.any) {
		return 0.0;
	}
	const Section &sec = section_of(vessel);
	const Frame f = frame(l.turn);
	const auto inside = [&](double down) {
		const double x = c.mid_x - down * f.up_x(), y = c.mid_y - down * f.up_y();
		return y >= sec.floor && y <= sec.height && std::fabs(x) <= sec.at(y);
	};
	const double most = level - l.lowest;
	double lo = 0.0, hi = most;
	constexpr int N = 96;
	for (int i = 1; i <= N; ++i) {
		if (!inside(most * i / N)) {
			lo = most * (i - 1) / N;
			hi = most * i / N;
			for (int k = 0; k < 30; ++k) {
				const double mid = 0.5 * (lo + hi);
				(inside(mid) ? lo : hi) = mid;
			}
			hi = lo;
			break;
		}
	}
	// (how far across the cut's middle is from the pile's, along the level's line)
	const double across = (c.mid_x - l.pile.x) * f.c + (c.mid_y - l.pile.y) * f.s;
	return std::max(0.0, std::min(hi, level - sediment_top(l, across)));
}

} // namespace

Lip lip_of(const glass::Vessel &vessel, int side) {
	const glass::Point inner = brim_inner(vessel), outer = brim_outer(vessel);
	const double sense = side < 0 ? -1.0 : 1.0;
	return Lip{ sense * inner.r, inner.y, sense * outer.r, outer.y };
}

double held_at(const glass::Vessel &vessel, double turn, int side) {
	const Frame f = frame(turn);
	const glass::Point lip = brim_inner(vessel);
	return below(vessel, turn, f.level((side < 0 ? -1.0 : 1.0) * lip.r, lip.y));
}

double full_tilt(const glass::Vessel &vessel, int side) {
	// the least turn (toward the side poured from) at which nothing is held below the lip; found once for a
	// vessel (a pour asks every step)
	const double sense = side < 0 ? -1.0 : 1.0;
	static std::mutex lock;
	static std::map<const glass::Vessel *, double> found;
	{
		const std::lock_guard<std::mutex> hold(lock);
		if (const auto at = found.find(&vessel); at != found.end()) {
			return sense * at->second;
		}
	}
	double lo = 0.0, hi = PI;
	for (int i = 0; i < 50; ++i) {
		const double mid = 0.5 * (lo + hi);
		(held_at(vessel, mid, 1) > 1e-9 * vessel.capacity() ? lo : hi) = mid;
	}
	const std::lock_guard<std::mutex> hold(lock);
	found[&vessel] = hi;
	return sense * hi;
}

PlainPour plain_pour(const glass::Vessel &vessel, double turn, double liquid_cm3, int side) {
	constexpr int N = 128;
	struct Table {
		const glass::Vessel *vessel = nullptr;
		double turn = 0.0;
		int side = 0;
		double lo = 0.0, hi = 0.0, lip_level = 0.0, lip_holds = 0.0, brim = 0.0;
		double under[N + 1] = {}, slope[N + 1] = {}; // the volume under each level, and how fast it rises there (per level between two)
	};
	thread_local Table tables[4];
	thread_local int next = 0;
	const Table *t = nullptr;
	for (const Table &k : tables) {
		if (k.vessel == &vessel && k.turn == turn && k.side == side) {
			t = &k;
		}
	}
	if (t == nullptr) {
		Table &k = tables[next];
		next = (next + 1) % 4;
		k.vessel = &vessel;
		k.turn = turn;
		k.side = side;
		const Frame f = frame(turn);
		reach(vessel, f, k.lo, k.hi);
		const glass::Point inner = brim_inner(vessel);
		const double lip_x = (side < 0 ? -1.0 : 1.0) * inner.r;
		k.lip_level = f.level(lip_x, inner.y);
		k.lip_holds = below(vessel, turn, k.lip_level);
		k.brim = std::fabs(lip_x);
		for (int i = 0; i <= N; ++i) {
			k.under[i] = below(vessel, turn, k.lo + (k.hi - k.lo) * i / N);
		}
		// slopes that keep the cubic rising (Fritsch and Carlson): the harmonic mean of the rises on either side
		for (int i = 0; i <= N; ++i) {
			const double before = i > 0 ? k.under[i] - k.under[i - 1] : k.under[1] - k.under[0], after = i < N ? k.under[i + 1] - k.under[i] : before;
			k.slope[i] = before > 0.0 && after > 0.0 ? 2.0 * before * after / (before + after) : 0.0;
		}
		t = &k;
	}
	double level = t->lo;
	if (liquid_cm3 >= t->under[N]) {
		level = t->hi;
	} else if (liquid_cm3 > 0.0) {
		int a = 0, b = N;
		while (b - a > 1) {
			const int mid = (a + b) / 2;
			(t->under[mid] <= liquid_cm3 ? a : b) = mid;
		}
		// where between the two levels the cubic through them holds the volume (it rises: halved)
		const double v0 = t->under[a], v1 = t->under[a + 1], s0 = t->slope[a], s1 = t->slope[a + 1];
		double lo = 0.0, hi = 1.0;
		for (int i = 0; i < 48; ++i) {
			const double u = 0.5 * (lo + hi), u2 = u * u, u3 = u2 * u;
			const double v = (2.0 * u3 - 3.0 * u2 + 1.0) * v0 + (u3 - 2.0 * u2 + u) * s0 + (-2.0 * u3 + 3.0 * u2) * v1 + (u3 - u2) * s1;
			(v < liquid_cm3 ? lo : hi) = u;
		}
		level = t->lo + (t->hi - t->lo) * (a + 0.5 * (lo + hi)) / N;
	}
	return PlainPour{ level - t->lip_level, t->lip_holds, t->brim };
}

double over_brim(double brim, double turn, double head) {
	if (!(head > 0.0) || !(brim > 0.0)) {
		return 0.0;
	}
	// the mouth's opening, from the lip (s = 0) across to its far edge (s = 2 brim): each place of it stands
	// s |sin turn| over the lip, and is 2 sqrt(s (2 brim - s)) wide; it is wet as far as the head reaches
	const double rise = std::fabs(std::sin(turn));
	const double wet = rise * 2.0 * brim > head ? head / rise : 2.0 * brim;
	// short of lying on its side the liquid leaves level, over the lip: through the opening as seen from the side
	const double seen = std::cos(turn) > 0.0 ? rise : 1.0;
	// in a from 0 to pi / 2 with s = wet sin^2 a: the integrand's square roots (at the lip, at the far edge, where
	// the head runs out) are then smooth
	static constexpr double NODE[4] = { -0.8611363115940526, -0.3399810435848563, 0.3399810435848563, 0.8611363115940526 };
	static constexpr double WEIGHT[4] = { 0.3478548451374538, 0.6521451548625461, 0.6521451548625461, 0.3478548451374538 };
	constexpr int STRETCHES = 8;
	double sum = 0.0;
	for (int k = 0; k < STRETCHES; ++k) {
		for (int n = 0; n < 4; ++n) {
			const double a = 0.5 * PI * (k + 0.5 + 0.5 * NODE[n]) / STRETCHES;
			const double sin_a = std::sin(a), cos_a = std::cos(a), s = wet * sin_a * sin_a;
			const double depth = std::max(head - s * rise, 0.0), across = std::max(2.0 * brim - s, 0.0);
			// (sqrt(s) = sqrt(wet) sin a, and ds = 2 wet sin a cos a da)
			sum += WEIGHT[n] * 0.5 * (0.5 * PI / STRETCHES) * std::sqrt(2.0 * GRAVITY * depth) * 2.0 * std::sqrt(wet) * sin_a * std::sqrt(across) * 2.0 * wet * sin_a * cos_a;
		}
	}
	return WEIR_CD * seen * sum;
}

Swirl swirl(const glass::Vessel &vessel, double turn, double liquid_cm3, double solid_cm3, double mass_g, double energy) {
	Swirl out;
	if (!(liquid_cm3 > 0.0) || !(mass_g > 0.0)) {
		return out;
	}
	// (kg, m/s^2 and m: J)
	const double weight = mass_g * 1e-3 * GRAVITY * 1e-2;
	const Lying full = lying(vessel, turn, liquid_cm3, solid_cm3, 1.0, 1);
	out.depth = full.depth;
	out.most = weight * full.depth * 1e-2 / 3.0;
	if (!(energy > 0.0) || !(out.most > 0.0)) {
		return out;
	}
	// its share: with the depth the liquid has under that vortex (a smaller vortex lifts the surface less): the
	// share s with s = cbrt(3 E / (W depth(s))), found by the secant on their difference from the share the full
	// depth gives (Stage 36: three or four searches of how the contents lie, where the fixed point took a dozen)
	const auto asks = [&](double share, bool &ok) {
		const Lying l = lying(vessel, turn, liquid_cm3, solid_cm3, share, 1);
		ok = l.depth > 0.0;
		return ok ? std::min(1.0, std::cbrt(3.0 * energy / (weight * l.depth * 1e-2))) : share;
	};
	out.share = std::min(1.0, std::cbrt(energy / out.most));
	bool ok = true;
	double a = out.share, fa = asks(a, ok) - a;
	if (!ok || !(out.share < 1.0)) {
		return out;
	}
	double b = std::min(1.0, a + fa), fb = 0.0;
	for (int pass = 0; pass < 12; ++pass) {
		fb = asks(b, ok) - b;
		if (!ok) {
			break;
		}
		out.share = b;
		if (std::fabs(fb) < 1e-7 || fb == fa) {
			break;
		}
		const double next = std::clamp(b - fb * (b - a) / (fb - fa), 0.0, 1.0);
		a = b;
		fa = fb;
		b = next;
	}
	out.share = std::min(1.0, out.share + (ok ? fb : 0.0));
	return out;
}

namespace {
Lying lying_made(const glass::Vessel &vessel, double turn, double liquid_cm3, double solid_cm3, double vortex, int side);
} // namespace

// (What was asked last is kept, on each thread: a vessel held turned over another is asked at every step of the
// lab's clock, 300 of them a frame at 600 times its speed, and each answer is a search of sixty volumes. While
// nothing in the vessel changes the question is the same to the bit, and so is the answer. Stage 33: a pour by
// hand at 600x ran at ten frames a second.)
Lying lying(const glass::Vessel &vessel, double turn, double liquid_cm3, double solid_cm3, double vortex, int side) {
	struct Asked {
		const glass::Vessel *vessel = nullptr;
		double turn = 0.0, liquid = 0.0, solid = 0.0, vortex = 0.0;
		int side = 0;
		Lying answer;
	};
	thread_local Asked last;
	if (last.vessel != &vessel || last.turn != turn || last.liquid != liquid_cm3 || last.solid != solid_cm3 || last.vortex != vortex || last.side != side) {
		last.answer = lying_made(vessel, turn, liquid_cm3, solid_cm3, vortex, side);
		last.vessel = &vessel;
		last.turn = turn;
		last.liquid = liquid_cm3;
		last.solid = solid_cm3;
		last.vortex = vortex;
		last.side = side;
	}
	return last.answer;
}

namespace {

// The sediment's lie at a turn (GAME_SPEC §11): its flat section and its pile, from the vessel, its turn and the
// solids' volume alone, whatever liquid or vortex is over it. (Kept apart and the last few kept, Stage 36: a
// vortex's size is found by trying a dozen, each a search of the liquid over the same sediment, and the sediment's
// own search is a thousand sections. The answer is the same to the bit.)
struct Settled {
	bool solid = false;
	double level = 0.0;
	Cone pile;
};
Settled settled_made(const glass::Vessel &vessel, double turn, double solid_cm3, double lowest, double highest) {
	Settled out;
	const double span = highest - lowest;
	const double bulk = solid_cm3 > 0.0 ? solid_cm3 / PACKING : 0.0;
	if (bulk > 0.0) {
		out.solid = true;
		// for a peak at a level: the flat section under it (the lowest whose pile, at repose, reaches the peak),
		// the pile, and what the two hold
		const auto heap = [&](double peak, double &flat, Cone &pile) {
			const auto short_of = [&](double h) { // how far the steepest pile on the section at h falls short of the peak
				const Cut c = cut(vessel, turn, h);
				return (peak - h) - REPOSE * (c.any ? std::min(c.half, c.deep()) : 0.0);
			};
			const double first = lowest + 1e-6 * span;
			flat = first;
			if (short_of(first) > 0.0) {
				double lo = first, hi = peak;
				constexpr int N = 24;
				for (int i = 1; i <= N; ++i) {
					const double h = first + (peak - first) * i / N;
					if (short_of(h) <= 0.0) {
						hi = h;
						break;
					}
					lo = h;
				}
				for (int i = 0; i < 24; ++i) {
					const double mid = 0.5 * (lo + hi);
					(short_of(mid) > 0.0 ? lo : hi) = mid;
				}
				flat = hi;
			}
			const Cut c = cut(vessel, turn, flat);
			pile = Cone{};
			if (c.any && peak > flat) {
				pile.height = peak - flat;
				pile.x = c.mid_x;
				pile.y = c.mid_y;
				pile.across = c.half;
				pile.deep = c.deep();
			}
			return below(vessel, turn, flat) + pile.volume();
		};
		// (the bulk a peak holds, at 32 peaks from the lowest to the highest, kept for the turn: a sediment's peak
		// is then between two of them, and found there by false position with Illinois's guard, to the closeness
		// halving from the whole height came to in forty. Stage 36: while a stirred vessel pours, what lies as
		// sediment changes at every step.)
		constexpr int PEAKS = 32;
		struct Table {
			const glass::Vessel *vessel = nullptr;
			double turn = 0.0;
			std::array<double, PEAKS + 1> bulk{};
		};
		thread_local std::array<Table, 4> tables;
		thread_local size_t next = 0;
		const Table *t = nullptr;
		for (const Table &k : tables) {
			if (k.vessel == &vessel && k.turn == turn) {
				t = &k;
			}
		}
		double flat = lowest;
		Cone pile;
		if (t == nullptr) {
			Table &k = tables[next];
			next = (next + 1) % tables.size();
			k.vessel = &vessel;
			k.turn = turn;
			for (int i = 0; i <= PEAKS; ++i) {
				k.bulk[static_cast<size_t>(i)] = i == 0 ? 0.0 : heap(lowest + span * i / PEAKS, flat, pile);
			}
			t = &k;
		}
		double lo = lowest, hi = highest;
		if (t->bulk[PEAKS] <= bulk) {
			lo = hi; // more than the vessel holds: it is full of it
		} else {
			int i = 0;
			while (i < PEAKS - 1 && t->bulk[static_cast<size_t>(i + 1)] < bulk) {
				++i;
			}
			lo = lowest + span * i / PEAKS;
			hi = lowest + span * (i + 1) / PEAKS;
			double f_lo = t->bulk[static_cast<size_t>(i)] - bulk, f_hi = t->bulk[static_cast<size_t>(i + 1)] - bulk;
			int last = 0;
			for (int k = 0; k < 60 && hi - lo > 1e-9 * span; ++k) {
				double mid = f_hi != f_lo ? hi - f_hi * (hi - lo) / (f_hi - f_lo) : 0.5 * (lo + hi);
				if (!(mid > lo && mid < hi) || k % 8 == 7) {
					mid = 0.5 * (lo + hi);
				}
				const double f_mid = heap(mid, flat, pile) - bulk;
				if (f_mid < 0.0) {
					lo = mid;
					f_lo = f_mid;
					f_hi = last == -1 ? 0.5 * f_hi : f_hi;
					last = -1;
				} else {
					hi = mid;
					f_hi = f_mid;
					f_lo = last == 1 ? 0.5 * f_lo : f_lo;
					last = 1;
				}
			}
		}
		heap(0.5 * (lo + hi), flat, pile);
		out.level = flat;
		out.pile = pile;
	} else {
		out.level = lowest;
	}
	return out;
}
Settled settled(const glass::Vessel &vessel, double turn, double solid_cm3, double lowest, double highest) {
	struct Asked {
		const glass::Vessel *vessel = nullptr;
		double turn = 0.0, solid = 0.0;
		Settled answer;
	};
	thread_local std::array<Asked, 4> kept;
	thread_local size_t next = 0;
	for (const Asked &a : kept) {
		if (a.vessel == &vessel && a.turn == turn && a.solid == solid_cm3) {
			return a.answer;
		}
	}
	Asked &a = kept[next];
	next = (next + 1) % kept.size();
	a.vessel = &vessel;
	a.turn = turn;
	a.solid = solid_cm3;
	a.answer = settled_made(vessel, turn, solid_cm3, lowest, highest);
	return a.answer;
}

Lying lying_made(const glass::Vessel &vessel, double turn, double liquid_cm3, double solid_cm3, double vortex, int side) {
	Lying l;
	const Frame f = frame(turn);
	l.turn = turn;
	l.up_x = f.up_x();
	l.up_y = f.up_y();
	reach(vessel, f, l.lowest, l.highest);
	const double span = l.highest - l.lowest;
	const glass::Point inner = brim_inner(vessel), outer = brim_outer(vessel);
	const double sense = side < 0 ? -1.0 : 1.0;
	l.lip_x = sense * inner.r;
	l.lip_y = inner.y;
	l.tip_x = sense * outer.r;
	l.tip_y = outer.y;
	l.lip_level = f.level(l.lip_x, l.lip_y);

	// ---- the sediment: as peaked as the angle of repose allows; its peak's height is what holds its bulk
	const Settled sediment = settled(vessel, turn, solid_cm3, l.lowest, l.highest);
	l.solid = sediment.solid;
	l.solid_level = sediment.level;
	l.pile = sediment.pile;
	l.solid_head = l.solid ? l.solid_level - l.lip_level : -span;

	// ---- the liquid: its level is where the inside under it, less the powder under it and less the vortex,
	// holds it
	if (liquid_cm3 > 0.0) {
		l.liquid = true;
		const double share = std::min(std::max(vortex, 0.0), 1.0);
		const auto vortex_at = [&](double level, Cone &cone, double &area, double &depth) {
			const Cut c = cut(vessel, turn, level);
			area = c.area;
			depth = depth_under(vessel, l, c, level);
			cone = Cone{};
			if (c.any && share > 0.0) {
				cone.height = share * depth;
				cone.x = c.mid_x;
				cone.y = c.mid_y;
				cone.across = share * c.half;
				cone.deep = share * c.deep();
			}
			return cone.volume();
		};
		const auto holds = [&](double level) {
			Cone cone;
			double area, depth;
			return below(vessel, turn, level) - PACKING * bulk_under(vessel, l, level) - vortex_at(level, cone, area, depth);
		};
		double lo = l.lowest, hi = l.highest;
		if (holds(hi) <= liquid_cm3) {
			lo = hi;
		}
		// (by false position with Illinois's guard, to the same closeness as halving came to: what it holds rises
		// smoothly with the level, and some ten volumes find it where halving took forty. Stage 33: a pour by
		// hand asks at every step of the lab's clock.)
		if (hi > lo) {
			double f_lo = holds(lo) - liquid_cm3, f_hi = holds(hi) - liquid_cm3;
			int last = 0;
			for (int i = 0; i < 80 && hi - lo > 1e-12 * span; ++i) {
				double mid = f_hi != f_lo ? hi - f_hi * (hi - lo) / (f_hi - f_lo) : 0.5 * (lo + hi);
				if (!(mid > lo && mid < hi) || i % 8 == 7) {
					mid = 0.5 * (lo + hi); // (and a halving now and then, where the curve has a corner)
				}
				const double f_mid = holds(mid) - liquid_cm3;
				if (f_mid < 0.0) {
					lo = mid;
					f_lo = f_mid;
					f_hi = last == -1 ? 0.5 * f_hi : f_hi;
					last = -1;
				} else {
					hi = mid;
					f_hi = f_mid;
					f_lo = last == 1 ? 0.5 * f_lo : f_lo;
					last = 1;
				}
			}
		}
		l.liquid_level = 0.5 * (lo + hi);
		vortex_at(l.liquid_level, l.vortex, l.surface_area, l.depth);
	} else {
		l.liquid_level = l.lowest;
	}
	l.head = l.liquid ? l.liquid_level - l.lip_level : -span;
	return l;
}

} // namespace

std::string check_H1(const glass::Vessel &vessel, const Lying &l, double liquid_cm3, double solid_cm3) {
	char buffer[240];
	const double whole = vessel.capacity();
	if (l.solid) {
		const double holds = below(vessel, l.turn, l.solid_level) + l.pile.volume(), bulk = solid_cm3 / PACKING;
		if (std::fabs(holds - std::min(bulk, whole)) > 1e-4 * whole) {
			std::snprintf(buffer, sizeof buffer, "H1: the sediment's flat section and pile hold %.6f cm3, its bulk is %.6f", holds, bulk);
			return buffer;
		}
	}
	if (l.liquid) {
		const double under = below(vessel, l.turn, l.liquid_level);
		const double holds = under - PACKING * bulk_under(vessel, l, l.liquid_level) - l.vortex.volume();
		if (std::fabs(holds - liquid_cm3) > 1e-4 * whole && under < whole * (1.0 - 1e-9)) {
			std::snprintf(buffer, sizeof buffer, "H1: under its surface the liquid has room for %.6f cm3, and is %.6f", holds, liquid_cm3);
			return buffer;
		}
	}
	return "";
}

std::string check_H2(const Lying &l) {
	char buffer[240];
	if (l.solid && l.pile.height > REPOSE * std::min(l.pile.across, l.pile.deep) * (1.0 + 1e-6) + 1e-9) {
		std::snprintf(buffer, sizeof buffer, "H2: a pile %.6f cm high stands on a base %.6f by %.6f cm: steeper than the angle of repose", l.pile.height, l.pile.across, l.pile.deep);
		return buffer;
	}
	if (l.pile.height < 0.0 || l.vortex.height < 0.0) {
		return "H2: a cone of less than no height";
	}
	if (l.liquid && l.vortex.height > l.depth * (1.0 + 1e-9) + 1e-12) {
		std::snprintf(buffer, sizeof buffer, "H2: a vortex %.6f cm deep in %.6f cm of liquid", l.vortex.height, l.depth);
		return buffer;
	}
	if (l.liquid && l.surface_area > 0.0 && l.vortex.base_area() > l.surface_area * (1.0 + 1e-6)) {
		std::snprintf(buffer, sizeof buffer, "H2: a vortex %.6f cm2 wide in a surface of %.6f cm2", l.vortex.base_area(), l.surface_area);
		return buffer;
	}
	return "";
}

} // namespace lab::contents
