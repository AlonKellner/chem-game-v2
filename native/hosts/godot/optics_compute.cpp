#include "optics_compute.hpp"

#include <godot_cpp/classes/rd_shader_source.hpp>
#include <godot_cpp/classes/rd_shader_spirv.hpp>
#include <godot_cpp/classes/rd_texture_format.hpp>
#include <godot_cpp/classes/rd_texture_view.hpp>
#include <godot_cpp/classes/rd_uniform.hpp>
#include <godot_cpp/classes/rendering_server.hpp>
#include <godot_cpp/variant/color.hpp>
#include <godot_cpp/variant/packed_byte_array.hpp>
#include <godot_cpp/variant/typed_array.hpp>

#include <algorithm>
#include <cstdlib>
#include <chrono>
#include <cstring>

namespace chem_godot {

namespace {

using godot::RenderingDevice;

// What the per-pixel source is given, and what it writes (lab/optics_scene.hpp lays the buffers out).
const char *const WRAPPER_HEAD = R"(#version 450
layout(set = 0, binding = 0, std430) restrict readonly buffer SceneBuffer { vec4 v[]; } scene_buffer;
layout(set = 0, binding = 1, std430) restrict readonly buffer RowBuffer { float v[]; } row_buffer;
layout(set = 0, binding = 2, std430) restrict readonly buffer LineBuffer { vec4 v[]; } line_buffer;
layout(set = 0, binding = 3, std430) restrict readonly buffer LightBuffer { vec4 v[]; } light_buffer;
layout(set = 0, binding = 4, rgba32f) uniform restrict image2D hist_image;
layout(set = 0, binding = 5, rgba32f) uniform restrict image2D key_image;
layout(set = 0, binding = 6, rgba32f) uniform restrict image2D prox_image;
layout(set = 0, binding = 7, rgba32f) uniform restrict image2D aux_image;
layout(set = 0, binding = 8, rgba8) uniform restrict image2D shown_image;
layout(set = 0, binding = 9, std430) restrict buffer AskBuffer { uint total[6]; } ask_buffer;
layout(set = 0, binding = 11, std430) restrict readonly buffer WallBuffer { vec4 v[]; } wall_buffer;
layout(set = 0, binding = 13, std430) restrict readonly buffer ShapeBuffer { vec4 v[]; } shape_buffer;
layout(set = 0, binding = 14, std430) restrict buffer CornerBuffer { vec4 v[]; } corner_buffer;
layout(set = 0, binding = 15, std430) restrict buffer KeptBuffer { vec4 v[]; } kept_buffer;
layout(set = 0, binding = 16, std430) restrict readonly buffer ImageBuffer { vec4 v[]; } image_buffer;
layout(set = 0, binding = 17, rgba32f) uniform restrict readonly image2D anchor_hist;
layout(set = 0, binding = 18, rgba32f) uniform restrict readonly image2D anchor_prox;
layout(set = 0, binding = 19, rgba32f) uniform restrict readonly image2D anchor_aux;
layout(set = 0, binding = 20, rgba8) uniform restrict readonly image2D anchor_seen;
layout(set = 0, binding = 21, rgba8) uniform restrict readonly image2D whole_seen;
layout(set = 0, binding = 22, std430) restrict buffer RelayBuffer { vec4 v[]; } relay_buffer;
layout(push_constant, std430) uniform Params { int frame; int parity; float budget; float pixels; int left; int top; int right; int bottom; int kept; int stamp; float rays; float dt; vec4 view_now; vec4 view_from; vec4 room; int mode; float level; int pad4; int pad5; } params;
// (Stage 90) the display's exposure: what it is, what the pixels are shown at, whether they are shown again this frame,
// its target; and the meter's 16 x 9 blocks' summed light (a 256th of the room's white a unit)
layout(set = 0, binding = 23, std430) restrict buffer ExposureBuffer { float e; float shown_e; uint reshow; float target; uint sums[144]; } exposure_buffer;
float display_exposure() { return exposure_buffer.shown_e; }
vec4 scene_vec(int i) { return scene_buffer.v[i]; }
float row_value(int i) { return row_buffer.v[i]; }
vec4 line_vec(int i) { return line_buffer.v[i]; }
vec4 light_vec(int i) { return light_buffer.v[i]; }
vec4 wall_vec(int i) { return wall_buffer.v[i]; }
vec4 shape_vec(int i) { return shape_buffer.v[i]; }
vec4 corner_vec(int i) { return corner_buffer.v[i]; }
void corner_put(int i, vec4 v) { corner_buffer.v[i] = v; }
vec4 kept_vec(int i) { return kept_buffer.v[i]; }
void kept_put(int i, vec4 v) { kept_buffer.v[i] = v; }
vec4 image_vec(int i) { return image_buffer.v[i]; }
vec4 relay_vec(int i) { return relay_buffer.v[i]; }
void relay_put(int i, vec4 v) { relay_buffer.v[i] = v; }
// the pixels as they were at the view before it moved (Stage 42): hist, prox, aux, and what they showed (linear)
float anchor_linear(float v) { return v <= 0.04045 ? v / 12.92 : pow((v + 0.055) / 1.055, 2.4); }
vec4 anchor_vec(int which, int x, int y) {
	ivec2 at = ivec2(clamp(x, 0, 1919), clamp(y, 0, 1079));
	if (which == 0) { return imageLoad(anchor_hist, at); }
	if (which == 1) { return imageLoad(anchor_prox, at); }
	if (which == 2) { return imageLoad(anchor_aux, at); }
	vec4 c = which == 3 ? imageLoad(anchor_seen, at) : imageLoad(whole_seen, at);
	return vec4(anchor_linear(c.x), anchor_linear(c.y), anchor_linear(c.z), c.w);
}
// ---- the per-pixel source (native/game/shader/optics_pixel.glsl)
)";

// A pixel's own rays are traced in the pass (Stage 29): the tracer's tolerances in single precision
const char *const TRACE_HEAD = R"(
#define TRACE_NEAR 2.0e-5
#define TRACE_NUDGE 1.0e-4
#define TRACE_SLACK 2.0e-5
#define TRACE_FINE 4.0e-7
// (how far a beam's middle ray may land from the cubic through its nodes before its stretch of the spectrum is
// halved: in single precision two rays a nanometre apart through two vessels land up to half a pixel apart by
// rounding alone, and at the reference's 0.02 a carried vessel's pixels halved to 64 stretches, 1,300 rays each,
// for nothing: 30 ms frames, Stage 33)
#define BEAM_BETWEEN 0.5
)";

const char *const WRAPPER_TAIL = R"(
layout(local_size_x = OPTICS_GROUP_X, local_size_y = OPTICS_GROUP_Y, local_size_z = 1) in;
// ---- one invocation a pixel
// The pass over the whole screen (OPTICS_SCREEN_PASS) is compiled without the reading by a pixel's own rays, and
// leaves the pixels that are read by it alone; the beams' pass (OPTICS_BEAM_PASS), over the rectangles where they
// can be, takes those and no others.
#if defined(OPTICS_METER_PASS)
// The meter (Stage 90): each pixel's light in linear terms (a body's history, the wall's own where none is), its
// luminance summed over its group in shared memory and added once into its block's sum (16 x 9 blocks of 120 x 120)
shared float meter_sum[OPTICS_GROUP_X * OPTICS_GROUP_Y];
void main() {
	ivec2 at = ivec2(gl_GlobalInvocationID.xy);
	uint me = gl_LocalInvocationIndex;
	float y = 0.0;
	if (at.x < 1920 && at.y < 1080) {
		vec2 p = vec2(float(at.x) + 0.5, float(at.y) + 0.5);
		if (!in_panels(p)) {
			vec4 k = imageLoad(key_image, at);
			if (k == wall_key()) {
				y = wall_span(wall_profile(p.y), p.x - 0.5, p.x + 0.5);
			} else {
				vec4 hst = imageLoad(hist_image, at), prx = imageLoad(prox_image, at);
				y = meter_luminance(vec3(hst.x + prx.x, hst.y + prx.y, hst.z + prx.z));
			}
		}
	}
	meter_sum[me] = clamp(y, 0.0, 250.0);
	barrier();
	for (uint reach = uint(OPTICS_GROUP_X * OPTICS_GROUP_Y) / 2u; reach > 0u; reach /= 2u) {
		if (me < reach) {
			meter_sum[me] += meter_sum[me + reach];
		}
		barrier();
	}
	if (me == 0u) {
		int bx = int(gl_WorkGroupID.x) * OPTICS_GROUP_X / 120, by = int(gl_WorkGroupID.y) * OPTICS_GROUP_Y / 120;
		if (bx < METER_X && by < METER_Y) {
			atomicAdd(exposure_buffer.sums[by * METER_X + bx], uint(meter_sum[0] * 256.0 + 0.5));
		}
	}
}
#elif defined(OPTICS_EXPOSE_PASS)
// The exposure's step (Stage 90): the brightest block's mean light, the target it and the room's level ask, a step
// toward it; the pixels shown again where it has moved by more than half a per cent; the sums cleared for the next frame
void main() {
	float brightest = 0.0;
	for (int k = 0; k < METER_X * METER_Y; ++k) {
		brightest = max(brightest, float(exposure_buffer.sums[k]) / (256.0 * 14400.0));
		exposure_buffer.sums[k] = 0u;
	}
	float t = exposure_target(brightest, params.level);
	float e = exposure_step(exposure_buffer.e > 0.0 ? exposure_buffer.e : 1.0, t, params.dt);
	exposure_buffer.e = e;
	exposure_buffer.target = t;
	float shown = exposure_buffer.shown_e > 0.0 ? exposure_buffer.shown_e : 1.0;
	if (abs(log(e / shown)) > 0.005) {
		exposure_buffer.shown_e = e;
		exposure_buffer.reshow = 1u;
	} else {
		exposure_buffer.shown_e = shown;
		exposure_buffer.reshow = 0u;
	}
}
#elif defined(OPTICS_RESAMPLE_PASS)
// A view's change (Stage 42): what each pixel shows, carried from the anchor's pixels to the view now (mode 0, while
// the view moves); and at rest (mode 1), its state too, its content key's shape parts no longer its
void main() {
	ivec2 at = ivec2(gl_GlobalInvocationID.xy) + ivec2(params.left, params.top);
	if (at.x >= params.right || at.y >= params.bottom || at.x < 0 || at.y < 0 || at.x >= 1920 || at.y >= 1080) {
		return;
	}
	vec2 p = vec2(float(at.x) + 0.5, float(at.y) + 0.5);
	// (where the view's anchor holds the room, its pixels; beyond, what the room showed when last seen whole)
	vec3 a = anchor_place(p, params.view_now, params.view_from);
	bool covered = anchor_covers(a, params.room);
	vec3 c = covered ? anchor_shown(a, 3) : anchor_shown(anchor_place(p, params.view_now, vec4(1.0, 0.0, 0.0, 0.0)), 4);
	imageStore(shown_image, at, vec4(encode1(c.x), encode1(c.y), encode1(c.z), 1.0));
	if (params.mode == 0) {
		return;
	}
	bool bare = !near_any(p);
	Path q;
	if (!bare) {
		q = path_of(p, 1.0);
		bare = q.cover <= 0.0;
	}
	if (bare || !covered) {
		imageStore(key_image, at, vec4(-3.0)); // the wall, shown again from its table; or no anchor: it starts afresh
		return;
	}
	PixelState s = anchor_state(p, params.view_now, params.view_from, q.key.z);
	imageStore(hist_image, at, s.hist);
	imageStore(key_image, at, s.key);
	imageStore(prox_image, at, s.prox);
	imageStore(aux_image, at, s.aux);
}
#elif defined(OPTICS_CORNER_PASS)
// The corners' pass (Stage 33): one invocation a corner of the lattice, over a rectangle's corners (its pixels'
// and one more across and down), before the pixels' passes read them.
void main() {
	ivec2 at = ivec2(gl_GlobalInvocationID.xy) + ivec2(params.left, params.top);
	if (at.x >= params.right || at.y >= params.bottom || at.x < 0 || at.y < 0 || at.x > 1920 || at.y > 1080) {
		return;
	}
	corner_step(vec2(float(at.x), float(at.y)));
}
#else
void main() {
	ivec2 at = ivec2(gl_GlobalInvocationID.xy) + ivec2(params.left, params.top);
	if (at.x >= params.right || at.y >= params.bottom || at.x < 0 || at.y < 0 || at.x >= 1920 || at.y >= 1080) {
		return;
	}
	vec2 p = vec2(float(at.x) + 0.5, float(at.y) + 0.5);
#ifdef OPTICS_BEAM_PASS
	if (!near_any(p)) {
		return;
	}
	Path q = path_of(p, 1.0);
	if (q.cover <= 0.0) {
		return;
	}
#else
	// no body here: the wall drawn there, written once for each wall (its state holds the wall's tag)
	Path q;
	bool bare = !near_any(p);
	if (!bare) {
		q = path_of(p, 1.0);
		bare = q.cover <= 0.0;
	}
	if (bare) {
		vec4 wall = wall_key();
		if (imageLoad(key_image, at) != wall || exposure_buffer.reshow != 0u) { // (Stage 90: or shown again at a new exposure)
			imageStore(shown_image, at, wall_shown(p, 1.0));
			imageStore(key_image, at, wall);
			imageStore(aux_image, at, vec4(0.0));
		}
		return;
	}
#endif
	PixelState s;
	s.key = imageLoad(key_image, at);
	s.aux = imageLoad(aux_image, at);
	if (done(q, s.key, s.aux)) {
		// settled: what it shows stands, unless it was cleared while nothing covered it (a flame gone and lit
		// again as it was, a vessel moved away and back): then it is shown again from its history
		if (imageLoad(shown_image, at).w < 0.5 || exposure_buffer.reshow != 0u) { // (Stage 90: or at a new exposure)
			s.hist = imageLoad(hist_image, at);
			s.prox = imageLoad(prox_image, at);
			relay_slot = q.relay >= 0 ? relay_slot_at(q.relay, p) : -1;
			imageStore(shown_image, at, shown(q, s));
		}
		return;
	}
	// whose pixel it is (asked only of a pixel with something still to do)
#ifdef OPTICS_BEAM_PASS
	if (!beamed(q)) {
		return;
	}
#else
	if (beamed(q)) {
		return; // the beams' own pass reads it
	}
#endif
#ifdef OPTICS_BEAM_PASS
	// a pixel within two bodies' rectangles is stepped in the first's pass alone (Stage 33): once a frame, as a
	// pixel is (its state says in which run of the passes it was last stepped)
	if (s.aux.w == float(params.stamp)) {
		return;
	}
#endif
	s.hist = imageLoad(hist_image, at);
	s.prox = imageLoad(prox_image, at);
#ifdef OPTICS_BEAM_PASS
	// (the rays' own budget, Stage 33: their asks are summed apart, in the totals' second pair; and the beams
	// traced last frame are counted against it, in the third: while a thing moves, what is settling waits)
	float scale = budget_scale(float(ask_buffer.total[2 + params.parity]), max(params.rays - RAY_POINTS_A_BEAM * float(ask_buffer.total[4 + params.parity]), 0.0), 1.0);
#else
	float scale = budget_scale(float(ask_buffer.total[params.parity]), params.budget, params.pixels);
#endif
#ifdef OPTICS_BEAM_PASS
	lattice_on = true; // its corners were traced by the pass before this one
	// its record among its rectangle's (Stage 33), if the rectangle has room in the pool
	kept_slot = params.kept < 0 ? -1 : (params.kept + (at.y - params.top) * (params.right - params.left) + (at.x - params.left)) * (1 + KEPT_MOST * KEPT_BEAM);
#endif
	s = step_pixel(q, s, p, params.frame, scale);
	// a sensor's display: its record's step (Stage 46), what it asks for added to the pixel's
	relay_slot = q.relay >= 0 ? relay_slot_at(q.relay, p) : -1;
	s.aux.z = s.aux.z + relay_step(q, p, params.frame);
#ifdef OPTICS_BEAM_PASS
	s.aux.w = float(params.stamp);
	if (beam_deferred) {
		atomicAdd(ask_buffer.total[5 - params.parity], 1u);
	}
#endif
	imageStore(hist_image, at, s.hist);
	imageStore(key_image, at, s.key);
	imageStore(prox_image, at, s.prox);
	imageStore(aux_image, at, s.aux);
	if (s.aux.z > 0.0) {
#ifdef OPTICS_BEAM_PASS
		atomicAdd(ask_buffer.total[3 - params.parity], uint(s.aux.z + 0.5));
#else
		atomicAdd(ask_buffer.total[1 - params.parity], uint(s.aux.z + 0.5));
#endif
	}
	imageStore(shown_image, at, shown(q, s));
}
#endif
)";

} // namespace

OpticsCompute::~OpticsCompute() {
	release();
}

void OpticsCompute::release() {
	if (rd_ == nullptr) {
		return;
	}
	shown_texture_.unref();
	for (const godot::RID &rid : { uniforms_[0], uniforms_[1], uniforms_[2], uniforms_[3], uniforms_[4], uniforms_[5], pipeline_[0], pipeline_[1], pipeline_[2], pipeline_[3], pipeline_[4], pipeline_[5], shader_[0], shader_[1], shader_[2], shader_[3],
				 shader_[4], shader_[5], corners_, kept_, relay_, scene_.rid, rows_.rid, lines_.rid, lights_.rid, wall_.rid, shapes_.rid, images_.rid, anchor_[0], anchor_[1], anchor_[2], anchor_[3], whole_, asked_, exposure_, state_[0],
				 state_[1], state_[2], state_[3], shown_ }) {
		if (rid.is_valid()) {
			rd_->free_rid(rid);
		}
	}
	rd_ = nullptr;
}

godot::RID OpticsCompute::image(RenderingDevice::DataFormat format) {
	godot::Ref<godot::RDTextureFormat> f;
	f.instantiate();
	f->set_format(format);
	f->set_width(WIDTH);
	f->set_height(HEIGHT);
	f->set_usage_bits(RenderingDevice::TEXTURE_USAGE_STORAGE_BIT | RenderingDevice::TEXTURE_USAGE_SAMPLING_BIT | RenderingDevice::TEXTURE_USAGE_CAN_UPDATE_BIT |
			RenderingDevice::TEXTURE_USAGE_CAN_COPY_TO_BIT | RenderingDevice::TEXTURE_USAGE_CAN_COPY_FROM_BIT);
	godot::Ref<godot::RDTextureView> view;
	view.instantiate();
	return rd_->texture_create(f, view);
}

bool OpticsCompute::open(const godot::String &pixel_source, godot::String &why) {
	godot::RenderingServer *rs = godot::RenderingServer::get_singleton();
	RenderingDevice *rd = rs != nullptr ? rs->get_rendering_device() : nullptr;
	if (rd == nullptr) {
		why = "no RenderingDevice (a headless or Compatibility renderer): the optical pass is off";
		return false;
	}
	if (pixel_source.is_empty()) {
		why = "the per-pixel source is missing (res://shaders/optics_pixel.glslinc: make native copies it)";
		return false;
	}
	// the source once for each pass: over the screen, over the beams' rectangles, over their corners
	rd_ = rd;
	if (const char *asked = std::getenv("LAB_RAY_POINTS")) {
		ray_points_ = static_cast<float>(std::atof(asked));
	}
	for (int pass = 0; pass < PASSES; ++pass) {
		godot::Ref<godot::RDShaderSource> source;
		source.instantiate();
		source->set_language(RenderingDevice::SHADER_LANGUAGE_GLSL);
		source->set_stage_source(RenderingDevice::SHADER_STAGE_COMPUTE,
				godot::String(WRAPPER_HEAD) + godot::String(TRACE_HEAD) +
						godot::String(pass == BEAMS ? "#define OPTICS_BEAM_PASS 1\n"
										: pass == CORNERS ? "#define OPTICS_SCREEN_PASS 1\n#define OPTICS_CORNER_PASS 1\n"
										: pass == RESAMPLE ? "#define OPTICS_SCREEN_PASS 1\n#define OPTICS_RESAMPLE_PASS 1\n"
										: pass == METER	   ? "#define OPTICS_SCREEN_PASS 1\n#define OPTICS_METER_PASS 1\n"
										: pass == EXPOSE   ? "#define OPTICS_SCREEN_PASS 1\n#define OPTICS_EXPOSE_PASS 1\n"
														   : "#define OPTICS_SCREEN_PASS 1\n") +
						"#define OPTICS_GROUP_X " + godot::String::num_int64(pass == METER ? 8 : pass == EXPOSE ? 1 : GROUP_X) + "\n#define OPTICS_GROUP_Y " +
						godot::String::num_int64(pass == METER ? 8 : pass == EXPOSE ? 1 : GROUP_Y) + "\n" + pixel_source + godot::String(WRAPPER_TAIL));
		const godot::Ref<godot::RDShaderSPIRV> spirv = rd->shader_compile_spirv_from_source(source);
		if (spirv.is_null() || !spirv->get_stage_compile_error(RenderingDevice::SHADER_STAGE_COMPUTE).is_empty()) {
			why = "the optical pass does not compile: " + (spirv.is_null() ? godot::String("no SPIR-V") : spirv->get_stage_compile_error(RenderingDevice::SHADER_STAGE_COMPUTE));
			release();
			return false;
		}
		shader_[pass] = rd_->shader_create_from_spirv(spirv, pass == BEAMS ? "chem optical pass: the beams" : pass == CORNERS ? "chem optical pass: the corners" : pass == RESAMPLE ? "chem optical pass: a view's change" : pass == METER ? "chem optical pass: the meter" : pass == EXPOSE ? "chem optical pass: the exposure" : "chem optical pass");
		if (!shader_[pass].is_valid()) {
			why = "the optical pass's shader was not created";
			release();
			return false;
		}
		pipeline_[pass] = rd_->compute_pipeline_create(shader_[pass]);
	}
	for (godot::RID &s : state_) {
		s = image(RenderingDevice::DATA_FORMAT_R32G32B32A32_SFLOAT);
	}
	// a content key matches nothing at first: every pixel's history starts with its first frame
	rd_->texture_clear(state_[1], godot::Color(-1, -1, -1, -1), 0, 1, 0, 1);
	for (const int i : { 0, 2, 3 }) {
		rd_->texture_clear(state_[i], godot::Color(0, 0, 0, 0), 0, 1, 0, 1);
	}
	shown_ = image(RenderingDevice::DATA_FORMAT_R8G8B8A8_UNORM);
	rd_->texture_clear(shown_, godot::Color(0, 0, 0, 0), 0, 1, 0, 1);
	// the pixels as they were at the view before it moved (Stage 42): their history, estimate and noise, and what
	// they showed
	for (int i = 0; i < 3; ++i) {
		anchor_[i] = image(RenderingDevice::DATA_FORMAT_R32G32B32A32_SFLOAT);
		rd_->texture_clear(anchor_[i], godot::Color(0, 0, 0, 0), 0, 1, 0, 1);
	}
	anchor_[3] = image(RenderingDevice::DATA_FORMAT_R8G8B8A8_UNORM);
	rd_->texture_clear(anchor_[3], godot::Color(0, 0, 0, 0), 0, 1, 0, 1);
	whole_ = image(RenderingDevice::DATA_FORMAT_R8G8B8A8_UNORM);
	rd_->texture_clear(whole_, godot::Color(0, 0, 0, 0), 0, 1, 0, 1);
	shown_texture_.instantiate();
	shown_texture_->set_texture_rd_rid(shown_);
	return true;
}

void OpticsCompute::hold(Buffer &buffer, const float *data, int32_t floats) {
	const uint32_t bytes = static_cast<uint32_t>(floats > 0 ? floats : 0) * 4;
	if (bytes > buffer.capacity || !buffer.rid.is_valid()) {
		// larger than it was: a new buffer, with room to grow, and the uniform sets that name it made again; the
		// room beyond what it is given is cleared
		for (godot::RID &set : uniforms_) {
			if (set.is_valid()) {
				rd_->free_rid(set);
				set = godot::RID();
			}
		}
		uint32_t capacity = std::max<uint32_t>(1024, buffer.capacity);
		while (capacity < bytes) {
			capacity *= 2;
		}
		const godot::RID made = rd_->storage_buffer_create(capacity);
		if (buffer.rid.is_valid()) {
			rd_->free_rid(buffer.rid);
		}
		if (capacity > bytes) {
			rd_->buffer_clear(made, bytes, capacity - bytes);
		}
		buffer.rid = made;
		buffer.capacity = capacity;
	}
	if (bytes > 0) {
		godot::PackedByteArray given;
		given.resize(bytes);
		std::memcpy(given.ptrw(), data, bytes);
		rd_->buffer_update(buffer.rid, 0, bytes, given);
	}
}

void OpticsCompute::upload(const cg_optics &optics) {
	if (rd_ == nullptr) {
		return;
	}
	if (!have_scene_ || optics.revision != uploaded_) {
		hold(scene_, optics.table, 4 * optics.vec_count);
		hold(rows_, optics.rows, optics.row_count);
		hold(lines_, optics.lines, 4 * optics.line_count);
		hold(lights_, optics.lights, 4 * optics.light_count);
		uploaded_ = optics.revision;
	}
	if (!have_scene_ || optics.wall_revision != uploaded_wall_) {
		hold(wall_, optics.wall, 4 * optics.wall_count);
		uploaded_wall_ = optics.wall_revision;
	}
	if (!have_scene_ || optics.shapes_revision != uploaded_shapes_) {
		hold(shapes_, optics.shapes, 4 * optics.shape_count);
		uploaded_shapes_ = optics.shapes_revision;
	}
	// the images things emit by (Stage 42: a screen's), with a revision of their own
	if (!have_scene_ || optics.images_revision != uploaded_images_) {
		hold(images_, optics.images, optics.images != nullptr ? 4 * optics.image_count : 0);
		uploaded_images_ = optics.images_revision;
	}
	if (!asked_.is_valid()) {
		godot::PackedByteArray zero;
		zero.resize(24);
		zero.fill(0);
		asked_ = rd_->storage_buffer_create(24, zero);
	}
	if (!exposure_.is_valid()) {
		// (Stage 90) the exposure at 1, shown at 1, nothing to show again, the target 1; the sums nothing
		godot::PackedByteArray start;
		start.resize(4 * (4 + 144));
		start.fill(0);
		const float one = 1.0f;
		std::memcpy(start.ptrw(), &one, 4);
		std::memcpy(start.ptrw() + 4, &one, 4);
		std::memcpy(start.ptrw() + 12, &one, 4);
		exposure_ = rd_->storage_buffer_create(static_cast<uint32_t>(start.size()), start);
	}
	room_level_ = optics.room_level > 0.0f ? optics.room_level : 1.0f; // (Stage 90) the room's lights, for the exposure's bounds
	// where the passes over the pixels read by their own rays run
	rectangles_.assign(optics.rectangles, optics.rectangles + (optics.rectangles != nullptr && optics.rectangle_count > 0 ? 4 * optics.rectangle_count : 0));
	if (!corners_.is_valid()) {
		// the lattice of pixel corners (Stage 33), over the whole screen: nothing traced at first
		const uint32_t bytes = 1921u * 1081u * 4u * 16u;
		corners_ = rd_->storage_buffer_create(bytes);
		rd_->buffer_clear(corners_, 0, bytes);
	}
	// each rectangle's records in the pool of kept beams (Stage 33), one rectangle after another in the list's
	// order: a rectangle that is where and what it was has the records it had, and one that is not finds records
	// that are not its pixels' and traces again. The pool grows with what the rectangles need, up to a limit.
	{
		constexpr int64_t MOST = int64_t(384) * 1024 * 1024 / (CG_OPTICS_KEPT_VECS * 16); // records
		kept_base_.clear();
		int64_t records = 0;
		for (size_t i = 0; i + 3 < rectangles_.size(); i += 4) {
			const int64_t left = std::max(0, static_cast<int32_t>(rectangles_[i])), top = std::max(0, static_cast<int32_t>(rectangles_[i + 1]));
			const int64_t right = std::min(WIDTH, static_cast<int32_t>(rectangles_[i + 2])), bottom = std::min(HEIGHT, static_cast<int32_t>(rectangles_[i + 3]));
			const int64_t area = std::max<int64_t>(right - left, 0) * std::max<int64_t>(bottom - top, 0);
			const bool room = records + area <= MOST;
			kept_base_.push_back(room ? static_cast<int32_t>(records) : -1);
			records += room ? area : 0;
		}
		if (!kept_.is_valid() || records > kept_capacity_) {
			for (godot::RID &set : uniforms_) {
				if (set.is_valid()) {
					rd_->free_rid(set);
					set = godot::RID();
				}
			}
			if (kept_.is_valid()) {
				rd_->free_rid(kept_);
			}
			kept_capacity_ = std::min(MOST, std::max<int64_t>(records + records / 4, 4096));
			const uint32_t bytes = static_cast<uint32_t>(kept_capacity_ * CG_OPTICS_KEPT_VECS * 16);
			kept_ = rd_->storage_buffer_create(bytes);
			rd_->buffer_clear(kept_, 0, bytes);
		}
	}
	// the sensor displays' pixels' records (Stage 46): as many as the scene asks, all zero again when that changes
	if (!relay_.is_valid() || optics.relay_pool != relay_pool_) {
		const int64_t records = std::max<int64_t>(optics.relay_pool, 1);
		if (!relay_.is_valid() || records > relay_capacity_) {
			for (godot::RID &set : uniforms_) {
				if (set.is_valid()) {
					rd_->free_rid(set);
					set = godot::RID();
				}
			}
			if (relay_.is_valid()) {
				rd_->free_rid(relay_);
			}
			relay_capacity_ = std::max<int64_t>(records, 1024);
			relay_ = rd_->storage_buffer_create(static_cast<uint32_t>(relay_capacity_ * CG_OPTICS_RELAY_VECS * 16));
		}
		rd_->buffer_clear(relay_, 0, static_cast<uint32_t>(relay_capacity_ * CG_OPTICS_RELAY_VECS * 16));
		relay_pool_ = optics.relay_pool;
	}
	if (!uniforms_[0].is_valid() || !uniforms_[1].is_valid() || !uniforms_[2].is_valid() || !uniforms_[3].is_valid() || !uniforms_[METER].is_valid() || !uniforms_[EXPOSE].is_valid()) {
		godot::TypedArray<godot::RDUniform> uniforms;
		const auto add = [&](RenderingDevice::UniformType type, int binding, const godot::RID &id) {
			godot::Ref<godot::RDUniform> u;
			u.instantiate();
			u->set_uniform_type(type);
			u->set_binding(binding);
			u->add_id(id);
			uniforms.push_back(u);
		};
		add(RenderingDevice::UNIFORM_TYPE_STORAGE_BUFFER, 0, scene_.rid);
		add(RenderingDevice::UNIFORM_TYPE_STORAGE_BUFFER, 1, rows_.rid);
		add(RenderingDevice::UNIFORM_TYPE_STORAGE_BUFFER, 2, lines_.rid);
		add(RenderingDevice::UNIFORM_TYPE_STORAGE_BUFFER, 3, lights_.rid);
		for (int i = 0; i < 4; ++i) {
			add(RenderingDevice::UNIFORM_TYPE_IMAGE, 4 + i, state_[i]);
		}
		add(RenderingDevice::UNIFORM_TYPE_IMAGE, 8, shown_);
		add(RenderingDevice::UNIFORM_TYPE_STORAGE_BUFFER, 9, asked_);
		add(RenderingDevice::UNIFORM_TYPE_STORAGE_BUFFER, 11, wall_.rid);
		add(RenderingDevice::UNIFORM_TYPE_STORAGE_BUFFER, 13, shapes_.rid);
		add(RenderingDevice::UNIFORM_TYPE_STORAGE_BUFFER, 16, images_.rid);
		for (int i = 0; i < 4; ++i) {
			add(RenderingDevice::UNIFORM_TYPE_IMAGE, 17 + i, anchor_[i]);
		}
		add(RenderingDevice::UNIFORM_TYPE_IMAGE, 21, whole_);
		add(RenderingDevice::UNIFORM_TYPE_STORAGE_BUFFER, 14, corners_);
		add(RenderingDevice::UNIFORM_TYPE_STORAGE_BUFFER, 15, kept_);
		add(RenderingDevice::UNIFORM_TYPE_STORAGE_BUFFER, 22, relay_);
		add(RenderingDevice::UNIFORM_TYPE_STORAGE_BUFFER, 23, exposure_); // (Stage 90)
		for (int pass = 0; pass < PASSES; ++pass) {
			if (uniforms_[pass].is_valid()) {
				rd_->free_rid(uniforms_[pass]);
			}
			uniforms_[pass] = rd_->uniform_set_create(uniforms, shader_[pass], 0);
		}
	}
	have_scene_ = true;
}

float OpticsCompute::exposure() const {
	if (rd_ == nullptr || !exposure_.is_valid()) {
		return 1.0f;
	}
	const godot::PackedByteArray bytes = rd_->buffer_get_data(exposure_, 4, 4);
	float shown = 1.0f;
	if (bytes.size() == 4) {
		std::memcpy(&shown, bytes.ptr(), 4);
	}
	return shown > 0.0f ? shown : 1.0f;
}

void OpticsCompute::restart() {
	if (rd_ != nullptr) {
		rd_->texture_clear(state_[1], godot::Color(-1, -1, -1, -1), 0, 1, 0, 1);
	}
}

void OpticsCompute::run(double budget, bool again) {
	if (rd_ == nullptr || !have_scene_) {
		return;
	}
	if (again) {
		--frame_;
		parity_ = 1 - parity_;
	} else {
		// this frame adds its asks into the other total, which starts from nothing
		rd_->buffer_clear(asked_, static_cast<uint32_t>(4 * (1 - parity_)), 4);
		rd_->buffer_clear(asked_, static_cast<uint32_t>(4 * (3 - parity_)), 4);
		rd_->buffer_clear(asked_, static_cast<uint32_t>(4 * (5 - parity_)), 4);
	}
	stamp_ = stamp_ % 16000000 + 1; // (a float holds it exactly; 0 is no run)
	// (Stage 90) the frame's seconds, for the exposure's step: none for a frame run again
	const double now_s = static_cast<double>(std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch()).count()) * 1e-6;
	const float dt = again || last_frame_s_ < 0.0 ? 0.0f : static_cast<float>(std::clamp(now_s - last_frame_s_, 0.0, 0.1));
	last_frame_s_ = now_s;
	const int64_t list = rd_->compute_list_begin();
	const auto pass_over = [&](int pass, int32_t left, int32_t top, int32_t right, int32_t bottom, int32_t kept = -1) {
		const Params params{ frame_, parity_, static_cast<float>(budget), static_cast<float>(WIDTH * HEIGHT), left, top, right, bottom, kept, stamp_, ray_points_, dt, { 1, 0, 0, 0 }, { 1, 0, 0, 0 }, { 0, 0, 0, 0 }, 0, room_level_, 0, 0 };
		godot::PackedByteArray push;
		push.resize(sizeof params);
		std::memcpy(push.ptrw(), &params, sizeof params);
		rd_->compute_list_bind_compute_pipeline(list, pipeline_[pass]);
		rd_->compute_list_bind_uniform_set(list, uniforms_[pass], 0);
		rd_->compute_list_set_push_constant(list, push, static_cast<uint32_t>(push.size()));
		// groups two pixels wide and thirty-two tall: a vessel's pixels in one column are read in the same
		// number of parts, and pixels run together wait for the one with most to do (measured: 8 x 8, 1.72 ms
		// a pass where every history starts again; 2 x 32, 1.56)
		const int gx = pass == METER ? 8 : pass == EXPOSE ? 1 : GROUP_X, gy = pass == METER ? 8 : pass == EXPOSE ? 1 : GROUP_Y;
		rd_->compute_list_dispatch(list, static_cast<uint32_t>((right - left + gx - 1) / gx), static_cast<uint32_t>((bottom - top + gy - 1) / gy), 1);
	};
	// the whole screen, with nothing of the reading by a pixel's own rays: what a pass costs follows what its shader
	// could do (measured, Stage 21: with the other readings in it, a settled pass 0.58 ms and a restarting one 2.15;
	// without, nothing and 1.56)
	pass_over(SCREEN, 0, 0, WIDTH, HEIGHT);
	// then, over each rectangle where pixels can be read by their own rays: the corners of its pixels first (Stage
	// 33), then the pixels (Stage 29)
	for (size_t i = 0; i + 3 < rectangles_.size(); i += 4) {
		const int32_t left = std::max(0, static_cast<int32_t>(rectangles_[i])), top = std::max(0, static_cast<int32_t>(rectangles_[i + 1]));
		const int32_t right = std::min(WIDTH, static_cast<int32_t>(rectangles_[i + 2])), bottom = std::min(HEIGHT, static_cast<int32_t>(rectangles_[i + 3]));
		if (right > left && bottom > top) {
			rd_->compute_list_add_barrier(list);
			pass_over(CORNERS, left, top, right + 1, bottom + 1);
			rd_->compute_list_add_barrier(list);
			pass_over(BEAMS, left, top, right, bottom, i / 4 < kept_base_.size() ? kept_base_[i / 4] : -1);
		}
	}
	// (Stage 90) the meter over the whole screen, then the exposure's step, for the next frame's pixels
	rd_->compute_list_add_barrier(list);
	pass_over(METER, 0, 0, WIDTH, HEIGHT);
	rd_->compute_list_add_barrier(list);
	pass_over(EXPOSE, 0, 0, 1, 1);
	rd_->compute_list_end();
	parity_ = 1 - parity_;
	++frame_;
}

void OpticsCompute::anchor() {
	if (rd_ == nullptr) {
		return;
	}
	const godot::Vector3 origin(0, 0, 0), size(WIDTH, HEIGHT, 1);
	const godot::RID from[4] = { state_[0], state_[2], state_[3], shown_ };
	for (int i = 0; i < 4; ++i) {
		rd_->texture_copy(from[i], anchor_[i], origin, origin, size, 0, 0, 0, 0);
	}
}

void OpticsCompute::keep_whole() {
	if (rd_ != nullptr) {
		rd_->texture_copy(shown_, whole_, godot::Vector3(0, 0, 0), godot::Vector3(0, 0, 0), godot::Vector3(WIDTH, HEIGHT, 1), 0, 0, 0, 0);
	}
}

void OpticsCompute::resample(const float now[4], const float from[4], const float room[4], bool rest) {
	if (rd_ == nullptr || !have_scene_ || !uniforms_[RESAMPLE].is_valid()) {
		return;
	}
	Params params{ frame_, parity_, 0.0f, static_cast<float>(WIDTH * HEIGHT), 0, 0, WIDTH, HEIGHT, -1, stamp_, ray_points_, 0.0f, {}, {}, {}, rest ? 1 : 0, room_level_, 0, 0 };
	std::copy(now, now + 4, params.view_now);
	std::copy(from, from + 4, params.view_from);
	std::copy(room, room + 4, params.room);
	godot::PackedByteArray push;
	push.resize(sizeof params);
	std::memcpy(push.ptrw(), &params, sizeof params);
	const int64_t list = rd_->compute_list_begin();
	rd_->compute_list_bind_compute_pipeline(list, pipeline_[RESAMPLE]);
	rd_->compute_list_bind_uniform_set(list, uniforms_[RESAMPLE], 0);
	rd_->compute_list_set_push_constant(list, push, static_cast<uint32_t>(push.size()));
	rd_->compute_list_dispatch(list, static_cast<uint32_t>((WIDTH + GROUP_X - 1) / GROUP_X), static_cast<uint32_t>((HEIGHT + GROUP_Y - 1) / GROUP_Y), 1);
	rd_->compute_list_end();
	if (rest) {
		// The first frame at the view reads its budget's scale from the last frame's asks, which were a still
		// picture's: as though every pixel asks a fresh start's points, so that frame's batches keep to the budget
		// too. (Each pixel whose history starts again still reads its estimate whole: where a screen fills the room's
		// part of the screen, that first frame takes 250 ms on the machine measured; docs/TDD_42_VIEWER.md, Open.)
		const uint32_t fresh = static_cast<uint32_t>(WIDTH * HEIGHT) * 64u;
		godot::PackedByteArray total;
		total.resize(4);
		std::memcpy(total.ptrw(), &fresh, 4);
		rd_->buffer_update(asked_, static_cast<uint32_t>(4 * parity_), 4, total);
	}
}

} // namespace chem_godot
