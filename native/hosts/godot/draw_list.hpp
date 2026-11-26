// Painting a draw list (cg_draw_list) on a canvas item: the part under the
// optical pass, or the part over it. Shared by the game (ChemLab) and the
// optics demo (ChemOpticsDemo).
#pragma once

#include "chem_api.h"

#include <godot_cpp/classes/canvas_item.hpp>
#include <godot_cpp/classes/image_texture.hpp>

#include <cstdint>
#include <unordered_map>

namespace chem_godot {

// (Stage 61) the pictures' textures a painter keeps, by their hash: its owner's (a node's), freed with it
using Textures = std::unordered_map<uint64_t, godot::Ref<godot::ImageTexture>>;

void paint_list(godot::CanvasItem *on, const cg_draw_list &list, bool above, Textures *textures = nullptr);

} // namespace chem_godot
