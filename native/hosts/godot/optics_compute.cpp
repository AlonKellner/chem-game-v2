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

#include <cstring>

namespace chem_godot {

namespace {

using godot::RenderingDevice;

// What the per-pixel source is given, and what it writes (lab/optics_scene.hpp lays the buffers out).
const char *const WRAPPER_HEAD = R"(#version 450
layout(local_size_x = 2, local_size_y = 32, local_size_z = 1) in;
layout(set = 0, binding = 0, std430) restrict readonly buffer SceneBuffer { vec4 v[]; } scene_buffer;
layout(set = 0, binding = 1, std430) restrict readonly buffer RowBuffer { float v[]; } row_buffer;
layout(set = 0, binding = 2, std430) restrict readonly buffer LineBuffer { vec4 v[]; } line_buffer;
layout(set = 0, binding = 3, std430) restrict readonly buffer LightBuffer { vec4 v[]; } light_buffer;
layout(set = 0, binding = 4, rgba32f) uniform restrict image2D hist_image;
layout(set = 0, binding = 5, rgba32f) uniform restrict image2D key_image;
layout(set = 0, binding = 6, rgba32f) uniform restrict image2D prox_image;
layout(set = 0, binding = 7, rgba32f) uniform restrict image2D aux_image;
layout(set = 0, binding = 8, rgba8) uniform restrict writeonly image2D shown_image;
layout(set = 0, binding = 9, std430) restrict buffer AskBuffer { uint total[2]; } ask_buffer;
layout(set = 0, binding = 10, std430) restrict readonly buffer MapBuffer { vec4 v[]; } map_buffer;
layout(set = 0, binding = 11, std430) restrict readonly buffer WallBuffer { vec4 v[]; } wall_buffer;
layout(push_constant, std430) uniform Params { int frame; int parity; float budget; float pixels; } params;
vec4 scene_vec(int i) { return scene_buffer.v[i]; }
float row_value(int i) { return row_buffer.v[i]; }
vec4 line_vec(int i) { return line_buffer.v[i]; }
vec4 light_vec(int i) { return light_buffer.v[i]; }
vec4 map_vec(int i) { return map_buffer.v[i]; }
vec4 wall_vec(int i) { return wall_buffer.v[i]; }
// ---- the per-pixel source (native/game/shader/optics_pixel.glsl)
)";

const char *const WRAPPER_TAIL = R"(
// ---- one invocation a pixel
void main() {
	ivec2 at = ivec2(gl_GlobalInvocationID.xy);
	if (at.x >= 1920 || at.y >= 1080) {
		return;
	}
	vec2 p = vec2(float(at.x) + 0.5, float(at.y) + 0.5);
	if (!near_any(p)) {
		imageStore(shown_image, at, vec4(0.0));
		return;
	}
	Path q = path_of(p, 1.0);
	if (q.cover <= 0.0) {
		imageStore(shown_image, at, vec4(0.0));
		return;
	}
	PixelState s;
	s.key = imageLoad(key_image, at);
	s.aux = imageLoad(aux_image, at);
	if (done(q, s.key, s.aux)) {
		return;
	}
	s.hist = imageLoad(hist_image, at);
	s.prox = imageLoad(prox_image, at);
	float scale = budget_scale(float(ask_buffer.total[params.parity]), params.budget, params.pixels);
	s = step_pixel(q, s, p, params.frame, scale);
	imageStore(hist_image, at, s.hist);
	imageStore(key_image, at, s.key);
	imageStore(prox_image, at, s.prox);
	imageStore(aux_image, at, s.aux);
	if (s.aux.z > 0.0) {
		atomicAdd(ask_buffer.total[1 - params.parity], uint(s.aux.z + 0.5));
	}
	imageStore(shown_image, at, shown(q, s));
}
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
	for (const godot::RID &rid : { uniforms_, pipeline_, shader_, scene_.rid, rows_.rid, lines_.rid, lights_.rid, maps_.rid, wall_.rid, asked_, state_[0], state_[1], state_[2], state_[3], shown_ }) {
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
	godot::Ref<godot::RDShaderSource> source;
	source.instantiate();
	source->set_language(RenderingDevice::SHADER_LANGUAGE_GLSL);
	source->set_stage_source(RenderingDevice::SHADER_STAGE_COMPUTE, godot::String(WRAPPER_HEAD) + pixel_source + godot::String(WRAPPER_TAIL));
	const godot::Ref<godot::RDShaderSPIRV> spirv = rd->shader_compile_spirv_from_source(source);
	if (spirv.is_null() || !spirv->get_stage_compile_error(RenderingDevice::SHADER_STAGE_COMPUTE).is_empty()) {
		why = "the optical pass does not compile: " + (spirv.is_null() ? godot::String("no SPIR-V") : spirv->get_stage_compile_error(RenderingDevice::SHADER_STAGE_COMPUTE));
		return false;
	}
	rd_ = rd;
	shader_ = rd_->shader_create_from_spirv(spirv, "chem optical pass");
	if (!shader_.is_valid()) {
		why = "the optical pass's shader was not created";
		release();
		return false;
	}
	pipeline_ = rd_->compute_pipeline_create(shader_);
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
	godot::PackedByteArray given;
	if (bytes <= buffer.capacity && buffer.rid.is_valid()) {
		if (bytes > 0) {
			given.resize(bytes);
			std::memcpy(given.ptrw(), data, bytes);
			rd_->buffer_update(buffer.rid, 0, bytes, given);
		}
		return;
	}
	// larger than it was: a new buffer, with room to grow, and the uniform set that names it made again
	if (uniforms_.is_valid()) {
		rd_->free_rid(uniforms_);
		uniforms_ = godot::RID();
	}
	if (buffer.rid.is_valid()) {
		rd_->free_rid(buffer.rid);
	}
	uint32_t capacity = 1024;
	while (capacity < bytes) {
		capacity *= 2;
	}
	given.resize(capacity);
	given.fill(0);
	if (bytes > 0) {
		std::memcpy(given.ptrw(), data, bytes);
	}
	buffer.rid = rd_->storage_buffer_create(capacity, given);
	buffer.capacity = capacity;
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
	if (!have_scene_ || optics.maps_revision != uploaded_maps_) {
		hold(maps_, optics.maps, 4 * optics.map_count);
		uploaded_maps_ = optics.maps_revision;
	}
	if (!have_scene_ || optics.wall_revision != uploaded_wall_) {
		hold(wall_, optics.wall, 4 * optics.wall_count);
		uploaded_wall_ = optics.wall_revision;
	}
	if (!asked_.is_valid()) {
		godot::PackedByteArray zero;
		zero.resize(8);
		zero.fill(0);
		asked_ = rd_->storage_buffer_create(8, zero);
	}
	if (!uniforms_.is_valid()) {
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
		add(RenderingDevice::UNIFORM_TYPE_STORAGE_BUFFER, 10, maps_.rid);
		add(RenderingDevice::UNIFORM_TYPE_STORAGE_BUFFER, 11, wall_.rid);
		uniforms_ = rd_->uniform_set_create(uniforms, shader_, 0);
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
	}
	struct {
		int32_t frame, parity;
		float budget, pixels;
	} params{ frame_, parity_, static_cast<float>(budget), static_cast<float>(WIDTH * HEIGHT) };
	godot::PackedByteArray push;
	push.resize(sizeof params);
	std::memcpy(push.ptrw(), &params, sizeof params);
	const int64_t list = rd_->compute_list_begin();
	rd_->compute_list_bind_compute_pipeline(list, pipeline_);
	rd_->compute_list_bind_uniform_set(list, uniforms_, 0);
	rd_->compute_list_set_push_constant(list, push, static_cast<uint32_t>(push.size()));
	// groups two pixels wide and thirty-two tall: a vessel's pixels in one column are read in the same number
	// of parts, and pixels run together wait for the one with most to do (measured: 8 x 8, 1.72 ms a pass
	// where every history starts again; 2 x 32, 1.56)
	rd_->compute_list_dispatch(list, (WIDTH + 1) / 2, (HEIGHT + 31) / 32, 1);
	rd_->compute_list_end();
	parity_ = 1 - parity_;
	++frame_;
}

} // namespace chem_godot
