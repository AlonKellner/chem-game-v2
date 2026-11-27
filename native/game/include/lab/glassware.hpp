#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

// A vessel in its whole shape (GAME_SPEC §12.3, Stage 24): a piece of glassware made to its standard
// (data/glassware.json), its outside profile turned about its axis. One model, read by the lab's capacity,
// the liquid's level, the screen's silhouette and the tracer's surfaces. Lengths in cm.
namespace lab::glass {

// A place in the half-plane of a section: r from the axis, y up from the base.
struct Point {
	double r = 0.0, y = 0.0;
};

// A piece of a profile: a straight piece from a to b, or an arc of the circle (centre, radius) from angle
// t0 to t1 (radians from +r toward +y; t1 < t0 where the arc turns clockwise), from a to b.
struct Piece {
	bool arc = false;
	Point a, b;
	Point centre;
	double radius = 0.0, t0 = 0.0, t1 = 0.0;
	Point at(double s) const; // s from 0 (a) to 1 (b)
};
using Chain = std::vector<Piece>;

// A corner of a polyline: the point, the radius of the arc that rounds it (0: sharp), and how far the
// inside lies from the segment that leaves this corner, measured into the glass.
struct Corner {
	Point p;
	double fillet = 0.0;
	double thickness = 0.0;
};
// The polyline as a chain, each corner rounded by an arc tangent to both its segments (a radius too large
// for them is made as large as fits). Its first corner is on the axis.
Chain rounded(const std::vector<Corner> &corners);
// The same polyline moved into the glass, each segment by its own thickness, each corner's arc tangent to
// the moved segments (smaller where its centre is on the glass's side, larger where not).
std::vector<Corner> inward(const std::vector<Corner> &corners);
// A chain cut at height y: what is above is dropped, a piece across it ends there.
Chain below(const Chain &chain, double y);

struct Vessel {
	std::string kind;
	double nominal_ml = 0.0;
	double diameter = 0.0, height = 0.0, wall = 0.0, floor = 0.0; // the outside's greatest; overall; least; at the base
	// The outside from the axis at the base, up to the brim at `height`; the inside from the axis at the
	// floor's top, up to the brim. The top of the wall is the flat ring between their ends.
	Chain outside, inside;
	double capacity() const; // the inside's volume to the brim, cm^3
	double volume_below(double y) const; // the inside's volume under height y, cm^3
	// (Stage 71) the volume its outside encloses under height y over its base, cm^3: what it displaces standing in a
	// liquid to that depth (its glass and its inside both)
	double outside_below(double y) const;
	double level(double cm3) const; // the height where the inside below holds cm3 (the brim if more)
	double radius_outside(double y) const; // 0 outside [0, height]
	double radius_inside(double y) const; // 0 outside [floor, height]
	double widest() const; // the outside's greatest radius: a beaker's brim, a flask's base
	// Pouring (GAME_SPEC §13, Stage 27): a held pour's rate at full tilt, mL/s; the bore of the cap that fits
	// its mouth, cm; its mouth's inside radius, cm; and, upside down with a volume in it, how deep the liquid
	// stands over its mouth (the head an orifice there pours under), cm.
	double pour_ml_s = 0.0, cap_bore = 0.0;
	// Pressure (GAME_SPEC §13, Stage 28): how far over the room's its glass holds, Pa (sealed and past it, it
	// bursts); whether a screw cap fits it, upright, to seal it (a bottle's thread)
	double burst = 0.0;
	bool screw_cap = false;
	// its marks (GAME_SPEC §13, Readings; Stage 50): their spacing, mL (0: none), and how far one may be off its volume, a share
	double marks_ml = 0.0, marks_error = 0.0;
	double mouth() const;
	double head_inverted(double cm3) const;
	// Its open mouth's path for a gas (GAME_SPEC §6, Stage 50: Stefan's tube): the integral of dz / A(z) over the inside
	// from what it holds (cm^3 of solids and liquids) up to its brim, 1/cm, at least `least` cm over the mouth's area.
	// From a table made with the vessel (PATHS + 1 fills from empty to full), interpolated.
	double mouth_path(double held_cm3) const;
	// Volumetric glassware (Stage 52): its class A tolerance and the spacing of its scale, mL (0: none); an outlet at
	// its foot (a burette's, a pipette's) and its tip's bore, cm, made to give the standard's delivery time `delivery_s`
	// (Torricelli's, the orifice's Cd, from its zero line or its mark to empty); a burette's zero line, cm over its tip
	double tolerance_ml = 0.0, graduation_ml = 0.0, delivery_s = 0.0, tip_bore = 0.0, zero = 0.0;
	bool outlet = false;
	bool mark = false; // one mark it is filled to (a pipette's, a volumetric flask's)
	// (Stage 59) a filter funnel's: its stem's length over which its cone starts, cm; its cone's half-angle, radians;
	// its paper's Herzberg time (s for 100 mL of water at 20 C through 10 cm2 under 10 cm of head) and the height its
	// paper cone stands to, cm over the cone's apex (0: no paper)
	double stem = 0.0, cone_half_angle = 0.0, paper_herzberg_s = 0.0, paper_height = 0.0;
	// (Stage 79) a gas syringe's: its plunger's face from fully in (`plunger_least`, cm over its base) to its last mark
	// (`plunger_most`); the plunger's friction, N, and drag, N s/m; its length and its handle's width, cm
	bool syringe = false;
	double plunger_least = 0.0, plunger_most = 0.0, plunger_friction = 0.0, plunger_drag = 0.0, plunger_length = 0.0, handle = 0.0;
	double plunger_area() const; // the bore's, cm^2
	double plunger_y(double cm3) const; // its face's height holding cm3 under it (from fully in, the dead volume)
	static constexpr size_t PATHS = 256; // (Stage 52: a slender pipette's bore changes too fast for 64)
	std::vector<double> paths; // the table: at k / PATHS of its capacity
	double capacity_cached = 0.0; // capacity(), kept with the table
	void make_paths();
};
// the widest mouth a balloon's or a condenser's fitting seals, cm (data/glassware.json: seals_mm)
double seals();
// how far over the room's a vessel made as no glassware holds, Pa (data/glassware.json: burst_kpa)
double burst_default();

// The vessel of a kind and nominal capacity, from data/glassware.json; none if there is no such row.
std::optional<Vessel> make(std::string_view kind, double nominal_ml, std::string &why);
// The same, made once and kept: null if there is no such row.
const Vessel *find(std::string_view kind, double nominal_ml);
// What the table offers: each kind and its capacities.
struct Offer {
	std::string kind, standard;
	std::vector<double> ml;
};
std::vector<Offer> offered();

} // namespace lab::glass
