#include "chem_lab.hpp"

#include "chem_optics_demo.hpp"
#include "draw_list.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>

#include <godot_cpp/classes/dir_access.hpp>
#include <godot_cpp/classes/display_server.hpp>
#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/classes/font.hpp>
#include <godot_cpp/classes/global_constants.hpp>
#include <godot_cpp/classes/input_event_key.hpp>
#include <godot_cpp/classes/input_event_mouse_button.hpp>
#include <godot_cpp/classes/input_event_mouse_motion.hpp>
#include <godot_cpp/classes/project_settings.hpp>
#include <godot_cpp/classes/rendering_server.hpp>
#include <godot_cpp/classes/theme_db.hpp>
#include <godot_cpp/classes/time.hpp>
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

godot::Array ChemLab::signals() const {
	godot::Array out;
	cg_signals sig;
	sig.size = sizeof sig;
	if (ui_ == nullptr || cg_ui_signals(ui_, &sig) != CG_OK) {
		return out;
	}
	for (int32_t i = 0; i < sig.count; ++i) {
		godot::Dictionary d;
		d["id"] = godot::String::utf8(sig.parts[i].id);
		d["kind"] = godot::String::utf8(sig.parts[i].kind);
		d["out"] = sig.parts[i].out != 0;
		d["ringing"] = sig.parts[i].ringing != 0;
		d["left_s"] = sig.parts[i].left_s;
		out.push_back(d);
	}
	return out;
}

bool ChemLab::paused() const {
	cg_signals sig;
	sig.size = sizeof sig;
	return ui_ != nullptr && cg_ui_signals(ui_, &sig) == CG_OK && sig.paused != 0;
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
		// a play's frames are timed from one _process to the next, each under the mark it was given
		const uint64_t now = godot::Time::get_singleton()->get_ticks_usec();
		if (!mark_.is_empty() && last_usec_ != 0) {
			Timed *t = timed_for(mark_);
			t->frame_ms.push_back(static_cast<double>(now - last_usec_) / 1000.0);
			t->ui_ms.push_back(ui_ms_);
			t->scene_ms.push_back(scene_ms_);
			t->pass_ms.push_back(pass_ms_);
			t->paint_ms.push_back(paint_ms_);
			paint_ms_ = 0.0;
		}
		last_usec_ = now;
		// (a play's frames are a 120 Hz display's, whatever this machine makes of them: the same play each time)
		cg_ui_frame(ui_, cg_ui_playing(ui_) != nullptr ? 1.0 / 120.0 : delta);
		ui_ms_ = static_cast<double>(godot::Time::get_singleton()->get_ticks_usec() - now) / 1000.0;
		const char *mark = cg_ui_playing(ui_);
		mark_ = mark != nullptr ? godot::String::utf8(mark) : godot::String();
		keep_export();
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
	if (button.is_valid() && (button->get_button_index() == godot::MOUSE_BUTTON_LEFT || button->get_button_index() == godot::MOUSE_BUTTON_RIGHT)) {
		down_ = button->is_pressed();
		const bool right = button->get_button_index() == godot::MOUSE_BUTTON_RIGHT; // the full menu (Stage 27)
		const godot::Vector2 at = get_global_transform_with_canvas().affine_inverse().xform(button->get_position());
		cg_ui_pointer(ui_, at.x, at.y, state_of(button.ptr()) | (right ? CG_POINTER_RIGHT : 0));
		return;
	}
	// keys for the screen's focused field (GAME_SPEC §13.3)
	const godot::Ref<godot::InputEventKey> key = event;
	if (key.is_valid() && key->is_pressed()) {
		const godot::Key code = key->get_keycode();
		if ((key->is_ctrl_pressed() || key->is_meta_pressed()) && code == godot::KEY_Z) {
			cg_ui_key(ui_, CG_KEY_UNDO, 0); // (Stage 34: what was last put in the trash comes back)
		} else if (code == godot::KEY_BACKSPACE || code == godot::KEY_DELETE) {
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

// the debug panel's export (§13.6): the lab's save, in user://saves/, named by the time
void ChemLab::keep_export() {
	const char *save = cg_ui_take_export(ui_);
	if (save == nullptr || save[0] == '\0') {
		return;
	}
	godot::DirAccess::make_dir_recursive_absolute("user://saves");
	const godot::String path = "user://saves/lab-" + godot::String::num_int64(static_cast<int64_t>(godot::Time::get_singleton()->get_unix_time_from_system() * 1000.0)) + ".json";
	const godot::Ref<godot::FileAccess> file = godot::FileAccess::open(path, godot::FileAccess::WRITE);
	if (file.is_null()) {
		godot::UtilityFunctions::push_error("ChemLab: the export could not be written to ", path);
		return;
	}
	file->store_string(godot::String::utf8(save));
	file->close();
	exported_ = path;
	godot::UtilityFunctions::print("ChemLab: the lab's save exported to ", godot::ProjectSettings::get_singleton()->globalize_path(path));
}

godot::String ChemLab::exported() const {
	return exported_;
}

void ChemLab::frame(double seconds) {
	if (ui_ != nullptr) {
		cg_ui_frame(ui_, seconds);
		keep_export();
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

ChemLab::Timed *ChemLab::timed_for(const godot::String &mark) {
	for (Timed &t : timed_) {
		if (t.mark == mark) {
			return &t;
		}
	}
	timed_.push_back(Timed{ mark, {}, {}, {}, {}, {} });
	return &timed_.back();
}

bool ChemLab::play(const godot::String &play_json) {
	timed_.clear();
	mark_ = godot::String();
	return ui_ != nullptr && cg_ui_play(ui_, play_json.utf8().get_data()) == CG_OK;
}

bool ChemLab::playing() const {
	return ui_ != nullptr && cg_ui_playing(ui_) != nullptr;
}

godot::Array ChemLab::played() const {
	godot::Array out;
	if (!compute_.ready() && godot::DisplayServer::get_singleton() != nullptr && godot::DisplayServer::get_singleton()->get_name() != "headless") {
		// (a window with no optical pass: its frames are not the pass's, and are not given as if they were)
		godot::UtilityFunctions::push_error("ChemLab: the optical pass is off, so the play's frames were not timed: ", optics_why_);
		return out;
	}
	const auto of = [](std::vector<double> v, bool worst) {
		if (v.empty()) {
			return -1.0;
		}
		std::sort(v.begin(), v.end());
		return worst ? v.back() : v[v.size() / 2];
	};
	for (const Timed &t : timed_) {
		if (std::getenv("LAB_PLAY_SERIES") != nullptr) { // every frame of the mark, in order (ms)
			std::printf("%s:", t.mark.utf8().get_data());
			for (size_t i = 0; i < t.frame_ms.size(); ++i) {
				std::printf("%s%.0f", i % 30 == 0 ? "\n  " : " ", t.frame_ms[i]);
			}
			std::printf("\n the game's frame, the scene composed, the pass sent, the list painted (CPU):");
			for (size_t i = 0; i < t.frame_ms.size(); ++i) {
				std::printf("%s%.0f/%.0f/%.0f/%.0f", i % 20 == 0 ? "\n  " : " ", t.ui_ms[i], t.scene_ms[i], t.pass_ms[i], t.paint_ms[i]);
			}
			std::printf("\n");
		}
		godot::Array row;
		row.push_back(t.mark);
		row.push_back(static_cast<int64_t>(t.frame_ms.size()));
		row.push_back(of(t.frame_ms, false));
		row.push_back(of(t.frame_ms, true));
		out.push_back(row);
	}
	return out;
}

// one frame of the optical pass: the scene's buffers when they have changed, then every pixel
void ChemLab::optics_frame() {
	cg_optics optics;
	optics.size = sizeof optics;
	const uint64_t began = godot::Time::get_singleton()->get_ticks_usec();
	if (ui_ == nullptr || cg_ui_optics(ui_, &optics) != CG_OK) {
		return;
	}
	const uint64_t composed = godot::Time::get_singleton()->get_ticks_usec();
	scene_ms_ = static_cast<double>(composed - began) / 1000.0;
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
	pass_ms_ = static_cast<double>(godot::Time::get_singleton()->get_ticks_usec() - composed) / 1000.0;
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
	const uint64_t began = godot::Time::get_singleton()->get_ticks_usec();
	cg_draw_list list;
	list.size = sizeof list;
	if (cg_ui_draw(ui_, &list) == CG_OK) {
		paint_list(on, list, above);
	}
	paint_ms_ += static_cast<double>(godot::Time::get_singleton()->get_ticks_usec() - began) / 1000.0;
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
	godot::ClassDB::bind_method(godot::D_METHOD("signals"), &ChemLab::signals);
	godot::ClassDB::bind_method(godot::D_METHOD("paused"), &ChemLab::paused);
	godot::ClassDB::bind_method(godot::D_METHOD("build_name"), &ChemLab::build_name);
	godot::ClassDB::bind_method(godot::D_METHOD("debug_report", "message"), &ChemLab::debug_report);
	godot::ClassDB::bind_method(godot::D_METHOD("pointer", "x", "y", "down", "mods"), &ChemLab::pointer, DEFVAL(0));
	godot::ClassDB::bind_method(godot::D_METHOD("frame", "seconds"), &ChemLab::frame);
	godot::ClassDB::bind_method(godot::D_METHOD("locate", "what"), &ChemLab::locate);
	godot::ClassDB::bind_method(godot::D_METHOD("report"), &ChemLab::report);
	godot::ClassDB::bind_method(godot::D_METHOD("message"), &ChemLab::message);
	godot::ClassDB::bind_method(godot::D_METHOD("exported"), &ChemLab::exported);
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
	godot::ClassDB::bind_method(godot::D_METHOD("play", "play_json"), &ChemLab::play);
	godot::ClassDB::bind_method(godot::D_METHOD("playing"), &ChemLab::playing);
	godot::ClassDB::bind_method(godot::D_METHOD("played"), &ChemLab::played);
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
