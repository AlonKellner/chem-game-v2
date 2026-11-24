// The truth in three dimensions (GAME_SPEC §12.3, Stage 24): each sub-pixel ray traced through a vessel's
// whole shape by the general tracer, landing on the wall across and up.
#include "lab/colour.hpp"
#include "lab/glassware.hpp"
#include "lab/optics_scene.hpp"

#include "sparse.hpp"

#include <doctest/doctest.h>

#include <cmath>
#include <vector>

using lab::ItemView;
using lab::OpticsScene;
using lab::SubstanceView;

namespace {

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

// a vessel made as glassware, holding 100 mL of water, drawn 90 px wide as the screen draws it
ItemView glassware(const char *kind, double nominal_ml) {
	const lab::glass::Vessel *g = lab::glass::find(kind, nominal_ml);
	REQUIRE(g != nullptr);
	ItemView it;
	it.id = kind;
	it.kind = "vessel";
	it.glassware = kind;
	it.nominal_ml = nominal_ml;
	it.volume_ml = g->capacity();
	it.celsius = 20.0;
	it.liquid_ml = it.condensed_ml = 100.0;
	it.contents = { substance("water", 'l', 5.5, 100.0), substance("glass", 's', 0.83, 50.0, true) };
	it.wall_mm = 10.0 * g->wall;
	it.floor_mm = 10.0 * g->floor;
	return it;
}

constexpr float X = 100, Y = 100, W = 90;

float height_of(const char *kind, double ml) {
	const lab::glass::Vessel *g = lab::glass::find(kind, ml);
	return static_cast<float>(W * g->height / (2.0 * g->widest()));
}

OpticsScene scene_of(const ItemView &item, std::vector<lab::OpticsShape> shapes = {}) {
	lab::OpticsRoom r = room();
	r.shapes = std::move(shapes);
	OpticsScene scene;
	lab::optics_compose(scene, r, { lab::OpticsBody{ &item, X, Y, W, height_of(item.glassware.c_str(), item.nominal_ml) } });
	REQUIRE(lab::check_O1(scene) == "");
	return scene;
}

double row_at(const OpticsScene &scene, int which, double nm) {
	const size_t base = static_cast<size_t>(which) * lab::optics_layout::ROW;
	const auto i = static_cast<size_t>(nm - 380.0);
	const double f = nm - 380.0 - static_cast<double>(i);
	return scene.rows[base + i] * (1.0 - f) + scene.rows[base + i + 1] * f;
}

double eye(const double a[3], const double b[3]) {
	const auto seen = [](const double c[3]) {
		return lab::shown(chem::optics::Xyz{ 0.4124564 * c[0] + 0.3575761 * c[1] + 0.1804375 * c[2], 0.2126729 * c[0] + 0.7151522 * c[1] + 0.0721750 * c[2],
				0.0193339 * c[0] + 0.1191920 * c[1] + 0.9503041 * c[2] });
	};
	return lab::eye_difference(seen(a), seen(b));
}

} // namespace

TEST_CASE("truth: through a beaker's upright band the truth's rays land where the glass cylinder's closed form puts them (Stage 24)") {
	// half way up a 250 mL beaker its profile is upright: 3.5 cm outside (not the brim's 3.85), 1.1 mm of
	// glass. Through concentric circles a ray keeps n d; turned by delta, it lands (d - D sin delta) / cos delta
	const ItemView beaker = glassware("beaker", 250);
	const OpticsScene scene = scene_of(beaker);
	namespace L = lab::optics_layout;
	const float *form = &scene.table[static_cast<size_t>(L::HEADER_VECS + L::FORM) * 4];
	const double cm = form[1], radius = 3.5, inner = radius - 0.11, wall = scene.table[2 * 4] * cm, axis = X + W / 2.0;
	const lab::glass::Vessel &g = *lab::glass::find("beaker", 250);
	const int row = static_cast<int>(Y + height_of("beaker", 250) - (g.floor + 0.4 * (g.level(100.0) - g.floor)) / cm); // in the liquid, above the corner
	int compared = 0;
	for (const double nm : { 430.0, 590.0 }) {
		const double n_glass = row_at(scene, L::WALL_INDEX_ROW, nm), n_water = row_at(scene, L::LIQUID_INDEX_ROW, nm);
		for (int x = static_cast<int>(X) + 12; x < static_cast<int>(X + W) - 12; x += 3) {
			const double d = (x + 0.5 - axis) * cm, a = std::fabs(d);
			if (!(a < n_glass * inner && a < n_water * inner)) {
				continue;
			}
			const double turn = 2.0 * (std::asin(a / radius) - std::asin(a / (n_glass * radius)) + std::asin(a / (n_glass * inner)) - std::asin(a / (n_water * inner)));
			const double landed = (a - wall * std::sin(turn)) / std::cos(turn);
			CAPTURE(x);
			CHECK(lab::optics_exact_bend(scene, x, row, nm) == doctest::Approx(axis + (d < 0.0 ? -landed : landed) / cm).epsilon(1e-9));
			++compared;
		}
	}
	CHECK(compared > 30);
}

TEST_CASE("truth: through a conical flask's cone and its neck a uniform wall stays as it is: O7 in three dimensions (Stage 24)") {
	const ItemView flask = glassware("conical_flask", 250);
	OpticsScene scene = scene_of(flask);
	const auto truth = [](const OpticsScene &s, int x, int y, double nm) { return lab::optics_area_through(s, x, y, nm, 1); };
	const int h = static_cast<int>(height_of("conical_flask", 250));
	CHECK(lab::check_O7(scene, static_cast<int>(X), static_cast<int>(Y), static_cast<int>(X + W), static_cast<int>(Y) + h, 0.6, truth) == "");
}

TEST_CASE("truth: a level line on the wall, seen through a conical flask's cone, is seen on another row (Stage 24)") {
	// a dark line drawn level across the wall; down a column through the flask's cone the pixel that shows it
	// most is not on the line's own row: the cone's slant bends the rays up or down (a cylinder would not)
	const ItemView flask = glassware("conical_flask", 250);
	const float line_y = Y + 0.55f * height_of("conical_flask", 250);
	const OpticsScene with = scene_of(flask, { lab::OpticsShape{ 0.0f, line_y, 1000.0f, 0.8f, 0.05 } }), without = scene_of(flask);
	const int column = static_cast<int>(X + 0.5f * W + 0.28f * W);
	int darkest = -1;
	double most = 0.0;
	for (int y = static_cast<int>(line_y) - 30; y < static_cast<int>(line_y) + 30; ++y) {
		const double seen = lab::optics_area_through(without, column, y, 550.0, 4) - lab::optics_area_through(with, column, y, 550.0, 4);
		if (seen > most) {
			most = seen;
			darkest = y;
		}
	}
	REQUIRE(darkest >= 0);
	CHECK(most > 0.01);
	CHECK(std::abs(darkest - static_cast<int>(line_y)) >= 2);
	MESSAGE("the line on row " << line_y << " is seen most on row " << darkest);
}

TEST_CASE("renderer: through a beaker's upright band its pixels are the 3D truth's; a flask keeps a uniform wall as it is (Stage 24)") {
	// a level ray through an upright band never meets the curves, and the renderer's pixel (its beam's footprint,
	// from its own rays: Stage 35) is the truth's (a grid of rays traced in three dimensions). A line drawn
	// upright on the wall, seen through the glass, is the test of it.
	const ItemView beaker = glassware("beaker", 250);
	const OpticsScene scene = scene_of(beaker, { lab::OpticsShape{ X + 37.0f, 0.0f, 0.6f, 1000.0f, 0.05 }, lab::OpticsShape{ X + 61.3f, 0.0f, 1.4f, 1000.0f, 0.15 } });
	const lab::glass::Vessel &g = *lab::glass::find("beaker", 250);
	const float *form = &scene.table[static_cast<size_t>(lab::optics_layout::HEADER_VECS + lab::optics_layout::FORM) * 4];
	const int row = static_cast<int>(Y + height_of("beaker", 250) - (g.floor + 0.4 * (g.level(100.0) - g.floor)) / form[1]);
	// pixel by pixel on the eye metric (visible above 8): the renderer's colour against 64 exact rays a pixel
	double worst = 0.0;
	int compared = 0;
	for (int x = static_cast<int>(X) + 3; x < static_cast<int>(X + W) - 3; x += sparse()) {
		double shown[3], truth[3];
		lab::optics_truth(scene, x, row, shown);
		lab::optics_area_truth(scene, x, row, 8, truth);
		worst = std::max(worst, eye(shown, truth));
		++compared;
	}
	MESSAGE("through a beaker's upright band, " << compared << " pixels: worst " << worst << " on the eye metric");
	CHECK(worst < lab::EYE_VISIBLE);
	// O7, the renderer's own reading, through a flask's cone and neck
	const ItemView flask = glassware("conical_flask", 250);
	const OpticsScene through_flask = scene_of(flask);
	const int h = static_cast<int>(height_of("conical_flask", 250));
	CHECK(lab::check_O7(through_flask, static_cast<int>(X), static_cast<int>(Y), static_cast<int>(X + W), static_cast<int>(Y) + h, 0.6) == "");
}

TEST_CASE("frames: a shaped vessel moved up or down settles on its new picture, not its old rows (Stage 24)") {
	// a cylinder's rows are alike, so a pixel's history survives its moving up or down; a flask's are not: a
	// pixel's history follows its row in the vessel (the burner's stand lifting a flask showed its old rows)
	const ItemView flask = glassware("conical_flask", 250);
	const float h = height_of("conical_flask", 250);
	lab::OpticsScene before, after;
	lab::optics_compose(before, room(), { lab::OpticsBody{ &flask, X, Y + 60.0f, W, h } });
	lab::optics_compose(after, room(), { lab::OpticsBody{ &flask, X, Y + 20.0f, W, h } });
	const int x0 = static_cast<int>(X), y0 = static_cast<int>(Y), w = static_cast<int>(W), hh = static_cast<int>(h) + 70;
	lab::OpticsFrames frames(x0, y0, w, hh, 64.0);
	for (int k = 0; k < 40; ++k) {
		frames.frame(before);
	}
	for (int k = 0; k < 40; ++k) {
		frames.frame(after);
	}
	double worst = 0.0;
	int looked = 0;
	for (int y = y0 + 25; y < y0 + static_cast<int>(h); y += 4 * sparse()) {
		for (int x = x0 + 20; x < x0 + 70; x += 4) {
			if (lab::optics_cover(after, x, y) <= 0.0) {
				continue; // beside its neck and its cone (Stage 26: a flask is its shape, not its rectangle): the wall, which the frames do not hold
			}
			double shown[3], truth[3];
			frames.colour(after, x, y, shown);
			lab::optics_truth(after, x, y, truth);
			worst = std::max(worst, eye(shown, truth));
			++looked;
		}
	}
	MESSAGE("a flask moved up 40 px: settled, " << looked << " pixels, worst " << worst << " on the eye metric from its new picture");
	CHECK(looked > 250 / sparse());
	CHECK(worst < lab::EYE_VISIBLE);
}

// ctest: threads 4
TEST_CASE("renderer: through a flask's curved rows, before a wall of upright lines, its pixels are the 3D truth's (Stage 25)") {
	// lines drawn up and down the wall: bending up and down changes nothing of them, so the curved rows are held
	// to the truth across, row by row (its rims apart: within a pixel of one, rays graze)
	std::vector<lab::OpticsShape> lines;
	for (int k = 0; k < 12; ++k) {
		lines.push_back(lab::OpticsShape{ X - 20.0f + 11.3f * static_cast<float>(k), 0.0f, 0.7f + 0.2f * static_cast<float>(k % 3), 1000.0f, 0.1 + 0.05 * (k % 4) });
	}
	const ItemView flask = glassware("conical_flask", 250);
	const OpticsScene scene = scene_of(flask, lines);
	const lab::glass::Vessel &g = *lab::glass::find("conical_flask", 250);
	const float h = height_of("conical_flask", 250), cm = static_cast<float>(2.0 * g.widest() / W);
	double worst = 0.0, sum = 0.0;
	int compared = 0, visible = 0;
	for (int y = static_cast<int>(Y) + 2; y < static_cast<int>(Y + h) - 2; y += 3 * sparse()) {
		const double y_cm = (Y + h - (y + 0.5f)) * cm, outer = g.radius_outside(y_cm) / cm, inner = g.radius_inside(y_cm) / cm;
		for (int x = static_cast<int>(X); x < static_cast<int>(X + W); x += 2) {
			const double off = std::fabs(x + 0.5 - (X + W / 2.0));
			if (lab::optics_cover(scene, x, y) < 0.99 || std::fabs(off - outer) < 1.5 || (inner > 0.0 && std::fabs(off - inner) < 1.5)) {
				continue;
			}
			double shown[3], truth[3];
			lab::optics_truth(scene, x, y, shown);
			lab::optics_area_truth(scene, x, y, 6, truth);
			const double e = eye(shown, truth);
			worst = std::max(worst, e);
			sum += e;
			visible += e >= lab::EYE_VISIBLE ? 1 : 0;
			++compared;
		}
	}
	MESSAGE("through a flask's curved rows, " << compared << " pixels: worst " << worst << ", mean " << sum / compared << " on the eye metric, " << visible << " visible");
	// a thin line's image is coloured by a cubic through a pixel's four nodes of the spectrum: at one pixel in a
	// thousand its fringe shows
	CHECK(compared > 300 / sparse());
	CHECK(visible <= compared / 500);
	CHECK(sum / compared < 1.0);
}
