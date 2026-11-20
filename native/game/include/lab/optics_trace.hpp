#pragma once

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
struct Patch {
	Vec3 a, l;
	double c = 0.0;
	Vec3 lo{ -1.0e300, -1.0e300, -1.0e300 }, hi{ 1.0e300, 1.0e300, 1.0e300 };
	int below = 0, above = 0; // the medium where f < 0, and where f > 0
};

struct Shape {
	std::vector<Patch> patches;
};

// What becomes of one ray.
struct Landing {
	bool lands = false; // it leaves the body going away from the eye, and reaches the wall
	double x = 0.0, y = 0.0; // where, on the wall's plane
	double pass = 1.0; // what its surfaces let through (Fresnel at each crossing)
	std::array<double, MAX_MEDIA> chord{}; // its path in each medium, cm
	uint32_t route = 0; // the surfaces it crossed and where it was turned back: the same for rays that go the same way
	int meetings = 0;
};

// One ray from `origin` along `direction` (a unit vector) through the shape, to the wall's plane
// z = wall; `index` is each medium's refractive index. At each surface it refracts by vector Snell,
// losing what Fresnel reflects; where it cannot pass it is turned back, all of it.
Landing trace(const Shape &shape, Vec3 origin, Vec3 direction, double wall, const std::array<double, MAX_MEDIA> &index);

// An upright cylinder of radius `outer` (cm) with a wall of `glass` (cm) around its core; with no
// glass, the core alone.
Shape cylinder(double outer, double glass);
// An upright prism: a triangle of glass seen from above, its corners (x, z).
Shape prism(const double corners[3][2]);
// A ball of glass (the tracer's rays bend up and down as well as across).
Shape ball(double radius);

// ---- a body's refraction map

// What a map is traced for: the body, how it is drawn, and its indices at 1 nm from 380 nm (401
// values each; null: 1).
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
	int64_t rays = 0; // traced to build it
	int samples() const;
	const float *cell(int medium, int piece, int sample) const {
		return &cells[((static_cast<size_t>(medium) * static_cast<size_t>(samples()) + static_cast<size_t>(sample)) * static_cast<size_t>(pieces) + static_cast<size_t>(piece)) * MAP_CELL];
	}
};

inline constexpr int MAP_FINE = 4; // samples a pixel: a pixel is read in four parts at most
inline constexpr int MAP_PIECES_MOST = 64;
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
};
MapRay map_ray(const MapSpec &spec, double x, double nm, int medium);
// The same read from the map: straight between two samples across (the nearer one's where the route
// changes between them); in wavelength, the cubic through its piece's four nodes.
MapRay map_read(const Map &map, double x, double nm, int medium);

// The map as the renderer reads it (lab/optics_scene.hpp lays the scene's maps out): vec4s.
//   [0]                 coarse, media, samples, pieces
//   [1 .. coarse]       each whole pixel's level (x), and how fast its landing bends in wavelength (y)
//   then                the cells, four vec4s each, by core, sample, piece
std::vector<float> map_block(const Map &map);

} // namespace lab::trace
