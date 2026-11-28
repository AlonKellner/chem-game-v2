#pragma once

#include <span>
#include <vector>

// (Stage 95, GAME_SPEC §6, Tubes) A tube's path: its centre-line in space, pure. Vertices in metres, z up over the
// bench's datum, joined by straight runs; at an inner vertex the two runs are joined by an arc of a circle of the
// vertex's fillet radius, tangent to both (0: a sharp corner). So a path is straight pieces and arcs end to end,
// its tangent continuous but at a sharp corner. Lengths are along it from its first vertex. What a tube asks of
// it: where a place along it is and how high, the mean height and the highest and lowest points of a stretch (a
// slug's weight and where it would part), and how far a stretch turns (a turn's loss).
namespace chem {

struct Point3 {
	double x = 0.0, y = 0.0, z = 0.0;
	bool operator==(const Point3 &) const = default;
};

struct PathVertex {
	Point3 at; // m
	double fillet = 0.0; // m: the radius of the centre-line's arc at this vertex (an inner one; 0: a sharp corner)
	bool operator==(const PathVertex &) const = default;
};

// A straight run (no radius, no angle), an arc (a radius and an angle), or a sharp corner (an angle, no length).
struct PathPiece {
	double start = 0.0, length = 0.0; // m along the path
	Point3 from; // where it begins
	Point3 along; // its tangent there, a unit
	Point3 inward; // an arc's or a corner's: the unit from where it begins toward its centre
	double radius = 0.0; // m
	double angle = 0.0; // rad it turns through
	bool operator==(const PathPiece &) const = default;
};

class Path {
  public:
	Path() = default;
	// The path through the vertices. Not good (and empty) with fewer than two, a value not finite, two neighbours at
	// one place, a run doubling straight back, or a fillet that does not fit the runs it joins (its two tangent
	// lengths, r tan(angle / 2), within each run's length, shared with the run's other fillet).
	explicit Path(std::span<const PathVertex> vertices);
	bool good() const { return !pieces_.empty(); }
	double length() const { return length_; }
	const std::vector<PathVertex> &vertices() const { return vertices_; }
	const std::vector<PathPiece> &pieces() const { return pieces_; }

	// at a length along it (held within the path)
	Point3 point(double s) const;
	Point3 tangent(double s) const; // a unit; at a sharp corner, the run's that leaves it
	double height(double s) const; // point(s).z

	// of the stretch between two lengths (either order): the mean of its height along it (0 length: its height), its
	// highest and lowest heights and where along it they are, and the angle it turns through, rad
	double mean_height(double s1, double s2) const;
	double highest(double s1, double s2, double *at = nullptr) const;
	double lowest(double s1, double s2, double *at = nullptr) const;
	double turned(double s1, double s2) const;

	bool operator==(const Path &other) const { return vertices_ == other.vertices_; }

  private:
	size_t piece_at(double s) const;
	double extreme(double s1, double s2, bool high, double *at) const;
	std::vector<PathVertex> vertices_;
	std::vector<PathPiece> pieces_;
	double length_ = 0.0;
};

// Presets (GAME_SPEC §6): a straight run between two points; and through corner points with one fillet at each.
Path straight_path(Point3 from, Point3 to);
Path bent_path(std::span<const Point3> points, double fillet);

} // namespace chem
