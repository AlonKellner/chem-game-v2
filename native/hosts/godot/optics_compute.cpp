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
#include <godot_cpp/classes/time.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

#include <algorithm>
#include <cstdlib>
#include <cstdio>
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
layout(push_constant, std430) uniform Params { int frame; int parity; float budget; float pixels; int left; int top; int right; int bottom; int kept; int stamp; float rays; int pad2; } params;
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
#ifdef OPTICS_CORNER_PASS
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
		if (imageLoad(key_image, at) != wall) {
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
		if (imageLoad(shown_image, at).w < 0.5) {
			s.hist = imageLoad(hist_image, at);
			s.prox = imageLoad(prox_image, at);
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
	for (const godot::RID &rid : { uniforms_[0], uniforms_[1], uniforms_[2], pipeline_[0], pipeline_[1], pipeline_[2], shader_[0], shader_[1], shader_[2], corners_, kept_, scene_.rid, rows_.rid, lines_.rid, lights_.rid, wall_.rid, shapes_.rid, asked_, state_[0], state_[1], state_[2], state_[3], shown_ }) {
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
	// (LAB_BEAM_GROUP=x,y: the beams' pass's groups, for measuring)
	if (const char *asked = std::getenv("LAB_BEAM_GROUP")) {
		int x = 0, y = 0;
		if (std::sscanf(asked, "%d,%d", &x, &y) == 2 && x > 0 && y > 0 && x * y <= 1024) {
			group_[BEAMS][0] = x;
			group_[BEAMS][1] = y;
		}
	}
	for (int pass = 0; pass < PASSES; ++pass) {
		godot::Ref<godot::RDShaderSource> source;
		source.instantiate();
		source->set_language(RenderingDevice::SHADER_LANGUAGE_GLSL);
		source->set_stage_source(RenderingDevice::SHADER_STAGE_COMPUTE,
				godot::String(WRAPPER_HEAD) + godot::String(TRACE_HEAD) + (std::getenv("LAB_BEAM_STUB") != nullptr ? godot::String("#define OPTICS_STUB_") + godot::String(std::getenv("LAB_BEAM_STUB")).replace(",", " 1\n#define OPTICS_STUB_") + " 1\n" : godot::String("")) + godot::String(std::getenv("LAB_BEAM_AT_ONCE") != nullptr ? "#define CORRECTIONS_AT_ONCE true\n" : "") + (std::getenv("LAB_BEAM_DEFINE") != nullptr ? godot::String("#define ") + godot::String(std::getenv("LAB_BEAM_DEFINE")) + "\n" : godot::String("")) + godot::String(pass == BEAMS ? "#define OPTICS_BEAM_PASS 1\n" : (pass == CORNERS ? "#define OPTICS_SCREEN_PASS 1\n#define OPTICS_CORNER_PASS 1\n" : "#define OPTICS_SCREEN_PASS 1\n")) + "#define OPTICS_GROUP_X " + godot::String::num_int64(group_[pass][0]) + "\n#define OPTICS_GROUP_Y " + godot::String::num_int64(group_[pass][1]) + "\n" + pixel_source + godot::String(WRAPPER_TAIL));
		// LAB_BEAM_TIMES: how long each stage of the shader's build takes (the GLSL to SPIR-V, the shader, the pipeline)
		const bool timed = std::getenv("LAB_BEAM_TIMES") != nullptr;
		const uint64_t t0 = godot::Time::get_singleton()->get_ticks_msec();
		const godot::Ref<godot::RDShaderSPIRV> spirv = rd->shader_compile_spirv_from_source(source);
		if (timed) {
			godot::UtilityFunctions::print("optics: pass ", pass, ": SPIR-V in ", godot::Time::get_singleton()->get_ticks_msec() - t0, " ms");
		}
		if (spirv.is_null() || !spirv->get_stage_compile_error(RenderingDevice::SHADER_STAGE_COMPUTE).is_empty()) {
			why = "the optical pass does not compile: " + (spirv.is_null() ? godot::String("no SPIR-V") : spirv->get_stage_compile_error(RenderingDevice::SHADER_STAGE_COMPUTE));
			release();
			return false;
		}
		shader_[pass] = rd_->shader_create_from_spirv(spirv, pass == BEAMS ? "chem optical pass: the beams" : (pass == CORNERS ? "chem optical pass: the corners" : "chem optical pass"));
		if (!shader_[pass].is_valid()) {
			why = "the optical pass's shader was not created";
			release();
			return false;
		}
		if (timed) {
			godot::UtilityFunctions::print("optics: pass ", pass, ": shader at ", godot::Time::get_singleton()->get_ticks_msec() - t0, " ms");
		}
		pipeline_[pass] = rd_->compute_pipeline_create(shader_[pass]);
		if (timed) {
			godot::UtilityFunctions::print("optics: pass ", pass, ": pipeline at ", godot::Time::get_singleton()->get_ticks_msec() - t0, " ms");
		}
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
	if (std::getenv("LAB_UPLOADS") != nullptr && bytes > 0) { // (a measurement: what is sent to the GPU, and when)
		std::printf("frame %d: buffer %d sends %u bytes\n", frame_, static_cast<int>(&buffer - &scene_), bytes);
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
	if (!asked_.is_valid()) {
		godot::PackedByteArray zero;
		zero.resize(24);
		zero.fill(0);
		asked_ = rd_->storage_buffer_create(24, zero);
	}
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
	if (!uniforms_[0].is_valid() || !uniforms_[1].is_valid() || !uniforms_[2].is_valid()) {
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
		add(RenderingDevice::UNIFORM_TYPE_STORAGE_BUFFER, 14, corners_);
		add(RenderingDevice::UNIFORM_TYPE_STORAGE_BUFFER, 15, kept_);
		for (int pass = 0; pass < PASSES; ++pass) {
			if (uniforms_[pass].is_valid()) {
				rd_->free_rid(uniforms_[pass]);
			}
			uniforms_[pass] = rd_->uniform_set_create(uniforms, shader_[pass], 0);
		}
	}
	have_scene_ = true;
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
	struct Params {
		int32_t frame, parity;
		float budget, pixels;
		int32_t left, top, right, bottom;
		int32_t kept, stamp; // where the rectangle's kept beams begin (the beams' pass); which run of the passes this is
		float rays; // the rays' budget: points a frame
		int32_t pad;
	};
	stamp_ = stamp_ % 16000000 + 1; // (a float holds it exactly; 0 is no run)
	if (std::getenv("LAB_UPLOADS") != nullptr) { // (a measurement: the rectangles the passes run over)
		std::printf("frame %d: rectangles", frame_);
		for (size_t i = 0; i + 3 < rectangles_.size(); i += 4) {
			std::printf(" %.0f,%.0f-%.0f,%.0f", rectangles_[i], rectangles_[i + 1], rectangles_[i + 2], rectangles_[i + 3]);
		}
		std::printf("\n");
	}
	const int64_t list = rd_->compute_list_begin();
	const auto pass_over = [&](int pass, int32_t left, int32_t top, int32_t right, int32_t bottom, int32_t kept = -1) {
		const Params params{ frame_, parity_, static_cast<float>(budget), static_cast<float>(WIDTH * HEIGHT), left, top, right, bottom, kept, stamp_, ray_points_, 0 };
		godot::PackedByteArray push;
		push.resize(sizeof params);
		std::memcpy(push.ptrw(), &params, sizeof params);
		rd_->compute_list_bind_compute_pipeline(list, pipeline_[pass]);
		rd_->compute_list_bind_uniform_set(list, uniforms_[pass], 0);
		rd_->compute_list_set_push_constant(list, push, static_cast<uint32_t>(push.size()));
		// groups two pixels wide and thirty-two tall: a vessel's pixels in one column are read in the same
		// number of parts, and pixels run together wait for the one with most to do (measured: 8 x 8, 1.72 ms
		// a pass where every history starts again; 2 x 32, 1.56)
		rd_->compute_list_dispatch(list, static_cast<uint32_t>((right - left + group_[pass][0] - 1) / group_[pass][0]), static_cast<uint32_t>((bottom - top + group_[pass][1] - 1) / group_[pass][1]), 1);
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
	rd_->compute_list_end();
	parity_ = 1 - parity_;
	++frame_;
}

} // namespace chem_godot
