// Painting a draw list (cg_draw_list) on a canvas item: the part under the
// optical pass, or the part over it. Shared by the game (ChemLab) and the
// optics demo (ChemOpticsDemo).
#pragma once

#include "chem_api.h"

#include <godot_cpp/classes/canvas_item.hpp>

namespace chem_godot {

void paint_list(godot::CanvasItem *on, const cg_draw_list &list, bool above);

} // namespace chem_godot
