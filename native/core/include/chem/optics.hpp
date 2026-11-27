#pragma once

#include "chem/species.hpp"

#include <array>
#include <optional>
#include <span>
#include <string_view>

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
// A species' refractive index at 1 nm (GAME_SPEC §12.3): ones where none is
// known; and its density in g/cm3 where its index carries one (glass: for a
// wall's thickness), else 0.
const Spectrum &refractive_index(SpeciesId species);
double index_density(SpeciesId species);
// Its extinction coefficient k at 1 nm, the imaginary part of its index n + ik (Stage 78: a metal's, measured; GAME_SPEC
// §12.3): zeros for a clear dielectric or none known.
const Spectrum &extinction_index(SpeciesId species);
// An optical material that is no species (the prism demo's flint glass, "n_sf11"), or nullptr.
const Spectrum *material_index(std::string_view name);
// What the lab's things that are no substance reflect (a part's face, a wire, a burner's metal: a reflectance),
// and the lights of a lamp or a flame (a multiple of the room's light at each wavelength), by name; null for
// one there is not (data/optics/media.json: surfaces, emitters; GAME_SPEC §12.3, Stage 22).
const Spectrum *surface(std::string_view name);
const Spectrum *emitter(std::string_view name);
// What scatters in a liquid and is no species (data/optics/media.json: scatterers; GAME_SPEC §12, Stage 45): its
// single-scattering albedo at each wavelength, its extinction efficiency (extinction over geometric cross-section)
// and its asymmetry parameter g; by name, null for one there is not. "bubbles": gas bubbles in water.
struct Scatterer {
	const Spectrum *albedo = nullptr;
	double efficiency = 0.0, asymmetry = 0.0;
};
const Scatterer *scatterer(std::string_view name);
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
	double signal = 0.0; // a long-wave light's whole, W / m2 / sr (Stage 46: its integral, at a nanometre)
	EnergyEntry at(double share) const; // interpolated between entries
};
// `continuum` is radiance at 1 nm on the room's scale (d65() is the room's
// white); `lines` add radiance (each line's peak in the same unit).
EnergyTable energy_table(const Spectrum &continuum, std::span<const Line> lines = {});
// The room's light: the illuminant itself.
const EnergyTable &room_light();

// ---- the long-wave infrared, 8-14 um (GAME_SPEC §12.1, Stage 46): what a thermal camera's microbolometer sees

inline constexpr int LWIR_FIRST_NM = 8000;
inline constexpr int LWIR_STEP_NM = 20;
inline constexpr int LWIR_COUNT = 301; // 8000 .. 14000 nm
using LwirSpectrum = std::array<double, LWIR_COUNT>;
constexpr double lwir_nm(int i) {
	return LWIR_FIRST_NM + LWIR_STEP_NM * static_cast<double>(i);
}
// A species' complex refractive index n + ik over the band, measured (data/optics: water's, glass's), or null.
const LwirSpectrum *lwir_n(SpeciesId species);
const LwirSpectrum *lwir_k(SpeciesId species);
// What n + ik makes of light: the reflectance at normal incidence from air; the real index with that reflectance
// (what the pass bends and turns by: at least 1); the absorption coefficient, 1/cm (4 pi k / wavelength).
double normal_reflectance(double n, double k);
double effective_index(double n, double k);
// Fresnel's unpolarised reflectance (Stage 78; Born and Wolf §13.2: the mean of the s and p waves') of light arriving
// at `cos_i` from a medium of index `n1` + i`k1` onto one of `n2` + i`k2`: exact from a clear medium (k1 nought), the
// customary complex form from an absorbing one. Light past the critical angle is all turned back.
double fresnel_reflectance(double n1, double k1, double n2, double k2, double cos_i);
double absorption_per_cm(double k, double nm);
// Grey values over the band, by name (a surface's, or a solid species'): its emissivity (what it does not emit it
// reflects), a wall's transmittance at its reference thickness; none where there is no entry.
std::optional<double> lwir_emissivity(std::string_view name);
std::optional<double> lwir_transmittance(std::string_view name);
// A flame's absorption coefficient per cm of path: its gas's, grey, and its soot's, as 1 / wavelength from 10 um.
struct LwirFlame {
	double gas_per_cm = 0.0, soot_per_cm = 0.0;
	double per_cm(double nm) const {
		return gas_per_cm + soot_per_cm * 10000.0 / nm;
	}
};
const LwirFlame *lwir_flame(std::string_view name);
// The sensor's spectral response: flat over the band, 0 outside it.
double lwir_response(double nm);
// Planck's law: a black body's spectral radiance, W / m2 / sr / nm.
double planck(double nm, double kelvin);
// What the sensor collects from a black body: its radiance times the response, W / m2 / sr; in closed form
// (the series of the integral of x^3 / (e^x - 1)).
double band_signal(double kelvin);
// The camera's temperature from a signal (W / m2 / sr): of a grey body of `emissivity` that reflects the rest of a
// room at `reflected_kelvin` (signal = e B(T) + (1 - e) B(T_r), inverted); 0 where the signal is under the
// reflection's. To a millikelvin over 1 to 10000 K.
double brightness_kelvin(double signal, double emissivity, double reflected_kelvin);
// The room's long-wave light as the sensor weighs it: a black body at the room's temperature times the response,
// cut by equal shares of the signal; on the scale on which the room's whole signal is 1 (in x, y and z alike).
EnergyTable lwir_energy_table(double room_kelvin);

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
