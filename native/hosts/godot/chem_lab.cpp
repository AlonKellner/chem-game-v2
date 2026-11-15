#include "chem_lab.hpp"

#include "chem_api.h"

#include <godot_cpp/classes/font.hpp>
#include <godot_cpp/classes/theme_db.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

namespace chem_godot {

namespace {

void on_log(void *, const char *message) {
	godot::UtilityFunctions::push_error(godot::String::utf8(message));
}

} // namespace

godot::String ChemLab::build_name() const {
	return godot::String::utf8(cg_build_name());
}

int ChemLab::debug_report(const godot::String &message) {
	return cg_debug_report(message.utf8().get_data());
}

void ChemLab::_draw() {
	const godot::Ref<godot::Font> font = godot::ThemeDB::get_singleton()->get_fallback_font();
	if (font.is_valid()) {
		draw_string(font, godot::Vector2(32, 48), build_name());
	}
}

void ChemLab::_bind_methods() {
	godot::ClassDB::bind_method(godot::D_METHOD("build_name"), &ChemLab::build_name);
	godot::ClassDB::bind_method(godot::D_METHOD("debug_report", "message"), &ChemLab::debug_report);
}

void register_host() {
	cg_set_log(on_log, nullptr);
	GDREGISTER_CLASS(ChemLab);
}

void unregister_host() {
	cg_set_log(nullptr, nullptr); // no log sink pointing into an unloaded library
}

} // namespace chem_godot
