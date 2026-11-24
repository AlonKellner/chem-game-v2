// Everything that takes space is physical (GAME_SPEC §12.3, Stage 22): the lab's things that are no vessel
// are bodies of the optical pass, a slab, a bar or a cone of glowing gas, each made of what it reflects and
// what it emits; seen straight and through what is held, against the truth's own rays.
#include "lab/colour.hpp"
#include "lab/optics_scene.hpp"

#include "chem/optics.hpp"

#include "sparse.hpp"

#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <vector>

using lab::ItemView;
using lab::OpticsBody;
using lab::OpticsScene;
using lab::SubstanceView;

namespace {

constexpr double WALL = 0.7305;

lab::OpticsRoom room() {
	lab::OpticsRoom r;
	r.w = r.h = 1000;
	r.bench = WALL;
	r.elsewhere = 0.02;
	for (int k = 0; k < 40; ++k) { // something drawn behind: thin lines at odd places
		r.shapes.push_back(lab::OpticsShape{ 101.3f + 7.77f * static_cast<float>(k), 0.0f, 0.4f + 0.11f * static_cast<float>(k % 7), 1000.0f, 0.15 + 0.01 * (k % 5) });
	}
	return r;
}

OpticsBody slab(float x, float y, float w, float h, float radius, const char *surface, const char *light = nullptr) {
	OpticsBody b;
	b.shape = OpticsBody::Slab;
	b.x = x;
	b.y = y;
	b.w = w;
	b.h = h;
	b.size = radius;
	b.surface = surface;
	b.light = light;
	return b;
}

OpticsBody bar(float x0, float y0, float x1, float y1, float width, const char *surface) {
	OpticsBody b;
	b.shape = OpticsBody::Bar;
	b.x0 = x0;
	b.y0 = y0;
	b.x1 = x1;
	b.y1 = y1;
	b.size = width;
	b.surface = surface;
	return b;
}

OpticsBody flame(float x, float y, float w, float h, double toward) {
	OpticsBody b;
	b.shape = OpticsBody::Cone;
	b.x = x;
	b.y = y;
	b.w = w;
	b.h = h;
	b.light = "flame_sooty";
	b.light_to = "flame_blue";
	b.toward = toward;
	return b;
}

OpticsScene scene_of(const std::vector<OpticsBody> &bodies) {
	OpticsScene scene;
	lab::optics_compose(scene, room(), bodies);
	REQUIRE(lab::check_O1(scene) == "");
	return scene;
}

double eye(const double a[3], const double b[3]) {
	const auto seen = [](const double c[3]) {
		return lab::shown(chem::optics::Xyz{ 0.4124564 * c[0] + 0.3575761 * c[1] + 0.1804375 * c[2], 0.2126729 * c[0] + 0.7151522 * c[1] + 0.0721750 * c[2],
				0.0193339 * c[0] + 0.1191920 * c[1] + 0.9503041 * c[2] });
	};
	return lab::eye_difference(seen(a), seen(b));
}

// every pixel of a rectangle that a body covers, against 64 of the truth's rays: the worst on the eye metric
// (under the sanitizers every sparse()-th across: tests/support/sparse.hpp)
double worst_of(const OpticsScene &scene, int x0, int y0, int x1, int y1, int step, int &compared) {
	double worst = 0.0;
	for (int y = y0; y < y1; y += step) {
		for (int x = x0; x < x1; x += step * sparse()) {
			if (lab::optics_cover(scene, x, y) <= 0.0) {
				continue;
			}
			double shown[3], truth[3];
			lab::optics_truth(scene, x, y, shown);
			lab::optics_area_truth(scene, x, y, 8, truth);
			CAPTURE(x);
			CAPTURE(y);
			const double e = eye(shown, truth);
			CHECK(e < lab::EYE_VISIBLE);
			worst = std::max(worst, e);
			++compared;
		}
	}
	return worst;
}

double surface_at(const char *name, double nm) {
	return (*chem::optics::surface(name))[static_cast<size_t>(nm - 380.0)];
}
double emitter_at(const char *name, double nm) {
	return (*chem::optics::emitter(name))[static_cast<size_t>(nm - 380.0)];
}

} // namespace

TEST_CASE("things: a slab is opaque, of its face's reflectance, its corners rounded; its edges are what its rays bring (Stage 22)") {
	const OpticsScene scene = scene_of({ slab(200.3f, 150.6f, 60.0f, 36.0f, 6.0f, "part_face") });
	// inside, the room's light off its face
	for (const double nm : { 450.0, 550.0, 650.0 }) {
		CHECK(lab::optics_through(scene, 230, 168, nm) == doctest::Approx(surface_at("part_face", nm)).epsilon(1e-6));
	}
	// a corner is rounded: the pixel at the rectangle's very corner is mostly the wall
	CHECK(lab::optics_cover(scene, 201, 151) < 0.5);
	CHECK(lab::optics_cover(scene, 210, 151) > 0.5);
	int compared = 0;
	const double worst = worst_of(scene, 198, 148, 264, 190, 2, compared);
	CHECK(compared > 400 / sparse());
	MESSAGE(compared << " pixels of a slab against 64 rays each: worst " << worst << " on the eye metric");
}

TEST_CASE("things: a bar is every place within half its width of its line, at any angle (Stage 22)") {
	const OpticsScene scene = scene_of({ bar(210.0f, 160.0f, 330.7f, 221.2f, 3.0f, "wire") });
	// on its line, the wire's colour; a pixel beside it, the wall's lines
	CHECK(lab::optics_through(scene, 270, 190, 550.0) == doctest::Approx(surface_at("wire", 550.0)).epsilon(0.05));
	CHECK(lab::optics_cover(scene, 270, 200) == 0.0);
	int compared = 0;
	const double worst = worst_of(scene, 206, 156, 336, 226, 1, compared);
	CHECK(compared > 300 / sparse());
	MESSAGE(compared << " pixels of a bar against 64 rays each: worst " << worst);
}

TEST_CASE("things: a lamp's face glows: the room's light off its glass and its own light (Stage 22)") {
	const OpticsScene lit = scene_of({ slab(300.0f, 200.0f, 12.0f, 12.0f, 6.0f, "lamp_glass", "lamp_green") });
	const OpticsScene dark = scene_of({ slab(300.0f, 200.0f, 12.0f, 12.0f, 6.0f, "lamp_glass") });
	for (const double nm : { 450.0, 530.0, 620.0 }) {
		CHECK(lab::optics_through(lit, 306, 206, nm) == doctest::Approx(surface_at("lamp_glass", nm) + emitter_at("lamp_green", nm)).epsilon(1e-5));
		CHECK(lab::optics_through(dark, 306, 206, nm) == doctest::Approx(surface_at("lamp_glass", nm)).epsilon(1e-5));
	}
	double green[3];
	lab::optics_truth(lit, 306, 206, green);
	CHECK(green[1] > 2.0 * green[0]);
	CHECK(green[1] > 2.0 * green[2]);
	CHECK(lab::check_O2(lit, 306, 206) == ""); // brighter than the wall: it emits
}

TEST_CASE("things: a flame is glowing gas: the wall is seen through it, with its light added along the path (Stage 22)") {
	const OpticsBody f = flame(400.0f, 100.0f, 24.0f, 40.0f, 0.0);
	const OpticsScene scene = scene_of({ f });
	OpticsScene bare;
	lab::optics_compose(bare, room(), {});
	// a cone seen from the side: at its base the path on its axis is its width; half way up, half that
	const double nm = 640.0;
	const double base = lab::optics_through(scene, 411, 138, nm) - lab::optics_through(bare, 411, 138, nm);
	const double half = lab::optics_through(scene, 411, 118, nm) - lab::optics_through(bare, 411, 118, nm);
	CHECK(base == doctest::Approx(emitter_at("flame_sooty", nm) * 2.0 * std::sqrt(12.0 * 12.0 * std::pow(38.5 / 40.0, 2.0) - 0.25)).epsilon(0.02));
	CHECK(half / base == doctest::Approx(0.5).epsilon(0.08));
	// it absorbs nothing: what is drawn behind it is seen through it (a line of the wall)
	for (int x = 400; x < 424; ++x) {
		const double through_flame = lab::optics_through(scene, x, 130, 450.0) - lab::optics_through(bare, x, 130, 450.0);
		CHECK(through_flame >= -1e-9);
	}
	// open, it is blue: the bands of CH and C2
	const OpticsScene blue = scene_of({ flame(400.0f, 100.0f, 24.0f, 40.0f, 1.0) });
	double sooty[3], open[3];
	lab::optics_truth(scene, 411, 138, sooty);
	lab::optics_truth(blue, 411, 138, open);
	CHECK(sooty[0] > sooty[2]);
	CHECK(open[2] > open[0]);
	CHECK(lab::check_O2(scene, 411, 138) == "");
	int compared = 0;
	const double worst = worst_of(scene, 398, 98, 426, 142, 1, compared);
	CHECK(compared > 300 / sparse());
	MESSAGE(compared << " pixels of a flame against 64 rays each: worst " << worst);
}

TEST_CASE("things: things of one make share a set of spectra (Stage 22)") {
	std::vector<OpticsBody> parts;
	for (int k = 0; k < 50; ++k) {
		parts.push_back(slab(20.0f + 40.0f * static_cast<float>(k % 10), 20.0f + 50.0f * static_cast<float>(k / 10), 36.0f, 36.0f, 4.0f, "part_face"));
	}
	parts.push_back(slab(600.0f, 600.0f, 12.0f, 12.0f, 6.0f, "lamp_glass", "lamp_green"));
	const OpticsScene scene = scene_of(parts);
	namespace L = lab::optics_layout;
	CHECK(scene.rows.size() == static_cast<size_t>(2 * L::ROWS_PER_SET * L::ROW));
	CHECK(scene.table[0] == 51.0f);
}

TEST_CASE("things: seen through a held vessel of water, a part and a flame standing behind it are bent as the wall is (Stage 22)") {
	ItemView water;
	water.id = "jar";
	water.kind = "vessel";
	water.volume_ml = 50.0;
	water.celsius = 20.0;
	water.liquid_ml = water.condensed_ml = 45.0;
	SubstanceView w;
	w.species = "water";
	w.phase = 'l';
	w.mol = 2.5;
	w.g = 45.0;
	SubstanceView g;
	g.species = "glass";
	g.phase = 's';
	g.mol = 0.83;
	g.g = 50.0;
	g.wall = true;
	water.contents = { w, g };
	water.wall_mm = 0.8;
	water.floor_mm = 1.9;
	OpticsBody hand{ &water, 210.0f, 120.0f, 70.0f, 87.5f };
	hand.layer = 1;
	const OpticsScene scene = scene_of({ slab(225.0f, 150.0f, 40.0f, 40.0f, 5.0f, "circuit_face"), flame(248.0f, 140.0f, 20.0f, 34.0f, 0.5), hand });
	int compared = 0, moved = 0;
	double worst = 0.0;
	for (const int y : { 155, 165, 172, 185 }) {
		for (int x = 214; x < 276; x += 3) {
			double shown[3], truth[3];
			lab::optics_truth(scene, x, y, shown);
			lab::optics_area_truth(scene, x, y, 8, truth);
			CAPTURE(x);
			CAPTURE(y);
			const double e = eye(shown, truth);
			CHECK(e < lab::EYE_VISIBLE);
			worst = std::max(worst, e);
			++compared;
			moved += std::fabs(lab::optics_exact_bend(scene, x, y, 550.0) - (x + 0.5)) > 5.0 ? 1 : 0;
		}
	}
	CHECK(compared > 70);
	CHECK(moved > 30); // the water turns the picture over: what is seen of the part and the flame is elsewhere
	MESSAGE(compared << " pixels of a part and a flame through a held vessel of water, against 64 rays each: worst " << worst);
}
