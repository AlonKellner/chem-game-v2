// The Godot host: ChemLab, the one node that hosts the game. The game itself is
// behind chem_api.h. Stage 0: it names the build and routes the library's
// reports to Godot's error log.
#pragma once

#include <godot_cpp/classes/node2d.hpp>
#include <godot_cpp/variant/string.hpp>

namespace chem_godot {

class ChemLab : public godot::Node2D {
	GDCLASS(ChemLab, godot::Node2D)

public:
	godot::String build_name() const;
	// the library's self-check (cg_debug_report): the message reaches push_error
	int debug_report(const godot::String &message);
	void _draw() override;

protected:
	static void _bind_methods();
};

void register_host();
void unregister_host();

} // namespace chem_godot
