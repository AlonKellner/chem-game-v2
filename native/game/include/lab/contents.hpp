#pragma once

#include "lab/glassware.hpp"

#include <string>

// What lies in a vessel, at any turn (GAME_SPEC §13, §12.3, Stage 30): its liquid level in the room, its
// sediment a flat section with a cone on it, a stirred liquid's vortex a cone hanging from its surface, and how
// far the liquid stands over the pouring lip. One model, read by the lab's pour and by the picture. Pure: no
// state, no engine. Lengths in cm, volumes in cm^3, angles in radians.
//
// The vessel's frame: x across from its axis, y up from its base. It is turned about the eye's axis by `turn`,
// clockwise as seen (its top toward +x, as a vessel pouring from its right lip). The room's up, in the vessel's
// frame, is then (-sin turn, cos turn); a level is a height along it.
namespace lab::contents {

// A level cut through the inside: the line it makes in the vessel's section (seen from the eye), and the area
// of the level surface itself (which reaches into the depth as far as the vessel is round there).
struct Cut {
	bool any = false;
	double x0 = 0.0, y0 = 0.0, x1 = 0.0, y1 = 0.0; // the line's two ends
	double mid_x = 0.0, mid_y = 0.0, half = 0.0; // its middle, and half its length
	double area = 0.0; // cm^2
	double deep() const; // half the surface's reach into the depth, were it an ellipse of that area on that line
};

// A cone standing on a level cut (a pile) or hanging from one (a vortex): its base an ellipse centred on the
// cut's middle, as wide as the cut (so on the screen its edges touch the cut's ends) and as deep as gives it
// the cut's area; its tip on the room's upright through the middle. Its volume is its base's area times its
// height over three, whatever the vessel and the turn.
struct Cone {
	double height = 0.0;
	double x = 0.0, y = 0.0; // the base's middle
	double across = 0.0, deep = 0.0; // the base's half width on the screen, and into the depth
	double base_area() const;
	double volume() const;
};

struct Lying {
	double turn = 0.0;
	double up_x = 0.0, up_y = 1.0; // the room's up, in the vessel's frame
	double lowest = 0.0, highest = 0.0; // the inside's lowest and highest levels
	// the sediment: a flat section up to solid_level, and the pile on it (its height 0 where there is none)
	bool solid = false;
	double solid_level = 0.0;
	Cone pile;
	// the liquid: its surface at liquid_level; the vortex hanging from it (its height 0 where there is none)
	bool liquid = false;
	double liquid_level = 0.0;
	double surface_area = 0.0; // of the liquid's level surface
	double depth = 0.0; // of liquid under the surface's middle, to the glass or the sediment
	Cone vortex;
	// the pouring lip (the inner edge of the brim, on the side poured from) and tip (its outer edge), and how far
	// the liquid's surface and the sediment's flat top stand over the lip (0 or less: not at it)
	double lip_x = 0.0, lip_y = 0.0, tip_x = 0.0, tip_y = 0.0;
	double lip_level = 0.0;
	double head = 0.0, solid_head = 0.0;
};

// A vessel's pouring lip (the inner edge of its brim) and tip (its outer edge), on the side poured from, in its
// own frame
struct Lip {
	double lip_x = 0.0, lip_y = 0.0, tip_x = 0.0, tip_y = 0.0;
};
Lip lip_of(const glass::Vessel &vessel, int side);

// the inside's volume under a level, and the level under which it holds a volume
double below(const glass::Vessel &vessel, double turn, double level);
double level_of(const glass::Vessel &vessel, double turn, double cm3);
Cut cut(const glass::Vessel &vessel, double turn, double level);

// The angle of repose's tangent (35 degrees), and how much of a heap of powder is powder (the rest is what it
// lies in): as the heap was drawn before (GAME_SPEC §12.3).
inline constexpr double REPOSE = 0.7, PACKING = 0.5;

// What lies in the vessel turned by `turn`, pouring from its right lip (side 1) or its left (-1): `liquid_cm3` of
// liquid (what is dissolved in it with it), `solid_cm3` of sediment (the solids' own volume: the pile's is that
// over PACKING, and the liquid fills the rest of it where it reaches), a vortex of the share `vortex` (0 to 1)
// of the largest that fits.
Lying lying(const glass::Vessel &vessel, double turn, double liquid_cm3, double solid_cm3, double vortex, int side);

// The turn at which nothing is held below the lip (the vessel is poured out), and what it holds below its lip
// at a turn.
double full_tilt(const glass::Vessel &vessel, int side);
double held_at(const glass::Vessel &vessel, double turn, int side);

// ---- pouring over the lip (GAME_SPEC §13, Stage 31)

// What runs out of a round mouth, cm^3 a second: Torricelli's speed, sqrt(2 g depth), over the wet part of the
// opening, contracted as a jet from a sharp edge is. The mouth, a disc of radius `brim` turned by `turn`, rises
// from its lip; what stands `head` over the lip wets it as far as it rises that much. Short of lying on its side
// the liquid leaves level, over the lip, through the opening as it is seen from the side: a weir, for a small
// head Cd sqrt(2 g) (pi / 4) sqrt(2 brim / sin turn) head^2. Turned further it leaves along the mouth's axis,
// through the opening itself: upside down, an orifice under the head, Cd pi brim^2 sqrt(2 g head). One integral.
//
// The contraction: of a jet leaving a slot in a wall, pi / (pi + 2) (Kirchhoff's free streamline, 1869); sharp
// weirs and orifices are measured to discharge 0.60 to 0.62 of the ideal (docs/DATA_SOURCES.md). Air coming in
// against the liquid (a full mouth glugs) is not modelled (GAME_SPEC §20).
inline constexpr double WEIR_CD = 0.6110154703516573;
inline constexpr double GRAVITY = 980.665; // cm/s^2, standard
double over_brim(double brim, double turn, double head);

// ---- a liquid turning in its vessel (GAME_SPEC §11, Stage 32)

// A stirred liquid turns, and its surface dips. For any swirl that turns in columns, its kinetic energy is the
// liquid's weight on a unit of its surface times the volume the dip displaces: E = (M g / A) V (each column's
// pressure holds its turning, and the surface's shape is the sum). The dip is drawn as a cone in the proportions
// of the space it has, the surface's ellipse by the depth D under its middle: a share s of that space displaces
// s^3 A D / 3, so s = cbrt(E / E_most) with E_most = M g D / 3, the energy at which the vortex reaches the glass
// (or the sediment). A heavier liquid holds more energy in a smaller vortex; a shallow one cannot make a deep one.
struct Swirl {
	double most = 0.0; // J: the most its turning can hold
	double depth = 0.0; // cm: of liquid under the surface's middle, with that vortex in it
	double share = 0.0; // the vortex's share (0 to 1) of the largest that fits, for the energy asked
};
// `mass_g`: of the liquid with what is dissolved in it; `energy`: J.
Swirl swirl(const glass::Vessel &vessel, double turn, double liquid_cm3, double solid_cm3, double mass_g, double energy);

// ---- invariants (GAME_SPEC §18): "" or the failure, starting with its id

// H1: what lies in a vessel adds up: the liquid's region, less the powder in the part of the pile under it and
// less the vortex, holds the liquid; the flat section and the pile hold the sediment's bulk.
std::string check_H1(const glass::Vessel &vessel, const Lying &lying, double liquid_cm3, double solid_cm3);
// H2: a pile is no steeper than the angle of repose, and stands on its flat section; a vortex is no wider than
// the liquid's surface and no deeper than the liquid under its middle.
std::string check_H2(const Lying &lying);

} // namespace lab::contents
