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
inline constexpr int GAME_BANDS = 24; // what the renderer uses (GAME_SPEC §12.2)

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
// An absorber's narrow lines, beside its 1 nm spectrum (GAME_SPEC §12.1): each a
// Gaussian of the given full width at half maximum, read at the exact wavelength
// a point falls on, never averaged over a nanometre.
struct Line {
	double centre_nm = 0.0, fwhm_nm = 1.0, peak = 0.0; // peak: as the spectrum's unit
};
std::span<const Line> lines(SpeciesId species);
// A line's value at a wavelength.
double line_at(const Line &line, double nm);

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
// (GAME_SPEC §12.2), and held there by a test.
const Bands &game_bands();

// ---- energy bands with moving points (GAME_SPEC §12.2)

// What a display makes of a nanometre: the magnitudes of its red, green and blue
// matching curves (sRGB primaries), summed.
double display_weight(double nm);
// The observer at any wavelength, interpolated between the table's nanometres.
Xyz observer_at(double nm);
// The illuminant at any wavelength, likewise.
double d65_at(double nm);

// A light's energy table: its spectrum times the display's weight, cut by share
// of that energy. Entry j (0 .. ENERGY_STEPS) is at share j / ENERGY_STEPS: the
// wavelength there and the light's colour up to it, on the scale on which the
// room's white is Y = 1. Built at whatever resolution its lines need: a line a
// hundredth of a nanometre wide is resolved, and owns its share of the entries.
inline constexpr int ENERGY_STEPS = 4096;
struct EnergyEntry {
	double nm = 0.0;
	Xyz up_to;
};
struct EnergyTable {
	std::array<EnergyEntry, ENERGY_STEPS + 1> entry{};
	Xyz total; // the whole light's colour
	EnergyEntry at(double share) const; // interpolated between entries
};
// `continuum` is radiance at 1 nm on the room's scale (d65() is the room's
// white); `lines` add radiance (each line's peak in the same unit).
EnergyTable energy_table(const Spectrum &continuum, std::span<const Line> lines = {});
// The room's light: the illuminant itself.
const EnergyTable &room_light();

// One frame's estimate of the colour of `light` through `through(nm)` (a
// factor, 0 .. 1), by n bands of equal energy, each its exact colour times what
// gets through at one point in it; `offset` in [0, 1) places the points.
template <class Through>
Xyz estimate(const EnergyTable &light, int n, double offset, Through through) {
	Xyz out, before = light.at(0.0).up_to;
	for (int b = 0; b < n; ++b) {
		const Xyz upto = light.at(static_cast<double>(b + 1) / n).up_to;
		const double t = through(light.at((b + offset) / n).nm);
		out.x += (upto.x - before.x) * t;
		out.y += (upto.y - before.y) * t;
		out.z += (upto.z - before.z) * t;
		before = upto;
	}
	return out;
}
// The colour itself, to the table's resolution: every entry its own band, read at its middle.
template <class Through>
Xyz exact(const EnergyTable &light, Through through) {
	Xyz out;
	for (int j = 0; j < ENERGY_STEPS; ++j) {
		const EnergyEntry &a = light.entry[static_cast<size_t>(j)], &b = light.entry[static_cast<size_t>(j) + 1];
		const double t = through(light.at((j + 0.5) / ENERGY_STEPS).nm);
		out.x += (b.up_to.x - a.up_to.x) * t;
		out.y += (b.up_to.y - a.up_to.y) * t;
		out.z += (b.up_to.z - a.up_to.z) * t;
	}
	return out;
}

} // namespace chem::optics
