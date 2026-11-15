#include "chem_lab.hpp"

#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/classes/font.hpp>
#include <godot_cpp/classes/global_constants.hpp>
#include <godot_cpp/classes/input_event_mouse_button.hpp>
#include <godot_cpp/classes/input_event_mouse_motion.hpp>
#include <godot_cpp/classes/theme_db.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/color.hpp>
#include <godot_cpp/variant/packed_vector2_array.hpp>
#include <godot_cpp/variant/rect2.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

namespace chem_godot {

namespace {

void on_log(void *, const char *message) {
	godot::UtilityFunctions::push_error(godot::String::utf8(message));
}

godot::Color colour(uint32_t rgba) {
	return godot::Color(static_cast<float>((rgba >> 24) & 0xff) / 255.0f, static_cast<float>((rgba >> 16) & 0xff) / 255.0f, static_cast<float>((rgba >> 8) & 0xff) / 255.0f,
			static_cast<float>(rgba & 0xff) / 255.0f);
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
	set_process(true);
	set_process_input(true);
}

void ChemLab::_process(double delta) {
	if (ui_ != nullptr) {
		cg_ui_frame(ui_, delta);
		queue_redraw();
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
		cg_ui_pointer(ui_, at.x, at.y, down_ ? 1 : 0);
		return;
	}
	const godot::Ref<godot::InputEventMouseMotion> motion = event;
	if (motion.is_valid()) {
		const godot::Vector2 at = get_global_transform_with_canvas().affine_inverse().xform(motion->get_position());
		cg_ui_pointer(ui_, at.x, at.y, down_ ? 1 : 0);
	}
}

void ChemLab::pointer(double x, double y, bool down) {
	if (ui_ != nullptr) {
		down_ = down;
		cg_ui_pointer(ui_, static_cast<float>(x), static_cast<float>(y), down ? 1 : 0);
	}
}

void ChemLab::frame(double seconds) {
	if (ui_ != nullptr) {
		cg_ui_frame(ui_, seconds);
		queue_redraw();
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

void ChemLab::_draw() {
	if (ui_ == nullptr) {
		return;
	}
	cg_draw_list list;
	list.size = sizeof list;
	if (cg_ui_draw(ui_, &list) != CG_OK) {
		return;
	}
	const godot::Ref<godot::Font> font = godot::ThemeDB::get_singleton()->get_fallback_font();
	for (int32_t i = 0; i < list.command_count; ++i) {
		const cg_draw_command &c = list.commands[i];
		const godot::Color color = colour(c.color);
		switch (c.type) {
			case CG_DRAW_RECT:
				draw_rect(godot::Rect2(c.x, c.y, c.w, c.h), color, true);
				break;
			case CG_DRAW_OUTLINE:
				draw_rect(godot::Rect2(c.x, c.y, c.w, c.h), color, false, c.width);
				break;
			case CG_DRAW_LINE:
				draw_line(godot::Vector2(c.x, c.y), godot::Vector2(c.w, c.h), color, c.width);
				break;
			case CG_DRAW_POLYGON: {
				godot::PackedVector2Array points;
				points.resize(c.count);
				for (int32_t p = 0; p < c.count; ++p) {
					points.set(p, godot::Vector2(list.points[2 * (c.first + p)], list.points[2 * (c.first + p) + 1]));
				}
				draw_colored_polygon(points, color);
				break;
			}
			case CG_DRAW_CIRCLE:
				draw_circle(godot::Vector2(c.x, c.y), c.w, color);
				break;
			case CG_DRAW_TEXT:
				if (font.is_valid()) {
					draw_string(font, godot::Vector2(c.x, c.y), godot::String::utf8(list.text + c.first, c.count), godot::HORIZONTAL_ALIGNMENT_LEFT, -1.0f,
							static_cast<int>(c.h), color);
				}
				break;
			default:
				break;
		}
	}
}

void ChemLab::_bind_methods() {
	godot::ClassDB::bind_method(godot::D_METHOD("set_bench_path", "path"), &ChemLab::set_bench_path);
	godot::ClassDB::bind_method(godot::D_METHOD("get_bench_path"), &ChemLab::get_bench_path);
	ADD_PROPERTY(godot::PropertyInfo(godot::Variant::STRING, "bench_path", godot::PROPERTY_HINT_FILE, "*.json"), "set_bench_path", "get_bench_path");
	godot::ClassDB::bind_method(godot::D_METHOD("build_name"), &ChemLab::build_name);
	godot::ClassDB::bind_method(godot::D_METHOD("debug_report", "message"), &ChemLab::debug_report);
	godot::ClassDB::bind_method(godot::D_METHOD("pointer", "x", "y", "down"), &ChemLab::pointer);
	godot::ClassDB::bind_method(godot::D_METHOD("frame", "seconds"), &ChemLab::frame);
	godot::ClassDB::bind_method(godot::D_METHOD("locate", "what"), &ChemLab::locate);
	godot::ClassDB::bind_method(godot::D_METHOD("report"), &ChemLab::report);
	godot::ClassDB::bind_method(godot::D_METHOD("message"), &ChemLab::message);
}

void register_host() {
	cg_set_log(on_log, nullptr);
	GDREGISTER_CLASS(ChemLab);
}

void unregister_host() {
	cg_set_log(nullptr, nullptr); // no log sink pointing into an unloaded library
}

} // namespace chem_godot
