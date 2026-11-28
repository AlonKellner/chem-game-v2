// A flame's light from what is in it (GAME_SPEC §12, Stage 82).
#include "lab/flame.hpp"

#include "lab/flame_data.hpp"

#include "chem/optics.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <map>

namespace lab::flame {

namespace {

constexpr double HC_K = 1.438776877; // second radiation constant, cm K
constexpr double REFERENCE_K = 2200.15; // the lab burner's flame, 1927 C: where a brightness is 1 for sodium

struct Data {
	std::map<std::string, std::vector<std::array<double, 4>>> lines; // nm, A, g, E (cm-1)
	std::map<std::string, std::vector<std::array<double, 2>>> bands; // nm, relative
	std::map<std::string, double> ground_g, band_light;
	std::map<std::string, std::string> band_of;
	double per_mol = 0.0, band_fwhm = 4.0, fade = 3.0, drop = 0.02;
};

const Data &data() {
	static const Data d = [] {
		Data o;
		const nlohmann::json j = nlohmann::json::parse(FLAME_JSON);
		for (const auto &[el, rows] : j.at("lines").items()) {
			for (const auto &r : rows) {
				o.lines[el].push_back({ r.at(0).get<double>(), r.at(1).get<double>(), r.at(2).get<double>(), r.at(3).get<double>() });
			}
		}
		for (const auto &[b, rows] : j.at("bands").items()) {
			for (const auto &r : rows) {
				o.bands[b].push_back({ r.at(0).get<double>(), r.at(1).get<double>() });
			}
		}
		const nlohmann::json &m = j.at("model");
		for (const auto &[el, g] : m.at("ground_g").items()) {
			o.ground_g[el] = g.get<double>();
		}
		for (const auto &[el, s] : m.at("band_light").items()) {
			o.band_light[el] = s.get<double>();
		}
		for (const auto &[el, b] : m.at("band_of").items()) {
			o.band_of[el] = b.get<std::string>();
		}
		o.per_mol = m.at("per_mol").get<double>();
		o.band_fwhm = m.at("band_fwhm_nm").get<double>();
		o.fade = m.at("fade_s").get<double>();
		o.drop = m.at("drop_ml").get<double>();
		return o;
	}();
	return d;
}

// Boltzmann's sum over an element's lines at T: sum of g_k A_ki exp(-hc E_k / kT) / lambda, over its ground level's g
double atom_sum(const std::string &el, double kelvin) {
	const Data &d = data();
	double sum = 0.0;
	for (const auto &l : d.lines.at(el)) {
		sum += l[2] * l[1] * std::exp(-HC_K * l[3] / kelvin) / l[0];
	}
	return sum / d.ground_g.at(el);
}

} // namespace

bool shows(const std::string &element) {
	return data().lines.count(element) != 0;
}

double fade_s() {
	return data().fade;
}

double drop_ml() {
	return data().drop;
}

// each of an element's molecule's bands' light at T, against sodium's atoms at the lab burner's 1927 C: its calibrated
// light there, times Boltzmann's factor for its photon's energy between there and T
std::vector<double> band_lights(const std::string &el, double kelvin) {
	const Data &d = data();
	std::vector<double> out;
	const auto light = d.band_light.find(el);
	if (light == d.band_light.end()) {
		return out;
	}
	const auto &bands = d.bands.at(d.band_of.at(el));
	double sum = 0.0;
	for (const auto &b : bands) {
		sum += b[1];
	}
	for (const auto &b : bands) {
		out.push_back(light->second * b[1] / sum * std::exp(-HC_K * (1.0e7 / b[0]) * (1.0 / kelvin - 1.0 / REFERENCE_K)));
	}
	return out;
}

std::vector<Part> parts(const std::string &el, double kelvin) {
	const Data &d = data();
	std::vector<Part> out;
	if (!shows(el) || !(kelvin > 0.0)) {
		return out;
	}
	const double total = brightness(el, kelvin), sodium = atom_sum("Na", REFERENCE_K);
	for (const auto &l : d.lines.at(el)) {
		out.push_back(Part{ l[0], 0.0, l[2] * l[1] * std::exp(-HC_K * l[3] / kelvin) / l[0] / d.ground_g.at(el) / sodium / total });
	}
	const std::vector<double> lights = band_lights(el, kelvin);
	for (size_t i = 0; i < lights.size(); ++i) {
		out.push_back(Part{ d.bands.at(d.band_of.at(el))[i][0], d.band_fwhm, lights[i] / total });
	}
	return out;
}

double brightness(const std::string &el, double kelvin) {
	if (!shows(el) || !(kelvin > 0.0)) {
		return 0.0;
	}
	// its atoms' light against sodium's at the lab burner's 1927 C, and its molecule's bands beside them
	double bands = 0.0;
	for (const double b : band_lights(el, kelvin)) {
		bands += b;
	}
	return atom_sum(el, kelvin) / atom_sum("Na", REFERENCE_K) + bands;
}

std::array<double, NM> glow(const std::vector<std::pair<std::string, double>> &elements_mol, double kelvin) {
	std::array<double, NM> out{};
	const Data &d = data();
	const chem::optics::Spectrum &room = chem::optics::d65();
	const double white = room[180]; // the room's light at 560 nm: the unit
	for (const auto &[el, mol] : elements_mol) {
		if (!(mol > 0.0) || !shows(el)) {
			continue;
		}
		const double light = d.per_mol * mol * brightness(el, kelvin); // multiples of the room's white times nm, a pixel of path
		for (const Part &p : parts(el, kelvin)) {
			const double energy = light * p.share;
			if (p.fwhm > 0.0) {
				// a band: a Gaussian sampled at each nanometre, its samples summing to its energy
				double sum = 0.0;
				std::array<double, NM> g{};
				for (int k = 0; k < NM; ++k) {
					const double u = (380.0 + k - p.nm) / p.fwhm;
					g[static_cast<size_t>(k)] = std::exp(-4.0 * std::log(2.0) * u * u);
					sum += g[static_cast<size_t>(k)];
				}
				for (int k = 0; sum > 0.0 && k < NM; ++k) {
					out[static_cast<size_t>(k)] += energy * g[static_cast<size_t>(k)] / sum;
				}
			} else {
				// a line: on the two nanometres about its centre, in shares that keep its whole and its centre
				const double x = p.nm - 380.0;
				const int k = static_cast<int>(std::floor(x));
				const double f = x - k;
				if (k >= 0 && k + 1 < NM) {
					out[static_cast<size_t>(k)] += energy * (1.0 - f);
					out[static_cast<size_t>(k + 1)] += energy * f;
				}
			}
		}
	}
	// as a multiple of the room's light at each nanometre
	for (int k = 0; k < NM; ++k) {
		out[static_cast<size_t>(k)] *= white / room[static_cast<size_t>(k)];
	}
	return out;
}

std::vector<std::pair<double, double>> spikes(const std::vector<std::pair<std::string, double>> &elements_mol, double kelvin, int most) {
	struct Run {
		double from, to, light;
	};
	std::vector<Run> runs;
	const Data &d = data();
	for (const auto &[el, mol] : elements_mol) {
		if (!(mol > 0.0) || !shows(el)) {
			continue;
		}
		const double light = d.per_mol * mol * brightness(el, kelvin);
		for (const Part &p : parts(el, kelvin)) {
			const double k = std::floor(p.nm);
			if (p.fwhm > 0.0 || !(p.share > 1e-3) || k < 381.0 || k + 2.0 > 780.0) {
				continue; // (a band is wide enough for the points; a line of under a thousandth of its element's light, left to them)
			}
			runs.push_back(Run{ k - 1.0, k + 2.0, light * p.share });
		}
	}
	std::sort(runs.begin(), runs.end(), [](const Run &a, const Run &b) { return a.from < b.from; });
	std::vector<Run> merged;
	for (const Run &r : runs) {
		if (!merged.empty() && r.from <= merged.back().to) {
			merged.back().to = std::max(merged.back().to, r.to);
			merged.back().light += r.light;
		} else {
			merged.push_back(r);
		}
	}
	std::sort(merged.begin(), merged.end(), [](const Run &a, const Run &b) { return a.light > b.light; });
	if (merged.size() > static_cast<size_t>(std::max(most, 0))) {
		merged.resize(static_cast<size_t>(std::max(most, 0)));
	}
	std::sort(merged.begin(), merged.end(), [](const Run &a, const Run &b) { return a.from < b.from; });
	std::vector<std::pair<double, double>> out;
	for (const Run &r : merged) {
		out.emplace_back(r.from, r.to);
	}
	return out;
}

} // namespace lab::flame
