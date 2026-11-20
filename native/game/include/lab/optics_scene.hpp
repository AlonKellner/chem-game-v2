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
inline constexpr int SIZE_VECS = HEADER_VECS + MAX_BODIES * BODY_VECS;
// within a body; REFRACT: its glass's thickness (cm), unused, unused, whether it refracts. MAP: where its
// refraction map is in the scene's maps (vec4s), the whole pixels it spans, its first sample's place
// (pixels from its axis), its rows in wavelength (lab/optics_trace.hpp lays a map out).
// A prism (FORM x = 2): MORE and GLOW x, y hold its three corners (cm from its centre, x across and z away from
// the eye), GLOW z its centre's offset from its rectangle's (pixels)
inline constexpr int RECT = 0, FORM = 1, MORE = 2, GLOW = 3, IDS = 4, REFRACT = 5, MAP = 6;
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
// and after them the table the other way: the share of its energy up to each whole nm from 380 (x), ROW of them
inline constexpr int LIGHT_SIZE = LIGHT_ENTRIES + ROW;
inline constexpr int LIGHTS = 1;
// The wall as a table (GAME_SPEC §12.3), in vec4s. The edges of everything drawn on it are its
// breakpoints across; between two it is one radiance on any row of pixels (the shapes over a row's
// height averaged). Rows alike share a profile.
//   [0]                          breakpoints m, profiles, 0, 0
//   [WALL_ROW_BASE + y]          the profile of pixel row y (x)
//   [WALL_COLUMN_BASE + c]       the cell that x = c is in: how many breakpoints are at or left of it (x)
//   [WALL_BREAK_BASE + j]        breakpoint j (x), ascending
//   then, a profile after another, m + 1 cells each: cell j is from breakpoint j - 1 to breakpoint j
//                                (the first from the far left, the last to the far right):
//                                (its radiance, the profile's integral from breakpoint 0 to the cell's
//                                left end in two parts, coarse and fine: their sum, exactly; 0)
inline constexpr int WALL_ROWS = 1080, WALL_COLUMNS = 1920;
inline constexpr int WALL_ROW_BASE = 1, WALL_COLUMN_BASE = WALL_ROW_BASE + WALL_ROWS, WALL_BREAK_BASE = WALL_COLUMN_BASE + WALL_COLUMNS;
} // namespace optics_layout

struct OpticsScene {
	std::vector<float> table; // SIZE_VECS * 4
	std::vector<float> rows; // ROWS_SIZE
	std::vector<float> lines; // MAX_LINES * 4
	std::vector<float> lights; // LIGHTS * LIGHT_SIZE * 4
	uint64_t revision = 0; // changes when any of them does
	// The refraction maps and the wall's table (GAME_SPEC §12.3): large, and seldom changed, so each has
	// its own revision. A map is traced again only when its body's shape, glass or indices change.
	std::vector<float> maps; // vec4s: each refracting body's map (lab/optics_trace.hpp: map_block)
	std::vector<float> wall; // vec4s: optics_layout's wall table
	uint64_t maps_revision = 0, wall_revision = 0;
	std::vector<uint64_t> map_keys; // what each body's map was traced for
	std::vector<std::vector<float>> map_blocks;
	uint64_t wall_key = 0; // what the wall's table was made from
	// what is drawn on the wall, 5 floats a shape (x, y, w, h, radiance), in painting order: the truth
	// reads the wall from these, not from the table
	std::vector<float> drawn;
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
// The maps again, from the scene's own table and spectra: for a scene whose indices were changed after it
// was composed (a map is traced for its body's indices).
void optics_maps(OpticsScene &scene);
// The wall's table and what is drawn on it, from the room (optics_compose calls it; O7 makes a wall of
// its own with it).
void optics_wall(OpticsScene &scene, const OpticsRoom &room);

// ---- the reference: the per-pixel source compiled as C++, in float64

// The pixel (x, y) is the one whose centre is at (x + 0.5, y + 0.5).
// What reaches the eye at one wavelength, of the room's light (a factor: the
// backdrop's radiance times everything in the way); and how much a body covers.
double optics_through(const OpticsScene &scene, int x, int y, double nm);
// The same over a band of the light's energy, from the share u0 to u1, its point t of the way through
// (GAME_SPEC §12.2, §12.3): the wall read over the band's sweep, the liquids and the surfaces at its point.
// And the wavelength at a share of the light's energy.
double optics_band(const OpticsScene &scene, int x, int y, double u0, double u1, double t);
double optics_share_nm(const OpticsScene &scene, double u);
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
// The truth of a pixel's area (GAME_SPEC §12.3, Stage 20): the average of a grid of grid x grid
// sub-pixel rays, each bent by the reference's own exact tracer (vector Snell and Fresnel through
// whatever medium a point is in, turned back where it cannot pass), not the renderer's: what
// reaches the eye at one wavelength, and the colour (every band read; linear sRGB, glow included).
double optics_area_through(const OpticsScene &scene, int x, int y, double nm, int grid);
void optics_area_truth(const OpticsScene &scene, int x, int y, int grid, double rgb[3]);
// A renderer's light at a pixel and a wavelength, for the checks below (optics_through by default).
using OpticsThrough = double (*)(const OpticsScene &scene, int x, int y, double nm);
// O7 (GAME_SPEC §18, Stage 20): brightness is neither made nor lost by bending. The scene is made
// uniform (the wall, the room in front and off the bench all `radiance`, nothing drawn, nothing
// absorbing), and every pixel a body covers in [x0, x1) x [y0, y1) must then show exactly that
// radiance, at three wavelengths. "" when it holds.
std::string check_O7(const OpticsScene &scene, int x0, int y0, int x1, int y1, double radiance, OpticsThrough through = nullptr);
// O8 (GAME_SPEC §18, Stage 20): a thin line keeps its light. Summed over the pixels of [x0, x1) on a
// row, what a scene with the line shows beyond the same scene without it is what the truth's rays
// bring (traced exactly; the ends of the line's image found by halving, the light between them summed by
// Simpson's rule), wherever the line lands. "" when it holds.
std::string check_O8(const OpticsScene &with_line, const OpticsScene &without, int row, int x0, int x1, double nm, OpticsThrough through = nullptr);
// where the exact tracer lands the pixel's central ray, on the screen
double optics_exact_bend(const OpticsScene &scene, int x, int y, double nm);
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
