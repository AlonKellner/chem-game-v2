// The spectral viewer's probe (GAME_SPEC §12, §16; Stage 42): a disc's light at every nanometre, ray by ray.
#include "lab/spectrometer.hpp"

#include "lab/colour.hpp"

#include "chem/optics.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

namespace lab {

const std::array<int, SPECTRUM_NM> &reading_order() {
	static const std::array<int, SPECTRUM_NM> order = [] {
		std::array<int, SPECTRUM_NM> o{};
		std::array<bool, SPECTRUM_NM> taken{};
		int n = 0;
		for (int step = 16; step >= 1; step /= 2) {
			for (int k = 0; k < SPECTRUM_NM; k += step) {
				if (!taken[static_cast<size_t>(k)]) {
					taken[static_cast<size_t>(k)] = true;
					o[static_cast<size_t>(n++)] = k;
				}
			}
		}
		return o;
	}();
	return order;
}

namespace {

// R2: the plastic number's powers (Roberts 2018), each nanometre moved by its own golden-ratio offsets
void r2(int k, int64_t n, double &s, double &t) {
	constexpr double A1 = 0.7548776662466927, A2 = 0.5698402909980532;
	const double o1 = std::fmod(0.5 + 0.6180339887498949 * k, 1.0), o2 = std::fmod(0.5 + 0.4142135623730950 * k, 1.0);
	s = std::fmod(o1 + A1 * static_cast<double>(n), 1.0);
	t = std::fmod(o2 + A2 * static_cast<double>(n), 1.0);
}

} // namespace

void disc_point(int k, int64_t n, double &u, double &v) {
	double s = 0.0, t = 0.0;
	r2(k, n, s, t);
	const double r = std::sqrt(s), a = 6.283185307179586 * t;
	u = r * std::cos(a);
	v = r * std::sin(a);
}

double radiance_of(double factor, int k) {
	// the room's white is D65 at ROOM_WHITE_CD_M2: 683 lm/W over its luminance
	static const double scale = [] {
		const chem::optics::Spectrum &d = chem::optics::d65(), &y = chem::optics::ybar();
		double lum = 0.0;
		for (int i = 0; i < SPECTRUM_NM; ++i) {
			lum += d[static_cast<size_t>(i)] * y[static_cast<size_t>(i)];
		}
		return ROOM_WHITE_CD_M2 / (683.0 * lum);
	}();
	return factor * chem::optics::d65()[static_cast<size_t>(k)] * scale;
}

namespace {

void key_of(const OpticsScene &scene, double x, double y, double radius, double key[4]) {
	// what the disc's square looks through, at its middle and at its four sides (each a sample of the disc's
	// width: a body of a neighbouring square of the scene is seen from the side that lies in it)
	key[0] = key[1] = key[2] = key[3] = 0.0;
	const double at[5][2] = { { 0.0, 0.0 }, { -1.0, 0.0 }, { 1.0, 0.0 }, { 0.0, -1.0 }, { 0.0, 1.0 } };
	for (int i = 0; i < 5; ++i) {
		double k[4];
		optics_point_key(scene, x + at[i][0] * radius, y + at[i][1] * radius, 2.0 * radius, k);
		for (int j = 0; j < 4; ++j) {
			key[j] += k[j] * (1.0 + 0.381966 * i);
		}
	}
}

} // namespace

void Spectrometer::restart(bool keep_before) {
	if (keep_before && read_count() > 0) {
		before_ = curve();
		has_before_ = true;
	} else if (!keep_before) {
		has_before_ = false;
	}
	sum_.fill(0.0);
	sum2_.fill(0.0);
	mean_.fill(0.0);
	counts_.fill(0);
	cursor_ = 0;
	++restarts_;
}

void Spectrometer::aim(const OpticsScene &scene, double x, double y, double radius) {
	double key[4];
	key_of(scene, x, y, radius, key);
	const bool moved = x != x_ || y != y_ || radius != radius_;
	const bool changed = key[0] != key_[0] || key[1] != key_[1] || key[2] != key_[2] || key[3] != key_[3];
	x_ = x;
	y_ = y;
	radius_ = radius;
	for (int j = 0; j < 4; ++j) {
		key_[j] = key[j];
	}
	if (moved && restarting_) {
		restart(false);
	} else if (changed && restarting_) {
		restart(true);
	}
}

int Spectrometer::read(const OpticsScene &scene, int64_t most, const std::function<bool()> &more) {
	const std::array<int, SPECTRUM_NM> &order = reading_order();
	int done = 0;
	while (done < most && !settled()) {
		if (more && !more()) {
			break;
		}
		const int k = order[static_cast<size_t>(cursor_)];
		cursor_ = (cursor_ + 1) % SPECTRUM_NM;
		int &n = counts_[static_cast<size_t>(k)];
		if (n >= PROBE_SAMPLES_MOST) {
			continue;
		}
		double u = 0.0, v = 0.0;
		switch (sampling_) {
			case Sampling::Disc:
				disc_point(k, n, u, v);
				break;
			case Sampling::Centre:
				break;
			case Sampling::Square: {
				double a = 0.0, b = 0.0;
				r2(k, n, a, b);
				u = 2.0 * a - 1.0;
				v = 2.0 * b - 1.0;
				break;
			}
			case Sampling::ByRadius: {
				disc_point(k, n, u, v);
				const double r = std::hypot(u, v);
				if (r > 0.0) {
					u *= r;
					v *= r;
				}
				break;
			}
		}
		const double light = optics_point(scene, x_ + radius_ * u, y_ + radius_ * v, SPECTRUM_FIRST_NM + k);
		sum_[static_cast<size_t>(k)] += light;
		sum2_[static_cast<size_t>(k)] += light * light;
		++n;
		mean_[static_cast<size_t>(k)] = sum_[static_cast<size_t>(k)] / n;
		++done;
	}
	return done;
}

double Spectrometer::spread(int k) const {
	const int n = counts_[static_cast<size_t>(k)];
	if (n < 2) {
		return 0.0;
	}
	const double m = sum_[static_cast<size_t>(k)] / n;
	const double var = (sum2_[static_cast<size_t>(k)] - n * m * m) / (n - 1);
	return var > 0.0 ? std::sqrt(var) : 0.0;
}

std::array<double, SPECTRUM_NM> Spectrometer::curve() const {
	std::array<double, SPECTRUM_NM> c{};
	int last = -1;
	for (int k = 0; k < SPECTRUM_NM; ++k) {
		if (counts_[static_cast<size_t>(k)] == 0) {
			continue;
		}
		c[static_cast<size_t>(k)] = mean_[static_cast<size_t>(k)];
		if (!has_before_) {
			// between this one and the one read before it: the straight line; before the first, the first
			for (int j = last + 1; j < k; ++j) {
				c[static_cast<size_t>(j)] = last < 0 ? mean_[static_cast<size_t>(k)] : mean_[static_cast<size_t>(last)] + (mean_[static_cast<size_t>(k)] - mean_[static_cast<size_t>(last)]) * (j - last) / static_cast<double>(k - last);
			}
		}
		last = k;
	}
	for (int k = 0; k < SPECTRUM_NM; ++k) {
		if (counts_[static_cast<size_t>(k)] == 0) {
			c[static_cast<size_t>(k)] = has_before_ ? before_[static_cast<size_t>(k)] : (last < 0 ? 0.0 : (k > last ? mean_[static_cast<size_t>(last)] : c[static_cast<size_t>(k)]));
		}
	}
	return c;
}

int Spectrometer::read_count() const {
	int n = 0;
	for (const int c : counts_) {
		n += c > 0 ? 1 : 0;
	}
	return n;
}

bool Spectrometer::settled() const {
	for (const int c : counts_) {
		if (c < PROBE_SAMPLES_MOST) {
			return false;
		}
	}
	return true;
}

// ---- the viewer's screen

namespace {

// OKLab (Ottosson 2020) from linear sRGB, and back
void oklab_of(const double rgb[3], double lab[3]) {
	const double l = std::cbrt(0.4122214708 * rgb[0] + 0.5363325363 * rgb[1] + 0.0514459929 * rgb[2]);
	const double m = std::cbrt(0.2119034982 * rgb[0] + 0.6806995451 * rgb[1] + 0.1073969566 * rgb[2]);
	const double s = std::cbrt(0.0883024619 * rgb[0] + 0.2817188376 * rgb[1] + 0.6299787005 * rgb[2]);
	lab[0] = 0.2104542553 * l + 0.7936177850 * m - 0.0040720468 * s;
	lab[1] = 1.9779984951 * l - 2.4285922050 * m + 0.4505937099 * s;
	lab[2] = 0.0259040371 * l + 0.7827717662 * m - 0.8086757660 * s;
}

void rgb_of(double lightness, double chroma, double hue, double rgb[3]) {
	const double a = chroma * std::cos(hue), b = chroma * std::sin(hue);
	const double l = std::pow(lightness + 0.3963377774 * a + 0.2158037573 * b, 3.0);
	const double m = std::pow(lightness - 0.1055613458 * a - 0.0638541728 * b, 3.0);
	const double s = std::pow(lightness - 0.0894841775 * a - 1.2914855480 * b, 3.0);
	rgb[0] = 4.0767416621 * l - 3.3077115913 * m + 0.2309699292 * s;
	rgb[1] = -1.2684380046 * l + 2.6097574011 * m - 0.3413193965 * s;
	rgb[2] = -0.0041960863 * l - 0.7034186147 * m + 1.7076147010 * s;
}

bool in_gamut(const double rgb[3]) {
	return rgb[0] >= -1e-9 && rgb[1] >= -1e-9 && rgb[2] >= -1e-9 && rgb[0] <= 1.0 + 1e-9 && rgb[1] <= 1.0 + 1e-9 && rgb[2] <= 1.0 + 1e-9;
}

} // namespace

const std::array<std::array<uint8_t, 3>, SPECTRUM_NM> &rainbow() {
	static const std::array<std::array<uint8_t, 3>, SPECTRUM_NM> colours = [] {
		std::array<std::array<uint8_t, 3>, SPECTRUM_NM> c{};
		const chem::optics::Spectrum &xb = chem::optics::xbar(), &yb = chem::optics::ybar(), &zb = chem::optics::zbar();
		for (int k = 0; k < SPECTRUM_NM; ++k) {
			const double nm = SPECTRUM_FIRST_NM + k;
			// the light of this wavelength alone: its hue (OKLab), and of that hue the most saturated colour the
			// display has (its cusp: the greatest chroma at any lightness)
			double rgb[3], lab[3];
			to_linear_srgb(chem::optics::Xyz{ xb[static_cast<size_t>(k)], yb[static_cast<size_t>(k)], zb[static_cast<size_t>(k)] }, rgb);
			oklab_of(rgb, lab);
			// (the cube roots of a colour no display has can be of negatives: its hue is still the line from grey)
			const double hue = std::atan2(lab[2], lab[1]);
			double best_c = -1.0, best_l = 0.5;
			for (int i = 0; i <= 120; ++i) {
				const double lightness = 0.30 + 0.65 * i / 120.0;
				double lo = 0.0, hi = 0.5;
				for (int j = 0; j < 40; ++j) {
					const double mid = 0.5 * (lo + hi);
					double probe[3];
					rgb_of(lightness, mid, hue, probe);
					(in_gamut(probe) ? lo : hi) = mid;
				}
				if (lo > best_c) {
					best_c = lo;
					best_l = lightness;
				}
			}
			rgb_of(best_l, best_c, hue, rgb);
			// dimmer toward the ends, where the eye sees little (as a spectrometer's display draws them)
			const double taper = nm < 420.0 ? 0.3 + 0.7 * (nm - 380.0) / 40.0 : (nm > 700.0 ? 0.3 + 0.7 * (780.0 - nm) / 80.0 : 1.0);
			for (int i = 0; i < 3; ++i) {
				const double v = std::clamp(rgb[i], 0.0, 1.0) * taper * taper;
				c[static_cast<size_t>(k)][static_cast<size_t>(i)] = static_cast<uint8_t>(std::lround(255.0 * srgb_encode(v)));
			}
		}
		return c;
	}();
	return colours;
}

namespace {

// digits and the letters n and m, three pixels across and five down, a row a byte (its three low bits, the
// leftmost the highest)
const uint8_t *glyph(char c) {
	static const uint8_t digits[10][5] = { { 7, 5, 5, 5, 7 }, { 2, 6, 2, 2, 7 }, { 7, 1, 7, 4, 7 }, { 7, 1, 7, 1, 7 }, { 5, 5, 7, 1, 1 }, { 7, 4, 7, 1, 7 }, { 7, 4, 7, 5, 7 }, { 7, 1, 1, 2, 2 },
		{ 7, 5, 7, 5, 7 }, { 7, 5, 7, 1, 7 } };
	static const uint8_t n[5] = { 0, 6, 5, 5, 5 }, m[5] = { 0, 7, 7, 5, 5 };
	if (c >= '0' && c <= '9') {
		return digits[c - '0'];
	}
	return c == 'n' ? n : (c == 'm' ? m : nullptr);
}

void put(Picture &p, int x, int y, const std::array<uint8_t, 3> &c) {
	if (x < 0 || y < 0 || x >= p.w || y >= p.h) {
		return;
	}
	uint8_t *at = &p.rgb[(static_cast<size_t>(y) * static_cast<size_t>(p.w) + static_cast<size_t>(x)) * 3];
	at[0] = c[0];
	at[1] = c[1];
	at[2] = c[2];
}

// a string from (x, y), its top left, a glyph each four pixels
void write(Picture &p, int x, int y, const std::string &s, const std::array<uint8_t, 3> &c) {
	for (size_t i = 0; i < s.size(); ++i) {
		const uint8_t *g = glyph(s[i]);
		if (g == nullptr) {
			continue;
		}
		for (int row = 0; row < 5; ++row) {
			for (int col = 0; col < 3; ++col) {
				if ((g[row] >> (2 - col)) & 1) {
					put(p, x + 4 * static_cast<int>(i) + col, y + row, c);
				}
			}
		}
	}
}

int text_width(const std::string &s) {
	return s.empty() ? 0 : 4 * static_cast<int>(s.size()) - 1;
}

// an 8-bit sRGB code dimmed to `share` of its light
uint8_t dimmed(uint8_t code, double share) {
	const double v = code / 255.0;
	const double linear = v <= 0.04045 ? v / 12.92 : std::pow((v + 0.055) / 1.055, 2.4);
	return static_cast<uint8_t>(std::lround(255.0 * srgb_encode(linear * share)));
}

} // namespace

void plot_spectrum(const std::array<double, SPECTRUM_NM> &curve, int read, Picture &p) {
	p.w = SCREEN_W;
	p.h = SCREEN_H;
	p.rgb.assign(static_cast<size_t>(p.w) * static_cast<size_t>(p.h) * 3, 0);
	const std::array<uint8_t, 3> grid{ 28, 28, 28 }, axis{ 120, 120, 120 }, white{ 235, 235, 235 }, label{ 190, 190, 190 };
	const std::array<std::array<uint8_t, 3>, SPECTRUM_NM> &bow = rainbow();
	// the grid, every 50 nm, and the axes
	for (int nm = 400; nm <= 750; nm += 50) {
		for (int y = PLOT_TOP; y <= PLOT_BOTTOM; ++y) {
			put(p, PLOT_X + nm - SPECTRUM_FIRST_NM, y, grid);
		}
	}
	for (int x = PLOT_X - 1; x < PLOT_X + SPECTRUM_NM; ++x) {
		put(p, x, AXIS_ROW, axis);
	}
	for (int y = PLOT_TOP; y <= AXIS_ROW; ++y) {
		put(p, PLOT_X - 1, y, axis);
	}
	// the rainbow under the axis, and the ticks and labels under it
	for (int k = 0; k < SPECTRUM_NM; ++k) {
		for (int y = STRIP_TOP; y < STRIP_TOP + STRIP_ROWS; ++y) {
			put(p, PLOT_X + k, y, bow[static_cast<size_t>(k)]);
		}
	}
	for (int nm = 400; nm <= 750; nm += 50) {
		const int x = PLOT_X + nm - SPECTRUM_FIRST_NM;
		for (int y = TICK_TOP; y < TICK_TOP + (nm % 100 == 0 ? 3 : 2); ++y) {
			put(p, x, y, label);
		}
		if (nm % 100 == 0) {
			const std::string s = std::to_string(nm);
			write(p, x - text_width(s) / 2, LABEL_TOP, s, label);
		}
	}
	write(p, PLOT_X + SPECTRUM_NM - text_width("nm"), LABEL_TOP + 7, "nm", label);
	// the curve, scaled to its peak
	double peak = 0.0;
	int peak_k = -1;
	if (read > 0) {
		for (int k = 0; k < SPECTRUM_NM; ++k) {
			const double v = curve[static_cast<size_t>(k)];
			if (std::isfinite(v) && v > peak) {
				peak = v;
				peak_k = k;
			}
		}
	}
	if (peak_k >= 0 && peak > 0.0) {
		const int rows = PLOT_BOTTOM - PLOT_TOP;
		std::array<int, SPECTRUM_NM> top{};
		for (int k = 0; k < SPECTRUM_NM; ++k) {
			const double v = curve[static_cast<size_t>(k)];
			const double share = std::isfinite(v) ? std::clamp(v / peak, 0.0, 1.0) : 0.0;
			top[static_cast<size_t>(k)] = PLOT_BOTTOM - static_cast<int>(std::lround(share * rows));
		}
		for (int k = 0; k < SPECTRUM_NM; ++k) {
			const int x = PLOT_X + k;
			const std::array<uint8_t, 3> fill{ dimmed(bow[static_cast<size_t>(k)][0], 0.35), dimmed(bow[static_cast<size_t>(k)][1], 0.35), dimmed(bow[static_cast<size_t>(k)][2], 0.35) };
			for (int y = top[static_cast<size_t>(k)] + 1; y <= PLOT_BOTTOM; ++y) {
				put(p, x, y, fill);
			}
			// the line: down or up to each neighbour's height, half way
			int lo = top[static_cast<size_t>(k)], hi = top[static_cast<size_t>(k)];
			for (const int j : { k - 1, k + 1 }) {
				if (j >= 0 && j < SPECTRUM_NM) {
					const int mid = (top[static_cast<size_t>(k)] + top[static_cast<size_t>(j)]) / 2;
					lo = std::min(lo, mid);
					hi = std::max(hi, mid);
				}
			}
			for (int y = lo; y <= hi; ++y) {
				put(p, x, y, white);
			}
		}
		const std::string s = std::to_string(SPECTRUM_FIRST_NM + peak_k);
		const int at = std::clamp(PLOT_X + peak_k - text_width(s) / 2, 0, SCREEN_W - text_width(s));
		write(p, at, PLOT_TOP - 9, s, white);
	}
	// the hash: FNV-1a over every byte
	uint64_t h = 1469598103934665603ull;
	for (const uint8_t b : p.rgb) {
		h = (h ^ b) * 1099511628211ull;
	}
	p.hash = h;
}

double probe_truth(const OpticsScene &scene, double x, double y, double radius, int k, int rings) {
	const int around = 6 * rings;
	double sum = 0.0;
	for (int i = 0; i < rings; ++i) {
		const double r = radius * std::sqrt((i + 0.5) / rings);
		for (int j = 0; j < around; ++j) {
			const double a = 6.283185307179586 * (j + 0.5 * (i % 2)) / around;
			sum += optics_point_truth(scene, x + r * std::cos(a), y + r * std::sin(a), SPECTRUM_FIRST_NM + k);
		}
	}
	return sum / (rings * around);
}

std::string check_O19(const OpticsScene &scene, const Spectrometer &probe, const std::vector<int> &nms, int rings) {
	for (const int k : nms) {
		const int n = probe.counts()[static_cast<size_t>(k)];
		if (n < 1) {
			return "O19: nanometre " + std::to_string(SPECTRUM_FIRST_NM + k) + " was not read";
		}
		const double truth = probe_truth(scene, probe.x(), probe.y(), probe.radius(), k, rings);
		const double mean = probe.mean()[static_cast<size_t>(k)];
		const double allowed = 4.0 * probe.spread(k) / std::sqrt(static_cast<double>(n)) + 1.0e-4 * std::max(1.0, std::fabs(truth));
		if (!(std::fabs(mean - truth) <= allowed)) {
			char buffer[200];
			std::snprintf(buffer, sizeof buffer, "O19: at %d nm the probe's mean of %d rays is %.6g, the disc's truth %.6g (allowed %.3g)", SPECTRUM_FIRST_NM + k, n, mean, truth, allowed);
			return buffer;
		}
	}
	return "";
}

} // namespace lab
