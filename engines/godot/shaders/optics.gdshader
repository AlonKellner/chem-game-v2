// The optical pass (GAME_SPEC §12): one full-screen rectangle, 1920 x 1080 in
// the game's own pixels. Everything is in the include, which the lab layer
// also compiles as the C++ reference.
shader_type canvas_item;

#include "res://shaders/optics_pixel.gdshaderinc"

void fragment() {
	COLOR = shade(UV * vec2(1920.0, 1080.0));
}
