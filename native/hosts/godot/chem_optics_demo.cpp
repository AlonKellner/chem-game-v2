#include "chem_optics_demo.hpp"

#include "draw_list.hpp"

#include <godot_cpp/classes/display_server.hpp>
#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/classes/global_constants.hpp>
#include <godot_cpp/classes/input_event_mouse_button.hpp>
#include <godot_cpp/classes/input_event_mouse_motion.hpp>
#include <godot_cpp/classes/input_event_pan_gesture.hpp>
#include <godot_cpp/classes/rendering_server.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/callable_method_pointer.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

namespace chem_godot {

namespace {

constexpr double WHEEL_TURN = 0.0175; // a degree a notch

} // namespace

ChemOpticsDemo::~ChemOpticsDemo() {
	cg_demo_close(demo_);
}

void ChemOpticsDemo::set_demo(const godot::String &name) {
	name_ = name;
}

godot::String ChemOpticsDemo::get_demo() const {
	return name_;
}

bool ChemOpticsDemo::optics_running() const {
	return compute_.ready();
}

godot::String ChemOpticsDemo::optics_why() const {
	return why_;
}

void ChemOpticsDemo::_ready() {
	if (godot::Engine::get_singleton()->is_editor_hint()) {
		return;
	}
	demo_ = cg_demo_open(name_.utf8().get_data());
	if (demo_ == nullptr) {
		godot::UtilityFunctions::push_error("ChemOpticsDemo: ", godot::String::utf8(cg_last_error()));
		return;
	}
	optics_ = memnew(godot::TextureRect);
	optics_->set_size(godot::Vector2(1920.0f, 1080.0f));
	optics_->set_mouse_filter(godot::Control::MOUSE_FILTER_IGNORE);
	optics_->set_expand_mode(godot::TextureRect::EXPAND_IGNORE_SIZE);
	add_child(optics_);
	if (compute_.open(godot::FileAccess::get_file_as_string("res://shaders/optics_pixel.glslinc"), why_)) {
		optics_->set_texture(compute_.shown());
	} else if (godot::DisplayServer::get_singleton() == nullptr || godot::DisplayServer::get_singleton()->get_name() != "headless") {
		godot::UtilityFunctions::push_error("ChemOpticsDemo: ", why_);
	}
	overlay_ = memnew(godot::Node2D);
	add_child(overlay_);
	overlay_->connect("draw", godot::Callable(this, "_paint_over"));
	set_process(true);
	set_process_input(true);
}

void ChemOpticsDemo::_process(double) {
	if (demo_ == nullptr) {
		return;
	}
	if (compute_.ready()) {
		godot::RenderingServer::get_singleton()->call_on_render_thread(callable_mp(this, &ChemOpticsDemo::optics_frame));
	}
	queue_redraw();
	overlay_->queue_redraw();
}

void ChemOpticsDemo::optics_frame() {
	cg_optics optics;
	optics.size = sizeof optics;
	if (demo_ != nullptr && cg_demo_optics(demo_, &optics) == CG_OK) {
		compute_.upload(optics);
		compute_.run(16.0);
	}
}

void ChemOpticsDemo::_input(const godot::Ref<godot::InputEvent> &event) {
	if (demo_ == nullptr) {
		return;
	}
	const godot::Ref<godot::InputEventMouseButton> button = event;
	if (button.is_valid()) {
		if (button->get_button_index() == godot::MOUSE_BUTTON_WHEEL_UP || button->get_button_index() == godot::MOUSE_BUTTON_WHEEL_DOWN) {
			if (button->is_pressed()) {
				turn(button->get_button_index() == godot::MOUSE_BUTTON_WHEEL_UP ? WHEEL_TURN : -WHEEL_TURN);
			}
			return;
		}
		if (button->get_button_index() == godot::MOUSE_BUTTON_LEFT) {
			const godot::Vector2 at = get_global_transform_with_canvas().affine_inverse().xform(button->get_position());
			pointer(at.x, at.y, button->is_pressed());
		}
		return;
	}
	const godot::Ref<godot::InputEventPanGesture> pan = event;
	if (pan.is_valid()) {
		turn(-pan->get_delta().y * WHEEL_TURN);
		return;
	}
	const godot::Ref<godot::InputEventMouseMotion> motion = event;
	if (motion.is_valid()) {
		const godot::Vector2 at = get_global_transform_with_canvas().affine_inverse().xform(motion->get_position());
		pointer(at.x, at.y, down_);
	}
}

void ChemOpticsDemo::pointer(double x, double y, bool down) {
	if (demo_ != nullptr) {
		down_ = down;
		cg_demo_pointer(demo_, static_cast<float>(x), static_cast<float>(y), down ? 1 : 0);
	}
}

void ChemOpticsDemo::turn(double radians) {
	if (demo_ != nullptr) {
		cg_demo_turn(demo_, static_cast<float>(radians));
	}
}

void ChemOpticsDemo::_draw() {
	cg_draw_list list;
	list.size = sizeof list;
	if (demo_ != nullptr && cg_demo_draw(demo_, &list) == CG_OK) {
		paint_list(this, list, false);
	}
}

void ChemOpticsDemo::paint_over() {
	cg_draw_list list;
	list.size = sizeof list;
	if (demo_ != nullptr && overlay_ != nullptr && cg_demo_draw(demo_, &list) == CG_OK) {
		paint_list(overlay_, list, true);
	}
}

void ChemOpticsDemo::_bind_methods() {
	godot::ClassDB::bind_method(godot::D_METHOD("set_demo", "name"), &ChemOpticsDemo::set_demo);
	godot::ClassDB::bind_method(godot::D_METHOD("get_demo"), &ChemOpticsDemo::get_demo);
	ADD_PROPERTY(godot::PropertyInfo(godot::Variant::STRING, "demo"), "set_demo", "get_demo");
	godot::ClassDB::bind_method(godot::D_METHOD("optics_running"), &ChemOpticsDemo::optics_running);
	godot::ClassDB::bind_method(godot::D_METHOD("optics_why"), &ChemOpticsDemo::optics_why);
	godot::ClassDB::bind_method(godot::D_METHOD("pointer", "x", "y", "down"), &ChemOpticsDemo::pointer);
	godot::ClassDB::bind_method(godot::D_METHOD("turn", "radians"), &ChemOpticsDemo::turn);
	godot::ClassDB::bind_method(godot::D_METHOD("_paint_over"), &ChemOpticsDemo::paint_over);
	godot::ClassDB::bind_method(godot::D_METHOD("optics_frame"), &ChemOpticsDemo::optics_frame);
}

void register_demo() {
	GDREGISTER_CLASS(ChemOpticsDemo);
}

} // namespace chem_godot
