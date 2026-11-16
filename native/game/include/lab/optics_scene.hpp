#pragma once

#include "lab/lab.hpp"

#include <cstdint>
#include <string>
#include <vector>

// What the renderer is given (GAME_SPEC §12): a table of the bodies on the
// screen and what their contents do to light, band by band, computed from the
// live state. The shader reads it as an array of vec4; the same shader source
// compiled as C++ reads it here, as the reference.
namespace lab {

namespace optics_layout {
inline constexpr int BANDS = 24; // chem::optics::GAME_BANDS
inline constexpr int BAND_VECS = BANDS / 4;
inline constexpr int MAX_BODIES = 16;
inline constexpr int HEADER_VECS = 2 + BANDS; // the scene's own two, each band's colour
inline constexpr int BODY_VECS = 4 + 3 * BAND_VECS;
inline constexpr int SIZE_VECS = HEADER_VECS + MAX_BODIES * BODY_VECS;
// within a body
inline constexpr int RECT = 0, FORM = 1, MORE = 2, GLOW = 3, LIQUID = 4, SOLID = LIQUID + BAND_VECS, WALL = SOLID + BAND_VECS;
} // namespace optics_layout

struct OpticsScene {
	std::vector<float> table; // SIZE_VECS * 4 floats
	uint64_t revision = 0; // changes when the table does
};

// One vessel or balloon as the screen places it.
struct OpticsBody {
	const ItemView *item = nullptr;
	float x = 0, y = 0, w = 0, h = 0; // its rectangle, in pixels
};

// The room: what is behind the bodies. The backdrop reflects evenly, so it is
// one number: its radiance as a share of the room's white.
struct OpticsRoom {
	float x = 0, y = 0, w = 0, h = 0; // the bench's rectangle
	double bench = 0.73; // its radiance
	double elsewhere = 0.02;
};

// The room's white, for what glows: a white page in a lit room (GAME_SPEC §12).
inline constexpr double ROOM_WHITE_CD_M2 = 100.0;

// Fills the table from the state (bodies in painting order; those past
// MAX_BODIES are left out). The revision moves only if the table did.
void optics_compose(OpticsScene &scene, const OpticsRoom &room, const std::vector<OpticsBody> &bodies);

// ---- the reference: the shader's own source, compiled as C++, in float64

struct PixelLight {
	double band[optics_layout::BANDS]; // radiance by band, the room's white = 1
	double glow[3]; // emitted, linear sRGB
	double cover; // how much of the pixel a body covers
};
// The light reaching the eye at the centre of pixel (x, y) is at (x + 0.5, y + 0.5).
PixelLight optics_transport(const OpticsScene &scene, double x, double y);
// sRGB-encoded red, green, blue in [0, 1], and alpha (0 where no body is).
void optics_pixel(const OpticsScene &scene, double x, double y, double rgba[4]);
// A rectangle of the screen, 8 bits a channel, rows from the top.
std::vector<uint8_t> optics_picture(const OpticsScene &scene, int x, int y, int w, int h);

// ---- invariants (GAME_SPEC §18): "" or the failure, starting with its id

// O1: nothing in the table can make light: every depth is at least 0, every reflectance within [0, 1].
std::string check_O1(const OpticsScene &scene);
// O2: at this pixel no band is brighter than the brighter of the backdrop and the room's white.
std::string check_O2(const OpticsScene &scene, double x, double y);

} // namespace lab
