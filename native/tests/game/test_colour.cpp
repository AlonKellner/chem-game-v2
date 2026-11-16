// Colour as seen and the renderer's spectral bands (GAME_SPEC §12.1, O3).
#include "lab/colour.hpp"

#include <doctest/doctest.h>

using chem::optics::Bands;
using chem::optics::Xyz;

TEST_CASE("colour: CIEDE2000 agrees with the published test pairs (Stage 10)") {
	// Sharma, Wu and Dalal (2005), table 1: pairs 1, 4, 17 and 25
	CHECK(lab::difference({ 50.0, 2.6772, -79.7751 }, { 50.0, 0.0, -82.7485 }) == doctest::Approx(2.0425).epsilon(1e-4));
	CHECK(lab::difference({ 50.0, -1.3802, -84.2814 }, { 50.0, 0.0, -82.7485 }) == doctest::Approx(1.0000).epsilon(1e-4));
	CHECK(lab::difference({ 50.0, 2.5, 0.0 }, { 73.0, 25.0, -18.0 }) == doctest::Approx(27.1492).epsilon(1e-4));
	CHECK(lab::difference({ 60.2574, -34.0099, 36.2677 }, { 60.4626, -34.1751, 39.4387 }) == doctest::Approx(1.2644).epsilon(1e-4));
	CHECK(lab::difference({ 40.0, 10.0, -20.0 }, { 40.0, 10.0, -20.0 }) == 0.0);
}

TEST_CASE("colour: the illuminant is white, on the page and on a display (Stage 10)") {
	chem::optics::Spectrum one;
	one.fill(1.0);
	const Xyz white = chem::optics::colour(one);
	CHECK(white.y == doctest::Approx(1.0));
	// D65's chromaticity
	CHECK(white.x / (white.x + white.y + white.z) == doctest::Approx(0.3127).epsilon(1e-3));
	CHECK(white.y / (white.x + white.y + white.z) == doctest::Approx(0.3290).epsilon(1e-3));
	const lab::CieLab l = lab::to_lab(white);
	CHECK(l.l == doctest::Approx(100.0));
	CHECK(l.a == doctest::Approx(0.0).epsilon(1e-9));
	CHECK(l.b == doctest::Approx(0.0).epsilon(1e-9));
	double rgb[3];
	lab::to_linear_srgb(white, rgb);
	for (const double channel : rgb) {
		CHECK(channel == doctest::Approx(1.0).epsilon(2e-3));
	}
	CHECK(lab::srgb_encode(0.0) == 0.0);
	CHECK(lab::srgb_encode(1.0) == doctest::Approx(1.0));
	CHECK(lab::srgb_encode(0.18) == doctest::Approx(0.4614).epsilon(1e-3));
}

TEST_CASE("colour: the bands share out the whole spectrum (Stage 10)") {
	for (const Bands &bands : { chem::optics::game_bands(), chem::optics::bands_uniform(7, 400, 700), chem::optics::bands_equal_weight(13) }) {
		CHECK(bands.edge[0] == 0);
		CHECK(bands.edge[static_cast<size_t>(bands.count)] == chem::optics::COUNT);
		Xyz sum;
		for (int k = 0; k < bands.count; ++k) {
			CHECK(bands.edge[static_cast<size_t>(k)] < bands.edge[static_cast<size_t>(k) + 1]);
			sum.x += bands.lit[static_cast<size_t>(k)].x;
			sum.y += bands.lit[static_cast<size_t>(k)].y;
			sum.z += bands.lit[static_cast<size_t>(k)].z;
		}
		CHECK(sum.y == doctest::Approx(1.0)); // nothing in the way: white
		CHECK(sum.x == doctest::Approx(0.9505).epsilon(1e-3));
		CHECK(sum.z == doctest::Approx(1.0888).epsilon(1e-3));
	}
}

TEST_CASE("O3: the game's bands show every colour of the demo within the threshold (Stage 10)") {
	const Bands &bands = chem::optics::game_bands();
	CHECK(bands.count == chem::optics::GAME_BANDS);
	const lab::BandError e = lab::band_error(bands, lab::BandModel::Mean);
	INFO("worst: " << e.worst << " at " << e.worst_case << ", mean " << e.mean);
	CHECK(e.worst <= lab::BAND_ERROR_WORST);
	CHECK(e.mean <= lab::BAND_ERROR_MEAN);
}

TEST_CASE("O3: the check fires on too few bands (Stage 10)") {
	const lab::BandError e = lab::band_error(chem::optics::bands_equal_weight(6), lab::BandModel::Mean);
	CHECK(e.worst > lab::BAND_ERROR_WORST);
	CHECK(e.mean > lab::BAND_ERROR_MEAN);
	// and more bands are better, in the end
	CHECK(lab::band_error(chem::optics::bands_equal_weight(32), lab::BandModel::Mean).worst < lab::band_error(chem::optics::game_bands(), lab::BandModel::Mean).worst);
}
