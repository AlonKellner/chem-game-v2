#pragma once

#include "chem/optics.hpp"

#include <string>
#include <vector>

// Colour as seen (GAME_SPEC §12.2): CIE L*a*b* from XYZ, the CIEDE2000
// difference between two colours, and sRGB for a display.
namespace lab {

struct CieLab {
	double l = 0.0, a = 0.0, b = 0.0;
};

// L*a*b* relative to the D65 illuminant's white.
CieLab to_lab(const chem::optics::Xyz &xyz);
// CIEDE2000 (Sharma, Wu and Dalal 2005): about 1 is a just-noticeable difference.
double difference(const CieLab &a, const CieLab &b);
// Linear sRGB (D65 white), not clamped.
void to_linear_srgb(const chem::optics::Xyz &xyz, double rgb[3]);
// The sRGB transfer function, a linear value in [0, 1] to an encoded one.
double srgb_encode(double linear);

// What a band carries of an absorber.
enum class BandModel {
	Mean, // its average absorption over the band
	Tilted, // its average and its slope across the band: the band's light is not dimmed evenly
};

// The error of a band set over the colours the game can show (GAME_SPEC §12.2).
struct BandError {
	double worst = 0.0; // the largest CIEDE2000 difference from the 1 nm reference
	double mean = 0.0;
	std::string worst_case; // which colour it was
	double worst_narrow = 0.0; // the same over synthetic narrow absorption lines: where bands degrade
	std::vector<std::pair<double, std::string>> all; // every colour's error, if asked for
};
BandError band_error(const chem::optics::Bands &bands, BandModel model, bool detail = false);

// what the game promises (GAME_SPEC §12.2)
inline constexpr double BAND_ERROR_WORST = 1.0;
inline constexpr double BAND_ERROR_MEAN = 0.5;

} // namespace lab
