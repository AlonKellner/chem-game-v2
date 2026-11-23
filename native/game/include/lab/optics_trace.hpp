#pragma once

#include "lab/glassware.hpp"

#include <array>
#include <cstdint>
#include <vector>

// The tracer and the refraction maps (GAME_SPEC §12.3, Stage 20). Rays are traced on the CPU,
// in float64, through bodies made of surface patches; never in a pixel's loop. What a body does to
// the view is traced once into a map, and the renderer reads the map.
namespace lab::trace {

// A body's media: 0 is the air outside it.
inline constexpr int MAX_MEDIA = 4;
inline constexpr int GLASS = 1, CORE = 2;

struct Vec3 {
	double x = 0.0, y = 0.0, z = 0.0;
};

// One surface of a body: where f(p) = a.x x^2 + a.y y^2 + a.z z^2 + l . p + c is zero, within its
// bounds: a plane (a = 0), an upright cylinder, a sphere, a cone. x is across, y up, z away from the
// eye, in centimetres from the body's centre.
// Or (Stage 24) a torus about the upright axis: the circle of radius `minor` about (major, cy) in the
// half-plane of a section, turned about the axis; f = (rho - major)^2 + (y - cy)^2 - minor^2, rho the
// distance from the axis. It has no quadratic: a ray meets it where the torus's quartic changes sign,
// found by stepping along the ray within the patch's bounds and halving. Its arc: the angles from t_lo to
// t_hi (radians, from +rho toward +y), within its bounds.
struct Patch {
	Vec3 a, l;
	double c = 0.0;
	Vec3 lo{ -1.0e300, -1.0e300, -1.0e300 }, hi{ 1.0e300, 1.0e300, 1.0e300 };
	double rho_lo = 0.0, rho_hi = 1.0e300; // bounds on the distance from the upright axis (Stage 24)
	bool torus = false;
	double major = 0.0, cy = 0.0, minor = 0.0, t_lo = 0.0, t_hi = 0.0;
	int below = 0, above = 0; // the medium where f < 0, and where f > 0
};

struct Shape {
	std::vector<Patch> patches;
};

// What lies in a body's core (GAME_SPEC §12.3, Stage 30; lab/contents.hpp makes it), in the body's frame: its
// liquid under a level of the room, less a vortex's cone hanging from that surface; its sediment under another
// level, and a pile's cone standing on it. `up` is the room's up; a cone's base is an ellipse about its middle
// (x, y), `across` along the level's line on the screen and `deep` into the depth. With this, a ray in the core
// is in the liquid (medium 2: CORE), in the air over it (medium 3) or ends in the sediment, by where it is; and
// it bends, or is turned back, at the liquid's surface and the vortex's.
struct Inside {
	double up_x = 0.0, up_y = 1.0;
	bool liquid = false;
	double liquid_level = 0.0;
	double vortex = 0.0, vortex_across = 0.0, vortex_deep = 0.0, vortex_x = 0.0, vortex_y = 0.0; // its depth; 0: none
	bool solid = false;
	double solid_level = 0.0;
	double pile = 0.0, pile_across = 0.0, pile_deep = 0.0, pile_x = 0.0, pile_y = 0.0; // its height; 0: none
};
inline constexpr int CORE_AIR = 3;

// What becomes of one ray.
struct Landing {
	bool lands = false; // it leaves the body going away from the eye, and reaches the wall
	double x = 0.0, y = 0.0; // where, on the wall's plane
	double pass = 1.0; // what its surfaces let through (Fresnel at each crossing)
	std::array<double, MAX_MEDIA> chord{}; // its path in each medium, cm
	uint32_t route = 0; // the surfaces it crossed and where it was turned back: the same for rays that go the same way
	int meetings = 0;
	// crossings of a surface with the same medium on both sides, not grazing: none in a shape whose
	// surfaces name their media consistently (a test of a shape, Stage 24)
	int mismatched = 0;
	// where it last was and which way it went on (cm, the body's frame): a ray carried on beyond (Stage 24);
	// whether it left the body (not still inside after every meeting allowed)
	Vec3 at, toward;
	bool left = true;
	bool solid = false; // it ended in the sediment (Stage 30): what it shows is the sediment, lit by the room
};

// One ray from `origin` along `direction` (a unit vector) through the shape, to the wall's plane
// z = wall; `index` is each medium's refractive index. At each surface it refracts by vector Snell,
// losing what Fresnel reflects; where it cannot pass it is turned back, all of it.
Landing trace(const Shape &shape, Vec3 origin, Vec3 direction, double wall, const std::array<double, MAX_MEDIA> &index, const Inside *inside = nullptr);
// The meetings a ray is allowed in one shape (Stage 33). Straight through a vessel is four, through its
// liquid's surface five, through a vortex and a floor more. Measured over the play (test_cost.cpp,
// LAB_RAY_STATS=most): 2% of passages take more than eight. On the GPU a group of pixels pays for its longest
// ray.
inline constexpr int MEETINGS_MOST = 16;
// How often a ray may be turned back inside one shape (total reflection). A ray through a vessel, its liquid's
// surface, a vortex and a floor crosses many surfaces and is turned back at none: those are followed (to
// MEETINGS_MOST). Light held in the glass of a rim or a fillet is turned back again and again: after
// TURNS_MOST it goes straight on from where it is, as if the rest of the body were not there, and so does a ray
// still inside after MEETINGS_MOST. (The user's call, 2026-11-23: with a ray simply ended after eight meetings
// and bringing nothing, a vortex in a shallow liquid showed grey patches where its rays were cut short.)
inline constexpr int TURNS_MOST = 4;
// (a measurement's switch: the meetings a ray is allowed, and the times it may be turned back)
void meetings_most(int most, int turns = TURNS_MOST);

// An upright cylinder of radius `outer` (cm) with a wall of `glass` (cm) around its core; with no
// glass, the core alone.
Shape cylinder(double outer, double glass);
// An upright prism: a triangle of glass seen from above, its corners (x, z).
Shape prism(const double corners[3][2]);
// A ball of glass (the tracer's rays bend up and down as well as across).
Shape ball(double radius);
// A vessel's glass, its profile turned about the upright axis (GAME_SPEC §12.3, Stage 24): y up from its
// base, cm. Its straight pieces are cones (cylinders, flat rings), its arcs tori (spheres where centred on
// the axis); the outside has air beyond it and glass within, the inside glass beyond and the core within.
Shape revolved(const glass::Vessel &vessel);
// The same, made once for each vessel and kept.
const Shape &revolved_of(const glass::Vessel &vessel);

// ---- a body's refraction map

// What a map is traced for: the body, how it is drawn, and its indices at 1 nm from 380 nm (401
// values each; null: 1).
inline constexpr int MAP_PIECES_MOST = 64;
struct MapSpec {
	Shape shape;
	double cm_per_pixel = 1.0;
	double left = 0.0, right = 0.0; // the span to cover, in pixels from the body's axis
	// where the screen's pixels lie: a pixel's edge is at `grid` from the axis, and at every whole pixel from
	// there. The map's samples are laid on that grid's sixteenths: a pixel's parts begin and end on samples.
	double grid = 0.0;
	double wall_cm = 20.0;
	int media = 2; // 2: its core as liquid, then as air; 3: and then as glass (a vessel's floor, seen from the side); 1: its core as air alone (a prism)
	const float *glass_index = nullptr, *liquid_index = nullptr;
	// Stage 24: the height the map's rays come in at (cm, the body's frame: up from a vessel's base), and the
	// fewest pieces in wavelength it may have (so that a body's rows share them)
	double y_cm = 0.0;
	int pieces_at_least = 1;
	// and the most it may have (a shaped vessel's bands: ROW_PIECES_MOST)
	int pieces_most = MAP_PIECES_MOST;
};

// The map (GAME_SPEC §12.3): samples across the body, MAP_FINE a pixel; pieces in wavelength, even in
// 1 / nm^2 from 380 to 780 nm (what glass and water disperse nearly straight in). For each core, sample and
// piece a cell: the ray at the piece's four nodes (its ends and its thirds): where it lands (pixels from the
// axis), what its surfaces pass, the square of its chord in the core (cm^2: straight across where the chord
// rises as a root, at a rim); and its route (a small whole number, the same for rays that go the same way).
// Within a piece a ray is the cubic through its four nodes: a vessel's whole spectrum is one piece, so one
// cell is a ray at every wavelength. A ray whose route changes within a piece is its nearest node's there. And for each whole pixel across, its level: into
// how many equal parts it must be read for the map to be straight within each (1, 2 or 4). The
// format is even, so that a pass on the GPU can build it too (a stack of bodies that moves: later).
inline constexpr int MAP_CELL = 16; // floats: four vec4s
struct Map {
	int media = 0, pieces = 0;
	double left = 0.0; // the first sample's place, pixels from the axis
	int coarse = 0; // whole pixels spanned: coarse * MAP_FINE + 1 samples
	std::vector<float> level; // coarse
	std::vector<float> bend; // coarse: how fast the landing bends in wavelength there, at most (pixels of the screen per nm^2)
	// media * samples * pieces * MAP_CELL, by core, sample, piece: the landing at the four nodes; what passes;
	// the chord squared; the route at each (with half of how far along the route changes before the next sample)
	std::vector<float> cells;
	// Stage 25: how far each ray lands below the row it came in on, on the screen (pixels), at the four nodes:
	// media * samples * pieces * 4, laid after the cells
	std::vector<float> drops;
	int64_t rays = 0; // traced to build it
	int samples() const;
	const float *cell(int medium, int piece, int sample) const {
		return &cells[((static_cast<size_t>(medium) * static_cast<size_t>(samples()) + static_cast<size_t>(sample)) * static_cast<size_t>(pieces) + static_cast<size_t>(piece)) * MAP_CELL];
	}
};

inline constexpr int MAP_FINE = 4; // samples a pixel: a pixel is read in four parts at most
// How far the map may put the image off, in pixels of the screen, within a part and within a piece: the
// landing's error on the wall over how fast the landing moves across the body there (at least 1).
inline constexpr double MAP_LANDING_TOLERANCE = 0.02;
// Or, within a piece, how far off in wavelength (nm): where a landing runs away with the wavelength.
inline constexpr double MAP_NM_TOLERANCE = 0.05;
inline constexpr double MAP_PASS_TOLERANCE = 0.005;
// How far apart the slopes of a part's two halves may be, as a share of the slope: a thin line's image is
// as bright as the slope where it lands is small, so this is twice the error of its light: at most 4% of
// it, half of what the eye sees of the brightest line there can be.
inline constexpr double MAP_SLOPE_TOLERANCE = 0.08;
// How far a band's sweep may stray from the true landing, in pixels of the screen, between two of its steps.
inline constexpr double MAP_SWEEP_TOLERANCE = 0.02;

// Where a wavelength is among a map's pieces: 0 at 380 nm, 1 at 780 nm, even in 1 / nm^2; and back.
inline constexpr double MAP_PLACE_SPAN = 1.0 - (380.0 / 780.0) * (380.0 / 780.0);
double map_place(double nm);
double map_nm(double place);

Map build_map(const MapSpec &spec);

// One ray of the map's body, traced directly: at `x` pixels from the axis, a wavelength, a core.
// What a cell holds, exactly: for the tests.
struct MapRay {
	double landing = 0.0, pass = 1.0, chord = 0.0;
	uint32_t route = 0;
	double drop = 0.0; // how far below its own row it lands, on the screen (pixels; Stage 25)
};
MapRay map_ray(const MapSpec &spec, double x, double nm, int medium);
// The same read from the map: straight between two samples across (the nearer one's where the route
// changes between them); in wavelength, the cubic through its piece's four nodes.
MapRay map_read(const Map &map, double x, double nm, int medium);

// ---- a shaped vessel's rows (GAME_SPEC §12.3, Stage 24, 25)

inline constexpr int ROW_PIECES_MOST = 1; // one piece, as every upright vessel's: read by the plain reading, followed in stacks
// One ray of a shaped vessel's map traced directly at `y` pixels down from its top (its foot the vessel's base).
MapRay rows_ray(const MapSpec &spec, double height, double x, double y, double nm, int medium);

// ---- a shaped vessel's knots: its rows read between two maps, bending up and down (GAME_SPEC §12.3, Stage 25)

// Knots down a shaped vessel's height, each a map traced at its own row. An upright band is one map for its
// whole height (its rays level). A curved band's knots are refined by halving where the middle row is not the
// line between its neighbours, read at the same place in the rims' measure (the deflection, the drop, what
// passes, the chord; the quarters too past a pixel; a route that changes split for), at most `most` knots.
// A row is read between the two knots around it, at the same place in that measure.
inline constexpr int ROW_KNOTS_MOST = 96;
// How far a row between two knots may put the image off, up or down or across, as a shift on the screen
// (pixels): far under what the eye sees, looser than a map across (whose fiftieth keeps a thin line's light)
inline constexpr double ROW_TOLERANCE = 0.1;
struct KnotMap {
	std::vector<double> knots; // pixels down from the body's top, ascending: its top and its foot among them
	std::vector<int> map_of; // which of the maps each knot is
	std::vector<char> level; // from knot k to k + 1 one map, every ray level
	std::vector<double> outer, inner; // the rims each knot's map was traced at, pixels
	std::vector<double> row_outer, row_inner; // each whole row's rims, at its middle
	std::vector<Map> maps;
	int64_t rays = 0;
};
KnotMap build_knots(const MapSpec &spec, const glass::Vessel &vessel, double height, int most = ROW_KNOTS_MOST);
MapRay knots_read(const KnotMap &knots, double x, double y, double nm, int medium);
// The table as the renderer reads it: [0] (K knots, R whole rows); [1 .. K] (its row, its map's offset in vec4s
// from the table's start, its pieces, 1 if a level span begins at it); [1 + K .. K + R] (the knot at or above
// the row's top, its outer rim, its inner); [1 + K + R .. 2K + R] (each knot's rims); then each map's map_block
// followed by its drops.
std::vector<float> knots_block(const KnotMap &knots);

// The map as the renderer reads it (lab/optics_scene.hpp lays the scene's maps out): vec4s.
//   [0]                 coarse, media, samples, pieces
//   [1 .. coarse]       each whole pixel's level (x), and how fast its landing bends in wavelength (y)
//   then                the cells, four vec4s each, by core, sample, piece
std::vector<float> map_block(const Map &map);

} // namespace lab::trace
