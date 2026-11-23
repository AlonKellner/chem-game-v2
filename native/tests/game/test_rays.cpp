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
#include <cstdio>
#include <cstdlib>
#include <vector>

using lab::ItemView;
using lab::OpticsScene;
using lab::SubstanceView;

namespace lab {
void optics_beam_kinds(int64_t kinds[6], bool reset);
}

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

TEST_CASE("rays: how many a pixel follows to be read once, over a bench (a count, printed where LAB_RAY_STATS is set) (Stage 29)") {
	if (std::getenv("LAB_RAY_STATS") == nullptr) {
		return;
	}
	const std::vector<Stood> things = bench();
	const OpticsScene scene = scene_of(things);
	std::vector<int64_t> counts;
	int64_t kinds[6];
	lab::optics_beam_kinds(kinds, true);
	int64_t work[9];
	lab::optics_work(work, true);
	for (int y = 95; y < 335; ++y) {
		for (int x = 95; x < 465; ++x) {
			const int64_t rays = lab::optics_rays_for(scene, x, y);
			if (rays > 0) {
				counts.push_back(rays);
			}
		}
	}
	lab::optics_beam_kinds(kinds, true);
	std::printf("whole pixels: read whole %lld; one way but off by up to 0.02: %lld, 0.08: %lld, 0.32: %lld, more: %lld; rays part: %lld\n", static_cast<long long>(kinds[0]), static_cast<long long>(kinds[1]),
			static_cast<long long>(kinds[2]), static_cast<long long>(kinds[3]), static_cast<long long>(kinds[4]), static_cast<long long>(kinds[5]));
	lab::optics_work(work, true);
	const double rays_all = static_cast<double>(std::max<int64_t>(work[0], 1));
	std::printf("a ray's work: %lld rays; a ray tries %.2f bodies, searches %.2f meetings, tries %.1f quadrics and %.1f tori, solves %.2f tori's quartics, makes %.2f roots exact; past eight meetings %.2f%%, to the last allowed %.2f%%\n",
			static_cast<long long>(work[0]), static_cast<double>(work[1]) / rays_all, static_cast<double>(work[2]) / rays_all, static_cast<double>(work[3]) / rays_all, static_cast<double>(work[4]) / rays_all,
			static_cast<double>(work[5]) / rays_all, static_cast<double>(work[6]) / rays_all, 100.0 * static_cast<double>(work[7]) / rays_all, 100.0 * static_cast<double>(work[8]) / rays_all);
	std::sort(counts.begin(), counts.end());
	double total = 0.0;
	for (const int64_t c : counts) {
		total += static_cast<double>(c);
	}
	const auto at = [&](double share) { return counts[static_cast<size_t>(share * static_cast<double>(counts.size() - 1))]; };
	int64_t whole = 0, over_100 = 0, over_1000 = 0;
	for (const int64_t c : counts) {
		whole += c <= 20 ? 1 : 0;
		over_100 += c > 100 ? 1 : 0;
		over_1000 += c > 1000 ? 1 : 0;
	}
	std::printf("rays a pixel: %zu pixels, mean %.1f, median %lld, 90%% %lld, 99%% %lld, most %lld; read whole (20 rays) %.1f%%, over 100 rays %.1f%%, over 1000 rays %.1f%%\n", counts.size(),
			total / static_cast<double>(counts.size()), static_cast<long long>(at(0.5)), static_cast<long long>(at(0.9)), static_cast<long long>(at(0.99)), static_cast<long long>(counts.back()),
			100.0 * static_cast<double>(whole) / static_cast<double>(counts.size()), 100.0 * static_cast<double>(over_100) / static_cast<double>(counts.size()),
			100.0 * static_cast<double>(over_1000) / static_cast<double>(counts.size()));
}
