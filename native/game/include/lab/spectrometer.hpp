#pragma once

#include "lab/optics_scene.hpp"

#include <array>
#include <cstdint>
#include <functional>
#include <vector>

// The spectral viewer's probe and its screen (GAME_SPEC §12: the spectrum probe; §16: the spectrometer; Stage 42),
// pure: what reaches the eye from a small disc of the screen, at every nanometre, read ray by ray and averaged
// over time; and the picture its screen shows of it, an image of red, green and blue pixels.
namespace lab {

inline constexpr int SPECTRUM_FIRST_NM = 380;
inline constexpr int SPECTRUM_NM = 401; // 380 .. 780, at 1 nm
// the probe's disc, in the screen's pixels at the room's own scale (design: a fibre's face, a tenth of a square)
inline constexpr double PROBE_RADIUS_PX = 4.0;
// the samples a nanometre's mean takes before it is settled (its noise then a sixty-fourth of one sample's)
inline constexpr int PROBE_SAMPLES_MOST = 4096;

// The order a sweep reads the nanometres in: every 16th, then the 8ths between them, the 4ths, the 2nds and
// the rest; so a sweep cut short anywhere has read the spectrum evenly at some spacing.
const std::array<int, SPECTRUM_NM> &reading_order();
// The n-th point of nanometre k's own sequence over the unit disc, uniform by area: the plastic number's
// sequence (R2), moved by an offset of k's own (Cranley-Patterson), its radius the square root of the first.
void disc_point(int k, int64_t n, double &u, double &v);
// A factor of the room's light at a wavelength as spectral radiance (W / (sr m^2 nm)): the room's white
// (ROOM_WHITE_CD_M2) is D65 at that luminance.
double radiance_of(double factor, int k);

// What reaches the eye from a disc of the screen, at each nanometre (GAME_SPEC §12): the mean of rays through
// points uniform over the disc, each nanometre's its own, read by the pass's own source (optics_point). A
// sweep reads one ray at each nanometre (reading_order), and the sweeps go on until every nanometre has
// PROBE_SAMPLES_MOST: the mean then is the disc's own uniform integral, to its noise. It starts again where the
// disc moves, or what it looks through changes (its content key, as a pixel's history: optics_point_key).
class Spectrometer {
public:
	// Where it reads: the disc's centre and radius, in the scene's pixels, and what it looks through. A move, a
	// change of radius or of what it looks through starts the reading again; the same aim does nothing.
	void aim(const OpticsScene &scene, double x, double y, double radius);
	// Reads rays, at most `most`, and while `more()` says there is time (none: no limit but `most`); returns how
	// many it read. Each ray is the same ray whatever the budget: what a reading comes to depends only on how
	// many rays it has read.
	int read(const OpticsScene &scene, int64_t most, const std::function<bool()> &more = {});
	// each nanometre's mean (a factor of the room's light) and how many rays it has
	const std::array<double, SPECTRUM_NM> &mean() const { return mean_; }
	const std::array<int, SPECTRUM_NM> &counts() const { return counts_; }
	// one ray's spread at each nanometre (its standard deviation, from the rays read)
	double spread(int k) const;
	// What its screen shows: the mean where a nanometre has been read; between them, the straight line from
	// those on each side (beyond the last, the last); where what it looks through changed and it has not yet
	// been read again, the reading before (a curve that moves where the light moves, not one that starts empty).
	std::array<double, SPECTRUM_NM> curve() const;
	// how many nanometres have been read at all, and whether every one has its rays
	int read_count() const;
	bool settled() const;
	// how many times it started again (a test's count)
	int restarts() const { return restarts_; }
	double x() const { return x_; }
	double y() const { return y_; }
	double radius() const { return radius_; }

	// (for O19's self-tests: a way of sampling that is wrong)
	enum class Sampling { Disc, Centre, Square, ByRadius };
	void set_sampling(Sampling s) { sampling_ = s; }
	void set_restarting(bool on) { restarting_ = on; }

private:
	void restart(bool keep_before);
	double x_ = -1.0e30, y_ = -1.0e30, radius_ = 0.0;
	double key_[4] = { 0.0, 0.0, 0.0, 0.0 };
	std::array<double, SPECTRUM_NM> sum_{}, sum2_{}, mean_{}, before_{};
	std::array<int, SPECTRUM_NM> counts_{};
	bool has_before_ = false;
	int cursor_ = 0; // where in reading_order the next ray is
	int restarts_ = 0;
	Sampling sampling_ = Sampling::Disc;
	bool restarting_ = true;
};

// ---- the viewer's screen: an image of red, green and blue pixels (GAME_SPEC §16, Stage 42)

// The screen's pixels across and down: a column a nanometre for the plot, and the margins about it.
inline constexpr int SCREEN_W = 416;
inline constexpr int SCREEN_H = 232;
// Where the plot lies on it: nanometre k in column PLOT_X + k; the plot's rows PLOT_TOP to PLOT_BOTTOM (its
// floor), the axis under it, then the rainbow strip and the ticks, and the labels under them.
inline constexpr int PLOT_X = 8;
inline constexpr int PLOT_TOP = 14;
inline constexpr int PLOT_BOTTOM = 199;
inline constexpr int AXIS_ROW = 200;
inline constexpr int STRIP_TOP = 202;
inline constexpr int STRIP_ROWS = 8;
inline constexpr int TICK_TOP = 210;
inline constexpr int LABEL_TOP = 215;

// What a screen shows: each pixel's red, green and blue as the display is sent them (8-bit, sRGB-encoded),
// row by row from the top; and a hash of them, which moves only when a pixel does.
struct Picture {
	int w = SCREEN_W, h = SCREEN_H;
	std::vector<uint8_t> rgb = std::vector<uint8_t>(static_cast<size_t>(SCREEN_W) * SCREEN_H * 3, 0);
	uint64_t hash = 0;
	const uint8_t *at(int x, int y) const { return &rgb[(static_cast<size_t>(y) * static_cast<size_t>(w) + static_cast<size_t>(x)) * 3]; }
};
// The colour each nanometre stands for, as the screen shows it (8-bit sRGB): the light of that wavelength
// alone, at one lightness (dimmer toward the ends, where the eye sees little), its chroma brought into the
// display's gamut (OKLCh: its hue kept). Computed once.
const std::array<std::array<uint8_t, 3>, SPECTRUM_NM> &rainbow();
// The plot of a reading: under its curve each column filled with its nanometre's colour, dimmed; the curve
// in white over it, scaled so its peak fills the plot; a grid every 50 nm; under it the axis, the rainbow,
// a tick every 50 nm, the labels 400 to 700 and "nm"; over the peak, its wavelength. `read` nanometres read
// (0: no curve, the axes alone). The same reading is the same picture.
void plot_spectrum(const std::array<double, SPECTRUM_NM> &curve, int read, Picture &picture);

// The disc's truth (GAME_SPEC §18: O19): the uniform integral of the light over the disc at a nanometre, by the
// truth's own tracer (optics_point_truth) on a polar grid of `rings` rings of equal area, each of `rings` * 6
// points in each, each point the middle of its cell.
double probe_truth(const OpticsScene &scene, double x, double y, double radius, int k, int rings = 24);
// O19 (GAME_SPEC §18, Stage 42): the probe settles to its disc. A spectrometer that has read every nanometre
// `rays` times holds, at each nanometre of `nms`, a mean within four of its own standard errors (and 1e-4 of
// the light) of the disc's truth. "" when it holds.
std::string check_O19(const OpticsScene &scene, const Spectrometer &probe, const std::vector<int> &nms, int rings = 24);

} // namespace lab
