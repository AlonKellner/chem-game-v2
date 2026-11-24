#pragma once

#include "lab/glassware.hpp"
#include "lab/lab.hpp"

#include <array>
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
// The shapes' head (Stage 29, 30): SHAPE_BLOCK vec4s a body: its shape's start (-1: it bends nothing), whether it
// is made as glassware, whether what lies in it is given, its turn (radians, clockwise as seen); the turn's
// pivot (pixels) and its cosine and sine; its rectangle upright; the room's up in its frame (x, y), whether it
// holds liquid, the liquid's level (cm along up); its vortex's depth, half width and half depth, whether it
// holds sediment; the vortex's middle (x, y), the sediment's level, its pile's height; the pile's half width
// and half depth, its middle (x, y).
inline constexpr int SHAPE_BLOCK = 10;
inline constexpr int MAX_BODIES = 256;
// bodies, the backdrop on the bench and off it, lights; the bench's rectangle; the wall's distance behind
// the bench's plane (pixels), the radiance of the room in front, whether anything refracts (1 or 0), whether
// bands are cut at the edges a bent view crosses (1 or 0); the gap between two layers' planes (pixels), the
// nearest layer any body is on, the room's side (0: none) and its cells' width (pixels; Stage 33); the room's middle
// on the screen (Stage 42: where the view puts it), 0, 0
inline constexpr int HEADER_VECS = 5;
inline constexpr int BODY_VECS = 8;
// The screen in squares (GAME_SPEC §12.3, Stage 22): each lists the bodies whose rectangles (and a pixel
// round them) reach into it, in painting order, the nearest the eye last: its count and up to GRID_SLOTS
// bodies, in GRID_VECS vec4s. A pixel looks at its square's bodies alone. Where more reach in, the nearest are
// kept (Stage 26: fifteen, not seven: two signal parts side by side, each a face, a lamp and ports, straddle
// one square of the screen, and the first of them were left out, half a part unseen).
inline constexpr int GRID_SQUARE = 40, GRID_COLS = 48, GRID_ROWS = 27, GRID_SLOTS = 15, GRID_VECS = 4;
inline constexpr int GRID_BASE = HEADER_VECS + MAX_BODIES * BODY_VECS;
inline constexpr int SIZE_VECS = GRID_BASE + GRID_COLS * GRID_ROWS * GRID_VECS;
// within a body; REFRACT: its glass's thickness (cm), its layer (0: the bench's; each one more a gap nearer
// the eye), its floor's thickness (cm: glass all through, which what it holds rests on), whether it refracts.
// Its vec4 6 (Stage 45; until Stage 35 where a body's refraction map lay): a vessel's boiling's bubbles, its steam
// bubbles' extinction a centimetre at its floor (times 1 - g: the share their scattering turns back), the height over its floor where they end (cm), the exponent their
// extinction falls by to there, (1 - z / end)^exponent, and a reaction's gas's extinction a centimetre, the same at
// every height (GAME_SPEC §11); 0 for any other body.
// A prism (FORM x = 2): MORE and GLOW x, y hold its three corners (cm from its centre, x across and z away from
// the eye), GLOW z its centre's offset from its rectangle's (pixels)
// SET: the set of spectra it is made of (a row set of `rows`), unused, a signature of the bodies of a layer
// behind it that stand in its rows (where they stand, what they are and hold; 0: none), whether it is set
// upside down.
// FORM x is its shape: 0 a cylinder (a vessel), 1 a ball (a balloon), 2 a prism; 3 a slab, 4 a bar, 5 a cone
// (Stage 22: FORM z its size, a slab's corners' radius or a bar's width; MORE a bar's two ends)
inline constexpr int RECT = 0, FORM = 1, MORE = 2, GLOW = 3, IDS = 4, REFRACT = 5, SET = 7;
// each set of spectra (Stage 22: what a body is made of, named by its SET; a vessel's is its own), ROW values
// at 1 nm from 380 nm: its liquid's optical depth per centimetre
// (natural), its solids' reflectance, its wall's optical depth at the reference thickness, its
// liquid's refractive index, its wall's, and what its solids glow with (Planck's law times their
// emissivity, as a multiple of the room's light at that wavelength: a light in the pass like the wall's), and what
// its bubbles turn back (Stage 45: their single-scattering albedo, data/optics/media.json's scatterers; 0 where it
// has none)
inline constexpr int ROW = 401;
inline constexpr int ROWS_PER_SET = 7;
inline constexpr int LIQUID_ROW = 0, SOLID_ROW = 1, WALL_ROW = 2, LIQUID_INDEX_ROW = 3, WALL_INDEX_ROW = 4, GLOW_ROW = 5, BUBBLE_ROW = 6;
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
//   [0]                          breakpoints m, profiles, the wall's tag (pixels that show it alone follow it), 0
//   [WALL_ROW_BASE + y]          the profile of pixel row y (x), for y from -1 to WALL_ROWS: the screen's rows,
//                                the row over them and the row under them (where a ray lands beyond the screen
//                                it reads the wall as it is just past the screen's edge, Stage 30)
//   [WALL_COLUMN_BASE + c]       the cell that x = c is in: how many breakpoints are at or left of it (x)
//   [WALL_BREAK_BASE + j]        breakpoint j (x), ascending
//   then, a profile after another, m + 1 cells each: cell j is from breakpoint j - 1 to breakpoint j
//                                (the first from the far left, the last to the far right):
//                                (its radiance, the profile's integral from breakpoint 0 to the cell's
//                                left end in two parts, coarse and fine: their sum, exactly; 0)
inline constexpr int WALL_ROWS = 1080, WALL_COLUMNS = 1920;
inline constexpr int WALL_ROW_BASE = 2, WALL_COLUMN_BASE = WALL_ROW_BASE + WALL_ROWS + 1, WALL_BREAK_BASE = WALL_COLUMN_BASE + WALL_COLUMNS;
// A shape's outline (Stage 33), after its patches: how far from its axis it reaches in each of this many bands
// of its height (its head says between which heights: z and w).
inline constexpr int OUTLINE_BANDS = 32;
// The rays' budget (Stage 33): the points a frame gives, over the whole screen, to the pixels read by their own
// rays, whose points are rays (a correction each, or one in four where a beam's rays go one way). Asks above it
// are scaled down as the screen's budget scales the others': a vessel that has just come to rest takes its first
// corrections over a few frames, none of them long.
inline constexpr double RAY_POINTS = 300000.0;
// A pixel's kept beams (Stage 33): the vec4s of one pixel's record (the per-pixel source says what they hold).
inline constexpr int KEPT_VECS = 24;
// The images things emit by (GAME_SPEC §12.1: image weights; Stage 42, the spectral viewer's screen), in vec4s
// of their own buffer, one image after another: its head (its pixels across and down, how much of its third of
// a pixel each subpixel fills across, how much of the pixel it fills down) and (where its channels' spectra
// begin, where its rows' sums begin, where its weights begin in `image_weights`, 0); its three channels' light,
// red, green and blue, ROW values each, four to a vec4 (a multiple of the room's light: the screen's primaries,
// scaled so its full white is the room's white); then for each of its rows, w + 1 running sums of its pixels'
// weights (linear, 0 to 1: red, green, blue, 0), the first 0. A thing that emits one is a slab whose set's glow
// is -1 and whose MORE is (where its image begins, 1, 0, 0).
inline constexpr int IMAGE_HEAD_VECS = 2;
inline constexpr double SCREEN_FILL_X = 0.8, SCREEN_FILL_Y = 0.9;
} // namespace optics_layout

struct OpticsScene {
	std::vector<float> table; // SIZE_VECS * 4
	std::vector<float> rows; // ROWS_PER_SET * ROW a set, as many sets as there are
	std::vector<float> lines; // MAX_LINES * 4
	std::vector<float> lights; // LIGHTS * LIGHT_SIZE * 4
	uint64_t revision = 0; // changes when any of them does
	// The wall's table (GAME_SPEC §12.3): large, and seldom changed, so it has its own revision.
	std::vector<float> wall; // vec4s: optics_layout's wall table
	uint64_t wall_revision = 0;
	uint64_t wall_key = 0; // what the wall's table was made from
	// what is drawn on the wall, 5 floats a shape (x, y, w, h, radiance), in painting order: the truth
	// reads the wall from these, not from the table
	std::vector<float> drawn;
	// what the wall's table has far off to the left and to the right (where rays that reach a face's black cells,
	// and its others, are put), and beyond the screen's edges (below 0: as it is at the edge): for the truth's own reading
	double far_left = 0.0, far_right = 0.0, wall_beyond = -1.0;
	// Where pixels are read by their own rays (GAME_SPEC §12.3, Stage 29): the rectangle of each body that bends,
	// and of each grating: 4 floats a rectangle (left, top, right, bottom, whole pixels). A host runs its passes
	// over the corners and the pixels of these, after its pass over the whole screen, and lays its pool of kept
	// beams out by them, one rectangle after another (Stage 33).
	std::vector<float> rectangles;
	std::vector<int> rectangle_bodies; // which body each rectangle is
	// each body's glassware, if it is made as one (GAME_SPEC §12.3, Stage 24): its whole shape, which the truth
	// traces (null: a cylinder, a prism, a ball, a thing of the lab)
	std::vector<const glass::Vessel *> vessels;
	// The shapes (GAME_SPEC §12.3, Stage 29): each bending body's surfaces, as the tracer's patches, for the
	// renderer that traces its rays live: vec4s, as native/game/shader/optics_pixel.glsl lays them out (where
	// each body's begins, then each shape once). They change with a body's kind, size or glass, not its place.
	std::vector<float> shapes;
	uint64_t shapes_revision = 0;
	// the same in double precision, for the reference: a ray that grazes a rim is thrown about by its surfaces,
	// and a surface's last digit in single precision moves where it lands by a pixel (what the GPU's picture may
	// differ by there is O4's to bound; the pass's tracer itself is held to the truth's ray by ray, O9)
	std::vector<double> shapes_exact;
	// How each body lies (Stage 30), 40 numbers a body, as the shapes' head lays them out: where it stands
	// upright, how it is turned and about where, and what lies in it (lab/contents.hpp), in its own frame.
	std::vector<double> lies;
	// The images things emit by (Stage 42; optics_layout's images), made again only when one changes; and each
	// image's weights as they are (w * h * 3 floats, linear, red, green and blue, row by row), for the truth's
	// own reading of them.
	std::vector<float> images;
	uint64_t images_revision = 0;
	uint64_t images_key = 0;
	std::vector<float> image_weights;
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
	// Turned about the eye's axis (Stage 30: a held pour's tilt): by `angle`, clockwise as seen, about the pivot (a
	// place of the screen); its rectangle is where it would stand upright. What it pours from: its right lip (1)
	// or its left (-1). And its liquid's vortex: its share (0 to 1) of the largest that fits.
	float angle = 0.0f, pivot_x = 0.0f, pivot_y = 0.0f;
	int side = 1;
	float vortex = 0.0f;
	bool flipped = false; // set upside down (Stage 27: a placed pour): its glass's rows from the other end
	// A thing of the lab that is no vessel (GAME_SPEC §12.3, Stage 22): with no item, a shape made of what it
	// reflects and what it emits, which bends nothing.
	//   Slab: its rectangle, the corners rounded by `size`; opaque, lit by the room's light.
	//   Bar: every place within `size` / 2 of the line from (x0, y0) to (x1, y1); opaque. Its rectangle is
	//        found from the line. With a `dash`, dashed along it: from its start, on for 0.6 of each `dash`
	//        pixels, then off (a sensing line, §13.3, Stage 26).
	//   Cone: a cone of glowing gas, its apex at its rectangle's top centre, its base its rectangle's bottom;
	//         it emits along the path through it (its light is a multiple of the room's for each pixel of
	//         path) and absorbs nothing.
	//   Screen: a slab that emits an image (Stage 42: the spectral viewer's): its `image`, `image_w` by `image_h`
	//           pixels as the screen is to show them (8-bit sRGB, row by row), shown over its rectangle; its face
	//           the screen's black glass. The screen drives its three primaries to show each pixel's colour
	//           (optics_screen_drive). It is made again only when `image_hash` changes.
	enum Shape : int { Item = 0, Slab = 3, Bar = 4, Cone = 5, Grating = 6, Screen = 7 };
	Shape shape = Item;
	float size = 0.0f;
	float dash = 0.0f; // a bar's dash and gap together (pixels); 0: unbroken
	float x0 = 0.0f, y0 = 0.0f, x1 = 0.0f, y1 = 0.0f;
	const char *surface = nullptr; // what it reflects: data/optics/media.json's surfaces (none: nothing)
	// what it emits: an emitter's light, or toward another's by `toward`, times `strength` (none: nothing)
	const char *light = nullptr;
	const char *light_to = nullptr;
	double toward = 0.0, strength = 1.0;
	uint64_t id = 0; // which thing it is: a pixel's history follows it
	const uint8_t *image = nullptr;
	int image_w = 0, image_h = 0;
	uint64_t image_hash = 0;
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
	// The wall beyond the screen, on every side: an endless plane of this radiance (Stage 33). A ray bent past the
	// screen's edge lands on it. Below 0 (a scene made by hand): the wall as it is at the screen's edge, as before.
	double beyond = -1.0;
	// The room (Stage 33): a cube `cube` pixels on a side about the screen's middle, its back face the wall. A
	// ray that leaves the last body it meets goes on to a face of it. Each face is ruled in cells `cube_cell`
	// wide, and the cells along the cube's twelve edges are of the radiance `edge` (black): a ray bent far, as at
	// a vessel's rim, comes to them. On the screen the wall is as it is drawn; elsewhere on a face it is
	// `beyond`. 0: no room (the wall's plane without end, and `front` for a ray that never reaches it).
	double cube = 0.0, cube_cell = 0.0, edge = 0.0;
	double cube_x = 960.0, cube_y = 540.0; // its middle, on the screen (Stage 42: the view moves it)
	// the layout's pixels on the screen (Stage 42: a view of the room): how wide one is, and where its x = 0 is. A
	// vessel made as glassware stands with its axis on a whole one of them.
	double pixel = 1.0, pixel_x0 = 0.0;
	bool cuts = true; // GAME_SPEC §12.2: a pixel's bands cut where its bent view crosses a drawn edge
};

// The room's white, for what glows: a white page in a lit room (GAME_SPEC §12).
inline constexpr double ROOM_WHITE_CD_M2 = 100.0;

// The spectral viewer's screen (Stage 42; GAME_SPEC §12.1): its three primaries' light at each nanometre (a
// multiple of the room's light: data/optics/media.json's led_red, led_green and led_blue, scaled so that every
// subpixel full, over a whole pixel with its gaps, is the room's white); and what it drives them to (linear
// weights, 0 to 1) to show a colour (8-bit sRGB): the colour's light, or where its primaries cannot make it, the
// nearest toward the grey of its luminance that they can (its lightness and hue kept, its chroma given up).
const std::array<std::array<double, 401>, 3> &optics_screen_primaries();
void optics_screen_drive(const uint8_t code[3], float weights[3]);

// Fills the scene from the state (bodies in painting order, a nearer layer's over a further one's; those
// past MAX_BODIES are left out). The revision moves only if the scene did.
void optics_compose(OpticsScene &scene, const OpticsRoom &room, const std::vector<OpticsBody> &bodies);
// The wall's table and what is drawn on it, from the room (optics_compose calls it; O7 makes a wall of
// its own with it).
void optics_wall(OpticsScene &scene, const OpticsRoom &room);
// The shapes, from the scene's own table (optics_compose calls it).
void optics_shapes(OpticsScene &scene);

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
// One ray's light at one wavelength through the point (x, y) of the screen, a factor of the room's light as
// optics_through (Stage 42: the spectrum probe's sample, GAME_SPEC §12): by the pass's own source, and by the
// truth's own tracer. And the content key of what a sample `size` across about the point looks through, as a
// pixel's history keys it (its bodies, their shapes and contents, the backdrop and the wall's tag).
double optics_point(const OpticsScene &scene, double x, double y, double nm);
double optics_point_truth(const OpticsScene &scene, double x, double y, double nm);
void optics_point_key(const OpticsScene &scene, double x, double y, double size, double key[4]);
// A screen's light (Stage 42; GAME_SPEC §12.1: image weights) over a sample `across` by `up` about (x, y), at a
// wavelength, as a multiple of the room's light over the whole sample (body: which body of the scene): as the pass
// reads it, from its rows' sums, and as the truth does, pixel by pixel, subpixel by subpixel.
double optics_image_glow(const OpticsScene &scene, int body, double x, double y, double across, double up, double nm);
double optics_image_glow_truth(const OpticsScene &scene, int body, double x, double y, double across, double up, double nm);
// O17 (GAME_SPEC §18, Stage 42): a screen emits what its picture says. Over `samples` samples of every screen of
// the scene, from a ray's point to many of its pixels, on it and over its edges, at three wavelengths, what the
// pass reads of it is what its pixels' subpixels give, to a float's precision (1e-4). "" when it holds.
std::string check_O17(const OpticsScene &scene, int samples);
// O18 (GAME_SPEC §18, Stage 42): the view is a similarity. The scene of a room seen at a view (`scale`, the
// layout's point (ox, oy) at the room's corner (rx, ry) of the screen) is the scene of it laid out, larger: at
// a point of every `step`th pixel of the layout's [x0, x1) x [y0, y1) (each 0.47 of the way into it), at three
// wavelengths, a ray through the view's place of a point brings what a ray through the point brings in the
// layout, but for the few that graze a surface; of the rays the view lands on its own screen (past it, its wall is
// the far wall's mean). "" when it holds.
std::string check_O18(const OpticsScene &whole, const OpticsScene &zoomed, double scale, double ox, double oy, double rx, double ry, double x0, double y0, double x1, double y1, double step);
// where the exact tracer lands the pixel's central ray, on the screen
double optics_exact_bend(const OpticsScene &scene, int x, int y, double nm);

// ---- a pixel's own rays (GAME_SPEC §12.3, Stage 29)

// One ray from the eye straight in at a place of the screen, at a wavelength: where it lands on the wall, and
// what it met that bent it, the nearest first (three at most): which body, what its surfaces passed, its path
// in its liquid (cm); whether it reaches the wall.
struct OpticsRay {
	double x = 0.0, y = 0.0;
	bool lands = true;
	int bent = 0;
	int body[3] = { -1, -1, -1 };
	double pass[3] = { 1.0, 1.0, 1.0 }, chord[3] = { 0.0, 0.0, 0.0 };
	uint32_t route[2] = { 0, 0 }; // the first two's own routes through their surfaces
	int ways = 1; // in how many ways it could go on from a grating it met
	bool solid = false; // it ended in a vessel's sediment (Stage 30)
};
// which body of the scene an item is (by its id, as its tag was made from it), or -1
int optics_body(const OpticsScene &scene, const std::string &id);
// by the pass's own tracer (native/game/shader/optics_pixel.glsl, in float64), and by the truth's
OpticsRay optics_ray(const OpticsScene &scene, double x, double y, double nm);
// the same in one of the ways a ray goes on from a grating (0: straight on; 1, 2: its orders +1 and -1; 3, 4: +3 and -3)
OpticsRay optics_ray_way(const OpticsScene &scene, double x, double y, double nm, int way);
OpticsRay optics_exact_ray(const OpticsScene &scene, double x, double y, double nm);
using OpticsRayOf = OpticsRay (*)(const OpticsScene &scene, double x, double y, double nm);
// O9 (GAME_SPEC §18, Stage 29): the pass's rays are the truth's. At the middle and the corner of every `step`th
// pixel of [x0, x1) x [y0, y1), at three wavelengths, the pass's tracer and the truth's land their ray at the
// same place and meet the same bodies with the same passing and the same chords; rays the two send different
// ways (a ray that grazes a surface) are few. "" when it holds.
std::string check_O9(const OpticsScene &scene, int x0, int y0, int x1, int y1, int step, OpticsRayOf ray = nullptr);
// The stretches of the spectrum a pixel is read in (places: 0 at 380 nm, 1 at 780 nm; a pair a stretch), as its
// reading cut and halved it; and as it would be if stretches were never halved (O10's own test).
std::vector<double> optics_stretches(const OpticsScene &scene, int x, int y);
std::vector<double> optics_stretches_unhalved(const OpticsScene &scene, int x, int y);
using OpticsStretches = std::vector<double> (*)(const OpticsScene &scene, int x, int y);
// O10 (Stage 29): a pixel's four nodes speak for each stretch of the spectrum it is read in. Where a pixel's
// middle ray goes one way at a stretch's four nodes, the cubic through them puts the ray at the wavelengths
// between where the ray traced there lands (to the sweep's tolerance), passing as much, through as much
// liquid; and few rays go another way between nodes that agree. "" when it holds.
std::string check_O10(const OpticsScene &scene, int x0, int y0, int x1, int y1, int step, OpticsStretches stretches = nullptr);
// How much of the pixel its beams made up, over the whole spectrum (1: all of it, once); 1 where the pixel is
// not read by beams.
double optics_made(const OpticsScene &scene, int x, int y);
// How many rays the pass's tracer follows to read the pixel once (0 where it is not read by beams).
int64_t optics_rays_for(const OpticsScene &scene, int x, int y);
// One ray through one body's shape by the pass's own tracer of surfaces (Stage 33), in the body's frame (cm from
// its axis, y up from its foot, z away from the eye; what lies in the body traced where it is given): in double,
// as the reference follows it, or (`single`) in single precision with the GPU's tolerances, as the GPU does.
struct OpticsPassage {
	double at[3] = { 0.0, 0.0, 0.0 }, toward[3] = { 0.0, 0.0, 1.0 }; // where it last was and which way it went on
	double pass = 1.0, chord = 0.0, first = 0.0; // what its surfaces passed; its path in the liquid; how far to the shape
	uint32_t route = 0;
	int meetings = 0;
	bool left = true, solid = false;
};
OpticsPassage optics_passage(const OpticsScene &scene, int body, const double from[3], const double heading[3], double n_glass, double n_core, bool single);
// O15, the pass's rays in single precision are its rays in double (GAME_SPEC §18, Stage 33): over every body of
// the scene that bends, rays across it straight in and aslant; fewer than one in two hundred go another way
// in single precision, and of the rest all but one in a hundred leave within a thousandth of a centimetre and
// of a heading of where they leave in double. (A ray that takes many meetings to leave is thrown wide by the
// last digit of its first: those are the hundredth.) `single`: the tracer held (null: the pass's own in single
// precision). "" when it holds.
using OpticsPassageOf = OpticsPassage (*)(const OpticsScene &scene, int body, const double from[3], const double heading[3], double n_glass, double n_core);
std::string check_O15(const OpticsScene &scene, OpticsPassageOf single = nullptr);
// What the pass's tracer has done on this thread (Stage 33), as counts: [0] rays followed; [1] bodies a ray
// tried; [2] meetings searched for; [3] quadric surfaces tried; [4] tori tried; [5] tori whose quartic was
// solved; [6] roots of those made exact; [7] passages through a shape that went on past eight meetings;
// [8] those that used every meeting allowed. The frame rate's measure where no GPU runs: what a ray costs is
// what it tries.
void optics_work(int64_t work[9], bool reset);
// Where a ray from p along u (the screen's pixels; z away from the eye, the bench's plane 0) comes to the scene's
// room (Stage 33): 0, the wall, off the cells along its edges (`landing`: where); 1, another cell of another face;
// 2, a cell along one of the room's twelve edges, which is black; -1, the scene has no room.
int optics_room_exit(const OpticsScene &scene, const double p[3], const double u[3], double landing[2]);
using OpticsMade = double (*)(const OpticsScene &scene, int x, int y);
// O11 (Stage 29): a pixel's parts make it whole. However a pixel is cut (in four, at where its rays part,
// along the spectrum), what it is read from adds up to all of it, once. "" when it holds.
std::string check_O11(const OpticsScene &scene, int x0, int y0, int x1, int y1, OpticsMade made = nullptr);
// A rectangle of the screen, 8 bits a channel, rows from the top, of the truth.
std::vector<uint8_t> optics_picture(const OpticsScene &scene, int x, int y, int w, int h);

struct OpticsFramesCore; // (src/optics_frames.hpp)
// The renderer's own frames over a rectangle of the screen, exactly as the GPU
// runs them (GAME_SPEC §12.2): each pixel's history, its content key and fixed
// estimate, its points by need under a budget per pixel.
class OpticsFrames {
public:
	// (`step`: every step-th pixel across and down, from (x, y): a sample of a large rectangle; w and h are
	// then how many across and down. `single` (Stage 35): the whole per-pixel source in single precision with
	// the GPU's own tolerances, as the GPU runs it, where no GPU runs: what a float's arithmetic makes of a
	// pixel, beside what the reference's float64 makes of it.)
	OpticsFrames(int x, int y, int w, int h, double budget = 16.0, int step = 1, bool single = false);
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
	double beams_traced() const { return beams_traced_; } // the pixels whose beams the last frame traced
	int frames() const;
	// whether pixel (x, y)'s history started again in the last frame
	bool restarted(int x, int y) const;
	// the points pixel (x, y) spent in the last frame
	double spent(int x, int y) const;
	// What the last frame cost (Stage 33; GAME_SPEC §18: O13): the pixels that read anything, the rays they
	// followed in all, and the most one pixel followed.
	int64_t read() const { return read_; }
	int64_t rays() const { return rays_; }
	int64_t most_rays() const { return most_rays_; }
	// A view's change (Stage 42; GAME_SPEC §12.3): every pixel's state carried from `anchor`'s, made at the view
	// `from`, to this one's at the view `now` (each (scale, x, y): a point l of the layout at l * scale + (x, y) on the
	// screen), as the GPU's pass carries it when the view comes to rest; `full_key`: each given the scene's whole
	// content key, its estimate not read again (O20's self-test).
	void resample(const OpticsFrames &anchor, const OpticsScene &scene, const double now[3], const double from[3], bool full_key = false);
	// Whether a pixel's beams are kept from frame to frame (Stage 33; as the GPU keeps them), or traced at every
	// read: the same picture, for holding one to the other.
	void keep(bool on) { keeping_ = on; }
	// Whether the corners of neighbouring pixels are shared (Stage 33; the lattice, traced before the pixels as the
	// GPU's pass over it), or each pixel traces its own: the same beams.
	void share(bool on) { sharing_ = on; }
	// O12's reading: whether pixel (x, y)'s kept beams are those of what it looks through now (its record is for
	// its shape key), and if so whether they are, value for value, the beams traced afresh. A pixel with none
	// kept, or kept for another key, has nothing to hold: fresh is true.
	bool kept_fresh(const OpticsScene &scene, int x, int y, bool &holds) const;

private:
	int x_, y_, w_, h_;
	int step_ = 1;
	bool keeping_ = true;
	bool sharing_ = true;
	OpticsFramesCore *core_ = nullptr; // its pixels and their histories, in the arithmetic asked for
	int64_t read_ = 0, rays_ = 0, most_rays_ = 0;
	double budget_;
	int frame_ = 0;
	double asked_ = 0.0, points_ = 0.0;
	double ray_asked_ = 0.0; // of asked_, by the pixels read by their own rays
	double beams_traced_ = 0.0; // the pixels whose beams the last frame traced (counted against the rays' budget)
	std::vector<uint8_t> restarted_;
};

// O12, a kept beam is the beam (GAME_SPEC §18, Stage 33): every pixel of the sample (every `step`-th from
// (x0, y0), the frames' own sample) whose kept beams are for what it looks through now holds, value for value,
// the beams traced afresh: no pixel is read from beams of something that has since moved. `held`: how many
// pixels had beams to hold (a check that held none checked nothing). "" when it holds.
std::string check_O12(const OpticsFrames &frames, const OpticsScene &scene, int x0, int y0, int x1, int y1, int step, int64_t *held = nullptr);
// O16, the pass in single precision is the pass (GAME_SPEC §18, Stage 35): the whole per-pixel source compiled
// with a float's arithmetic and the GPU's tolerances (`single`: OpticsFrames made so) comes, over the pixels of
// a sample that bodies cover, to the colours it comes to in double (`exact`), both stepped through the same
// frames: their mean under 1 apart on the eye metric, and fewer than one pixel in two hundred visibly apart.
// (A pixel whose rays are chaotic, at a vessel's floor near its axis, is another draw in each arithmetic, and
// about one in ten thousand of either stops visibly off the truth: docs/TDD_35_ONE.md.) `stats`: the pixels
// compared, those visibly apart, the worst and the mean. "" when it holds.
std::string check_O16(const OpticsFrames &exact, const OpticsFrames &single, const OpticsScene &scene, int x0, int y0, int x1, int y1, int step, double stats[4] = nullptr);

// O13, a frame's rays are bounded (GAME_SPEC §18, Stage 33): in the frame last given, no pixel followed more
// than `most` rays: its beam's middle, and a correction a band of the most bands a frame reads, each cut at
// the pixel's lines (a vessel's pixel; a prism's or a grating's reads its spectrum in stretches, each a beam).
// What a frame costs is the rays it follows: with this, and the pixels that read, a frame's cost is bounded
// where no GPU runs. "" when it holds.
inline constexpr int64_t OPTICS_RAYS_MOST = 96;
std::string check_O13(const OpticsFrames &frames, int64_t most = OPTICS_RAYS_MOST);

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
