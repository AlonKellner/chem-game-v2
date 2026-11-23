// The pass's tracer of one shape's surfaces, compiled in single precision with the GPU's own tolerances
// (GAME_SPEC §12.3, Stage 33): the same text the GPU runs (native/game/shader/optics_pixel.glsl), every sum and
// product a float's. So what single precision does to a ray is seen where no GPU runs, and held to the same ray
// in double (lab/optics_scene.hpp: optics_passage, check_O15).
#include "lab/optics_scene.hpp"
#include "lab/optics_trace.hpp"

#include <cmath>
#include <cstdint>

namespace lab {

namespace shader32 {

using uint = std::uint32_t;

// a float that stays one: a literal (a double in C++) is rounded to it before anything is done with it
struct f32 {
	float v = 0.0f;
	f32() = default;
	f32(double d) :
			v(static_cast<float>(d)) {}
	f32(float f) :
			v(f) {}
	f32(int i) :
			v(static_cast<float>(i)) {}
	explicit operator int() const { return static_cast<int>(v); }
};
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
inline f32 sqrt(f32 a) {
	return f32(std::sqrt(a.v));
}
inline f32 abs(f32 a) {
	return f32(std::fabs(a.v));
}
inline f32 min(f32 a, f32 b) {
	return a.v < b.v ? a : b;
}
inline f32 max(f32 a, f32 b) {
	return a.v > b.v ? a : b;
}
inline f32 clamp(f32 v, f32 lo, f32 hi) {
	return v.v < lo.v ? lo : (v.v > hi.v ? hi : v);
}
inline f32 pow(f32 a, f32 b) {
	return f32(std::pow(a.v, b.v));
}
inline f32 cos(f32 a) {
	return f32(std::cos(a.v));
}
inline f32 acos(f32 a) {
	return f32(std::acos(a.v));
}
inline f32 atan(f32 y, f32 x) {
	return f32(std::atan2(y.v, x.v));
}
inline f32 floor(f32 a) {
	return f32(std::floor(a.v));
}
struct vec2 {
	f32 x, y;
	vec2() = default;
	vec2(f32 a, f32 b) :
			x(a), y(b) {}
};
struct vec3 {
	f32 x, y, z;
	vec3() = default;
	vec3(f32 a, f32 b, f32 c) :
			x(a), y(b), z(c) {}
};
struct vec4 {
	f32 x, y, z, w;
	vec4() = default;
	vec4(f32 a, f32 b, f32 c, f32 d) :
			x(a), y(b), z(c), w(d) {}
};

thread_local const float *shapes = nullptr;
inline vec4 shape_vec(int i) {
	const float *v = &shapes[static_cast<size_t>(i) * 4];
	return vec4(v[0], v[1], v[2], v[3]);
}

#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wkeyword-macro"
#endif
#define float f32
#define OPTICS_SHAPES_ALONE 1
// the GPU's tolerances (native/hosts/godot/optics_compute.cpp: TRACE_HEAD)
#define TRACE_NEAR 2.0e-5
#define TRACE_NUDGE 1.0e-4
#define TRACE_SLACK 2.0e-5
#define TRACE_FINE 4.0e-7
#include "../shader/optics_pixel.glsl"
#undef float
#if defined(__clang__)
#pragma clang diagnostic pop
#endif

} // namespace shader32

static_assert(shader32::TRACE_MOST == trace::MEETINGS_MOST && shader32::TRACE_TURNS == trace::TURNS_MOST, "the pass's tracer and the truth's allow a ray the same meetings and turns");

OpticsPassage optics_passage_single(const OpticsScene &scene, int body, const double from[3], const double heading[3], double n_glass, double n_core) {
	OpticsPassage out;
	namespace L = optics_layout;
	if (body < 0 || body >= L::MAX_BODIES || scene.shapes.size() < static_cast<size_t>(L::MAX_BODIES) * L::SHAPE_BLOCK * 4) {
		return out;
	}
	shader32::shapes = scene.shapes.data();
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
