// The reference renderer: the per-pixel source compiled as C++, in float64 (GAME_SPEC §12, §12.2).
#include "lab/optics_scene.hpp"

#include "lab/colour.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>

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

// the scene being drawn
const OpticsScene *current = nullptr;
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
	return path_of(vec2(x + 0.5, y + 0.5));
}

} // namespace shader

double optics_through(const OpticsScene &scene, int x, int y, double nm) {
	if (!shader::ready(scene)) {
		return 0.0;
	}
	const shader::Use use(scene);
	return shader::through(shader::at(x, y), nm);
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

bool OpticsFrames::restarted(int x, int y) const {
	const int col = x - x_, row = y - y_;
	return col >= 0 && row >= 0 && col < w_ && row < h_ && restarted_[static_cast<size_t>(row) * static_cast<size_t>(w_) + static_cast<size_t>(col)] != 0;
}

// ---- the GPU's picture against the truth (O4)

OpticsParity optics_compare(const OpticsScene &scene, const uint8_t *rgba, int w, int h) {
	OpticsParity out;
	if (!shader::ready(scene)) {
		return out;
	}
	for (int row = 0; row < h; ++row) {
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
		const double extinction = scene.table[body + static_cast<size_t>(MORE) * 4], thickness = scene.table[body + static_cast<size_t>(MORE) * 4 + 1];
		if (!(extinction >= 0.0) || !(thickness >= 0.0)) {
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
