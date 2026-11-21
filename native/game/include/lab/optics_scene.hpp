#pragma once

#include "lab/glassware.hpp"
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
inline constexpr int MAX_BODIES = 256;
// bodies, the backdrop on the bench and off it, lights; the bench's rectangle; the wall's distance behind
// the bench's plane (pixels), the radiance of the room in front, whether anything refracts (1 or 0), whether
// bands are cut at the edges a bent view crosses (1 or 0); the gap between two layers' planes (pixels), the
// nearest layer any body is on, unused, unused
inline constexpr int HEADER_VECS = 4;
inline constexpr int BODY_VECS = 8;
// The screen in squares (GAME_SPEC §12.3, Stage 22): each lists the bodies whose rectangles (and a pixel
// round them) reach into it, in painting order, the nearest the eye last: its count and up to GRID_SLOTS
// bodies, in two vec4s. A pixel looks at its square's bodies alone. Where more reach in, the nearest are kept.
inline constexpr int GRID_SQUARE = 40, GRID_COLS = 48, GRID_ROWS = 27, GRID_SLOTS = 7;
inline constexpr int GRID_BASE = HEADER_VECS + MAX_BODIES * BODY_VECS;
inline constexpr int SIZE_VECS = GRID_BASE + GRID_COLS * GRID_ROWS * 2;
// within a body; REFRACT: its glass's thickness (cm), its layer (0: the bench's; each one more a gap nearer
// the eye), its floor's thickness (cm: glass all through, which what it holds rests on), whether it refracts. MAP: where its
// refraction map is in the scene's maps (vec4s), the whole pixels it spans, its first sample's place
// (pixels from its axis), its pieces in wavelength (lab/optics_trace.hpp lays a map out).
// A prism (FORM x = 2): MORE and GLOW x, y hold its three corners (cm from its centre, x across and z away from
// the eye), GLOW z its centre's offset from its rectangle's (pixels)
// SET: the set of spectra it is made of (a row set of `rows`), whether a body of a layer behind it stands in
// its rows (1 or 0), a signature of those (where they stand, what they are), unused.
// FORM x is its shape: 0 a cylinder (a vessel), 1 a ball (a balloon), 2 a prism; 3 a slab, 4 a bar, 5 a cone
// (Stage 22: FORM z its size, a slab's corners' radius or a bar's width; MORE a bar's two ends)
inline constexpr int RECT = 0, FORM = 1, MORE = 2, GLOW = 3, IDS = 4, REFRACT = 5, MAP = 6, SET = 7;
// each set of spectra (Stage 22: what a body is made of, named by its SET; a vessel's is its own), ROW values
// at 1 nm from 380 nm: its liquid's optical depth per centimetre
// (natural), its solids' reflectance, its wall's optical depth at the reference thickness, its
// liquid's refractive index, its wall's, and what its solids glow with (Planck's law times their
// emissivity, as a multiple of the room's light at that wavelength: a light in the pass like the wall's)
inline constexpr int ROW = 401;
inline constexpr int ROWS_PER_SET = 6;
inline constexpr int LIQUID_ROW = 0, SOLID_ROW = 1, WALL_ROW = 2, LIQUID_INDEX_ROW = 3, WALL_INDEX_ROW = 4, GLOW_ROW = 5;
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
	std::vector<float> rows; // ROWS_PER_SET * ROW a set, as many sets as there are
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
	// Where pixels may need more than one vessel's reading (GAME_SPEC §12.3, Stage 21): the rectangle of
	// each body that bends and is on a layer nearer than the bench's, or has a map of several pieces, or
	// stands across another of its own layer that bends: 4 floats a rectangle (left, top, right, bottom,
	// whole pixels). A host whose pass over the screen leaves those pixels alone runs its second pass over
	// these.
	std::vector<float> chain;
	std::vector<int> chain_bodies; // which body each rectangle is
	// The stacks (GAME_SPEC §12.3, Stage 21): for each of those bodies whose map is of one piece, the rays
	// that come in across it, followed through every body behind it (the chain) at the spectrum's four nodes,
	// in each band of rows in which every body is one thing: vec4s, as native/game/shader/optics_pixel.glsl
	// lays them out. Followed again only when a body of the stack moves or changes shape or index.
	std::vector<float> stacks;
	uint64_t stacks_revision = 0;
	std::vector<uint64_t> stack_keys; // what each body's stack was followed for
	std::vector<std::vector<float>> stack_blocks;
	int64_t stack_rays = 0; // followed in the last compose that followed any
	// each body's glassware, if it is made as one (GAME_SPEC §12.3, Stage 24): its whole shape, which the truth
	// traces (null: a cylinder, a prism, a ball, a thing of the lab)
	std::vector<const glass::Vessel *> vessels;
};

// One vessel or balloon as the screen places it.
struct OpticsBody {
	const ItemView *item = nullptr;
	float x = 0, y = 0, w = 0, h = 0; // its rectangle, in pixels
	// a prism (an item of kind "prism", the prism demo): an equilateral triangle of flint glass PRISM_SIDE_CM
	// on a side, w pixels on a side, turned by `turn` radians about its upright axis; its rectangle is centred
	// on (x + w / 2) and recomputed as its silhouette
	double turn = 0.0;
	// its layer (GAME_SPEC §12.3): 0 stands on the bench's plane; each one more is a gap nearer the eye (what
	// is held: 1). Up to OPTICS_LAYERS
	int layer = 0;
	// A thing of the lab that is no vessel (GAME_SPEC §12.3, Stage 22): with no item, a shape made of what it
	// reflects and what it emits, which bends nothing.
	//   Slab: its rectangle, the corners rounded by `size`; opaque, lit by the room's light.
	//   Bar: every place within `size` / 2 of the line from (x0, y0) to (x1, y1); opaque. Its rectangle is
	//        found from the line.
	//   Cone: a cone of glowing gas, its apex at its rectangle's top centre, its base its rectangle's bottom;
	//         it emits along the path through it (its light is a multiple of the room's for each pixel of
	//         path) and absorbs nothing.
	enum Shape : int { Item = 0, Slab = 3, Bar = 4, Cone = 5 };
	Shape shape = Item;
	float size = 0.0f;
	float x0 = 0.0f, y0 = 0.0f, x1 = 0.0f, y1 = 0.0f;
	const char *surface = nullptr; // what it reflects: data/optics/media.json's surfaces (none: nothing)
	// what it emits: an emitter's light, or toward another's by `toward`, times `strength` (none: nothing)
	const char *light = nullptr;
	const char *light_to = nullptr;
	double toward = 0.0, strength = 1.0;
	uint64_t id = 0; // which thing it is: a pixel's history follows it
};
inline constexpr int OPTICS_LAYERS = 6;
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
	// The screen is one world in pixels (GAME_SPEC §12.3): the wall stands this far behind the bench's plane
	// (20 cm at a 250 mL vessel's scale), and each layer's plane a gap nearer the eye than the one behind it
	double wall_px = 284.0;
	double gap_px = 120.0;
	double front = 0.3; // the radiance of the room in front of the bench, which glass reflects
	bool cuts = true; // GAME_SPEC §12.2: a pixel's bands cut where its bent view crosses a drawn edge
};

// The room's white, for what glows: a white page in a lit room (GAME_SPEC §12).
inline constexpr double ROOM_WHITE_CD_M2 = 100.0;

// The maps' store (GAME_SPEC §12.3, Stage 24): a directory where maps made before the game runs are kept, each
// under a key of everything it was traced for and of the builder's version; none until one is named. A map not
// found there is made, and written there if it can be.
void optics_store(const std::string &directory);
bool optics_store_read(uint64_t key, std::vector<float> &block);
void optics_store_write(uint64_t key, const std::vector<float> &block);

// Fills the scene from the state (bodies in painting order, a nearer layer's over a further one's; those
// past MAX_BODIES are left out). The revision moves only if the scene did.
void optics_compose(OpticsScene &scene, const OpticsRoom &room, const std::vector<OpticsBody> &bodies);
// The maps again, from the scene's own table and spectra: for a scene whose indices were changed after it
// was composed (a map is traced for its body's indices).
void optics_maps(OpticsScene &scene);
// The wall's table and what is drawn on it, from the room (optics_compose calls it; O7 makes a wall of
// its own with it).
void optics_wall(OpticsScene &scene, const OpticsRoom &room);
// The stacks, from the scene's own table, spectra and maps (optics_compose and optics_maps call it).
void optics_stacks(OpticsScene &scene);

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
// optics_through by the general reading alone (GAME_SPEC §12.3): a pixel through a map of one piece is
// drawn by a reading made for it, and the two must agree.
double optics_through_general(const OpticsScene &scene, int x, int y, double nm);
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
