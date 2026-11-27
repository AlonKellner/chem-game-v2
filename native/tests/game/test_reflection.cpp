// Reflection traced (GAME_SPEC §12.3, Stage 78): a metal is its complex index n + ik, and what comes to it is turned
// back by Fresnel's reflectance and never goes in. Liquid mercury in a beaker, held to the closed form ray by ray, the
// pass's tracer to the truth's (O9), and what its pixels show to the truth's rays (O4 on the CPU).
#include "chem/builtin_ids.hpp"
#include "chem/optics.hpp"
#include "lab/colour.hpp"
#include "lab/contents.hpp"
#include "lab/glassware.hpp"
#include "lab/optics_scene.hpp"

#include "sparse.hpp"

#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <vector>

using lab::ItemView;
using lab::OpticsScene;
using lab::SubstanceView;

namespace {

constexpr float X = 300, Y = 200, W = 90;

lab::OpticsRoom room() {
	lab::OpticsRoom r;
	r.w = r.h = 1000;
	r.bench = 0.7305;
	r.elsewhere = 0.02;
	for (int c = 0; c < 12; ++c) { // a grid, as the game draws on its wall
		r.shapes.push_back(lab::OpticsShape{ 40.0f + 60.0f * static_cast<float>(c), 0.0f, 1.0f, 1000.0f, 0.55 });
		r.shapes.push_back(lab::OpticsShape{ 0.0f, 30.0f + 60.0f * static_cast<float>(c), 1000.0f, 1.0f, 0.55 });
	}
	return r;
}

SubstanceView substance(const char *species, char phase, double mol, double g, double ml, bool wall = false) {
	SubstanceView s;
	s.species = species;
	s.phase = phase;
	s.mol = mol;
	s.g = g;
	s.ml = ml;
	s.wall = wall;
	return s;
}

// a 250 mL beaker of liquid mercury (13.534 g/mL), or of water
ItemView beaker(double ml, bool mercury) {
	const lab::glass::Vessel *g = lab::glass::find("beaker", 250);
	REQUIRE(g != nullptr);
	ItemView it;
	it.id = "beaker";
	it.kind = "vessel";
	it.glassware = "beaker";
	it.nominal_ml = 250;
	it.volume_ml = g->capacity();
	it.celsius = 20.0;
	it.liquid_ml = ml;
	it.condensed_ml = ml;
	it.contents = { mercury ? substance("mercury", 'l', ml * 13.534 / 200.59, ml * 13.534, ml) : substance("water", 'l', ml / 18.015, ml, ml), substance("glass", 's', 0.83, 50.0, 0.0, true) };
	it.wall_mm = 10.0 * g->wall;
	it.floor_mm = 10.0 * g->floor;
	return it;
}

float height_px() {
	const lab::glass::Vessel *g = lab::glass::find("beaker", 250);
	return static_cast<float>(W * g->height / (2.0 * g->widest()));
}

// the room as the game makes it (ui.cpp): a cube twice the screen's width, its edge cells black, the wall beyond the
// screen and (Stage 78) its faces ruled as the wall is, a line each way every 40 pixels
lab::OpticsRoom cube_room() {
	lab::OpticsRoom r = room();
	const double white = 0.7305, ruled = 0.55, sq = 40.0, share = 2.0 / sq - 1.0 / (sq * sq);
	r.beyond = white - share * (white - ruled);
	r.cube = 3840.0;
	r.cube_cell = 240.0;
	r.edge = 0.0;
	r.cube_x = 500.0;
	r.cube_y = 500.0;
	const double black = 1.0 - (14.0 / 16.0) * (14.0 / 16.0);
	r.front = (1.0 - black) * r.beyond;
	r.face_white = white;
	r.face_ruled = ruled;
	r.face_pitch = sq;
	r.face_line = 1.0;
	return r;
}

OpticsScene scene_of(const ItemView &item, bool cube = false) {
	lab::OpticsBody b{ &item, X, Y, W, height_px() };
	OpticsScene scene;
	lab::optics_compose(scene, cube ? cube_room() : room(), { b });
	REQUIRE(lab::check_O1(scene) == "");
	return scene;
}

// the screen's row of a height over the beaker's base (cm)
double row_of(double y_cm) {
	const lab::glass::Vessel *g = lab::glass::find("beaker", 250);
	const double cm = 2.0 * g->widest() / W;
	return Y + height_px() - y_cm / cm;
}

double level_cm(double ml) {
	const lab::glass::Vessel *g = lab::glass::find("beaker", 250);
	return lab::contents::lying(*g, 0.0, ml, 0.0, 0.0, 1).liquid_level;
}

double eye(const double a[3], const double b[3]) {
	const auto seen = [](const double c[3]) {
		return lab::shown(chem::optics::Xyz{ 0.4124564 * c[0] + 0.3575761 * c[1] + 0.1804375 * c[2], 0.2126729 * c[0] + 0.7151522 * c[1] + 0.0721750 * c[2],
				0.0193339 * c[0] + 0.1191920 * c[1] + 0.9503041 * c[2] });
	};
	return lab::eye_difference(seen(a), seen(b));
}

} // namespace

TEST_CASE("reflection: a ray straight at liquid mercury is turned back by its complex Fresnel reflectance, in the pass and in the truth (Stage 78)") {
	const ItemView item = beaker(150.0, true);
	const OpticsScene scene = scene_of(item);
	const double y = row_of(0.5 * level_cm(150.0)), x = X + W / 2.0; // the beaker's axis, half way up the mercury
	const chem::optics::Spectrum &n_glass = chem::optics::refractive_index(chem::builtin::glass);
	const chem::optics::Spectrum &n_hg = chem::optics::refractive_index(chem::builtin::mercury), &k_hg = chem::optics::extinction_index(chem::builtin::mercury);
	for (const int nm : { 420, 550, 680 }) {
		CAPTURE(nm);
		const size_t i = static_cast<size_t>(nm - 380);
		// into the glass and out again at normal incidence, turned back by the mercury behind it between
		const double into = 1.0 - chem::optics::fresnel_reflectance(1.0, 0.0, n_glass[i], 0.0, 1.0);
		const double back = chem::optics::fresnel_reflectance(n_glass[i], 0.0, n_hg[i], k_hg[i], 1.0);
		// and what the glass takes in on the way, through its wall and back (its row's absorption per cm, as both tracers
		// read it)
		const double alpha = scene.rows[static_cast<size_t>(lab::optics_layout::WALL_ROW * lab::optics_layout::ROW) + i];
		const double expected = into * back * into * std::exp(-alpha * 2.0 * lab::glass::find("beaker", 250)->wall);
		CHECK(back > 0.69); // (glass onto mercury: less than air's 0.78, the index step smaller)
		for (const bool truth : { false, true }) {
			CAPTURE(truth);
			const lab::OpticsRay r = truth ? lab::optics_exact_ray(scene, x, y, nm) : lab::optics_ray(scene, x, y, nm);
			REQUIRE(r.bent >= 1);
			CHECK(r.chord[0] == 0.0); // it never goes into the mercury
			CHECK(r.pass[0] == doctest::Approx(expected).epsilon(1e-6));
			CHECK(!r.lands); // back toward the eye: to the room in front
		}
	}
	// over water the same ray goes through to the wall, as it always did
	const OpticsScene water = scene_of(beaker(150.0, false));
	const lab::OpticsRay w = lab::optics_ray(water, x, y, 550.0);
	CHECK(w.lands);
	CHECK(w.chord[0] > 1.0);
}

TEST_CASE("O9: through and beside a beaker of mercury, the pass's rays are the truth's (Stage 78)") {
	const ItemView item = beaker(150.0, true);
	const OpticsScene scene = scene_of(item);
	const int top = static_cast<int>(row_of(level_cm(150.0))) - 6, foot = static_cast<int>(Y + height_px()) + 1;
	CHECK(lab::check_O9(scene, static_cast<int>(X) - 2, top, static_cast<int>(X + W) + 2, foot, sparse() > 1 ? 3 : 1) == "");
}

TEST_CASE("reflection: what a beaker of mercury shows is what the truth's rays bring, across it and over its surface (Stage 78)") {
	const ItemView item = beaker(150.0, true);
	const OpticsScene scene = scene_of(item);
	const int level = static_cast<int>(row_of(level_cm(150.0))), middle = static_cast<int>(row_of(0.5 * level_cm(150.0)));
	std::vector<std::pair<int, int>> places;
	for (int d = 1; d < static_cast<int>(W) - 1; d += 4 * sparse()) {
		places.push_back({ static_cast<int>(X) + d, middle }); // across the mercury, rim to rim
	}
	for (int d = -6; d <= 10; d += 2 * sparse()) {
		places.push_back({ static_cast<int>(X + W / 2), level + d }); // down through its surface
	}
	double worst = 0.0, grey = 0.0;
	int compared = 0;
	for (const auto &[px, py] : places) {
		if (lab::optics_cover(scene, px, py) <= 0.0) {
			continue;
		}
		double shown[3], truth[3];
		lab::optics_truth(scene, px, py, shown);
		lab::optics_area_truth(scene, px, py, 6, truth);
		double e = eye(shown, truth);
		if (e >= lab::EYE_VISIBLE) {
			// (where a pixel's rays part, a rim, the pixel is what its rays bring over the frames)
			lab::OpticsFrames settle(px, py, 1, 1);
			for (int f = 0; f < 400; ++f) {
				settle.frame(scene);
			}
			settle.colour(scene, px, py, shown);
			lab::optics_area_truth(scene, px, py, 16, truth);
			e = eye(shown, truth);
		}
		CAPTURE(px);
		CAPTURE(py);
		CHECK(e < lab::EYE_VISIBLE);
		worst = std::max(worst, e);
		if (py == middle && std::abs(px - static_cast<int>(X + W / 2)) < 4) {
			grey = (shown[0] + shown[1] + shown[2]) / 3.0;
			CHECK(std::max({ shown[0], shown[1], shown[2] }) - std::min({ shown[0], shown[1], shown[2] }) < 0.06 * grey); // neutral: mercury's reflectance is flat
		}
		++compared;
	}
	CHECK(compared > 10);
	// the middle of the mercury: the room in front, turned back by glass and mercury, neutral grey (no colour of its own)
	CHECK(grey > 0.15);
	MESSAGE("mercury: " << compared << " pixels, the worst " << worst << " on the eye metric; its middle " << grey);
}

TEST_CASE("O28: a beaker of mercury in the room shows the room's ruled faces, where the truth's rays find them (Stage 78)") {
	const ItemView item = beaker(150.0, true);
	const OpticsScene scene = scene_of(item, true);
	const int top = static_cast<int>(row_of(level_cm(150.0))) - 6, foot = static_cast<int>(Y + height_px()) + 1, middle = static_cast<int>(row_of(0.5 * level_cm(150.0)));
	// ray by ray: the faces' places the pass puts its rays at are the truth's (O9)
	CHECK(lab::check_O9(scene, static_cast<int>(X) - 2, top, static_cast<int>(X + W) + 2, foot, sparse() > 1 ? 3 : 1) == "");
	// pixel by pixel down the mercury's middle and across it: what is shown is what the truth's rays bring
	double least = 1.0, most = 0.0, worst = 0.0;
	int compared = 0;
	std::vector<std::pair<int, int>> places;
	for (int d = 2; d < static_cast<int>(W) - 2; d += 3 * sparse()) {
		places.push_back({ static_cast<int>(X) + d, middle });
	}
	for (int y = top + 8; y < foot - 4; y += 2 * sparse()) {
		places.push_back({ static_cast<int>(X + W / 2), y });
	}
	for (const auto &[px, py] : places) {
		if (lab::optics_cover(scene, px, py) <= 0.0) {
			continue; // (beside the beaker: the wall as drawn)
		}
		double shown[3], truth[3];
		lab::OpticsFrames settle(px, py, 1, 1);
		for (int f = 0; f < 400; ++f) {
			settle.frame(scene);
		}
		settle.colour(scene, px, py, shown);
		lab::optics_area_truth(scene, px, py, 16, truth);
		const double e = eye(shown, truth);
		CAPTURE(px);
		CAPTURE(py);
		CHECK(e < lab::EYE_VISIBLE);
		worst = std::max(worst, e);
		if (px == static_cast<int>(X + W / 2)) {
			least = std::min(least, truth[1]);
			most = std::max(most, truth[1]);
		}
		++compared;
	}
	// down its middle the mirror shows the front face's lines across: darker rows among lighter ones
	CHECK(most - least > 0.05);
	MESSAGE("mercury in the room: " << compared << " pixels, the worst " << worst << " on the eye metric; down its middle from " << least << " to " << most);
}

TEST_CASE("O27: a reflection is a branch, fairly drawn: the pass's seeded rays take the truth's branches through glass and water (Stage 78)") {
	const ItemView item = beaker(150.0, false);
	const OpticsScene scene = scene_of(item, true);
	const int top = static_cast<int>(Y) - 2, foot = static_cast<int>(Y + height_px()) + 1;
	CHECK(lab::check_O27(scene, static_cast<int>(X) - 2, top, static_cast<int>(X + W) + 2, foot, sparse() > 1 ? 9 : 5, 4) == "");
	// its own test: a roulette run against T fires
	CHECK(lab::check_O27(scene, static_cast<int>(X) - 2, top, static_cast<int>(X + W) + 2, foot, 9, 2, true).rfind("O27", 0) == 0);
	// and a seeded ray does take a branch: of 64 seeds at a rim, where glass turns back much, some go another way than
	// the beam's own ray (which takes none), and some its way
	const double y = row_of(0.5 * level_cm(150.0));
	double x = X;
	while (x < X + W / 2 && lab::optics_ray(scene, x, y, 550.0).bent == 0) {
		x += 0.25; // (to the glass's own edge: the beaker is narrower than its lip)
	}
	x += 0.5;
	const lab::OpticsRay beam = lab::optics_ray(scene, x, y, 550.0);
	REQUIRE(beam.bent >= 1);
	int other = 0;
	for (uint32_t k = 1; k <= 64; ++k) {
		other += lab::optics_ray_seeded(scene, x, y, 550.0, k * 2654435761u | 1u).route[0] != beam.route[0] ? 1 : 0;
	}
	CHECK(other > 0);
	CHECK(other < 64);
}
