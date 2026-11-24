// The spectral viewer (GAME_SPEC §12, §16; Stage 42): its probe reads what reaches the eye from a small disc of
// the screen at every nanometre, ray by ray through points uniform over the disc, and settles to the disc's own
// integral (O19).
#include "lab/colour.hpp"
#include "lab/optics_scene.hpp"
#include "lab/spectrometer.hpp"

#include "chem/optics.hpp"

#include <doctest/doctest.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <set>
#include <string>
#include <vector>

using lab::ItemView;
using lab::OpticsBody;
using lab::OpticsScene;
using lab::Spectrometer;
using lab::SubstanceView;

namespace {

constexpr double WALL = 0.7305;

lab::OpticsRoom room() {
	lab::OpticsRoom r;
	r.w = r.h = 1000;
	r.bench = WALL;
	r.elsewhere = 0.02;
	for (int k = 0; k < 12; ++k) { // something drawn behind: thin lines
		r.shapes.push_back(lab::OpticsShape{ 101.3f + 31.7f * static_cast<float>(k), 0.0f, 0.6f, 1000.0f, 0.15 });
	}
	return r;
}

OpticsBody slab(float x, float y, float w, float h, const char *surface, const char *light = nullptr) {
	OpticsBody b;
	b.shape = OpticsBody::Slab;
	b.x = x;
	b.y = y;
	b.w = w;
	b.h = h;
	b.size = 3.0f;
	b.surface = surface;
	b.light = light;
	return b;
}

OpticsBody flame(float x, float y, float w, float h) {
	OpticsBody b;
	b.shape = OpticsBody::Cone;
	b.x = x;
	b.y = y;
	b.w = w;
	b.h = h;
	b.light = "flame_sooty";
	b.light_to = "flame_blue";
	b.toward = 1.0;
	return b;
}

ItemView water_jar() {
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
	return water;
}

OpticsScene scene_of(const std::vector<OpticsBody> &bodies) {
	OpticsScene scene;
	lab::optics_compose(scene, room(), bodies);
	REQUIRE(lab::check_O1(scene) == "");
	return scene;
}

// a probe at (x, y) that has read every nanometre `rays` times
Spectrometer settled_at(const OpticsScene &scene, double x, double y, int rays, Spectrometer::Sampling sampling = Spectrometer::Sampling::Disc) {
	Spectrometer probe;
	probe.set_sampling(sampling);
	probe.aim(scene, x, y, lab::PROBE_RADIUS_PX);
	probe.read(scene, static_cast<int64_t>(rays) * lab::SPECTRUM_NM);
	return probe;
}

const std::vector<int> SOME_NM = { 20, 120, 170, 250, 330 }; // 400, 500, 550, 630 and 710 nm

} // namespace

TEST_CASE("viewer: a ray at a point is the pass's ray, held to the truth's own (Stage 42)") {
	OpticsBody jar{ nullptr, 300.0f, 200.0f, 70.0f, 87.5f };
	const ItemView water = water_jar();
	jar.item = &water;
	const OpticsScene scene = scene_of({ slab(100.0f, 100.0f, 60.0f, 60.0f, "part_face"), flame(200.0f, 100.0f, 24.0f, 40.0f), jar });
	// beside everything a point is the pixel's light where the pixel is all one (the wall between its lines)
	CHECK(lab::optics_point(scene, 620.5, 600.5, 550.0) == doctest::Approx(lab::optics_through(scene, 620, 600, 550.0)).epsilon(1e-9));
	CHECK(lab::optics_point(scene, 130.5, 130.5, 550.0) == doctest::Approx(lab::optics_through(scene, 130, 130, 550.0)).epsilon(1e-9));
	// on a part, in a flame and through the water: the pass's ray and the truth's bring the same light
	int compared = 0, apart = 0;
	// (off the edges of what is drawn: a point on an edge is on both sides of it)
	for (double y = 104.2513; y < 290.0; y += 7.7071) {
		for (double x = 96.5137; x < 375.0; x += 5.3119) {
			for (const double nm : { 430.0, 551.0, 660.0 }) {
				const double pass = lab::optics_point(scene, x, y, nm), truth = lab::optics_point_truth(scene, x, y, nm);
				++compared;
				const bool off = std::fabs(pass - truth) > 1e-3 * std::max(1.0, std::fabs(truth));
				apart += off ? 1 : 0;
				if (off && apart < 10) {
					MESSAGE("apart at " << x << ", " << y << " nm " << nm << ": " << pass << " against " << truth);
				}
			}
		}
	}
	CHECK(compared > 3000);
	CHECK(apart < compared / 200); // (a ray that grazes a surface may go another way: O9's few)
	MESSAGE(compared << " rays, " << apart << " of them apart from the truth's");
}

TEST_CASE("viewer: the probe reads every nanometre once a sweep, coarse first (Stage 42)") {
	const std::array<int, lab::SPECTRUM_NM> &order = lab::reading_order();
	std::set<int> all(order.begin(), order.end());
	CHECK(all.size() == static_cast<size_t>(lab::SPECTRUM_NM));
	CHECK(*all.begin() == 0);
	CHECK(*all.rbegin() == 400);
	for (int i = 0; i < 26; ++i) {
		CHECK(order[static_cast<size_t>(i)] % 16 == 0);
	}
	// the points of a nanometre lie evenly over the disc: as many in each quarter, as many within half its
	// radius as a quarter of them
	for (const int k : { 0, 170, 400 }) {
		int inner = 0, quarter[4] = {};
		const int n = 4096;
		for (int i = 0; i < n; ++i) {
			double u = 0.0, v = 0.0;
			lab::disc_point(k, i, u, v);
			REQUIRE(u * u + v * v <= 1.0);
			inner += u * u + v * v < 0.25 ? 1 : 0;
			++quarter[(u >= 0.0 ? 1 : 0) + (v >= 0.0 ? 2 : 0)];
		}
		CHECK(std::abs(inner - n / 4) < n / 100);
		for (const int q : quarter) {
			CHECK(std::abs(q - n / 4) < n / 100);
		}
	}
	// read short, it shows the whole spectrum: straight lines between the nanometres it has
	const OpticsScene scene = scene_of({ flame(200.0f, 100.0f, 24.0f, 40.0f) });
	Spectrometer probe;
	probe.aim(scene, 212.0, 135.0, lab::PROBE_RADIUS_PX);
	CHECK(probe.read(scene, 26) == 26);
	CHECK(probe.read_count() == 26);
	const std::array<double, lab::SPECTRUM_NM> c = probe.curve();
	CHECK(c[8] == doctest::Approx(0.5 * (probe.mean()[0] + probe.mean()[16])));
	CHECK(c[400] == probe.mean()[400]);
	CHECK(!probe.settled());
	// the same aim leaves it be; a move starts it again
	probe.aim(scene, 212.0, 135.0, lab::PROBE_RADIUS_PX);
	CHECK(probe.read_count() == 26);
	probe.aim(scene, 213.0, 135.0, lab::PROBE_RADIUS_PX);
	CHECK(probe.read_count() == 0);
	// read to the end, it is settled and reads nothing more
	CHECK(probe.read(scene, 100000000) == lab::SPECTRUM_NM * lab::PROBE_SAMPLES_MOST);
	CHECK(probe.settled());
	CHECK(probe.read(scene, 10) == 0);
}

TEST_CASE("viewer: on the wall the probe reads the wall's light, in the room's radiance (Stage 42)") {
	const OpticsScene scene = scene_of({});
	const Spectrometer probe = settled_at(scene, 640.0, 520.0, 4);
	for (int k = 0; k < lab::SPECTRUM_NM; k += 25) {
		CHECK(probe.mean()[static_cast<size_t>(k)] == doctest::Approx(WALL).epsilon(1e-6));
	}
	// the room's white: D65 at 100 cd/m2
	double lum = 0.0;
	for (int k = 0; k < lab::SPECTRUM_NM; ++k) {
		lum += 683.0 * lab::radiance_of(1.0, k) * chem::optics::ybar()[static_cast<size_t>(k)];
	}
	CHECK(lum == doctest::Approx(lab::ROOM_WHITE_CD_M2).epsilon(1e-9));
}

TEST_CASE("viewer: O19, the probe settles to its disc: across an edge, in a flame, through water (Stage 42)") {
	OpticsBody jar{ nullptr, 300.0f, 200.0f, 70.0f, 87.5f };
	const ItemView water = water_jar();
	jar.item = &water;
	const OpticsScene scene = scene_of({ slab(100.0f, 100.0f, 60.0f, 60.0f, "part_face"), flame(200.0f, 100.0f, 24.0f, 40.0f), jar });
	struct At {
		double x, y;
		const char *what;
	};
	CHECK(lab::optics_ray(scene, 335.0, 250.0, 550.0).bent > 0); // the water bends what is seen through it
	for (const At &at : { At{ 100.0, 130.0, "a part's edge" }, At{ 212.0, 130.0, "a flame" }, At{ 335.0, 250.0, "the water" }, At{ 302.0, 250.0, "the jar's rim" } }) {
		CAPTURE(std::string(at.what));
		const auto start = std::chrono::steady_clock::now();
		const Spectrometer probe = settled_at(scene, at.x, at.y, 256);
		const double us = std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - start).count() / (256.0 * lab::SPECTRUM_NM);
		CHECK(lab::check_O19(scene, probe, SOME_NM) == "");
		MESSAGE("the probe on " << std::string(at.what) << ": " << us << " us a ray");
	}
}

TEST_CASE("viewer: O19 fires on a probe that samples its disc wrongly or does not start again (Stage 42)") {
	const OpticsScene scene = scene_of({ slab(100.0f, 100.0f, 60.0f, 60.0f, "part_face"), flame(200.0f, 100.0f, 24.0f, 40.0f) });
	// an edge off the disc's middle: the centre alone, the disc's square, points crowded to the middle each read
	// another share of the part than the disc holds (the square's by a twentieth of the light: told apart only
	// settled, every nanometre's rays read)
	const double x = 102.0, y = 130.0;
	const Spectrometer right = settled_at(scene, x, y, lab::PROBE_SAMPLES_MOST);
	CHECK(right.settled());
	CHECK(lab::check_O19(scene, right, SOME_NM) == "");
	CHECK(lab::check_O19(scene, settled_at(scene, x, y, lab::PROBE_SAMPLES_MOST, Spectrometer::Sampling::Centre), SOME_NM) != "");
	CHECK(lab::check_O19(scene, settled_at(scene, x, y, lab::PROBE_SAMPLES_MOST, Spectrometer::Sampling::Square), SOME_NM) != "");
	CHECK(lab::check_O19(scene, settled_at(scene, x, y, lab::PROBE_SAMPLES_MOST, Spectrometer::Sampling::ByRadius), SOME_NM) != "");
	// moved into the flame without starting again: the part's light is still in its means
	Spectrometer probe;
	probe.set_restarting(false);
	probe.aim(scene, x, y, lab::PROBE_RADIUS_PX);
	probe.read(scene, 128 * lab::SPECTRUM_NM);
	probe.aim(scene, 212.0, 130.0, lab::PROBE_RADIUS_PX);
	probe.read(scene, 128 * lab::SPECTRUM_NM);
	CHECK(lab::check_O19(scene, probe, SOME_NM) != "");
}

namespace {

std::array<uint8_t, 3> pixel(const lab::Picture &p, int x, int y) {
	const uint8_t *at = p.at(x, y);
	return { at[0], at[1], at[2] };
}

double hue_of(const std::array<uint8_t, 3> &c) {
	double lin[3];
	for (int i = 0; i < 3; ++i) {
		const double v = c[static_cast<size_t>(i)] / 255.0;
		lin[i] = v <= 0.04045 ? v / 12.92 : std::pow((v + 0.055) / 1.055, 2.4);
	}
	const lab::CieLab l = lab::to_lab(chem::optics::Xyz{ 0.4124564 * lin[0] + 0.3575761 * lin[1] + 0.1804375 * lin[2], 0.2126729 * lin[0] + 0.7151522 * lin[1] + 0.0721750 * lin[2],
			0.0193339 * lin[0] + 0.1191920 * lin[1] + 0.9503041 * lin[2] });
	const double h = std::atan2(l.b, l.a) * 180.0 / 3.141592653589793;
	return h < 0.0 ? h + 360.0 : h;
}

// a reading with one peak, a Gaussian at `centre` nanometres of the first
std::array<double, lab::SPECTRUM_NM> peaked(double centre, double height) {
	std::array<double, lab::SPECTRUM_NM> c{};
	for (int k = 0; k < lab::SPECTRUM_NM; ++k) {
		const double d = (lab::SPECTRUM_FIRST_NM + k - centre) / 20.0;
		c[static_cast<size_t>(k)] = height * (0.05 + std::exp(-d * d));
	}
	return c;
}

} // namespace

TEST_CASE("viewer: the plot's columns are the wavelengths, its strip the rainbow, its axes and ticks in place (Stage 42)") {
	lab::Picture p;
	lab::plot_spectrum(peaked(530.0, 1.0), lab::SPECTRUM_NM, p);
	CHECK(p.w == lab::SCREEN_W);
	CHECK(p.h == lab::SCREEN_H);
	const auto &bow = lab::rainbow();
	// the strip: each column its nanometre's colour, every row of it
	for (int k = 0; k < lab::SPECTRUM_NM; ++k) {
		CHECK(pixel(p, lab::PLOT_X + k, lab::STRIP_TOP) == bow[static_cast<size_t>(k)]);
		CHECK(pixel(p, lab::PLOT_X + k, lab::STRIP_TOP + lab::STRIP_ROWS - 1) == bow[static_cast<size_t>(k)]);
	}
	// the rainbow runs from violet through blue, green, yellow and orange to red: its hue falls all the way (by
	// every 25 nm; nanometre by nanometre, to the display's 8 bits)
	double was = 400.0, step_was = 400.0;
	for (int nm = 450; nm <= 650; ++nm) {
		const double h = hue_of(bow[static_cast<size_t>(nm - lab::SPECTRUM_FIRST_NM)]);
		CAPTURE(nm);
		CHECK(h <= was + 1.5); // (an 8-bit colour's hue is a degree or so either way)
		was = h;
		if (nm % 25 == 0) {
			CHECK(h < step_was);
			step_was = h;
		}
	}
	CHECK(hue_of(bow[70]) > 240.0); // 450 nm: blue
	CHECK(hue_of(bow[150]) > 120.0); // 530 nm: green
	CHECK(hue_of(bow[150]) < 170.0);
	CHECK(hue_of(bow[270]) < 50.0); // 650 nm: red
	// the ticks every 50 nm, the axis over the strip, nothing between the ticks
	for (int nm = 400; nm <= 750; nm += 50) {
		CHECK(pixel(p, lab::PLOT_X + nm - 380, lab::TICK_TOP)[0] > 100);
		CHECK(pixel(p, lab::PLOT_X + nm - 380 + 25, lab::TICK_TOP)[0] == 0);
	}
	CHECK(pixel(p, lab::PLOT_X + 10, lab::AXIS_ROW)[0] > 100);
	// the peak's column reaches the top of the plot in white, and its colour fills it under the curve
	CHECK(pixel(p, lab::PLOT_X + 150, lab::PLOT_TOP)[0] > 200);
	CHECK(pixel(p, lab::PLOT_X + 150, lab::PLOT_TOP)[2] > 200);
	const std::array<uint8_t, 3> fill = pixel(p, lab::PLOT_X + 151, lab::PLOT_BOTTOM);
	CHECK(fill[1] > fill[0]);
	CHECK(fill[1] > fill[2]);
	CHECK(fill[1] < bow[151][1]);
	// far from the peak the curve is low: the plot above it is dark
	CHECK(pixel(p, lab::PLOT_X + 10, lab::PLOT_TOP + 20)[0] < 40);
}

TEST_CASE("viewer: the plot scales to its peak; nothing read, no curve (Stage 42)") {
	lab::Picture a, b, none, odd;
	lab::plot_spectrum(peaked(610.0, 0.2), lab::SPECTRUM_NM, a);
	lab::plot_spectrum(peaked(610.0, 0.4), lab::SPECTRUM_NM, b);
	CHECK(a.rgb == b.rgb);
	CHECK(a.hash == b.hash);
	lab::plot_spectrum({}, 0, none);
	lab::Picture zero;
	lab::plot_spectrum({}, lab::SPECTRUM_NM, zero);
	CHECK(none.rgb == zero.rgb);
	std::array<double, lab::SPECTRUM_NM> broken = peaked(610.0, 0.2);
	broken[3] = std::nan("");
	broken[4] = -1.0;
	lab::plot_spectrum(broken, lab::SPECTRUM_NM, odd);
	for (int k = 0; k < lab::SPECTRUM_NM; ++k) {
		CHECK(pixel(none, lab::PLOT_X + k, lab::PLOT_TOP + 1)[0] < 40); // the grid alone
	}
	CHECK(pixel(odd, lab::PLOT_X + 3, lab::PLOT_TOP + 1)[0] < 40);
}

TEST_CASE("viewer: one reading, one picture (Stage 42)") {
	lab::Picture a, b, c;
	lab::plot_spectrum(peaked(480.0, 1.0), lab::SPECTRUM_NM, a);
	lab::plot_spectrum(peaked(480.0, 1.0), lab::SPECTRUM_NM, b);
	lab::plot_spectrum(peaked(482.0, 1.0), lab::SPECTRUM_NM, c);
	CHECK(a.hash == b.hash);
	CHECK(a.hash != c.hash);
	CHECK(a.rgb != c.rgb);
}
