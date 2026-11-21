// A shaped vessel's knots (GAME_SPEC §12.3, Stage 25): upright bands one map, curved bands refined in the rims'
// measure; a row read between two knots, against the rays themselves.
#include "lab/glassware.hpp"
#include "lab/optics_trace.hpp"

#include <doctest/doctest.h>

#include <chrono>
#include <cstdlib>
#include <cmath>
#include <random>
#include <vector>

using namespace lab::trace;
namespace glass = lab::glass;

namespace {

std::vector<float> cauchy(double a, double b) {
	std::vector<float> out(401);
	for (int i = 0; i < 401; ++i) {
		out[static_cast<size_t>(i)] = static_cast<float>(a + b / ((380.0 + i) * (380.0 + i)));
	}
	return out;
}

const std::vector<float> GLASS = cauchy(1.4580, 4200.0), WATER = cauchy(1.3240, 3050.0);

// a vessel as the screen draws it: 90 pixels across its widest, the wall 284 pixels behind
MapSpec spec_of(const glass::Vessel &v, double &height) {
	MapSpec spec;
	spec.shape = revolved_of(v);
	spec.cm_per_pixel = 2.0 * v.widest() / 90.0;
	spec.left = -46.0;
	spec.right = 46.0;
	spec.wall_cm = 284.0 * spec.cm_per_pixel;
	spec.glass_index = GLASS.data();
	spec.liquid_index = WATER.data();
	height = v.height / spec.cm_per_pixel;
	return spec;
}

} // namespace

TEST_CASE("knots: a shaped vessel's rows read between two knots are its rays, across and up and down, within tolerance (Stage 25)") {
	for (const auto &[kind, ml] : { std::pair{ "beaker", 250.0 }, std::pair{ "conical_flask", 250.0 }, std::pair{ "bottle", 500.0 } }) {
		CAPTURE(kind);
		const glass::Vessel &v = *glass::find(kind, ml);
		double height = 0.0;
		const MapSpec spec = spec_of(v, height);
		const auto t0 = std::chrono::steady_clock::now();
		const KnotMap knots = build_knots(spec, v, height);
		const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
		REQUIRE(knots.knots.size() >= 2);
		CHECK(knots.knots.size() <= static_cast<size_t>(ROW_KNOTS_MOST) + 1);
		std::mt19937_64 rng(11);
		std::uniform_real_distribution<double> unit(0.0, 1.0);
		double worst = 0.0, worst_drop = 0.0;
		int compared = 0, over = 0;
		for (int i = 0; i < 5000; ++i) {
			const double x = -45.0 + 90.0 * unit(rng), y = height * unit(rng), nm = 382.0 + 396.0 * unit(rng);
			const int medium = i % 2;
			// within a pixel of a rim a ray is its nearer knot's (GAME_SPEC §12.3)
			const double y_cm = (height - y) * spec.cm_per_pixel, outer = v.radius_outside(y_cm) / spec.cm_per_pixel, inner = v.radius_inside(y_cm) / spec.cm_per_pixel;
			if (std::fabs(std::fabs(x) - outer) < 1.0 || (inner > 0.0 && std::fabs(std::fabs(x) - inner) < 1.0)) {
				continue;
			}
			const MapRay exact = rows_ray(spec, height, x, y, nm, medium), read = knots_read(knots, x, y, nm, medium);
			const double q = 1.0 / MAP_FINE, place = map_place(nm), piece = 1.0;
			const MapRay left = rows_ray(spec, height, x - q, y, nm, medium), right = rows_ray(spec, height, x + q, y, nm, medium);
			const MapRay up = rows_ray(spec, height, x, y - q, nm, medium), down = rows_ray(spec, height, x, y + q, nm, medium);
			const MapRay bluer = rows_ray(spec, height, x, y, map_nm(place - piece), medium), redder = rows_ray(spec, height, x, y, map_nm(place + piece), medium);
			if (left.route != exact.route || right.route != exact.route || up.route != exact.route || down.route != exact.route || bluer.route != exact.route ||
					redder.route != exact.route || exact.pass < 0.02 || std::fabs(exact.landing) > 1e4) {
				continue;
			}
			const double slope = std::max({ 1.0, std::fabs(right.landing - left.landing) * MAP_FINE / 2.0, std::fabs(down.landing - up.landing) * MAP_FINE / 2.0 });
			const double slope_drop = std::max({ 1.0, std::fabs(down.drop - up.drop) * MAP_FINE / 2.0, std::fabs(right.drop - left.drop) * MAP_FINE / 2.0 });
			const double e = std::fabs(read.landing - exact.landing) / slope, ed = std::fabs(read.drop - exact.drop) / slope_drop;
			over += e > 3.0 * ROW_TOLERANCE || ed > 3.0 * ROW_TOLERANCE ? 1 : 0;
			worst = std::max(worst, e);
			worst_drop = std::max(worst_drop, ed);
			++compared;
		}
		size_t floats = 0;
		for (const Map &m : knots.maps) {
			floats += m.cells.size() + m.drops.size();
		}
		MESSAGE(kind << ": " << knots.knots.size() << " knots, " << knots.maps.size() << " maps (" << floats * 4 / 1024 << " KB), " << knots.rays << " rays, " << ms << " ms; worst landing " << worst
					 << " px, worst drop " << worst_drop << " px, " << over << " of " << compared << " beyond tolerance");
		// rays that cross a vessel's base or its neck's joint by several routes within a quarter of a pixel, and the
		// knots capped for the GPU's cache: a few in a hundred go astray
		CHECK(compared > 1000);
		CHECK(over <= compared * 5 / 100);
	}
}
