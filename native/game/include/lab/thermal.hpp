#pragma once

#include "lab/lab.hpp"
#include "lab/optics_scene.hpp"

#include <array>
#include <string>
#include <string_view>
#include <vector>

// A thermal camera (GAME_SPEC §13, Stage 48): what it shows and how it reads, pure. Its palettes and gains are
// data/thermal.json's; its picture is the renderer's sensor display (GAME_SPEC §12.4); here, the colours it shows a
// temperature in, the span it shows, and the search for its hottest and coldest places, which its markers show.
namespace lab {

// the palettes' names, in data/thermal.json's order
const std::vector<std::string> &thermal_palette_names();
// a palette at 256 entries, linear RGB, its coldest first (an unknown name: the first palette)
const std::array<std::array<float, 3>, 256> &thermal_palette(std::string_view name);

// The temperatures a camera shows across its palette, and the gain it reads at.
struct ThermalSpan {
	double low_c = 0.0, high_c = 100.0;
	std::string gain = "high"; // the gain it reads at: high or low
	bool over = false; // something it sees is past its gain's range (">150", ">650")
};
// a gain's range, C: high -20 to 150, low 0 to 650
void thermal_gain_range(std::string_view gain, double &low_c, double &high_c);
// The span now, from the hottest and coldest it reads and the span it had: locked, its level and span; else the
// coldest to the hottest, at least a few degrees, within its gain's range, moved only when an end has moved by more
// than half a kelvin or 3 % of the span (so its picture does not start again at every reading). Auto gain: high, and
// low above 140 C, back to high below 120 C.
ThermalSpan thermal_span(const ThermalSettings &settings, double coldest_c, double hottest_c, const ThermalSpan &was);

// What a camera reads (Stage 48): its frame's place on the reading scene (the room laid out at 1x), its sensor pixels
// the layout's pixels there; the hottest and coldest of them and its centre's spot (3 x 3 pixels), each a mean band
// signal (the room's scale) and where (pixels from the frame's top-left).
struct ThermalFrame {
	double x = 0.0, y = 0.0; // the frame's top-left on the reading scene
	int w = 0, h = 0; // its sensor pixels
	uint32_t hide = 1; // the camera's own bodies
};
struct ThermalSpot {
	double signal = 0.0;
	int x = 0, y = 0;
};
struct ThermalReading {
	bool done = false;
	ThermalSpot hottest, coldest, centre;
};

// The search (Stage 48), a step at a time under a budget of reads: every second sensor pixel across and down,
// each a 2 x 2 square of them, at COARSE_BANDS bands; then each pixel of the 3 x 3 squares about the four hottest and
// the four coldest at FINE_BANDS; and the centre's spot. `coarse_only`: the first pass alone (O26's self-test).
class ThermalSearch {
public:
	static constexpr int COARSE_BANDS = 8, FINE_BANDS = 16;
	void start(const ThermalFrame &frame, bool coarse_only = false);
	// up to `reads` more reads of the scene; true once the reading is done
	bool step(const OpticsScene &scene, int reads);
	const ThermalReading &reading() const { return reading_; }
	const ThermalFrame &frame() const { return frame_; }

private:
	ThermalFrame frame_;
	bool coarse_only_ = false;
	int phase_ = 0, next_ = 0;
	std::vector<double> coarse_;
	std::vector<std::array<int, 2>> fine_;
	ThermalReading reading_, building_;
};

// Every sensor pixel at FINE_BANDS bands: the hottest and coldest, by brute force (O26's reference).
ThermalReading thermal_brute(const OpticsScene &scene, const ThermalFrame &frame);
// A signal as the camera reads it: its temperature, C, by its emissivity and the room it reflects.
double thermal_celsius(double signal, double room_kelvin, double emissivity, double reflected_kelvin);
// O26 (GAME_SPEC §18): the search finds what brute force does: its hottest and coldest within 0.05 K of every pixel's.
// "" when it does.
std::string check_O26(const ThermalReading &search, const ThermalReading &brute, double room_kelvin, double emissivity, double reflected_kelvin);

// What a camera's screen shows over its picture (Stage 48), an LCD pixel a sensor pixel: its palette's scale on the
// right (hottest up) with its span's ends, its centre's cross and spot reading, its emissivity and whether its range is
// auto or locked, ">150" or ">650" past its gain, and, with its markers on, a red mark on its hottest pixel and a blue
// one on its coldest with their temperatures. 8-bit sRGB and how much of each pixel it is (an OpticsRelay's overlay).
struct ThermalOverlay {
	int w = 0, h = 0;
	std::vector<uint8_t> rgb;
	std::vector<float> mask;
	uint64_t hash = 0;
};
// (the temperatures, C; `reading` not done: no marks yet)
void thermal_overlay(const ThermalSettings &settings, const ThermalSpan &span, const ThermalReading &reading, double hottest_c, double coldest_c, double centre_c, int w, int h,
		ThermalOverlay &out);

} // namespace lab
