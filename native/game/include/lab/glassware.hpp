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
	double level(double cm3) const; // the height where the inside below holds cm3 (the brim if more)
	double radius_outside(double y) const; // 0 outside [0, height]
	double radius_inside(double y) const; // 0 outside [floor, height]
};

// The vessel of a kind and nominal capacity, from data/glassware.json; none if there is no such row.
std::optional<Vessel> make(std::string_view kind, double nominal_ml, std::string &why);
// What the table offers: each kind and its capacities.
struct Offer {
	std::string kind, standard;
	std::vector<double> ml;
};
std::vector<Offer> offered();

} // namespace lab::glass
