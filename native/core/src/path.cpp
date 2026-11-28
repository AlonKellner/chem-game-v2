// (Stage 95, GAME_SPEC §6, Tubes) A tube's path: straight runs and arcs end to end.
#include "chem/path.hpp"

#include "chem/math.hpp"

#include <algorithm>
#include <cmath>

namespace chem {

namespace {

Point3 sum(Point3 a, Point3 b, double k = 1.0) {
	return Point3{ a.x + k * b.x, a.y + k * b.y, a.z + k * b.z };
}

double dot(Point3 a, Point3 b) {
	return a.x * b.x + a.y * b.y + a.z * b.z;
}

double norm(Point3 a) {
	return std::sqrt(dot(a, a));
}

Point3 scaled(Point3 a, double k) {
	return Point3{ a.x * k, a.y * k, a.z * k };
}

bool finite(double v) {
	return v - v == 0.0;
}

bool finite(Point3 p) {
	return finite(p.x) && finite(p.y) && finite(p.z);
}

// an arc's point and tangent an angle along it
Point3 arc_point(const PathPiece &p, double angle) {
	const double s = sin(angle), c = cos(angle);
	return sum(sum(p.from, p.inward, p.radius * (1.0 - c)), p.along, p.radius * s);
}

Point3 arc_tangent(const PathPiece &p, double angle) {
	return sum(scaled(p.inward, sin(angle)), p.along, cos(angle));
}

} // namespace

Path::Path(std::span<const PathVertex> vertices) {
	const size_t n = vertices.size();
	if (n < 2) {
		return;
	}
	for (const PathVertex &v : vertices) {
		if (!finite(v.at) || !finite(v.fillet) || v.fillet < 0.0) {
			return;
		}
	}
	// each run's direction and length
	std::vector<Point3> dir(n - 1);
	std::vector<double> run(n - 1);
	for (size_t i = 0; i + 1 < n; ++i) {
		const Point3 d = sum(vertices[i + 1].at, vertices[i].at, -1.0);
		run[i] = norm(d);
		if (!(run[i] > 0.0)) {
			return;
		}
		dir[i] = scaled(d, 1.0 / run[i]);
	}
	// each inner vertex's turn: its angle, and what its fillet takes of the two runs it joins
	std::vector<double> angle(n, 0.0), trim(n, 0.0);
	std::vector<Point3> inward(n);
	for (size_t i = 1; i + 1 < n; ++i) {
		const Point3 less = sum(dir[i], dir[i - 1], -1.0), more = sum(dir[i], dir[i - 1]);
		const double a = norm(less), b = norm(more);
		if (!(b > 1e-9)) {
			return; // (doubling straight back: no arc joins it)
		}
		const double half = a / b; // tan(angle / 2)
		if (!(a > 1e-12)) {
			continue; // (straight on)
		}
		angle[i] = 2.0 * atan(half);
		trim[i] = vertices[i].fillet * half;
		// toward the centre: the leaving run's direction, less its part along the arriving one
		const Point3 across = sum(dir[i], dir[i - 1], -dot(dir[i], dir[i - 1]));
		const double m = norm(across);
		if (!(m > 0.0)) {
			return;
		}
		inward[i] = scaled(across, 1.0 / m);
	}
	std::vector<PathPiece> pieces;
	double at = 0.0;
	for (size_t i = 0; i + 1 < n; ++i) {
		const double straight = run[i] - trim[i] - trim[i + 1];
		if (straight < -1e-12 * run[i]) {
			return; // (the fillets do not fit this run)
		}
		if (straight > 0.0) {
			PathPiece p;
			p.start = at;
			p.length = straight;
			p.from = sum(vertices[i].at, dir[i], trim[i]);
			p.along = dir[i];
			pieces.push_back(p);
			at += straight;
		}
		if (i + 2 < n && angle[i + 1] > 0.0) {
			PathPiece p;
			p.start = at;
			p.radius = vertices[i + 1].fillet;
			p.angle = angle[i + 1];
			p.length = p.radius * p.angle;
			p.from = sum(vertices[i + 1].at, dir[i], -trim[i + 1]);
			p.along = dir[i];
			p.inward = inward[i + 1];
			pieces.push_back(p);
			at += p.length;
		}
	}
	if (!(at > 0.0)) {
		return;
	}
	vertices_.assign(vertices.begin(), vertices.end());
	pieces_ = std::move(pieces);
	length_ = at;
}

// the piece a length lies in: the last that begins at or before it and has a length (a corner holds no length)
size_t Path::piece_at(double s) const {
	size_t found = 0;
	bool any = false;
	for (size_t i = 0; i < pieces_.size(); ++i) {
		if (pieces_[i].length > 0.0 && (!any || pieces_[i].start <= s)) {
			found = i;
			any = true;
		}
	}
	return found;
}

Point3 Path::point(double s) const {
	if (pieces_.empty()) {
		return Point3{};
	}
	s = std::clamp(s, 0.0, length_);
	const PathPiece &p = pieces_[piece_at(s)];
	const double along = std::clamp(s - p.start, 0.0, p.length);
	return p.radius > 0.0 ? arc_point(p, along / p.radius) : sum(p.from, p.along, along);
}

Point3 Path::tangent(double s) const {
	if (pieces_.empty()) {
		return Point3{};
	}
	s = std::clamp(s, 0.0, length_);
	const PathPiece &p = pieces_[piece_at(s)];
	const double along = std::clamp(s - p.start, 0.0, p.length);
	return p.radius > 0.0 ? arc_tangent(p, along / p.radius) : p.along;
}

double Path::height(double s) const {
	return point(s).z;
}

double Path::mean_height(double s1, double s2) const {
	if (pieces_.empty()) {
		return 0.0;
	}
	double lo = std::clamp(std::min(s1, s2), 0.0, length_), hi = std::clamp(std::max(s1, s2), 0.0, length_);
	if (!(hi > lo)) {
		return height(lo);
	}
	double integral = 0.0; // of z ds
	for (const PathPiece &p : pieces_) {
		const double a = std::max(lo, p.start) - p.start, b = std::min(hi, p.start + p.length) - p.start;
		if (!(b > a)) {
			continue;
		}
		if (p.radius > 0.0) {
			// z = z0 + r (1 - cos t) in + r sin t al, ds = r dt
			const double ta = a / p.radius, tb = b / p.radius;
			const auto primitive = [&](double t) { return p.radius * ((p.from.z + p.radius * p.inward.z) * t - p.radius * p.inward.z * sin(t) - p.radius * p.along.z * cos(t)); };
			integral += primitive(tb) - primitive(ta);
		} else {
			integral += (b - a) * (p.from.z + p.along.z * 0.5 * (a + b));
		}
	}
	return integral / (hi - lo);
}

double Path::extreme(double s1, double s2, bool high, double *at) const {
	if (pieces_.empty()) {
		return 0.0;
	}
	const double lo = std::clamp(std::min(s1, s2), 0.0, length_), hi = std::clamp(std::max(s1, s2), 0.0, length_);
	double best = height(lo), where = lo;
	const auto take = [&](double s) {
		const double z = height(s);
		if (high ? z > best : z < best) {
			best = z;
			where = s;
		}
	};
	take(hi);
	for (const PathPiece &p : pieces_) {
		const double a = std::max(lo, p.start), b = std::min(hi, p.start + p.length);
		if (!(b > a)) {
			continue;
		}
		take(a);
		take(b);
		if (p.radius > 0.0) {
			// dz/dt = r (sin t in + cos t al) = 0: t = atan2(-al, in), and half a turn on
			constexpr double PI = 3.14159265358979311600;
			double t = atan2(-p.along.z, p.inward.z);
			for (int k = 0; k < 4; ++k, t += PI) {
				const double s = p.start + t * p.radius;
				if (t >= 0.0 && s > a && s < b) {
					take(s);
				}
			}
		}
	}
	if (at != nullptr) {
		*at = where;
	}
	return best;
}

double Path::highest(double s1, double s2, double *at) const {
	return extreme(s1, s2, true, at);
}

double Path::lowest(double s1, double s2, double *at) const {
	return extreme(s1, s2, false, at);
}

double Path::turned(double s1, double s2) const {
	const double lo = std::clamp(std::min(s1, s2), 0.0, length_), hi = std::clamp(std::max(s1, s2), 0.0, length_);
	double angle = 0.0;
	for (const PathPiece &p : pieces_) {
		if (!(p.angle > 0.0)) {
			continue;
		}
		if (p.length > 0.0) {
			const double a = std::max(lo, p.start), b = std::min(hi, p.start + p.length);
			if (b > a) {
				angle += p.angle * (b - a) / p.length;
			}
		} else if (p.start > lo && p.start < hi) {
			angle += p.angle; // (a sharp corner within the stretch)
		}
	}
	return angle;
}

Path straight_path(Point3 from, Point3 to) {
	const PathVertex v[2] = { { from, 0.0 }, { to, 0.0 } };
	return Path(v);
}

Path bent_path(std::span<const Point3> points, double fillet) {
	std::vector<PathVertex> v;
	v.reserve(points.size());
	for (size_t i = 0; i < points.size(); ++i) {
		v.push_back(PathVertex{ points[i], i > 0 && i + 1 < points.size() ? fillet : 0.0 });
	}
	return Path(v);
}

} // namespace chem
