// The GDExtension's entry point (chem.gdextension: chem_library_init): the
// Godot host's classes (chem_lab.hpp). The game itself is behind chem_api.h.
#include "chem_lab.hpp"

#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/core/defs.hpp>
#include <godot_cpp/godot.hpp>

namespace {

void initialize(godot::ModuleInitializationLevel level) {
	if (level == godot::MODULE_INITIALIZATION_LEVEL_SCENE) {
		chem_godot::register_host();
	}
}

void uninitialize(godot::ModuleInitializationLevel level) {
	if (level == godot::MODULE_INITIALIZATION_LEVEL_SCENE) {
		chem_godot::unregister_host();
	}
}

} // namespace

extern "C" {
GDExtensionBool GDE_EXPORT chem_library_init(GDExtensionInterfaceGetProcAddress get_proc_address, GDExtensionClassLibraryPtr library,
		GDExtensionInitialization *initialization) {
	godot::GDExtensionBinding::InitObject init(get_proc_address, library, initialization);
	init.register_initializer(initialize);
	init.register_terminator(uninitialize);
	init.set_minimum_library_initialization_level(godot::MODULE_INITIALIZATION_LEVEL_SCENE);
	return init.init();
}
}
