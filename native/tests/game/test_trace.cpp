// The tracer (GAME_SPEC §12.3, Stage 20): rays through surface patches, held to closed forms.
#include "lab/optics_trace.hpp"

#include <doctest/doctest.h>

#include <cmath>

using namespace lab::trace;

namespace {

Landing level_ray(const Shape &shape, double x, double y, double wall, double n_glass, double n_core) {
	return trace(shape, Vec3{ x, y, -1000.0 }, Vec3{ 0.0, 0.0, 1.0 }, wall, { 1.0, n_glass, n_core, 1.0 });
}

} // namespace

TEST_CASE("trace: through a glass cylinder of water a ray lands where the closed form puts it, and crosses the chord it gives (Stage 20)") {
	// Through concentric circles a ray keeps its distance from the axis times the index: it meets a surface
	// of radius r at asin(d / (n1 r)) and leaves at asin(d / (n2 r)). Four crossings turn it by delta; it
	// lands (d - D sin delta) / cos delta from the axis; inside the core it is d / n from the axis.
	const double R = 3.2, glass = 0.2, ri = R - glass, wall = 20.0;
	const Shape shape = cylinder(R, glass);
	for (const double n_glass : { 1.47, 1.52 }) {
		for (const double n_core : { 1.0, 1.333 }) {
			for (double d = -2.9; d < 2.95; d += 0.37) {
				const double a = std::fabs(d);
				if (!(a < n_glass * ri && a < n_core * ri)) {
					continue; // turned back at the inner surface: no closed form
				}
				const double turn = 2.0 * (std::asin(a / R) - std::asin(a / (n_glass * R)) + std::asin(a / (n_glass * ri)) - std::asin(a / (n_core * ri)));
				const double landed = (a - wall * std::sin(turn)) / std::cos(turn);
				const Landing l = level_ray(shape, d, 0.7, wall, n_glass, n_core);
				CAPTURE(d);
				CAPTURE(n_core);
				REQUIRE(l.lands);
				CHECK(l.x == doctest::Approx(d < 0.0 ? -landed : landed).epsilon(1e-10));
				CHECK(l.y == doctest::Approx(0.7).epsilon(1e-12)); // upright: nothing bends up or down
				CHECK(l.chord[CORE] == doctest::Approx(2.0 * std::sqrt(ri * ri - (a / n_core) * (a / n_core))).epsilon(1e-10));
				CHECK(l.meetings == 4);
			}
		}
	}
	// a ray past the rim meets nothing and goes straight
	const Landing past = level_ray(shape, 3.3, 0.0, wall, 1.47, 1.333);
	CHECK(past.meetings == 0);
	CHECK(past.x == 3.3);
	CHECK(past.pass == 1.0);
}

TEST_CASE("trace: Fresnel at each crossing; a ray that cannot pass is turned back, all of it (Stage 20)") {
	// straight through the middle of a ball: two crossings at normal incidence, each losing ((n - 1) / (n + 1))^2
	const double n = 1.5, r = ((n - 1.0) / (n + 1.0)) * ((n - 1.0) / (n + 1.0));
	const Landing through = level_ray(ball(2.0), 0.0, 0.0, 30.0, n, 1.0);
	CHECK(through.pass == doctest::Approx((1.0 - r) * (1.0 - r)).epsilon(1e-12));
	CHECK(through.chord[GLASS] == doctest::Approx(4.0).epsilon(1e-12));
	// a right-angled prism of glass, its long face at 45 degrees to the ray: inside, the ray meets it past the
	// critical angle (41.8 degrees) and is turned back whole: no light lost there, and it leaves sideways
	const double corners[3][2] = { { -1.0, -1.0 }, { 1.0, -1.0 }, { -1.0, 1.0 } };
	const Landing turned = level_ray(prism(corners), -0.5, 0.0, 30.0, n, 1.0);
	CHECK_FALSE(turned.lands);
	CHECK(turned.meetings == 3);
	CHECK(turned.pass == doctest::Approx((1.0 - r) * (1.0 - r)).epsilon(1e-12)); // in and out at normal incidence; the turn costs nothing
}

TEST_CASE("trace: a prism of 60 degrees deviates least by 2 asin(n sin 30) - 60 degrees (Stage 20)") {
	const double n = 1.78472, side = 5.0, h = side * std::sqrt(3.0) / 2.0;
	const double least = 2.0 * std::asin(n * 0.5) - 3.14159265358979323846 / 3.0;
	double smallest = 10.0;
	for (int k = 0; k < 2000; ++k) {
		const double turn = 6.283185307179586 * k / 2000.0;
		double corners[3][2];
		const double base[3][2] = { { -side / 2.0, -h / 3.0 }, { side / 2.0, -h / 3.0 }, { 0.0, 2.0 * h / 3.0 } };
		for (int c = 0; c < 3; ++c) {
			corners[c][0] = base[c][0] * std::cos(turn) - base[c][1] * std::sin(turn);
			corners[c][1] = base[c][0] * std::sin(turn) + base[c][1] * std::cos(turn);
		}
		const Landing near = level_ray(prism(corners), 0.3, 0.0, 100.0, n, 1.0), far = level_ray(prism(corners), 0.3, 0.0, 200.0, n, 1.0);
		if (near.lands && far.lands && near.meetings == 2) {
			smallest = std::min(smallest, std::fabs(std::atan2(far.x - near.x, 100.0)));
		}
	}
	CHECK(smallest == doctest::Approx(least).epsilon(1e-4)); // the turn is stepped, and the deviation is flat at its least
}

TEST_CASE("trace: rays bend up and down too: a ball of glass focuses at n R / (2 (n - 1)) from its centre (Stage 20)") {
	// the paraxial focus of a ball lens, from its centre; a ray at height h, across or up, crosses the axis there
	const double n = 1.5, R = 2.0, focus = n * R / (2.0 * (n - 1.0));
	for (const double wall : { 4.0, 9.0 }) {
		const Landing across = level_ray(ball(R), 1e-3, 0.0, wall, n, 1.0), up = level_ray(ball(R), 0.0, 1e-3, wall, n, 1.0);
		CHECK(across.x == doctest::Approx(1e-3 * (1.0 - wall / focus)).epsilon(1e-5));
		CHECK(up.y == doctest::Approx(1e-3 * (1.0 - wall / focus)).epsilon(1e-5));
		CHECK(std::fabs(across.y) < 1e-15);
		CHECK(std::fabs(up.x) < 1e-15);
	}
}
