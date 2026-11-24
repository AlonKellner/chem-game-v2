#pragma once

#include "lab/glassware.hpp"

#include <array>
#include <cstdint>
#include <vector>

// The truth's tracer (GAME_SPEC §12.3, Stage 20). Rays are traced on the CPU, in float64, through bodies
// made of surface patches. The renderer traces a pixel's rays by a tracer of its own, the same method written
// again (native/game/shader/optics_pixel.glsl), and is held to this one ray by ray (O9).
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
	double pass = 1.0; // what its surfaces let through (Fresnel at each crossing), less what its media took in
	// (Stage 46) what its media took in on the way (each medium's `absorb`, per cm), and what its surfaces turned
	// away: pass + absorbed + turned = 1 (O22)
	double absorbed = 0.0, turned = 0.0;
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
// (Stage 46) `absorb`: each medium's absorption per cm (none: nothing absorbs).
Landing trace(const Shape &shape, Vec3 origin, Vec3 direction, double wall, const std::array<double, MAX_MEDIA> &index, const Inside *inside = nullptr,
		const std::array<double, MAX_MEDIA> *absorb = nullptr);
// The meetings a ray is allowed in one shape (Stage 33). Straight through a vessel is four, through its
// liquid's surface five, through a vortex and a floor more. Measured over the play (Stage 33): 2% of passages
// take more than eight. On the GPU a group of pixels pays for its longest ray.
inline constexpr int MEETINGS_MOST = 16;
// How often a ray may be turned back inside one shape (total reflection). A ray through a vessel, its liquid's
// surface, a vortex and a floor crosses many surfaces and is turned back at none: those are followed (to
// MEETINGS_MOST). Light held in the glass of a rim or a fillet is turned back again and again: after
// TURNS_MOST it goes straight on from where it is, as if the rest of the body were not there, and so does a ray
// still inside after MEETINGS_MOST. (The user's call, 2026-11-23: with a ray simply ended after eight meetings
// and bringing nothing, a vortex in a shallow liquid showed grey patches where its rays were cut short.)
inline constexpr int TURNS_MOST = 4;

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

} // namespace lab::trace
