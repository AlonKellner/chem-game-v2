#pragma once

#include "lab/lab.hpp"

#include <cstdint>
#include <string>
#include <vector>

// What the renderer is given (GAME_SPEC §12, §12.2): the bodies on the screen,
// what each holds as spectra at 1 nm (and narrow lines beside them), and each
// light's energy table, all computed from the live state. The GPU reads them
// as buffers; the same per-pixel source compiled as C++ reads them here, as
// the reference.
namespace lab {

namespace optics_layout {
inline constexpr int MAX_BODIES = 16;
// bodies, the backdrop on the bench and off it, lights; the bench's rectangle; the wall's distance behind
// the vessels (cm), the radiance of the room in front, whether anything refracts (1 or 0), whether bands are cut
// at the edges a bent view crosses (1 or 0)
inline constexpr int HEADER_VECS = 3;
inline constexpr int BODY_VECS = 7;
// the drawn shapes behind the bodies (GAME_SPEC §12.3): two vec4s each, its rectangle and its radiance
inline constexpr int MAX_SHAPES = 256;
inline constexpr int SHAPE_BASE = HEADER_VECS + MAX_BODIES * BODY_VECS;
inline constexpr int SIZE_VECS = SHAPE_BASE + 2 * MAX_SHAPES;
// within a body; REFRACT: its glass's thickness (cm), its first shape, how many, whether it refracts.
// A prism (FORM x = 2): MORE and GLOW x, y hold its three corners (cm from its centre, x across and z away from
// the eye), GLOW z its centre's offset from its rectangle's (pixels)
inline constexpr int RECT = 0, FORM = 1, MORE = 2, GLOW = 3, IDS = 4, REFRACT = 5;
// each body's spectra, ROW values at 1 nm from 380 nm: its liquid's optical depth per centimetre
// (natural), its solids' reflectance, its wall's optical depth at the reference thickness, its
// liquid's refractive index, its wall's
inline constexpr int ROW = 401;
inline constexpr int ROWS_PER_BODY = 5;
inline constexpr int LIQUID_ROW = 0, SOLID_ROW = 1, WALL_ROW = 2, LIQUID_INDEX_ROW = 3, WALL_INDEX_ROW = 4;
inline constexpr int ROWS_SIZE = MAX_BODIES * ROWS_PER_BODY * ROW;
// narrow lines of the liquids: centre (nm), full width at half maximum (nm), optical depth per centimetre at the peak, unused
inline constexpr int MAX_LINES = 256;
// the lights: the room's (the only one yet), 4,097 entries of (nm - 380, the colour up to it: X, Y, Z)
inline constexpr int LIGHT_ENTRIES = 4097;
inline constexpr int LIGHTS = 1;
} // namespace optics_layout

struct OpticsScene {
	std::vector<float> table; // SIZE_VECS * 4
	std::vector<float> rows; // ROWS_SIZE
	std::vector<float> lines; // MAX_LINES * 4
	std::vector<float> lights; // LIGHTS * LIGHT_ENTRIES * 4
	uint64_t revision = 0; // changes when any of them does
};

// One vessel or balloon as the screen places it.
struct OpticsBody {
	const ItemView *item = nullptr;
	float x = 0, y = 0, w = 0, h = 0; // its rectangle, in pixels
	// a prism (an item of kind "prism", the prism demo): an equilateral triangle of flint glass PRISM_SIDE_CM
	// on a side, w pixels on a side, turned by `turn` radians about its upright axis; its rectangle is centred
	// on (x + w / 2) and recomputed as its silhouette
	double turn = 0.0;
};
inline constexpr double PRISM_SIDE_CM = 5.0;

// A shape drawn on the wall behind the bodies: a rectangle of a flat grey.
struct OpticsShape {
	float x = 0, y = 0, w = 0, h = 0;
	double radiance = 0.0; // a share of the room's white
};

// The room: what is behind the bodies. The backdrop reflects evenly, so it is
// one number: its radiance as a share of the room's white; and what is drawn on it.
struct OpticsRoom {
	float x = 0, y = 0, w = 0, h = 0; // the bench's rectangle
	double bench = 0.73; // its radiance
	double elsewhere = 0.02;
	bool refraction = true; // GAME_SPEC §12.3: glass and liquids bend the light
	std::vector<OpticsShape> shapes; // in painting order
	double wall_cm = 20.0; // how far behind the vessels' axes the wall stands
	double front = 0.3; // the radiance of the room in front of the bench, which glass reflects
	bool cuts = true; // GAME_SPEC §12.2: a pixel's bands cut where its bent view crosses a drawn edge
};

// The room's white, for what glows: a white page in a lit room (GAME_SPEC §12).
inline constexpr double ROOM_WHITE_CD_M2 = 100.0;

// Fills the scene from the state (bodies in painting order; those past
// MAX_BODIES are left out). The revision moves only if the scene did.
void optics_compose(OpticsScene &scene, const OpticsRoom &room, const std::vector<OpticsBody> &bodies);

// ---- the reference: the per-pixel source compiled as C++, in float64

// The pixel (x, y) is the one whose centre is at (x + 0.5, y + 0.5).
// What reaches the eye at one wavelength, of the room's light (a factor: the
// backdrop's radiance times everything in the way); and how much a body covers.
double optics_through(const OpticsScene &scene, int x, int y, double nm);
double optics_cover(const OpticsScene &scene, int x, int y);
// Where the pixel's view meets the wall at a wavelength (GAME_SPEC §12.3: bent by the vessel it looks
// through, if any), on the screen; what the vessel's surfaces let through; the chord of liquid crossed (cm).
struct OpticsBend {
	double x = 0.0, pass = 1.0, chord = 0.0;
};
OpticsBend optics_bend(const OpticsScene &scene, int x, int y, double nm);
// The colour itself (every share of the light's energy its own band), linear
// sRGB, glow included: what the pixel should show.
void optics_truth(const OpticsScene &scene, int x, int y, double rgb[3]);
// That colour as the screen shows it: gamut-mapped, sRGB-encoded, in [0, 1];
// alpha 0 where no body is.
void optics_pixel(const OpticsScene &scene, int x, int y, double rgba[4]);
// A rectangle of the screen, 8 bits a channel, rows from the top, of the truth.
std::vector<uint8_t> optics_picture(const OpticsScene &scene, int x, int y, int w, int h);

// The renderer's own frames over a rectangle of the screen, exactly as the GPU
// runs them (GAME_SPEC §12.2): each pixel's history, its content key and fixed
// estimate, its points by need under a budget per pixel.
class OpticsFrames {
public:
	OpticsFrames(int x, int y, int w, int h, double budget = 16.0);
	~OpticsFrames();
	OpticsFrames(const OpticsFrames &) = delete;
	OpticsFrames &operator=(const OpticsFrames &) = delete;
	void frame(const OpticsScene &scene); // one frame, every pixel
	// what pixel (x, y) shows now (screen coordinates), as optics_pixel
	void shown(const OpticsScene &scene, int x, int y, double rgba[4]) const;
	// its colour before the display (linear sRGB, glow included)
	void colour(const OpticsScene &scene, int x, int y, double rgb[3]) const;
	double points() const; // the points spent in the last frame, all pixels
	double asked() const; // the points asked for in the last frame
	int frames() const;
	// whether pixel (x, y)'s history started again in the last frame
	bool restarted(int x, int y) const;
	// the points pixel (x, y) spent in the last frame
	double spent(int x, int y) const;

private:
	struct State;
	int x_, y_, w_, h_;
	double budget_;
	int frame_ = 0;
	double asked_ = 0.0, points_ = 0.0;
	std::vector<State> *states_;
	std::vector<uint8_t> restarted_;
};

// A picture of the whole screen from somewhere else (a GPU, settled), held to
// the truth where a body covers at least OPTICS_PARITY_COVER of the pixel (O4),
// on the eye metric: how many such pixels, the largest difference, how many
// are visibly different.
struct OpticsParity {
	int64_t covered = 0;
	double worst = 0.0;
	int64_t over = 0;
	int32_t worst_x = -1, worst_y = -1; // where the largest difference is
};
inline constexpr double OPTICS_PARITY_COVER = 0.02;
OpticsParity optics_compare(const OpticsScene &scene, const uint8_t *rgba, int w, int h);

// ---- invariants (GAME_SPEC §18): "" or the failure, starting with its id

// O1: nothing in the scene can make light: every depth at least 0, every reflectance within [0, 1], every line's width above 0.
std::string check_O1(const OpticsScene &scene);
// O2: at this pixel no wavelength comes out brighter than the brighter of the backdrop and the room's white.
std::string check_O2(const OpticsScene &scene, int x, int y);

} // namespace lab
