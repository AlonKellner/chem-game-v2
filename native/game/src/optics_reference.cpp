// The reference renderer: the per-pixel source compiled as C++, in float64 (GAME_SPEC §12, §12.2).
#include "lab/optics_scene.hpp"
#include "lab/optics_trace.hpp"

#include "lab/colour.hpp"

#include <algorithm>
#include <tuple>
#include <map>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <thread>
#include <vector>

namespace lab {

namespace shader {

// what the shading language has and C++ has not, in float64
using uint = std::uint32_t;
struct vec2 {
	double x = 0.0, y = 0.0;
	vec2(double a, double b) :
			x(a), y(b) {}
};
struct vec3 {
	double x = 0.0, y = 0.0, z = 0.0;
	vec3() = default;
	explicit vec3(double a) :
			x(a), y(a), z(a) {}
	vec3(double a, double b, double c) :
			x(a), y(b), z(c) {}
};
struct vec4 {
	double x = 0.0, y = 0.0, z = 0.0, w = 0.0;
	double &operator[](int i) { return i == 0 ? x : i == 1 ? y : i == 2 ? z : w; }
	double operator[](int i) const { return i == 0 ? x : i == 1 ? y : i == 2 ? z : w; }
	vec4() = default;
	explicit vec4(double a) :
			x(a), y(a), z(a), w(a) {}
	vec4(double a, double b, double c, double d) :
			x(a), y(b), z(c), w(d) {}
};
inline vec3 operator+(const vec3 &a, const vec3 &b) {
	return vec3(a.x + b.x, a.y + b.y, a.z + b.z);
}
inline vec3 operator-(const vec3 &a, const vec3 &b) {
	return vec3(a.x - b.x, a.y - b.y, a.z - b.z);
}
inline vec3 operator*(const vec3 &a, double s) {
	return vec3(a.x * s, a.y * s, a.z * s);
}
inline vec3 operator/(const vec3 &a, double s) {
	return vec3(a.x / s, a.y / s, a.z / s);
}
inline vec4 operator+(const vec4 &a, const vec4 &b) {
	return vec4(a.x + b.x, a.y + b.y, a.z + b.z, a.w + b.w);
}
inline vec4 operator*(const vec4 &a, double s) {
	return vec4(a.x * s, a.y * s, a.z * s, a.w * s);
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
inline double dot(const vec4 &a, const vec4 &b) {
	return a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
}
inline double mix(double a, double b, double t) {
	return a * (1.0 - t) + b * t;
}
inline vec3 mix(const vec3 &a, const vec3 &b, double t) {
	return a * (1.0 - t) + b * t;
}
inline vec4 mix(const vec4 &a, const vec4 &b, double t) {
	return a * (1.0 - t) + b * t;
}

// the scene being drawn (one a thread: the comparison below runs on several)
thread_local const OpticsScene *current = nullptr;
inline vec4 scene_vec(int i) {
	const float *v = &current->table[static_cast<size_t>(i) * 4];
	return vec4(v[0], v[1], v[2], v[3]);
}
inline double row_value(int i) {
	return current->rows[static_cast<size_t>(i)];
}
inline vec4 line_vec(int i) {
	const float *v = &current->lines[static_cast<size_t>(i) * 4];
	return vec4(v[0], v[1], v[2], v[3]);
}
inline vec4 light_vec(int i) {
	const float *v = &current->lights[static_cast<size_t>(i) * 4];
	return vec4(v[0], v[1], v[2], v[3]);
}
inline vec4 map_vec(int i) {
	const float *v = &current->maps[static_cast<size_t>(i) * 4];
	return vec4(v[0], v[1], v[2], v[3]);
}
inline vec4 stack_vec(int i) {
	const float *v = &current->stacks[static_cast<size_t>(i) * 4];
	return vec4(v[0], v[1], v[2], v[3]);
}
inline vec4 wall_vec(int i) {
	const float *v = &current->wall[static_cast<size_t>(i) * 4];
	return vec4(v[0], v[1], v[2], v[3]);
}

#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wkeyword-macro"
#endif
#define float double
#define OPTICS_CHAIN 1 // the reference reads every pixel: a prism's and a stack's too (the host compiles those apart)
#define OPTICS_FOLLOW 1 // and follows the rays of a stack, which runs on the CPU alone
#include "../shader/optics_pixel.glsl"
#undef float
#if defined(__clang__)
#pragma clang diagnostic pop
#endif

static_assert(HEADER_VECS == optics_layout::HEADER_VECS && BODY_VECS == optics_layout::BODY_VECS && ROW == optics_layout::ROW && ROWS_PER_SET == optics_layout::ROWS_PER_SET &&
				LIGHT_ENTRIES == optics_layout::LIGHT_ENTRIES,
		"the per-pixel source and lab/optics_scene.hpp lay the scene out alike");

struct Use {
	explicit Use(const OpticsScene &s) { current = &s; }
	~Use() { current = nullptr; }
	Use(const Use &) = delete;
	Use &operator=(const Use &) = delete;
};

bool ready(const OpticsScene &s) {
	return s.table.size() == static_cast<size_t>(optics_layout::SIZE_VECS) * 4 && s.rows.size() % static_cast<size_t>(optics_layout::ROWS_PER_SET * optics_layout::ROW) == 0 && !s.wall.empty() &&
		   s.stacks.size() >= static_cast<size_t>(optics_layout::MAX_BODIES) * 4 &&
		   s.lines.size() == static_cast<size_t>(optics_layout::MAX_LINES) * 4 && s.lights.size() == static_cast<size_t>(optics_layout::LIGHTS * optics_layout::LIGHT_SIZE) * 4;
}

Path at(int x, int y) {
	return path_of(vec2(x + 0.5, y + 0.5), 1.0);
}

} // namespace shader

double optics_through(const OpticsScene &scene, int x, int y, double nm) {
	if (!shader::ready(scene)) {
		return 0.0;
	}
	const shader::Use use(scene);
	return shader::band_light(shader::at(x, y), 0.0, 0.0, 0.0, nm);
}

double optics_through_general(const OpticsScene &scene, int x, int y, double nm) {
	if (!shader::ready(scene)) {
		return 0.0;
	}
	const shader::Use use(scene);
	return shader::general_band(shader::at(x, y), 0.0, 0.0, 0.0, nm);
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
	// the pixel's central ray, followed through every layer; in the row its centre is in
	const shader::Stack stack = shader::stack_of(q, true);
	const shader::Ray r = shader::chain_at(stack, q.y, q.x, nm);
	OpticsBend out{ r.x, 1.0, 0.0 };
	bool first = true;
	for (int j = 0; j < stack.count; ++j) {
		if (r.bent[j]) {
			out.pass *= r.pass[j];
			out.chord = first ? r.chord[j] : out.chord; // its chord in the nearest body that bent it
			first = false;
		}
	}
	return out;
}

// The reference's heavy loops (thousands of exact rays a pixel) are dealt among the machine's threads: `count`
// pieces, each done whole by one thread. A piece is the same piece on every machine and results are joined in
// the pieces' order, so a sum comes out the same however many threads there are. The scene in use is each
// thread's own (shader::current): a piece names its own.
template <typename Piece>
void in_pieces(int count, const Piece &piece) {
	// half the machine's threads, six at most (measured on six fast cores and six slow ones: 20 s on one thread,
	// 7.6 s on four, 14 s on twelve); or LAB_THREADS, where several such loops run side by side (ctest)
	static const int most = [] {
		const char *asked = std::getenv("LAB_THREADS");
		const int n = asked != nullptr ? std::atoi(asked) : static_cast<int>(std::min(6u, std::thread::hardware_concurrency() / 2));
		return n < 1 ? 1 : n > 64 ? 64 : n;
	}();
	const int threads = std::max(1, std::min(count, most));
	if (threads == 1) {
		for (int i = 0; i < count; ++i) {
			piece(i);
		}
		return;
	}
	std::atomic<int> next{ 0 };
	std::vector<std::thread> workers;
	for (int t = 0; t < threads; ++t) {
		workers.emplace_back([&] {
			for (int i = next.fetch_add(1); i < count; i = next.fetch_add(1)) {
				piece(i);
			}
		});
	}
	for (std::thread &w : workers) {
		w.join();
	}
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
// glassware's whole profile, or a glass cylinder, or the prism's faces. Independent of the renderer's maps,
// footprints and chain: it takes a ray from anywhere.
struct Exact {
	bool met = false; // the ray met the body
	double entry = 0.0; // how far along the line, from where it was given (pixels)
	double pass = 1.0, chord = 0.0; // what its surfaces passed; its path in the liquid (cm)
	int meetings = 0;
	bool out = true; // it left the body (not still inside after every meeting allowed)
};

// `line` through body `body`, whose axis stands at the depth z_axis, its core its liquid (0), the air over
// it (1) or glass all through (2: a cylinder's floor; glassware's floor is in its shape); the line it leaves
// on replaces `line`.
Exact exact_body(int body, int core, double nm, double z_axis, Line &line) {
	const bool liquid = core == 0;
	Exact out;
	using shader::row_at;
	using shader::scene_vec;
	const int at = optics_layout::HEADER_VECS + body * optics_layout::BODY_VECS;
	const shader::vec4 rect = scene_vec(at), form = scene_vec(at + 1);
	const bool prism = form.x > 1.5;
	const double cm = form.y, rr = rect.z * 0.5 * cm, rt = scene_vec(at + 5).x;
	const double axis = rect.x + rect.z * 0.5 + (prism ? scene_vec(at + 3).z : 0.0), foot = static_cast<double>(rect.y) + rect.w;
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
	// prism: 0 air, 1 glass; a vessel: 0 air, 1 glass, 2 its core
	const std::array<double, trace::MAX_MEDIA> index{ 1.0, n_glass, prism ? n_glass : n_in, 1.0 };
	// from well before the body, on the line; the body's frame: cm from its axis, y up from its foot
	const double back = 1000.0;
	const trace::Vec3 u{ line.ux, -line.uy, line.uz };
	const trace::Vec3 o{ (line.x - axis) * cm - back * u.x, (foot - line.y) * cm - back * u.y, (line.z - z_axis) * cm - back * u.z };
	const trace::Landing l = trace::trace(*shape, o, u, 1.0e9, index);
	out.meetings = l.meetings;
	if (l.meetings == 0) {
		return out;
	}
	out.met = true;
	out.entry = (l.chord[0] - back) / cm;
	out.pass = l.pass;
	out.chord = liquid && !prism ? l.chord[trace::CORE] : 0.0;
	out.out = l.left;
	line = Line{ axis + l.at.x / cm, foot - l.at.y / cm, z_axis + l.at.z / cm, l.toward.x, -l.toward.y, l.toward.z };
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
};
struct ExactRay {
	double x = 0.0, y = 0.0;
	std::vector<Met> met;
};

ExactRay exact_ray(double x, double y, double nm) {
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
	bool lands = true;
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
				out.met.push_back(Met{ k, cross, cross_y, true, e.pass, e.chord });
				if (!e.out || line.uz <= 0.05) {
					lands = false;
					out.x = line.ux < 0.0 ? -100000.0 : 100000.0;
					out.y = line.y;
					break;
				}
			} else if (!bends(k)) {
				out.met.push_back(Met{ k, cross, cross_y, false, 1.0, 0.0 });
			}
		}
		last = first - 1;
	}
	if (lands) {
		out.x = line.x + (wall - line.z) * line.ux / line.uz;
		out.y = line.y + (wall - line.z) * line.uy / line.uz;
	}
	return out;
}

// the wall at a point, from what is drawn on it (not from the renderer's table): the bench or the room,
// then each shape in painting order
double drawn_at(double x, double y) {
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

// what one ray of the room's light brings at nm, at the point p of the screen (`size` across, for the
// bodies' outlines): the truth's own tracing through every body, and its own reading of the wall; each body
// read where the ray met it, gathered from the eye inward
double exact_through(const shader::vec2 &p, double size, double nm) {
	const shader::Path q = shader::path_of(p, size);
	// beside every body, and behind what bends nothing, the wall itself as the screen has it, over the sample
	const double half = 0.5 * size;
	if (q.layers == 0 || q.refractor < 0) {
		return shader::along(q, nm, 1.0, 0.0, 0.0, false, size > 1.0e-3 ? drawn_over(p.x - half, p.x + half, p.y - half, p.y + half) : drawn_at(p.x, p.y));
	}
	const ExactRay ray = exact_ray(p.x, p.y, nm);
	const double room = shader::scene_vec(2).y;
	double a = 1.0, b = 0.0;
	for (const Met &m : ray.met) {
		const shader::Layer l = shader::layer_at(m.body, shader::vec2(m.cross, m.cross_y), size, size);
		const int start = shader::set_of(m.body) * optics_layout::ROWS_PER_SET * optics_layout::ROW;
		const double per_cm = shader::liquid_at(m.body, start, nm);
		const shader::vec2 does = shader::body_does(l, shader::row_at(start + optics_layout::ROW, nm), shader::row_at(start + 5 * optics_layout::ROW, nm), shader::row_at(start + 2 * optics_layout::ROW, nm),
				per_cm * (m.bent ? m.chord : l.depth_cm), m.pass, room);
		b += a * does.y;
		a *= does.x;
	}
	return a * drawn_at(ray.x, ray.y) + b;
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
		room.bench = room.elsewhere = radiance;
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

void optics_pixel(const OpticsScene &scene, int x, int y, double rgba[4]) {
	double rgb[3];
	optics_truth(scene, x, y, rgb);
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

// ---- the renderer's own frames

struct OpticsFrames::State {
	shader::PixelState s;
};

OpticsFrames::OpticsFrames(int x, int y, int w, int h, double budget) :
		x_(x), y_(y), w_(w > 0 ? w : 0), h_(h > 0 ? h : 0), budget_(budget), states_(new std::vector<State>(static_cast<size_t>(w_) * static_cast<size_t>(h_))),
		restarted_(static_cast<size_t>(w_) * static_cast<size_t>(h_), 0) {
	for (State &st : *states_) {
		st.s.hist = st.s.prox = st.s.aux = shader::vec4(0.0);
		st.s.key = shader::vec4(-1.0); // matches no content: the first frame starts every history
	}
}

OpticsFrames::~OpticsFrames() {
	delete states_;
}

void OpticsFrames::frame(const OpticsScene &scene) {
	if (!shader::ready(scene)) {
		return;
	}
	const double scale = shader::budget_scale(asked_, budget_, static_cast<double>(w_) * h_);
	// a row a piece: a pixel's step touches its own state alone
	std::vector<double> asked_of(static_cast<size_t>(h_), 0.0), points_of(static_cast<size_t>(h_), 0.0);
	in_pieces(h_, [&](int row) {
		const shader::Use use(scene);
		double &asked = asked_of[static_cast<size_t>(row)], &points = points_of[static_cast<size_t>(row)];
		for (int col = 0; col < w_; ++col) {
			const size_t i = static_cast<size_t>(row) * static_cast<size_t>(w_) + static_cast<size_t>(col);
			const int x = x_ + col, y = y_ + row;
			const shader::Path q = shader::at(x, y);
			restarted_[i] = 0;
			if (q.cover <= 0.0) {
				continue; // nothing here: the pixel keeps what it had, and asks nothing
			}
			shader::PixelState &st = (*states_)[i].s;
			if (shader::done(q, st.key, st.aux)) {
				st.aux.w = 0.0; // as the GPU, it does nothing; what it spent is nothing
				continue;
			}
			const double before = st.hist.w;
			st = shader::step_pixel(q, st, shader::vec2(x + 0.5, y + 0.5), frame_, scale);
			// a history that had points and is now one frame old (or none) started again
			restarted_[i] = before > 0.0 && st.aux.y <= 1.0 ? 1 : 0;
			asked += st.aux.z;
			points += st.aux.w;
		}
	});
	double asked = 0.0, points = 0.0;
	for (int row = 0; row < h_; ++row) {
		asked += asked_of[static_cast<size_t>(row)];
		points += points_of[static_cast<size_t>(row)];
	}
	asked_ = asked;
	points_ = points;
	++frame_;
}

void OpticsFrames::colour(const OpticsScene &scene, int x, int y, double rgb[3]) const {
	rgb[0] = rgb[1] = rgb[2] = 0.0;
	const int col = x - x_, row = y - y_;
	if (!shader::ready(scene) || col < 0 || row < 0 || col >= w_ || row >= h_) {
		return;
	}
	const shader::Use use(scene);
	const shader::PixelState &st = (*states_)[static_cast<size_t>(row) * static_cast<size_t>(w_) + static_cast<size_t>(col)].s;
	rgb[0] = st.prox.x + st.hist.x;
	rgb[1] = st.prox.y + st.hist.y;
	rgb[2] = st.prox.z + st.hist.z;
}

void OpticsFrames::shown(const OpticsScene &scene, int x, int y, double rgba[4]) const {
	rgba[0] = rgba[1] = rgba[2] = rgba[3] = 0.0;
	const int col = x - x_, row = y - y_;
	if (!shader::ready(scene) || col < 0 || row < 0 || col >= w_ || row >= h_) {
		return;
	}
	const shader::Use use(scene);
	const shader::vec4 c = shader::shown(shader::at(x, y), (*states_)[static_cast<size_t>(row) * static_cast<size_t>(w_) + static_cast<size_t>(col)].s);
	rgba[0] = c.x;
	rgba[1] = c.y;
	rgba[2] = c.z;
	rgba[3] = c.w;
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
	const int col = x - x_, row = y - y_;
	if (col < 0 || row < 0 || col >= w_ || row >= h_) {
		return 0.0;
	}
	return (*states_)[static_cast<size_t>(row) * static_cast<size_t>(w_) + static_cast<size_t>(col)].s.aux.w;
}

bool OpticsFrames::restarted(int x, int y) const {
	const int col = x - x_, row = y - y_;
	return col >= 0 && row >= 0 && col < w_ && row < h_ && restarted_[static_cast<size_t>(row) * static_cast<size_t>(w_) + static_cast<size_t>(col)] != 0;
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
	const int sets = static_cast<int>(scene.rows.size()) / (ROWS_PER_SET * ROW);
	for (int i = 0; i < count; ++i) {
		const float set = scene.table[static_cast<size_t>(HEADER_VECS + i * BODY_VECS + SET) * 4];
		if (!(set >= 0.0f) || set >= static_cast<float>(sets) || set != std::floor(set)) {
			std::snprintf(text, sizeof text, "O1: body %d is made of set %g of spectra; there are %d", i, static_cast<double>(set), sets);
			return text;
		}
		for (int k = 0; k < ROW; ++k) {
			const size_t base = static_cast<size_t>(static_cast<int>(scene.table[static_cast<size_t>(HEADER_VECS + i * BODY_VECS + SET) * 4]) * ROWS_PER_SET * ROW + k);
			const double liquid = scene.rows[base + LIQUID_ROW * ROW], solid = scene.rows[base + SOLID_ROW * ROW], wall = scene.rows[base + WALL_ROW * ROW];
			if (!(liquid >= 0.0) || !(wall >= 0.0) || !(solid >= 0.0 && solid <= 1.0) || !(scene.rows[base + GLOW_ROW * ROW] >= 0.0)) {
				std::snprintf(text, sizeof text, "O1: body %d at %d nm: a transmittance outside [0, 1] (depth %g, wall %g, reflectance %g)", i, 380 + k, liquid, wall, solid);
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
			sum += shader::row_at(shader::set_of(b) * optics_layout::ROWS_PER_SET * optics_layout::ROW + optics_layout::GLOW_ROW * optics_layout::ROW, nm) * (1.0 + path);
		}
		return sum;
	};
	for (int k = 0; k <= 800; ++k) {
		const double nm = 380.0 + 0.5 * k, t = optics_through(scene, x, y, nm);
		if (!(t >= 0.0 && t <= (most + emitted(nm)) * (1.0 + 1e-12))) {
			char text[160];
			std::snprintf(text, sizeof text, "O2: at (%d, %d), %g nm carries %g: more than came in or was emitted", x, y, nm, t);
			return text;
		}
	}
	return "";
}

// ---- the stacks (GAME_SPEC §12.3, Stage 21): the rays of a vessel with bodies behind it, followed here

namespace {

uint64_t fnv_of(const void *data, size_t size, uint64_t h = 1469598103934665603ull) {
	const auto *bytes = static_cast<const unsigned char *>(data);
	for (size_t i = 0; i < size; ++i) {
		h = (h ^ bytes[i]) * 1099511628211ull;
	}
	return h;
}

} // namespace

void optics_stacks(OpticsScene &scene) {
	namespace L = optics_layout;
	const size_t directory = static_cast<size_t>(L::MAX_BODIES) * 4;
	std::vector<float> out(directory, 0.0f);
	for (int b = 0; b < L::MAX_BODIES; ++b) {
		out[static_cast<size_t>(b) * 4] = -1.0f;
	}
	scene.stack_keys.resize(static_cast<size_t>(L::MAX_BODIES), 0);
	scene.stack_blocks.resize(static_cast<size_t>(L::MAX_BODIES));
	std::vector<bool> has(static_cast<size_t>(L::MAX_BODIES), false);
	if (scene.table.size() == static_cast<size_t>(L::SIZE_VECS) * 4 && scene.rows.size() % static_cast<size_t>(L::ROWS_PER_SET * L::ROW) == 0 && !scene.wall.empty()) {
		if (scene.stacks.size() < directory) {
			scene.stacks = out; // following a ray reads no stack; the pixel's path may look for one
		}
		const shader::Use use(scene);
		const int count = static_cast<int>(scene.table[0]);
		const auto vec = [&](int body, int which) { return &scene.table[static_cast<size_t>(L::HEADER_VECS + body * L::BODY_VECS + which) * 4]; };
		const auto bends = [&](int body) { return vec(body, L::REFRACT)[3] > 0.5f && (vec(body, L::FORM)[0] < 0.5f || vec(body, L::FORM)[0] > 1.5f); };
		int64_t followed = 0;
		// a body's maps are of one piece: its one map, or every band of its table of rows' bands (Stage 24)
		const auto one_piece = [&](int body) {
			const float *map = vec(body, L::MAP);
			if (map[3] >= 0.0f) {
				return map[3] == 1.0f;
			}
			for (int k = 0; k < static_cast<int>(-map[3]); ++k) {
				if (scene.maps[(static_cast<size_t>(map[0]) + 1 + static_cast<size_t>(k)) * 4 + 2] != 1.0f) {
					return false;
				}
			}
			return true;
		};
		for (const int b : scene.chain_bodies) {
			if (b < 0 || b >= count || !bends(b) || !one_piece(b)) {
				continue; // a map of several pieces (a prism) is read as one vessel's
			}
			const float *rect = vec(b, L::RECT);
			const double layer = vec(b, L::REFRACT)[1], top = rect[1] - 2.0, bottom = static_cast<double>(rect[1]) + rect[3] + 2.0, centre = rect[0] + rect[2] * 0.5;
			// its slots, the nearest the eye first (the last painted of the nearest layer): itself; what stands
			// behind it, in its rows; what stands across it on its own layer; what bends nothing in front of it
			std::vector<int> slots;
			for (int o = count - 1; o >= 0; --o) {
				const float *other = vec(o, L::RECT);
				const double other_layer = vec(o, L::REFRACT)[1];
				const bool rows = other[1] < bottom && static_cast<double>(other[1]) + other[3] > top;
				const bool across = other[0] < rect[0] + rect[2] + 2.0f && other[0] + other[2] + 2.0f > rect[0];
				if (o == b || (rows && (other_layer < layer || (across && (other_layer == layer || !bends(o)))))) {
					slots.push_back(o);
				}
			}
			if (slots.size() > static_cast<size_t>(shader::MAX_VISITS)) {
				// more than a ray is followed through: what stands behind its own columns before what is beside
				// them, and there the largest first (a part's face before its lamp and its ports: Stage 26), then
				// the nearest across
				std::vector<int> nearest = slots;
				std::stable_sort(nearest.begin(), nearest.end(), [&](int p, int q) {
					const auto rank = [&](int o) {
						const float *r = vec(o, L::RECT);
						const bool behind = r[0] < rect[0] + rect[2] && r[0] + r[2] > rect[0];
						return std::make_tuple(o == b ? 0 : 1, behind ? 0 : 1, -static_cast<double>(r[2]) * r[3], std::fabs(r[0] + r[2] * 0.5 - centre));
					};
					return rank(p) < rank(q);
				});
				nearest.resize(static_cast<size_t>(shader::MAX_VISITS));
				std::vector<int> kept;
				for (const int o : slots) {
					if (std::find(nearest.begin(), nearest.end(), o) != nearest.end()) {
						kept.push_back(o);
					}
				}
				slots = kept;
			}
			// what it is followed for: where each of its bodies stands, its shape, its surface, its map
			uint64_t key = fnv_of(&scene.table[2 * 4], 2 * 4 * sizeof(float));
			for (const int o : slots) {
				key = fnv_of(vec(o, L::RECT), 4 * sizeof(float), key);
				key = fnv_of(vec(o, L::FORM), 3 * sizeof(float), key);
				key = fnv_of(vec(o, L::REFRACT), 4 * sizeof(float), key);
				key = fnv_of(vec(o, L::MAP), 4 * sizeof(float), key);
				const uint64_t map_key = static_cast<size_t>(o) < scene.map_keys.size() ? scene.map_keys[static_cast<size_t>(o)] : 0;
				key = fnv_of(&map_key, sizeof map_key, key);
				key = fnv_of(&o, sizeof o, key);
			}
			key |= 1u;
			has[static_cast<size_t>(b)] = true;
			std::vector<float> &block = scene.stack_blocks[static_cast<size_t>(b)];
			if (scene.stack_keys[static_cast<size_t>(b)] == key && !block.empty()) {
				continue;
			}
			scene.stack_keys[static_cast<size_t>(b)] = key;
			// the stack as a ray is followed through it
			shader::Stack stack = shader::stack_of(shader::path_of(shader::vec2(rect[0], rect[1]), 1.0), false);
			int bending = 0;
			std::vector<double> edges{ top, bottom };
			for (size_t j = 0; j < slots.size(); ++j) {
				const int o = slots[j];
				const float *r = vec(o, L::RECT), *form = vec(o, L::FORM);
				stack.body[j] = o;
				stack.layer[j] = vec(o, L::REFRACT)[1];
				stack.bends[j] = bends(o);
				stack.round[j] = form[0] < 0.5f;
				stack.axis[j] = r[0] + r[2] * 0.5 + (form[0] > 1.5f ? static_cast<double>(vec(o, L::GLOW)[2]) : 0.0);
				stack.radius[j] = r[2] * 0.5;
				stack.top[j] = r[1];
				stack.foot[j] = static_cast<double>(r[1]) + r[3];
				stack.surface[j] = form[2];
				stack.base[j] = stack.foot[j] - static_cast<double>(vec(o, L::REFRACT)[2]) / form[1];
				if (stack.bends[j]) {
					const float *map = vec(o, L::MAP);
					// a table of rows' bands: its first band's map's head (every band's is alike)
					const size_t first_map = map[3] >= 0.0f ? static_cast<size_t>(map[0]) : static_cast<size_t>(map[0]) + static_cast<size_t>(scene.maps[(static_cast<size_t>(map[0]) + 1) * 4 + 1]);
					const float *head = &scene.maps[first_map * 4];
					stack.map[j] = static_cast<int>(map[0]);
					stack.coarse[j] = static_cast<int>(map[1]);
					stack.left[j] = map[2];
					stack.pieces[j] = static_cast<int>(map[3]);
					stack.media[j] = static_cast<int>(head[1]);
					stack.samples[j] = static_cast<int>(head[2]);
					if (bending == 0) {
						stack.first = static_cast<int>(j);
					}
					++bending;
					// its rows change what it is: a band ends at its top, its floor and its surface
					for (const double e : { stack.top[j], stack.foot[j], stack.surface[j], stack.base[j] }) {
						if (e > top + 1e-4 && e < bottom - 1e-4) {
							edges.push_back(e);
						}
					}
					// and, for one with a table of knots, every two pixels at most down its height (Stage 25: within a
					// band of the stack its rows are read at the band's middle; knots crowd where rays graze)
					if (map[3] < 0.0f) {
						double last = -1.0e9;
						for (int k = 0; k < static_cast<int>(-map[3]); ++k) {
							const double e = stack.top[j] + scene.maps[(static_cast<size_t>(map[0]) + 1 + static_cast<size_t>(k)) * 4];
							if (e > top + 1e-4 && e < bottom - 1e-4 && e - last >= 2.0) {
								edges.push_back(e);
								last = e;
							}
						}
					}
				}
			}
			stack.count = static_cast<int>(slots.size());
			stack.chained = bending > 1;
			std::sort(edges.begin(), edges.end());
			std::vector<double> bands; // tops, then the last bottom
			for (const double e : edges) {
				if (bands.empty() || e > bands.back() + 1e-4) {
					bands.push_back(e);
				}
			}
			const int band_count = static_cast<int>(bands.size()) - 1, slot_count = stack.count;
			// its samples: four a pixel, on the screen's own quarter pixels, from a pixel before its first to one after its last
			const int px_min = static_cast<int>(std::floor(rect[0] - 1.0f)), px_max = static_cast<int>(std::floor(rect[0] + rect[2] + 1.0f));
			const int first = px_min - 1, coarse = px_max - px_min + 3, samples = coarse * 4 + 1, stride = 2 + 4 * slot_count;
			const size_t cells_at = static_cast<size_t>(2 + band_count + slot_count);
			block.assign((cells_at + static_cast<size_t>(band_count) * static_cast<size_t>(samples) * static_cast<size_t>(stride)) * 4, 0.0f);
			const auto put = [&](size_t vec4_at, int part, double v) { block[vec4_at * 4 + static_cast<size_t>(part)] = static_cast<float>(v); };
			put(0, 0, coarse);
			put(0, 1, band_count);
			put(0, 2, samples);
			put(0, 3, slot_count);
			put(1, 0, first);
			for (int k = 0; k < band_count; ++k) {
				put(static_cast<size_t>(2 + k), 0, bands[static_cast<size_t>(k)]);
				put(static_cast<size_t>(2 + k), 1, bands[static_cast<size_t>(k) + 1]);
			}
			for (int j = 0; j < slot_count; ++j) {
				put(static_cast<size_t>(2 + band_count + j), 0, stack.body[j]);
				put(static_cast<size_t>(2 + band_count + j), 1, stack.bends[j] ? 1.0 : 0.0);
			}
			std::map<double, int> ids; // routes as small whole numbers, in the order met
			// each band's rays followed on a thread of its own (no band's depend on another's), then written in order
			std::vector<std::vector<shader::Ray>> band_rays(static_cast<size_t>(band_count));
			std::vector<std::vector<double>> band_along(static_cast<size_t>(band_count));
			in_pieces(band_count, [&](int k) {
				const shader::Use use(scene);
				const double y = 0.5 * (bands[static_cast<size_t>(k)] + bands[static_cast<size_t>(k) + 1]);
				std::vector<shader::Ray> &rays = band_rays[static_cast<size_t>(k)];
				std::vector<double> &alongs = band_along[static_cast<size_t>(k)];
				rays.resize(static_cast<size_t>(samples) * 4);
				alongs.assign(static_cast<size_t>(samples) * 4, 0.0);
				for (int i = 0; i < samples; ++i) {
					for (int n = 0; n < 4; ++n) {
						rays[static_cast<size_t>(i) * 4 + static_cast<size_t>(n)] = shader::chain_at(stack, y, first + i * 0.25, trace::map_nm(n / 3.0));
					}
				}
				for (int i = 0; i + 1 < samples; ++i) {
					for (int n = 0; n < 4; ++n) {
						const shader::Ray &r = rays[static_cast<size_t>(i) * 4 + static_cast<size_t>(n)];
						const shader::Ray &next = rays[static_cast<size_t>(i + 1) * 4 + static_cast<size_t>(n)];
						if (next.route == r.route) {
							continue;
						}
						// where the route changes on the way to the next sample: by the map of the first body the two
						// rays differ in, from how far from its axis each passes
						double along = 0.5;
						for (int j = 0; j < slot_count; ++j) {
							if (r.bent[j] != next.bent[j] || r.way[j] != next.way[j]) {
								const double lo = r.from_axis[j], hi = next.from_axis[j];
								if (std::fabs(hi - lo) > 1e-9) {
									const int core = stack.media[j] > 1 ? std::max(shader::core_of(stack, j, y), 0) : 0;
									const double change = shader::map_change(shader::ref_at(stack, j, y), core, std::min(lo, hi), std::max(lo, hi), trace::map_nm(n / 3.0));
									along = std::min(std::max((change - lo) / (hi - lo), 0.0), 1.0);
								}
								break;
							}
						}
						alongs[static_cast<size_t>(i) * 4 + static_cast<size_t>(n)] = along;
					}
				}
			});
			for (int k = 0; k < band_count; ++k) {
				followed += static_cast<int64_t>(samples) * 4;
				for (int i = 0; i < samples; ++i) {
					const size_t cell = cells_at + static_cast<size_t>(k * samples + i) * static_cast<size_t>(stride);
					for (int n = 0; n < 4; ++n) {
						const shader::Ray &r = band_rays[static_cast<size_t>(k)][static_cast<size_t>(i) * 4 + static_cast<size_t>(n)];
						const double along = band_along[static_cast<size_t>(k)][static_cast<size_t>(i) * 4 + static_cast<size_t>(n)];
						const int id = ids.emplace(r.route, static_cast<int>(ids.size())).first->second;
						put(cell, n, r.x);
						put(cell + 1, n, id + 0.5 * along);
						for (int j = 0; j < slot_count; ++j) {
							const size_t at = cell + 2 + static_cast<size_t>(4 * j);
							put(at, n, r.bent[j] ? 2.0 : r.seen[j] ? 1.0 : 0.0);
							put(at + 1, n, r.cross[j]);
							put(at + 2, n, r.pass[j]);
							put(at + 3, n, r.chord[j] * r.chord[j]);
						}
					}
				}
			}
		}
		if (followed > 0) {
			scene.stack_rays = followed;
		}
	}
	for (int b = 0; b < L::MAX_BODIES; ++b) {
		if (!has[static_cast<size_t>(b)]) {
			scene.stack_keys[static_cast<size_t>(b)] = 0;
			scene.stack_blocks[static_cast<size_t>(b)].clear();
			continue;
		}
		out[static_cast<size_t>(b) * 4] = static_cast<float>(out.size() / 4);
		out.insert(out.end(), scene.stack_blocks[static_cast<size_t>(b)].begin(), scene.stack_blocks[static_cast<size_t>(b)].end());
	}
	if (out != scene.stacks) {
		scene.stacks = std::move(out);
		++scene.stacks_revision;
	}
}

} // namespace lab
