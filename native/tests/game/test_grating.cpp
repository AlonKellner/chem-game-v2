// A diffraction grating in the prism demo (GAME_SPEC §12.3, Stage 29): a ruled plate that sends each ray on in
// several ways at once, each turned by its own wavelength. Nothing in it is refraction: it is here to try the
// reading that takes a pixel as its beam's exact reading and the average of its rays' corrections, on something
// that reading was not made for.
#include "lab/colour.hpp"
#include "lab/optics_scene.hpp"
#include "lab/prism_demo.hpp"

#include "sparse.hpp"

#include <doctest/doctest.h>

#include <cmath>
#include <vector>

using lab::OpticsScene;
using lab::PrismDemo;

namespace {

constexpr double PI = 3.14159265358979323846;
constexpr double SLIT_MIDDLE = PrismDemo::SLIT_X + PrismDemo::SLIT_W / 2.0;
constexpr int ROW_Y = 540;

// the demo with its grating held before the slit
OpticsScene held_before_the_slit() {
	PrismDemo demo;
	demo.place_grating(static_cast<float>(SLIT_MIDDLE), static_cast<float>(ROW_Y));
	return demo.optics();
}

double wall_px(const OpticsScene &scene) {
	return scene.table[2 * 4];
}

// how far across the wall an order lands from where its ray crossed the grating: the grating equation
// (sin = m lambda / d, from straight on) carried to the wall
double shift_px(const OpticsScene &scene, int order, double nm) {
	const double sine = order * nm / PrismDemo::GRATING_PITCH_NM;
	return wall_px(scene) * sine / std::sqrt(1.0 - sine * sine);
}

int order_of(int way) {
	return way == 0 ? 0 : way == 1 ? 1 : way == 2 ? -1 : way == 3 ? 3 : -3;
}

// the share of the light a half-open ruling sends into an order: its Fourier series
double share_of(int order) {
	return order == 0 ? 0.25 : order % 2 == 0 ? 0.0 : 1.0 / (PI * PI * order * order);
}

double eye(const double a[3], const double b[3]) {
	const auto seen = [](const double c[3]) {
		return lab::shown(chem::optics::Xyz{ 0.4124564 * c[0] + 0.3575761 * c[1] + 0.1804375 * c[2], 0.2126729 * c[0] + 0.7151522 * c[1] + 0.0721750 * c[2],
				0.0193339 * c[0] + 0.1191920 * c[1] + 0.9503041 * c[2] });
	};
	return lab::eye_difference(seen(a), seen(b));
}

} // namespace

TEST_CASE("grating: a ray goes on in each order where the grating equation sends it; an order past a right angle does not leave (Stage 29)") {
	const OpticsScene scene = held_before_the_slit();
	const double x = SLIT_MIDDLE - 40.25, y = ROW_Y + 0.5;
	for (const double nm : { 400.0, 450.0, 550.0, 650.0, 700.0 }) {
		for (int way = 0; way < 5; ++way) {
			CAPTURE(nm);
			CAPTURE(way);
			const lab::OpticsRay r = lab::optics_ray_way(scene, x, y, nm, way);
			CHECK(r.ways == 5);
			const double sine = order_of(way) * nm / PrismDemo::GRATING_PITCH_NM;
			if (std::fabs(sine) >= 1.0) {
				CHECK(!r.lands);
				continue;
			}
			if (sine * sine < 0.8) { // (nearer a right angle the landing runs away, and with it the pitch's last digit)
				REQUIRE(r.lands);
				CHECK(r.x == doctest::Approx(x + shift_px(scene, order_of(way), nm)).epsilon(1e-6)); // (the pitch is held in single precision)
				CHECK(r.y == doctest::Approx(y).epsilon(1e-12));
			}
		}
	}
	// red is turned further than violet, and the two first orders are each other's mirror
	CHECK(shift_px(scene, 1, 650.0) > shift_px(scene, 1, 450.0));
	CHECK(lab::optics_ray_way(scene, x, y, 550.0, 1).x - x == doctest::Approx(x - lab::optics_ray_way(scene, x, y, 550.0, 2).x));
	// beside the plate a ray meets no grating, and goes straight on
	const lab::OpticsRay beside = lab::optics_ray_way(scene, SLIT_MIDDLE + PrismDemo::GRATING_W, y, 550.0, 1);
	CHECK(beside.ways == 1);
	CHECK(beside.x == doctest::Approx(SLIT_MIDDLE + PrismDemo::GRATING_W));
	// and the pass's tracer is the truth's there too (O9: its rays straight on, the order 0)
	CHECK(lab::check_O9(scene, static_cast<int>(SLIT_MIDDLE) - 60, ROW_Y - 4, static_cast<int>(SLIT_MIDDLE) + 60, ROW_Y + 4, 3) == "");
}

TEST_CASE("grating: each order of the slit carries its share of the slit's light, at any wavelength: a quarter straight on, 1 / pi^2 in each first order (Stage 29)") {
	const OpticsScene scene = held_before_the_slit();
	const double slit_light = PrismDemo::SLIT_W * (PrismDemo::SLIT - PrismDemo::DARK);
	for (const double nm : { 450.0, 600.0 }) {
		for (const int order : { 0, 1, -1 }) {
			CAPTURE(nm);
			CAPTURE(order);
			// the slit's image in this order: seen from where the order's turn lands the eye's ray on the slit
			const double middle = SLIT_MIDDLE - shift_px(scene, order, nm);
			double summed = 0.0;
			for (int x = static_cast<int>(std::floor(middle)) - 6; x <= static_cast<int>(std::floor(middle)) + 6; ++x) {
				summed += lab::optics_through(scene, x, ROW_Y, nm) - PrismDemo::DARK;
			}
			CHECK(summed == doctest::Approx(share_of(order) * slit_light).epsilon(0.01));
		}
		// and between the orders the room is as dark as it is: the grating makes no light
		CHECK(lab::optics_through(scene, static_cast<int>(SLIT_MIDDLE - 0.5 * shift_px(scene, 1, nm)), ROW_Y, nm) == doctest::Approx(PrismDemo::DARK).epsilon(1e-6));
	}
}

TEST_CASE("grating: held before the slit it shows the slit, dimmed, and to each side its spectrum, violet nearest and red furthest (Stage 29)") {
	const OpticsScene scene = held_before_the_slit();
	const auto colour_at = [&](double x, double rgb[3]) { lab::optics_truth(scene, static_cast<int>(std::floor(x)), ROW_Y, rgb); };
	double white[3], blue_left[3], red_left[3], blue_right[3], red_right[3], dark[3];
	colour_at(SLIT_MIDDLE, white);
	colour_at(SLIT_MIDDLE - shift_px(scene, 1, 450.0), blue_left);
	colour_at(SLIT_MIDDLE - shift_px(scene, 1, 640.0), red_left);
	colour_at(SLIT_MIDDLE + shift_px(scene, 1, 450.0), blue_right);
	colour_at(SLIT_MIDDLE + shift_px(scene, 1, 640.0), red_right);
	colour_at(SLIT_MIDDLE - 0.5 * shift_px(scene, 1, 400.0), dark);
	// straight on: the slit itself, every wavelength where it was, so white; a quarter of its light
	CHECK(white[0] > 0.5);
	CHECK(std::fabs(white[0] - white[1]) < 0.15 * white[1]);
	CHECK(std::fabs(white[2] - white[1]) < 0.15 * white[1]);
	// its first orders: violet and blue nearest the slit, red furthest, on both sides alike
	CHECK(blue_left[2] > 3.0 * std::max(blue_left[0], 0.0));
	CHECK(red_left[0] > 3.0 * std::max(red_left[2], 0.0));
	CHECK(blue_right[2] > 3.0 * std::max(blue_right[0], 0.0));
	CHECK(red_right[0] > 3.0 * std::max(red_right[2], 0.0));
	CHECK(shift_px(scene, 1, 640.0) > shift_px(scene, 1, 450.0) + 10.0);
	// and nothing between the slit and its spectra
	CHECK(dark[0] + dark[1] + dark[2] < 0.05);
	// what the pass's own reading shows is what the truth's rays bring (each ray followed in every order)
	double worst = 0.0;
	int compared = 0;
	for (const double x : { SLIT_MIDDLE, SLIT_MIDDLE - shift_px(scene, 1, 450.0), SLIT_MIDDLE - shift_px(scene, 1, 550.0), SLIT_MIDDLE + shift_px(scene, 1, 600.0), SLIT_MIDDLE - 20.0 }) {
		double shown[3], truth[3];
		lab::optics_truth(scene, static_cast<int>(std::floor(x)), ROW_Y, shown);
		lab::optics_area_truth(scene, static_cast<int>(std::floor(x)), ROW_Y, 6 / std::min(sparse(), 2), truth);
		CAPTURE(x);
		worst = std::max(worst, eye(shown, truth));
		++compared;
	}
	CHECK(worst < lab::EYE_VISIBLE);
	MESSAGE(compared << " pixels of the grating's orders against the truth's rays: worst " << worst << " on the eye metric (visible above 8)");
}

TEST_CASE("grating: O7 holds through it: in a room as bright in front as behind, a ruled plate changes nothing (Stage 29)") {
	const OpticsScene scene = held_before_the_slit();
	CHECK(lab::check_O7(scene, static_cast<int>(SLIT_MIDDLE) - 136, ROW_Y - 3, static_cast<int>(SLIT_MIDDLE) + 136, ROW_Y + 3, 0.6) == "");
}

TEST_CASE("grating: frame by frame the picture comes to the truth: after four seconds no pixel of the orders is visibly wrong (Stage 29)") {
	const OpticsScene scene = held_before_the_slit();
	const int x0 = static_cast<int>(SLIT_MIDDLE) - 80, w = 160, y0 = ROW_Y - 2, h = 4;
	lab::OpticsFrames frames(x0, y0, w, h);
	int wrong = 0, compared = 0, dark_ends = 0;
	double worst = 0.0, sum = 0.0;
	for (int f = 1; f <= 240; ++f) {
		frames.frame(scene);
		if (f != 15 && f != 60 && f != 120 && f != 240) {
			continue;
		}
		wrong = compared = dark_ends = 0;
		worst = sum = 0.0;
		for (int x = x0; x < x0 + w; x += 2) {
			double shown[3], truth[3];
			frames.colour(scene, x, ROW_Y, shown);
			lab::optics_truth(scene, x, ROW_Y, truth);
			const double e = eye(shown, truth);
			// (At the very ends of a spectrum, 380 and 780 nm, a pixel is nearly black, and the first and last bands
			// of the light's energy are tens of nanometres wide: its dark colour may be another dark colour. That is
			// the bands', as before this stage; anything brighter must be right.)
			const double brightest = std::max({ shown[0], shown[1], shown[2], truth[0], truth[1], truth[2] });
			wrong += e > lab::EYE_VISIBLE && brightest > 0.03 ? 1 : 0;
			dark_ends += e > lab::EYE_VISIBLE && brightest <= 0.03 ? 1 : 0;
			worst = std::max(worst, e);
			sum += e;
			++compared;
		}
		MESSAGE(compared << " pixels across the grating's orders after " << f << " frames: " << wrong << " visibly wrong (" << dark_ends << " more at a spectrum's black end), worst " << worst << " on the eye metric, mean " << sum / compared << "; points in the last frame " << frames.points());
	}
	CHECK(wrong == 0);
	CHECK(dark_ends <= 2);
	CHECK(worst < 1.25 * lab::EYE_VISIBLE);
	CHECK(sum / compared < 1.5);
}
