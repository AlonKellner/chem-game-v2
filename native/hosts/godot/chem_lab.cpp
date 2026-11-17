#include "chem_lab.hpp"

#include "chem_optics_demo.hpp"
#include "draw_list.hpp"

#include <godot_cpp/classes/display_server.hpp>
#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/classes/font.hpp>
#include <godot_cpp/classes/global_constants.hpp>
#include <godot_cpp/classes/input_event_key.hpp>
#include <godot_cpp/classes/input_event_mouse_button.hpp>
#include <godot_cpp/classes/input_event_mouse_motion.hpp>
#include <godot_cpp/classes/rendering_server.hpp>
#include <godot_cpp/classes/theme_db.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/callable_method_pointer.hpp>
#include <godot_cpp/variant/color.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/packed_float32_array.hpp>
#include <godot_cpp/variant/packed_vector2_array.hpp>
#include <godot_cpp/variant/rect2.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

namespace chem_godot {

namespace {

void on_log(void *, const char *message) {
	godot::UtilityFunctions::push_error(godot::String::utf8(message));
}

} // namespace

ChemLab::~ChemLab() {
	cg_ui_close(ui_);
}

void ChemLab::set_bench_path(const godot::String &path) {
	bench_path_ = path;
}

godot::String ChemLab::get_bench_path() const {
	return bench_path_;
}

void ChemLab::set_tutorial_path(const godot::String &path) {
	tutorial_path_ = path;
}

godot::String ChemLab::get_tutorial_path() const {
	return tutorial_path_;
}

godot::PackedInt32Array ChemLab::tutorial() const {
	godot::PackedInt32Array out;
	cg_tutorial t;
	t.size = sizeof t;
	if (ui_ != nullptr && cg_ui_tutorial(ui_, &t) == CG_OK) {
		out.push_back(t.state);
		out.push_back(t.step);
		out.push_back(t.steps);
	}
	return out;
}

void ChemLab::key(int code, int codepoint) {
	if (ui_ != nullptr) {
		cg_ui_key(ui_, code, static_cast<uint32_t>(codepoint));
	}
}

godot::Array ChemLab::timers() const {
	godot::Array out;
	cg_timers t;
	t.size = sizeof t;
	if (ui_ == nullptr || cg_ui_timers(ui_, &t) != CG_OK) {
		return out;
	}
	for (int32_t i = 0; i < t.count; ++i) {
		godot::Dictionary d;
		d["fired"] = t.timers[i].fired != 0;
		d["conditions"] = t.timers[i].conditions;
		d["limited"] = t.timers[i].limited != 0;
		d["left_s"] = t.timers[i].left_s;
		d["fired_by"] = godot::String::utf8(t.timers[i].fired_by);
		d["item"] = godot::String::utf8(t.timers[i].item);
		out.push_back(d);
	}
	return out;
}

bool ChemLab::paused() const {
	cg_timers t;
	t.size = sizeof t;
	return ui_ != nullptr && cg_ui_timers(ui_, &t) == CG_OK && t.paused != 0;
}

godot::String ChemLab::build_name() const {
	return godot::String::utf8(cg_build_name());
}

int ChemLab::debug_report(const godot::String &message) {
	return cg_debug_report(message.utf8().get_data());
}

void ChemLab::_ready() {
	if (godot::Engine::get_singleton()->is_editor_hint()) {
		return;
	}
	const godot::String bench = godot::FileAccess::get_file_as_string(bench_path_);
	if (bench.is_empty()) {
		godot::UtilityFunctions::push_error("ChemLab: cannot read the bench ", bench_path_);
		return;
	}
	ui_ = cg_ui_open(bench.utf8().get_data());
	if (ui_ == nullptr) {
		godot::UtilityFunctions::push_error("ChemLab: ", godot::String::utf8(cg_last_error()));
		return;
	}
	if (!tutorial_path_.is_empty()) {
		const godot::String tutorial = godot::FileAccess::get_file_as_string(tutorial_path_);
		if (tutorial.is_empty() || cg_ui_tutor(ui_, tutorial.utf8().get_data()) != CG_OK) {
			godot::UtilityFunctions::push_error("ChemLab: the tutorial ", tutorial_path_, ": ", tutorial.is_empty() ? godot::String("cannot be read") : godot::String::utf8(cg_last_error()));
		}
	}
	// the optical pass: a compute shader over the whole screen, shown under what is painted over it
	optics_ = memnew(godot::TextureRect);
	optics_->set_size(godot::Vector2(1920.0f, 1080.0f));
	optics_->set_mouse_filter(godot::Control::MOUSE_FILTER_IGNORE);
	optics_->set_expand_mode(godot::TextureRect::EXPAND_IGNORE_SIZE);
	add_child(optics_);
	if (compute_.open(godot::FileAccess::get_file_as_string("res://shaders/optics_pixel.glslinc"), optics_why_)) {
		optics_->set_texture(compute_.shown());
	} else if (godot::DisplayServer::get_singleton() == nullptr || godot::DisplayServer::get_singleton()->get_name() != "headless") {
		godot::UtilityFunctions::push_error("ChemLab: ", optics_why_);
	}
	overlay_ = memnew(godot::Node2D);
	add_child(overlay_);
	overlay_->connect("draw", godot::Callable(this, "_paint_over"));
	set_process(true);
	set_process_input(true);
}

void ChemLab::_process(double delta) {
	if (ui_ != nullptr) {
		cg_ui_frame(ui_, delta);
		if (compute_.ready()) {
			godot::RenderingServer::get_singleton()->call_on_render_thread(callable_mp(this, &ChemLab::optics_frame));
		}
		queue_redraw();
		overlay_->queue_redraw();
	}
}

void ChemLab::_input(const godot::Ref<godot::InputEvent> &event) {
	if (ui_ == nullptr) {
		return;
	}
	const godot::Ref<godot::InputEventMouseButton> button = event;
	if (button.is_valid() && button->get_button_index() == godot::MOUSE_BUTTON_LEFT) {
		down_ = button->is_pressed();
		const godot::Vector2 at = get_global_transform_with_canvas().affine_inverse().xform(button->get_position());
		cg_ui_pointer(ui_, at.x, at.y, state_of(button.ptr()));
		return;
	}
	// keys for the screen's focused field (GAME_SPEC §13.3)
	const godot::Ref<godot::InputEventKey> key = event;
	if (key.is_valid() && key->is_pressed()) {
		const godot::Key code = key->get_keycode();
		if (code == godot::KEY_BACKSPACE) {
			cg_ui_key(ui_, CG_KEY_BACKSPACE, 0);
		} else if (code == godot::KEY_ENTER || code == godot::KEY_KP_ENTER) {
			cg_ui_key(ui_, CG_KEY_ENTER, 0);
		} else if (code == godot::KEY_ESCAPE) {
			cg_ui_key(ui_, CG_KEY_ESCAPE, 0);
		} else if (key->get_unicode() > 0) {
			cg_ui_key(ui_, CG_KEY_TEXT, static_cast<uint32_t>(key->get_unicode()));
		}
		return;
	}
	const godot::Ref<godot::InputEventMouseMotion> motion = event;
	if (motion.is_valid()) {
		const godot::Vector2 at = get_global_transform_with_canvas().affine_inverse().xform(motion->get_position());
		cg_ui_pointer(ui_, at.x, at.y, state_of(motion.ptr()));
	}
}

// the button, and the keys held with it (§13.1)
int32_t ChemLab::state_of(const godot::InputEventWithModifiers *event) const {
	return (down_ ? CG_POINTER_DOWN : 0) | (event->is_shift_pressed() ? CG_POINTER_SHIFT : 0) | (event->is_ctrl_pressed() ? CG_POINTER_CTRL : 0) |
			(event->is_meta_pressed() ? CG_POINTER_META : 0);
}

void ChemLab::pointer(double x, double y, bool down, int mods) {
	if (ui_ != nullptr) {
		down_ = down;
		cg_ui_pointer(ui_, static_cast<float>(x), static_cast<float>(y), (down ? CG_POINTER_DOWN : 0) | mods);
	}
}

void ChemLab::frame(double seconds) {
	if (ui_ != nullptr) {
		cg_ui_frame(ui_, seconds);
		queue_redraw();
		overlay_->queue_redraw();
	}
}

godot::Vector2 ChemLab::locate(const godot::String &what) const {
	float x = -1.0f, y = -1.0f;
	if (ui_ == nullptr || cg_ui_locate(ui_, what.utf8().get_data(), &x, &y) == 0) {
		return godot::Vector2(-1.0f, -1.0f);
	}
	return godot::Vector2(x, y);
}

godot::String ChemLab::report() const {
	return ui_ != nullptr ? godot::String::utf8(cg_ui_report(ui_)) : godot::String();
}

godot::String ChemLab::message() const {
	return ui_ != nullptr ? godot::String::utf8(cg_ui_message(ui_)) : godot::String();
}

void ChemLab::set_flat_visible(bool on) {
	flat_visible_ = on;
	queue_redraw();
	if (overlay_ != nullptr) {
		overlay_->queue_redraw();
	}
}

void ChemLab::set_optics_visible(bool on) {
	if (optics_ != nullptr) {
		optics_->set_visible(on);
	}
}

godot::PackedInt32Array ChemLab::optics_parity(const godot::PackedByteArray &rgba) const {
	godot::PackedInt32Array out;
	cg_optics_parity parity;
	parity.size = sizeof parity;
	if (ui_ == nullptr || rgba.size() != 1920 * 1080 * 4 || cg_ui_optics_compare(ui_, rgba.ptr(), 1920, 1080, &parity) != CG_OK) {
		return out;
	}
	out.push_back(static_cast<int32_t>(parity.covered));
	out.push_back(parity.worst);
	out.push_back(static_cast<int32_t>(parity.over));
	out.push_back(parity.worst_x);
	out.push_back(parity.worst_y);
	return out;
}

bool ChemLab::optics_running() const {
	return compute_.ready();
}

godot::String ChemLab::optics_why() const {
	return optics_why_;
}

void ChemLab::set_optics_budget(double points) {
	optics_budget_ = points > 0.0 ? points : 0.0;
}

double ChemLab::get_optics_budget() const {
	return optics_budget_;
}

void ChemLab::set_optics_extra(int passes) {
	optics_extra_ = passes > 0 ? passes : 0;
}

void ChemLab::set_optics_restarting(bool on) {
	optics_restarting_ = on;
}

// one frame of the optical pass: the scene's buffers when they have changed, then every pixel
void ChemLab::optics_frame() {
	cg_optics optics;
	optics.size = sizeof optics;
	if (ui_ == nullptr || cg_ui_optics(ui_, &optics) != CG_OK) {
		return;
	}
	compute_.upload(optics);
	if (optics_restarting_) {
		compute_.restart();
	}
	compute_.run(optics_budget_);
	for (int i = 0; i < optics_extra_; ++i) {
		if (optics_restarting_) {
			compute_.restart();
		}
		compute_.run(optics_budget_, true);
	}
}

void ChemLab::_draw() {
	paint(this, false);
}

void ChemLab::paint_over() {
	paint(overlay_, true);
}

void ChemLab::paint(godot::CanvasItem *on, bool above) {
	if (ui_ == nullptr || on == nullptr || !flat_visible_) {
		return;
	}
	cg_draw_list list;
	list.size = sizeof list;
	if (cg_ui_draw(ui_, &list) == CG_OK) {
		paint_list(on, list, above);
	}
}

void ChemLab::_bind_methods() {
	godot::ClassDB::bind_method(godot::D_METHOD("set_bench_path", "path"), &ChemLab::set_bench_path);
	godot::ClassDB::bind_method(godot::D_METHOD("get_bench_path"), &ChemLab::get_bench_path);
	ADD_PROPERTY(godot::PropertyInfo(godot::Variant::STRING, "bench_path", godot::PROPERTY_HINT_FILE, "*.json"), "set_bench_path", "get_bench_path");
	godot::ClassDB::bind_method(godot::D_METHOD("set_tutorial_path", "path"), &ChemLab::set_tutorial_path);
	godot::ClassDB::bind_method(godot::D_METHOD("get_tutorial_path"), &ChemLab::get_tutorial_path);
	ADD_PROPERTY(godot::PropertyInfo(godot::Variant::STRING, "tutorial_path", godot::PROPERTY_HINT_FILE, "*.json"), "set_tutorial_path", "get_tutorial_path");
	godot::ClassDB::bind_method(godot::D_METHOD("tutorial"), &ChemLab::tutorial);
	godot::ClassDB::bind_method(godot::D_METHOD("key", "code", "codepoint"), &ChemLab::key);
	godot::ClassDB::bind_method(godot::D_METHOD("timers"), &ChemLab::timers);
	godot::ClassDB::bind_method(godot::D_METHOD("paused"), &ChemLab::paused);
	godot::ClassDB::bind_method(godot::D_METHOD("build_name"), &ChemLab::build_name);
	godot::ClassDB::bind_method(godot::D_METHOD("debug_report", "message"), &ChemLab::debug_report);
	godot::ClassDB::bind_method(godot::D_METHOD("pointer", "x", "y", "down", "mods"), &ChemLab::pointer, DEFVAL(0));
	godot::ClassDB::bind_method(godot::D_METHOD("frame", "seconds"), &ChemLab::frame);
	godot::ClassDB::bind_method(godot::D_METHOD("locate", "what"), &ChemLab::locate);
	godot::ClassDB::bind_method(godot::D_METHOD("report"), &ChemLab::report);
	godot::ClassDB::bind_method(godot::D_METHOD("message"), &ChemLab::message);
	godot::ClassDB::bind_method(godot::D_METHOD("set_flat_visible", "on"), &ChemLab::set_flat_visible);
	godot::ClassDB::bind_method(godot::D_METHOD("set_optics_visible", "on"), &ChemLab::set_optics_visible);
	godot::ClassDB::bind_method(godot::D_METHOD("optics_parity", "rgba"), &ChemLab::optics_parity);
	godot::ClassDB::bind_method(godot::D_METHOD("optics_running"), &ChemLab::optics_running);
	godot::ClassDB::bind_method(godot::D_METHOD("optics_why"), &ChemLab::optics_why);
	godot::ClassDB::bind_method(godot::D_METHOD("set_optics_budget", "points"), &ChemLab::set_optics_budget);
	godot::ClassDB::bind_method(godot::D_METHOD("get_optics_budget"), &ChemLab::get_optics_budget);
	ADD_PROPERTY(godot::PropertyInfo(godot::Variant::FLOAT, "optics_budget"), "set_optics_budget", "get_optics_budget");
	godot::ClassDB::bind_method(godot::D_METHOD("set_optics_extra", "passes"), &ChemLab::set_optics_extra);
	godot::ClassDB::bind_method(godot::D_METHOD("set_optics_restarting", "on"), &ChemLab::set_optics_restarting);
	godot::ClassDB::bind_method(godot::D_METHOD("optics_frame"), &ChemLab::optics_frame);
	godot::ClassDB::bind_method(godot::D_METHOD("_paint_over"), &ChemLab::paint_over);
}

void register_host() {
	cg_set_log(on_log, nullptr);
	GDREGISTER_CLASS(ChemLab);
	register_demo();
}

void unregister_host() {
	cg_set_log(nullptr, nullptr); // no log sink pointing into an unloaded library
}

} // namespace chem_godot
