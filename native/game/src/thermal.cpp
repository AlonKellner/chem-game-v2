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
