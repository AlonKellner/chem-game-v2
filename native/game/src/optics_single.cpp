// The per-pixel source compiled whole in single precision, with the GPU's own tolerances (GAME_SPEC §12.3; Stage
// 33: the tracer of one shape's surfaces; Stage 35: all of it): the same text the GPU runs
// (native/game/shader/optics_pixel.glsl), every sum and product a float's, the scene's numbers the floats the GPU is
// sent, a pixel's state and its kept beams floats as the GPU's images and buffers are. So what single precision
// does to a ray (lab/optics_scene.hpp: optics_passage, check_O15) and to a pixel's settled colour (OpticsFrames
// with `single`) is seen where no GPU runs, beside the same in double.
// It is not the GPU bit for bit: a GPU's compiler may join a product and a sum into one rounding and has its own
// sine and logarithm. What it shows is what a float's arithmetic alone makes of the source.
#include "lab/optics_scene.hpp"
#include "lab/optics_trace.hpp"

#include "optics_frames.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <unordered_map>
#include <vector>

namespace lab {

namespace shader32 {

// a float that stays one: a literal (a double in C++) is rounded to it before anything is done with it, and
// nothing is ever done in double on the way
struct f32 {
	float v = 0.0f;
	f32() = default;
	f32(double d) :
			v(static_cast<float>(d)) {}
	f32(float f) :
			v(f) {}
	f32(int i) :
			v(static_cast<float>(i)) {}
	f32(std::uint32_t u) :
			v(static_cast<float>(u)) {}
	explicit operator int() const { return static_cast<int>(v); }
	explicit operator std::uint32_t() const { return static_cast<std::uint32_t>(v); }
	f32 &operator+=(f32 o) {
		v += o.v;
		return *this;
	}
	f32 &operator-=(f32 o) {
		v -= o.v;
		return *this;
	}
	f32 &operator*=(f32 o) {
		v *= o.v;
		return *this;
	}
	f32 &operator/=(f32 o) {
		v /= o.v;
		return *this;
	}
};
using real = f32;
inline double to_double(f32 a) {
	return static_cast<double>(a.v);
}
inline f32 operator+(f32 a, f32 b) {
	return f32(a.v + b.v);
}
inline f32 operator-(f32 a, f32 b) {
	return f32(a.v - b.v);
}
inline f32 operator*(f32 a, f32 b) {
	return f32(a.v * b.v);
}
inline f32 operator/(f32 a, f32 b) {
	return f32(a.v / b.v);
}
inline f32 operator-(f32 a) {
	return f32(-a.v);
}
inline bool operator<(f32 a, f32 b) {
	return a.v < b.v;
}
inline bool operator<=(f32 a, f32 b) {
	return a.v <= b.v;
}
inline bool operator>(f32 a, f32 b) {
	return a.v > b.v;
}
inline bool operator>=(f32 a, f32 b) {
	return a.v >= b.v;
}
inline bool operator==(f32 a, f32 b) {
	return a.v == b.v;
}
inline bool operator!=(f32 a, f32 b) {
	return a.v != b.v;
}
inline f32 min(f32 a, f32 b) {
	return a.v < b.v ? a : b;
}
inline f32 max(f32 a, f32 b) {
	return a.v > b.v ? a : b;
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
inline f32 clamp(f32 v, f32 lo, f32 hi) {
	return v.v < lo.v ? lo : (v.v > hi.v ? hi : v);
}
// (two literals are two doubles in C++, which would rather be whole numbers than f32s: they are floats here)
inline f32 min(double a, double b) {
	return min(f32(a), f32(b));
}
inline f32 max(double a, double b) {
	return max(f32(a), f32(b));
}
inline f32 clamp(double v, double lo, double hi) {
	return clamp(f32(v), f32(lo), f32(hi));
}
inline f32 abs(f32 a) {
	return f32(std::fabs(a.v));
}
inline f32 sqrt(f32 a) {
	return f32(std::sqrt(a.v));
}
inline f32 exp(f32 a) {
	return f32(std::exp(a.v));
}
inline f32 log(f32 a) {
	return f32(std::log(a.v));
}
inline f32 pow(f32 a, f32 b) {
	return f32(std::pow(a.v, b.v));
}
inline f32 sin(f32 a) {
	return f32(std::sin(a.v));
}
inline f32 cos(f32 a) {
	return f32(std::cos(a.v));
}
inline f32 acos(f32 a) {
	return f32(std::acos(a.v));
}
inline f32 asin(f32 a) {
	return f32(std::asin(a.v));
}
inline f32 atan(f32 y, f32 x) {
	return f32(std::atan2(y.v, x.v));
}
inline f32 floor(f32 a) {
	return f32(std::floor(a.v));
}
inline f32 ceil(f32 a) {
	return f32(std::ceil(a.v));
}
inline f32 fract(f32 a) {
	return f32(a.v - std::floor(a.v));
}

#define OPTICS_HOST_NAMESPACE shader32
// the shapes as the GPU is sent them
#define OPTICS_HOST_SHAPES(scene) (scene).shapes
// the GPU's tolerances (native/hosts/godot/optics_compute.cpp: TRACE_HEAD)
#define OPTICS_HOST_BETWEEN 0.5
#define TRACE_NEAR 2.0e-5
#define TRACE_NUDGE 1.0e-4
#define TRACE_SLACK 2.0e-5
#define TRACE_FINE 4.0e-7
#include "optics_pixel_host.inc"

} // namespace shader32

OpticsFramesCore *optics_frames_single(int x, int y, int w, int h, int step) {
	return new shader32::Frames(x, y, w, h, step);
}

OpticsPassage optics_passage_single(const OpticsScene &scene, int body, const double from[3], const double heading[3], double n_glass, double n_core) {
	OpticsPassage out;
	namespace L = optics_layout;
	if (body < 0 || body >= L::MAX_BODIES || scene.shapes.size() < static_cast<size_t>(L::MAX_BODIES) * L::SHAPE_BLOCK * 4) {
		return out;
	}
	const shader32::Use use(scene);
	const int block = body * L::SHAPE_BLOCK;
	const shader32::vec4 head = shader32::shape_vec(block);
	const int s = static_cast<int>(head.x);
	if (s < 0) {
		return out;
	}
	const shader32::Traced t = shader32::trace_shape(s, shader32::vec3(from[0], from[1], from[2]), shader32::vec3(heading[0], heading[1], heading[2]), n_glass, n_core, head.z.v > 0.5f ? block : -1);
	out.at[0] = t.at.x.v;
	out.at[1] = t.at.y.v;
	out.at[2] = t.at.z.v;
	out.toward[0] = t.toward.x.v;
	out.toward[1] = t.toward.y.v;
	out.toward[2] = t.toward.z.v;
	out.pass = t.pass.v;
	out.chord = t.chord.v;
	out.first = t.first.v;
	out.route = t.route;
	out.meetings = t.meetings;
	out.left = t.left;
	out.solid = t.solid;
	return out;
}

} // namespace lab
