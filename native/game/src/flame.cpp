// A flame's light from what is in it (GAME_SPEC §12, Stage 82).
#include "lab/flame.hpp"

#include "lab/flame_data.hpp"

#include "chem/optics.hpp"

#include <nlohmann/json.hpp>

#include <cmath>
#include <map>

namespace lab::flame {

namespace {

constexpr double HC_K = 1.438776877; // second radiation constant, cm K

struct Data {
	std::map<std::string, std::vector<std::array<double, 4>>> lines; // nm, A, g, E (cm-1)
	std::map<std::string, std::vector<std::array<double, 2>>> bands; // nm, relative
	std::map<std::string, double> ground_g, band_share;
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
		for (const auto &[el, s] : m.at("band_share").items()) {
			o.band_share[el] = s.get<double>();
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

std::vector<Part> parts(const std::string &el, double kelvin) {
	const Data &d = data();
	std::vector<Part> out;
	if (!shows(el) || !(kelvin > 0.0)) {
		return out;
	}
	const double total = atom_sum(el, kelvin);
	const auto share = d.band_share.find(el);
	const double banded = share == d.band_share.end() ? 0.0 : share->second;
	for (const auto &l : d.lines.at(el)) {
		out.push_back(Part{ l[0], 0.0, (1.0 - banded) * l[2] * l[1] * std::exp(-HC_K * l[3] / kelvin) / l[0] / d.ground_g.at(el) / total });
	}
	if (banded > 0.0) {
		const auto &bands = d.bands.at(d.band_of.at(el));
		double sum = 0.0;
		for (const auto &b : bands) {
			sum += b[1];
		}
		for (const auto &b : bands) {
			out.push_back(Part{ b[0], d.band_fwhm, banded * b[1] / sum });
		}
	}
	return out;
}

double brightness(const std::string &el, double kelvin) {
	const Data &d = data();
	if (!shows(el)) {
		return 0.0;
	}
	const auto share = d.band_share.find(el);
	const double banded = share == d.band_share.end() ? 0.0 : share->second;
	// its atoms' light against sodium's at the lab burner's 1927 C, and its bands beside them
	return atom_sum(el, kelvin) / atom_sum("Na", 2200.15) / (1.0 - banded);
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

} // namespace lab::flame
