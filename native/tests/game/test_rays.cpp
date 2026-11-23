// A pixel's own rays (GAME_SPEC §12.3, Stage 29): the pass traces its rays itself, at four wavelengths, and
// reads every band from them. What that rests on, each as an invariant of its own (§18): the pass's rays are
// the truth's (O9), four nodes speak for a spectrum (O10), and a pixel's parts make it whole (O11).
#include "lab/glassware.hpp"
#include "lab/optics_scene.hpp"
#include "lab/prism_demo.hpp"

#include "sparse.hpp"

#include <doctest/doctest.h>

#include <algorithm>
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
	for (int c = 0; c < 8; ++c) { // a grid, as the game draws on its wall
		r.shapes.push_back(lab::OpticsShape{ 40.0f + 60.0f * static_cast<float>(c), 0.0f, 1.0f, 1000.0f, 0.55 });
	}
	r.shapes.push_back(lab::OpticsShape{ 0.0f, 150.0f, 1000.0f, 1.0f, 0.55 });
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

// a vessel made as glassware, holding water
ItemView glassware(const char *kind, double nominal_ml, double ml) {
	const lab::glass::Vessel *g = lab::glass::find(kind, nominal_ml);
	REQUIRE(g != nullptr);
	ItemView it;
	it.id = kind;
	it.kind = "vessel";
	it.glassware = kind;
	it.nominal_ml = nominal_ml;
	it.volume_ml = g->capacity();
	it.celsius = 20.0;
	it.liquid_ml = it.condensed_ml = ml;
	it.contents = { substance("water", 'l', ml / 18.0, ml), substance("copper_sulfate_aq", 'a', 0.02, 3.2), substance("glass", 's', 0.83, 50.0, true) };
	it.wall_mm = 10.0 * g->wall;
	it.floor_mm = 10.0 * g->floor;
	return it;
}

struct Stood {
	ItemView item;
	float x, y, w, h;
	int layer;
};

Stood stood(const char *kind, double nominal_ml, double ml, float x, float y, float w, int layer = 0) {
	const lab::glass::Vessel *g = lab::glass::find(kind, nominal_ml);
	REQUIRE(g != nullptr);
	return Stood{ glassware(kind, nominal_ml, ml), x, y, w, static_cast<float>(w * g->height / (2.0 * g->widest())), layer };
}

OpticsScene scene_of(const std::vector<Stood> &things) {
	std::vector<lab::OpticsBody> bodies;
	for (const Stood &s : things) {
		lab::OpticsBody b{ &s.item, s.x, s.y, s.w, s.h };
		b.layer = s.layer;
		bodies.push_back(b);
	}
	OpticsScene scene;
	lab::optics_compose(scene, room(), bodies);
	REQUIRE(lab::check_O1(scene) == "");
	return scene;
}

// the three kinds of glassware side by side, and a beaker held over the flask
std::vector<Stood> bench() {
	return { stood("beaker", 250, 120, 100, 100, 90), stood("conical_flask", 250, 100, 230, 100, 90), stood("bottle", 500, 300, 360, 100, 100), stood("beaker", 50, 30, 250, 130, 62, 1) };
}

// a tracer whose glass is another glass: every ray a little off (what O9 rules out)
lab::OpticsRay another_glass(const OpticsScene &scene, double x, double y, double nm) {
	return lab::optics_ray(scene, x, y, nm + 40.0);
}

// a reading that drops a sliver of every pixel (what O11 rules out)
double sliver_lost(const OpticsScene &scene, int x, int y) {
	return lab::optics_made(scene, x, y) - 1.0e-6;
}

} // namespace

TEST_CASE("O9: the pass's rays are the truth's: through a beaker, a flask, a bottle, and a vessel held over another (Stage 29)") {
	const std::vector<Stood> things = bench();
	const OpticsScene scene = scene_of(things);
	const int step = 3 * sparse();
	CHECK(lab::check_O9(scene, 95, 95, 465, 330, step) == "");
	// it fires on a tracer whose glass bends the light as another wavelength's
	CHECK(lab::check_O9(scene, 95, 95, 465, 330, step, another_glass).rfind("O9", 0) == 0);
}

TEST_CASE("O10: a pixel's four nodes speak for each stretch of the spectrum it is read in: through glassware, and through a prism (Stage 29)") {
	const std::vector<Stood> things = bench();
	const OpticsScene scene = scene_of(things);
	CHECK(lab::check_O10(scene, 95, 95, 465, 330, 3 * sparse()) == "");
	// a prism of flint glass spreads the light too far for four nodes: its pixels' spectra are halved until
	// four speak for each stretch
	lab::PrismDemo demo;
	const OpticsScene &prism = demo.optics();
	const int x0 = static_cast<int>(demo.centre_x()) - 40, y0 = static_cast<int>(demo.centre_y()) - 20, x1 = static_cast<int>(demo.centre_x()) + 40, y1 = static_cast<int>(demo.centre_y()) + 20;
	CHECK(lab::check_O10(prism, x0, y0, x1, y1, 4) == "");
	// and it fires on a reading that never halves a stretch: the whole spectrum by four nodes
	CHECK(lab::check_O10(prism, x0, y0, x1, y1, 4, lab::optics_stretches_unhalved).rfind("O10", 0) == 0);
}

TEST_CASE("O11: a pixel's parts make it whole: however it is cut, it is read from all of itself, once (Stage 29)") {
	const std::vector<Stood> things = bench();
	const OpticsScene scene = scene_of(things);
	// every pixel of the held beaker and of what it stands before: rims, floors, the liquids' surfaces
	CHECK(lab::check_O11(scene, 225, 95, 335, 240) == "");
	// it fires on a reading that loses a sliver
	CHECK(lab::check_O11(scene, 225, 95, 335, 240, sliver_lost).rfind("O11", 0) == 0);
}

TEST_CASE("O7: through every kind of glassware and a held vessel a uniform wall stays as it is, at any wavelength and over any band (Stage 29)") {
	const std::vector<Stood> things = bench();
	const OpticsScene scene = scene_of(things);
	// the held beaker and what it stands before, whole; a strip down each of the others, rim to foot
	CHECK(lab::check_O7(scene, 225, 95, 335, 240, 0.6) == "");
	CHECK(lab::check_O7(scene, 97, 95, 109, 240, 0.6) == "");
	CHECK(lab::check_O7(scene, 400, 95, 412, 330, 0.6) == "");
}
