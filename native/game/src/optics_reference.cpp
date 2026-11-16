// The reference renderer: the shader's source compiled as C++ (GAME_SPEC §12).
#include "lab/optics_scene.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace lab {

namespace shader {

// what the shading language has and C++ has not, in float64
struct vec2 {
	double x = 0.0, y = 0.0;
	vec2(double a, double b) :
			x(a), y(b) {}
};
struct vec4 {
	double x = 0.0, y = 0.0, z = 0.0, w = 0.0;
	vec4() = default;
	explicit vec4(double a) :
			x(a), y(a), z(a), w(a) {}
	vec4(double a, double b, double c, double d) :
			x(a), y(b), z(c), w(d) {}
};
inline vec4 operator+(const vec4 &a, const vec4 &b) {
	return vec4(a.x + b.x, a.y + b.y, a.z + b.z, a.w + b.w);
}
inline vec4 operator*(const vec4 &a, const vec4 &b) {
	return vec4(a.x * b.x, a.y * b.y, a.z * b.z, a.w * b.w);
}
inline vec4 operator*(const vec4 &a, double s) {
	return vec4(a.x * s, a.y * s, a.z * s, a.w * s);
}
inline vec4 operator-(const vec4 &a) {
	return vec4(-a.x, -a.y, -a.z, -a.w);
}
inline double min(double a, double b) {
	return a < b ? a : b;
}
inline double max(double a, double b) {
	return a > b ? a : b;
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
inline double pow(double a, double b) {
	return std::pow(a, b);
}
inline double clamp(double v, double lo, double hi) {
	return v < lo ? lo : v > hi ? hi : v;
}
inline vec4 exp(const vec4 &a) {
	return vec4(std::exp(a.x), std::exp(a.y), std::exp(a.z), std::exp(a.w));
}
inline vec4 clamp(const vec4 &v, const vec4 &lo, const vec4 &hi) {
	return vec4(clamp(v.x, lo.x, hi.x), clamp(v.y, lo.y, hi.y), clamp(v.z, lo.z, hi.z), clamp(v.w, lo.w, hi.w));
}
inline vec4 mix(const vec4 &a, const vec4 &b, double t) {
	return a * (1.0 - t) + b * t;
}

#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wkeyword-macro"
#endif
#define uniform static
#define float double
#include "../shader/optics_pixel.gdshaderinc"
#undef float
#undef uniform
#if defined(__clang__)
#pragma clang diagnostic pop
#endif

static_assert(BAND_VECS == optics_layout::BAND_VECS && HEADER_VECS == optics_layout::HEADER_VECS && BODY_VECS == optics_layout::BODY_VECS,
		"the shader and lab/optics_scene.hpp lay the table out alike");
static_assert(sizeof scene / sizeof scene[0] == optics_layout::SIZE_VECS, "the shader's table is the size the lab fills");

void load(const OpticsScene &from) {
	const size_t vecs = std::min(from.table.size() / 4, static_cast<size_t>(optics_layout::SIZE_VECS));
	for (size_t i = 0; i < static_cast<size_t>(optics_layout::SIZE_VECS); ++i) {
		scene[i] = i < vecs ? vec4(from.table[4 * i], from.table[4 * i + 1], from.table[4 * i + 2], from.table[4 * i + 3]) : vec4();
	}
}

} // namespace shader

PixelLight optics_transport(const OpticsScene &scene, double x, double y) {
	shader::load(scene);
	const shader::Light l = shader::transport(shader::vec2(x + 0.5, y + 0.5));
	PixelLight out;
	for (int j = 0; j < optics_layout::BAND_VECS; ++j) {
		out.band[4 * j] = l.band[j].x;
		out.band[4 * j + 1] = l.band[j].y;
		out.band[4 * j + 2] = l.band[j].z;
		out.band[4 * j + 3] = l.band[j].w;
	}
	out.glow[0] = l.glow.x;
	out.glow[1] = l.glow.y;
	out.glow[2] = l.glow.z;
	out.cover = l.cover;
	return out;
}

void optics_pixel(const OpticsScene &scene, double x, double y, double rgba[4]) {
	shader::load(scene);
	const shader::vec4 c = shader::shade(shader::vec2(x + 0.5, y + 0.5));
	rgba[0] = c.x;
	rgba[1] = c.y;
	rgba[2] = c.z;
	rgba[3] = c.w;
}

std::vector<uint8_t> optics_picture(const OpticsScene &scene, int x, int y, int w, int h) {
	shader::load(scene);
	std::vector<uint8_t> out(static_cast<size_t>(w > 0 ? w : 0) * static_cast<size_t>(h > 0 ? h : 0) * 4);
	size_t at = 0;
	for (int row = 0; row < h; ++row) {
		for (int col = 0; col < w; ++col) {
			const shader::vec4 c = shader::shade(shader::vec2(x + col + 0.5, y + row + 0.5));
			for (const double v : { c.x, c.y, c.z, c.w }) {
				out[at++] = static_cast<uint8_t>(std::lround(v * 255.0));
			}
		}
	}
	return out;
}

std::string check_O1(const OpticsScene &scene) {
	using namespace optics_layout;
	if (scene.table.size() != static_cast<size_t>(SIZE_VECS) * 4) {
		return "O1: the table is not the size the shader reads";
	}
	const int count = static_cast<int>(scene.table[0]);
	if (count < 0 || count > MAX_BODIES) {
		return "O1: more bodies than the table holds";
	}
	char text[160];
	for (int i = 0; i < count; ++i) {
		const size_t body = static_cast<size_t>(HEADER_VECS + i * BODY_VECS) * 4;
		for (int k = 0; k < BANDS; ++k) {
			const double liquid = scene.table[body + static_cast<size_t>(LIQUID) * 4 + static_cast<size_t>(k)];
			const double solid = scene.table[body + static_cast<size_t>(SOLID) * 4 + static_cast<size_t>(k)];
			const double wall = scene.table[body + static_cast<size_t>(WALL) * 4 + static_cast<size_t>(k)];
			if (!(liquid >= 0.0) || !(wall >= 0.0) || !(solid >= 0.0 && solid <= 1.0)) {
				std::snprintf(text, sizeof text, "O1: body %d, band %d: a transmittance outside [0, 1] (depth %g, wall %g, reflectance %g)", i, k, liquid, wall, solid);
				return text;
			}
		}
		const double extinction = scene.table[body + static_cast<size_t>(MORE) * 4], thickness = scene.table[body + static_cast<size_t>(MORE) * 4 + 1];
		if (!(extinction >= 0.0) || !(thickness >= 0.0)) {
			std::snprintf(text, sizeof text, "O1: body %d: a negative extinction or wall (%g, %g)", i, extinction, thickness);
			return text;
		}
	}
	return "";
}

std::string check_O2(const OpticsScene &scene, double x, double y) {
	if (scene.table.size() < 4) {
		return "O2: no table";
	}
	const PixelLight l = optics_transport(scene, x, y);
	const double most = std::max({ 1.0, static_cast<double>(scene.table[1]), static_cast<double>(scene.table[2]) });
	for (int k = 0; k < optics_layout::BANDS; ++k) {
		if (!(l.band[k] >= 0.0 && l.band[k] <= most * (1.0 + 1e-12))) {
			char text[160];
			std::snprintf(text, sizeof text, "O2: at (%g, %g) band %d carries %g: more than came in", x, y, k, l.band[k]);
			return text;
		}
	}
	return "";
}

} // namespace lab
