// A vessel's wall as rings (GAME_SPEC §6, Stage 53): lab/walls.hpp.
#include "lab/walls.hpp"

#include "chem/species.hpp"
#include "chem/thermo.hpp"
#include "lab/walls_data.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>

namespace lab::walls {

namespace {

constexpr double PI = 3.14159265358979323846;
constexpr double GRAVITY = 9.80665; // m/s^2, standard
constexpr int SAMPLES = 16; // a ring's integrals, at the middles of as many slices

constexpr double SIGMA = 5.670374419e-8; // W/m^2 K^4, Stefan and Boltzmann's (CODATA 2018, exact from h, c, k)

struct Data {
	double height_cm = 0.5;
	int least = 4, most = 24;
	std::vector<std::pair<double, Air>> air; // by kelvin
	std::vector<std::pair<std::string, std::vector<std::pair<double, double>>>> emittance; // by wall, by kelvin
};

const Data &data() {
	static const Data loaded = [] {
		Data d;
		const nlohmann::json j = nlohmann::json::parse(WALLS_JSON);
		const nlohmann::json &r = j.at("rings");
		d.height_cm = r.at("height_mm").get<double>() / 10.0;
		d.least = r.at("least").get<int>();
		d.most = r.at("most").get<int>();
		for (const nlohmann::json &row : j.at("air")) {
			d.air.emplace_back(row.at("kelvin").get<double>(), Air{ row.at("k").get<double>(), row.at("nu").get<double>(), row.at("alpha").get<double>(), row.at("pr").get<double>() });
		}
		for (const auto &[wall, rows] : j.at("emittance").items()) {
			std::vector<std::pair<double, double>> table;
			for (const nlohmann::json &row : rows) {
				table.emplace_back(row.at("kelvin").get<double>(), row.at("e").get<double>());
			}
			d.emittance.emplace_back(wall, std::move(table));
		}
		return d;
	}();
	return loaded;
}

// the integrals over [from, to] of pi (r_o^2 - r_i^2), 2 pi r_i and 2 pi r_o, cm^3 and cm^2
struct Integrals {
	double glass = 0.0, inside = 0.0, outside = 0.0;
};

Integrals integrate(const glass::Vessel &v, double from, double to) {
	Integrals out;
	const double dy = (to - from) / SAMPLES;
	for (int k = 0; k < SAMPLES; ++k) {
		const double y = from + (k + 0.5) * dy, ro = v.radius_outside(y), ri = v.radius_inside(y);
		out.glass += PI * std::max(ro * ro - ri * ri, 0.0) * dy;
		out.inside += 2.0 * PI * ri * dy;
		out.outside += 2.0 * PI * ro * dy;
	}
	return out;
}

} // namespace

std::vector<Ring> rings_of(const glass::Vessel &v) {
	const Data &d = data();
	std::vector<Ring> out;
	const double nudge = 1e-6 * v.height;
	// the floor: the glass under the inside's floor, its inside the floor's disc, its outside its base and its side
	{
		Ring f;
		f.floor = true;
		f.from = 0.0;
		f.to = v.floor;
		const Integrals i = integrate(v, 0.0, v.floor);
		const double ri = v.radius_inside(v.floor + nudge), ro = v.radius_outside(v.floor + nudge), base = v.radius_outside(nudge);
		f.glass_cm3 = i.glass;
		f.inside_cm2 = PI * ri * ri;
		f.outside_cm2 = PI * base * base + i.outside;
		f.across_cm2 = PI * std::max(ro * ro - ri * ri, 0.0);
		f.radius = ri;
		f.thickness_cm = v.floor;
		f.base_cm2 = PI * base * base;
		out.push_back(f);
	}
	const double span = v.height - v.floor;
	const int n = std::clamp(static_cast<int>(std::lround(span / d.height_cm)), d.least, d.most);
	for (int k = 0; k < n; ++k) {
		Ring r;
		r.from = v.floor + span * k / n;
		r.to = v.floor + span * (k + 1) / n;
		const Integrals i = integrate(v, r.from, r.to);
		r.glass_cm3 = i.glass;
		r.inside_cm2 = i.inside;
		r.outside_cm2 = i.outside;
		const double ro = v.radius_outside(r.to - nudge), ri = v.radius_inside(r.to - nudge);
		r.across_cm2 = PI * std::max(ro * ro - ri * ri, 0.0);
		r.radius = v.radius_inside(0.5 * (r.from + r.to));
		const double middle = 0.5 * (r.from + r.to);
		r.thickness_cm = std::max(v.radius_outside(middle) - v.radius_inside(middle), 0.0);
		out.push_back(r);
	}
	return out;
}

double inside_between(const glass::Vessel &v, double from, double to) {
	return to > from ? integrate(v, from, to).inside : 0.0;
}

double nusselt(double rayleigh, double prandtl) {
	const double ra = std::max(rayleigh, 0.0);
	const double shape = 1.0 + std::pow(0.492 / prandtl, 9.0 / 16.0);
	if (ra <= 1e9) {
		return 0.68 + 0.670 * std::pow(ra, 0.25) * std::pow(shape, -4.0 / 9.0); // (8.13a)
	}
	const double root = 0.825 + 0.387 * std::pow(ra, 1.0 / 6.0) / std::pow(shape, 8.0 / 27.0); // (8.13b)
	return root * root;
}

Air air_at(double kelvin) {
	const auto &rows = data().air;
	if (kelvin <= rows.front().first) {
		return rows.front().second;
	}
	if (kelvin >= rows.back().first) {
		return rows.back().second;
	}
	size_t k = 0;
	while (k + 2 < rows.size() && rows[k + 1].first < kelvin) {
		++k;
	}
	const double f = (kelvin - rows[k].first) / (rows[k + 1].first - rows[k].first);
	const Air &a = rows[k].second, &b = rows[k + 1].second;
	return Air{ a.k + (b.k - a.k) * f, a.nu + (b.nu - a.nu) * f, a.alpha + (b.alpha - a.alpha) * f, a.pr + (b.pr - a.pr) * f };
}

double into_water(double height_m, double wall_k, double fluid_k) {
	if (!(height_m > 0.0)) {
		return 0.0;
	}
	const chem::SolventPoint w = chem::solvent_at(chem::builtin_species(), 0.5 * (wall_k + fluid_k));
	if (!(w.k > 0.0) || !(w.nu > 0.0) || !(w.alpha > 0.0)) {
		return 0.0;
	}
	const double ra = GRAVITY * std::fabs(w.beta) * std::fabs(wall_k - fluid_k) * height_m * height_m * height_m / (w.nu * w.alpha);
	return nusselt(ra, w.pr) * w.k / height_m;
}

double into_air(double height_m, double wall_k, double fluid_k) {
	if (!(height_m > 0.0)) {
		return 0.0;
	}
	const double film = 0.5 * (wall_k + fluid_k);
	const Air a = air_at(film);
	const double ra = GRAVITY / film * std::fabs(wall_k - fluid_k) * height_m * height_m * height_m / (a.nu * a.alpha); // (an ideal gas: beta = 1 / T)
	return nusselt(ra, a.pr) * a.k / height_m;
}

double under_disc(double diameter_m, double wall_k, double air_k) {
	if (!(diameter_m > 0.0) || !(wall_k > 0.0) || !(air_k > 0.0)) {
		return 0.0;
	}
	const double film = 0.5 * (wall_k + air_k);
	const Air a = air_at(film);
	const double g = GRAVITY / film * std::fabs(wall_k - air_k) / (a.nu * a.alpha); // (an ideal gas: beta = 1 / T)
	if (wall_k >= air_k) { // warmer, facing down: stable (8.39)
		const double ra = g * diameter_m * diameter_m * diameter_m;
		return 0.82 * std::pow(ra, 0.2) * std::pow(a.pr, 0.034) * a.k / diameter_m;
	}
	const double l = 0.25 * diameter_m, ra = g * l * l * l; // colder, facing down: unstable, over L* = A/P
	return unstable_plate(ra, a.pr) * a.k / l;
}

double unstable_plate(double ra, double pr) {
	if (!(ra > 0.0)) {
		return 0.0;
	}
	if (ra >= 1e7) {
		return 0.14 * std::cbrt(ra) * (1.0 + 0.0107 * pr) / (1.0 + 0.01 * pr); // (8.35)
	}
	const double nu = 0.560 * std::pow(ra, 0.25) / std::pow(1.0 + std::pow(0.492 / pr, 9.0 / 16.0), 4.0 / 9.0); // (8.37a)
	return 1.4 / std::log(1.0 + 1.4 / nu); // (8.37b: the boundary layers' thickness, at every Nu: Stage 73)
}

double emittance(std::string_view wall, double kelvin) {
	for (const auto &[name, rows] : data().emittance) {
		if (name != wall || rows.empty()) {
			continue;
		}
		if (kelvin <= rows.front().first) {
			return rows.front().second;
		}
		if (kelvin >= rows.back().first) {
			return rows.back().second;
		}
		size_t k = 0;
		while (k + 2 < rows.size() && rows[k + 1].first < kelvin) {
			++k;
		}
		const double f = (kelvin - rows[k].first) / (rows[k + 1].first - rows[k].first);
		return rows[k].second + (rows[k + 1].second - rows[k].second) * f;
	}
	return 0.0;
}

double radiation(double e, double wall_k, double room_k) {
	return e * SIGMA * (wall_k * wall_k + room_k * room_k) * (wall_k + room_k);
}

double bed_conductivity(double fluid, double solid, double porosity, double shape) {
	if (!(fluid > 0.0) || !(porosity > 0.0 && porosity < 1.0)) {
		return 0.0;
	}
	if (!(solid > fluid)) {
		return fluid;
	}
	const double lambda = fluid / solid; // the fluid's over the solid's (Kandula's lambda)
	const double b = shape * std::pow((1.0 - porosity) / porosity, 10.0 / 9.0);
	const double n = 1.0 - lambda * b, root = std::sqrt(1.0 - porosity);
	const double core = (1.0 - lambda) * b / (n * n) * std::log(1.0 / (lambda * b)) - 0.5 * (b + 1.0) - (b - 1.0) / n;
	return fluid * (1.0 - root + 2.0 * root / n * core);
}

} // namespace lab::walls
