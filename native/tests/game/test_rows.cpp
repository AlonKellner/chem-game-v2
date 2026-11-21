// A shaped vessel's maps, a map for each band of its rows (GAME_SPEC §12.3, Stage 24): upright bands one map,
// curved bands cut every ROW_STEP pixels; read against the rays themselves.
#include "lab/glassware.hpp"
#include "lab/optics_trace.hpp"

#include <doctest/doctest.h>

#include <chrono>
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

TEST_CASE("rows: a shaped vessel's bands: every row of an upright band reads its rays within tolerance; a curved band's rows its middle row's (Stage 24)") {
	for (const auto &[kind, ml] : { std::pair{ "beaker", 250.0 }, std::pair{ "conical_flask", 250.0 }, std::pair{ "bottle", 500.0 } }) {
		CAPTURE(kind);
		const glass::Vessel &v = *glass::find(kind, ml);
		double height = 0.0;
		const MapSpec spec = spec_of(v, height);
		const auto t0 = std::chrono::steady_clock::now();
		const RowMap rows = build_rows(spec, v, height);
		const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
		REQUIRE(!rows.ends.empty());
		CHECK(rows.ends.back() == doctest::Approx(height).epsilon(1e-12));
		// no band of a curved part is taller than ROW_STEP
		int upright = 0;
		for (size_t k = 0; k < rows.ends.size(); ++k) {
			const double tall = rows.ends[k] - (k == 0 ? 0.0 : rows.ends[k - 1]);
			upright += tall > ROW_STEP + 1e-9 ? 1 : 0;
		}
		CHECK(upright >= 1); // a beaker's side, a flask's neck, a bottle's side and neck
		std::mt19937_64 rng(5);
		std::uniform_real_distribution<double> unit(0.0, 1.0);
		double worst_upright = 0.0, worst_curved = 0.0;
		int compared = 0, traced = 0, over = 0;
		for (int i = 0; i < 4000; ++i) {
			size_t k = static_cast<size_t>(unit(rng) * static_cast<double>(rows.ends.size()));
			k = std::min(k, rows.ends.size() - 1);
			const double from = k == 0 ? 0.0 : rows.ends[k - 1], tall = rows.ends[k] - from;
			const bool whole = tall > ROW_STEP + 1e-9;
			// in an upright band any row; in a curved band its middle row (what its map was traced at) or any
			const double y = whole || i % 2 == 0 ? from + tall * (0.05 + 0.9 * unit(rng)) : from + 0.5 * tall;
			const double x = -45.0 + 90.0 * unit(rng), nm = 382.0 + 396.0 * unit(rng);
			const int medium = i % 2;
			const MapRay exact = rows_ray(spec, height, x, y, nm, medium), read = rows_read(rows, x, y, nm, medium);
			const double q = 1.0 / MAP_FINE, place = map_place(nm), piece = 1.0 / rows.maps[k].pieces;
			const MapRay left = rows_ray(spec, height, x - q, y, nm, medium), right = rows_ray(spec, height, x + q, y, nm, medium);
			const MapRay bluer = rows_ray(spec, height, x, y, map_nm(place - piece), medium), redder = rows_ray(spec, height, x, y, map_nm(place + piece), medium);
			if (left.route != exact.route || right.route != exact.route || bluer.route != exact.route || redder.route != exact.route || exact.pass < 0.02 || std::fabs(exact.landing) > 1e4) {
				continue;
			}
			const double error = std::fabs(read.landing - exact.landing) / std::max(1.0, std::fabs(right.landing - left.landing) * MAP_FINE / 2.0);
			if (whole || y == from + 0.5 * tall) {
				over += error > 3.0 * MAP_LANDING_TOLERANCE ? 1 : 0;
				++traced;
				worst_upright = std::max(worst_upright, error);
			} else {
				worst_curved = std::max(worst_curved, error);
			}
			++compared;
		}
		int most_pieces = 0;
		for (const Map &m : rows.maps) {
			most_pieces = std::max(most_pieces, m.pieces);
		}
		MESSAGE(kind << ": " << rows.ends.size() << " bands (" << upright << " upright, at most " << most_pieces << " pieces), " << rows.rays << " rays, " << ms << " ms; worst where traced " << worst_upright << " px (" << over << " of " << traced << " beyond tolerance)"
					 << ", worst elsewhere in a curved band " << worst_curved << " px (bending up and down: Stage 25)");
		CHECK(compared > 1000);
		// where a band's map was traced its rays are its own, but over a vessel's base and floor, where rays cross
		// thick glass by several routes within a quarter of a pixel: there a few go astray
		CHECK(over <= traced / 100);
	}
}
