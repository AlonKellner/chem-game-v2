// Pixel footprints (GAME_SPEC §12.3, Stage 20): a pixel is the light of all its rays. The truth: a
// grid of sub-pixel rays, bent by the reference's own exact tracer.
#include "lab/optics_scene.hpp"

#include <doctest/doctest.h>

#include <cmath>
#include <vector>

using lab::ItemView;
using lab::OpticsScene;
using lab::SubstanceView;

namespace {

constexpr float X = 100, Y = 100, W = 90, H = 112.5f; // a 250 mL vessel drawn 90 wide, its axis at x = 145
const int LOW = 200; // a row deep in its liquid

lab::OpticsRoom room() {
	lab::OpticsRoom r;
	r.w = r.h = 1000;
	r.bench = 0.7305;
	r.elsewhere = 0.02;
	return r;
}

SubstanceView substance(const char *species, char phase, double mol, double g, bool wall = false) {
	SubstanceView s;
	s.species = species;
	s.phase = phase;
	s.mol = mol;
	s.g = g;
	s.wall = wall;
	return s;
}

// 100 mL of water in 50 g of glass
const ItemView &water_vessel() {
	static const ItemView v = [] {
		ItemView it;
		it.id = "flask";
		it.kind = "vessel";
		it.volume_ml = 250.0;
		it.celsius = 20.0;
		it.liquid_ml = it.condensed_ml = 100.0;
		it.contents = { substance("water", 'l', 5.5, 100.0), substance("glass", 's', 0.83, 50.0, true) };
		return it;
	}();
	return v;
}

OpticsScene scene_with(std::vector<lab::OpticsShape> shapes) {
	lab::OpticsRoom r = room();
	r.shapes = std::move(shapes);
	OpticsScene scene;
	lab::optics_compose(scene, r, { lab::OpticsBody{ &water_vessel(), X, Y, W, H } });
	REQUIRE(lab::check_O1(scene) == "");
	return scene;
}

} // namespace

TEST_CASE("footprint: the truth's own tracer lands where the renderer's does, wherever the renderer's model holds (Stage 20)") {
	// The renderer bends by the cylinder's invariant (the ray keeps d / n from the axis); the truth's
	// tracer intersects circles and applies vector Snell. Away from the rims, where nothing is turned
	// back, the two are the same geometry by different arithmetic.
	const OpticsScene scene = scene_with({});
	int compared = 0;
	for (int x = static_cast<int>(X); x < static_cast<int>(X + W); ++x) {
		const double off = std::fabs(x + 0.5 - (X + W / 2)) / (W / 2);
		if (off > 0.8) {
			continue;
		}
		for (const double nm : { 420.0, 550.0, 680.0 }) {
			CAPTURE(x);
			CAPTURE(nm);
			CHECK(lab::optics_exact_bend(scene, x, LOW, nm) == doctest::Approx(lab::optics_bend(scene, x, LOW, nm).x).epsilon(1e-9));
			++compared;
		}
	}
	CHECK(compared > 150);
}

TEST_CASE("footprint: the truth with one ray is the renderer's centre; with more it converges (Stage 20)") {
	const OpticsScene scene = scene_with({ { 0.0f, 0.0f, 1000.0f, 1.0f, 0.0 } });
	for (int x = static_cast<int>(X) + 10; x < static_cast<int>(X + W) - 10; x += 7) {
		CAPTURE(x);
		CHECK(lab::optics_area_through(scene, x, LOW, 550.0, 1) == doctest::Approx(lab::optics_through(scene, x, LOW, 550.0)).epsilon(1e-9));
		const double fine = lab::optics_area_through(scene, x, LOW, 550.0, 32);
		CHECK(lab::optics_area_through(scene, x, LOW, 550.0, 16) == doctest::Approx(fine).epsilon(2e-3));
	}
}

TEST_CASE("footprint: the tracer lands a cylinder's rays where the closed form does: the ray keeps d / n from the axis (Stage 20)") {
	// Through concentric circles a ray's distance from the axis, times the index, is kept: it meets
	// each surface of radius r at asin(d / (n1 r)) and leaves at asin(d / (n2 r)). Summed over the four
	// crossings that turns it by delta, and it lands (d - D sin delta) / cos delta from the axis.
	const OpticsScene scene = scene_with({});
	namespace L = lab::optics_layout;
	const auto vec = [&](int i) { return &scene.table[static_cast<size_t>(i) * 4]; };
	const float *rect = vec(L::HEADER_VECS + L::RECT), *form = vec(L::HEADER_VECS + L::FORM), *refract = vec(L::HEADER_VECS + L::REFRACT);
	const double cm = form[1], radius = rect[2] * 0.5 * cm, glass = refract[0], inner = radius - glass, wall = vec(2)[0];
	const auto row = [&](int which, double nm) {
		const size_t base = static_cast<size_t>(which) * L::ROW;
		const auto i = static_cast<size_t>(nm - 380.0);
		const double f = nm - 380.0 - static_cast<double>(i);
		return scene.rows[base + i] * (1.0 - f) + scene.rows[base + i + 1] * f;
	};
	for (const double nm : { 430.0, 590.0 }) {
		const double n_glass = row(L::WALL_INDEX_ROW, nm), n_water = row(L::LIQUID_INDEX_ROW, nm);
		for (int x = static_cast<int>(X) + 8; x < static_cast<int>(X + W) - 8; x += 3) {
			const double d = (x + 0.5 - (rect[0] + rect[2] * 0.5)) * cm;
			const double a = std::fabs(d);
			if (!(a < n_glass * inner && a < n_water * inner)) {
				continue; // turned back at the inner surface: no closed form
			}
			const double turn = 2.0 * (std::asin(a / radius) - std::asin(a / (n_glass * radius)) + std::asin(a / (n_glass * inner)) - std::asin(a / (n_water * inner)));
			const double landed = (a - wall * std::sin(turn)) / std::cos(turn);
			CAPTURE(x);
			CHECK(lab::optics_bend(scene, x, LOW, nm).x == doctest::Approx(rect[0] + rect[2] * 0.5 + (d < 0.0 ? -landed : landed) / cm).epsilon(1e-9));
		}
	}
}

namespace {

// a renderer that brightens where its rays crowd: the light times how far apart its neighbours land (what
// O7 rules out: bending makes no brightness)
double crowded(const OpticsScene &scene, int x, int y, double nm) {
	return lab::optics_through(scene, x, y, nm) * std::fabs(lab::optics_bend(scene, x + 1, y, nm).x - lab::optics_bend(scene, x, y, nm).x);
}

// a renderer that reads only each pixel's centre ray (what O8 rules out: a line between two centres is lost)
double centred(const OpticsScene &scene, int x, int y, double nm) {
	return lab::optics_area_through(scene, x, y, nm, 1);
}

lab::OpticsShape line_at(float x) {
	return lab::OpticsShape{ x, 0.0f, 0.3f, 1000.0f, 0.05 };
}

} // namespace

TEST_CASE("O7: brightness is neither made nor lost by bending: a uniform wall through clear glass and water, and a prism, stays as it is (Stage 20)") {
	const OpticsScene vessel = scene_with({ line_at(150.0f) });
	CHECK(lab::check_O7(vessel, static_cast<int>(X) - 1, 140, static_cast<int>(X + W) + 1, 210, 0.6) == "");
	// it fires on a renderer that brightens where its rays crowd
	CHECK(lab::check_O7(vessel, static_cast<int>(X) - 1, 140, static_cast<int>(X + W) + 1, 210, 0.6, crowded).rfind("O7", 0) == 0);
}

TEST_CASE("O8: a thin line keeps its light, wherever the glass puts it: summed over the pixels it is what its rays bring (Stage 20)") {
	const OpticsScene plain = scene_with({});
	int lost = 0;
	for (const float x : { 120.3f, 141.7f, 150.2f, 170.9f, 186.4f }) {
		CAPTURE(x);
		const OpticsScene lined = scene_with({ line_at(x) });
		CHECK(lab::check_O8(lined, plain, LOW, static_cast<int>(X) - 2, static_cast<int>(X + W) + 2, 550.0) == "");
		lost += lab::check_O8(lined, plain, LOW, static_cast<int>(X) - 2, static_cast<int>(X + W) + 2, 550.0, centred).empty() ? 0 : 1;
	}
	// it fires on centre sampling: a line thinner than a pixel is lost between centres, or counted whole
	CHECK(lost >= 3);
	MESSAGE("centre sampling misses " << lost << " of the 5 lines");
}
