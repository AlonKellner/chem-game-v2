// The prism demo (GAME_SPEC §12.3).
#include "lab/prism_demo.hpp"

#include "chem/optics.hpp"

#include <cmath>
#include <string>

namespace lab {

namespace {

constexpr double PI = 3.14159265358979323846;
constexpr uint32_t DARK_COLOUR = 0x070707ff, SLIT_COLOUR = 0xffffffff, EDGE = 0x5a6470ff, INK = 0xa8a8a8ff, DIM = 0x6c6c6cff, ACCENT = 0xd8d8d8ff;

void add(DrawList &list, DrawCommand c) {
	list.commands.push_back(c);
}

void rect_fill(DrawList &list, float x, float y, float w, float h, uint32_t colour) {
	DrawCommand c;
	c.type = DrawCommand::Rect;
	c.color = colour;
	c.x = x;
	c.y = y;
	c.w = w;
	c.h = h;
	add(list, c);
}

void line(DrawList &list, float x1, float y1, float x2, float y2, uint32_t colour, float width) {
	DrawCommand c;
	c.type = DrawCommand::Line;
	c.color = colour;
	c.width = width;
	c.x = x1;
	c.y = y1;
	c.w = x2;
	c.h = y2;
	add(list, c);
}

void text(DrawList &list, float x, float y, const std::string &s, uint32_t colour, float size) {
	DrawCommand c;
	c.type = DrawCommand::Text;
	c.color = colour;
	c.first = static_cast<int32_t>(list.text.size());
	c.count = static_cast<int32_t>(s.size());
	c.x = x;
	c.y = y;
	c.h = size;
	list.text += s;
	list.text.push_back('\0');
	add(list, c);
}

} // namespace

PrismDemo::PrismDemo() {
	prism_.id = "prism";
	prism_.kind = "prism";
	// at least deviation for yellow light: the face toward the eye turned by asin(n sin 30 degrees)
	const chem::optics::Spectrum *n = chem::optics::material_index("n_sf11");
	turn_ = std::asin((n != nullptr ? (*n)[209] : 1.785) * 0.5);
	// placed so that the middle of what it shows of the slit is yellow-green: at this turn the light passes two of
	// its faces through the quarter of its silhouette nearest the eye's side of the turn
	cx_ = 700.0f;
	compose();
	const float *rect = &scene_.table[static_cast<size_t>(optics_layout::HEADER_VECS + optics_layout::RECT) * 4];
	const int looking = static_cast<int>(rect[0] + 0.27f * rect[2]);
	const OpticsBend b = optics_bend(scene_, looking, static_cast<int>(cy_), 560.0);
	if (std::fabs(b.x) < 1e4) {
		cx_ += SLIT_X + SLIT_W / 2 - static_cast<float>(b.x);
	}
	compose();
}

void PrismDemo::pointer(float x, float y, bool down) {
	if (down && !dragging_) {
		const float *rect = &scene_.table[static_cast<size_t>(optics_layout::HEADER_VECS + optics_layout::RECT) * 4];
		if (x >= rect[0] && x < rect[0] + rect[2] && y >= rect[1] && y < rect[1] + rect[3]) {
			dragging_ = true;
			grab_x_ = x - cx_;
			grab_y_ = y - cy_;
		}
	} else if (down && dragging_) {
		cx_ = x - grab_x_;
		cy_ = y - grab_y_;
	} else {
		dragging_ = false;
	}
}

void PrismDemo::turn(double radians) {
	turn_ = std::remainder(turn_ + radians, 2.0 * PI);
}

void PrismDemo::compose() {
	OpticsRoom room;
	room.x = room.y = 0;
	room.w = 1920;
	room.h = 1080;
	room.bench = room.elsewhere = room.front = DARK;
	room.wall_cm = WALL_CM;
	room.shapes.push_back(OpticsShape{ SLIT_X, SLIT_Y, SLIT_W, SLIT_H, SLIT });
	OpticsBody body{ &prism_, cx_ - SIDE / 2, cy_ - HEIGHT / 2, SIDE, HEIGHT };
	body.turn = turn_;
	optics_compose(scene_, room, { body });
}

const OpticsScene &PrismDemo::optics() {
	compose();
	return scene_;
}

const DrawList &PrismDemo::draw() {
	compose();
	list_.commands.clear();
	list_.points.clear();
	list_.text.clear();
	// under the optical pass: the dark room and the slit
	rect_fill(list_, 0, 0, 1920, 1080, DARK_COLOUR);
	rect_fill(list_, SLIT_X, SLIT_Y, SLIT_W, SLIT_H, SLIT_COLOUR);
	list_.above = static_cast<int32_t>(list_.commands.size());
	// over it: the prism's silhouette, faintly
	const float *rect = &scene_.table[static_cast<size_t>(optics_layout::HEADER_VECS + optics_layout::RECT) * 4];
	DrawCommand outline;
	outline.type = DrawCommand::Outline;
	outline.color = EDGE;
	outline.width = 1;
	outline.x = rect[0];
	outline.y = rect[1];
	outline.w = rect[2];
	outline.h = rect[3];
	add(list_, outline);
	text(list_, 60, 70, "A prism of flint glass in a dark room, a slit of white light on the wall behind it", ACCENT, 24);
	text(list_, 60, 104, "Drag the prism to move it; turn it with the mouse wheel or two fingers. Each pixel sees the slit only", INK, 17);
	text(list_, 60, 128, "at the wavelengths the glass bends onto it: the colours settle over a few frames.", INK, 17);
	// seen from above: the prism, turned, and the slit's wall behind it
	const float ox = 160, oz = 900, scale = 22; // pixels a centimetre
	line(list_, ox - 120, oz - static_cast<float>(WALL_CM) * scale, ox + 120, oz - static_cast<float>(WALL_CM) * scale, DIM, 1);
	float px[3], pz[3];
	for (int k = 0; k < 3; ++k) {
		const double a = turn_ + PI / 2.0 + 2.0 * PI * k / 3.0, r = PRISM_SIDE_CM / std::sqrt(3.0);
		px[k] = ox + static_cast<float>(r * std::cos(a)) * scale;
		pz[k] = oz - static_cast<float>(r * std::sin(a)) * scale; // away from the eye is up
	}
	for (int k = 0; k < 3; ++k) {
		line(list_, px[k], pz[k], px[(k + 1) % 3], pz[(k + 1) % 3], ACCENT, 2);
	}
	line(list_, ox, oz + 110, ox, oz + 70, DIM, 1);
	text(list_, ox - 120, oz + 140, "seen from above: the wall behind, the eye below", DIM, 15);
	return list_;
}

} // namespace lab
