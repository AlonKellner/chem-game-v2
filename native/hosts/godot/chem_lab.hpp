// The Godot host: ChemLab, the one node that hosts the game. The game itself,
// screen logic included, is behind chem_api.h: this node forwards the pointer,
// advances a frame, and paints: the draw list's first part itself, the optical
// pass by a shader over the whole screen (a child), the list's second part
// over that (another child).
#pragma once

#include "chem_api.h"

#include <godot_cpp/classes/input_event.hpp>
#include <godot_cpp/classes/color_rect.hpp>
#include <godot_cpp/classes/node2d.hpp>
#include <godot_cpp/classes/shader_material.hpp>
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
	// A picture of the pass alone (1920 x 1080, RGBA8) against the reference renderer:
	// [pixels a body covers, the largest difference in 8-bit steps, pixels over 2 steps, where the largest is: x, y].
	godot::PackedInt32Array optics_parity(const godot::PackedByteArray &rgba) const;

	void _ready() override;
	void _process(double delta) override;
	void _input(const godot::Ref<godot::InputEvent> &event) override;
	void _draw() override;

protected:
	static void _bind_methods();

private:
	godot::String bench_path_ = "res://data/benches/lab.json";
	void paint(godot::CanvasItem *on, bool above);
	void paint_over(); // the overlay's draw signal
	void upload();

	cg_ui *ui_ = nullptr;
	bool down_ = false;
	godot::ColorRect *optics_ = nullptr; // the optical pass
	godot::Node2D *overlay_ = nullptr; // what is painted over it
	godot::Ref<godot::ShaderMaterial> material_;
	uint64_t uploaded_ = 0; // the revision of the table the shader has
	bool flat_visible_ = true;
};

void register_host();
void unregister_host();

} // namespace chem_godot
