// A thermal camera (GAME_SPEC §13, Stage 48): its palettes, its span and its search.
#include "lab/thermal.hpp"
#include "lab/thermal_data.hpp"

#include "chem/optics.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>

namespace lab {

namespace {

using json = nlohmann::json;

struct Data {
	std::vector<std::string> names;
	std::map<std::string, std::array<std::array<float, 3>, 256>, std::less<>> palettes;
	double high[2] = { -20.0, 150.0 }, low[2] = { 0.0, 650.0 }, up_c = 140.0, down_c = 120.0;
	double least_c = 5.0, moves_k = 0.5, moves_share = 0.03;
};

double linear(double code) {
	const double v = code / 255.0;
	return v <= 0.04045 ? v / 12.92 : std::pow((v + 0.055) / 1.055, 2.4);
}

const Data &data() {
	static const Data d = [] {
		Data o;
		const json j = json::parse(thermal::THERMAL_JSON);
		for (const auto &[name, points] : j.at("palettes").items()) {
			o.names.push_back(name);
			std::array<std::array<float, 3>, 256> table{};
			for (int k = 0; k < 256; ++k) {
				const double t = k / 255.0;
				size_t i = 0;
				while (i + 2 < points.size() && points.at(i + 1).at(0).get<double>() < t) { // (the segment t is in)
					++i;
				}
				const json &a = points.at(i), &b = points.at(std::min(i + 1, points.size() - 1));
				const double t0 = a.at(0).get<double>(), t1 = b.at(0).get<double>(), f = t1 > t0 ? std::clamp((t - t0) / (t1 - t0), 0.0, 1.0) : 0.0;
				for (int c = 0; c < 3; ++c) {
					table[static_cast<size_t>(k)][static_cast<size_t>(c)] =
							static_cast<float>(linear(a.at(static_cast<size_t>(1 + c)).get<double>()) * (1.0 - f) + linear(b.at(static_cast<size_t>(1 + c)).get<double>()) * f);
				}
			}
			o.palettes.emplace(name, table);
		}
		const json &g = j.at("gains");
		o.high[0] = g.at("high").at(0).get<double>();
		o.high[1] = g.at("high").at(1).get<double>();
		o.low[0] = g.at("low").at(0).get<double>();
		o.low[1] = g.at("low").at(1).get<double>();
		o.up_c = g.at("auto_up_c").get<double>();
		o.down_c = g.at("auto_down_c").get<double>();
		const json &s = j.at("auto_span");
		o.least_c = s.at("least_c").get<double>();
		o.moves_k = s.at("moves_k").get<double>();
		o.moves_share = s.at("moves_share").get<double>();
		return o;
	}();
	return d;
}

} // namespace

const std::vector<std::string> &thermal_palette_names() {
	return data().names;
}

const std::array<std::array<float, 3>, 256> &thermal_palette(std::string_view name) {
	const Data &d = data();
	const auto found = d.palettes.find(name);
	return found != d.palettes.end() ? found->second : d.palettes.find(d.names.front())->second;
}

void thermal_gain_range(std::string_view gain, double &low_c, double &high_c) {
	const Data &d = data();
	low_c = gain == "low" ? d.low[0] : d.high[0];
	high_c = gain == "low" ? d.low[1] : d.high[1];
}

ThermalSpan thermal_span(const ThermalSettings &settings, double coldest_c, double hottest_c, const ThermalSpan &was) {
	const Data &d = data();
	ThermalSpan s;
	s.gain = settings.gain == "auto" ? (was.gain == "low" ? (hottest_c < d.down_c ? "high" : "low") : (hottest_c > d.up_c ? "low" : "high")) : settings.gain;
	double lo = 0.0, hi = 0.0;
	thermal_gain_range(s.gain, lo, hi);
	s.over = hottest_c > hi;
	if (settings.locked) {
		s.low_c = settings.level_c - 0.5 * settings.span_c;
		s.high_c = settings.level_c + 0.5 * settings.span_c;
		return s;
	}
	double a = std::clamp(coldest_c, lo, hi), b = std::clamp(hottest_c, lo, hi);
	if (b - a < d.least_c) {
		const double middle = 0.5 * (a + b);
		a = middle - 0.5 * d.least_c;
		b = middle + 0.5 * d.least_c;
	}
	// moved only past its hysteresis (a gain changed: at once)
	const double slack = std::max(d.moves_k, d.moves_share * (was.high_c - was.low_c));
	if (was.gain == s.gain && std::fabs(a - was.low_c) <= slack && std::fabs(b - was.high_c) <= slack) {
		s.low_c = was.low_c;
		s.high_c = was.high_c;
		return s;
	}
	s.low_c = a;
	s.high_c = b;
	return s;
}

void ThermalSearch::start(const ThermalFrame &frame, bool coarse_only) {
	frame_ = frame;
	coarse_only_ = coarse_only;
	phase_ = 0;
	next_ = 0;
	coarse_.assign(static_cast<size_t>((frame.w + 1) / 2) * static_cast<size_t>((frame.h + 1) / 2), 0.0);
	fine_.clear();
	building_ = ThermalReading{};
	building_.hottest.signal = -1e30;
	building_.coldest.signal = 1e30;
}

bool ThermalSearch::step(const OpticsScene &scene, int reads) {
	const int bw = (frame_.w + 1) / 2, bh = (frame_.h + 1) / 2;
	const auto signal = [&](double px, double py, double size, int bands) { return optics_lwir_signal(scene, frame_.x + px, frame_.y + py, size, bands, frame_.hide); };
	const auto take = [&](double s, int x, int y) {
		if (s > building_.hottest.signal) {
			building_.hottest = ThermalSpot{ s, x, y };
		}
		if (s < building_.coldest.signal) {
			building_.coldest = ThermalSpot{ s, x, y };
		}
	};
	while (reads > 0) {
		if (phase_ == 0) {
			// every second pixel, each a square of two by two
			if (next_ >= bw * bh) {
				if (coarse_only_) {
					for (int k = 0; k < bw * bh; ++k) {
						take(coarse_[static_cast<size_t>(k)], std::min(2 * (k % bw), frame_.w - 1), std::min(2 * (k / bw), frame_.h - 1));
					}
					phase_ = 2;
					next_ = 0;
					continue;
				}
				// the four hottest and the four coldest squares, and the pixels of the three by three about each
				std::vector<int> order(static_cast<size_t>(bw * bh));
				for (int k = 0; k < bw * bh; ++k) {
					order[static_cast<size_t>(k)] = k;
				}
				std::sort(order.begin(), order.end(), [&](int a, int b) { return coarse_[static_cast<size_t>(a)] < coarse_[static_cast<size_t>(b)] || (coarse_[static_cast<size_t>(a)] == coarse_[static_cast<size_t>(b)] && a < b); });
				std::vector<int> picks;
				for (int k = 0; k < 4 && k < bw * bh; ++k) {
					picks.push_back(order[static_cast<size_t>(k)]);
					picks.push_back(order[order.size() - 1 - static_cast<size_t>(k)]);
				}
				std::vector<uint8_t> seen(static_cast<size_t>(frame_.w) * static_cast<size_t>(frame_.h), 0);
				for (const int k : picks) {
					const int cx = k % bw, cy = k / bw;
					for (int y = 2 * (cy - 1); y < 2 * (cy + 2); ++y) {
						for (int x = 2 * (cx - 1); x < 2 * (cx + 2); ++x) {
							if (x >= 0 && y >= 0 && x < frame_.w && y < frame_.h && !seen[static_cast<size_t>(y) * static_cast<size_t>(frame_.w) + static_cast<size_t>(x)]) {
								seen[static_cast<size_t>(y) * static_cast<size_t>(frame_.w) + static_cast<size_t>(x)] = 1;
								fine_.push_back({ x, y });
							}
						}
					}
				}
				phase_ = 1;
				next_ = 0;
				continue;
			}
			const int bx = next_ % bw, by = next_ / bw;
			coarse_[static_cast<size_t>(next_)] = signal(std::min(2.0 * bx + 1.0, static_cast<double>(frame_.w)), std::min(2.0 * by + 1.0, static_cast<double>(frame_.h)), 2.0, COARSE_BANDS);
			++next_;
			--reads;
		} else if (phase_ == 1) {
			if (next_ >= static_cast<int>(fine_.size())) {
				phase_ = 2;
				next_ = 0;
				continue;
			}
			const auto [x, y] = fine_[static_cast<size_t>(next_)];
			take(signal(x + 0.5, y + 0.5, 1.0, FINE_BANDS), x, y);
			++next_;
			--reads;
		} else {
			// the centre's spot: 3 x 3 pixels
			building_.centre = ThermalSpot{ signal(0.5 * frame_.w, 0.5 * frame_.h, 3.0, FINE_BANDS), frame_.w / 2, frame_.h / 2 };
			building_.done = true;
			reading_ = building_;
			start(frame_, coarse_only_);
			return true;
		}
	}
	return false;
}

ThermalReading thermal_brute(const OpticsScene &scene, const ThermalFrame &frame) {
	ThermalReading r;
	r.hottest.signal = -1e30;
	r.coldest.signal = 1e30;
	for (int y = 0; y < frame.h; ++y) {
		for (int x = 0; x < frame.w; ++x) {
			const double s = optics_lwir_signal(scene, frame.x + x + 0.5, frame.y + y + 0.5, 1.0, ThermalSearch::FINE_BANDS, frame.hide);
			if (s > r.hottest.signal) {
				r.hottest = ThermalSpot{ s, x, y };
			}
			if (s < r.coldest.signal) {
				r.coldest = ThermalSpot{ s, x, y };
			}
		}
	}
	r.done = true;
	return r;
}

double thermal_celsius(double signal, double room_kelvin, double emissivity, double reflected_kelvin) {
	return chem::optics::brightness_kelvin(signal * chem::optics::band_signal(room_kelvin), emissivity, reflected_kelvin) - 273.15;
}

namespace {

// a 3 x 5 font: what the camera's screen writes (a row a byte, its three low bits, the leftmost the highest)
const uint8_t *glyph(char c) {
	static const uint8_t digits[10][5] = { { 7, 5, 5, 5, 7 }, { 2, 6, 2, 2, 7 }, { 7, 1, 7, 4, 7 }, { 7, 1, 7, 1, 7 }, { 5, 5, 7, 1, 1 }, { 7, 4, 7, 1, 7 }, { 7, 4, 7, 5, 7 }, { 7, 1, 1, 2, 2 },
		{ 7, 5, 7, 5, 7 }, { 7, 5, 7, 1, 7 } };
	static const std::map<char, std::array<uint8_t, 5>> letters{ { '-', { 0, 0, 7, 0, 0 } }, { '.', { 0, 0, 0, 0, 2 } }, { '>', { 4, 2, 1, 2, 4 } }, { 'C', { 7, 4, 4, 4, 7 } },
		{ 'A', { 2, 5, 7, 5, 5 } }, { 'U', { 5, 5, 5, 5, 7 } }, { 'T', { 7, 2, 2, 2, 2 } }, { 'O', { 7, 5, 5, 5, 7 } }, { 'L', { 4, 4, 4, 4, 7 } }, { 'K', { 5, 5, 6, 5, 5 } },
		{ 'E', { 7, 4, 6, 4, 7 } }, { 'S', { 7, 4, 7, 1, 7 } }, { 'P', { 7, 5, 7, 4, 4 } }, { 'H', { 5, 5, 7, 5, 5 } } };
	if (c >= '0' && c <= '9') {
		return digits[c - '0'];
	}
	const auto found = letters.find(c);
	return found != letters.end() ? found->second.data() : nullptr;
}

struct Canvas {
	ThermalOverlay &o;
	void put(int x, int y, const std::array<uint8_t, 3> &c) {
		if (x < 0 || y < 0 || x >= o.w || y >= o.h) {
			return;
		}
		const size_t k = static_cast<size_t>(y) * static_cast<size_t>(o.w) + static_cast<size_t>(x);
		o.rgb[k * 3] = c[0];
		o.rgb[k * 3 + 1] = c[1];
		o.rgb[k * 3 + 2] = c[2];
		o.mask[k] = 1.0f;
	}
	static int width(const std::string &s) { return s.empty() ? 0 : 4 * static_cast<int>(s.size()) - 1; }
	// a string from (x, y), its top left, over a shadow a pixel down and right (readable on any colour)
	void write(int x, int y, const std::string &s, const std::array<uint8_t, 3> &c) {
		for (int pass = 0; pass < 2; ++pass) {
			for (size_t i = 0; i < s.size(); ++i) {
				const uint8_t *g = glyph(s[i]);
				for (int row = 0; g != nullptr && row < 5; ++row) {
					for (int col = 0; col < 3; ++col) {
						if ((g[row] >> (2 - col)) & 1) {
							put(x + 4 * static_cast<int>(i) + col + (pass == 0 ? 1 : 0), y + row + (pass == 0 ? 1 : 0), pass == 0 ? std::array<uint8_t, 3>{ 0, 0, 0 } : c);
						}
					}
				}
			}
		}
	}
};

std::string degrees(double c) {
	char text[32];
	std::snprintf(text, sizeof text, std::fabs(c) < 100.0 ? "%.1fC" : "%.0fC", c);
	return text;
}

uint8_t code_of(float linear) {
	const double v = std::clamp(static_cast<double>(linear), 0.0, 1.0);
	return static_cast<uint8_t>(std::lround(255.0 * (v <= 0.0031308 ? 12.92 * v : 1.055 * std::pow(v, 1.0 / 2.4) - 0.055)));
}

} // namespace

void thermal_overlay(const ThermalSettings &settings, const ThermalSpan &span, const ThermalReading &reading, double hottest_c, double coldest_c, double centre_c, int w, int h,
		ThermalOverlay &out) {
	out.w = w;
	out.h = h;
	out.rgb.assign(static_cast<size_t>(std::max(w, 0)) * static_cast<size_t>(std::max(h, 0)) * 3, 0);
	out.mask.assign(static_cast<size_t>(std::max(w, 0)) * static_cast<size_t>(std::max(h, 0)), 0.0f);
	if (w < 24 || h < 24) {
		out.hash = 0;
		return;
	}
	Canvas c{ out };
	const std::array<uint8_t, 3> white{ 255, 255, 255 };
	// the scale: the palette, hottest up, and its span's ends
	const auto &palette = thermal_palette(settings.palette);
	const int top = 9, bottom = h - 9;
	for (int y = top; y < bottom; ++y) {
		const auto &e = palette[static_cast<size_t>(std::clamp(static_cast<int>(std::lround(255.0 * (bottom - 1 - y) / std::max(bottom - 1 - top, 1))), 0, 255))];
		for (int x = w - 7; x < w - 2; ++x) {
			c.put(x, y, { code_of(e[0]), code_of(e[1]), code_of(e[2]) });
		}
	}
	const std::string high = degrees(span.high_c), low = degrees(span.low_c);
	c.write(w - 2 - Canvas::width(high), 1, high, white);
	c.write(w - 2 - Canvas::width(low), h - 7, low, white);
	// the centre's cross, and its spot's reading
	const int mx = w / 2, my = h / 2;
	for (int d = 2; d <= 5; ++d) {
		c.put(mx - d, my, white);
		c.put(mx + d, my, white);
		c.put(mx, my - d, white);
		c.put(mx, my + d, white);
	}
	c.write(2, 1, "SP" + degrees(centre_c), white);
	char e[16];
	std::snprintf(e, sizeof e, "E%.2f", settings.emissivity);
	c.write(2, h - 7, std::string(e) + (settings.locked ? " LOCK" : " AUTO"), white);
	if (span.over) {
		const std::string over = span.gain == "low" ? ">650" : ">150";
		c.write(mx - Canvas::width(over) / 2, 1, over, { 255, 80, 60 });
	}
	// the hottest and the coldest, each a mark (a triangle, up and down) and its temperature
	if (settings.markers && reading.done) {
		const auto mark = [&](const ThermalSpot &s, double celsius, bool up, const std::array<uint8_t, 3> &colour) {
			for (int row = 0; row < 4; ++row) {
				for (int dx = -row; dx <= row; ++dx) {
					c.put(s.x + dx, up ? s.y + 1 + row : s.y - 1 - row, colour);
				}
			}
			const std::string t = degrees(celsius);
			const int tx = std::clamp(s.x + 5, 1, w - 10 - Canvas::width(t)), ty = std::clamp(up ? s.y + 1 : s.y - 6, 8, h - 15);
			c.write(tx, ty, t, colour);
		};
		mark(reading.hottest, hottest_c, true, { 255, 60, 40 });
		mark(reading.coldest, coldest_c, false, { 70, 140, 255 });
	}
	uint64_t hash = 1469598103934665603ull;
	for (const uint8_t b : out.rgb) {
		hash = (hash ^ b) * 1099511628211ull;
	}
	for (const float m : out.mask) {
		hash = (hash ^ (m > 0.0f ? 1u : 0u)) * 1099511628211ull;
	}
	out.hash = hash;
}

std::string check_O26(const ThermalReading &search, const ThermalReading &brute, double room_kelvin, double emissivity, double reflected_kelvin) {
	char text[240];
	const double hot = thermal_celsius(search.hottest.signal, room_kelvin, emissivity, reflected_kelvin), hot_b = thermal_celsius(brute.hottest.signal, room_kelvin, emissivity, reflected_kelvin);
	const double cold = thermal_celsius(search.coldest.signal, room_kelvin, emissivity, reflected_kelvin), cold_b = thermal_celsius(brute.coldest.signal, room_kelvin, emissivity, reflected_kelvin);
	if (!search.done || !(std::fabs(hot - hot_b) <= 0.05) || !(std::fabs(cold - cold_b) <= 0.05)) {
		std::snprintf(text, sizeof text, "O26: the search finds %.3f C (%d, %d) and %.3f C; every pixel's hottest is %.3f C (%d, %d), its coldest %.3f C", hot, search.hottest.x, search.hottest.y, cold, hot_b,
				brute.hottest.x, brute.hottest.y, cold_b);
		return text;
	}
	return "";
}

} // namespace lab
