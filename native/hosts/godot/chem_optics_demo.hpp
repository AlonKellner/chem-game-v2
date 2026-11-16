// The Godot host of the optics demo (cg_demo_*): ChemOpticsDemo, a Node2D that
// forwards the mouse (press and drag; the wheel or two fingers turn), runs the
// optical pass over the screen (OpticsCompute) and paints the demo's draw list
// under and over it. No logic of its own.
#pragma once

#include "chem_api.h"
#include "optics_compute.hpp"

#include <godot_cpp/classes/input_event.hpp>
#include <godot_cpp/classes/node2d.hpp>
#include <godot_cpp/classes/texture_rect.hpp>
#include <godot_cpp/variant/string.hpp>

namespace chem_godot {

class ChemOpticsDemo : public godot::Node2D {
	GDCLASS(ChemOpticsDemo, godot::Node2D)

public:
	~ChemOpticsDemo() override;

	void set_demo(const godot::String &name);
	godot::String get_demo() const;
	bool optics_running() const;
	godot::String optics_why() const;
	// For scripted runs: what the mouse does, and a turn.
	void pointer(double x, double y, bool down);
	void turn(double radians);

	void _ready() override;
	void _process(double delta) override;
	void _input(const godot::Ref<godot::InputEvent> &event) override;
	void _draw() override;

protected:
	static void _bind_methods();

private:
	void paint_over();
	void optics_frame(); // on the render thread

	godot::String name_ = "prism";
	cg_demo *demo_ = nullptr;
	bool down_ = false;
	godot::TextureRect *optics_ = nullptr;
	godot::Node2D *overlay_ = nullptr;
	OpticsCompute compute_;
	godot::String why_;
};

void register_demo();

} // namespace chem_godot
