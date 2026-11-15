#pragma once

#include "chem/species.hpp"

#include <array>
#include <span>

// Light (GAME_SPEC §12). Spectra are tables at 1 nm from 380 to 780 nm. A
// colour is CIE XYZ under the 1931 2-degree observer, with the D65 illuminant's
// white at Y = 1. The renderer does not carry 401 numbers a pixel: it carries
// a few bands, and this is where the bands, and their error, come from.
namespace chem::optics {

inline constexpr int FIRST_NM = 380;
inline constexpr int COUNT = 401; // 380 .. 780
inline constexpr int MAX_BANDS = 32;
inline constexpr int GAME_BANDS = 24; // what the renderer uses (GAME_SPEC §12.1)

using Spectrum = std::array<double, COUNT>;

struct Xyz {
	double x = 0.0, y = 0.0, z = 0.0;
};

// The observer and the illuminant (data/optics, embedded by the build).
const Spectrum &xbar();
const Spectrum &ybar();
const Spectrum &zbar();
const Spectrum &d65();

// What a species does to light.
enum class Kind : uint8_t {
	None,
	Absorb, // a solute: molar absorptivity, L/mol/cm (decadic)
	Reflect, // a solid: diffuse reflectance, 0..1
	Transmit, // a wall: transmittance at its reference thickness, 0..1
};
Kind kind(SpeciesId species);
const Spectrum &spectrum(SpeciesId species); // zeros for Kind::None

// The reference: the colour of light whose spectrum is `factor` times the
// illuminant's (a transmittance, a reflectance), integrated at 1 nm.
Xyz colour(const Spectrum &factor);
// The colour of light of spectral radiance `radiance`, on the scale on which
// the illuminant itself is white (Y = 1).
Xyz colour_emitted(const Spectrum &radiance);

// A set of bands: the spectrum cut into n pieces. A quantity that is linear in
// light (a reflectance, an absorption coefficient before the exponential) is
// carried as its average over each band, weighted by what the eye and the
// illuminant make of that band; the colour of n factors is then a sum of n
// fixed vectors. Exact for a factor that is constant within each band.
struct Bands {
	int count = 0;
	std::array<int, MAX_BANDS + 1> edge{}; // band k is the table's entries [edge[k], edge[k + 1])
	std::array<Xyz, MAX_BANDS> lit{}; // the colour of band k of the illuminant
	std::array<Xyz, MAX_BANDS> plain{}; // the colour of unit radiance over band k
	std::array<double, MAX_BANDS> centre_nm{}; // its weighted centre, for labels
};

// n bands of equal width over [from_nm, to_nm]; what lies outside goes to the end bands.
Bands bands_uniform(int count, int from_nm, int to_nm);
// n bands that each carry an equal share of what the eye and the illuminant weigh.
Bands bands_equal_weight(int count);
// Bands cut at the given wavelengths (ascending, inside 380-780 nm): one more band than cuts.
Bands bands_cut_at(std::span<const int> cuts_nm);

// The average of a spectrum over each band, weighted by what the eye makes of
// each nanometre, and by the illuminant too unless the light is `emitted`.
void band_averages(const Bands &bands, const Spectrum &s, std::span<double> out, bool emitted = false);
// The colour of light that is factor[k] times the illuminant's in band k.
Xyz colour(const Bands &bands, std::span<const double> factor);
// The colour of light of radiance[k] in band k.
Xyz colour_emitted(const Bands &bands, std::span<const double> radiance);

// The bands the game renders with: chosen by `chem_tool study spectrum` as the
// fewest that keep every colour the game can show within its threshold
// (GAME_SPEC §12.1), and held there by a test.
const Bands &game_bands();

} // namespace chem::optics
