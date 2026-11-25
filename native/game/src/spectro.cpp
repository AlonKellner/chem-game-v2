// A spectrophotometer (GAME_SPEC §13, Stage 55).
#include "lab/spectro.hpp"
#include "lab/instruments_data.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace lab::spectro {

namespace optics = chem::optics;

const Model &model() {
	static const Model m = [] {
		Model o;
		const nlohmann::json j = nlohmann::json::parse(instruments::INSTRUMENTS_JSON).at("spectrophotometer");
		o.first_nm = j.at("range_nm").at(0).get<double>();
		o.last_nm = j.at("range_nm").at(1).get<double>();
		o.band_nm = j.at("band_nm").get<double>();
		o.stray = j.at("stray_light").get<double>();
		o.wavelength_error_nm = j.at("wavelength_accuracy_nm").get<double>();
		o.resolution = j.at("resolution").get<double>();
		o.most = j.at("most").get<double>();
		o.lamp_k = j.at("lamp_k").get<double>();
		o.beam_mm = j.at("beam_mm").get<double>();
		const nlohmann::json &h = j.at("holder");
		o.holder_least_c = h.at("range_c").at(0).get<double>();
		o.holder_most_c = h.at("range_c").at(1).get<double>();
		o.holder_gain_w_k = h.at("gain_w_k").get<double>();
		o.holder_most_w = h.at("most_w").get<double>();
		return o;
	}();
	return m;
}

double turbidity(double grains_per_cm3, double diameter_cm) {
	constexpr double EXTINCTION = 2.0; // a large grain's efficiency: what it blocks and what it diffracts
	const double pi = 3.14159265358979323846;
	return EXTINCTION * pi * 0.25 * diameter_cm * diameter_cm * grains_per_cm3 / std::log(10.0);
}

optics::Spectrum absorbance(const Sample &sample) {
	optics::Spectrum a{};
	a.fill(sample.turbidity * sample.path_cm);
	for (const auto &[id, c] : sample.molar) {
		if (optics::kind(id) != optics::Kind::Absorb || !(c > 0.0)) {
			continue;
		}
		const optics::Spectrum &e = optics::spectrum(id);
		for (int i = 0; i < optics::COUNT; ++i) {
			a[static_cast<size_t>(i)] += e[static_cast<size_t>(i)] * c * sample.path_cm;
		}
	}
	return a;
}

double absorbance_at(const Sample &sample, const optics::Spectrum &a, double nm) {
	const double x = std::clamp(nm - optics::FIRST_NM, 0.0, static_cast<double>(optics::COUNT - 1));
	const int i = std::min(static_cast<int>(x), optics::COUNT - 2);
	const double f = x - i;
	double out = a[static_cast<size_t>(i)] * (1.0 - f) + a[static_cast<size_t>(i) + 1] * f;
	for (const auto &[id, c] : sample.molar) {
		if (optics::kind(id) == optics::Kind::Absorb && c > 0.0) {
			for (const optics::Line &l : optics::lines(id)) {
				out += optics::line_at(l, nm) * c * sample.path_cm;
			}
		}
	}
	return out;
}

namespace {

constexpr double STEP_NM = 0.1; // the slit's band, integrated

// the light through the slit's band at `nm` (lamp times the sample's transmittance, under the band's triangle)
double band(const Model &m, double nm, const Sample &sample, const optics::Spectrum &a, bool through) {
	double sum = 0.0;
	const int n = static_cast<int>(std::lround(m.band_nm / STEP_NM));
	for (int k = -n; k <= n; ++k) {
		const double at = nm + k * STEP_NM, weight = 1.0 - std::fabs(static_cast<double>(k)) / n;
		if (weight <= 0.0) {
			continue;
		}
		const double t = through ? std::pow(10.0, -absorbance_at(sample, a, at)) : 1.0;
		sum += weight * optics::planck(at, m.lamp_k) * t;
	}
	return sum * STEP_NM;
}

// the lamp's whole visible spectrum through the sample, as a share of the lamp's (the stray light's colour)
double whole_share(const Model &m, const optics::Spectrum &a) {
	double through = 0.0, all = 0.0;
	for (int i = 0; i < optics::COUNT; ++i) {
		const double lamp = optics::planck(optics::FIRST_NM + i, m.lamp_k);
		through += lamp * std::pow(10.0, -a[static_cast<size_t>(i)]);
		all += lamp;
	}
	return all > 0.0 ? through / all : 0.0;
}

} // namespace

double signal(const Model &m, double nm, const Sample &sample) {
	if (sample.opaque) {
		return 0.0;
	}
	const optics::Spectrum a = absorbance(sample);
	// stray light: a share of the band's own light in the blank, of every colour the lamp gives, through the sample
	return band(m, nm, sample, a, true) + m.stray * band(m, nm, sample, a, false) * whole_share(m, a);
}

std::optional<double> reading(const Model &m, double sample_signal, double blank_signal) {
	if (!(sample_signal > 0.0) || !(blank_signal > 0.0)) {
		return std::nullopt;
	}
	const double a = -std::log10(sample_signal / blank_signal);
	if (a > m.most) {
		return std::nullopt;
	}
	const double r = std::round(a / m.resolution) * m.resolution;
	return std::fabs(r) < 0.5 * m.resolution ? 0.0 : r;
}

std::string display_text(std::optional<double> absorbance, bool blanked, bool over) {
	if (!blanked) {
		return "----";
	}
	if (over || !absorbance) {
		return "OL";
	}
	char text[32];
	std::snprintf(text, sizeof text, "%.3f", *absorbance);
	return text;
}

std::string wavelength_text(double nm) {
	char text[32];
	std::snprintf(text, sizeof text, "%.0f", nm);
	return text;
}

} // namespace lab::spectro
