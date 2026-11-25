#pragma once

#include "chem/optics.hpp"
#include "chem/species.hpp"

#include <optional>
#include <string>
#include <utility>
#include <vector>

// A spectrophotometer (GAME_SPEC §13, Stage 55), pure: a single-beam instrument's light through a sample. Its lamp
// (a tungsten filament, Planck's law at its temperature), its exit slit's band (triangular, its full width at half
// maximum), the stray light that reaches its detector whatever the wavelength set (a share of the band's light, of the
// lamp's whole spectrum through the sample), its wavelength's error. The sample's absorbance at 1 nm is the same
// spectra's the pass draws with (chem/optics.hpp: decadic molar absorptivity, water's own), Beer and Lambert's sum;
// what the instrument reads departs from it as a real one's does: under stray light at a high absorbance, over its
// band where the absorbance is steep, and by the lamp's slope at another wavelength than its blank's.
namespace lab::spectro {

// What the instrument states of itself (data/instruments.json, `spectrophotometer`, design)
struct Model {
	double first_nm = 380.0, last_nm = 780.0; // what it can be set to (the simulation's light)
	double band_nm = 4.0; // its slit's band, full width at half maximum
	double stray = 0.001; // stray light, a share of the band's light in its blank
	double wavelength_error_nm = 1.0; // its wavelength's accuracy: the true one within this of the one set
	double resolution = 0.001; // absorbance, what it displays to
	double most = 3.0; // what it displays at most ("OL" past it)
	double lamp_k = 2900.0; // its lamp's filament
	double beam_mm = 15.0; // the beam's height over a cell's floor
};
const Model &model();

// A sample: what absorbs in it, each a species and its molar concentration (mol/L), and its path (cm)
struct Sample {
	std::vector<std::pair<chem::SpeciesId, double>> molar;
	double path_cm = 1.0;
	bool opaque = false; // a solid in the beam: no light through
};
// Its absorbance at 1 nm (decadic): Beer and Lambert, sum of epsilon c l over what absorbs
chem::optics::Spectrum absorbance(const Sample &sample);
// Its absorbance at a wavelength, between the nanometres, its narrow lines read exactly
double absorbance_at(const Sample &sample, const chem::optics::Spectrum &a, double nm);

// The detector's signal with the instrument set at `nm` (the true wavelength: the set one plus its error), through a
// sample: the slit's band over the lamp times the sample's transmittance, plus its stray light
double signal(const Model &m, double nm, const Sample &sample);
// What it reads: -log10(sample / blank) to its resolution; none past `most` (its display's "OL") or with no light
std::optional<double> reading(const Model &m, double sample_signal, double blank_signal);

// Its display (seven segments, as a balance's): the absorbance, "OL" past its range, "----" without a blank; and the
// wavelength set ("750")
std::string display_text(std::optional<double> absorbance, bool blanked, bool over);
std::string wavelength_text(double nm);

} // namespace lab::spectro
