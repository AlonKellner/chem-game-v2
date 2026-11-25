// The lab's instruments (GAME_SPEC §13, Readings; Stage 50).
#include "lab/instruments.hpp"
#include "lab/instruments_data.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace lab::instruments {

namespace {

using json = nlohmann::json;

struct Data {
	std::vector<Balance> balances;
	Meter thermometer, ph, gauge;
	double hand = 0.1, reference = 8000.0;
	std::vector<std::pair<std::string, double>> walls;
};

const Data &data() {
	static const Data d = [] {
		Data o;
		const json j = json::parse(INSTRUMENTS_JSON);
		o.reference = j.at("reference_density_kg_m3").get<double>();
		for (const json &b : j.at("balances")) {
			o.balances.push_back(Balance{ b.at("name").get<std::string>(), b.at("readability_g").get<double>(), b.at("capacity_g").get<double>(), b.at("linearity_g").get<double>() });
		}
		o.thermometer = Meter{ j.at("thermometer").at("resolution_c").get<double>(), j.at("thermometer").at("accuracy_c").get<double>(), j.at("thermometer").value("time_constant_s", 0.0) };
		o.ph = Meter{ j.at("ph_meter").at("resolution").get<double>(), j.at("ph_meter").at("accuracy").get<double>() };
		o.gauge = Meter{ j.at("gauge").at("resolution_kpa").get<double>(), j.at("gauge").at("accuracy_kpa").get<double>() };
		o.hand = j.at("hand").get<double>();
		for (const auto &[name, density] : j.at("walls_g_cm3").items()) {
			o.walls.emplace_back(name, density.get<double>());
		}
		return o;
	}();
	return d;
}

} // namespace

const std::vector<Balance> &balances() {
	return data().balances;
}

const Balance *balance(std::string_view name) {
	for (const Balance &b : data().balances) {
		if (b.name == name) {
			return &b;
		}
	}
	return nullptr;
}

Meter thermometer() {
	return data().thermometer;
}

Meter ph_meter() {
	return data().ph;
}

Meter gauge() {
	return data().gauge;
}

double hand() {
	return data().hand;
}

double reference_density() {
	return data().reference;
}

double wall_density(std::string_view species) {
	for (const auto &[name, density] : data().walls) {
		if (name == species) {
			return density;
		}
	}
	return 0.0;
}

double share(uint64_t seed, std::string_view key) {
	uint64_t h = 1469598103934665603ull; // FNV-1a, 64 bits
	for (const char c : key) {
		h ^= static_cast<uint8_t>(c);
		h *= 1099511628211ull;
	}
	uint64_t z = h + seed * 0x9e3779b97f4a7c15ull + 0x9e3779b97f4a7c15ull; // SplitMix64
	z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ull;
	z = (z ^ (z >> 27)) * 0x94d049bb133111ebull;
	z ^= z >> 31;
	return static_cast<double>(z >> 11) / static_cast<double>(1ull << 52) - 1.0; // 53 bits to [-1, 1)
}

double lagged(double reading, double truth, double dt, double tau) {
	return tau > 0.0 ? reading + (truth - reading) * (1.0 - std::exp(-dt / tau)) : truth;
}

double rounded(double value, double resolution) {
	return resolution > 0.0 ? std::round(value / resolution) * resolution : value;
}

double conventional_g(double mass_g, double volume_cm3, double air_kg_m3) {
	const double air_g_cm3 = air_kg_m3 * 1e-3;
	return (mass_g - air_g_cm3 * volume_cm3) / (1.0 - air_kg_m3 / reference_density());
}

double linearity_g(const Balance &model, double gross_g, double share) {
	return share * model.linearity_g * std::clamp(gross_g / model.capacity_g, 0.0, 1.0);
}

double display_g(const Balance &model, double gross_g, double tare_g, double share) {
	return rounded(gross_g + linearity_g(model, gross_g, share) - (tare_g + linearity_g(model, tare_g, share)), model.readability_g);
}

double marks_ml(double ml, double spacing_ml, double error, double share) {
	return rounded(ml * (1.0 + error * share), spacing_ml);
}

std::string check_I1(const std::vector<Read> &reads) {
	for (const Read &r : reads) {
		// within its error and half its resolution (and a hair of rounding), and a multiple of its resolution
		const double slack = 1e-9 * std::max(1.0, std::fabs(r.truth));
		const double off = std::fabs(r.reading - r.truth);
		const double steps = r.resolution > 0.0 ? r.reading / r.resolution : 0.0;
		char line[256];
		if (!(off <= r.error + r.resolution / 2.0 + slack)) {
			std::snprintf(line, sizeof line, "I1: %s reads %.10g, %.3g from its truth %.10g, past its error %.3g and half its resolution %.3g", r.what.c_str(), r.reading, off, r.truth, r.error,
					r.resolution / 2.0);
			return line;
		}
		if (r.resolution > 0.0 && std::fabs(steps - std::round(steps)) > 1e-6) {
			std::snprintf(line, sizeof line, "I1: %s reads %.10g, not a multiple of its resolution %.3g", r.what.c_str(), r.reading, r.resolution);
			return line;
		}
	}
	return "";
}

std::string display_text(const Balance &model, std::optional<double> display_g, bool overload) {
	if (!display_g) {
		return "";
	}
	if (overload) {
		return "OL";
	}
	const int decimals = std::max(0, static_cast<int>(std::lround(-std::log10(model.readability_g))));
	char text[64];
	// (a reading of -0 reads 0)
	std::snprintf(text, sizeof text, "%.*f g", decimals, std::fabs(*display_g) < 0.5 * model.readability_g ? 0.0 : *display_g);
	return text;
}

void plot_display(const std::string &text, Picture &p) {
	p.w = 96;
	p.h = 15;
	p.rgb.assign(static_cast<size_t>(p.w) * static_cast<size_t>(p.h) * 3, 0);
	static constexpr uint8_t LIT[3] = { 70, 235, 190 }, DIM[3] = { 5, 11, 9 };
	const auto bar = [&](int x0, int y0, int x1, int y1, const uint8_t *c) {
		for (int y = y0; y <= y1; ++y) {
			for (int x = x0; x <= x1; ++x) {
				if (x >= 0 && x < p.w && y >= 0 && y < p.h) {
					uint8_t *px = &p.rgb[(static_cast<size_t>(y) * static_cast<size_t>(p.w) + static_cast<size_t>(x)) * 3];
					px[0] = c[0];
					px[1] = c[1];
					px[2] = c[2];
				}
			}
		}
	};
	// segments a to g as bits 1 to 64
	const auto mask = [](char c) -> int {
		static constexpr int DIGITS[10] = { 63, 6, 91, 79, 102, 109, 125, 7, 127, 111 };
		return c >= '0' && c <= '9' ? DIGITS[c - '0'] : c == '-' ? 64 : c == 'O' ? 63 : c == 'L' ? 56 : c == 'g' ? 111 : 0;
	};
	const auto advance = [](char c) { return c == '.' ? 2 : c == ' ' ? 4 : c == 'g' ? 4 : 7; };
	int width = 0;
	for (const char c : text) {
		width += advance(c);
	}
	int x = p.w - 3 - width;
	const int y0 = 3;
	for (const char c : text) {
		if (c == '.') {
			bar(x - 1, y0 + 8, x - 1, y0 + 8, LIT);
		} else if (c == 'g') {
			// the unit: a small lowercase g, lower than the digits and dimmer, a legend beside them as a balance prints
			// it (in seven segments a g is a 9: "0.00 g" read as "0.009")
			// (a round bowl over a hooked tail, from the digits' middle to below their foot)
			static constexpr uint8_t ROWS[6] = { 3, 5, 5, 3, 1, 6 }, UNIT[3] = { 40, 150, 120 };
			for (int r = 0; r < 6; ++r) {
				for (int b = 0; b < 3; ++b) {
					if ((ROWS[r] >> (2 - b) & 1) != 0) {
						bar(x + b, y0 + 4 + r, x + b, y0 + 4 + r, UNIT);
					}
				}
			}
		} else if (c != ' ') {
			const int m = mask(c);
			const bool digit = true; // a digit's place shows its unlit segments faintly
			const auto seg = [&](int bit, int x0, int ya, int x1, int yb) {
				if ((m & bit) != 0) {
					bar(x0, ya, x1, yb, LIT);
				} else if (digit) {
					bar(x0, ya, x1, yb, DIM);
				}
			};
			seg(1, x + 1, y0, x + 3, y0);
			seg(2, x + 4, y0 + 1, x + 4, y0 + 3);
			seg(4, x + 4, y0 + 5, x + 4, y0 + 7);
			seg(8, x + 1, y0 + 8, x + 3, y0 + 8);
			seg(16, x, y0 + 5, x, y0 + 7);
			seg(32, x, y0 + 1, x, y0 + 3);
			seg(64, x + 1, y0 + 4, x + 3, y0 + 4);
		}
		x += advance(c);
	}
	uint64_t h = 1469598103934665603ull;
	for (const uint8_t b : p.rgb) {
		h = (h ^ b) * 1099511628211ull;
	}
	p.hash = h ^ 0x6261616c616e6365ull; // apart from a spectrum's picture of the same pixels
}

} // namespace lab::instruments
