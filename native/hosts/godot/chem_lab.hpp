// The Godot host: ChemLab, the one node that hosts the game. The game itself,
// screen logic included, is behind chem_api.h: this node forwards the pointer,
// advances a frame, and paints: the draw list's first part itself, the optical
// pass (a compute shader over the whole screen, shown by a child), the list's
// second part over that (another child).
#pragma once

#include "chem_api.h"
#include "optics_compute.hpp"

#include <godot_cpp/classes/input_event.hpp>
#include <godot_cpp/classes/node2d.hpp>
#include <godot_cpp/classes/texture_rect.hpp>
#include <godot_cpp/variant/packed_byte_array.hpp>
#include <godot_cpp/variant/packed_int32_array.hpp>
#include <godot_cpp/variant/string.hpp>
#include <godot_cpp/variant/vector2.hpp>

namespace chem_godot {

class ChemLab : public godot::Node2D {
	GDCLASS(ChemLab, godot::Node2D)

public:
	~ChemLab() override;

	// the bench it opens (a "chem-bench/1" file)
	void set_bench_path(const godot::String &path);
	godot::String get_bench_path() const;
	// the tutorial over it (a "chem-tutorial/1" file); empty for none
	void set_tutorial_path(const godot::String &path);
	godot::String get_tutorial_path() const;
	// [state (-1 none, 0 running, 1 complete, 2 failed), step, steps]
	godot::PackedInt32Array tutorial() const;

	godot::String build_name() const;
	// the library's self-check (cg_debug_report): the message reaches push_error
	int debug_report(const godot::String &message);

	// For tests and scripted play: what the mouse and the frame do.
	void pointer(double x, double y, bool down);
	void frame(double seconds);
	// the centre of a named thing on the screen ("item:flask", "cell:3,1", ...), or (-1, -1)
	godot::Vector2 locate(const godot::String &what) const;
	godot::String report() const;
	godot::String message() const;
	// For looking at the optical pass alone, and at the screen without it.
	void set_flat_visible(bool on);
	void set_optics_visible(bool on);
	// A settled picture of the pass alone (1920 x 1080, RGBA8) against what each pixel should show:
	// [pixels a body covers, the largest difference on the eye metric (rounded up), pixels visibly
	// different (above 8), where the largest is: x, y].
	godot::PackedInt32Array optics_parity(const godot::PackedByteArray &rgba) const;
	// Whether the pass runs here (a GPU with a RenderingDevice), and why not.
	bool optics_running() const;
	godot::String optics_why() const;
	// The most points a pixel may spend in a frame, on average over the screen (GAME_SPEC §12.2):
	// the setting that trades accuracy for speed on weaker hardware.
	void set_optics_budget(double points);
	double get_optics_budget() const;
	// For timing: the pass run this many more times each frame; and every pixel's history started
	// again before each frame's pass (the worst frame: everything changed at once).
	void set_optics_extra(int passes);
	void set_optics_restarting(bool on);

	void _ready() override;
	void _process(double delta) override;
	void _input(const godot::Ref<godot::InputEvent> &event) override;
	void _draw() override;

protected:
	static void _bind_methods();

private:
	godot::String bench_path_ = "res://data/benches/lab.json";
	godot::String tutorial_path_ = "res://data/tutorials/demo.json";
	void paint(godot::CanvasItem *on, bool above);
	void paint_over(); // the overlay's draw signal
	void optics_frame(); // on the render thread

	cg_ui *ui_ = nullptr;
	bool down_ = false;
	godot::TextureRect *optics_ = nullptr; // what the optical pass shows
	godot::Node2D *overlay_ = nullptr; // what is painted over it
	OpticsCompute compute_;
	godot::String optics_why_;
	double optics_budget_ = 16.0;
	int optics_extra_ = 0;
	bool optics_restarting_ = false;
	bool flat_visible_ = true;
};

void register_host();
void unregister_host();

} // namespace chem_godot
