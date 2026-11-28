// Discharge lamps (GAME_SPEC §12.5, Stage 85).
#include "lab/lamps.hpp"

#include "lab/lamps_data.hpp"

#include "chem/optics.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <map>

namespace lab::lamps {

namespace {

struct Data {
	std::vector<std::string> order;
	std::map<std::string, std::vector<std::array<double, 2>>> lines;
	double light = 0.0;
};

const Data &data() {
	static const Data d = [] {
		Data o;
		const nlohmann::json j = nlohmann::json::parse(LAMPS_JSON);
		for (const auto &[gas, rows] : j.at("lines").items()) {
			o.order.push_back(gas);
			for (const auto &r : rows) {
				o.lines[gas].push_back({ r.at(0).get<double>(), r.at(1).get<double>() });
			}
		}
		o.light = j.at("model").at("light").get<double>();
		return o;
	}();
	return d;
}

} // namespace

bool has(const std::string &gas) {
	return data().lines.count(gas) != 0;
}

std::vector<std::string> gases() {
	return data().order;
}

const std::vector<std::array<double, 2>> &lines(const std::string &gas) {
	static const std::vector<std::array<double, 2>> none;
	const auto found = data().lines.find(gas);
	return found == data().lines.end() ? none : found->second;
}

std::array<double, 401> glow(const std::string &gas, double power) {
	std::array<double, 401> out{};
	const std::vector<std::array<double, 2>> &ls = lines(gas);
	double sum = 0.0;
	for (const auto &l : ls) {
		sum += l[1];
	}
	if (!(sum > 0.0) || !(power > 0.0)) {
		return out;
	}
	const chem::optics::Spectrum &room = chem::optics::d65();
	const double white = room[180];
	for (const auto &l : ls) {
		// on the two nanometres about its centre, in shares that keep its whole and its centre (as a flame's lines)
		const double energy = data().light * power * l[1] / sum, x = l[0] - 380.0;
		const int k = static_cast<int>(std::floor(x));
		const double f = x - k;
		if (k >= 0 && k + 1 < 401) {
			out[static_cast<size_t>(k)] += energy * (1.0 - f);
			out[static_cast<size_t>(k + 1)] += energy * f;
		}
	}
	for (size_t k = 0; k < out.size(); ++k) {
		out[k] *= white / room[k];
	}
	return out;
}

std::vector<std::pair<double, double>> spikes(const std::string &gas, int most) {
	struct Run {
		double from, to, light;
	};
	std::vector<Run> runs;
	for (const auto &l : lines(gas)) {
		const double k = std::floor(l[0]);
		if (k >= 381.0 && k + 2.0 <= 780.0) {
			runs.push_back(Run{ k - 1.0, k + 2.0, l[1] });
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

} // namespace lab::lamps
