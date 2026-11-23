// What lies in a vessel, at any turn (GAME_SPEC §13, §12.3, Stage 30): levels, the sediment's flat section and
// pile, the vortex's cone, the lip; held to closed forms and to its own invariants (H1, H2).
#include "lab/contents.hpp"

#include <doctest/doctest.h>

#include <cmath>
#include <vector>

using lab::contents::Lying;
using lab::glass::Vessel;

namespace {

constexpr double PI = 3.14159265358979323846;

// a plain cylinder: a flat floor and an upright wall, no glass to speak of
Vessel cylinder(double radius, double height) {
	Vessel v;
	v.kind = "cylinder";
	v.diameter = 2.0 * radius;
	v.height = height;
	lab::glass::Piece floor, wall;
	floor.a = { 0.0, 0.0 };
	floor.b = { radius, 0.0 };
	wall.a = { radius, 0.0 };
	wall.b = { radius, height };
	v.inside = { floor, wall };
	v.outside = v.inside;
	return v;
}

const Vessel &made(const char *kind, double ml) {
	const Vessel *v = lab::glass::find(kind, ml);
	REQUIRE(v != nullptr);
	return *v;
}

} // namespace

TEST_CASE("contents: upright, a level is the vessel's own; upside down, it lies from the mouth (Stage 30)") {
	for (const auto &[kind, ml] : std::vector<std::pair<const char *, double>>{ { "beaker", 250 }, { "conical_flask", 250 }, { "bottle", 500 } }) {
		const Vessel &v = made(kind, ml);
		CAPTURE(kind);
		for (const double share : { 0.1, 0.4, 0.8 }) {
			const double cm3 = share * v.capacity();
			CHECK(lab::contents::level_of(v, 0.0, cm3) == doctest::Approx(v.level(cm3)).epsilon(1e-9));
			CHECK(lab::contents::below(v, 0.0, v.level(cm3)) == doctest::Approx(cm3).epsilon(1e-9));
			// turned half round, the room's up is the vessel's down: its mouth is at the level -height
			CHECK(lab::contents::level_of(v, PI, cm3) + v.height == doctest::Approx(v.head_inverted(cm3)).epsilon(1e-7));
		}
	}
}

TEST_CASE("contents: a cylinder turned holds under a level what its geometry says: a slant cut, and a hoof (Stage 30)") {
	const Vessel v = cylinder(3.0, 10.0);
	for (const double turn : { 0.2, 0.5, -0.5 }) {
		CAPTURE(turn);
		// a level through the axis at the height 4, cutting neither end: the cylinder under that height
		CHECK(lab::contents::below(v, turn, 4.0 * std::cos(turn)) == doctest::Approx(PI * 9.0 * 4.0).epsilon(1e-6));
		// a level through the middle of the floor: a hoof, two thirds of R^3 times the slant
		CHECK(lab::contents::below(v, turn, 0.0) == doctest::Approx(2.0 / 3.0 * 27.0 * std::tan(std::fabs(turn))).epsilon(1e-5));
	}
	// and the surface of the slant cut is an ellipse: as wide as the cylinder over the cosine, as deep as it is round
	const lab::contents::Cut c = lab::contents::cut(v, 0.5, 4.0 * std::cos(0.5));
	REQUIRE(c.any);
	CHECK(c.half == doctest::Approx(3.0 / std::cos(0.5)).epsilon(1e-4));
	CHECK(c.area == doctest::Approx(PI * 9.0 / std::cos(0.5)).epsilon(2e-3));
	CHECK(c.deep() == doctest::Approx(3.0).epsilon(2e-3));
	CHECK(c.mid_x == doctest::Approx(0.0).epsilon(1e-4));
	CHECK(c.mid_y == doctest::Approx(4.0).epsilon(1e-4));
}

TEST_CASE("contents: a level rises with what is held and moves smoothly with the turn (Stage 30)") {
	const Vessel &v = made("conical_flask", 250);
	double before = -1e9;
	for (int i = 1; i <= 20; ++i) {
		const double level = lab::contents::level_of(v, 0.7, v.capacity() * i / 40.0);
		CHECK(level > before);
		before = level;
	}
	// a hundredth of a radian moves the surface's middle by little
	double x = 0.0, y = 0.0;
	for (int i = 0; i <= 150; ++i) {
		const double turn = 0.01 * i;
		const Lying l = lab::contents::lying(v, turn, 80.0, 0.0, 0.0, 1);
		const lab::contents::Cut c = lab::contents::cut(v, turn, l.liquid_level);
		REQUIRE(c.any);
		if (i > 0) {
			CAPTURE(turn);
			CHECK(std::hypot(c.mid_x - x, c.mid_y - y) < 0.25);
		}
		x = c.mid_x;
		y = c.mid_y;
	}
}

TEST_CASE("H1, H2: what lies in a vessel adds up, at any turn: liquid, sediment, a pile no steeper than repose, a vortex within the liquid (Stage 30)") {
	int tried = 0;
	for (const auto &[kind, ml] : std::vector<std::pair<const char *, double>>{ { "beaker", 250 }, { "conical_flask", 250 }, { "bottle", 500 } }) {
		const Vessel &v = made(kind, ml);
		for (const double turn : { 0.0, 0.3, 0.8, 1.3, -0.6 }) {
			for (const double liquid : { 0.0, 0.12, 0.45 }) {
				for (const double solid : { 0.0, 0.004, 0.05 }) {
					for (const double vortex : { 0.0, 0.5, 1.0 }) {
						// (no more than it holds below its lip at this turn: the rest would have poured)
						const double room = lab::contents::held_at(v, turn, 1);
						const double liquid_cm3 = std::min(liquid * v.capacity(), 0.6 * room), solid_cm3 = std::min(solid * v.capacity(), 0.1 * room);
						const Lying l = lab::contents::lying(v, turn, liquid_cm3, solid_cm3, vortex, 1);
						CAPTURE(kind);
						CAPTURE(turn);
						CAPTURE(liquid_cm3);
						CAPTURE(solid_cm3);
						CAPTURE(vortex);
						CHECK(lab::contents::check_H1(v, l, liquid_cm3, solid_cm3) == "");
						CHECK(lab::contents::check_H2(l) == "");
						++tried;
					}
				}
			}
		}
	}
	CHECK(tried == 3 * 5 * 3 * 3 * 3);
	// they fire: a surface a few millimetres too high holds more than there is; a pile twice as high is too steep;
	// a vortex deeper than the liquid
	const Vessel &beaker = made("beaker", 250);
	Lying wrong = lab::contents::lying(beaker, 0.4, 100.0, 5.0, 0.5, 1);
	REQUIRE(lab::contents::check_H1(beaker, wrong, 100.0, 5.0) == "");
	wrong.liquid_level += 0.3;
	CHECK(lab::contents::check_H1(beaker, wrong, 100.0, 5.0).rfind("H1", 0) == 0);
	wrong = lab::contents::lying(beaker, 0.0, 100.0, 40.0, 0.5, 1);
	REQUIRE(lab::contents::check_H2(wrong) == "");
	wrong.pile.height *= 2.0;
	CHECK(lab::contents::check_H2(wrong).rfind("H2", 0) == 0);
	wrong = lab::contents::lying(beaker, 0.0, 100.0, 0.0, 1.0, 1);
	wrong.vortex.height = wrong.depth * 1.1;
	CHECK(lab::contents::check_H2(wrong).rfind("H2", 0) == 0);
}

TEST_CASE("contents: a little powder on a flat floor is a low cone across the floor; more raises a flat section under a pile at repose (Stage 30)") {
	const Vessel &v = made("beaker", 250);
	const Lying little = lab::contents::lying(v, 0.0, 0.0, 1.0, 0.0, 1);
	REQUIRE(little.solid);
	CHECK(little.solid_level - little.lowest < 0.02);
	// (across the floor's flat: the glass rounds up to the wall beyond it)
	CHECK(little.pile.across > 0.6 * v.radius_inside(v.floor + 0.3 * v.height));
	CHECK(little.pile.across < v.radius_inside(v.floor + 0.3 * v.height));
	CHECK(little.pile.height < lab::contents::REPOSE * little.pile.across);
	CHECK(little.pile.volume() == doctest::Approx(1.0 / lab::contents::PACKING).epsilon(0.02));
	const Lying more = lab::contents::lying(v, 0.0, 0.0, 40.0, 0.0, 1);
	CHECK(more.solid_level - more.lowest > 0.5);
	CHECK(more.pile.height == doctest::Approx(lab::contents::REPOSE * std::min(more.pile.across, more.pile.deep)).epsilon(1e-3));
	// turned, it lies in the low corner: its middle off the axis, toward the side turned down
	const Lying turned = lab::contents::lying(v, 0.9, 0.0, 3.0, 0.0, 1);
	CHECK(turned.pile.x > 0.3 * v.radius_inside(v.floor + 0.3 * v.height));
}

TEST_CASE("contents: the more a vessel is turned the less it holds below its lip, to nothing at its full tilt; the liquid stands over the lip when there is more (Stage 30)") {
	for (const auto &[kind, ml] : std::vector<std::pair<const char *, double>>{ { "beaker", 250 }, { "conical_flask", 250 }, { "bottle", 500 } }) {
		const Vessel &v = made(kind, ml);
		CAPTURE(kind);
		const double full = lab::contents::full_tilt(v, 1);
		CHECK(full > 1.2);
		CHECK(full < 3.1);
		double before = lab::contents::held_at(v, 0.0, 1);
		CHECK(before == doctest::Approx(v.capacity()).epsilon(1e-6));
		for (int i = 1; i <= 10; ++i) {
			const double held = lab::contents::held_at(v, full * i / 10.0, 1);
			CHECK(held <= before + 1e-9);
			before = held;
		}
		CHECK(before < 1e-6 * v.capacity());
		// poured to the left, the mirror of it
		CHECK(lab::contents::full_tilt(v, -1) == doctest::Approx(-full).epsilon(1e-9));
		CHECK(lab::contents::held_at(v, -0.7, -1) == doctest::Approx(lab::contents::held_at(v, 0.7, 1)).epsilon(1e-9));
		// at a turn that holds half of what is in it, the rest stands over the lip
		const double cm3 = 0.5 * v.capacity();
		double lo = 0.0, hi = full;
		for (int i = 0; i < 40; ++i) {
			const double mid = 0.5 * (lo + hi);
			(lab::contents::held_at(v, mid, 1) > 0.5 * cm3 ? lo : hi) = mid;
		}
		CHECK(lab::contents::lying(v, hi, cm3, 0.0, 0.0, 1).head > 0.0);
		CHECK(lab::contents::lying(v, 0.2 * hi, 0.2 * cm3, 0.0, 0.0, 1).head < 0.0);
	}
}

TEST_CASE("contents: a vortex is a cone of the liquid's own proportions, the cube of its share of the largest that fits; the liquid stands higher round it (Stage 30)") {
	const Vessel &v = made("beaker", 250);
	const Lying still = lab::contents::lying(v, 0.0, 150.0, 0.0, 0.0, 1);
	const Lying half = lab::contents::lying(v, 0.0, 150.0, 0.0, 0.5, 1);
	const Lying whole = lab::contents::lying(v, 0.0, 150.0, 0.0, 1.0, 1);
	CHECK(still.vortex.height == 0.0);
	CHECK(half.liquid_level > still.liquid_level);
	CHECK(whole.liquid_level > half.liquid_level);
	// its volume: the surface's area times the depth under its middle, over three, times the share cubed
	CHECK(half.vortex.volume() == doctest::Approx(0.125 * half.surface_area * half.depth / 3.0).epsilon(1e-6));
	CHECK(whole.vortex.height == doctest::Approx(whole.depth).epsilon(1e-9));
	CHECK(whole.vortex.across == doctest::Approx(v.radius_inside(whole.liquid_level)).epsilon(5e-3));
	// what the liquid rose by is the vortex's volume over the surface: a third of the depth, for the whole one
	CHECK(whole.liquid_level - still.liquid_level == doctest::Approx(whole.depth / 3.0).epsilon(0.03));
}

TEST_CASE("contents: upside down, the liquid stands over the mouth as the Stage 27 head says: half a turn of the one model is the placed pour's geometry (Stage 30)") {
	constexpr double HALF_TURN = 3.14159265358979323846;
	for (const char *kind : { "beaker", "conical_flask", "bottle" }) {
		const lab::glass::Vessel *g = lab::glass::find(kind, kind == std::string("bottle") ? 500.0 : 250.0);
		REQUIRE(g != nullptr);
		for (const double share : { 0.05, 0.3, 0.6, 0.9 }) {
			const double cm3 = share * g->capacity();
			const lab::contents::Lying l = lab::contents::lying(*g, HALF_TURN, cm3, 0.0, 0.0, 1);
			CAPTURE(kind);
			CAPTURE(share);
			CHECK(l.head == doctest::Approx(g->head_inverted(cm3)).epsilon(2e-3));
		}
	}
}
