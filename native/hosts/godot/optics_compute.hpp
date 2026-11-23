// The optical pass on the GPU (GAME_SPEC §12.2): the per-pixel source
// (native/game/shader/optics_pixel.glsl, copied to res://shaders) wrapped as a
// compute shader and run over the whole screen every frame (and, compiled with
// the chain, over the rectangles of what is held or is a prism). Each pixel's state
// (its history, content key, fixed estimate, noise) lives in four float images
// on the GPU and is updated in place; the screen's total ask for points is
// summed on the GPU for the next frame's budget. Nothing is read back.
#pragma once

#include "chem_api.h"

#include <godot_cpp/classes/rendering_device.hpp>
#include <godot_cpp/classes/texture2drd.hpp>
#include <godot_cpp/variant/rid.hpp>
#include <godot_cpp/variant/string.hpp>

#include <vector>

namespace chem_godot {

class OpticsCompute {
public:
	static constexpr int WIDTH = 1920, HEIGHT = 1080;

	OpticsCompute() = default;
	~OpticsCompute();
	OpticsCompute(const OpticsCompute &) = delete;
	OpticsCompute &operator=(const OpticsCompute &) = delete;

	// Builds the pipeline from the per-pixel source; false (and why) where there is no
	// RenderingDevice (the headless and Compatibility renderers) or it does not compile.
	bool open(const godot::String &pixel_source, godot::String &why);
	bool ready() const { return rd_ != nullptr; }
	// The scene's buffers, when its revision has moved; its maps and its wall's table, when theirs have.
	void upload(const cg_optics &optics);
	// One frame over the screen, `budget` points a pixel at most (on the render thread). `again`: the
	// same frame run once more, for timing (no new total of asks, the same frame number).
	void run(double budget, bool again = false);
	// Every pixel's history starts again next frame (for timing the worst frame).
	void restart();
	// What the screen shows: an RGBA8 texture, sRGB-encoded, alpha 0 where no body is.
	godot::Ref<godot::Texture2DRD> shown() const { return shown_texture_; }

private:
	void release();
	godot::RID image(godot::RenderingDevice::DataFormat format);

	godot::RenderingDevice *rd_ = nullptr;
	// two passes of one source (GAME_SPEC §12.3): [0] over the whole screen, compiled without the chain; [1] the
	// chain, over the rectangles of the bodies whose pixels can need it
	godot::RID shader_[4], pipeline_[4], uniforms_[4]; // the screen's pass, the chain's, the beams' (Stage 29), and the corners' (Stage 33)
	godot::RID corners_; // the lattice of pixel corners: three vec4s a corner (Stage 33)
	// each pixel's kept beams (Stage 33): a record for every pixel of the rectangles, one rectangle after another;
	// where each begins, in records (-1: there was no room for it, and its beams are traced at every read)
	godot::RID kept_;
	int64_t kept_capacity_ = 0; // records
	std::vector<int32_t> kept_base_;
	int passes_ = 2;
	// each pass's groups of pixels, across and down: pixels of a group wait for the one with most to do
	int group_[4][2] = { { 2, 32 }, { 2, 32 }, { 2, 32 }, { 2, 32 } };
	std::vector<float> chain_; // those rectangles: left, top, right, bottom
	// a storage buffer and how many bytes it can hold: made anew, larger, when what it is given outgrows it
	struct Buffer {
		godot::RID rid;
		uint32_t capacity = 0;
	};
	void hold(Buffer &buffer, const float *data, int32_t floats, int64_t from = 0); // from: the first float that changed
	Buffer scene_, rows_, lines_, lights_, maps_, wall_, stacks_, shapes_;
	godot::RID asked_;
	godot::RID state_[4];
	godot::RID shown_;
	godot::Ref<godot::Texture2DRD> shown_texture_;
	uint64_t uploaded_ = 0, uploaded_maps_ = 0, uploaded_wall_ = 0, uploaded_stacks_ = 0, uploaded_shapes_ = 0;
	bool have_scene_ = false;
	int32_t frame_ = 0;
	int32_t stamp_ = 0; // counts the runs of the passes (a pixel is stepped once in each)
	float ray_points_ = static_cast<float>(CG_OPTICS_RAY_POINTS); // the rays' budget (LAB_RAY_POINTS: another, for measuring)
	int32_t parity_ = 0;
};

} // namespace chem_godot
