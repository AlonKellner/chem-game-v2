#pragma once

#include <algorithm>

// The view of the room (GAME_SPEC §13.1, Stage 42), pure: a camera on the lit room, the screen's rectangle `room`
// (x, y, w, h). The room's own layout (where everything in it is, at 1x) is shown at `scale` from its point (x, y)
// at the room's top-left corner; the screen's other parts (its top bar, the readout's column, the panels over the
// room) are not in it. At 1x it is the layout itself.
namespace lab {

inline constexpr float VIEW_LEAST = 1.0f, VIEW_MOST = 16.0f;

struct ViewRoom {
	float x = 0.0f, y = 0.0f, w = 0.0f, h = 0.0f;
};

struct View {
	float scale = 1.0f;
	float x = 0.0f, y = 0.0f; // the layout's point at the room's top-left corner
	bool operator==(const View &o) const { return scale == o.scale && x == o.x && y == o.y; }
	bool operator!=(const View &o) const { return !(*this == o); }
};

// the view that shows the room as it is laid out
inline View view_whole(const ViewRoom &room) {
	return View{ 1.0f, room.x, room.y };
}
// a layout point on the screen, and a screen point in the layout
inline float screen_x(const View &v, const ViewRoom &room, float x) {
	return room.x + (x - v.x) * v.scale;
}
inline float screen_y(const View &v, const ViewRoom &room, float y) {
	return room.y + (y - v.y) * v.scale;
}
inline float layout_x(const View &v, const ViewRoom &room, float x) {
	return v.x + (x - room.x) / v.scale;
}
inline float layout_y(const View &v, const ViewRoom &room, float y) {
	return v.y + (y - room.y) / v.scale;
}
// kept within the room: no part of the screen's room shows what is beyond it
inline View view_within(View v, const ViewRoom &room) {
	v.scale = std::clamp(v.scale, VIEW_LEAST, VIEW_MOST);
	v.x = std::clamp(v.x, room.x, room.x + room.w * (1.0f - 1.0f / v.scale));
	v.y = std::clamp(v.y, room.y, room.y + room.h * (1.0f - 1.0f / v.scale));
	return v;
}
// zoomed by `factor` about the screen's point (sx, sy): what is under it stays under it (where the room allows)
inline View view_zoomed(const View &v, const ViewRoom &room, float sx, float sy, float factor) {
	const float lx = layout_x(v, room, sx), ly = layout_y(v, room, sy);
	View z = v;
	z.scale = std::clamp(v.scale * factor, VIEW_LEAST, VIEW_MOST);
	z.x = lx - (sx - room.x) / z.scale;
	z.y = ly - (sy - room.y) / z.scale;
	return view_within(z, room);
}
// moved by (dx, dy) of the screen's pixels, as a hand drags the room
inline View view_panned(const View &v, const ViewRoom &room, float dx, float dy) {
	View p = v;
	p.x -= dx / v.scale;
	p.y -= dy / v.scale;
	return view_within(p, room);
}

} // namespace lab
