// The optical pass on the GPU (GAME_SPEC §12.2): the per-pixel source
// (native/game/shader/optics_pixel.glsl, copied to res://shaders) wrapped as a
// compute shader and run over the whole screen every frame (and, compiled with
// the reading by a pixel's own rays, over the rectangles of what bends). Each pixel's state
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
	// The scene's buffers, when its revision has moved; its wall's table and its shapes, when theirs have.
	void upload(const cg_optics &optics);
	// One frame over the screen, `budget` points a pixel at most (on the render thread). `again`: the
	// same frame run once more, for timing (no new total of asks, the same frame number).
	void run(double budget, bool again = false);
	// Every pixel's history starts again next frame (for timing the worst frame).
	void restart();
	// A view's change (Stage 42; GAME_SPEC §12.3): the pixels as they stand kept as the anchor (when the view starts
	// to move); and what each pixel shows carried from the anchor's, made at the view `from`, to the view `now` (each
	// (scale, x, y, 0)), tracing nothing; `rest`: its state too, as it starts again at the view now.
	void anchor();
	// what the room shows at 1x, kept (a view further out than the anchor's shows the rest of the room from it); and
	// the room's part of the anchor's screen (left, top, right, bottom): what the anchor holds of it
	void keep_whole();
	void resample(const float now[4], const float from[4], const float room[4], bool rest);
	// What the screen shows: an RGBA8 texture, sRGB-encoded, alpha 0 where no body is.
	godot::Ref<godot::Texture2DRD> shown() const { return shown_texture_; }

private:
	void release();
	godot::RID image(godot::RenderingDevice::DataFormat format);

	godot::RenderingDevice *rd_ = nullptr;
	// three passes of one source (GAME_SPEC §12.3): over the whole screen, compiled without the reading by a pixel's
	// own rays; the beams' (Stage 29), over the rectangles of the bodies whose pixels are read by it; and, before
	// that one, the corners' of those pixels (Stage 33)
	// (and, Stage 42, a view's change: what the pixels show and hold carried from one view to another)
	enum Pass : int { SCREEN = 0, BEAMS = 1, CORNERS = 2, RESAMPLE = 3, PASSES = 4 };
	struct Params {
		int32_t frame, parity;
		float budget, pixels;
		int32_t left, top, right, bottom;
		int32_t kept, stamp; // where the rectangle's kept beams begin (the beams' pass); which run of the passes this is
		float rays; // the rays' budget: points a frame
		int32_t pad;
		float view_now[4], view_from[4], room[4]; // (a view's change, Stage 42)
		int32_t mode, pad3, pad4, pad5;
	};
	godot::RID anchor_[4];
	godot::RID whole_; // what the room showed at 1x, when last seen whole (Stage 42)
	godot::RID shader_[PASSES], pipeline_[PASSES], uniforms_[PASSES];
	godot::RID corners_; // the lattice of pixel corners: three vec4s a corner (Stage 33)
	// each pixel's kept beams (Stage 33): a record for every pixel of the rectangles, one rectangle after another;
	// where each begins, in records (-1: there was no room for it, and its beams are traced at every read)
	godot::RID kept_;
	int64_t kept_capacity_ = 0; // records
	godot::RID relay_; // the sensor displays' pixels' records (Stage 46)
	int64_t relay_capacity_ = 0, relay_pool_ = -1;
	std::vector<int32_t> kept_base_;
	// a pass's groups of pixels, across and down: pixels of a group wait for the one with most to do
	static constexpr int GROUP_X = 2, GROUP_Y = 32;
	std::vector<float> rectangles_; // where pixels are read by their own rays: left, top, right, bottom
	// a storage buffer and how many bytes it can hold: made anew, larger, when what it is given outgrows it
	struct Buffer {
		godot::RID rid;
		uint32_t capacity = 0;
	};
	void hold(Buffer &buffer, const float *data, int32_t floats);
	Buffer scene_, rows_, lines_, lights_, wall_, shapes_, images_;
	godot::RID asked_;
	godot::RID state_[4];
	godot::RID shown_;
	godot::Ref<godot::Texture2DRD> shown_texture_;
	uint64_t uploaded_ = 0, uploaded_wall_ = 0, uploaded_shapes_ = 0, uploaded_images_ = 0;
	bool have_scene_ = false;
	int32_t frame_ = 0;
	int32_t stamp_ = 0; // counts the runs of the passes (a pixel is stepped once in each)
	float ray_points_ = static_cast<float>(CG_OPTICS_RAY_POINTS); // the rays' budget (LAB_RAY_POINTS: another, for measuring)
	int32_t parity_ = 0;
};

} // namespace chem_godot
