// The reference renderer: the per-pixel source compiled as C++, in float64 (GAME_SPEC §12, §12.2).
#include "lab/optics_scene.hpp"

#include "lab/colour.hpp"

#include <algorithm>
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

#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wkeyword-macro"
#endif
#define float double
#include "../shader/optics_pixel.glsl"
#undef float
#if defined(__clang__)
#pragma clang diagnostic pop
#endif

static_assert(HEADER_VECS == optics_layout::HEADER_VECS && BODY_VECS == optics_layout::BODY_VECS && ROW == optics_layout::ROW && ROWS_PER_BODY == optics_layout::ROWS_PER_BODY &&
				LIGHT_ENTRIES == optics_layout::LIGHT_ENTRIES,
		"the per-pixel source and lab/optics_scene.hpp lay the scene out alike");

struct Use {
	explicit Use(const OpticsScene &s) { current = &s; }
	~Use() { current = nullptr; }
	Use(const Use &) = delete;
	Use &operator=(const Use &) = delete;
};

bool ready(const OpticsScene &s) {
	return s.table.size() == static_cast<size_t>(optics_layout::SIZE_VECS) * 4 && s.rows.size() == static_cast<size_t>(optics_layout::ROWS_SIZE) &&
		   s.lines.size() == static_cast<size_t>(optics_layout::MAX_LINES) * 4 && s.lights.size() == static_cast<size_t>(optics_layout::LIGHTS * optics_layout::LIGHT_ENTRIES) * 4;
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
	return shader::through(shader::at(x, y), nm, -1.0);
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
	const shader::Bend b = shader::bend_at(shader::at(x, y), nm);
	return OpticsBend{ b.x, b.pass, b.chord };
}

// ---- the truth of a pixel's area (GAME_SPEC §12.3, Stage 20): its own tracer, not the renderer's

namespace {

// Where a level ray meets the wall through one body, traced exactly (vector Snell, Fresnel at every
// crossing, turned back where it cannot pass), seen from above: x across, z away from the eye, in cm
// from the body's axis. Independent of the renderer's formulas: it asks only which medium a point is in.
struct Exact {
	double x = 0.0, pass = 1.0, chord = 0.0;
	int meetings = 0;
};

Exact exact_bend(const shader::Path &q, double nm) {
	Exact out;
	out.x = q.x;
	if (q.refractor < 0) {
		return out;
	}
	using shader::row_at;
	using shader::scene_vec;
	const int at = optics_layout::HEADER_VECS + q.refractor * optics_layout::BODY_VECS;
	const int start = q.refractor * optics_layout::ROWS_PER_BODY * optics_layout::ROW;
	const double n_glass = row_at(start + 4 * optics_layout::ROW, nm);
	const double n_in = q.rliquid ? row_at(start + 3 * optics_layout::ROW, nm) : 1.0;
	const double ri = q.rr - q.rt;
	// the prism's corners (cm from its centre)
	const shader::vec4 m = scene_vec(at + 2), g = scene_vec(at + 3);
	const double cx[3] = { m.x, m.z, g.x }, cz[3] = { m.y, m.w, g.y };
	const auto inside_prism = [&](double x, double z) {
		bool in = true;
		double sign = 0.0;
		for (int e = 0; e < 3; ++e) {
			const int f = (e + 1) % 3;
			const double c = (cx[f] - cx[e]) * (z - cz[e]) - (cz[f] - cz[e]) * (x - cx[e]);
			if (sign == 0.0) {
				sign = c;
			} else if (c * sign < 0.0) {
				in = false;
			}
		}
		return in;
	};
	// 0 air, 1 glass, 2 the core (liquid or air inside the glass)
	const auto medium = [&](double x, double z) {
		if (q.rprism) {
			return inside_prism(x, z) ? 1 : 0;
		}
		const double r = std::sqrt(x * x + z * z);
		return r < ri && q.rt > 0.0 ? 2 : r < q.rr ? (q.rt > 0.0 ? 1 : 2) : 0;
	};
	const auto index = [&](int k) { return k == 0 ? 1.0 : k == 1 ? n_glass : n_in; };
	// the nearest boundary ahead: circles of radius rr and ri, or the prism's three faces
	const auto next = [&](double ox, double oz, double ux, double uz, double &t, double &nx, double &nz) {
		t = 1e30;
		if (q.rprism) {
			for (int e = 0; e < 3; ++e) {
				const int f = (e + 1) % 3;
				const double dx = cx[f] - cx[e], dz = cz[f] - cz[e];
				const double denom = ux * dz - uz * dx;
				if (std::fabs(denom) < 1e-15) {
					continue;
				}
				const double wx = cx[e] - ox, wz = cz[e] - oz;
				const double tt = (wx * dz - wz * dx) / denom, ss = (wx * uz - wz * ux) / denom;
				if (tt > 1e-9 && ss >= 0.0 && ss <= 1.0 && tt < t) {
					t = tt;
					const double len = std::sqrt(dx * dx + dz * dz);
					nx = dz / len;
					nz = -dx / len;
				}
			}
		} else {
			for (int k = 0; k < 2; ++k) {
				const double r = k == 0 ? q.rr : ri; // the outer surface, then the inner, if there is glass
				if (r <= 0.0 || (k == 1 && q.rt <= 0.0)) {
					continue;
				}
				const double b = ox * ux + oz * uz, c = ox * ox + oz * oz - r * r, disc = b * b - c;
				if (disc < 0.0) {
					continue;
				}
				for (const double tt : { -b - std::sqrt(disc), -b + std::sqrt(disc) }) {
					if (tt > 1e-9 && tt < t) {
						t = tt;
						nx = (ox + tt * ux) / r;
						nz = (oz + tt * uz) / r;
					}
				}
			}
		}
		return t < 1e29;
	};
	double ox = q.rd, oz = -1000.0, ux = 0.0, uz = 1.0;
	int here = 0;
	for (int k = 0; k < 16; ++k) {
		double t = 0.0, nx = 0.0, nz = 0.0;
		if (!next(ox, oz, ux, uz, t, nx, nz)) {
			break;
		}
		if (here == 2 && q.rliquid) {
			out.chord += t;
		}
		ox += t * ux;
		oz += t * uz;
		++out.meetings;
		const int there = medium(ox + 1e-7 * ux, oz + 1e-7 * uz);
		if (there == here) {
			continue; // grazing: the same medium on both sides
		}
		if (nx * ux + nz * uz > 0.0) { // the normal against the ray
			nx = -nx;
			nz = -nz;
		}
		const double n1 = index(here), n2 = index(there);
		const double cos_i = -(nx * ux + nz * uz), eta = n1 / n2, sin2 = eta * eta * (1.0 - cos_i * cos_i);
		if (sin2 >= 1.0) { // turned back: all of it
			ux += 2.0 * cos_i * nx;
			uz += 2.0 * cos_i * nz;
			continue;
		}
		const double cos_t = std::sqrt(1.0 - sin2);
		const double rs = (n1 * cos_i - n2 * cos_t) / (n1 * cos_i + n2 * cos_t), rp = (n1 * cos_t - n2 * cos_i) / (n1 * cos_t + n2 * cos_i);
		out.pass *= 1.0 - 0.5 * (rs * rs + rp * rp);
		ux = eta * ux + (eta * cos_i - cos_t) * nx;
		uz = eta * uz + (eta * cos_i - cos_t) * nz;
		here = there;
	}
	if (here != 0) {
		out.pass = 0.0; // still inside: nothing of the wall comes this way
	}
	const double wall = scene_vec(2).x;
	const double landed = uz > 0.05 ? ox + ux * (wall - oz) / uz : (ux < 0.0 ? -100000.0 : 100000.0);
	out.x = q.raxis + landed / q.rcm;
	out.pass = 1.0 - q.rcover * (1.0 - out.pass);
	return out;
}

// what one ray of the room's light brings at nm: the renderer's layers, the truth's own bend
double exact_through(const shader::Path &q, double nm) {
	double seen = q.back;
	const Exact b = exact_bend(q, nm);
	if (q.refractor >= 0) {
		seen = shader::backdrop(b.x, q.y, q.refractor);
	}
	const shader::Layer *layers[4] = { &q.l0, &q.l1, &q.l2, &q.l3 };
	for (int k = 0; k < q.layers && k < 4; ++k) {
		const shader::Layer &l = *layers[k];
		seen = shader::through_layer(l, seen, nm, l.body == q.refractor ? b.chord * l.wet : l.depth_cm);
	}
	if (q.refractor >= 0) {
		seen = b.pass * seen + (1.0 - b.pass) * shader::scene_vec(2).y;
	}
	return seen;
}

} // namespace

double optics_area_through(const OpticsScene &scene, int x, int y, double nm, int grid) {
	if (!shader::ready(scene) || grid < 1) {
		return 0.0;
	}
	const shader::Use use(scene);
	double sum = 0.0;
	for (int j = 0; j < grid; ++j) {
		for (int i = 0; i < grid; ++i) {
			const shader::Path q = shader::path_of(shader::vec2(x + (i + 0.5) / grid, y + (j + 0.5) / grid), 1.0 / grid);
			sum += exact_through(q, nm);
		}
	}
	return sum / (grid * grid);
}

double optics_exact_bend(const OpticsScene &scene, int x, int y, double nm) {
	if (!shader::ready(scene)) {
		return 0.0;
	}
	const shader::Use use(scene);
	return exact_bend(shader::at(x, y), nm).x;
}

void optics_area_truth(const OpticsScene &scene, int x, int y, int grid, double rgb[3]) {
	rgb[0] = rgb[1] = rgb[2] = 0.0;
	if (!shader::ready(scene) || grid < 1) {
		return;
	}
	const shader::Use use(scene);
	std::vector<shader::Path> paths;
	shader::vec3 glow;
	for (int j = 0; j < grid; ++j) {
		for (int i = 0; i < grid; ++i) {
			paths.push_back(shader::path_of(shader::vec2(x + (i + 0.5) / grid, y + (j + 0.5) / grid), 1.0 / grid));
			glow = glow + paths.back().glow / static_cast<double>(grid * grid);
		}
	}
	const int shares = truth_shares(scene);
	shader::vec3 sum;
	for (int j = 0; j < shares; ++j) {
		const shader::vec4 a = shader::share(static_cast<double>(j) / shares), b = shader::share(static_cast<double>(j + 1) / shares);
		const double nm = shader::share((j + 0.5) / shares).x + 380.0;
		double seen = 0.0;
		for (const shader::Path &q : paths) {
			seen += exact_through(q, nm);
		}
		sum = sum + shader::vec3(b.y - a.y, b.z - a.z, b.w - a.w) * (seen / static_cast<double>(paths.size()));
	}
	const shader::vec3 c = shader::to_linear_srgb(sum) + glow;
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
	for (int b = 0; b < L::MAX_BODIES; ++b) {
		even.table[static_cast<size_t>(L::HEADER_VECS + b * L::BODY_VECS + L::REFRACT) * 4 + 2] = 0.0f; // nothing drawn behind it
		for (const int row : { L::LIQUID_ROW, L::WALL_ROW }) {
			for (int i = 0; i < L::ROW; ++i) {
				even.rows[static_cast<size_t>((b * L::ROWS_PER_BODY + row) * L::ROW + i)] = 0.0f; // nothing absorbs
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
	double shown = 0.0;
	for (int x = x0; x < x1; ++x) {
		shown += read(with_line, x, row, nm) - read(without, x, row, nm);
	}
	// the truth: 1024 rays a pixel across, at the row's middle, each traced exactly (a line's image can be a
	// tenth of a pixel wide: fewer rays would count it by whole steps)
	constexpr int RAYS = 1024;
	double truth = 0.0;
	for (const OpticsScene *scene : { &with_line, &without }) {
		const shader::Use use(*scene);
		double sum = 0.0;
		for (int x = x0; x < x1; ++x) {
			for (int i = 0; i < RAYS; ++i) {
				sum += exact_through(shader::path_of(shader::vec2(x + (i + 0.5) / RAYS, row + 0.5), 1.0 / RAYS), nm);
			}
		}
		truth += (scene == &with_line ? 1.0 : -1.0) * sum / RAYS;
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
	const shader::vec3 c = shader::truth(q, truth_shares(scene)) + q.glow;
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
	rgba[3] = optics_cover(scene, x, y) > 0.0 ? 1.0 : 0.0;
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
	const shader::Use use(scene);
	const double scale = shader::budget_scale(asked_, budget_, static_cast<double>(w_) * h_);
	double asked = 0.0, points = 0.0;
	for (int row = 0; row < h_; ++row) {
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
	const shader::Path q = shader::at(x, y);
	rgb[0] = st.prox.x + st.hist.x + q.glow.x;
	rgb[1] = st.prox.y + st.hist.y + q.glow.y;
	rgb[2] = st.prox.z + st.hist.z + q.glow.z;
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

// O4 over rows [from, to) of the picture
OpticsParity compare_rows(const OpticsScene &scene, const uint8_t *picture, int w, int from, int to) {
	OpticsParity out;
	for (int row = from; row < to; ++row) {
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
	// the truth costs a pixel thousands of rays: the rows are shared among the machine's threads
	const unsigned threads = std::max(1u, std::min(16u, std::thread::hardware_concurrency()));
	std::vector<OpticsParity> parts(threads);
	std::vector<std::thread> workers;
	for (unsigned t = 0; t < threads; ++t) {
		const int from = static_cast<int>(static_cast<long long>(h) * t / threads), to = static_cast<int>(static_cast<long long>(h) * (t + 1) / threads);
		workers.emplace_back([&, t, from, to] { parts[t] = compare_rows(scene, rgba, w, from, to); });
	}
	OpticsParity out;
	for (unsigned t = 0; t < threads; ++t) {
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
	for (int i = 0; i < count; ++i) {
		for (int k = 0; k < ROW; ++k) {
			const size_t base = static_cast<size_t>(i * ROWS_PER_BODY * ROW + k);
			const double liquid = scene.rows[base + LIQUID_ROW * ROW], solid = scene.rows[base + SOLID_ROW * ROW], wall = scene.rows[base + WALL_ROW * ROW];
			if (!(liquid >= 0.0) || !(wall >= 0.0) || !(solid >= 0.0 && solid <= 1.0)) {
				std::snprintf(text, sizeof text, "O1: body %d at %d nm: a transmittance outside [0, 1] (depth %g, wall %g, reflectance %g)", i, 380 + k, liquid, wall, solid);
				return text;
			}
		}
		const size_t body = static_cast<size_t>(HEADER_VECS + i * BODY_VECS) * 4;
		const bool prism = scene.table[body + static_cast<size_t>(FORM) * 4] > 1.5f; // its MORE holds its corners
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
	for (int k = 0; k < optics_layout::MAX_SHAPES; ++k) {
		most = std::max(most, static_cast<double>(scene.table[static_cast<size_t>(optics_layout::SHAPE_BASE + 2 * k + 1) * 4]));
	}
	for (int k = 0; k <= 800; ++k) {
		const double nm = 380.0 + 0.5 * k, t = optics_through(scene, x, y, nm);
		if (!(t >= 0.0 && t <= most * (1.0 + 1e-12))) {
			char text[160];
			std::snprintf(text, sizeof text, "O2: at (%d, %d), %g nm carries %g: more than came in", x, y, nm, t);
			return text;
		}
	}
	return "";
}

} // namespace lab
