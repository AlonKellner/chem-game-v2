// Layers of refraction (GAME_SPEC §12.3, Stage 21): what is held is seen in front of what stands. A ray
// is followed layer by layer by the bodies' maps (the chain); the truth traces every body of the one world
// by its own tracer.
#include "lab/colour.hpp"
#include "lab/optics_scene.hpp"

#include "chem/builtin_ids.hpp"
#include "chem/optics.hpp"

#include "sparse.hpp"

#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

using lab::ItemView;
using lab::OpticsBody;
using lab::OpticsScene;
using lab::SubstanceView;

namespace {

// a 250 mL vessel standing, drawn 90 wide, its axis at x = 245; a 50 mL one in the hand, drawn 70 wide
constexpr float X = 200, Y = 100, W = 90, H = 112.5f;
constexpr float HELD_W = 70, HELD_H = 87.5f;
constexpr double WALL = 0.7305;

lab::OpticsRoom room() {
	lab::OpticsRoom r;
	r.w = r.h = 1000;
	r.bench = WALL;
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

// a glass vessel of `volume` mL holding `liquid` mL of water, with copper sulfate at `molar`
ItemView vessel(double volume, double liquid, double molar, double wall_mm) {
	ItemView v;
	v.id = "vessel";
	v.kind = "vessel";
	v.volume_ml = volume;
	v.celsius = 20.0;
	v.liquid_ml = v.condensed_ml = liquid;
	if (liquid > 0.0) {
		v.contents.push_back(substance("water", 'l', liquid / 18.0, liquid));
	}
	if (molar > 0.0) {
		v.contents.push_back(substance("copper_sulfate_aq", 'a', molar * liquid / 1000.0, molar * liquid * 0.1596));
	}
	v.contents.push_back(substance("glass", 's', 0.83, 50.0, true));
	v.wall_mm = wall_mm;
	return v;
}

const ItemView &standing() {
	static const ItemView v = vessel(250.0, 100.0, 0.3, 1.1);
	return v;
}
const ItemView &held() {
	static const ItemView v = vessel(50.0, 30.0, 0.0, 0.8);
	return v;
}
const ItemView &held_blue() {
	static const ItemView v = vessel(50.0, 30.0, 0.5, 0.8);
	return v;
}

// the standing vessel, and one held with its left edge at `x` and its top at `y`
OpticsScene scene_of(const lab::OpticsRoom &r, const ItemView &in_hand, float x, float y, const ItemView &behind = standing()) {
	OpticsBody stands{ &behind, X, Y, W, H };
	OpticsBody hand{ &in_hand, x, y, HELD_W, HELD_H };
	hand.layer = 1;
	OpticsScene scene;
	lab::optics_compose(scene, r, { hand, stands }); // given in any order: a nearer layer is painted over a further one
	REQUIRE(lab::check_O1(scene) == "");
	return scene;
}

// a wall with something to see: a grid of thin lines at odd places, a band across
lab::OpticsRoom drawn_room() {
	lab::OpticsRoom r = room();
	for (int k = 0; k < 60; ++k) {
		r.shapes.push_back(lab::OpticsShape{ 31.3f + 7.77f * static_cast<float>(k), 0.0f, 0.4f + 0.11f * static_cast<float>(k % 7), 1000.0f, 0.15 + 0.01 * (k % 5) });
	}
	r.shapes.push_back(lab::OpticsShape{ 0.0f, 149.6f, 1000.0f, 0.8f, 0.05 });
	return r;
}

double eye(const double a[3], const double b[3]) {
	const auto seen = [](const double c[3]) {
		return lab::shown(chem::optics::Xyz{ 0.4124564 * c[0] + 0.3575761 * c[1] + 0.1804375 * c[2], 0.2126729 * c[0] + 0.7151522 * c[1] + 0.0721750 * c[2],
				0.0193339 * c[0] + 0.1191920 * c[1] + 0.9503041 * c[2] });
	};
	return lab::eye_difference(seen(a), seen(b));
}

double index_at(chem::SpeciesId id, double nm) {
	const chem::optics::Spectrum &n = chem::optics::refractive_index(id);
	const auto i = static_cast<size_t>(nm - 380.0);
	const double f = nm - 380.0 - static_cast<double>(i);
	return n[i] * (1.0 - f) + n[i + 1] * f;
}

} // namespace

TEST_CASE("layers: a nearer layer's bodies are painted over a further one's, and the scene says how far apart they stand (Stage 21)") {
	const OpticsScene scene = scene_of(room(), held(), 215, 110);
	namespace L = lab::optics_layout;
	const auto vec = [&](int i) { return &scene.table[static_cast<size_t>(i) * 4]; };
	CHECK(vec(0)[0] == 2.0f);
	CHECK(vec(2)[0] == 284.0f); // the wall, behind the bench's plane (pixels)
	CHECK(vec(3)[0] == 120.0f); // the gap between two layers' planes
	CHECK(vec(3)[1] == 1.0f); // the nearest layer anything is on
	// the standing vessel first (layer 0), the held one after it
	CHECK(vec(L::HEADER_VECS + L::RECT)[0] == X);
	CHECK(vec(L::HEADER_VECS + L::REFRACT)[1] == 0.0f);
	CHECK(vec(L::HEADER_VECS + L::BODY_VECS + L::RECT)[0] == 215.0f);
	CHECK(vec(L::HEADER_VECS + L::BODY_VECS + L::REFRACT)[1] == 1.0f);
}

TEST_CASE("layers: a vessel picked up sees the wall from a gap further (Stage 21)") {
	// near its axis a vessel of water shows the wall at 1 - 2 D (n - 1) / (n R) times: D is 284 px standing, 404 held
	const ItemView water = vessel(50.0, 45.0, 0.0, 0.0);
	const double n = index_at(chem::builtin::water, 550.0), radius = HELD_W / 2.0;
	for (const int layer : { 0, 1, 3 }) {
		OpticsBody body{ &water, 400, 300, HELD_W, HELD_H };
		body.layer = layer;
		OpticsScene scene;
		lab::optics_compose(scene, room(), { body });
		const int row = 370;
		const double times = (lab::optics_bend(scene, 435, row, 550.0).x - lab::optics_bend(scene, 433, row, 550.0).x) / 2.0;
		const double wall = 284.0 + 120.0 * layer;
		CAPTURE(layer);
		CHECK(times == doctest::Approx(1.0 - 2.0 * wall * (n - 1.0) / (n * radius)).epsilon(0.02));
		// and the truth's own tracer agrees
		CHECK(lab::optics_exact_bend(scene, 440, row, 550.0) == doctest::Approx(lab::optics_bend(scene, 440, row, 550.0).x).epsilon(1e-4));
	}
}

TEST_CASE("layers: a ray followed through a held vessel and a standing one lands where both traced together land it (Stage 21)") {
	// The renderer chains the two bodies' maps (each its body alone, straight in); the truth traces a ray
	// through whatever medium a point of the one world is in. The chain itself is exact (to 1e-8 of a pixel,
	// docs/TDD_21_LAYERS.md); each map is true to a fiftieth of a pixel of the screen, and the second vessel
	// magnifies the first's share: the two put the image within 0.06 of a pixel, away from where rays part ways.
	int compared = 0, through_both = 0;
	double worst = 0.0;
	for (const float x : { 190.0f, 210.0f, 231.5f, 255.25f }) {
		const OpticsScene scene = scene_of(room(), held(), x, 118);
		for (const int row : { 150, 185, 200 }) { // the held vessel's air over the standing one's air; its liquid over air, and over liquid
			for (int px = static_cast<int>(x) + 3; px < static_cast<int>(x + HELD_W) - 3; ++px) {
				for (const double nm : { 430.0, 550.0, 680.0 }) {
					const double exact = lab::optics_exact_bend(scene, px, row, nm), before = lab::optics_exact_bend(scene, px - 1, row, nm), after = lab::optics_exact_bend(scene, px + 1, row, nm);
					const double slope = std::max(1.0, std::fabs(after - before) / 2.0);
					if (std::fabs(exact) > 5000.0 || slope > 30.0 || std::fabs(exact - 0.5 * (before + after)) > 2.0) {
						continue; // it does not land, or it or its neighbours go another way (a rim of the vessel behind)
					}
					CAPTURE(x);
					CAPTURE(px);
					CAPTURE(row);
					CAPTURE(nm);
					const double off = std::fabs(exact - lab::optics_bend(scene, px, row, nm).x) / slope;
					CHECK(off < 0.06);
					worst = std::max(worst, off);
					++compared;
					through_both += px >= static_cast<int>(X) + 5 && px < static_cast<int>(X + W) - 5 ? 1 : 0;
				}
			}
		}
	}
	CHECK(compared > 1500);
	CHECK(through_both > 500);
	MESSAGE(compared << " rays through a held vessel over a standing one: the chain within " << worst << " of a pixel of the truth's tracer");
}

TEST_CASE("layers: every pixel's colour is what its rays bring, through what is held and what stands behind it (Stage 21)") {
	// The truth: 64 rays a pixel, each traced through every body, the wall read from the shapes themselves.
	// The held vessel's top, surface and floor, and the standing one's, cut pixels' rows; its rims cross the
	// standing vessel's; the standing one's liquid is blue.
	double worst = 0.0, sum = 0.0;
	int compared = 0;
	const lab::OpticsRoom r = drawn_room();
	for (const float x : { 205.0f, 237.5f }) {
		const OpticsScene scene = scene_of(r, held_blue(), x, 118.3f);
		namespace L = lab::optics_layout;
		const int surface_standing = static_cast<int>(scene.table[static_cast<size_t>(L::HEADER_VECS + L::FORM) * 4 + 2]);
		const int surface_held = static_cast<int>(scene.table[static_cast<size_t>(L::HEADER_VECS + L::BODY_VECS + L::FORM) * 4 + 2]);
		for (const int y : { 118, 135, 149, surface_standing, surface_held, 195, 205 }) {
			for (int px = static_cast<int>(x) - 1; px <= static_cast<int>(x + HELD_W); px += (y == surface_held ? 1 : 3) * sparse()) {
				if (lab::optics_cover(scene, px, y) <= 0.0) {
					continue;
				}
				double shown[3], truth[3];
				lab::optics_truth(scene, px, y, shown);
				lab::optics_area_truth(scene, px, y, 8, truth);
				const double e = eye(shown, truth);
				CAPTURE(x);
				CAPTURE(px);
				CAPTURE(y);
				CHECK(e < lab::EYE_VISIBLE);
				worst = std::max(worst, e);
				sum += e;
				++compared;
			}
		}
	}
	CHECK(compared > 300 / sparse());
	CHECK(sum / compared < 1.5);
	MESSAGE(compared << " pixels through a held vessel and a standing one, against 64 rays each: worst " << worst << " on the eye metric (visible above 8), mean " << sum / compared);
}

TEST_CASE("layers: light between layers is a spectrum: the standing vessel's liquid absorbs along the chord the bent ray crosses there (Stage 21)") {
	// through clear water in the hand, then copper sulfate standing: at 680 nm what the standing liquid takes is
	// e^(-tau L) along the chord of the ray as the held vessel sent it in, not along the chord straight behind
	// the pixel
	const ItemView clear = vessel(250.0, 100.0, 0.0, 1.1), weak = vessel(250.0, 100.0, 0.02, 1.1);
	const OpticsScene with = scene_of(room(), held(), 215, 118, weak), without = scene_of(room(), held(), 215, 118, clear);
	const double tau_per_cm = chem::optics::spectrum(chem::builtin::copper_sulfate_aq)[300] * 2.302585092994046 * 0.02;
	const double cm_per_px = with.table[static_cast<size_t>(lab::optics_layout::HEADER_VECS + lab::optics_layout::FORM) * 4 + 1];
	int bent_chords = 0;
	for (const int px : { 232, 245, 250, 262 }) {
		const int row = 200; // both liquids
		const lab::OpticsBend b = lab::optics_bend(with, px, row, 680.0);
		const double front = 0.3, reflected = (1.0 - b.pass) * front;
		const double t = (lab::optics_through(with, px, row, 680.0) - reflected) / (lab::optics_through(without, px, row, 680.0) - reflected);
		// the chord through the standing liquid, from the truth's own tracer: what both scenes' truths say was taken
		const double t_truth = (lab::optics_area_through(with, px, row, 680.0, 16) - reflected) / (lab::optics_area_through(without, px, row, 680.0, 16) - reflected);
		CAPTURE(px);
		CHECK(t == doctest::Approx(t_truth).epsilon(0.01));
		// and it is not the chord straight behind the pixel
		const double d = (px + 0.5 - (X + W / 2)) * cm_per_px, r_in = 45.0 * cm_per_px - 0.11;
		const double straight = 2.0 * std::sqrt(r_in * r_in - d * d);
		bent_chords += std::fabs(-std::log(t) / tau_per_cm - straight) > 0.05 ? 1 : 0;
	}
	CHECK(bent_chords >= 2);
}

TEST_CASE("O5: where every index is 1, layers change nothing: a held vessel and a standing one are their straight light (Stage 21)") {
	const ItemView front = vessel(50.0, 30.0, 0.4, 0.0), back = vessel(250.0, 100.0, 0.3, 0.0);
	lab::OpticsRoom bends = room(), straight = room();
	straight.refraction = false;
	OpticsScene bent = scene_of(bends, front, 215, 118, back);
	const OpticsScene plain = scene_of(straight, front, 215, 118, back);
	using namespace lab::optics_layout;
	for (int b = 0; b < 2; ++b) {
		for (const int row : { LIQUID_INDEX_ROW, WALL_INDEX_ROW }) {
			for (int i = 0; i < ROW; ++i) {
				bent.rows[static_cast<size_t>((b * ROWS_PER_BODY + row) * ROW + i)] = 1.0f;
			}
		}
	}
	lab::optics_maps(bent);
	double middle_worst = 0.0, truth_worst = 0.0;
	for (const int y : { 150, 185, 200 }) {
		for (int x = 214; x <= 286; x += 3) {
			const bool middle = x > 225 && x < 275; // away from both vessels' rims
			for (const double nm : { 420.0, 550.0, 700.0 }) {
				const double a = lab::optics_through(bent, x, y, nm);
				truth_worst = std::max(truth_worst, std::fabs(a - lab::optics_area_through(bent, x, y, nm, 64)));
				if (middle) {
					middle_worst = std::max(middle_worst, std::fabs(a - lab::optics_through(plain, x, y, nm)));
				}
			}
		}
	}
	CHECK(truth_worst < 0.002); // of the room's white
	CHECK(middle_worst < 5e-4);
	// and it fires: with their own indices the two bend and reflect
	const OpticsScene real = scene_of(bends, front, 215, 118, back);
	CHECK(std::fabs(lab::optics_through(real, 262, 200, 550.0) - lab::optics_through(plain, 262, 200, 550.0)) > 1e-3);
}

TEST_CASE("O6: Fresnel loses no light through layers: in a room as bright in front as behind, clear vessels one before another change nothing (Stage 21)") {
	lab::OpticsRoom r = room();
	r.front = WALL;
	r.elsewhere = WALL;
	const ItemView clear_held = vessel(50.0, 30.0, 0.0, 0.8), clear_standing = vessel(250.0, 100.0, 0.0, 1.1);
	OpticsScene scene = scene_of(r, clear_held, 215, 118, clear_standing);
	using namespace lab::optics_layout;
	for (int b = 0; b < 2; ++b) {
		for (int i = 0; i < ROW; ++i) {
			scene.rows[static_cast<size_t>((b * ROWS_PER_BODY + LIQUID_ROW) * ROW + i)] = 0.0f; // water that absorbs nothing
		}
	}
	double worst = 0.0;
	for (int y = 110; y <= 210; y += 6) {
		for (int x = 214; x < 286; x += 2) {
			for (const double nm : { 400.0, 550.0, 760.0 }) {
				worst = std::max(worst, std::fabs(lab::optics_through(scene, x, y, nm) - WALL));
			}
		}
	}
	CHECK(worst < 1e-6);
}

TEST_CASE("O7: brightness is neither made nor lost by bending twice, or six times: a uniform wall through layers stays as it is (Stage 21)") {
	const OpticsScene two = scene_of(drawn_room(), held(), 215, 118);
	CHECK(lab::check_O7(two, 199, 100, 291, 213, 0.6) == "");
	// six vessels, each a layer nearer and a little aside
	std::vector<ItemView> items;
	for (int k = 0; k < 6; ++k) {
		items.push_back(vessel(k % 2 == 0 ? 250.0 : 50.0, k % 2 == 0 ? 100.0 : 30.0, 0.1 * k, 1.0));
	}
	std::vector<OpticsBody> bodies;
	for (int k = 0; k < 6; ++k) {
		OpticsBody b{ &items[static_cast<size_t>(k)], 200.0f + 9.5f * static_cast<float>(k), 100.0f + 4.0f * static_cast<float>(k), k % 2 == 0 ? W : HELD_W, k % 2 == 0 ? H : HELD_H };
		b.layer = k;
		bodies.push_back(b);
	}
	OpticsScene six;
	lab::optics_compose(six, drawn_room(), bodies);
	REQUIRE(lab::check_O1(six) == "");
	CHECK(six.table[3 * 4 + 1] == 5.0f);
	CHECK(lab::check_O7(six, 199, 100, 320, 220, 0.6) == "");
	// and through six the chain still lands where the truth's tracer does
	int compared = 0;
	for (int px = 252; px < 285; px += 2) {
		const double exact = lab::optics_exact_bend(six, px, 190, 550.0);
		const double slope = std::max(1.0, std::fabs(lab::optics_exact_bend(six, px + 1, 190, 550.0) - lab::optics_exact_bend(six, px - 1, 190, 550.0)) / 2.0);
		if (std::fabs(exact) > 5000.0 || slope > 30.0) {
			continue;
		}
		CAPTURE(px);
		CHECK(std::fabs(exact - lab::optics_bend(six, px, 190, 550.0).x) / slope < 0.02);
		++compared;
	}
	CHECK(compared > 5);
}

TEST_CASE("O8: a thin line keeps its light through two layers: summed over the pixels it is what its rays bring (Stage 21)") {
	const OpticsScene plain = scene_of(room(), held(), 215, 118);
	// lines whose images are made by rays through the vessels' bodies: to 1% of the line's light
	for (const float x : { 226.4f, 236.7f, 250.2f, 262.4f }) {
		CAPTURE(x);
		lab::OpticsRoom r = room();
		r.shapes.push_back(lab::OpticsShape{ x, 0.0f, 0.3f, 1000.0f, 0.05 });
		const OpticsScene lined = scene_of(r, held(), 215, 118);
		CHECK(lab::check_O8(lined, plain, 200, 198, 292, 550.0) == ""); // both liquids
		CHECK(lab::check_O8(lined, plain, 150, 198, 292, 550.0) == ""); // the air over both
	}
	// A line beside the vessels is seen only in their rims, where rays part ways within a quarter of a pixel
	// and each side is read at its own ray (GAME_SPEC §20): its light is kept to 3%, or to a thousandth of a
	// pixel of the line's full contrast where its image is that faint.
	for (const float x : { 190.3f, 281.9f }) {
		lab::OpticsRoom r = room();
		r.shapes.push_back(lab::OpticsShape{ x, 0.0f, 0.3f, 1000.0f, 0.05 });
		const OpticsScene lined = scene_of(r, held(), 215, 118);
		for (const int row : { 150, 200 }) {
			const std::string said = lab::check_O8(lined, plain, row, 198, 292, 550.0);
			double shown = 0.0, rays = 0.0;
			if (!said.empty()) {
				REQUIRE(std::sscanf(said.c_str(), "O8: the line brings %lf summed over the pixels of row %*d, its rays %lf", &shown, &rays) == 2);
				CAPTURE(x);
				CAPTURE(row);
				CHECK(std::fabs(shown - rays) < 0.03 * std::fabs(rays) + 0.001 * (WALL - 0.05));
			}
		}
	}
}

TEST_CASE("layers: a balloon behind a held vessel is seen where the bent rays cross it (Stage 21)") {
	ItemView balloon;
	balloon.id = "balloon";
	balloon.kind = "balloon";
	balloon.gas_ml = 400.0;
	balloon.contents = { substance("latex", 's', 0.0, 2.0, true) };
	const ItemView water = vessel(50.0, 45.0, 0.0, 0.8);
	OpticsBody stands{ &balloon, 220, 120, 80, 80 };
	OpticsBody hand{ &water, 215, 118, HELD_W, HELD_H };
	hand.layer = 1;
	OpticsScene scene;
	lab::optics_compose(scene, drawn_room(), { stands, hand });
	REQUIRE(lab::check_O1(scene) == "");
	// A ball's outline blends by degrees (its depth times how much of a sample it covers: GAME_SPEC §12.3), as
	// it does seen straight: a pixel whose rays the outline cuts aslant may differ from the truth's sharp
	// outline. Those apart (two at most here), every pixel is what its rays bring.
	std::vector<double> off;
	int compared = 0, moved = 0;
	for (const int y : { 140, 170, 190 }) {
		for (int px = 216; px < 284; px += 3) {
			double shown[3], truth[3];
			lab::optics_truth(scene, px, y, shown);
			lab::optics_area_truth(scene, px, y, 8, truth);
			off.push_back(eye(shown, truth));
			++compared;
			// the water turns the picture over: left of the vessel's axis the ray crosses the balloon right of it
			moved += std::fabs(lab::optics_exact_bend(scene, px, y, 550.0) - (px + 0.5)) > 10.0 ? 1 : 0;
		}
	}
	CHECK(compared > 60);
	CHECK(moved > 30);
	std::sort(off.begin(), off.end());
	CHECK(off[off.size() - 3] < lab::EYE_VISIBLE);
	MESSAGE(compared << " pixels of a balloon behind a held vessel of water: all but two within " << off[off.size() - 3] << " on the eye metric; on its outline, " << off.back());
}

TEST_CASE("layers: a hot solid's glow is a light in the pass: what is held before it bends it and takes its red (Stage 21)") {
	// copper oxide at 850 C glows a dull red in a standing vessel; a vessel of copper sulfate solution is held
	// in front of it. The glow is seen where the held vessel's rays reach the heap, not straight behind the
	// pixel, and through the solution, which takes the red: as the truth's rays have it.
	ItemView hot;
	hot.id = "hot";
	hot.kind = "vessel";
	hot.volume_ml = 250.0;
	hot.celsius = 850.0;
	hot.condensed_ml = 6.0;
	SubstanceView oxide = substance("copper_oxide", 's', 0.08, 6.3);
	oxide.particles = 1000000;
	oxide.size_um = 100.0;
	hot.contents = { oxide, substance("glass", 's', 0.83, 50.0, true) };
	hot.wall_mm = 1.1;
	const ItemView water = vessel(50.0, 45.0, 0.0, 0.8), blue = vessel(50.0, 45.0, 0.5, 0.8);
	const float hand_y = Y + H - HELD_H; // low: its liquid before the heap on the standing vessel's floor
	const int row = static_cast<int>(Y + H) - 4;
	const auto red_of = [&](const OpticsScene &scene, int px) {
		double shown[3];
		lab::optics_truth(scene, px, row, shown);
		return shown[0];
	};
	const OpticsScene clear = scene_of(room(), water, 215, hand_y, hot), tinted = scene_of(room(), blue, 215, hand_y, hot);
	OpticsScene alone;
	lab::optics_compose(alone, room(), { OpticsBody{ &hot, X, Y, W, H } });
	// the heap alone glows in the middle of the standing vessel's floor
	const double bare = red_of(alone, 245);
	CHECK(bare > 0.5); // far above what the room's light makes of black copper oxide
	double worst = 0.0;
	int compared = 0, seen = 0, dimmer = 0;
	for (int px = 222; px < 280; px += 2) {
		double shown[3], truth[3];
		lab::optics_truth(clear, px, row, shown);
		lab::optics_area_truth(clear, px, row, 8, truth);
		CAPTURE(px);
		// the glow is many times the room's light: compared as light, not on the screen (where it is past white)
		const double scale = std::max(0.05, std::max(shown[0], truth[0]));
		worst = std::max(worst, std::fabs(shown[0] - truth[0]) / scale);
		++compared;
		if (shown[0] > 0.3) {
			++seen;
			dimmer += red_of(tinted, px) < 0.5 * shown[0] ? 1 : 0; // the solution takes most of the red
		}
	}
	CHECK(compared > 20);
	CHECK(seen > 5);
	CHECK(dimmer == seen);
	CHECK(worst < 0.25); // a heap's sloping edge blends by degrees, as it does seen straight
	MESSAGE("a glowing heap through a held vessel of water: " << seen << " of " << compared << " pixels show it; against 64 rays each, within " << worst << " of the light");
}

TEST_CASE("layers: a vessel's floor is glass all through: a rod seen from the side, and what it holds rests on it (Stage 21)") {
	// no outline is drawn: a vessel is its glass and what it holds. Its floor, 2.6 mm thick, is glass from side
	// to side, and near its axis shows the wall behind at 1 - 2 D (n - 1) / (n R) times: turned over and three
	// times its size
	ItemView v = vessel(250.0, 100.0, 0.3, 1.1);
	v.floor_mm = 2.6;
	OpticsScene scene;
	lab::optics_compose(scene, drawn_room(), { OpticsBody{ &v, X, Y, W, H } });
	REQUIRE(lab::check_O1(scene) == "");
	namespace L = lab::optics_layout;
	const float *form = &scene.table[static_cast<size_t>(L::HEADER_VECS + L::FORM) * 4], *refract = &scene.table[static_cast<size_t>(L::HEADER_VECS + L::REFRACT) * 4];
	const double floor_px = refract[2] / form[1], foot = Y + H;
	CHECK(refract[2] == doctest::Approx(0.26));
	CHECK(floor_px == doctest::Approx(3.69).epsilon(0.01));
	CHECK(scene.maps[1] == 3.0f); // its map's cores: its liquid, the air over it, its floor
	// its liquid's surface stands that much higher than without a floor
	ItemView bare = v;
	bare.floor_mm = 0.0;
	OpticsScene without;
	lab::optics_compose(without, drawn_room(), { OpticsBody{ &bare, X, Y, W, H } });
	CHECK(form[2] == doctest::Approx(without.table[static_cast<size_t>(L::HEADER_VECS + L::FORM) * 4 + 2] - floor_px).epsilon(1e-5));
	// in its floor's rows a ray near the axis goes through glass alone
	const int row = static_cast<int>(foot - floor_px / 2.0), axis = static_cast<int>(X + W / 2);
	const double n = index_at(chem::builtin::glass, 550.0), radius = W / 2.0;
	const double times = (lab::optics_bend(scene, axis + 1, row, 550.0).x - lab::optics_bend(scene, axis - 1, row, 550.0).x) / 2.0;
	CHECK(times == doctest::Approx(1.0 - 2.0 * 284.0 * (n - 1.0) / (n * radius)).epsilon(0.02));
	CHECK(times < -2.5);
	CHECK(lab::optics_bend(scene, axis + 10, row, 550.0).chord == 0.0); // no liquid in its way: nothing of the blue
	// the truth's own tracer agrees, there and in the rows the floor's top cuts
	for (int px = static_cast<int>(X) + 8; px < static_cast<int>(X + W) - 8; px += 5) {
		CAPTURE(px);
		const double exact = lab::optics_exact_bend(scene, px, row, 550.0);
		const double slope = std::max(1.0, std::fabs(lab::optics_exact_bend(scene, px + 1, row, 550.0) - lab::optics_exact_bend(scene, px - 1, row, 550.0)) / 2.0);
		CHECK(std::fabs(exact - lab::optics_bend(scene, px, row, 550.0).x) / slope < 0.02);
	}
	double worst = 0.0;
	int compared = 0;
	for (int y = static_cast<int>(foot) - 7; y <= static_cast<int>(foot); ++y) {
		for (int px = static_cast<int>(X) - 1; px <= static_cast<int>(X + W); px += 2 * sparse()) {
			if (lab::optics_cover(scene, px, y) <= 0.0) {
				continue;
			}
			double shown[3], truth[3];
			lab::optics_truth(scene, px, y, shown);
			lab::optics_area_truth(scene, px, y, 8, truth);
			const double e = eye(shown, truth);
			CAPTURE(px);
			CAPTURE(y);
			CHECK(e < lab::EYE_VISIBLE);
			worst = std::max(worst, e);
			++compared;
		}
	}
	CHECK(compared > 250 / sparse());
	// a uniform wall stays uniform through it (O7), and held before another vessel it is followed as the rest is
	CHECK(lab::check_O7(scene, static_cast<int>(X) - 1, static_cast<int>(foot) - 8, static_cast<int>(X + W) + 1, static_cast<int>(foot) + 1, 0.6) == "");
	ItemView in_hand = vessel(50.0, 30.0, 0.0, 0.8);
	in_hand.floor_mm = 1.9;
	const OpticsScene held_scene = scene_of(drawn_room(), in_hand, 215, 118, v);
	const int held_floor = static_cast<int>(118 + HELD_H) - 1;
	double worst_held = 0.0;
	for (int px = 216; px < 284; px += 3) {
		double shown[3], truth[3];
		lab::optics_truth(held_scene, px, held_floor, shown);
		lab::optics_area_truth(held_scene, px, held_floor, 8, truth);
		worst_held = std::max(worst_held, eye(shown, truth));
	}
	CHECK(worst_held < lab::EYE_VISIBLE);
	MESSAGE(compared << " pixels of a vessel's foot against 64 rays each: worst " << worst << " on the eye metric; its floor held before another vessel: " << worst_held << "; the wall through its floor at " << times << " times");
}
