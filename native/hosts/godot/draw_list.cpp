#include "draw_list.hpp"

#include <godot_cpp/classes/font.hpp>
#include <godot_cpp/classes/global_constants.hpp>
#include <godot_cpp/classes/theme_db.hpp>
#include <godot_cpp/variant/color.hpp>
#include <godot_cpp/variant/packed_vector2_array.hpp>
#include <godot_cpp/variant/rect2.hpp>

namespace chem_godot {

namespace {

godot::Color colour(uint32_t rgba) {
	return godot::Color(static_cast<float>((rgba >> 24) & 0xff) / 255.0f, static_cast<float>((rgba >> 16) & 0xff) / 255.0f, static_cast<float>((rgba >> 8) & 0xff) / 255.0f,
			static_cast<float>(rgba & 0xff) / 255.0f);
}

} // namespace

void paint_list(godot::CanvasItem *on, const cg_draw_list &list, bool above) {
	const godot::Ref<godot::Font> font = godot::ThemeDB::get_singleton()->get_fallback_font();
	for (int32_t i = above ? list.above : 0; i < (above ? list.command_count : list.above); ++i) {
		const cg_draw_command &c = list.commands[i];
		const godot::Color color = colour(c.color);
		switch (c.type) {
			case CG_DRAW_RECT:
				on->draw_rect(godot::Rect2(c.x, c.y, c.w, c.h), color, true);
				break;
			case CG_DRAW_OUTLINE:
				on->draw_rect(godot::Rect2(c.x, c.y, c.w, c.h), color, false, c.width);
				break;
			case CG_DRAW_LINE:
				on->draw_line(godot::Vector2(c.x, c.y), godot::Vector2(c.w, c.h), color, c.width);
				break;
			case CG_DRAW_POLYGON: {
				godot::PackedVector2Array points;
				points.resize(c.count);
				for (int32_t p = 0; p < c.count; ++p) {
					points.set(p, godot::Vector2(list.points[2 * (c.first + p)], list.points[2 * (c.first + p) + 1]));
				}
				on->draw_colored_polygon(points, color);
				break;
			}
			case CG_DRAW_CIRCLE:
				on->draw_circle(godot::Vector2(c.x, c.y), c.w, color);
				break;
			case CG_DRAW_TEXT:
				if (font.is_valid()) {
					on->draw_string(font, godot::Vector2(c.x, c.y), godot::String::utf8(list.text + c.first, c.count), godot::HORIZONTAL_ALIGNMENT_LEFT, -1.0f,
							static_cast<int>(c.h), color);
				}
				break;
			default:
				break;
		}
	}
}

} // namespace chem_godot
