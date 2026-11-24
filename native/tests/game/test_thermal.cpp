// A thermal camera's search (GAME_SPEC §13, Stage 48): its hottest and coldest places, a step at a time under a
// budget, are every sensor pixel's (O26), however thin what is hottest.
#include "lab/thermal.hpp"

#include "chem/optics.hpp"

#include <doctest/doctest.h>

#include <vector>

using lab::OpticsBody;
using lab::OpticsScene;

namespace {

constexpr double ROOM_K = 293.15;

OpticsBody slab(float x, float y, float w, float h, double kelvin, const char *surface = "part_face") {
	OpticsBody b;
	b.shape = OpticsBody::Slab;
	b.x = x;
	b.y = y;
	b.w = w;
	b.h = h;
	b.size = 2.0f;
	b.surface = surface;
	b.kelvin = kelvin;
	return b;
}

// a stream a pixel wide, down from (x, y)
OpticsBody stream(float x, float y, float length, double kelvin) {
	OpticsBody b;
	b.shape = OpticsBody::Bar;
	b.x0 = b.x1 = x;
	b.y0 = y;
	b.y1 = y + length;
	b.size = 1.0f;
	b.surface = "wire";
	b.kelvin = kelvin;
	return b;
}

OpticsScene scene_of(const std::vector<OpticsBody> &bodies) {
	lab::OpticsRoom r;
	r.w = r.h = 1000;
	r.kelvin = ROOM_K;
	OpticsScene scene;
	lab::optics_compose(scene, r, bodies);
	REQUIRE(lab::check_O1(scene) == "");
	return scene;
}

} // namespace

TEST_CASE("thermal: the search finds every pixel's hottest and coldest, a thin hot stream too (O26, Stage 48)") {
	// a warm face, a cold one, and a hot stream a pixel wide across an odd column; the camera's own case hidden
	OpticsBody own = slab(100, 100, 70, 50, 400.0);
	own.hidden = 1;
	const OpticsScene scene = scene_of({ slab(110, 110, 20, 15, 330.0), slab(140, 125, 15, 10, 280.0), stream(127.5f, 104, 30, 420.0), own });
	const lab::ThermalFrame frame{ 100.0, 100.0, 60, 45, 1 };
	const lab::ThermalReading brute = lab::thermal_brute(scene, frame);
	MESSAGE("every pixel: the hottest " << lab::thermal_celsius(brute.hottest.signal, ROOM_K, 0.95, ROOM_K) << " C at (" << brute.hottest.x << ", " << brute.hottest.y << "), the coldest "
									  << lab::thermal_celsius(brute.coldest.signal, ROOM_K, 0.95, ROOM_K) << " C");
	CHECK(brute.hottest.x == 27); // the stream's column
	lab::ThermalSearch search;
	search.start(frame);
	int steps = 0;
	while (!search.step(scene, 200)) {
		++steps; // a step at a time, as a frame's budget gives it
	}
	CHECK(steps > 3);
	CHECK(lab::check_O26(search.reading(), brute, ROOM_K, 0.95, ROOM_K) == "");
	CHECK(search.reading().hottest.x == 27);
	// its centre's spot: 3 x 3 pixels about the frame's middle
	CHECK(search.reading().centre.signal == lab::optics_lwir_signal(scene, 130.0, 122.5, 3.0, lab::ThermalSearch::FINE_BANDS, 1));
	// it fires: the first pass alone misses the stream's own pixel
	lab::ThermalSearch coarse;
	coarse.start(frame, true);
	while (!coarse.step(scene, 100000)) {
	}
	CHECK(lab::check_O26(coarse.reading(), brute, ROOM_K, 0.95, ROOM_K) != "");
}
