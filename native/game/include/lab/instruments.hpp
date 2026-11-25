#pragma once

#include "lab/spectrometer.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// The lab's instruments (GAME_SPEC §13, Readings; §16; Stage 50), pure: what each states of itself
// (data/instruments.json), the offsets a bench's seed draws for them, a reading, and a balance's conventional
// mass (OIML D 28). A reading is the truth plus an offset within the instrument's stated error, rounded to its
// resolution; the same bench draws the same instruments on every machine.
namespace lab::instruments {

// A balance's model: what it reads to, how much it takes, and its linearity (its most error), g
struct Balance {
	std::string name;
	double readability_g = 0.01, capacity_g = 620.0, linearity_g = 0.02;
};
// A meter's resolution and stated accuracy, in its own unit
struct Meter {
	double resolution = 0.1, accuracy = 0.2;
};

const std::vector<Balance> &balances(); // in data/instruments.json's order; the first is a bench's default
const Balance *balance(std::string_view name); // null if there is none of that name
Meter thermometer(); // °C
Meter ph_meter();
double hand(); // how far a hand misses an amount aimed at, a share (0.10)
double reference_density(); // the reference weights', kg/m^3 (OIML D 28: 8000)
// a wall species' density, g/cm^3: the air its glass or its rubber displaces on a balance (0: none known)
double wall_density(std::string_view species);

// A share uniform in [-1, 1], drawn from the seed and a key: FNV-1a of the key, through SplitMix64 with the seed
double share(uint64_t seed, std::string_view key);
// `value` to the nearest multiple of `resolution`
double rounded(double value, double resolution);

// What a load reads on a balance calibrated with reference weights (OIML D 28): its mass less the air it
// displaces, over the reference weights' own correction. g, from g, cm^3 and kg/m^3
double conventional_g(double mass_g, double volume_cm3, double air_kg_m3);
// A balance's linearity at a gross conventional mass: an offset in proportion to the load, at most its linearity
// at its capacity; `share` is its draw
double linearity_g(const Balance &model, double gross_g, double share);
// What a balance shows: the gross less the tare, each with its linearity, to its readability
double display_g(const Balance &model, double gross_g, double tare_g, double share);
// What marks read of a volume: the volume scaled by 1 + error × share, to the nearest mark
double marks_ml(double ml, double spacing_ml, double error, double share);

// One reading held to its truth (I1): what it reads, the truth, how far its instrument may be off, and its
// resolution (the reading is a multiple of it)
struct Read {
	std::string what; // "beaker: thermometer"
	double truth = 0.0, reading = 0.0, error = 0.0, resolution = 0.0;
};
// I1 (GAME_SPEC §18): every reading within its error and half its resolution of its truth, and a multiple of its
// resolution; "" when it holds, else a message starting "I1"
std::string check_I1(const std::vector<Read> &reads);

// A balance's display (Stage 50): its text ("123.45 g", "OL", "" when it is off) as seven segments lit on a dark
// glass, right-aligned, each digit's unlit segments faint; a picture its screen shows (its hash its content's)
std::string display_text(const Balance &model, std::optional<double> display_g, bool overload);
void plot_display(const std::string &text, Picture &picture);

} // namespace lab::instruments
