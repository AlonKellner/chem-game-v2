// Pixel footprints (GAME_SPEC §12.3, Stage 20): a pixel is the light of all its rays. The truth: a
// grid of sub-pixel rays, bent by the reference's own exact tracer.
#include "lab/colour.hpp"
#include "lab/optics_scene.hpp"
#include "lab/prism_demo.hpp"

#include "chem/optics.hpp"

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
		it.wall_mm = 1.1; // a 250 mL beaker's wall (ISO 3819)
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

TEST_CASE("footprint: the truth's own tracer lands where the renderer's map does, away from the rims (Stage 20)") {
	// The renderer reads a map traced through surface patches (lab/optics_trace.hpp); the truth's tracer asks
	// only which medium a point is in. Away from the rims the two are the same geometry by different code,
	// and the map puts the image within a hundredth of a pixel of the screen of it (the error on the wall,
	// over how fast the landing moves across the vessel there).
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
			const double slope = std::max(1.0, std::fabs(lab::optics_exact_bend(scene, x + 1, LOW, nm) - lab::optics_exact_bend(scene, x - 1, LOW, nm)) / 2.0);
			CHECK(std::fabs(lab::optics_exact_bend(scene, x, LOW, nm) - lab::optics_bend(scene, x, LOW, nm).x) / slope < 0.01);
			++compared;
		}
	}
	CHECK(compared > 150);
}

TEST_CASE("footprint: before an even wall the truth with one ray is the renderer's pixel; with more it converges (Stage 20)") {
	const OpticsScene scene = scene_with({ { 0.0f, 0.0f, 1000.0f, 1.0f, 0.0 } });
	for (int x = static_cast<int>(X) + 10; x < static_cast<int>(X + W) - 10; x += 7) {
		CAPTURE(x);
		CHECK(lab::optics_area_through(scene, x, LOW, 550.0, 1) == doctest::Approx(lab::optics_through(scene, x, LOW, 550.0)).epsilon(2e-3));
		const double fine = lab::optics_area_through(scene, x, LOW, 550.0, 32);
		CHECK(lab::optics_area_through(scene, x, LOW, 550.0, 16) == doctest::Approx(fine).epsilon(2e-3));
	}
}

TEST_CASE("footprint: a vessel's map lands its rays where the closed form does: the ray keeps d / n from the axis (Stage 20)") {
	// Through concentric circles a ray's distance from the axis, times the index, is kept: it meets
	// each surface of radius r at asin(d / (n1 r)) and leaves at asin(d / (n2 r)). Summed over the four
	// crossings that turns it by delta, and it lands (d - D sin delta) / cos delta from the axis.
	const OpticsScene scene = scene_with({});
	namespace L = lab::optics_layout;
	const auto vec = [&](int i) { return &scene.table[static_cast<size_t>(i) * 4]; };
	const float *rect = vec(L::HEADER_VECS + L::RECT), *form = vec(L::HEADER_VECS + L::FORM), *refract = vec(L::HEADER_VECS + L::REFRACT);
	const double cm = form[1], radius = rect[2] * 0.5 * cm, glass = refract[0], inner = radius - glass, wall = vec(2)[0] * cm; // the wall, in the vessel's own centimetres
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
			// the map is true to a hundredth of a pixel of the screen: on the wall, that times how fast the
			// landing moves across the vessel (the tracer itself: to 1e-10, test_trace.cpp)
			const double slope = std::max(1.0, std::fabs(lab::optics_bend(scene, x + 1, LOW, nm).x - lab::optics_bend(scene, x - 1, LOW, nm).x) / 2.0);
			CHECK(std::fabs(lab::optics_bend(scene, x, LOW, nm).x - (rect[0] + rect[2] * 0.5 + (d < 0.0 ? -landed : landed) / cm)) / slope < 0.01);
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

namespace {

// how far apart two colours are to the eye, as the screen shows them
double eye(const double a[3], const double b[3]) {
	const auto seen = [](const double c[3]) {
		return lab::shown(chem::optics::Xyz{ 0.4124564 * c[0] + 0.3575761 * c[1] + 0.1804375 * c[2], 0.2126729 * c[0] + 0.7151522 * c[1] + 0.0721750 * c[2],
				0.0193339 * c[0] + 0.1191920 * c[1] + 0.9503041 * c[2] });
	};
	return lab::eye_difference(seen(a), seen(b));
}

// 100 mL of copper sulfate solution in glass
const ItemView &blue_vessel() {
	static const ItemView v = [] {
		ItemView it = water_vessel();
		it.contents.push_back(substance("copper_sulfate_aq", 'a', 0.03, 4.8));
		return it;
	}();
	return v;
}

} // namespace

TEST_CASE("footprint: every pixel's colour is what its rays bring, before a wall of any drawing: many shapes, a huge one, overlaps, edges anywhere (Stage 20)") {
	// The wall: a grid of forty lines at odd places and widths, a rectangle larger than any the scene's table
	// once took, two that overlap it and each other, a line across. The truth traces 64 rays a pixel and reads
	// the wall from the shapes themselves, in painting order; the renderer reads its map and its table.
	lab::OpticsRoom r = room();
	r.shapes.push_back(lab::OpticsShape{ 20.0f, 150.0f, 700.0f, 400.0f, 0.5 }); // 280,000 square pixels
	for (int k = 0; k < 40; ++k) {
		r.shapes.push_back(lab::OpticsShape{ 31.3f + 7.77f * static_cast<float>(k), 0.0f, 0.4f + 0.11f * static_cast<float>(k % 7), 1000.0f, 0.15 + 0.01 * (k % 5) });
	}
	r.shapes.push_back(lab::OpticsShape{ 80.5f, 160.25f, 60.0f, 30.5f, 0.9 });
	r.shapes.push_back(lab::OpticsShape{ 120.0f, 170.0f, 90.0f, 12.5f, 0.3 });
	r.shapes.push_back(lab::OpticsShape{ 0.0f, 199.6f, 1000.0f, 0.8f, 0.05 });
	OpticsScene scene;
	lab::optics_compose(scene, r, { lab::OpticsBody{ &blue_vessel(), X, Y, W, H } });
	REQUIRE(lab::check_O1(scene) == "");
	REQUIRE(scene.drawn.size() == 5 * r.shapes.size());
	// the liquid's surface is in one of these rows, the vessel's top and floor in two others
	const int surface = static_cast<int>(scene.table[static_cast<size_t>(lab::optics_layout::HEADER_VECS + lab::optics_layout::FORM) * 4 + 2]);
	double worst = 0.0, sum = 0.0;
	int compared = 0;
	for (const int y : { static_cast<int>(Y), surface, 165, 175, 199, 200, static_cast<int>(Y + H) - 1 }) {
		for (int x = static_cast<int>(X) - 1; x <= static_cast<int>(X + W); x += (y == 199 || y == surface ? 1 : 3)) {
			if (lab::optics_cover(scene, x, y) <= 0.0) {
				continue;
			}
			double shown[3], truth[3];
			lab::optics_truth(scene, x, y, shown);
			lab::optics_area_truth(scene, x, y, 8, truth);
			const double e = eye(shown, truth);
			CAPTURE(x);
			CAPTURE(y);
			CHECK(e < lab::EYE_VISIBLE);
			worst = std::max(worst, e);
			sum += e;
			++compared;
		}
	}
	CHECK(compared > 250);
	CHECK(sum / compared < 1.0);
	MESSAGE(compared << " pixels against 64 rays each: worst " << worst << " on the eye metric (visible above 8), mean " << sum / compared);
}

TEST_CASE("footprint: a band's light is its wavelengths' together: the wall read over the band's sweep, each step by its share of the energy (Stage 20)") {
	// Through a prism a slit is seen in a narrow window of wavelengths, and a sixteenth of the light's energy
	// sweeps the view hundreds of pixels (its last sixteenth, the whole red tail). A band read at once must be
	// the mean of 4,000 wavelengths of it, even in energy.
	lab::PrismDemo demo;
	const OpticsScene scene = demo.optics();
	int compared = 0, off = 0;
	double worst = 0.0;
	for (int x = 890; x < 1190; x += 3) {
		if (lab::optics_cover(scene, x, 540) <= 0.0) {
			continue;
		}
		for (int band = 0; band < 16; ++band) {
			const double u0 = band / 16.0, u1 = (band + 1) / 16.0, swept = lab::optics_band(scene, x, 540, u0, u1, 0.5);
			double fine = 0.0;
			constexpr int N = 4000;
			for (int k = 0; k < N; ++k) {
				fine += lab::optics_through(scene, x, 540, lab::optics_share_nm(scene, u0 + (u1 - u0) * (k + 0.5) / N));
			}
			fine /= N;
			const double error = std::fabs(swept - fine) / (std::fabs(fine) + 0.02); // against the slit's 12 of the room's white
			worst = std::max(worst, error);
			off += error > 0.03 ? 1 : 0;
			++compared;
		}
	}
	CHECK(compared > 1000);
	CHECK(off * 200 <= compared); // where the view turns round at the least deviation, a step is less true
	MESSAGE(compared << " bands of a sixteenth: " << off << " more than 3% off, the worst " << worst);
}

TEST_CASE("footprint: a pixel through a map of one piece is read by a reading of its own, which is the general one's light (Stage 20)") {
	// What a pass costs on the GPU follows how much there is to do in a band, done or not: a vessel's pixels
	// are read with nothing of what they never need. Where a pixel's rays all go one way the two readings are
	// the same arithmetic; at a rim, each side of the route's change is its own ray's in the one, and carried
	// on from its neighbour in the other.
	lab::OpticsRoom r = room();
	for (int k = 0; k < 40; ++k) {
		r.shapes.push_back(lab::OpticsShape{ 31.3f + 7.77f * static_cast<float>(k), 0.0f, 0.4f + 0.11f * static_cast<float>(k % 7), 1000.0f, 0.15 + 0.01 * (k % 5) });
	}
	OpticsScene scene;
	lab::optics_compose(scene, r, { lab::OpticsBody{ &blue_vessel(), X, Y, W, H } });
	const int surface = static_cast<int>(scene.table[static_cast<size_t>(lab::optics_layout::HEADER_VECS + lab::optics_layout::FORM) * 4 + 2]);
	int same = 0, near = 0;
	for (const int y : { static_cast<int>(Y), surface, 175, LOW }) {
		for (int x = static_cast<int>(X) - 1; x <= static_cast<int>(X + W); ++x) {
			if (lab::optics_cover(scene, x, y) <= 0.0) {
				continue;
			}
			for (const double nm : { 430.0, 550.0, 690.0 }) {
				const double own = lab::optics_through(scene, x, y, nm), general = lab::optics_through_general(scene, x, y, nm);
				CAPTURE(x);
				CAPTURE(y);
				CAPTURE(nm);
				if (x >= static_cast<int>(X) + 2 && x < static_cast<int>(X + W) - 2) {
					CHECK(own == doctest::Approx(general).epsilon(1e-9));
					++same;
				} else {
					CHECK(std::fabs(own - general) < 0.02);
					++near;
				}
			}
		}
	}
	CHECK(same > 900);
	CHECK(near > 30);
}
