// The reference renderer: the per-pixel source compiled as C++, in float64 (GAME_SPEC §12, §12.2).
#include "lab/optics_scene.hpp"
#include "lab/optics_trace.hpp"
#include "chem/optics.hpp"

#include "lab/colour.hpp"
#include "optics_frames.hpp"

#include <algorithm>
#include <tuple>
#include <map>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <array>
#include <atomic>
#include <thread>
#include <unordered_map>
#include <vector>

namespace lab {

namespace shader {

// what the shading language has and C++ has not, in float64
using real = double;
inline double to_double(double a) {
	return a;
}
inline double min(double a, double b) {
	return a < b ? a : b;
}
inline double max(double a, double b) {
	return a > b ? a : b;
}
inline int min(int a, int b) {
	return a < b ? a : b;
}
inline int max(int a, int b) {
	return a > b ? a : b;
}
inline int clamp(int v, int lo, int hi) {
	return v < lo ? lo : v > hi ? hi : v;
}
inline double abs(double a) {
	return std::fabs(a);
}
inline double sqrt(double a) {
	return std::sqrt(a);
}
inline double exp(double a) {
	return std::exp(a);
}
inline double log(double a) {
	return std::log(a);
}
inline double pow(double a, double b) {
	return std::pow(a, b);
}
inline double sin(double a) {
	return std::sin(a);
}
inline double cos(double a) {
	return std::cos(a);
}
inline double acos(double a) {
	return std::acos(a);
}
inline double asin(double a) {
	return std::asin(a);
}
inline double atan(double y, double x) {
	return std::atan2(y, x);
}
inline double floor(double a) {
	return std::floor(a);
}
inline double fract(double a) {
	return a - std::floor(a);
}
inline double clamp(double v, double lo, double hi) {
	return v < lo ? lo : v > hi ? hi : v;
}
#define OPTICS_HOST_NAMESPACE shader
#define OPTICS_HOST_SHAPES(scene) (scene).shapes_exact
#define OPTICS_HOST_BETWEEN 0.02
#define TRACE_NEAR 1.0e-9
#define TRACE_NUDGE 1.0e-7
#define TRACE_SLACK 1.0e-9
#define TRACE_FINE 1.0e-13
#define TRACE_ROUGH 1.0e-9
#include "optics_pixel_host.inc"

} // namespace shader

double optics_through(const OpticsScene &scene, int x, int y, double nm) {
	if (!shader::ready(scene)) {
		return 0.0;
	}
	const shader::Use use(scene);
	return shader::band_light(shader::at(x, y), 0.0, 0.0, 0.0, nm);
}

double optics_point(const OpticsScene &scene, double x, double y, double nm) {
	if (!shader::ready(scene)) {
		return 0.0;
	}
	const shader::Use use(scene);
	return shader::point_light(shader::vec2(x, y), nm);
}

void optics_point_key(const OpticsScene &scene, double x, double y, double size, double key[4]) {
	key[0] = key[1] = key[2] = key[3] = 0.0;
	if (!shader::ready(scene)) {
		return;
	}
	const shader::Use use(scene);
	const shader::Path q = shader::path_of(shader::vec2(x, y), size);
	key[0] = q.key.x;
	key[1] = q.key.y;
	key[2] = q.key.z;
	key[3] = q.key.w + 0.31 * q.shape.x + 0.37 * q.shape.y + 0.41 * q.content.x + 0.43 * q.content.y + 0.47 * q.back + 0.53 * shader::wall_vec(0).z;
}

double optics_band(const OpticsScene &scene, int x, int y, double u0, double u1, double t) {
	if (!shader::ready(scene)) {
		return 0.0;
	}
	const shader::Use use(scene);
	return shader::band_light(shader::at(x, y), u0, u1, t, 0.0);
}

double optics_share_nm(const OpticsScene &scene, double u) {
	if (!shader::ready(scene)) {
		return 0.0;
	}
	const shader::Use use(scene);
	return shader::share(u).x + 380.0;
}

double optics_cover(const OpticsScene &scene, int x, int y) {
	if (!shader::ready(scene)) {
		return 0.0;
	}
	const shader::Use use(scene);
	return shader::at(x, y).cover;
}

namespace {

// the shares of the light the truth is read at: all of them where narrow lines are, else a quarter (the
// room's light and every continuum vary over nanometres, not over the table's tenths of one)
int truth_shares(const OpticsScene &scene) {
	for (size_t i = 1; i < scene.lines.size(); i += 4) {
		if (scene.lines[i] > 0.0f) {
			return 4096;
		}
	}
	return 1024;
}

} // namespace

OpticsBend optics_bend(const OpticsScene &scene, int x, int y, double nm) {
	if (!shader::ready(scene)) {
		return OpticsBend{};
	}
	const shader::Use use(scene);
	const shader::Path q = shader::at(x, y);
	if (q.refractor < 0) {
		return OpticsBend{ q.x, 1.0, 0.0 };
	}
	// its own ray, traced (Stage 29)
	const shader::RayMet m = shader::ray_met(shader::vec2(q.x, q.y), nm);
	OpticsBend traced{ m.x, 1.0, 0.0 };
	bool first = true;
	for (int k = 0; k < m.count; ++k) {
		const shader::vec4 slot = shader::met_slot(m, k);
		if (slot.y > 0.5) {
			traced.pass *= slot.z;
			traced.chord = first ? slot.w : traced.chord; // its chord in the nearest body that bent it
			first = false;
		}
	}
	return traced;
}


// ---- the truth of a pixel's area (GAME_SPEC §12.3, Stage 20): its own tracer, not the renderer's

namespace {

// A ray on its way, in the one world of the screen's pixels: x across the screen, y down it, z away from the
// eye (0: the bench's plane), and its direction (Stage 24: rays bend up and down as well as across).
struct Line {
	double x = 0.0, y = 0.0, z = 0.0, ux = 0.0, uy = 0.0, uz = 1.0;
};

// What one body does to a ray, traced exactly in three dimensions by the general tracer (vector Snell,
// Fresnel at every crossing, turned back where it cannot pass), through the body's own surfaces: its
// glassware's whole profile, or a glass cylinder, or the prism's faces. Independent of the renderer's beams
// and footprints: it takes a ray from anywhere.
struct Exact {
	bool met = false; // the ray met the body
	double entry = 0.0; // how far along the line, from where it was given (pixels)
	double pass = 1.0, chord = 0.0; // what its surfaces passed; its path in the liquid (cm)
	double absorbed = 0.0, turned = 0.0; // what its glass took in, and its surfaces turned away (Stage 46)
	int meetings = 0;
	bool out = true; // it left the body (not still inside after every meeting allowed)
	uint32_t route = 0; // the surfaces it crossed
	bool solid = false; // it ended in the body's sediment (Stage 30)
};

// `line` through body `body`, whose axis stands at the depth z_axis, its core its liquid (0), the air over
// it (1) or glass all through (2: a cylinder's floor; glassware's floor is in its shape); the line it leaves
// on replaces `line`.
Exact exact_body(int body, int core, double nm, double z_axis, Line &line) {
	Exact out;
	using shader::row_at;
	using shader::scene_vec;
	const int at = optics_layout::HEADER_VECS + body * optics_layout::BODY_VECS;
	const shader::vec4 form = scene_vec(at + 1);
	// where it stands upright, how it is turned and what lies in it (Stage 30: the shapes' head)
	const double *block = shader::current->shapes_exact.size() >= static_cast<size_t>(body + 1) * optics_layout::SHAPE_BLOCK * 4 ? &shader::current->shapes_exact[static_cast<size_t>(body) * optics_layout::SHAPE_BLOCK * 4] : nullptr;
	const shader::vec4 held = scene_vec(at);
	const double rect_x = block ? block[8] : held.x, rect_y = block ? block[9] : held.y, rect_w = block ? block[10] : held.z, rect_h = block ? block[11] : held.w;
	// (what lies in it is traced where the scene gives it: its liquid's surface, its sediment)
	const bool turned = block != nullptr && block[3] != 0.0, lies = block != nullptr && block[2] > 0.5;
	const bool liquid = core == 0 || lies;
	const bool prism = form.x > 1.5;
	const double cm = form.y, rr = rect_w * 0.5 * cm, rt = scene_vec(at + 5).x;
	const double axis = rect_x + rect_w * 0.5 + (prism ? scene_vec(at + 3).z : 0.0), foot = rect_y + rect_h;
	const int start = shader::set_of(body) * optics_layout::ROWS_PER_SET * optics_layout::ROW;
	const double n_glass = row_at(start + 4 * optics_layout::ROW, nm);
	const double n_in = liquid ? row_at(start + 3 * optics_layout::ROW, nm) : core == 2 ? n_glass : 1.0;
	const std::vector<const glass::Vessel *> &vessels = shader::current->vessels;
	const glass::Vessel *made = static_cast<size_t>(body) < vessels.size() ? vessels[static_cast<size_t>(body)] : nullptr;
	trace::Shape own;
	const trace::Shape *shape = &own;
	if (made) {
		shape = &trace::revolved_of(*made);
	} else if (prism) {
		const shader::vec4 m = scene_vec(at + 2), g = scene_vec(at + 3);
		const double corners[3][2] = { { m.x, m.y }, { m.z, m.w }, { g.x, g.y } };
		own = trace::prism(corners);
	} else {
		own = trace::cylinder(rr, rt);
	}
	// prism: 0 air, 1 glass; a vessel: 0 air, 1 glass, 2 its core (its liquid, where what lies in it is given), 3 the
	// air over its liquid
	const std::array<double, trace::MAX_MEDIA> index{ 1.0, n_glass, prism ? n_glass : n_in, 1.0 };
	trace::Inside inside;
	if (lies) {
		inside.up_x = block[12];
		inside.up_y = block[13];
		inside.liquid = block[14] > 0.5;
		inside.liquid_level = block[15];
		inside.vortex = block[16];
		inside.vortex_across = block[17];
		inside.vortex_deep = block[18];
		inside.solid = block[19] > 0.5;
		inside.vortex_x = block[20];
		inside.vortex_y = block[21];
		inside.solid_level = block[22];
		inside.pile = block[23];
		inside.pile_across = block[24];
		inside.pile_deep = block[25];
		inside.pile_x = block[26];
		inside.pile_y = block[27];
	}
	// from well before the body, on the line; the body's frame: cm from its axis, y up from its foot. Turned
	// (Stage 30; upside down is half a turn), the line is turned back about the pivot first, and the line it
	// leaves on turned forward again.
	const double back = 1000.0;
	const double pivot_x = turned ? block[4] : 0.0, pivot_y = turned ? block[5] : 0.0, c = turned ? block[6] : 1.0, s = turned ? block[7] : 0.0;
	// upside down by the scene's own mark, where no head says how it is turned (a scene laid out by hand)
	const bool flip = !turned && block == nullptr && scene_vec(at + 7).w > 0.5;
	const double base = flip ? rect_y : foot, sense = flip ? -1.0 : 1.0;
	const double lx = pivot_x + (line.x - pivot_x) * c + (line.y - pivot_y) * s, ly = pivot_y - (line.x - pivot_x) * s + (line.y - pivot_y) * c;
	const double lux = line.ux * c + line.uy * s, luy = -line.ux * s + line.uy * c;
	const trace::Vec3 u{ lux, -sense * luy, line.uz };
	const trace::Vec3 o{ (lx - axis) * cm - back * u.x, sense * (base - ly) * cm - back * u.y, (line.z - z_axis) * cm - back * u.z };
	// what its glass takes in per cm (Stage 46: nothing in the visible); a prism's core is its glass, and so is a
	// cylinder's floor
	const double alpha = row_at(start + 2 * optics_layout::ROW, nm);
	const std::array<double, trace::MAX_MEDIA> absorb{ 0.0, alpha, prism || core == 2 ? alpha : 0.0, 0.0 };
	const trace::Landing l = trace::trace(*shape, o, u, 1.0e9, index, lies ? &inside : nullptr, &absorb);
	out.meetings = l.meetings;
	if (l.meetings == 0) {
		return out;
	}
	out.met = true;
	out.entry = (l.chord[0] - back) / cm;
	out.pass = l.pass;
	out.absorbed = l.absorbed;
	out.turned = l.turned;
	out.chord = liquid && !prism ? l.chord[trace::CORE] : 0.0;
	out.out = l.left;
	out.route = l.route;
	out.solid = l.solid;
	const double ex = axis + l.at.x / cm, ey = base - sense * l.at.y / cm, eux = l.toward.x, euy = -sense * l.toward.y;
	line = Line{ pivot_x + (ex - pivot_x) * c - (ey - pivot_y) * s, pivot_y + (ex - pivot_x) * s + (ey - pivot_y) * c, z_axis + l.at.z / cm, eux * c - euy * s, eux * s + euy * c, l.toward.z };
	return out;
}

// One ray of the room's light, from the eye straight in at (x, y), followed exactly through the one world
// (GAME_SPEC §12.3): layer by layer, the nearest first; every body of a layer where the ray's line, as it
// arrives, crosses the layer's plane; the layer's body that bends, the first the line meets, traced by
// exact_body. What it met, in the order it met it, and where it lands on the wall (across and up).
struct Met {
	int body = 0;
	double cross = 0.0, cross_y = 0.0;
	bool bent = false;
	double pass = 1.0, chord = 0.0;
	uint32_t route = 0;
	int core = 0;
	bool solid = false; // it ended in this body's sediment (Stage 30)
	double absorbed = 0.0; // what its glass took in (Stage 46)
};
struct ExactRay {
	double x = 0.0, y = 0.0;
	std::vector<Met> met;
	int ways = 1; // in how many ways it could go on from a grating it met
};

// (a grating, Stage 29: the ray goes on in the order of `way`, turned across the ruling by that order times the
// wavelength over the pitch; the shader's own account of it is in native/game/shader/optics_pixel.glsl)
ExactRay exact_ray(double x, double y, double nm, int way = 0) {
	using shader::scene_vec;
	ExactRay out;
	const int count = static_cast<int>(scene_vec(0).x);
	const double wall = scene_vec(2).x, gap = scene_vec(3).x;
	const auto vec_of = [](int body, int which) { return scene_vec(optics_layout::HEADER_VECS + body * optics_layout::BODY_VECS + which); };
	const auto bends = [&](int body) { return vec_of(body, 5).w > 0.5 && (vec_of(body, 1).x < 0.5 || vec_of(body, 1).x > 1.5); };
	const auto in_rows = [&](int body, double row) { return row >= vec_of(body, 0).y && row <= static_cast<double>(vec_of(body, 0).y) + vec_of(body, 0).w; };
	const std::vector<const glass::Vessel *> &vessels = shader::current->vessels;
	// what a body is in a row: its floor (a cylinder's: glass all through), its liquid, or the air over it
	const auto core_in = [&](int body, double row) {
		const bool made = static_cast<size_t>(body) < vessels.size() && vessels[static_cast<size_t>(body)] != nullptr;
		const double foot = static_cast<double>(vec_of(body, 0).y) + vec_of(body, 0).w, floor_px = static_cast<double>(vec_of(body, 5).z) / vec_of(body, 1).y;
		return !made && floor_px > 0.0 && row >= foot - floor_px ? 2 : row >= vec_of(body, 1).z ? 0 : 1;
	};
	Line line{ x, y, -1.0e5, 0.0, 0.0, 1.0 };
	bool lands = true, cut_off = false;
	for (int last = count - 1; last >= 0 && lands;) {
		const double layer = vec_of(last, 5).y, z = -layer * gap;
		int first = last;
		while (first > 0 && vec_of(first - 1, 5).y == layer) {
			--first;
		}
		const double cross = line.x + (z - line.z) * line.ux / line.uz, cross_y = line.y + (z - line.z) * line.uy / line.uz;
		// the layer's body that bends which the line meets first
		int hit = -1;
		double nearest = 1e30;
		for (int k = first; k <= last; ++k) {
			if (bends(k) && in_rows(k, cross_y)) {
				Line trial = line;
				const Exact e = exact_body(k, core_in(k, cross_y), nm, z, trial);
				if (e.met && e.meetings > 0 && e.entry < nearest) {
					nearest = e.entry;
					hit = k;
				}
			}
		}
		for (int k = last; k >= first; --k) {
			if (k == hit) {
				const Exact e = exact_body(k, core_in(k, cross_y), nm, z, line);
				out.met.push_back(Met{ k, cross, cross_y, true, e.pass, e.chord, e.route + (e.out && line.uz > 0.05 ? 0u : 0x80000000u), core_in(k, cross_y), e.solid, e.absorbed });
				if (!e.out || line.uz <= 0.05) {
					lands = false;
					out.x = 100000.0;
					out.y = line.y;
					break;
				}
			} else if (!bends(k)) {
				const shader::vec4 rect = vec_of(k, 0);
				if (vec_of(k, 1).x > 5.5 && cross >= rect.x && cross <= static_cast<double>(rect.x) + rect.z && cross_y >= rect.y && cross_y <= static_cast<double>(rect.y) + rect.w) {
					out.ways = shader::GRATING_WAYS;
					const double across = line.ux + shader::way_order(way) * nm / std::max(static_cast<double>(vec_of(k, 2).x), 1.0), sine2 = across * across + line.uy * line.uy;
					line.x = cross;
					line.y = cross_y;
					line.z = z;
					if (sine2 >= 0.9975) {
						// an order that does not leave the grating
						out.met.push_back(Met{ k, cross, cross_y, false, 0.0, 0.0, 0u, 0 });
						lands = false;
						cut_off = true;
						out.x = 100000.0;
						out.y = line.y;
						break;
					}
					line.ux = across;
					line.uz = std::sqrt(1.0 - sine2);
					out.met.push_back(Met{ k, cross, cross_y, false, shader::GRATING_PASS, 0.0, 0u, 0 });
				} else {
					out.met.push_back(Met{ k, cross, cross_y, false, 1.0, 0.0, 0u, 0 });
				}
			}
		}
		last = first - 1;
	}
	if (const double side = shader::scene_vec(3).z; side > 0.0 && !cut_off) {
		// in a room (Stage 33): to whichever face it comes to, as the pass's own ray does
		const shader::vec3 comes = shader::room_exit(shader::vec3(line.x, line.y, line.z), shader::vec3(line.ux, line.uy, line.uz), wall, side, shader::scene_vec(3).w);
		if (comes.x < 0.5) {
			out.x = comes.y;
			out.y = comes.z;
		} else {
			out.x = comes.x > 1.5 ? -100000.0 : 100000.0;
			out.y = line.y;
		}
	} else if (lands) {
		out.x = line.x + (wall - line.z) * line.ux / line.uz;
		out.y = line.y + (wall - line.z) * line.uy / line.uz;
	}
	return out;
}

// the wall at a point, from what is drawn on it (not from the renderer's table): the bench or the room,
// then each shape in painting order
double drawn_at(double x, double y) {
	if (shader::band_range == 1) {
		return 1.0; // the long-wave room is one black body (Stage 46)
	}
	// (as the wall's table has it: far off to either side, where a ray that never reaches the wall is put; and
	// beyond the screen's edges, the wall's plane as the room says. Stage 33: the truth read the darkness beside
	// the bench at both, where the pass read the room.)
	const OpticsScene &scene = *shader::current;
	if (std::fabs(x) >= 5.0e4) {
		return x < 0.0 ? scene.far_left : scene.far_right;
	}
	if (scene.wall_beyond >= 0.0 && (x < 0.0 || x >= optics_layout::WALL_COLUMNS || y < 0.0 || y >= optics_layout::WALL_ROWS)) {
		return scene.wall_beyond;
	}
	const shader::vec4 head = shader::scene_vec(0), bench = shader::scene_vec(1);
	double seen = x >= bench.x && x < bench.x + bench.z && y >= bench.y && y < bench.y + bench.w ? head.y : head.z;
	const std::vector<float> &d = shader::current->drawn;
	for (size_t k = 0; k + 4 < d.size(); k += 5) {
		if (x >= d[k] && x < static_cast<double>(d[k]) + d[k + 2] && y >= d[k + 1] && y < static_cast<double>(d[k + 1]) + d[k + 3]) {
			seen = d[k + 4];
		}
	}
	return seen;
}

// the wall over a small square of the screen, exactly: what is drawn is rectangles, so the square is cut at
// their edges into cells, each one radiance, read by drawn_at at its middle (Stage 22: a line thinner than
// the truth's own samples was counted by how many of them it caught)
double drawn_over(double x0, double x1, double y0, double y1) {
	if (shader::band_range == 1) {
		return 1.0; // the long-wave room is one black body (Stage 46)
	}
	const shader::vec4 bench = shader::scene_vec(1);
	std::vector<double> xs{ x0, x1 }, ys{ y0, y1 };
	const auto cut = [](std::vector<double> &at, double lo, double hi, double v) {
		if (v > lo && v < hi) {
			at.push_back(v);
		}
	};
	cut(xs, x0, x1, bench.x);
	cut(xs, x0, x1, bench.x + bench.z);
	cut(ys, y0, y1, bench.y);
	cut(ys, y0, y1, bench.y + bench.w);
	for (const double edge : { 0.0, static_cast<double>(optics_layout::WALL_COLUMNS) }) {
		cut(xs, x0, x1, edge); // (the screen's own edges: beyond them is the room's plane)
	}
	for (const double edge : { 0.0, static_cast<double>(optics_layout::WALL_ROWS) }) {
		cut(ys, y0, y1, edge);
	}
	const std::vector<float> &d = shader::current->drawn;
	for (size_t k = 0; k + 4 < d.size(); k += 5) {
		if (d[k] < x1 && d[k] + d[k + 2] > x0 && d[k + 1] < y1 && d[k + 1] + d[k + 3] > y0) {
			cut(xs, x0, x1, d[k]);
			cut(xs, x0, x1, static_cast<double>(d[k]) + d[k + 2]);
			cut(ys, y0, y1, d[k + 1]);
			cut(ys, y0, y1, static_cast<double>(d[k + 1]) + d[k + 3]);
		}
	}
	std::sort(xs.begin(), xs.end());
	std::sort(ys.begin(), ys.end());
	double sum = 0.0;
	for (size_t i = 0; i + 1 < xs.size(); ++i) {
		for (size_t j = 0; j + 1 < ys.size(); ++j) {
			const double w = (xs[i + 1] - xs[i]) * (ys[j + 1] - ys[j]);
			if (w > 0.0) {
				sum += w * drawn_at(0.5 * (xs[i] + xs[i + 1]), 0.5 * (ys[j] + ys[j + 1]));
			}
		}
	}
	return sum / ((x1 - x0) * (y1 - y0));
}

// A screen's light over a sample (Stage 42; GAME_SPEC §12.1: image weights), by the truth's own reading: each of
// its pixels' subpixels in turn, its weight as the screen drives it (the scene's image_weights) times how much of
// the sample it covers, times its primary's light; as a multiple of the room's light over the part of the sample
// the screen covers (`cover`), as a layer's glow is.
double truth_image_glow(const OpticsScene &scene, int body, double px, double py, double across, double up, double nm, double cover) {
	using namespace optics_layout;
	if (!(cover > 0.0)) {
		return 0.0;
	}
	const float *rect = &scene.table[static_cast<size_t>(HEADER_VECS + body * BODY_VECS + RECT) * 4];
	const int img = static_cast<int>(scene.table[static_cast<size_t>(HEADER_VECS + body * BODY_VECS + MORE) * 4]);
	const float *head = &scene.images[static_cast<size_t>(img) * 4];
	const int w = static_cast<int>(head[0]), h = static_cast<int>(head[1]);
	const double fill_x = head[2], fill_y = head[3];
	const size_t weights = static_cast<size_t>(head[6]);
	const std::array<std::array<double, 401>, 3> &light = optics_screen_primaries();
	const int k = std::clamp(static_cast<int>(std::floor(nm - 380.0)), 0, 399);
	const double t = std::clamp(nm - 380.0 - k, 0.0, 1.0);
	// the sample in the image's pixels
	const double pw = static_cast<double>(rect[2]) / w, ph = static_cast<double>(rect[3]) / h;
	const double x0 = (px - 0.5 * across - rect[0]) / pw, x1 = (px + 0.5 * across - rect[0]) / pw;
	const double y0 = (py - 0.5 * up - rect[1]) / ph, y1 = (py + 0.5 * up - rect[1]) / ph;
	const auto overlap = [](double a0, double a1, double b0, double b1) { return std::max(0.0, std::min(a1, b1) - std::max(a0, b0)); };
	double sum = 0.0;
	for (int j = std::max(0, static_cast<int>(std::floor(y0))); j < h && j <= static_cast<int>(std::floor(y1)); ++j) {
		const double down = overlap(y0, y1, j + 0.5 * (1.0 - fill_y), j + 0.5 * (1.0 + fill_y));
		if (down <= 0.0) {
			continue;
		}
		for (int i = std::max(0, static_cast<int>(std::floor(x0))); i < w && i <= static_cast<int>(std::floor(x1)); ++i) {
			for (int c = 0; c < 3; ++c) {
				// subpixel c: the c-th third of the pixel across, fill of it lit, its gaps evenly on each side
				const double third = 1.0 / 3.0, gap = 0.5 * third * (1.0 - fill_x);
				const double across_lit = overlap(x0, x1, i + c * third + gap, i + (c + 1) * third - gap);
				const double weight = scene.image_weights[weights + (static_cast<size_t>(j) * static_cast<size_t>(w) + static_cast<size_t>(i)) * 3 + static_cast<size_t>(c)];
				const double primary = light[static_cast<size_t>(c)][static_cast<size_t>(k)] * (1.0 - t) + light[static_cast<size_t>(c)][static_cast<size_t>(k + 1)] * t;
				sum += weight * across_lit * down * primary;
			}
		}
	}
	return sum / ((x1 - x0) * (y1 - y0) * cover);
}

// what one ray of the room's light brings at nm, at the point p of the screen (`size` across, for the
// bodies' outlines): the truth's own tracing through every body, and its own reading of the wall; each body
// read where the ray met it, gathered from the eye inward
double exact_through(const shader::vec2 &p, double size, double nm) {
	const shader::Path q = shader::path_of(p, size);
	// beside every body, and behind what bends nothing, the wall itself as the screen has it, over the sample
	const double half = 0.5 * size;
	if (q.layers == 0 || (q.refractor < 0 && !q.ways)) {
		return shader::along(q, nm, 1.0, 0.0, 0.0, false, size > 1.0e-3 ? drawn_over(p.x - half, p.x + half, p.y - half, p.y + half) : drawn_at(p.x, p.y));
	}
	const double room = shader::room_front();
	// (through a grating: every way the ray goes on in, each by its share)
	double total = 0.0;
	int ways = 1;
	for (int way = 0; way < ways; ++way) {
		const ExactRay ray = exact_ray(p.x, p.y, nm, way);
		ways = way == 0 ? ray.ways : ways;
		double a = 1.0, b = 0.0;
		for (const Met &m : ray.met) {
			shader::Layer l = shader::layer_at(m.body, shader::vec2(m.cross, m.cross_y), size, size);
			double chord = m.chord;
			if (m.bent) {
				l.haze = shader::haze_along(m.body, m.chord); // along its own path through the liquid (Stage 26)
				if (shader::lies_in(m.body)) {
					// what lies in it is traced (Stage 30): its sediment is where the ray ended, lit by the room
					// through the liquid there and back
					l.solid = m.solid ? 1.0 : 0.0;
					chord = m.solid ? 2.0 * m.chord : m.chord;
				}
			}
			const int start = shader::set_of(m.body) * optics_layout::ROWS_PER_SET * optics_layout::ROW;
			const double per_cm = shader::liquid_at(m.body, start, nm);
			double glow = shader::row_at(start + optics_layout::GLOW_ROW * optics_layout::ROW, nm);
			if (glow < 0.0) {
				glow = truth_image_glow(*shader::current, m.body, m.cross, m.cross_y, size, size, nm, l.solid); // a screen's: its image's (Stage 42)
			}
			const shader::vec2 does = shader::body_does(l, shader::row_at(start + optics_layout::ROW, nm), glow, shader::row_at(start + 2 * optics_layout::ROW, nm),
					per_cm * (m.bent ? chord : l.depth_cm + l.glowing), m.pass, m.absorbed, room, shader::theta_of(nm, shader::body_kelvin(m.body)));
			b += a * does.y;
			a *= does.x;
		}
		total += (ways > 1 ? shader::way_share(way) : 1.0) * (a * drawn_at(ray.x, ray.y) + b);
	}
	return total;
}

} // namespace

double optics_area_through(const OpticsScene &scene, int x, int y, double nm, int grid) {
	if (!shader::ready(scene) || grid < 1) {
		return 0.0;
	}
	// a row of the grid a piece
	std::vector<double> rows(static_cast<size_t>(grid), 0.0);
	in_pieces(grid, [&](int j) {
		const shader::Use use(scene);
		for (int i = 0; i < grid; ++i) {
			rows[static_cast<size_t>(j)] += exact_through(shader::vec2(x + (i + 0.5) / grid, y + (j + 0.5) / grid), 1.0 / grid, nm);
		}
	});
	double sum = 0.0;
	for (const double r : rows) {
		sum += r;
	}
	return sum / (grid * grid);
}

double optics_point_truth(const OpticsScene &scene, double x, double y, double nm) {
	if (!shader::ready(scene)) {
		return 0.0;
	}
	const shader::Use use(scene);
	return exact_through(shader::vec2(x, y), shader::POINT_SIZE, nm);
}

double optics_image_glow(const OpticsScene &scene, int body, double x, double y, double across, double up, double nm) {
	if (!shader::ready(scene) || scene.images.empty()) {
		return 0.0;
	}
	const shader::Use use(scene);
	const shader::Layer l = shader::layer_at(body, shader::vec2(x, y), across, up);
	const int start = shader::set_of(body) * optics_layout::ROWS_PER_SET * optics_layout::ROW;
	return shader::glow_of(body, start, shader::vec2(x, y), across, up, nm, l.solid) * l.solid;
}

double optics_image_glow_truth(const OpticsScene &scene, int body, double x, double y, double across, double up, double nm) {
	if (!shader::ready(scene) || scene.images.empty()) {
		return 0.0;
	}
	const shader::Use use(scene);
	const shader::Layer l = shader::layer_at(body, shader::vec2(x, y), across, up);
	return truth_image_glow(scene, body, x, y, across, up, nm, l.solid) * l.solid;
}

std::string check_O17(const OpticsScene &scene, int samples) {
	using namespace optics_layout;
	if (!shader::ready(scene)) {
		return "O17: no scene";
	}
	uint64_t seed = 88172645463325252ull;
	const auto next = [&] {
		seed ^= seed << 13;
		seed ^= seed >> 7;
		seed ^= seed << 17;
		return static_cast<double>(seed >> 11) / 9007199254740992.0;
	};
	const int count = static_cast<int>(scene.table[0]);
	for (int b = 0; b < count; ++b) {
		const size_t at = static_cast<size_t>(HEADER_VECS + b * BODY_VECS) * 4;
		if (!(scene.table[at + MORE * 4 + 1] > 0.5f) || !(scene.table[at + FORM * 4] > 2.5f && scene.table[at + FORM * 4] < 3.5f)) {
			continue;
		}
		const double x = scene.table[at], y = scene.table[at + 1], w = scene.table[at + 2], h = scene.table[at + 3];
		for (int i = 0; i < samples; ++i) {
			// a sample anywhere on it or over its edge, from a ray's point to many of its pixels across
			const double size = std::pow(10.0, -3.0 + 5.0 * next());
			const double across = std::min(size, 0.5 * w), up = std::min(size * (0.5 + next()), 0.5 * h);
			const double px = x - 0.25 * across + next() * (w + 0.5 * across), py = y - 0.25 * up + next() * (h + 0.5 * up);
			for (const double nm : { 451.0, 532.5, 618.0 }) {
				const double pass = optics_image_glow(scene, b, px, py, across, up, nm), truth = optics_image_glow_truth(scene, b, px, py, across, up, nm);
				if (!(std::fabs(pass - truth) <= 1e-4 * std::max(1.0, std::fabs(truth)))) { // (the buffer's sums are a float's, as the GPU's)
					char text[220];
					std::snprintf(text, sizeof text, "O17: a screen (body %d) over %.4g by %.4g at (%.3f, %.3f), %g nm: the pass reads %.9g, its pixels say %.9g", b, across, up, px, py, nm, pass, truth);
					return text;
				}
			}
		}
	}
	return "";
}

std::string check_O18(const OpticsScene &whole, const OpticsScene &zoomed, double scale, double ox, double oy, double rx, double ry, double x0, double y0, double x1, double y1, double step) {
	if (!shader::ready(whole) || !shader::ready(zoomed)) {
		return "O18: no scene";
	}
	int compared = 0, apart = 0;
	double worst = 0.0, worst_x = 0.0, worst_y = 0.0;
	// (each point 0.47 of a pixel into its pixel of the layout: what is drawn on the wall is painted on whole pixels,
	// of the layout at 1x and of the screen at the view, which may stand a quarter of a layout's pixel apart)
	for (double y = std::floor(y0) + 0.47; y < y1; y += std::max(1.0, std::round(step))) {
		for (double x = std::floor(x0) + 0.47; x < x1; x += std::max(1.0, std::round(step))) {
			const double zx = rx + (x - ox) * scale, zy = ry + (y - oy) * scale;
			if (zx < 0.0 || zy < 0.0 || zx >= 1920.0 || zy >= 1080.0) {
				continue;
			}
			for (const double nm : { 447.0, 553.0, 641.0 }) {
				// (a ray the view lands past its own screen reads the far wall's mean there, not the wall drawn:
				// the view's wall is what is on its screen; GAME_SPEC §20)
				const OpticsRay ray = optics_ray(zoomed, zx, zy, nm);
				if (ray.bent > 0 && (!ray.lands || ray.x < 0.0 || ray.y < 0.0 || ray.x >= 1920.0 || ray.y >= 1080.0)) {
					continue;
				}
				const double a = optics_point(whole, x, y, nm), b = optics_point(zoomed, zx, zy, nm);
				const double off = std::fabs(a - b) / std::max(1.0, std::fabs(a));
				++compared;
				if (off > 2e-3) {
					++apart;
					if (off > worst) {
						worst = off;
						worst_x = x;
						worst_y = y;
					}
				}
			}
		}
	}
	if (compared == 0 || apart > compared / 100) {
		char text[220];
		std::snprintf(text, sizeof text, "O18: %d of %d rays bring another light seen at %gx than laid out (the worst %.3g apart, at (%.2f, %.2f) of the layout)", apart, compared, scale, worst, worst_x, worst_y);
		return text;
	}
	return "";
}

double optics_exact_bend(const OpticsScene &scene, int x, int y, double nm) {
	if (!shader::ready(scene)) {
		return 0.0;
	}
	const shader::Use use(scene);
	const shader::Path q = shader::at(x, y);
	return q.refractor < 0 ? q.x : exact_ray(q.x, q.y, nm).x;
}

void optics_area_truth(const OpticsScene &scene, int x, int y, int grid, double rgb[3]) {
	rgb[0] = rgb[1] = rgb[2] = 0.0;
	if (!shader::ready(scene) || grid < 1) {
		return;
	}
	const shader::Use use(scene);
	std::vector<shader::vec2> places;
	for (int j = 0; j < grid; ++j) {
		for (int i = 0; i < grid; ++i) {
			places.push_back(shader::vec2(x + (i + 0.5) / grid, y + (j + 0.5) / grid));
		}
	}
	// the light's shares, sixty-four to a piece
	const int shares = truth_shares(scene), pieces = (shares + 63) / 64;
	std::vector<shader::vec3> parts(static_cast<size_t>(pieces));
	in_pieces(pieces, [&](int piece) {
		const shader::Use mine(scene);
		shader::vec3 part;
		for (int j = piece * 64; j < std::min(shares, piece * 64 + 64); ++j) {
			const shader::vec4 a = shader::share(static_cast<double>(j) / shares), b = shader::share(static_cast<double>(j + 1) / shares);
			const double nm = shader::share((j + 0.5) / shares).x + 380.0;
			double seen = 0.0;
			for (const shader::vec2 &place : places) {
				seen += exact_through(place, 1.0 / grid, nm);
			}
			part = part + shader::vec3(b.y - a.y, b.z - a.z, b.w - a.w) * (seen / static_cast<double>(places.size()));
		}
		parts[static_cast<size_t>(piece)] = part;
	});
	shader::vec3 sum;
	for (const shader::vec3 &part : parts) {
		sum = sum + part;
	}
	const shader::vec3 c = shader::to_linear_srgb(sum);
	rgb[0] = c.x;
	rgb[1] = c.y;
	rgb[2] = c.z;
}

std::string check_O7(const OpticsScene &scene, int x0, int y0, int x1, int y1, double radiance, OpticsThrough through) {
	if (!shader::ready(scene)) {
		return "O7: the scene is not ready";
	}
	namespace L = optics_layout;
	OpticsScene even = scene;
	even.table[0 * 4 + 1] = even.table[0 * 4 + 2] = static_cast<float>(radiance); // the wall, on the bench and off it
	even.table[2 * 4 + 1] = static_cast<float>(radiance); // the room in front, which the glass reflects
	{
		// nothing drawn: one radiance everywhere
		OpticsRoom room;
		room.x = scene.table[4];
		room.y = scene.table[5];
		room.w = scene.table[6];
		room.h = scene.table[7];
		room.bench = room.elsewhere = room.front = radiance; // (the room in front is what a ray that never reaches the wall shows)
		optics_wall(even, room);
	}
	for (int b = 0; b < static_cast<int>(even.rows.size()) / (L::ROWS_PER_SET * L::ROW); ++b) {
		for (const int row : { L::LIQUID_ROW, L::WALL_ROW }) {
			for (int i = 0; i < L::ROW; ++i) {
				even.rows[static_cast<size_t>((b * L::ROWS_PER_SET + row) * L::ROW + i)] = 0.0f; // nothing absorbs
			}
		}
	}
	for (size_t i = 2; i < even.lines.size(); i += 4) {
		even.lines[i] = 0.0f;
	}
	const OpticsThrough read = through != nullptr ? through : optics_through;
	for (int y = y0; y < y1; ++y) {
		for (int x = x0; x < x1; ++x) {
			if (optics_cover(even, x, y) <= 0.0) {
				continue;
			}
			for (const double nm : { 430.0, 550.0, 670.0 }) {
				const double seen = read(even, x, y, nm);
				if (std::fabs(seen - radiance) > 1e-6 * radiance) {
					char buffer[200];
					std::snprintf(buffer, sizeof buffer, "O7: pixel (%d, %d) at %.0f nm shows %.9g of a uniform wall of %.9g", x, y, nm, seen, radiance);
					return buffer;
				}
			}
			// and over bands of the light, read as the pass reads them (the renderer's own reading alone)
			if (through == nullptr) {
				for (const double from : { 0.0, 0.31, 0.9 }) {
					const double seen = optics_band(even, x, y, from, from + 0.1, 0.3);
					if (std::fabs(seen - radiance) > 1e-6 * radiance) {
						char buffer[200];
						std::snprintf(buffer, sizeof buffer, "O7: pixel (%d, %d) over the band from %.2f shows %.9g of a uniform wall of %.9g", x, y, from, seen, radiance);
						return buffer;
					}
				}
			}
		}
	}
	return "";
}

std::string check_O8(const OpticsScene &with_line, const OpticsScene &without, int row, int x0, int x1, double nm, OpticsThrough through) {
	if (!shader::ready(with_line) || !shader::ready(without)) {
		return "O8: a scene is not ready";
	}
	const OpticsThrough read = through != nullptr ? through : optics_through;
	std::vector<double> of_pixel(static_cast<size_t>(std::max(0, x1 - x0)), 0.0);
	in_pieces(x1 - x0, [&](int i) { of_pixel[static_cast<size_t>(i)] = read(with_line, x0 + i, row, nm) - read(without, x0 + i, row, nm); });
	double shown = 0.0;
	for (const double v : of_pixel) {
		shown += v;
	}
	// The truth: what the line adds to one ray, with the line and without, traced exactly at the row's middle.
	// It is nothing but where rays land on the line: those places' ends are found by halving (a line's image
	// can be a tenth of a pixel wide: counting rays would count it by whole steps), and between two ends it
	// is summed by Simpson's rule.
	const auto adds = [&](double x) {
		double with = 0.0, bare = 0.0;
		{
			const shader::Use use(with_line);
			with = exact_through(shader::vec2(x, row + 0.5), 1.0e-6, nm);
		}
		{
			const shader::Use use(without);
			bare = exact_through(shader::vec2(x, row + 0.5), 1.0e-6, nm);
		}
		return with - bare;
	};
	// Where the rays land: an image can be far thinner than any even step (through two vessels a landing may
	// cross the whole wall within a hundredth of a pixel), so between two places whose rays land more than a
	// twentieth of a pixel apart the screen is halved again, down to where a landing jumps (the rays part ways).
	const auto lands = [&](double x) {
		const shader::Use use(without);
		return exact_ray(x, row + 0.5, nm).x;
	};
	constexpr int STEPS = 256; // a pixel, to begin with
	struct Place {
		double x, lands;
		bool on;
	};
	// a pixel a piece: each begins where the one before it ends, on the same ray
	std::vector<std::vector<double>> ends_of(static_cast<size_t>(std::max(0, x1 - x0)));
	in_pieces(x1 - x0, [&](int pixel) {
		std::vector<double> &ends = ends_of[static_cast<size_t>(pixel)];
		const auto end_between = [&](double lo, double hi, bool on_lo) {
			for (int it = 0; it < 40; ++it) {
				const double mid = 0.5 * (lo + hi);
				((adds(mid) != 0.0) == on_lo ? lo : hi) = mid;
			}
			ends.push_back(0.5 * (lo + hi));
		};
		std::vector<Place> todo; // the right ends of the pieces still to look at, the nearest last
		const double from = x0 + pixel;
		Place before{ from, lands(from), adds(from) != 0.0 };
		for (int i = 1; i <= STEPS; ++i) {
			const double x = from + static_cast<double>(i) / STEPS;
			todo.push_back(Place{ x, lands(x), adds(x) != 0.0 });
			while (!todo.empty()) {
				const Place here = todo.back();
				if (std::fabs(here.lands - before.lands) > 0.05 && here.x - before.x > 1e-11) {
					const double mid = 0.5 * (before.x + here.x);
					todo.push_back(Place{ mid, lands(mid), adds(mid) != 0.0 });
					continue;
				}
				todo.pop_back();
				if (before.on != here.on) {
					end_between(before.x, here.x, before.on);
				}
				before = here;
			}
		}
	});
	std::vector<double> ends;
	for (const std::vector<double> &some : ends_of) {
		ends.insert(ends.end(), some.begin(), some.end());
	}
	// each stretch between two ends a piece
	const int stretches = ends.size() > 1 ? static_cast<int>(ends.size()) - 1 : 0;
	std::vector<double> of_stretch(static_cast<size_t>(stretches), 0.0);
	in_pieces(stretches, [&](int e) {
		const double a = ends[static_cast<size_t>(e)] + 1e-9, b = ends[static_cast<size_t>(e) + 1] - 1e-9;
		if (adds(0.5 * (a + b)) == 0.0) {
			return; // between two images
		}
		constexpr int N = 32;
		double sum = adds(a) + adds(b);
		for (int k = 1; k < N; ++k) {
			sum += (k % 2 == 1 ? 4.0 : 2.0) * adds(a + (b - a) * k / N);
		}
		of_stretch[static_cast<size_t>(e)] = sum * (b - a) / (3.0 * N);
	});
	double truth = 0.0;
	for (const double v : of_stretch) {
		truth += v;
	}
	if (std::fabs(shown - truth) > 0.01 * std::fabs(truth) + 1e-6) {
		char buffer[200];
		std::snprintf(buffer, sizeof buffer, "O8: the line brings %.6g summed over the pixels of row %d, its rays %.6g", shown, row, truth);
		return buffer;
	}
	return "";
}

// ---- a pixel's own rays (GAME_SPEC §12.3, Stage 29): the pass's tracer held to the truth's

OpticsRay optics_ray_way(const OpticsScene &scene, double x, double y, double nm, int way) {
	OpticsRay out;
	if (!shader::ready(scene) || scene.shapes.empty()) {
		return out;
	}
	const shader::Use use(scene);
	shader::ray_way = way;
	const shader::RayMet m = shader::ray_met(shader::vec2(x, y), nm);
	shader::ray_way = 0;
	out.ways = m.ways;
	out.x = m.x;
	out.y = m.y;
	out.lands = std::fabs(m.x) < 5.0e4;
	// (a long route's count runs past 31 bits: the two tracers' are held to each other below that, as the truth's is kept)
	out.route[0] = m.t0 & 0x7fffffffu;
	out.route[1] = m.t1 & 0x7fffffffu;
	for (int k = 0; k < m.count; ++k) {
		const shader::vec4 slot = shader::met_slot(m, k);
		out.solid = out.solid || slot.y > 2.5;
		if (slot.y > 0.5 && (slot.y < 1.5 || slot.y > 2.5) && out.bent < 3) {
			out.body[out.bent] = static_cast<int>(slot.x);
			out.pass[out.bent] = slot.z;
			out.chord[out.bent] = slot.w;
			++out.bent;
		}
	}
	return out;
}

OpticsRay optics_ray(const OpticsScene &scene, double x, double y, double nm) {
	return optics_ray_way(scene, x, y, nm, 0);
}

OpticsRay optics_exact_ray(const OpticsScene &scene, double x, double y, double nm) {
	OpticsRay out;
	if (!shader::ready(scene)) {
		return out;
	}
	const shader::Use use(scene);
	const ExactRay ray = exact_ray(x, y, nm);
	out.x = ray.x;
	out.y = ray.y;
	out.lands = std::fabs(ray.x) < 5.0e4;
	for (const Met &m : ray.met) {
		out.solid = out.solid || m.solid;
		if (m.bent && out.bent < 3) {
			out.body[out.bent] = m.body;
			out.pass[out.bent] = m.pass;
			out.chord[out.bent] = m.solid ? 2.0 * m.chord : m.chord; // (to the sediment and back, as the pass's ray has it)
			if (out.bent < 2) {
				out.route[out.bent] = m.route & 0x7fffffffu;
			}
			++out.bent;
		}
	}
	return out;
}

namespace {

// How fast a ray's landing moves as the eye's place does, at a place and a wavelength: pixels of the wall a
// pixel of the screen, at least 1 (a tolerance on the wall is a tolerance on the screen times this: near a
// rim one pixel of the screen sees hundreds of the wall's)
double landing_slope(const OpticsScene &scene, double x, double y, double nm) {
	const OpticsRay left = optics_ray(scene, x - 0.5, y, nm), right = optics_ray(scene, x + 0.5, y, nm), up = optics_ray(scene, x, y - 0.5, nm), down = optics_ray(scene, x, y + 0.5, nm);
	double slope = 1.0;
	if (left.lands && right.lands) {
		slope = std::max({ slope, std::fabs(right.x - left.x), std::fabs(right.y - left.y) });
	}
	if (up.lands && down.lands) {
		slope = std::max({ slope, std::fabs(down.x - up.x), std::fabs(down.y - up.y) });
	}
	return slope;
}

} // namespace

std::string check_O9(const OpticsScene &scene, int x0, int y0, int x1, int y1, int step, OpticsRayOf ray) {
	if (!shader::ready(scene)) {
		return "O9: the scene is not ready";
	}
	const OpticsRayOf read = ray != nullptr ? ray : optics_ray;
	// a millionth of a pixel of the screen (on the wall, that times how fast the landing moves there), and a
	// hundred-millionth of how far the ray is sent (a ray thrown across the screen by a rim); a hundred-millionth
	// of what passes, and of a centimetre of liquid. The two tracers are one method written twice, on the same
	// surfaces in double precision: they differ by where a ray starts, and by rounding.
	constexpr double PLACE = 1.0e-6, FAR = 5.0e-8, PASS = 1.0e-7, CHORD = 1.0e-7;
	// (what passes, and a chord, of a ray thrown about a rim: to a thousandth)
	constexpr double THROWN = 1.0e-3;
	// Almost every ray is within those. A ray that grazes a rim is thrown about by its surfaces, and the last
	// digit of where it met one moves it: such rays, one in fifty at most, may be a thousand times further out
	// (a thousandth of a pixel of the screen, and a twenty-thousandth of how far the ray is sent).
	constexpr double LOOSE = 1000.0;
	int64_t rays = 0, astray = 0, loose = 0;
	char buffer[320];
	for (int y = y0; y < y1; y += step > 0 ? step : 1) {
		for (int x = x0; x < x1; x += step > 0 ? step : 1) {
			for (const double at : { 0.5, 0.0 }) {
				for (const double nm : { 400.0, 550.0, 700.0 }) {
					const double px = x + at, py = y + at;
					const OpticsRay mine = read(scene, px, py, nm), exact = optics_exact_ray(scene, px, py, nm);
					++rays;
					bool same = mine.lands == exact.lands && mine.bent == exact.bent;
					for (int k = 0; same && k < mine.bent; ++k) {
						same = mine.body[k] == exact.body[k];
					}
					// one way through each one's surfaces, or another (the two tracers number a shape's surfaces alike)
					same = same && mine.route[0] == exact.route[0] && mine.route[1] == exact.route[1];
					if (!same) {
						++astray;
						continue;
					}
					const double reach = PLACE * landing_slope(scene, px, py, nm) + FAR * std::hypot(exact.x - px, exact.y - py);
					double worst = mine.lands ? std::max(std::fabs(mine.x - exact.x), std::fabs(mine.y - exact.y)) / reach : 0.0;
					double thrown = 0.0;
					for (int k = 0; k < mine.bent; ++k) {
						worst = std::max({ worst, std::fabs(mine.pass[k] - exact.pass[k]) / PASS, std::fabs(mine.chord[k] - exact.chord[k]) / CHORD });
						thrown = std::max({ thrown, std::fabs(mine.pass[k] - exact.pass[k]), std::fabs(mine.chord[k] - exact.chord[k]) });
					}
					loose += worst > 1.0 ? 1 : 0;
					const double place = mine.lands ? std::max(std::fabs(mine.x - exact.x), std::fabs(mine.y - exact.y)) / reach : 0.0;
					if (place > LOOSE || thrown > THROWN) {
						std::snprintf(buffer, sizeof buffer, "O9: the ray at (%.1f, %.1f), %.0f nm lands at (%.6f, %.6f), the truth's at (%.6f, %.6f); it passes %.8f, the truth's %.8f; its chord %.7f cm, the truth's %.7f", px,
								py, nm, mine.x, mine.y, exact.x, exact.y, mine.bent > 0 ? mine.pass[0] : 1.0, exact.bent > 0 ? exact.pass[0] : 1.0, mine.bent > 0 ? mine.chord[0] : 0.0,
								exact.bent > 0 ? exact.chord[0] : 0.0);
						return buffer;
					}
				}
			}
		}
	}
	if (astray * 100 > rays || loose * 50 > rays) {
		std::snprintf(buffer, sizeof buffer, "O9: of %lld rays, %lld go another way than the truth's and %lld are not where the truth's are", static_cast<long long>(rays), static_cast<long long>(astray),
				static_cast<long long>(loose));
		return buffer;
	}
	return "";
}

std::vector<double> optics_stretches(const OpticsScene &scene, int x, int y) {
	if (!shader::ready(scene) || scene.shapes.empty()) {
		return {};
	}
	const shader::Use use(scene);
	const shader::Path q = shader::at(x, y);
	if (q.refractor < 0 && !q.ways) {
		return {};
	}
	// the stretches the average over time reads its beam in (those whose rays go one way: the others' beams are
	// their middle rays alone)
	shader::beam_stretches.clear();
	shader::beam_means(q, 16, 0.5, shader::cuts_none(), 0.0, 1.0, false, 0, false);
	return shader::beam_stretches;
}

std::vector<double> optics_stretches_unhalved(const OpticsScene &scene, int x, int y) {
	const double kept = shader::beam_between;
	shader::beam_between = 0.0;
	std::vector<double> out = optics_stretches(scene, x, y);
	shader::beam_between = kept;
	return out;
}

std::string check_O10(const OpticsScene &scene, int x0, int y0, int x1, int y1, int step, OpticsStretches stretches) {
	if (!shader::ready(scene) || scene.shapes.empty()) {
		return "O10: the scene is not ready";
	}
	const OpticsStretches read = stretches != nullptr ? stretches : optics_stretches;
	// the sweep's tolerance on the wall (0.02 of a pixel); a hundredth of what passes; a hundredth of the chord
	constexpr double PLACE = 0.02, PASS = 0.01, CHORD = 0.01;
	// Almost every ray is within those; where a rim or a floor's corner throws the light about, a ray may be
	// up to twenty times further out, and one in fifty at most.
	constexpr double LOOSE = 20.0;
	int64_t probes = 0, astray = 0, pixels = 0, loose = 0, stretches_all = 0, shortest = 0;
	char buffer[360];
	for (int y = y0; y < y1; y += step > 0 ? step : 1) {
		for (int x = x0; x < x1; x += step > 0 ? step : 1) {
			const std::vector<double> ranges = read(scene, x, y);
			const shader::Use use(scene);
			const shader::vec2 p(x + 0.5, y + 0.5);
			bool counted = false;
			for (size_t r = 0; r + 1 < ranges.size(); r += 2) {
				const double g_lo = ranges[r], g_hi = ranges[r + 1];
				++stretches_all;
				if (g_hi - g_lo <= 1.0 / 16.0) {
					++shortest;
					continue; // as short as a stretch is made: read as it is
				}
				shader::RayMet node[4];
				bool one = true;
				for (int w = 0; w < 4; ++w) {
					node[w] = shader::ray_met(p, shader::place_nm(g_lo + (g_hi - g_lo) * w / 3.0));
					one = one && node[w].route == node[0].route;
				}
				if (!one || node[0].count == 0) {
					continue; // its way changes along the stretch (the reading cuts it there); or nothing is in its way
				}
				pixels += counted ? 0 : 1;
				counted = true;
				const auto bent_of = [](const shader::RayMet &m, int which) {
					int seen = 0;
					for (int k = 0; k < m.count; ++k) {
						const shader::vec4 slot = shader::met_slot(m, k);
						if (slot.y > 0.5 && seen++ == which) {
							return slot;
						}
					}
					return shader::vec4(-1.0, 0.0, 1.0, 0.0);
				};
				for (const double g : { 1.0 / 6.0, 0.5, 5.0 / 6.0 }) {
					const double nm = shader::place_nm(g_lo + (g_hi - g_lo) * g);
					const shader::RayMet traced = shader::ray_met(p, nm);
					++probes;
					if (traced.route != node[0].route) {
						++astray;
						continue;
					}
					const shader::vec4 w = shader::thirds_weights(g);
					const double cx = w.x * node[0].x + w.y * node[1].x + w.z * node[2].x + w.w * node[3].x, cy = w.x * node[0].y + w.y * node[1].y + w.z * node[2].y + w.w * node[3].y;
					// (on the wall, the sweep's tolerance times how fast the landing moves across the pixel)
					double slope = 1.0;
					{
						const shader::RayMet l = shader::ray_met(shader::vec2(p.x - 0.5, p.y), nm), rr = shader::ray_met(shader::vec2(p.x + 0.5, p.y), nm), u = shader::ray_met(shader::vec2(p.x, p.y - 0.5), nm),
											 d = shader::ray_met(shader::vec2(p.x, p.y + 0.5), nm);
						if (l.route == traced.route && rr.route == traced.route) {
							slope = std::max({ slope, std::fabs(rr.x - l.x), std::fabs(rr.y - l.y) });
						}
						if (u.route == traced.route && d.route == traced.route) {
							slope = std::max({ slope, std::fabs(d.x - u.x), std::fabs(d.y - u.y) });
						}
					}
					// and a hundredth of how far the landing sweeps along the stretch: a rim that spreads the
					// light over hundreds of pixels is read to a pixel
					const double sweep = std::hypot(node[3].x - node[0].x, node[3].y - node[0].y);
					double worst = std::max(std::fabs(cx - traced.x), std::fabs(cy - traced.y)) / (PLACE * slope + 0.01 * sweep);
					for (int which = 0; which < 2; ++which) {
						const shader::vec4 t = bent_of(traced, which);
						if (t.x < 0.0) {
							break;
						}
						double pass = 0.0, chord2 = 0.0;
						for (int k = 0; k < 4; ++k) {
							const shader::vec4 n = bent_of(node[k], which);
							pass += w[k] * n.z;
							chord2 += w[k] * n.w * n.w;
						}
						worst = std::max({ worst, std::fabs(pass - t.z) / PASS, std::fabs(std::sqrt(std::max(chord2, 0.0)) - t.w) / (CHORD * std::max(t.w, 0.1)) });
					}
					loose += worst > 1.0 ? 1 : 0;
					if (worst > LOOSE) {
						std::snprintf(buffer, sizeof buffer, "O10: at pixel (%d, %d), %.0f nm, the nodes of the stretch from %.4f to %.4f put the ray at (%.4f, %.4f); traced there it lands at (%.4f, %.4f) (%.2f of what is allowed)", x, y,
								nm, g_lo, g_hi, cx, cy, traced.x, traced.y, worst);
						return buffer;
					}
				}
			}
		}
	}
	if (pixels == 0) {
		return "O10: no pixel there looks through anything";
	}
	if (astray * 100 > probes || loose * 50 > probes) {
		std::snprintf(buffer, sizeof buffer, "O10: of %lld rays between nodes that agree, %lld go another way and %lld are not where the nodes put them", static_cast<long long>(probes), static_cast<long long>(astray),
				static_cast<long long>(loose));
		return buffer;
	}
	// (and few stretches are as short as they are made, where nothing is checked: a tenth at most)
	if (shortest * 10 > stretches_all) {
		std::snprintf(buffer, sizeof buffer, "O10: %lld of %lld stretches are as short as they are made", static_cast<long long>(shortest), static_cast<long long>(stretches_all));
		return buffer;
	}
	return "";
}

OpticsPassage optics_passage_single(const OpticsScene &scene, int body, const double from[3], const double heading[3], double n_glass, double n_core);

OpticsPassage optics_passage(const OpticsScene &scene, int body, const double from[3], const double heading[3], double n_glass, double n_core, bool single) {
	if (single) {
		return optics_passage_single(scene, body, from, heading, n_glass, n_core);
	}
	OpticsPassage out;
	if (!shader::ready(scene) || body < 0 || body >= optics_layout::MAX_BODIES || scene.shapes_exact.size() < static_cast<size_t>(optics_layout::MAX_BODIES) * optics_layout::SHAPE_BLOCK * 4) {
		return out;
	}
	const shader::Use use(scene);
	const int block = body * shader::SHAPE_BLOCK;
	const shader::vec4 head = shader::shape_vec(block);
	const int s = static_cast<int>(head.x);
	if (s < 0) {
		return out;
	}
	const shader::Traced t = shader::trace_shape(s, shader::vec3(from[0], from[1], from[2]), shader::vec3(heading[0], heading[1], heading[2]), n_glass, n_core, head.z > 0.5 ? block : -1, 0.0, 0.0);
	out.at[0] = t.at.x;
	out.at[1] = t.at.y;
	out.at[2] = t.at.z;
	out.toward[0] = t.toward.x;
	out.toward[1] = t.toward.y;
	out.toward[2] = t.toward.z;
	out.pass = t.pass;
	out.chord = t.chord;
	out.first = t.first;
	out.route = t.route;
	out.meetings = t.meetings;
	out.left = t.left;
	out.solid = t.solid;
	return out;
}

namespace {
OpticsPassage passage_single(const OpticsScene &scene, int body, const double from[3], const double heading[3], double n_glass, double n_core) {
	return optics_passage_single(scene, body, from, heading, n_glass, n_core);
}
} // namespace

std::string check_O15(const OpticsScene &scene, OpticsPassageOf single) {
	if (!shader::ready(scene) || scene.shapes_exact.size() < static_cast<size_t>(optics_layout::MAX_BODIES) * optics_layout::SHAPE_BLOCK * 4) {
		return "O15: the scene is not ready";
	}
	const OpticsPassageOf held = single != nullptr ? single : passage_single;
	constexpr double CLOSE = 1.0e-3;
	int64_t rays = 0, astray = 0, loose = 0;
	for (int body = 0; body < optics_layout::MAX_BODIES; ++body) {
		const double *head = &scene.shapes_exact[static_cast<size_t>(body) * optics_layout::SHAPE_BLOCK * 4];
		if (head[0] < 0.0) {
			continue;
		}
		const double *shape = &scene.shapes_exact[static_cast<size_t>(head[0]) * 4];
		const int patches = static_cast<int>(shape[0]);
		const double reach = shape[1];
		double low = 1.0e30, high = -1.0e30;
		for (int i = 0; i < patches; ++i) {
			const double *bounds = shape + 4 + static_cast<size_t>(i) * 24 + 8;
			if (bounds[0] > -1.0e29 && bounds[1] < 1.0e29) {
				low = std::min(low, bounds[0]);
				high = std::max(high, bounds[1]);
			}
		}
		if (!(reach < 1.0e29) || !(high > low)) {
			continue; // a shape with no end (a test's cylinder): nothing to lay rays across
		}
		// straight in, and aslant as a ray bent by a body in front comes
		const double headings[3][2] = { { 0.0, 0.0 }, { 0.06, 0.02 }, { -0.03, -0.05 } };
		for (const auto &h : headings) {
			const double u[3] = { h[0], h[1], std::sqrt(1.0 - h[0] * h[0] - h[1] * h[1]) };
			for (int j = 0; j <= 240; ++j) {
				for (int i = 0; i <= 120; ++i) {
					const double from[3] = { -reach * 1.02 + 2.04 * reach * (i + 0.37) / 121.0 - u[0] * (reach + 1.0), low - 0.05 + (high - low + 0.1) * (j + 0.61) / 241.0 - u[1] * (reach + 1.0), -(reach + 1.0) * u[2] };
					const OpticsPassage d = optics_passage(scene, body, from, u, 1.47, 1.333, false);
					const OpticsPassage s = held(scene, body, from, u, 1.47, 1.333);
					++rays;
					if (s.route != d.route || s.left != d.left || s.solid != d.solid) {
						++astray;
					} else if (d.left && (std::hypot(s.at[0] - d.at[0], s.at[1] - d.at[1], s.at[2] - d.at[2]) > CLOSE || std::hypot(s.toward[0] - d.toward[0], s.toward[1] - d.toward[1], s.toward[2] - d.toward[2]) > CLOSE)) {
						++loose;
					}
				}
			}
		}
	}
	if (rays == 0) {
		return "O15: no body of the scene bends";
	}
	if (astray * 200 > rays || loose * 100 > rays) {
		char buffer[240];
		std::snprintf(buffer, sizeof buffer, "O15: of %lld rays, %lld go another way in single precision and %lld leave elsewhere than in double", static_cast<long long>(rays), static_cast<long long>(astray),
				static_cast<long long>(loose));
		return buffer;
	}
	return "";
}

int optics_room_exit(const OpticsScene &scene, const double p[3], const double u[3], double landing[2]) {
	if (!shader::ready(scene) || !(scene.table[3 * 4 + 2] > 0.0f)) {
		return -1;
	}
	const shader::Use use(scene);
	const shader::vec3 comes = shader::room_exit(shader::vec3(p[0], p[1], p[2]), shader::vec3(u[0], u[1], u[2]), shader::scene_vec(2).x, shader::scene_vec(3).z, shader::scene_vec(3).w);
	landing[0] = comes.y;
	landing[1] = comes.z;
	return static_cast<int>(comes.x);
}

void optics_work(int64_t work[9], bool reset) {
	work[0] = shader::rays_followed;
	for (int i = 0; i < 8; ++i) {
		work[i + 1] = shader::work_done[i];
		if (reset) {
			shader::work_done[i] = 0;
		}
	}
	if (reset) {
		shader::rays_followed = 0;
	}
}

int64_t optics_rays_for(const OpticsScene &scene, int x, int y) {
	if (!shader::ready(scene) || scene.shapes.empty()) {
		return 0;
	}
	const shader::Use use(scene);
	const shader::Path q = shader::at(x, y);
	if (q.refractor < 0 && !q.ways) {
		return 0;
	}
	// a frame in which its content has just changed: its beam, the fixed estimate and the least batch
	const int64_t before = shader::rays_followed;
	shader::beam_means(q, shader::LEAST_BATCH, 0.5, shader::cuts_none(), 0.0, 1.0, false, 1, true);
	return shader::rays_followed - before;
}

double optics_made(const OpticsScene &scene, int x, int y) {
	if (!shader::ready(scene) || scene.shapes.empty()) {
		return 1.0;
	}
	const shader::Use use(scene);
	const shader::Path q = shader::at(x, y);
	if (q.refractor < 0) {
		return 1.0;
	}
	// of the exact reading at one wavelength: each part followed to where its rays part
	return shader::beam_sum(q, 1, 0.5, shader::cuts_none(), 0.0, 1.0, true, 550.0).w;
}

std::string check_O11(const OpticsScene &scene, int x0, int y0, int x1, int y1, OpticsMade made) {
	if (!shader::ready(scene)) {
		return "O11: the scene is not ready";
	}
	const OpticsMade read = made != nullptr ? made : optics_made;
	for (int y = y0; y < y1; ++y) {
		for (int x = x0; x < x1; ++x) {
			const double whole = read(scene, x, y);
			if (std::fabs(whole - 1.0) > 1.0e-9) {
				char buffer[200];
				std::snprintf(buffer, sizeof buffer, "O11: pixel (%d, %d) is read from %.12f of itself", x, y, whole);
				return buffer;
			}
		}
	}
	return "";
}

void optics_truth(const OpticsScene &scene, int x, int y, double rgb[3]) {
	rgb[0] = rgb[1] = rgb[2] = 0.0;
	if (!shader::ready(scene)) {
		return;
	}
	const shader::Use use(scene);
	const shader::Path q = shader::at(x, y);
	const shader::vec3 c = shader::truth(q, truth_shares(scene));
	rgb[0] = c.x;
	rgb[1] = c.y;
	rgb[2] = c.z;
}

// What a pixel's colour is shown as (Stage 46): itself; in the debug infrared view (the scene read in the long-wave
// range), its band signal's temperature's colour, as the pass shows it (linear sRGB)
static void displayed(const OpticsScene &scene, double rgb[3]) {
	if (!(scene.table.size() > 4 * 4 + 3 && scene.table[4 * 4 + 3] > 0.5f)) {
		return;
	}
	const shader::Use use(scene);
	const shader::vec4 c = shader::infrared_shown((rgb[0] + rgb[1] + rgb[2]) / 3.0);
	const double code[3] = { c.x, c.y, c.z };
	for (int k = 0; k < 3; ++k) {
		rgb[k] = code[k] <= 0.04045 ? code[k] / 12.92 : std::pow((code[k] + 0.055) / 1.055, 2.4);
	}
}

void optics_pixel(const OpticsScene &scene, int x, int y, double rgba[4]) {
	double rgb[3];
	optics_truth(scene, x, y, rgb);
	displayed(scene, rgb);
	gamut_map(rgb);
	for (int c = 0; c < 3; ++c) {
		rgba[c] = srgb_encode(rgb[c]);
	}
	rgba[3] = 1.0; // the wall where no body is (GAME_SPEC §12.3: the room and the bodies one picture)
}

std::vector<uint8_t> optics_picture(const OpticsScene &scene, int x, int y, int w, int h) {
	std::vector<uint8_t> out(static_cast<size_t>(w > 0 ? w : 0) * static_cast<size_t>(h > 0 ? h : 0) * 4);
	size_t at = 0;
	for (int row = 0; row < h; ++row) {
		for (int col = 0; col < w; ++col) {
			double rgba[4];
			optics_pixel(scene, x + col, y + row, rgba);
			for (const double v : rgba) {
				out[at++] = static_cast<uint8_t>(std::lround(v * 255.0));
			}
		}
	}
	return out;
}

// ---- the renderer's own frames (their pixels and histories: optics_pixel_host.inc, in the arithmetic asked for)

OpticsFramesCore *optics_frames_exact(int x, int y, int w, int h, int step) {
	return new shader::Frames(x, y, w, h, step);
}

OpticsFrames::OpticsFrames(int x, int y, int w, int h, double budget, int step, bool single) :
		x_(x), y_(y), w_(w > 0 ? w : 0), h_(h > 0 ? h : 0), step_(step > 0 ? step : 1), budget_(budget), restarted_(static_cast<size_t>(w_) * static_cast<size_t>(h_), 0) {
	core_ = single ? optics_frames_single(x_, y_, w_, h_, step_) : optics_frames_exact(x_, y_, w_, h_, step_);
}

OpticsFrames::~OpticsFrames() {
	delete core_;
}

void OpticsFrames::frame(const OpticsScene &scene) {
	OpticsFramesCore::Frame f;
	f.number = frame_;
	f.budget = budget_;
	f.keeping = keeping_;
	f.sharing = sharing_;
	f.asked = asked_;
	f.ray_asked = ray_asked_;
	f.beams_traced = beams_traced_;
	f.points = points_;
	f.read = read_;
	f.rays = rays_;
	f.most_rays = most_rays_;
	if (!core_->frame(scene, f, restarted_)) {
		return;
	}
	asked_ = f.asked;
	ray_asked_ = f.ray_asked;
	beams_traced_ = f.beams_traced;
	points_ = f.points;
	read_ = f.read;
	rays_ = f.rays;
	most_rays_ = f.most_rays;
	++frame_;
}

void OpticsFrames::colour(const OpticsScene &scene, int x, int y, double rgb[3]) const {
	core_->colour(scene, x, y, rgb);
}

void OpticsFrames::shown(const OpticsScene &scene, int x, int y, double rgba[4]) const {
	core_->shown(scene, x, y, rgba);
}

void OpticsFrames::resample(const OpticsFrames &anchor, const OpticsScene &scene, const double now[3], const double from[3], bool full_key) {
	const double n[4] = { now[0], now[1], now[2], 0.0 }, f[4] = { from[0], from[1], from[2], 0.0 };
	core_->resample(scene, *anchor.core_, n, f, full_key);
	asked_ = ray_asked_ = 0.0;
}

bool OpticsFrames::kept_fresh(const OpticsScene &scene, int x, int y, bool &holds) const {
	return core_->kept_fresh(scene, x, y, holds);
}

std::string check_O16(const OpticsFrames &exact, const OpticsFrames &single, const OpticsScene &scene, int x0, int y0, int x1, int y1, int step, double stats[4]) {
	if (!shader::ready(scene) || step < 1) {
		return "O16: the scene is not ready";
	}
	const auto seen = [](const double c[3]) {
		return shown(chem::optics::Xyz{ 0.4124564 * c[0] + 0.3575761 * c[1] + 0.1804375 * c[2], 0.2126729 * c[0] + 0.7151522 * c[1] + 0.0721750 * c[2], 0.0193339 * c[0] + 0.1191920 * c[1] + 0.9503041 * c[2] });
	};
	int64_t covered = 0, apart = 0;
	double worst = 0.0, sum = 0.0;
	for (int y = y0; y < y1; y += step) {
		for (int x = x0; x < x1; x += step) {
			if (optics_cover(scene, x, y) < OPTICS_PARITY_COVER) {
				continue;
			}
			double a[3], b[3];
			exact.colour(scene, x, y, a);
			single.colour(scene, x, y, b);
			const double d = eye_difference(seen(a), seen(b));
			++covered;
			sum += d;
			worst = std::max(worst, d);
			apart += d > EYE_VISIBLE ? 1 : 0;
		}
	}
	const double mean = covered > 0 ? sum / static_cast<double>(covered) : 0.0;
	if (stats != nullptr) {
		stats[0] = static_cast<double>(covered);
		stats[1] = static_cast<double>(apart);
		stats[2] = worst;
		stats[3] = mean;
	}
	if (covered == 0) {
		return "O16: no pixel of a body was compared";
	}
	if (mean > 1.0 || apart * 200 > covered) {
		char buffer[240];
		std::snprintf(buffer, sizeof buffer, "O16: in single precision %lld of %lld pixels are visibly not what they are in double (the worst %.1f on the eye metric, the mean %.2f)", static_cast<long long>(apart), static_cast<long long>(covered), worst,
				mean);
		return buffer;
	}
	return "";
}

std::string check_O13(const OpticsFrames &frames, int64_t most) {
	if (frames.most_rays() > most) {
		char buffer[200];
		std::snprintf(buffer, sizeof buffer, "O13: a pixel followed %lld rays in a frame (%lld at most)", static_cast<long long>(frames.most_rays()), static_cast<long long>(most));
		return buffer;
	}
	return "";
}

std::string check_O12(const OpticsFrames &frames, const OpticsScene &scene, int x0, int y0, int x1, int y1, int step, int64_t *held) {
	if (!shader::ready(scene)) {
		return "O12: the scene is not ready";
	}
	int64_t count = 0;
	for (int y = y0; y < y1; y += step) {
		for (int x = x0; x < x1; x += step) {
			bool holds = false;
			if (!frames.kept_fresh(scene, x, y, holds)) {
				char buffer[200];
				std::snprintf(buffer, sizeof buffer, "O12: pixel (%d, %d) keeps beams that are not those of what it looks through now", x, y);
				return buffer;
			}
			count += holds ? 1 : 0;
		}
	}
	if (held != nullptr) {
		*held = count;
	}
	return "";
}

double OpticsFrames::points() const {
	return points_;
}

double OpticsFrames::asked() const {
	return asked_;
}

int OpticsFrames::frames() const {
	return frame_;
}

double OpticsFrames::spent(int x, int y) const {
	return core_->spent(x, y);
}

bool OpticsFrames::restarted(int x, int y) const {
	const int col = (x - x_) / step_, row = (y - y_) / step_;
	return x >= x_ && y >= y_ && col < w_ && row < h_ && restarted_[static_cast<size_t>(row) * static_cast<size_t>(w_) + static_cast<size_t>(col)] != 0;
}

// ---- the GPU's picture against the truth (O4)

namespace {

// O4 over every `stride`th row of the picture, from row `from`
OpticsParity compare_rows(const OpticsScene &scene, const uint8_t *picture, int w, int h, int from, int stride) {
	OpticsParity out;
	for (int row = from; row < h; row += stride) {
		const uint8_t *rgba = picture + static_cast<size_t>(row) * static_cast<size_t>(w) * 4;
		for (int col = 0; col < w; ++col, rgba += 4) {
			// Where a body covers less than a fiftieth of a pixel it is not held to account: whether such a
			// pixel is in the body at all turns on the last bit of a sum, and float32 and float64 need not agree.
			if (optics_cover(scene, col, row) < OPTICS_PARITY_COVER) {
				continue;
			}
			double rgb[3];
			optics_truth(scene, col, row, rgb);
			displayed(scene, rgb);
			const CieLab truth = shown(chem::optics::Xyz{ 0.4124564 * rgb[0] + 0.3575761 * rgb[1] + 0.1804375 * rgb[2], 0.2126729 * rgb[0] + 0.7151522 * rgb[1] + 0.0721750 * rgb[2],
					0.0193339 * rgb[0] + 0.1191920 * rgb[1] + 0.9503041 * rgb[2] });
			double lin[3];
			for (int c = 0; c < 3; ++c) {
				const double e = rgba[c] / 255.0;
				lin[c] = e <= 0.04045 ? e / 12.92 : std::pow((e + 0.055) / 1.055, 2.4);
			}
			const CieLab seen = to_lab(chem::optics::Xyz{ 0.4124564 * lin[0] + 0.3575761 * lin[1] + 0.1804375 * lin[2], 0.2126729 * lin[0] + 0.7151522 * lin[1] + 0.0721750 * lin[2],
					0.0193339 * lin[0] + 0.1191920 * lin[1] + 0.9503041 * lin[2] });
			const double d = eye_difference(truth, seen);
			++out.covered;
			if (d > out.worst) {
				out.worst = d;
				out.worst_x = col;
				out.worst_y = row;
			}
			out.over += d > EYE_VISIBLE ? 1 : 0;
			if (d > EYE_VISIBLE && std::getenv("LAB_O4_PRINT") != nullptr) { // (a measurement: what the two are, where they part)
				double area[3];
				optics_area_truth(scene, col, row, 8, area);
				// and what the pixel's own frames come to, in double and in single precision (Stage 35): the source
				// as the GPU runs it, where no GPU runs
				double settled[2][3];
				for (int single = 0; single < 2; ++single) {
					OpticsFrames frames(col, row, 1, 1, 16.0, 1, single == 1);
					for (int f = 0; f < 240; ++f) {
						frames.frame(scene);
					}
					frames.colour(scene, col, row, settled[single]);
				}
				std::fprintf(stderr,
						"O4 (%d, %d): %.1f apart; the picture %.3f %.3f %.3f, the reference's reading %.3f %.3f %.3f, the truth of its area %.3f %.3f %.3f, its frames in double %.3f %.3f %.3f, in single precision %.3f %.3f %.3f\n", col,
						row, d, lin[0], lin[1], lin[2], rgb[0], rgb[1], rgb[2], area[0], area[1], area[2], settled[0][0], settled[0][1], settled[0][2], settled[1][0], settled[1][1], settled[1][2]);
			}
		}
	}
	return out;
}

} // namespace

OpticsParity optics_compare(const OpticsScene &scene, const uint8_t *rgba, int w, int h) {
	if (!shader::ready(scene) || w <= 0 || h <= 0) {
		return OpticsParity{};
	}
	// the truth costs a pixel thousands of rays: the rows are dealt among the machine's threads, one each in
	// turn (the bodies are in a few rows of the screen, and a thread given a band of rows would have them all)
	const int threads = static_cast<int>(std::max(1u, std::min(16u, std::thread::hardware_concurrency())));
	std::vector<OpticsParity> parts(static_cast<size_t>(threads));
	std::vector<std::thread> workers;
	for (int t = 0; t < threads; ++t) {
		workers.emplace_back([&, t] { parts[static_cast<size_t>(t)] = compare_rows(scene, rgba, w, h, t, threads); });
	}
	OpticsParity out;
	for (size_t t = 0; t < workers.size(); ++t) {
		workers[t].join();
		const OpticsParity &p = parts[t];
		out.covered += p.covered;
		out.over += p.over;
		if (p.worst > out.worst) {
			out.worst = p.worst;
			out.worst_x = p.worst_x;
			out.worst_y = p.worst_y;
		}
	}
	return out;
}

// ---- invariants

// ---- the long-wave range (GAME_SPEC §12.4, Stage 46)

std::string check_O21(const OpticsScene &scene, int x0, int y0, int x1, int y1, int step, int points, double tolerance) {
	using namespace optics_layout;
	if (!shader::ready(scene)) {
		return "O21: the scene is not the size the renderer reads";
	}
	if (!(scene.table[4 * 4 + 3] > 0.5f)) {
		return "O21: the scene is not read in the long-wave range";
	}
	char text[240];
	int read = 0;
	for (int y = y0; y < y1; y += step) {
		for (int x = x0; x < x1; x += step) {
			if (optics_cover(scene, x, y) <= 0.0) {
				continue;
			}
			for (int k = 0; k < points; ++k) {
				const double nm = chem::optics::LWIR_FIRST_NM + (chem::optics::LWIR_COUNT - 1.0) * chem::optics::LWIR_STEP_NM * (k + 0.5) / points;
				const double v = optics_through(scene, x, y, nm);
				++read;
				if (!(std::fabs(v - 1.0) <= tolerance)) {
					std::snprintf(text, sizeof text, "O21: pixel (%d, %d) at %g nm reads %.15g of the room's long-wave light, not 1", x, y, nm, v);
					return text;
				}
			}
		}
	}
	if (read == 0) {
		return "O21: no pixel of the region is a body's";
	}
	return "";
}

std::string check_O22(double pass, double absorbed, double turned, double tolerance) {
	char text[200];
	if (!(pass >= -tolerance && absorbed >= -tolerance && turned >= -tolerance) || !(std::fabs(pass + absorbed + turned - 1.0) <= tolerance)) {
		std::snprintf(text, sizeof text, "O22: a ray's shares are passed %.15g, taken in %.15g, turned away %.15g: not all of it", pass, absorbed, turned);
		return text;
	}
	return "";
}

std::string check_O23(const chem::optics::EnergyTable &light, double room_kelvin) {
	char text[200];
	const double closed = chem::optics::band_signal(room_kelvin);
	if (!(std::fabs(light.signal - closed) <= 1e-7 * closed)) {
		std::snprintf(text, sizeof text, "O23: the long-wave light's whole is %.12g W/m2/sr; the band's integral at %g K is %.12g", light.signal, room_kelvin, closed);
		return text;
	}
	for (double kelvin = 230.0; kelvin <= 2500.0; kelvin *= 1.21) {
		for (const double e : { 0.1, 0.3, 0.95, 1.0 }) {
			const double signal = e * chem::optics::band_signal(kelvin) + (1.0 - e) * chem::optics::band_signal(room_kelvin);
			const double back = chem::optics::brightness_kelvin(signal, e, room_kelvin);
			if (!(std::fabs(back - kelvin) <= 1e-3)) {
				std::snprintf(text, sizeof text, "O23: %g K at an emissivity of %g comes back as %.6f K", kelvin, e, back);
				return text;
			}
		}
	}
	return "";
}

std::string check_O24(const OpticsScene &with, const OpticsScene &without, int x0, int y0, int x1, int y1, int step) {
	char text[240];
	for (int y = y0; y < y1; y += step) {
		for (int x = x0; x < x1; x += step) {
			double a[3], b[3];
			optics_truth(with, x, y, a);
			optics_truth(without, x, y, b);
			for (int c = 0; c < 3; ++c) {
				if (a[c] != b[c]) {
					std::snprintf(text, sizeof text, "O24: pixel (%d, %d): %.17g with the long-wave range, %.17g without (channel %d)", x, y, a[c], b[c], c);
					return text;
				}
			}
		}
	}
	return "";
}

bool optics_ray_shares(const OpticsScene &scene, int body, double x, double y, double nm, bool truth, double out[3]) {
	using namespace optics_layout;
	if (!shader::ready(scene) || body < 0 || body >= static_cast<int>(scene.table[0])) {
		return false;
	}
	const shader::Use use(scene);
	shader::band_range = nm > 7000.0 ? 1 : 0;
	if (!shader::trace_bends(body)) {
		return false;
	}
	const double gap = shader::scene_vec(3).x, z = -shader::scene_vec(HEADER_VECS + body * BODY_VECS + 5).y * gap;
	const double from_z = -(shader::scene_vec(3).y + 1.0) * gap - 400.0;
	const int core = shader::trace_core(body, y);
	if (truth) {
		Line line{ x, y, from_z, 0.0, 0.0, 1.0 };
		const Exact e = exact_body(body, core, nm, z, line);
		if (!e.met || e.meetings == 0) {
			return false;
		}
		out[0] = e.pass;
		out[1] = e.absorbed;
		out[2] = e.turned;
		return true;
	}
	const shader::BodyMet e = shader::trace_body(body, core, nm, z, shader::vec3(x, y, from_z), shader::vec3(0.0, 0.0, 1.0));
	if (!e.met) {
		return false;
	}
	out[0] = e.pass;
	out[1] = e.absorbed;
	out[2] = e.turned;
	return true;
}

bool optics_relay_truth(const OpticsScene &scene, int x, int y, double rgb[3]) {
	using namespace optics_layout;
	optics_truth(scene, x, y, rgb); // what is in front of the display, and its face, in the visible
	if (!shader::ready(scene)) {
		return false;
	}
	const shader::Use use(scene);
	const shader::vec2 p(x + 0.5, y + 0.5);
	const shader::Path q = shader::path_of(p, 1.0);
	if (q.relay < 0) {
		return false;
	}
	const int b = shader::relay_block(q.relay);
	const shader::vec4 h0 = shader::image_vec(b), h1 = shader::image_vec(b + 1);
	// its primaries through its front, every share of the visible light its own band
	double a[3][3] = {};
	shader::band_range = 0;
	const int bands = 1024;
	for (int j = 0; j < bands; ++j) {
		const shader::vec4 lo = shader::share(static_cast<double>(j) / bands), hi = shader::share(static_cast<double>(j + 1) / bands);
		const double nm = shader::share((j + 0.5) / bands).x + 380.0, through = shader::relay_through(q, nm);
		for (int c = 0; c < 3; ++c) {
			const double light = through * shader::relay_primary(b, c, nm);
			const shader::vec3 lin = shader::to_linear_srgb(shader::vec3((hi.y - lo.y) * light, (hi.z - lo.z) * light, (hi.w - lo.w) * light));
			a[c][0] += lin.x;
			a[c][1] += lin.y;
			a[c][2] += lin.z;
		}
	}
	// its sensor pixel's mean long-wave light, by the truth's own tracer: a grid over the pixel, every share its band
	const shader::vec3 sensor = shader::relay_sensor(q.relay, p);
	shader::range_fixed = true;
	shader::band_range = 1;
	shader::ray_hide = static_cast<int>(h1.z);
	const int grid = 6, shares = 96;
	double sum = 0.0;
	for (int i = 0; i < grid; ++i) {
		for (int k = 0; k < grid; ++k) {
			const shader::vec2 at(sensor.x + ((i + 0.5) / grid - 0.5) * sensor.z, sensor.y + ((k + 0.5) / grid - 0.5) * sensor.z);
			for (int j = 0; j < shares; ++j) {
				sum += exact_through(at, 1.0e-3, shader::share((j + 0.5) / shares).x + 380.0);
			}
		}
	}
	shader::range_fixed = false;
	shader::ray_hide = 0;
	shader::band_range = 0;
	const double signal = sum / (grid * grid * shares);
	// as the camera reads it, and shows it
	const double own = (signal - (1.0 - h0.z) * h0.w) / std::max(static_cast<double>(h0.z), 1e-6);
	const double t = std::clamp((shader::infrared_kelvin(own) - h1.x) / std::max(static_cast<double>(h1.y - h1.x), 1e-6), 0.0, 1.0) * (RELAY_PALETTE - 1);
	const int k = std::min(static_cast<int>(std::floor(t)), RELAY_PALETTE - 2);
	const shader::vec4 w = shader::mix(shader::image_vec(b + RELAY_HEAD_VECS + k), shader::image_vec(b + RELAY_HEAD_VECS + k + 1), t - k);
	const shader::vec3 st = shader::relay_stripes(q.relay, p, 1.0);
	const double drive[3] = { w.x * st.x, w.y * st.y, w.z * st.z };
	for (int c = 0; c < 3; ++c) {
		for (int ch = 0; ch < 3; ++ch) {
			rgb[ch] += a[c][ch] * drive[c];
		}
	}
	return true;
}

std::string check_O25(const OpticsScene &scene, const OpticsFrames &frames, int x0, int y0, int x1, int y1, int step) {
	char text[240];
	int read = 0;
	for (int y = y0; y < y1; y += step) {
		for (int x = x0; x < x1; x += step) {
			double truth[3], rgba[4];
			if (!optics_relay_truth(scene, x, y, truth)) {
				continue;
			}
			frames.shown(scene, x, y, rgba);
			const shader::Use use(scene);
			const auto decode = [](double v) { return v <= 0.04045 ? v / 12.92 : std::pow((v + 0.055) / 1.055, 2.4); };
			const shader::vec3 seen(decode(rgba[0]), decode(rgba[1]), decode(rgba[2]));
			const double off = shader::eye(shader::lab_of(seen), shader::lab_of(shader::gamut(shader::vec3(truth[0], truth[1], truth[2]))));
			++read;
			if (!(off <= 8.0)) {
				std::snprintf(text, sizeof text, "O25: the display's pixel (%d, %d) is %.2f from its truth on the eye metric (shows %.4f %.4f %.4f, truth %.4f %.4f %.4f)", x, y, off, seen.x,
						seen.y, seen.z, truth[0], truth[1], truth[2]);
				return text;
			}
		}
	}
	if (read == 0) {
		return "O25: no pixel of the region is a sensor display's";
	}
	return "";
}

std::string check_O1(const OpticsScene &scene) {
	using namespace optics_layout;
	if (!shader::ready(scene)) {
		return "O1: the scene is not the size the renderer reads";
	}
	const int count = static_cast<int>(scene.table[0]);
	if (count < 0 || count > MAX_BODIES) {
		return "O1: more bodies than the scene holds";
	}
	char text[200];
	const int sets = static_cast<int>(scene.table[3]) / (ROWS_PER_SET * ROW); // (then their long-wave rows: Stage 46)
	for (int i = 0; i < count; ++i) {
		const float set = scene.table[static_cast<size_t>(HEADER_VECS + i * BODY_VECS + SET) * 4];
		if (!(set >= 0.0f) || set >= static_cast<float>(sets) || set != std::floor(set)) {
			std::snprintf(text, sizeof text, "O1: body %d is made of set %g of spectra; there are %d", i, static_cast<double>(set), sets);
			return text;
		}
		for (int k = 0; k < ROW; ++k) {
			const size_t base = static_cast<size_t>(static_cast<int>(scene.table[static_cast<size_t>(HEADER_VECS + i * BODY_VECS + SET) * 4]) * ROWS_PER_SET * ROW + k);
			const double liquid = scene.rows[base + LIQUID_ROW * ROW], solid = scene.rows[base + SOLID_ROW * ROW], wall = scene.rows[base + WALL_ROW * ROW];
			// (a screen's glow is its image's: its row is -1, Stage 42)
			const bool imaged = scene.table[static_cast<size_t>(HEADER_VECS + i * BODY_VECS + MORE) * 4 + 1] > 0.5f && scene.table[static_cast<size_t>(HEADER_VECS + i * BODY_VECS + MORE) * 4 + 1] < 1.5f && scene.table[static_cast<size_t>(HEADER_VECS + i * BODY_VECS + FORM) * 4] == 3.0f;
			const double glow = scene.rows[base + GLOW_ROW * ROW];
			if (!(liquid >= 0.0) || !(wall >= 0.0) || !(solid >= 0.0 && solid <= 1.0) || !(glow >= 0.0 || (imaged && glow == -1.0))) {
				std::snprintf(text, sizeof text, "O1: body %d at %d nm: a transmittance outside [0, 1] (depth %g, wall %g, reflectance %g)", i, 380 + k, liquid, wall, solid);
				return text;
			}
		}
		for (int k = 0; k < LWIR_ROW; ++k) {
			// its long-wave rows (Stage 46): absorptions no less than nothing, a reflectance within [0, 1], indices at
			// least 1, no light of its own
			const size_t base = static_cast<size_t>(scene.table[3]) + static_cast<size_t>(static_cast<int>(set) * ROWS_PER_SET * LWIR_ROW + k);
			const double liquid = scene.rows[base + LIQUID_ROW * LWIR_ROW], solid = scene.rows[base + SOLID_ROW * LWIR_ROW], wall = scene.rows[base + WALL_ROW * LWIR_ROW];
			const double n_liquid = scene.rows[base + LIQUID_INDEX_ROW * LWIR_ROW], n_wall = scene.rows[base + WALL_INDEX_ROW * LWIR_ROW], glow = scene.rows[base + GLOW_ROW * LWIR_ROW];
			if (!(liquid >= 0.0) || !(wall >= 0.0) || !(solid >= 0.0 && solid <= 1.0) || !(n_liquid >= 1.0) || !(n_wall >= 1.0) || glow != 0.0) {
				std::snprintf(text, sizeof text, "O1: body %d at %g nm: a long-wave row out of its range (depth %g, wall %g, reflectance %g, n %g, %g, light %g)", i,
						chem::optics::lwir_nm(k), liquid, wall, solid, n_liquid, n_wall, glow);
				return text;
			}
		}
		const size_t body = static_cast<size_t>(HEADER_VECS + i * BODY_VECS) * 4;
		const bool prism = scene.table[body + static_cast<size_t>(FORM) * 4] > 1.5f; // its MORE holds its corners (a thing of the lab's, its ends)
		const double extinction = scene.table[body + static_cast<size_t>(MORE) * 4], thickness = scene.table[body + static_cast<size_t>(MORE) * 4 + 1];
		if (!prism && (!(extinction >= 0.0) || !(thickness >= 0.0))) {
			std::snprintf(text, sizeof text, "O1: body %d: a negative extinction or wall (%g, %g)", i, extinction, thickness);
			return text;
		}
		if (scene.table[body + static_cast<size_t>(MORE) * 4 + 1] > 0.5f && scene.table[body + static_cast<size_t>(MORE) * 4 + 1] < 1.5f && scene.table[body + static_cast<size_t>(FORM) * 4] == 3.0f) {
			// a screen (Stage 42): its image's channels no less than nothing, its weights within [0, 1], its rows'
			// sums its weights'
			const int img = static_cast<int>(scene.table[body + static_cast<size_t>(MORE) * 4]);
			if (img < 0 || static_cast<size_t>(img + IMAGE_HEAD_VECS) * 4 > scene.images.size()) {
				std::snprintf(text, sizeof text, "O1: body %d: its image is not in the scene", i);
				return text;
			}
			const float *head = &scene.images[static_cast<size_t>(img) * 4];
			const int w = static_cast<int>(head[0]), h = static_cast<int>(head[1]), channels = static_cast<int>(head[4]), sums = static_cast<int>(head[5]);
			const size_t weights = static_cast<size_t>(head[6]);
			if (w < 1 || h < 1 || static_cast<size_t>(sums + h * (w + 1)) * 4 > scene.images.size() || weights + static_cast<size_t>(w * h * 3) > scene.image_weights.size()) {
				std::snprintf(text, sizeof text, "O1: body %d: its image's %d by %d pixels are not all in the scene", i, w, h);
				return text;
			}
			for (int k = 0; k < 3 * ROW; ++k) {
				if (!(scene.images[static_cast<size_t>(channels) * 4 + static_cast<size_t>(k)] >= 0.0f)) {
					std::snprintf(text, sizeof text, "O1: body %d: its screen's channel %d is less than nothing at %d nm", i, k / ROW, 380 + k % ROW);
					return text;
				}
			}
			for (int y = 0; y < h; ++y) {
				double run[3] = { 0.0, 0.0, 0.0 };
				for (int x = 0; x < w; ++x) {
					for (int c = 0; c < 3; ++c) {
						const double v = scene.image_weights[weights + (static_cast<size_t>(y) * static_cast<size_t>(w) + static_cast<size_t>(x)) * 3 + static_cast<size_t>(c)];
						run[c] += v;
						const double held = scene.images[static_cast<size_t>(sums + y * (w + 1) + x + 1) * 4 + static_cast<size_t>(c)];
						if (!(v >= 0.0 && v <= 1.0) || !(std::fabs(held - run[c]) <= 1e-4 * std::max(1.0, run[c]))) {
							std::snprintf(text, sizeof text, "O1: body %d: its image's pixel (%d, %d) has a weight %g outside [0, 1] or its row's sum is not its pixels' (%g, %g)", i, x, y, v, held, run[c]);
							return text;
						}
					}
				}
			}
		}
		const int first = static_cast<int>(scene.table[body + static_cast<size_t>(IDS) * 4 + 2]), lines = static_cast<int>(scene.table[body + static_cast<size_t>(IDS) * 4 + 3]);
		for (int j = first; j < first + lines; ++j) {
			if (j < 0 || j >= MAX_LINES || !(scene.lines[static_cast<size_t>(j) * 4 + 1] > 0.0) || !(scene.lines[static_cast<size_t>(j) * 4 + 2] >= 0.0)) {
				std::snprintf(text, sizeof text, "O1: body %d: line %d has no width or a negative depth", i, j);
				return text;
			}
		}
	}
	return "";
}

std::string check_O2(const OpticsScene &scene, int x, int y) {
	if (!shader::ready(scene)) {
		return "O2: no scene";
	}
	// the brightest of the wall, the room's white, the room in front and what is drawn on the wall
	double most = std::max({ 1.0, static_cast<double>(scene.table[1]), static_cast<double>(scene.table[2]), static_cast<double>(scene.table[2 * 4 + 1]) });
	for (size_t k = 4; k < scene.drawn.size(); k += 5) {
		most = std::max(most, static_cast<double>(scene.drawn[k]));
	}
	// and what the bodies emit at that wavelength (Stage 22): a glowing solid or a lamp its light where it is
	// seen, glowing gas its light for each pixel of the longest path through it
	const auto emitted = [&](double nm) {
		const shader::Use use(scene);
		double sum = 0.0;
		for (int b = 0; b < static_cast<int>(scene.table[0]); ++b) {
			const size_t at = static_cast<size_t>(optics_layout::HEADER_VECS + b * optics_layout::BODY_VECS);
			const double path = scene.table[(at + optics_layout::FORM) * 4] > 4.5f ? static_cast<double>(scene.table[(at + optics_layout::RECT) * 4 + 2]) : 0.0;
			const double glow = shader::row_at(shader::set_of(b) * optics_layout::ROWS_PER_SET * optics_layout::ROW + optics_layout::GLOW_ROW * optics_layout::ROW, nm);
			if (glow < 0.0) {
				// a screen (Stage 42): no more than all three of its channels full
				const int img = static_cast<int>(scene.table[(at + optics_layout::MORE) * 4]);
				for (int c = 0; c < 3; ++c) {
					sum += shader::image_channel(img, c, nm);
				}
				continue;
			}
			sum += glow * (1.0 + path);
		}
		return sum;
	};
	// at each nanometre, the spectra's own points (Stage 40: it was each half, the halves the tables' interpolation)
	for (int k = 0; k <= 400; ++k) {
		const double nm = 380.0 + k, t = optics_through(scene, x, y, nm);
		if (!(t >= 0.0 && t <= (most + emitted(nm)) * (1.0 + 1e-12))) {
			char text[160];
			std::snprintf(text, sizeof text, "O2: at (%d, %d), %g nm carries %g: more than came in or was emitted", x, y, nm, t);
			return text;
		}
	}
	return "";
}

} // namespace lab
