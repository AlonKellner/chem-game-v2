// The display's exposure (GAME_SPEC §12.2, Stage 90): the eye adapting as a pupil does, metered from the picture's
// 16 x 9 blocks, the brightest block's mean light; 1 in the room at its full light, under 1 where something outshines
// its white, over 1 (to 8) in a dimmed room; closing fast and opening slowly; the picture and the truth shown at it.
#include "lab/flame.hpp"
#include "lab/optics_scene.hpp"

#include <doctest/doctest.h>

#include <cmath>
#include <vector>

TEST_CASE("exposure: the room at its full light asks for 1, a light brighter than its white for less, a dimmed room for more, within 1/64 and 8 (Stage 90)") {
	// the room as it is: its brightest block no brighter than its white
	CHECK(lab::optics_exposure_target(0.73, 1.0) == doctest::Approx(1.0));
	CHECK(lab::optics_exposure_target(1.0, 1.0) == doctest::Approx(1.0));
	// a block twice the room's white: half
	CHECK(lab::optics_exposure_target(2.0, 1.0) == doctest::Approx(0.5));
	// the room's lights at a quarter: its picture brought back up, four times, as far as nothing outshines it
	CHECK(lab::optics_exposure_target(0.73 * 0.25, 0.25) == doctest::Approx(4.0));
	CHECK(lab::optics_exposure_target(0.5, 0.25) == doctest::Approx(2.0)); // (a glow in it: only so far)
	// its bounds
	CHECK(lab::optics_exposure_target(1000.0, 1.0) == doctest::Approx(1.0 / 64.0));
	CHECK(lab::optics_exposure_target(0.001, 0.01) == doctest::Approx(8.0));
}

TEST_CASE("exposure: it closes on a flare in a few tenths of a second and opens again over seconds, in its logarithm (Stage 90)") {
	const double dt = 1.0 / 60.0;
	// closing: from 1 toward 0.1, most of the way (95 % of the logarithm) in 0.3 s
	double e = 1.0;
	for (int f = 0; f < 18; ++f) {
		e = lab::optics_exposure_step(e, 0.1, dt);
	}
	CHECK(std::log(e) / std::log(0.1) == doctest::Approx(1.0 - std::exp(-3.0)).epsilon(0.02));
	// opening: from 0.1 back to 1, after 0.3 s only a third of the way, most of it in 2 s
	double o = 0.1;
	for (int f = 0; f < 18; ++f) {
		o = lab::optics_exposure_step(o, 1.0, dt);
	}
	CHECK(std::log(o) / std::log(0.1) > 0.6);
	for (int f = 18; f < 120; ++f) {
		o = lab::optics_exposure_step(o, 1.0, dt);
	}
	CHECK(std::log(o) / std::log(0.1) < 0.06);
	// at its target it stays; a frame of no time moves nothing
	CHECK(lab::optics_exposure_step(0.5, 0.5, dt) == doctest::Approx(0.5));
	CHECK(lab::optics_exposure_step(0.5, 1.0, 0.0) == doctest::Approx(0.5));
}

TEST_CASE("exposure: the meter reads the brightest of 16 x 9 blocks' mean light: a glint is nothing, a flame a block's worth (Stage 90)") {
	const int w = 1920, h = 1080;
	std::vector<double> pic(static_cast<size_t>(w * h * 3), 0.73); // the bench, a grey of the room's white
	CHECK(lab::optics_meter(pic, w, h) == doctest::Approx(0.73));
	// a glint: one pixel at a hundred times the room's white moves its block's mean by under a hundredth
	for (int c = 0; c < 3; ++c) {
		pic[(static_cast<size_t>(500) * w + 700) * 3 + static_cast<size_t>(c)] = 100.0;
	}
	CHECK(lab::optics_meter(pic, w, h) < 0.74);
	// a flame: 24 x 40 pixels at twenty times the room's white, inside one block (120 x 120)
	for (int y = 250; y < 290; ++y) {
		for (int x = 250; x < 274; ++x) {
			for (int c = 0; c < 3; ++c) {
				pic[(static_cast<size_t>(y) * w + static_cast<size_t>(x)) * 3 + static_cast<size_t>(c)] = 20.0;
			}
		}
	}
	const double expected = 0.73 + (20.0 - 0.73) * (24.0 * 40.0) / (120.0 * 120.0);
	CHECK(lab::optics_meter(pic, w, h) == doctest::Approx(expected).epsilon(1e-9));
	CHECK(lab::optics_exposure_target(lab::optics_meter(pic, w, h), 1.0) == doctest::Approx(1.0 / expected).epsilon(1e-9));
}

TEST_CASE("exposure: the picture and its truth at an exposure: the bench darker by it, a light brighter than the display its own colour still (Stage 90)") {
	// a sodium flame of the lab's burner over the bench
	const std::array<double, 401> sodium = lab::flame::glow({ { "Na", 2e-6 } }, 2200.15);
	lab::OpticsBody cone;
	cone.shape = lab::OpticsBody::Cone;
	cone.x = 300.0f;
	cone.y = 200.0f;
	cone.w = 24.0f;
	cone.h = 42.0f;
	cone.light = "flame_sooty";
	cone.light_to = "flame_blue";
	cone.toward = 1.0;
	cone.kelvin = 2200.15;
	cone.id = 5;
	cone.glow_add = &sodium;
	lab::OpticsRoom room;
	room.w = room.h = 1000;
	room.bench = 0.7305;
	room.elsewhere = 0.02;
	lab::OpticsScene scene;
	lab::optics_compose(scene, room, { cone });
	double bench1[4], flame1[4], bench_half[4], flame_half[4];
	lab::optics_pixel(scene, 600, 600, bench1);
	lab::optics_pixel(scene, 312, 230, flame1);
	scene.exposure = 0.25f;
	lab::optics_pixel(scene, 600, 600, bench_half);
	lab::optics_pixel(scene, 312, 230, flame_half);
	const auto linear = [](double v) { return v <= 0.04045 ? v / 12.92 : std::pow((v + 0.055) / 1.055, 2.4); };
	MESSAGE("the bench " << linear(bench1[0]) << " at 1, " << linear(bench_half[0]) << " at 0.25; the flame " << flame1[0] << " " << flame1[1] << " " << flame1[2] << " at 1, " << flame_half[0] << " " << flame_half[1] << " "
						 << flame_half[2] << " at 0.25");
	// the bench: a quarter of its light
	CHECK(linear(bench_half[0]) == doctest::Approx(0.25 * linear(bench1[0])).epsilon(0.01));
	// the flame, brighter than the display at either: its red the display's brightest, its hue orange-yellow both ways,
	// and at 0.25 more of its own colour against the bench's (its green and blue relatively lower)
	CHECK(flame1[0] == doctest::Approx(1.0).epsilon(0.01));
	CHECK(flame_half[0] == doctest::Approx(1.0).epsilon(0.01));
	CHECK(flame_half[2] <= flame1[2] + 1e-9);
}
