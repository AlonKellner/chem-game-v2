// A vessel's glass as the tracer sees it (GAME_SPEC §12.3, Stage 24): its profile turned about its axis,
// straight pieces as cones, arcs as tori traced numerically; held to closed forms.
#include "lab/glassware.hpp"
#include "lab/optics_trace.hpp"

#include <doctest/doctest.h>

#include <cmath>
#include <numbers>
#include <random>
#include <string>

using namespace lab::trace;
namespace glass = lab::glass;

namespace {

glass::Vessel vessel(const char *kind, double ml) {
	std::string why;
	auto v = glass::make(kind, ml, why);
	REQUIRE_MESSAGE(v.has_value(), why);
	return *v;
}

constexpr std::array<double, MAX_MEDIA> WATER{ 1.0, 1.47, 1.333, 1.0 };

} // namespace

TEST_CASE("trace: a beaker's upright band is the glass cylinder's closed form (Stage 24)") {
	// half way up a 250 mL beaker its profile is a cylinder: 3.5 cm outside, 1.1 mm of glass
	const glass::Vessel v = vessel("beaker", 250);
	const Shape beaker = revolved(v), cylinder_ = cylinder(3.5, 0.11);
	int compared = 0;
	for (double x = -3.45; x < 3.5; x += 0.0937) {
		const Landing a = trace(beaker, Vec3{ x, 5.0, -1000.0 }, Vec3{ 0.0, 0.0, 1.0 }, 20.0, WATER);
		const Landing b = trace(cylinder_, Vec3{ x, 5.0, -1000.0 }, Vec3{ 0.0, 0.0, 1.0 }, 20.0, WATER);
		CAPTURE(x);
		CHECK(a.lands == b.lands);
		CHECK(a.x == doctest::Approx(b.x).epsilon(1e-9));
		CHECK(a.y == doctest::Approx(5.0).epsilon(1e-12));
		CHECK(a.pass == doctest::Approx(b.pass).epsilon(1e-9));
		CHECK(a.chord[CORE] == doctest::Approx(b.chord[CORE]).epsilon(1e-9));
		CHECK(a.mismatched == 0);
		++compared;
	}
	CHECK(compared > 70);
}

TEST_CASE("trace: an arc centred on the axis is a sphere, and traces as the ball does (Stage 24)") {
	glass::Vessel v;
	glass::Piece arc;
	arc.arc = true;
	arc.centre = { 0.0, 2.0 };
	arc.radius = 2.0;
	arc.t0 = -0.5 * std::numbers::pi;
	arc.t1 = 0.5 * std::numbers::pi;
	arc.a = arc.at(0.0);
	arc.b = arc.at(1.0);
	v.outside = { arc };
	const Shape profile = revolved(v), sphere = ball(2.0);
	REQUIRE(profile.patches.size() == 1);
	CHECK_FALSE(profile.patches[0].torus);
	for (double x = -1.9; x < 2.0; x += 0.31) {
		for (double y = -1.7; y < 1.8; y += 0.43) {
			if (x * x + y * y > 0.81 * 4.0) {
				continue; // grazing the rim, where a landing far off magnifies rounding alone
			}
			const Landing a = trace(profile, Vec3{ x, y + 2.0, -1000.0 }, Vec3{ 0.0, 0.0, 1.0 }, 25.0, WATER);
			const Landing b = trace(sphere, Vec3{ x, y, -1000.0 }, Vec3{ 0.0, 0.0, 1.0 }, 25.0, WATER);
			CHECK(a.x == doctest::Approx(b.x).epsilon(1e-9));
			CHECK(a.y - 2.0 == doctest::Approx(b.y).epsilon(1e-9));
			CHECK(a.pass == doctest::Approx(b.pass).epsilon(1e-9));
		}
	}
}

TEST_CASE("trace: a torus is met where its quartic first changes sign on its own arc (Stage 24)") {
	// each arc of the flask (its base's corner, its shoulder, its neck's bead) alone, as a surface of glass:
	// the first crossing the tracer finds is the first a fine search of the quartic finds, to 1e-9 cm
	const glass::Vessel v = vessel("conical_flask", 250);
	std::mt19937_64 rng(24);
	std::uniform_real_distribution<double> unit(-1.0, 1.0);
	int arcs = 0, hits = 0;
	for (const glass::Piece &piece : v.outside) {
		if (!piece.arc) {
			continue;
		}
		++arcs;
		glass::Vessel one;
		one.outside = { piece };
		const Shape shape = revolved(one);
		REQUIRE(shape.patches.size() == 1);
		const Patch &p = shape.patches[0];
		REQUIRE(p.torus);
		for (int i = 0; i < 400; ++i) {
			// from a point outside, toward a point near the arc
			const double t = p.t_lo + (p.t_hi - p.t_lo) * (0.5 + 0.5 * unit(rng)), phi = std::numbers::pi * unit(rng);
			const double rho = p.major + p.minor * std::cos(t) + 0.3 * p.minor * unit(rng);
			const Vec3 target{ rho * std::cos(phi), p.cy + p.minor * std::sin(t) + 0.3 * p.minor * unit(rng), rho * std::sin(phi) };
			Vec3 from{ 8.0 * unit(rng), 7.0 + 8.0 * unit(rng), -12.0 };
			Vec3 u{ target.x - from.x, target.y - from.y, target.z - from.z };
			const double len = std::sqrt(u.x * u.x + u.y * u.y + u.z * u.z);
			u = Vec3{ u.x / len, u.y / len, u.z / len };
			const Landing l = trace(shape, from, u, 40.0, WATER);
			// the fine search: the quartic's first change of sign at a point on the arc
			const auto f = [&](double s) {
				const double x = from.x + s * u.x, y = from.y + s * u.y, z = from.z + s * u.z;
				const double r2 = x * x + z * z, dy = y - p.cy, q = r2 + dy * dy + p.major * p.major - p.minor * p.minor;
				return q * q - 4.0 * p.major * p.major * r2;
			};
			const auto on_arc = [&](double s) {
				const double x = from.x + s * u.x, y = from.y + s * u.y, z = from.z + s * u.z, r = std::sqrt(x * x + z * z);
				double d = std::fmod(std::atan2(y - p.cy, r - p.major) - p.t_lo, 2.0 * std::numbers::pi);
				d = d < 0.0 ? d + 2.0 * std::numbers::pi : d;
				return d <= p.t_hi - p.t_lo + 1e-9 || d > 2.0 * std::numbers::pi - 1e-9;
			};
			double first = -1.0;
			const int steps = 400000;
			double prev = f(0.0);
			for (int k = 1; k <= steps && first < 0.0; ++k) {
				const double s1 = 2.0 * len * k / steps, now = f(s1);
				if ((prev < 0.0) != (now < 0.0)) {
					double lo = 2.0 * len * (k - 1) / steps, hi = s1;
					for (int j = 0; j < 100; ++j) {
						const double mid = 0.5 * (lo + hi);
						((f(mid) < 0.0) == (prev < 0.0) ? lo : hi) = mid;
					}
					if (on_arc(lo)) {
						first = lo;
					}
				}
				prev = now;
			}
			CAPTURE(i);
			if (first < 0.0) {
				CHECK(l.meetings == 0);
				continue;
			}
			REQUIRE(l.meetings > 0);
			CHECK(l.chord[0] == doctest::Approx(first).epsilon(1e-9));
			++hits;
		}
	}
	CHECK(arcs == 3);
	CHECK(hits > 600);
}

TEST_CASE("trace: every surface of every vessel names the media on its two sides alike (Stage 24)") {
	// rays from anywhere through every kind: each crossing goes from the medium it was in to another
	std::mt19937_64 rng(7);
	std::uniform_real_distribution<double> unit(-1.0, 1.0);
	for (const auto &[kind, ml] : { std::pair{ "beaker", 50.0 }, std::pair{ "beaker", 250.0 }, std::pair{ "conical_flask", 250.0 }, std::pair{ "bottle", 500.0 } }) {
		CAPTURE(kind);
		const glass::Vessel v = vessel(kind, ml);
		const Shape shape = revolved(v);
		const double R = 0.6 * v.diameter;
		int crossings = 0, mismatched = 0;
		for (int i = 0; i < 4000; ++i) {
			const Vec3 target{ R * unit(rng), v.height * (0.5 + 0.55 * unit(rng)), R * unit(rng) };
			const Vec3 from{ 30.0 * unit(rng), 30.0 * unit(rng), -30.0 };
			Vec3 u{ target.x - from.x, target.y - from.y, target.z - from.z };
			const double len = std::sqrt(u.x * u.x + u.y * u.y + u.z * u.z);
			u = Vec3{ u.x / len, u.y / len, u.z / len };
			const Landing l = trace(shape, from, u, 40.0, WATER);
			crossings += l.meetings;
			mismatched += l.mismatched;
		}
		CHECK(crossings > 8000);
		CHECK(mismatched == 0);
	}
}

TEST_CASE("trace: a flask bends the view up and down where its cone does, not through its neck (Stage 24)") {
	const glass::Vessel v = vessel("conical_flask", 250);
	const Shape shape = revolved(v);
	// through the neck (upright): level in, level out
	const Landing neck = trace(shape, Vec3{ 0.6, 13.5, -1000.0 }, Vec3{ 0.0, 0.0, 1.0 }, 20.0, WATER);
	CHECK(neck.y == doctest::Approx(13.5).epsilon(1e-12));
	// through the cone, off its axis: the slant turns the ray up or down
	const Landing cone = trace(shape, Vec3{ 2.0, 5.0, -1000.0 }, Vec3{ 0.0, 0.0, 1.0 }, 20.0, WATER);
	REQUIRE(cone.lands);
	CHECK(std::fabs(cone.y - 5.0) > 0.05);
	CHECK(cone.mismatched == 0);
}
