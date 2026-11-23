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
#include <cstring>
#include <array>
#include <atomic>
#include <thread>
#include <unordered_map>
#include <vector>

namespace lab {

namespace shader {

// what the shading language has and C++ has not, in float64
using uint = std::uint32_t;
struct vec2 {
	double x = 0.0, y = 0.0;
	vec2() = default;
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
	if (static_cast<size_t>(i) * 4 + 3 >= current->maps.size()) {
		// beyond what the maps hold, nothing: as the GPU's buffer is, cleared past its data (where a pixel's rays
		// are traced, Stage 29, no map is made, and a pixel's path reads none of what it looks for there)
		return vec4(0.0);
	}
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

// Stage 29, spike A: a band read as a beam of rays traced live, beside the maps' readings (LAB_BEAM: 0 the
// maps, 1 a point, 2 two corners across, 3 four corners). The rays are the exact tracer's here; the reading
// is what is being measured.
struct Path;
double ray_band(Path q, double u0, double u1, double t, double one_nm);
int &beam_mode_set() {
	static int mode = [] {
		const char *asked = std::getenv("LAB_BEAM");
		return asked != nullptr ? std::atoi(asked) : 0;
	}();
	return mode;
}
int beam_mode() {
	return beam_mode_set();
}
bool rays_on() {
	return beam_mode() > 0;
}
// 4: a pixel's own rays, at four wavelengths, traced by the shared source's own tracer (as the GPU does)
bool rays_nodes() {
	return beam_mode() == 4 || beam_mode() == 5;
}
// 5: a pixel as the beam's exact reading and the average of its rays' corrections (an average over time)
bool rays_mean() {
	return beam_mode() == 5;
}
inline vec4 shape_vec(int i) {
	const double *v = &current->shapes_exact[static_cast<size_t>(i) * 4];
	return vec4(v[0], v[1], v[2], v[3]);
}
inline vec3 operator*(double s, const vec3 &a) {
	return vec3(a.x * s, a.y * s, a.z * s);
}
struct OracleRay {
	double x = 0.0, y = 0.0; // where it lands on the wall
	double a = 1.0, b = 0.0; // what the bodies on its way do to the wall's light at its wavelength: a times it plus b
	uint64_t route = 0;
};
struct BeamKey {
	double x, y, nm;
	bool operator==(const BeamKey &o) const { return x == o.x && y == o.y && nm == o.nm; }
};
struct BeamHash {
	size_t operator()(const BeamKey &k) const {
		uint64_t h = 1469598103934665603ull;
		for (const double v : { k.x, k.y, k.nm }) {
			uint64_t bits;
			std::memcpy(&bits, &v, sizeof bits);
			h = (h ^ bits) * 1099511628211ull;
			h ^= h >> 29;
		}
		return static_cast<size_t>(h);
	}
};
thread_local std::unordered_map<BeamKey, OracleRay, BeamHash> beam_rays;
std::atomic<int64_t> beam_traced{ 0 }, beam_bands{ 0 };
struct BeamReport {
	~BeamReport() {
		if (beam_bands.load() > 0 && std::getenv("LAB_BEAM_COUNT") != nullptr) {
			std::printf("beams: %lld bands, %lld rays traced, %.2f a band\n", static_cast<long long>(beam_bands.load()), static_cast<long long>(beam_traced.load()),
					static_cast<double>(beam_traced.load()) / static_cast<double>(beam_bands.load()));
		}
	}
} beam_report;

#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wkeyword-macro"
#endif
inline int beam_setting(int which) {
	static const int deepest = [] {
		const char *asked = std::getenv("LAB_BEAM_DEEPEST");
		return asked != nullptr ? std::atoi(asked) : 2;
	}();
	static const int cut_at = [] {
		const char *asked = std::getenv("LAB_BEAM_CUT_AT");
		return asked != nullptr ? std::atoi(asked) : 0;
	}();
	static const int most = [] {
		const char *asked = std::getenv("LAB_BEAM_MOST");
		return asked != nullptr ? std::atoi(asked) : 100000;
	}();
	return which == 0 ? deepest : which == 1 ? cut_at : most;
}
inline double beam_straight_setting() {
	static const double v = [] {
		const char *asked = std::getenv("LAB_BEAM_STRAIGHT");
		return asked != nullptr ? std::atof(asked) : 0.05;
	}();
	return v;
}
// how many rays the pass's tracer followed on this thread (a count for the reference alone)
thread_local int64_t rays_followed = 0;
#define OPTICS_RAY_HOOK() (++rays_followed)
// what the tracer did on this thread (Stage 33; lab/optics_scene.hpp: optics_work)
thread_local int64_t work_done[8] = {};
#define OPTICS_WORK_HOOK(k) (++work_done[k])
// how passages through a shape ended (Stage 33; optics_ended): by their meetings, how many, how many of them
// still inside, and the light the others carried out
thread_local int64_t ended_count[65] = {}, ended_inside[65] = {};
thread_local double ended_light[65] = {};
inline void ended_hook(int meetings, double pass, bool inside) {
	const int m = meetings < 64 ? meetings : 64;
	++ended_count[m];
	if (inside) {
		++ended_inside[m];
	} else {
		ended_light[m] += pass;
	}
}
#define OPTICS_ENDED_HOOK(m, pass, inside) ended_hook(m, pass, inside)
// (a measurement: how many beams are read over a wall that is one under them, and how many not, by why)
thread_local int64_t even_count[4] = {};
thread_local int64_t swept_count = 0, steps_count[17] = {}, straight_count[4] = {};
#define OPTICS_SWEPT_HOOK() (++swept_count)
#define OPTICS_STEPS_HOOK(n, steps) (++steps_count[steps])
struct Beam;
void even_hook(const Beam &b, double even);
#define OPTICS_EVEN_HOOK(b, even) even_hook(b, even)
// the meetings a passage is allowed (optics_meetings_most)
int TRACE_MOST = trace::MEETINGS_MOST, TRACE_TURNS = trace::TURNS_MOST;
#define TRACE_MOST_SET 1
// what the whole pixel's beam was like, by kind: 0 read whole; 1 to 4 its rays go one way and its middle ray is
// off by up to 0.02, 0.08, 0.32, more; 5 its rays part
thread_local int64_t beam_kinds[6] = {};
thread_local int64_t side_kinds[6] = {};
inline void beam_stat(int level, bool one, double off) {
	if (level == 0) {
		++beam_kinds[!one ? 5 : off <= 0.005 ? 0 : off <= 0.02 ? 1 : off <= 0.08 ? 2 : off <= 0.32 ? 3 : 4];
	}
	if (level == 3) {
		++side_kinds[!one ? 5 : off <= 0.05 ? 0 : off <= 0.1 ? 1 : off <= 0.2 ? 2 : off <= 0.5 ? 3 : 4];
	}
}
#define OPTICS_STAT_HOOK(level, one, off) beam_stat(level, one, off)
#define OPTICS_THREAD thread_local
// a pixel's kept beams (Stage 33): the record of the pixel being read, for the shared source's kept_vec and kept_put
thread_local double *kept_now = nullptr;
inline vec4 kept_vec(int i) {
	const double *v = &kept_now[static_cast<size_t>(i) * 4];
	return vec4(v[0], v[1], v[2], v[3]);
}
inline void kept_put(int i, const vec4 &v) {
	double *to = &kept_now[static_cast<size_t>(i) * 4];
	to[0] = v.x;
	to[1] = v.y;
	to[2] = v.z;
	to[3] = v.w;
}
#define OPTICS_KEPT 1
// the lattice of pixel corners (Stage 33): each corner's record, by its row and its place across; a corner
// never traced reads as nothing, as the GPU's cleared buffer does
using Lattice = std::vector<std::unordered_map<int, std::array<double, 16>>>;
thread_local Lattice *lattice_now = nullptr;
inline vec4 corner_vec(int i) {
	const int corner = i / 4, k = i % 4;
	const auto &row = (*lattice_now)[static_cast<size_t>(corner / 1921)];
	const auto found = row.find(corner % 1921);
	if (found == row.end()) {
		return vec4(0.0);
	}
	const double *v = &found->second[static_cast<size_t>(k) * 4];
	return vec4(v[0], v[1], v[2], v[3]);
}
inline void corner_put(int i, const vec4 &v) {
	const int corner = i / 4, k = i % 4;
	double *to = &(*lattice_now)[static_cast<size_t>(corner / 1921)][corner % 1921][static_cast<size_t>(k) * 4];
	to[0] = v.x;
	to[1] = v.y;
	to[2] = v.z;
	to[3] = v.w;
}
#define OPTICS_LATTICE 1
#define float double
#define OPTICS_RAYS 1
#define OPTICS_RAYS_ORACLE 1
#define OPTICS_TRACE 1
#define OPTICS_NODES 1
// (how small a part is made before it is cut, by the environment while it is being settled: LAB_BEAM_DEEPEST,
// LAB_BEAM_CUT_AT, LAB_BEAM_STRAIGHT)
#define BEAM_DEEPEST beam_setting(0)
#define BEAM_CUT_AT beam_setting(1)
#define BEAM_STRAIGHT beam_straight_setting()
#define BEAM_RAYS_MOST beam_setting(2)
// (for measuring what a band's correction in every frame would look like: LAB_BEAM_STRIDE=1)
inline int correction_stride() {
	static const int v = [] {
		const char *asked = std::getenv("LAB_BEAM_STRIDE");
		return asked != nullptr && std::atoi(asked) > 0 ? std::atoi(asked) : 4;
	}();
	return v;
}
#define CORRECTION_STRIDE correction_stride()
// (a measurement's switch: a pixel whose content has just changed takes its corrections at once, as before Stage 33)
inline bool corrections_at_once() {
	static const bool v = std::getenv("LAB_BEAM_AT_ONCE") != nullptr;
	return v;
}
#define CORRECTIONS_AT_ONCE corrections_at_once()
// (whether a stretch of the spectrum is halved where its nodes miss the ray between them: off for O10's own test)
thread_local double beam_between = 0.02;
#define BEAM_BETWEEN beam_between
// the stretches of the spectrum the last pixel read was read in (for O10)
thread_local std::vector<double> beam_stretches;
#define OPTICS_STRETCH_HOOK(lo, hi) (beam_stretches.push_back(lo), beam_stretches.push_back(hi))
#define TRACE_NEAR 1.0e-9
#define TRACE_NUDGE 1.0e-7
#define TRACE_SLACK 1.0e-9
#define TRACE_FINE 1.0e-13
#define TRACE_ROUGH 1.0e-9
#define OPTICS_CHAIN 1 // the reference reads every pixel: a prism's and a stack's too (the host compiles those apart)
#define OPTICS_FOLLOW 1 // and follows the rays of a stack, which runs on the CPU alone
#include "../shader/optics_pixel.glsl"
#undef float
#if defined(__clang__)
#pragma clang diagnostic pop
#endif

inline void even_hook(const Beam &b, double even) {
	++even_count[even >= 0.0 ? 0 : (b.form == 2 ? 1 : 2)];
	if (even < 0.0 && b.form != 2) {
		++straight_count[b.straight ? 0 : (b.off <= 0.1 ? 1 : (b.off <= 0.3 ? 2 : 3))];
	}
}
static_assert(HEADER_VECS == optics_layout::HEADER_VECS && BODY_VECS == optics_layout::BODY_VECS && ROW == optics_layout::ROW && ROWS_PER_SET == optics_layout::ROWS_PER_SET &&
				LIGHT_ENTRIES == optics_layout::LIGHT_ENTRIES,
		"the per-pixel source and lab/optics_scene.hpp lay the scene out alike");
static_assert(1 + KEPT_MOST * KEPT_BEAM == optics_layout::KEPT_VECS, "a pixel's kept beams are a record of KEPT_VECS");

struct Use {
	explicit Use(const OpticsScene &s) {
		current = &s;
		beam_rays.clear();
	}
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
	if (shader::rays_nodes() && shader::beamed(q)) {
		// its own ray, traced (Stage 29)
		const shader::RayMet m = shader::ray_met(shader::vec2(q.x, q.y), nm);
		OpticsBend traced{ m.x, 1.0, 0.0 };
		bool first = true;
		for (int k = 0; k < m.count; ++k) {
			const shader::vec4 slot = shader::met_slot(m, k);
			if (slot.y > 0.5) {
				traced.pass *= slot.z;
				traced.chord = first ? slot.w : traced.chord;
				first = false;
			}
		}
		return traced;
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
	// (what lies in it is traced where the pixels are read by their own rays; where they are read by the maps,
	// its liquid's surface is a row of the screen, as the maps have it, until the maps go)
	const bool turned = block != nullptr && block[3] != 0.0, lies = block != nullptr && block[2] > 0.5 && shader::rays_nodes();
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
	const trace::Landing l = trace::trace(*shape, o, u, 1.0e9, index, lies ? &inside : nullptr);
	out.meetings = l.meetings;
	if (l.meetings == 0) {
		return out;
	}
	out.met = true;
	out.entry = (l.chord[0] - back) / cm;
	out.pass = l.pass;
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
				out.met.push_back(Met{ k, cross, cross_y, true, e.pass, e.chord, e.route + (e.out && line.uz > 0.05 ? 0u : 0x80000000u), core_in(k, cross_y), e.solid });
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
	const double room = shader::scene_vec(2).y;
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
				if (shader::rays_nodes() && shader::lies_in(m.body)) {
					// what lies in it is traced (Stage 30): its sediment is where the ray ended, lit by the room
					// through the liquid there and back
					l.solid = m.solid ? 1.0 : 0.0;
					chord = m.solid ? 2.0 * m.chord : m.chord;
				}
			}
			const int start = shader::set_of(m.body) * optics_layout::ROWS_PER_SET * optics_layout::ROW;
			const double per_cm = shader::liquid_at(m.body, start, nm);
			const shader::vec2 does = shader::body_does(l, shader::row_at(start + optics_layout::ROW, nm), shader::row_at(start + 5 * optics_layout::ROW, nm), shader::row_at(start + 2 * optics_layout::ROW, nm),
					per_cm * (m.bent ? chord : l.depth_cm), m.pass, room);
			b += a * does.y;
			a *= does.x;
		}
		total += (ways > 1 ? shader::way_share(way) : 1.0) * (a * drawn_at(ray.x, ray.y) + b);
	}
	return total;
}

// ---- Stage 29, spike A: a band as a beam of rays

// one ray of the beam: traced exactly through every body, and what they do to the wall's light at its wavelength
shader::OracleRay oracle_ray(double x, double y, double nm) {
	const shader::BeamKey key{ x, y, nm };
	if (const auto found = shader::beam_rays.find(key); found != shader::beam_rays.end()) {
		return found->second;
	}
	shader::beam_traced.fetch_add(1, std::memory_order_relaxed);
	const ExactRay ray = exact_ray(x, y, nm);
	const double room = shader::scene_vec(2).y;
	shader::OracleRay out;
	uint64_t h = 1469598103934665603ull;
	const auto mixed = [&h](uint64_t v) { h = (h ^ v) * 1099511628211ull; };
	double a = 1.0, b = 0.0;
	for (const Met &m : ray.met) {
		shader::Layer l = shader::layer_at(m.body, shader::vec2(m.cross, m.cross_y), 1.0e-3, 1.0e-3);
		if (m.bent) {
			l.haze = shader::haze_along(m.body, m.chord);
		}
		const int start = shader::set_of(m.body) * optics_layout::ROWS_PER_SET * optics_layout::ROW;
		const double per_cm = shader::liquid_at(m.body, start, nm);
		const shader::vec2 does = shader::body_does(l, shader::row_at(start + optics_layout::ROW, nm), shader::row_at(start + 5 * optics_layout::ROW, nm), shader::row_at(start + 2 * optics_layout::ROW, nm),
				per_cm * (m.bent ? m.chord : l.depth_cm), m.pass, room);
		b += a * does.y;
		a *= does.x;
		if (m.bent || l.cover > 0.5) {
			mixed(static_cast<uint64_t>(m.body) * 8 + (m.bent ? 1 : 0) + (l.solid > 0.5 ? 2 : 0) + (l.depth_cm > 0.0 ? 4 : 0));
			mixed(m.route);
			mixed(static_cast<uint64_t>(m.core));
		}
	}
	out.x = ray.x;
	out.y = ray.y;
	out.a = a;
	out.b = b;
	out.route = h;
	if (shader::beam_rays.size() > (1u << 18)) {
		shader::beam_rays.clear();
	}
	shader::beam_rays.emplace(key, out);
	return out;
}

int oracle_profile(double y) {
	return shader::wall_profile(y);
}

// A corner of the beam: its ray at the band's two ends (where the wall is swept) and at its point (what gets through)
struct OracleCorner {
	shader::OracleRay r0, r1, rp;
	bool one() const { return r0.route == r1.route && r0.route == rp.route; }
};
OracleCorner oracle_corner(double x, double y, double nm0, double nm1, double nm) {
	return OracleCorner{ oracle_ray(x, y, nm0), oracle_ray(x, y, nm1), oracle_ray(x, y, nm) };
}
// a corner alone: its own ray's light, the wall along its own sweep
double oracle_alone(const OracleCorner &c) {
	return c.rp.a * shader::wall_span(oracle_profile(c.rp.y), c.r0.x, c.r1.x) + c.rp.b;
}

const double ORACLE_STRAIGHT = 0.005; // pixels: how far a part's middle ray may land from half way between its ends' (the maps' tolerance is 0.02 on a quarter of a pixel)
// whether the ray in the middle goes the way of the two beside it and lands half way between them
bool oracle_straight(const shader::OracleRay &m, const shader::OracleRay &a, const shader::OracleRay &b) {
	return m.route == a.route && std::fabs(m.x - 0.5 * (a.x + b.x)) <= ORACLE_STRAIGHT && std::fabs(m.y - 0.5 * (a.y + b.y)) <= ORACLE_STRAIGHT;
}

const int ORACLE_DEPTH = 2; // a part whose rays go different ways is halved this many times, then cut where they part

// two corners that go one way: the wall over the footprint between them as the band is crossed
double oracle_foot(const OracleCorner &a, const OracleCorner &b) {
	const double wall = shader::swept_wall(oracle_profile(0.5 * (a.rp.y + b.rp.y)), a.r0.x, b.r0.x, a.r1.x, b.r1.x);
	return 0.5 * (a.rp.a + b.rp.a) * wall + 0.5 * (a.rp.b + b.rp.b);
}
bool oracle_together(const OracleCorner &a, const OracleCorner &b) {
	return a.one() && b.one() && a.rp.route == b.rp.route;
}

// Between two places of the pixel whose rays go different ways: where they part is found by halving, and each
// side is its own footprint, from its end's ray to the last ray that goes its way.
double oracle_edge(double ax, double ay, double bx, double by, double nm0, double nm1, double nm) {
	const OracleCorner a = oracle_corner(ax, ay, nm0, nm1, nm), b = oracle_corner(bx, by, nm0, nm1, nm);
	if (oracle_together(a, b)) {
		return oracle_foot(a, b);
	}
	double lo = 0.0, hi = 1.0;
	for (int it = 0; it < 10; ++it) {
		const double mid = 0.5 * (lo + hi);
		if (oracle_ray(shader::mix(ax, bx, mid), shader::mix(ay, by, mid), nm).route == a.rp.route) {
			lo = mid;
		} else {
			hi = mid;
		}
	}
	const OracleCorner a_edge = oracle_corner(shader::mix(ax, bx, lo), shader::mix(ay, by, lo), nm0, nm1, nm), b_edge = oracle_corner(shader::mix(ax, bx, hi), shader::mix(ay, by, hi), nm0, nm1, nm);
	const double share = 0.5 * (lo + hi);
	return share * (oracle_together(a, a_edge) ? oracle_foot(a, a_edge) : oracle_alone(a)) + (1.0 - share) * (oracle_together(b, b_edge) ? oracle_foot(b_edge, b) : oracle_alone(b));
}

// two corners across, at one height: the wall over the footprint between them as the band is crossed
double oracle_across(double x0, double x1, double y, double nm0, double nm1, double nm, int depth) {
	const OracleCorner l = oracle_corner(x0, y, nm0, nm1, nm), r = oracle_corner(x1, y, nm0, nm1, nm);
	const double xm = 0.5 * (x0 + x1);
	const bool together = oracle_together(l, r);
	if (together && (depth >= ORACLE_DEPTH || oracle_straight(oracle_ray(xm, y, nm), l.rp, r.rp))) {
		return oracle_foot(l, r);
	}
	if (depth >= ORACLE_DEPTH) {
		return oracle_edge(x0, y, x1, y, nm0, nm1, nm);
	}
	return 0.5 * (oracle_across(x0, xm, y, nm0, nm1, nm, depth + 1) + oracle_across(xm, x1, y, nm0, nm1, nm, depth + 1));
}

// four corners: the wall over the footprint, row by row of the wall it crosses
double oracle_patch(double x0, double x1, double y0, double y1, double nm0, double nm1, double nm, int depth) {
	const OracleCorner tl = oracle_corner(x0, y0, nm0, nm1, nm), tr = oracle_corner(x1, y0, nm0, nm1, nm), bl = oracle_corner(x0, y1, nm0, nm1, nm), br = oracle_corner(x1, y1, nm0, nm1, nm);
	const double xm = 0.5 * (x0 + x1), ym = 0.5 * (y0 + y1);
	const bool together = oracle_together(tl, tr) && oracle_together(bl, br) && tl.rp.route == bl.rp.route;
	if (together && (depth >= ORACLE_DEPTH || (oracle_straight(oracle_ray(xm, ym, nm), tl.rp, br.rp) && oracle_straight(oracle_ray(xm, ym, nm), tr.rp, bl.rp)))) {
		const double top = 0.5 * (tl.rp.y + tr.rp.y), bottom = 0.5 * (bl.rp.y + br.rp.y);
		const int rows = shader::clamp(static_cast<int>(std::ceil(std::fabs(bottom - top) - 1.0e-9)), 1, 8);
		double wall = 0.0;
		for (int i = 0; i < rows; ++i) {
			const double f = (i + 0.5) / rows;
			wall += shader::swept_wall(oracle_profile(shader::mix(top, bottom, f)), shader::mix(tl.r0.x, bl.r0.x, f), shader::mix(tr.r0.x, br.r0.x, f), shader::mix(tl.r1.x, bl.r1.x, f),
					shader::mix(tr.r1.x, br.r1.x, f));
		}
		return 0.25 * (tl.rp.a + tr.rp.a + bl.rp.a + br.rp.a) * wall / rows + 0.25 * (tl.rp.b + tr.rp.b + bl.rp.b + br.rp.b);
	}
	if (depth >= ORACLE_DEPTH) {
		// where its rays part across, its top and its bottom edges each cut there; where they part up and down, its sides
		if (!oracle_together(tl, tr) || !oracle_together(bl, br)) {
			return 0.5 * (oracle_edge(x0, y0, x1, y0, nm0, nm1, nm) + oracle_edge(x0, y1, x1, y1, nm0, nm1, nm));
		}
		return 0.5 * (oracle_edge(x0, y0, x0, y1, nm0, nm1, nm) + oracle_edge(x1, y0, x1, y1, nm0, nm1, nm));
	}
	return 0.25 * (oracle_patch(x0, xm, y0, ym, nm0, nm1, nm, depth + 1) + oracle_patch(xm, x1, y0, ym, nm0, nm1, nm, depth + 1) + oracle_patch(x0, xm, ym, y1, nm0, nm1, nm, depth + 1) +
						  oracle_patch(xm, x1, ym, y1, nm0, nm1, nm, depth + 1));
}

} // namespace

namespace shader {

// one straight step of a band's wavelengths, read by the mode's beam
double oracle_step(const Path &q, double nm0, double nm1, double nm, double sx, double sy) {
	const double half = 0.5 * q.size;
	switch (beam_mode()) {
		case 1: {
			const OracleRay r = oracle_ray(q.x - half + sx * q.size, q.y - half + sy * q.size, nm);
			return r.a * wall_span(oracle_profile(r.y), r.x, r.x) + r.b;
		}
		case 2:
			return oracle_across(q.x - half, q.x + half, q.y - half + sy * q.size, nm0, nm1, nm, 0);
		default:
			return oracle_patch(q.x - half, q.x + half, q.y - half, q.y + half, nm0, nm1, nm, 0);
	}
}

// A band from nm0 to nm1 in straight steps (as the maps' reading sweeps a band, GAME_SPEC §12.3): where the
// pixel's middle ray, half way through, does not land half way between its ends' (0.02 of a pixel, the maps'
// sweep tolerance) or goes another way, the band is halved, sixteen steps at most; each step weighed by its
// share of the light's energy, its point t of the way through it. x: the light times its weight; y: the weight.
vec2 oracle_steps(const Path &q, double nm0, double nm1, double nm, double t, double sx, double sy, int depth) {
	if (beam_mode() > 1 && depth < 4 && nm1 != nm0) {
		const double mid = 0.5 * (nm0 + nm1);
		const OracleRay a = oracle_ray(q.x, q.y, nm0), b = oracle_ray(q.x, q.y, nm1), m = oracle_ray(q.x, q.y, mid);
		if (m.route != a.route || m.route != b.route || std::fabs(m.x - 0.5 * (a.x + b.x)) > 0.02 || std::fabs(m.y - 0.5 * (a.y + b.y)) > 0.02) {
			const vec2 first = oracle_steps(q, nm0, mid, mix(nm0, mid, t), t, sx, sy, depth + 1), second = oracle_steps(q, mid, nm1, mix(mid, nm1, t), t, sx, sy, depth + 1);
			return vec2(first.x + second.x, first.y + second.y);
		}
	}
	const double weight = depth == 0 ? 1.0 : max(share_of(max(nm0, nm1)) - share_of(min(nm0, nm1)), 0.0);
	return vec2(weight * oracle_step(q, nm0, nm1, nm, sx, sy), weight);
}

double ray_band(Path q, double u0, double u1, double t, double one_nm) {
	beam_bands.fetch_add(1, std::memory_order_relaxed);
	const double nm0 = nm_at(u0, one_nm), nm1 = nm_at(u1, one_nm), nm = nm_at(mix(u0, u1, t), one_nm);
	// where in the pixel a band's rays are taken, where they are not its corners: a place of its own for each
	// band, moving with the frame's point (none for one wavelength alone: the pixel's middle)
	const double sx = one_nm > 0.0 ? 0.5 : fract(u0 * 64.0 * 0.7548776662466927 + t), sy = one_nm > 0.0 ? 0.5 : fract(u0 * 64.0 * 0.5698402909980532 + 0.6180339887498949 * t);
	const vec2 sum = oracle_steps(q, nm0, nm1, nm, t, sx, sy, 0);
	return sum.y > 0.0 ? sum.x / sum.y : 0.0;
}

} // namespace shader

// how many rays the beams traced and how many bands they read, since the last asking (spike A's measure)
void optics_beam_counts(int64_t &rays, int64_t &bands) {
	rays = shader::beam_traced.exchange(0);
	bands = shader::beam_bands.exchange(0);
}

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

int optics_reading(int mode) {
	const int before = shader::beam_mode_set();
	shader::beam_mode_set() = mode;
	return before;
}

bool optics_traced() {
	return shader::rays_nodes();
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
	// (the truth as the reading by a pixel's own rays has it: what lies in a vessel traced)
	struct Reading {
		int before = optics_reading(5);
		~Reading() { optics_reading(before); }
	} reading;
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
	if (std::getenv("LAB_O9_COUNTS") != nullptr) {
		std::printf("O9: %lld rays, %lld another way, %lld loose\n", static_cast<long long>(rays), static_cast<long long>(astray), static_cast<long long>(loose));
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
	int64_t probes = 0, astray = 0, pixels = 0, loose = 0, pieces = 0, stretches_all = 0, shortest = 0;
	double furthest = 0.0;
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
				++pieces;
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
					furthest = std::max(furthest, worst);
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
	if (std::getenv("LAB_O9_COUNTS") != nullptr) {
		std::printf("O10: %lld pixels, %lld stretches (%lld as short as made, of %lld), %lld rays, %lld another way, %lld loose, the furthest %.2f of what is allowed\n", static_cast<long long>(pixels),
				static_cast<long long>(pieces), static_cast<long long>(shortest), static_cast<long long>(stretches_all), static_cast<long long>(probes), static_cast<long long>(astray), static_cast<long long>(loose), furthest);
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
	const shader::Traced t = shader::trace_shape(s, shader::vec3(from[0], from[1], from[2]), shader::vec3(heading[0], heading[1], heading[2]), n_glass, n_core, head.z > 0.5 ? block : -1);
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
	out.most = shader::TRACE_MOST;
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
	int64_t rays = 0, astray = 0, loose = 0, long_single = 0, long_double = 0, last_single = 0, last_double = 0;
	std::array<int64_t, 8> apart{};
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
					long_single += s.meetings > 8 ? 1 : 0;
					long_double += d.meetings > 8 ? 1 : 0;
					last_single += s.meetings >= s.most ? 1 : 0;
					last_double += d.meetings >= d.most ? 1 : 0;
					if (s.route == d.route && s.left == d.left && s.solid == d.solid && d.left) {
						// (how far apart the two leave, in direction: by decades, from 1e-8)
						const double off = std::hypot(s.toward[0] - d.toward[0], s.toward[1] - d.toward[1], s.toward[2] - d.toward[2]);
						++apart[static_cast<size_t>(std::min(std::max(static_cast<int>(std::floor(std::log10(std::max(off, 1.0e-30)))) + 8, 0), 7))];
					}
					if (s.route != d.route || s.left != d.left || s.solid != d.solid) {
						++astray;
					} else if (d.left && (std::hypot(s.at[0] - d.at[0], s.at[1] - d.at[1], s.at[2] - d.at[2]) > CLOSE || std::hypot(s.toward[0] - d.toward[0], s.toward[1] - d.toward[1], s.toward[2] - d.toward[2]) > CLOSE)) {
						++loose;
					}
				}
			}
		}
	}
	if (std::getenv("LAB_O9_COUNTS") != nullptr) {
		std::printf("O15: rays that go the same way leave apart in direction by under 1e-7: %lld, to 1e-6: %lld, to 1e-5: %lld, to 1e-4: %lld, to 1e-3: %lld, to 1e-2: %lld, more: %lld\n", static_cast<long long>(apart[0]),
				static_cast<long long>(apart[1]), static_cast<long long>(apart[2]), static_cast<long long>(apart[3]), static_cast<long long>(apart[4]), static_cast<long long>(apart[5]), static_cast<long long>(apart[6] + apart[7]));
		std::printf("O15: %lld rays, %lld another way in single precision, %lld loose; past eight meetings %lld single and %lld double; to the last allowed %lld and %lld\n", static_cast<long long>(rays),
				static_cast<long long>(astray), static_cast<long long>(loose), static_cast<long long>(long_single), static_cast<long long>(long_double), static_cast<long long>(last_single), static_cast<long long>(last_double));
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

void optics_ended(int64_t count[65], int64_t inside[65], double light[65], bool reset) {
	for (int i = 0; i < 65; ++i) {
		count[i] = shader::ended_count[i];
		inside[i] = shader::ended_inside[i];
		light[i] = shader::ended_light[i];
		if (reset) {
			shader::ended_count[i] = 0;
			shader::ended_inside[i] = 0;
			shader::ended_light[i] = 0.0;
		}
	}
}

void optics_even(int64_t count[3], bool reset) {
	if (std::getenv("LAB_EVEN_PRINT") != nullptr) {
		std::printf("swept walls %lld; beams read by the steps a band of 40 nm takes:", static_cast<long long>(shader::swept_count));
		for (int i = 1; i <= 16; ++i) {
			std::printf(" %lld", static_cast<long long>(shader::steps_count[i]));
			shader::steps_count[i] = 0;
		}
		std::printf("\nof the beams over a wall that is not one, whose rays go one way: straight %lld, off by up to 0.1 of their reach %lld, to 0.3 %lld, more %lld\n", static_cast<long long>(shader::straight_count[0]),
				static_cast<long long>(shader::straight_count[1]), static_cast<long long>(shader::straight_count[2]), static_cast<long long>(shader::straight_count[3]));
		for (int64_t &c : shader::straight_count) {
			c = 0;
		}
		shader::swept_count = 0;
	}
	for (int i = 0; i < 3; ++i) {
		count[i] = shader::even_count[i];
		if (reset) {
			shader::even_count[i] = 0;
		}
	}
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

void optics_meetings_most(int most, int turns) {
	shader::TRACE_MOST = most;
	shader::TRACE_TURNS = turns;
	trace::meetings_most(most, turns);
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

void optics_beam_kinds(int64_t kinds[6], bool reset) {
	if (std::getenv("LAB_RAY_STATS") != nullptr && reset) {
		std::printf("sides: straight %lld; one way but off by up to 0.1: %lld, 0.2: %lld, 0.5: %lld, more: %lld; rays part: %lld\n", static_cast<long long>(shader::side_kinds[0]),
				static_cast<long long>(shader::side_kinds[1]), static_cast<long long>(shader::side_kinds[2]), static_cast<long long>(shader::side_kinds[3]), static_cast<long long>(shader::side_kinds[4]),
				static_cast<long long>(shader::side_kinds[5]));
		for (int64_t &k : shader::side_kinds) {
			k = 0;
		}
	}
	for (int i = 0; i < 6; ++i) {
		kinds[i] = shader::beam_kinds[i];
		if (reset) {
			shader::beam_kinds[i] = 0;
		}
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

struct OpticsFrames::Corners {
	shader::Lattice rows = shader::Lattice(1082);
};

struct OpticsFrames::State {
	shader::PixelState s;
	// its beams as they are kept (the shared source's record: Stage 33); nothing kept at first
	std::array<double, static_cast<size_t>(1 + KEPT_MOST * shader::KEPT_BEAM) * 4> kept{};
};

OpticsFrames::OpticsFrames(int x, int y, int w, int h, double budget, int step) :
		x_(x), y_(y), w_(w > 0 ? w : 0), h_(h > 0 ? h : 0), step_(step > 0 ? step : 1), budget_(budget), states_(new std::vector<State>(static_cast<size_t>(w_) * static_cast<size_t>(h_))),
		restarted_(static_cast<size_t>(w_) * static_cast<size_t>(h_), 0) {
	corners_ = new Corners;
	for (State &st : *states_) {
		st.s.hist = st.s.prox = st.s.aux = shader::vec4(0.0);
		st.s.key = shader::vec4(-1.0); // matches no content: the first frame starts every history
	}
}

OpticsFrames::~OpticsFrames() {
	delete corners_;
	delete states_;
}

void OpticsFrames::frame(const OpticsScene &scene) {
	if (!shader::ready(scene)) {
		return;
	}
	// the screen's budget for the pixels read as the scene says, the rays' for those read by their own rays (of the
	// whole screen's, the sample's share)
	const double scale = shader::budget_scale(asked_ - ray_asked_, budget_, static_cast<double>(w_) * h_);
	const double ray_scale = shader::budget_scale(ray_asked_, std::max(optics_layout::RAY_POINTS / (static_cast<double>(step_) * step_) - shader::RAY_POINTS_A_BEAM * beams_traced_, 0.0), 1.0);
	// a row a piece: a pixel's step touches its own state alone
	std::vector<double> asked_of(static_cast<size_t>(h_), 0.0), points_of(static_cast<size_t>(h_), 0.0), ray_asked_of(static_cast<size_t>(h_), 0.0), traced_of(static_cast<size_t>(h_), 0.0);
	std::vector<int64_t> read_of(static_cast<size_t>(h_), 0), rays_of(static_cast<size_t>(h_), 0), most_of(static_cast<size_t>(h_), 0);
	// The corners first (Stage 33; as the GPU's pass over the lattice before the pixels'): those of every pixel
	// of the sample, a row of corners a piece (a corner's record is its row's)
	std::vector<int> corner_rows;
	if (sharing_ && shader::rays_mean()) {
		for (int row = 0; row < h_; ++row) {
			for (int below = 0; below < 2; ++below) {
				const int y = y_ + row * step_ + below;
				if (corner_rows.empty() || corner_rows.back() < y) {
					corner_rows.push_back(y);
				}
			}
		}
	}
	std::vector<int64_t> corner_rays(corner_rows.size(), 0);
	in_pieces(static_cast<int>(corner_rows.size()), [&](int piece) {
		const shader::Use use(scene);
		shader::lattice_now = &corners_->rows;
		const int64_t before = shader::rays_followed;
		const int y = corner_rows[static_cast<size_t>(piece)];
		int last = -1;
		for (int col = 0; col < w_; ++col) {
			for (int right = 0; right < 2; ++right) {
				const int x = x_ + col * step_ + right;
				if (x > last) {
					shader::corner_step(shader::vec2(x, y));
					last = x;
				}
			}
		}
		corner_rays[static_cast<size_t>(piece)] = shader::rays_followed - before;
		shader::lattice_now = nullptr;
	});
	in_pieces(h_, [&](int row) {
		const shader::Use use(scene);
		double &asked = asked_of[static_cast<size_t>(row)], &points = points_of[static_cast<size_t>(row)];
		for (int col = 0; col < w_; ++col) {
			const size_t i = static_cast<size_t>(row) * static_cast<size_t>(w_) + static_cast<size_t>(col);
			const int x = x_ + col * step_, y = y_ + row * step_;
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
			const int64_t rays_before = shader::rays_followed;
			shader::kept_now = (*states_)[i].kept.data();
			shader::kept_slot = 0;
			shader::kept_read = keeping_;
			shader::lattice_now = &corners_->rows;
			shader::lattice_on = sharing_ && shader::rays_mean();
			const bool by_rays = shader::rays_mean() && shader::beamed(q);
			st = shader::step_pixel(q, st, shader::vec2(x + 0.5, y + 0.5), frame_, by_rays ? ray_scale : scale);
			shader::lattice_on = false;
			shader::kept_slot = -1;
			const int64_t followed = shader::rays_followed - rays_before;
			++read_of[static_cast<size_t>(row)];
			rays_of[static_cast<size_t>(row)] += followed;
			most_of[static_cast<size_t>(row)] = std::max(most_of[static_cast<size_t>(row)], followed);
			// a history that had points and is now one frame old (or none) started again
			restarted_[i] = before > 0.0 && st.aux.y <= 1.0 ? 1 : 0;
			asked += st.aux.z;
			ray_asked_of[static_cast<size_t>(row)] += by_rays ? st.aux.z : 0.0;
			traced_of[static_cast<size_t>(row)] += shader::beam_deferred ? 1.0 : 0.0;
			points += st.aux.w;
		}
	});
	double asked = 0.0, points = 0.0;
	ray_asked_ = 0.0;
	for (const double a : ray_asked_of) {
		ray_asked_ += a;
	}
	beams_traced_ = 0.0;
	for (const double a : traced_of) {
		beams_traced_ += a;
	}
	read_ = rays_ = most_rays_ = 0;
	for (int row = 0; row < h_; ++row) {
		asked += asked_of[static_cast<size_t>(row)];
		points += points_of[static_cast<size_t>(row)];
		read_ += read_of[static_cast<size_t>(row)];
		rays_ += rays_of[static_cast<size_t>(row)];
		most_rays_ = std::max(most_rays_, most_of[static_cast<size_t>(row)]);
	}
	// (a corner is four pixels': in a sample each pixel has corners of its own, each counted as the quarter it is
	// of a pixel's cost where the pixels lie side by side)
	for (const int64_t r : corner_rays) {
		rays_ += step_ > 1 ? r / 4 : r;
	}
	asked_ = asked;
	points_ = points;
	++frame_;
}

void OpticsFrames::colour(const OpticsScene &scene, int x, int y, double rgb[3]) const {
	rgb[0] = rgb[1] = rgb[2] = 0.0;
	const int col = (x - x_) / step_, row = (y - y_) / step_;
	if (!shader::ready(scene) || x < x_ || y < y_ || col >= w_ || row >= h_) {
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
	const int col = (x - x_) / step_, row = (y - y_) / step_;
	if (!shader::ready(scene) || x < x_ || y < y_ || col >= w_ || row >= h_) {
		return;
	}
	const shader::Use use(scene);
	const shader::vec4 c = shader::shown(shader::at(x, y), (*states_)[static_cast<size_t>(row) * static_cast<size_t>(w_) + static_cast<size_t>(col)].s);
	rgba[0] = c.x;
	rgba[1] = c.y;
	rgba[2] = c.z;
	rgba[3] = c.w;
}

bool OpticsFrames::kept_fresh(const OpticsScene &scene, int x, int y, bool &holds) const {
	holds = false;
	const int col = (x - x_) / step_, row = (y - y_) / step_;
	if (!shader::ready(scene) || x < x_ || y < y_ || col >= w_ || row >= h_ || (x - x_) % step_ != 0 || (y - y_) % step_ != 0) {
		return true;
	}
	const shader::Use use(scene);
	const shader::Path q = shader::at(x, y);
	const auto &kept = (*states_)[static_cast<size_t>(row) * static_cast<size_t>(w_) + static_cast<size_t>(col)].kept;
	const double pixel = std::floor(q.x) + 1920.0 * std::floor(q.y);
	if (!(kept[0] == q.shape.x && kept[1] == q.shape.y && kept[2] == pixel && kept[3] > 0.5)) {
		return true; // nothing kept for what it looks through now: its next read traces
	}
	holds = true;
	std::remove_const_t<std::remove_reference_t<decltype(kept)>> fresh{};
	shader::kept_now = fresh.data();
	shader::kept_slot = 0;
	shader::beam_means(q, 0, 0.5, shader::cuts_none(), 0.0, 1.0, false, 0, false); // traced, and kept: nothing read
	shader::kept_slot = -1;
	shader::kept_now = nullptr;
	return fresh == kept;
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
	const int col = (x - x_) / step_, row = (y - y_) / step_;
	if (x < x_ || y < y_ || col >= w_ || row >= h_) {
		return 0.0;
	}
	return (*states_)[static_cast<size_t>(row) * static_cast<size_t>(w_) + static_cast<size_t>(col)].s.aux.w;
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
				std::fprintf(stderr, "O4 (%d, %d): %.1f apart; the picture %.3f %.3f %.3f, the reference's reading %.3f %.3f %.3f, the truth of its area %.3f %.3f %.3f\n", col, row, d, lin[0], lin[1], lin[2], rgb[0], rgb[1], rgb[2], area[0],
						area[1], area[2]);
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
		// (where a pixel's rays are traced in the pass, Stage 29, nothing reads a stack: none is followed)
		const bool traced = shader::rays_nodes();
		for (const int b : scene.chain_bodies) {
			if (traced || b < 0 || b >= count || !bends(b) || !one_piece(b)) {
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
							const double knot = scene.maps[(static_cast<size_t>(map[0]) + 1 + static_cast<size_t>(k)) * 4];
							const double e = vec(o, L::SET)[3] > 0.5f ? stack.foot[j] - knot : stack.top[j] + knot; // upside down: from its foot (Stage 27)
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
