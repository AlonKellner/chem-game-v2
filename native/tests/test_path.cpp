// (Stage 95, GAME_SPEC §6, Tubes) A tube's path (chem/path.hpp) against closed forms and a walk along it.
#include "chem/path.hpp"

#include <doctest/doctest.h>

#include <cmath>
#include <limits>

using namespace chem;

namespace {

constexpr double PI = 3.141592653589793;

doctest::Approx near(double v, double eps = 1e-12) {
	return doctest::Approx(v).epsilon(eps).scale(1.0);
}

} // namespace

TEST_CASE("path: a straight run: its length, its heights, its tangent, nothing turned (Stage 95)") {
	const Path p = straight_path({ 0, 0, 0.1 }, { 0.3, 0, 0.5 });
	REQUIRE(p.good());
	CHECK(p.length() == near(0.5));
	CHECK(p.height(0.25) == near(0.3));
	CHECK(p.mean_height(0.0, 0.5) == near(0.3));
	CHECK(p.mean_height(0.5, 0.25) == near(0.4)); // either order
	CHECK(p.mean_height(0.2, 0.2) == near(p.height(0.2))); // no length: its height
	double at = -1.0;
	CHECK(p.highest(0.1, 0.4, &at) == near(0.1 + 0.8 * 0.4));
	CHECK(at == near(0.4));
	CHECK(p.lowest(0.1, 0.4, &at) == near(0.1 + 0.8 * 0.1));
	CHECK(at == near(0.1));
	CHECK(p.turned(0.0, 0.5) == 0.0);
	CHECK(p.tangent(0.2).x == near(0.6));
	CHECK(p.tangent(0.2).z == near(0.8));
	CHECK(p.height(-1.0) == near(0.1)); // held within the path
	CHECK(p.height(9.0) == near(0.5));
}

TEST_CASE("path: an inverted U with fillets (a siphon): its length the runs' less the corners' and the arcs', place and tangent continuous, the mean height a walk's (Stage 95)") {
	const double r = 0.02;
	const Point3 pts[4] = { { 0, 0, 0 }, { 0, 0, 0.3 }, { 0.2, 0, 0.3 }, { 0.2, 0, -0.2 } };
	const Path p = bent_path(pts, r);
	REQUIRE(p.good());
	CHECK(p.pieces().size() == 5);
	CHECK(p.length() == near((0.3 - r) + (0.2 - 2 * r) + (0.5 - r) + PI * r)); // two quarter arcs
	CHECK(p.turned(0.0, p.length()) == near(PI));
	CHECK(p.height(0.0) == near(0.0));
	CHECK(p.height(p.length()) == near(-0.2));
	double at = 0.0;
	CHECK(p.highest(0.0, p.length(), &at) == near(0.3));
	CHECK(p.lowest(0.0, p.length(), &at) == near(-0.2));
	CHECK(at == near(p.length()));
	// the middle of the first arc: a quarter circle about (r, 0, 0.3 - r)
	const double s = (0.3 - r) + 0.25 * PI * r;
	CHECK(p.point(s).x == near(r - r * std::cos(PI / 4)));
	CHECK(p.point(s).z == near(0.3 - r + r * std::sin(PI / 4)));
	CHECK(p.tangent(s).x == near(std::sin(PI / 4)));
	CHECK(p.tangent(s).z == near(std::cos(PI / 4)));
	for (const PathPiece &q : p.pieces()) {
		if (q.start > 0.0) {
			const Point3 a = p.point(q.start - 1e-9), b = p.point(q.start + 1e-9), ta = p.tangent(q.start - 1e-9), tb = p.tangent(q.start + 1e-9);
			CHECK(a.x == near(b.x, 1e-8));
			CHECK(a.z == near(b.z, 1e-8));
			CHECK(ta.x == near(tb.x, 1e-6));
			CHECK(ta.z == near(tb.z, 1e-6));
		}
	}
	const auto walk = [&](double a, double b) {
		const int n = 20000;
		double sum = 0.0;
		for (int i = 0; i < n; ++i) {
			sum += p.height(a + (b - a) * (i + 0.5) / n);
		}
		return sum / n;
	};
	CHECK(p.mean_height(0.0, p.length()) == near(walk(0.0, p.length()), 1e-8));
	CHECK(p.mean_height(0.27, 0.35) == near(walk(0.27, 0.35), 1e-8));
	CHECK(p.mean_height(0.5, 0.29) == near(walk(0.29, 0.5), 1e-8));
	// a unit tangent everywhere, and the place the tangent's integral
	Point3 q = p.point(0.0);
	const int n = 20000;
	for (int i = 0; i < n; ++i) {
		const Point3 t = p.tangent(p.length() * (i + 0.5) / n);
		REQUIRE(t.x * t.x + t.y * t.y + t.z * t.z == near(1.0));
		q.x += t.x * p.length() / n;
		q.y += t.y * p.length() / n;
		q.z += t.z * p.length() / n;
	}
	CHECK(q.x == near(0.2, 1e-7));
	CHECK(q.z == near(-0.2, 1e-7));
}

TEST_CASE("path: an arc's top lies inside it: over a hump of 45 degrees a side, the highest point at the arc's middle (Stage 95)") {
	const Point3 pts[3] = { { 0, 0, 0 }, { 0.1, 0, 0.1 }, { 0.2, 0, 0 } };
	const Path p = bent_path(pts, 0.03);
	REQUIRE(p.good());
	double at = 0.0;
	// the arc's centre is r sqrt(2) under the vertex, its top r over the centre
	CHECK(p.highest(0.0, p.length(), &at) == near(0.1 - 0.03 * std::sqrt(2.0) + 0.03));
	CHECK(at == near(0.5 * p.length()));
	CHECK(p.highest(0.0, 0.25 * p.length(), &at) == near(p.height(0.25 * p.length()))); // a stretch short of it: its end
	CHECK(p.turned(0.0, p.length()) == near(PI / 2));
	CHECK(p.turned(0.0, 0.5 * p.length()) == near(PI / 4));
	// a trough the same way up
	const Point3 down[3] = { { 0, 0, 0 }, { 0.1, 0, -0.1 }, { 0.2, 0, 0 } };
	const Path u = bent_path(down, 0.03);
	CHECK(u.lowest(0.0, u.length(), &at) == near(-0.1 + 0.03 * std::sqrt(2.0) - 0.03));
	CHECK(at == near(0.5 * u.length()));
}

TEST_CASE("path: out of the plane, and a sharp corner: no length at it, its angle turned as a stretch passes it (Stage 95)") {
	const Point3 pts[4] = { { 0, 0, 0 }, { 0.1, 0, 0 }, { 0.1, 0.1, 0 }, { 0.1, 0.1, 0.1 } };
	const Path p = bent_path(pts, 0.01);
	REQUIRE(p.good());
	CHECK(p.length() == near(0.3 - 4 * 0.01 + PI * 0.01));
	CHECK(p.point(p.length()).y == near(0.1));
	CHECK(p.point(p.length()).z == near(0.1));
	const Path sharp = bent_path(pts, 0.0);
	REQUIRE(sharp.good());
	CHECK(sharp.length() == near(0.3));
	CHECK(sharp.turned(0.0, 0.3) == near(PI));
	CHECK(sharp.turned(0.0, 0.15) == near(PI / 2));
	CHECK(sharp.point(0.15).y == near(0.05));
	CHECK(sharp.tangent(0.15).y == near(1.0));
	// the same vertices are the same path
	CHECK(sharp == bent_path(pts, 0.0));
	CHECK_FALSE(sharp == p);
}

TEST_CASE("path: refused: fewer than two vertices, two at one place, a run doubling back, a fillet that does not fit, a value not finite (Stage 95)") {
	const Point3 one[1] = { { 0, 0, 0 } };
	CHECK_FALSE(bent_path(one, 0.0).good());
	const Point3 same[2] = { { 0, 0, 0 }, { 0, 0, 0 } };
	CHECK_FALSE(bent_path(same, 0.0).good());
	const Point3 back[3] = { { 0, 0, 0 }, { 0.1, 0, 0 }, { 0, 0, 0 } };
	CHECK_FALSE(bent_path(back, 0.01).good());
	const Point3 corner[3] = { { 0, 0, 0 }, { 0.1, 0, 0 }, { 0.1, 0.1, 0 } };
	CHECK_FALSE(bent_path(corner, 0.11).good()); // longer than its runs
	CHECK(bent_path(corner, 0.1).good()); // one that takes them whole: a quarter circle alone
	CHECK(bent_path(corner, 0.1).length() == near(0.5 * PI * 0.1));
	const Point3 two[4] = { { 0, 0, 0 }, { 0.1, 0, 0 }, { 0.1, 0.1, 0 }, { 0.2, 0.1, 0 } };
	CHECK_FALSE(bent_path(two, 0.06).good()); // two fillets sharing a run too short for both
	CHECK(bent_path(two, 0.05).good());
	const Point3 nan[2] = { { 0, 0, 0 }, { std::numeric_limits<double>::quiet_NaN(), 0, 0 } };
	CHECK_FALSE(bent_path(nan, 0.0).good());
	const PathVertex negative[3] = { { { 0, 0, 0 }, 0.0 }, { { 0.1, 0, 0 }, -0.01 }, { { 0.1, 0.1, 0 }, 0.0 } };
	CHECK_FALSE(Path(negative).good());
	const Path none;
	CHECK_FALSE(none.good());
	CHECK(none.length() == 0.0);
	CHECK(none.height(0.1) == 0.0);
}
