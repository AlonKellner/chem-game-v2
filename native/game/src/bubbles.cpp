// Boiling's bubbles (GAME_SPEC §11, Stage 45): lab/bubbles.hpp.
#include "lab/bubbles.hpp"

#include "lab/boiling_data.hpp"

#include "chem/builtin_ids.hpp"
#include "chem/optics.hpp"
#include "chem/species.hpp"
#include "chem/thermo.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <map>
#include <vector>

namespace lab::bubbles {

namespace {

constexpr double G = 9.80665; // m/s^2
constexpr double PI = 3.14159265358979323846;
constexpr double KELVIN = 273.15;

struct Data {
	double b = 0.0, mu = 0.0, b2 = 0.0, tc = 0.0;
	std::map<std::string, Surface, std::less<>> surfaces;
};

const Data &data() {
	static const Data d = [] {
		Data out;
		const nlohmann::json j = nlohmann::json::parse(BOILING_JSON);
		const nlohmann::json &s = j.at("surface_tension");
		out.b = s.at("b_mn_m").get<double>() * 1e-3;
		out.mu = s.at("mu").get<double>();
		out.b2 = s.at("b2").get<double>();
		out.tc = s.at("tc_k").get<double>();
		for (const auto &[name, v] : j.at("surfaces").items()) {
			out.surfaces[name] = Surface{ v.at("cavity_um").get<double>() * 1e-6, v.at("contact_deg").get<double>(), v.at("csf").get<double>(), v.at("s").get<double>() };
		}
		return out;
	}();
	return d;
}

const chem::SpeciesDef &liquid_water() {
	return chem::builtin_species().species(chem::builtin::water);
}
const chem::SpeciesDef &steam() {
	return chem::builtin_species().species(chem::builtin::steam);
}
double molar_mass() { // kg/mol
	return static_cast<double>(chem::builtin_species().molar_mass_ug(chem::builtin::water)) * 1e-9;
}

// the latent heat the table gives water at a temperature, J/kg
double latent(double kelvin) {
	return ((steam().hf - liquid_water().hf) + (steam().cp - liquid_water().cp) * (kelvin - chem::T0)) / molar_mass();
}

// a temperature where f rises through zero, between lo and hi
template <typename F>
double root(F f, double lo, double hi) {
	for (int i = 0; i < 60; ++i) { // a 400 K range to 4e-16 of it
		const double middle = 0.5 * (lo + hi);
		(f(middle) < 0.0 ? lo : hi) = middle;
	}
	return 0.5 * (lo + hi);
}

} // namespace

Water water_at(double celsius) {
	// the core's own (data/solutions.json's solvent: Lienhard's Table A.3; Stage 47)
	const chem::SolventPoint p = chem::solvent_at(chem::builtin_species(), celsius + KELVIN);
	return Water{ p.rho, p.cp, p.k, p.alpha, p.nu, p.pr, p.beta };
}

// bubbles as the optics has them (data/optics/media.json: scatterers)
const chem::optics::Scatterer &medium() {
	static const chem::optics::Scatterer *const s = chem::optics::scatterer("bubbles");
	return *s;
}

double asymmetry() {
	return medium().asymmetry;
}

double efficiency() {
	return medium().efficiency;
}

double surface_tension(double kelvin) {
	const Data &d = data();
	const double t = std::max(1.0 - kelvin / d.tc, 0.0);
	return d.b * std::pow(t, d.mu) * (1.0 + d.b2 * t);
}

const Surface &surface(std::string_view name) {
	const auto &s = data().surfaces;
	const auto found = s.find(name);
	return found != s.end() ? found->second : s.at("glass");
}

double convection(double floor_m2, double perimeter_m, double floor_c, double bulk_c) {
	const double dt = floor_c - bulk_c;
	if (!(dt > 0.0) || !(floor_m2 > 0.0) || !(perimeter_m > 0.0)) {
		return 0.0;
	}
	const Water w = water_at(0.5 * (floor_c + bulk_c));
	const double length = floor_m2 / perimeter_m;
	const double ra = G * w.beta * dt * length * length * length / (w.nu * w.alpha);
	double nu = 0.0;
	if (ra < 1e7) {
		// eq. 8.37a, and 8.37b where the boundary layers are thick (Nu under 10)
		const double nu_l = 0.560 * std::pow(std::max(ra, 0.0), 0.25) / std::pow(1.0 + std::pow(0.492 / w.pr, 9.0 / 16.0), 4.0 / 9.0);
		nu = nu_l < 10.0 ? (nu_l > 0.0 ? 1.4 / std::log(1.0 + 1.4 / nu_l) : 0.0) : nu_l;
	} else {
		nu = 0.14 * std::cbrt(ra) * (1.0 + 0.0107 * w.pr) / (1.0 + 0.01 * w.pr); // eq. 8.35
	}
	return nu * w.k / length;
}

double rohsenow(double superheat, const Water &liquid, double sigma, double h_fg, double rho_v, const Surface &surface) {
	if (!(superheat > 0.0)) {
		return 0.0;
	}
	// c_p dT / (h_fg Pr^s) = C_sf [q / (mu h_fg) sqrt(sigma / g drho)]^0.33
	const double mu = liquid.nu * liquid.rho;
	const double left = liquid.cp * superheat / (h_fg * std::pow(liquid.pr, surface.s)) / surface.csf;
	return std::pow(left, 1.0 / 0.33) * mu * h_fg / std::sqrt(sigma / (G * (liquid.rho - rho_v)));
}

double departure(double contact_deg, double sigma, double drho) {
	return 0.0208 * contact_deg * std::sqrt(sigma / (G * drho));
}

double rise(double d, const Water &liquid, double sigma) {
	if (!(d > 0.0)) {
		return 0.0;
	}
	const double mu = liquid.nu * liquid.rho;
	// eq. 2, the potential flow's; eq. 6, with the viscous correction; eq. 8, Lehrer's wave analogy; eq. 1
	const double pot = liquid.rho * G * d * d / (36.0 * mu);
	const double viscous = pot * std::sqrt(1.0 + 0.73667 * std::sqrt(G * d) / pot);
	const double tension = std::sqrt(3.0 * sigma / (liquid.rho * d) + G * d / 2.0);
	return 1.0 / std::sqrt(1.0 / (viscous * viscous) + 1.0 / (tension * tension));
}

double condensing(double d, double u, const Water &liquid) {
	if (!(d > 0.0)) {
		return 0.0;
	}
	const double re = u * d / liquid.nu;
	return liquid.k / d * 0.185 * std::pow(re, 0.7) * std::sqrt(liquid.pr);
}

double saturation_pressure(double kelvin) {
	const double dh = (steam().hf - liquid_water().hf) + (steam().cp - liquid_water().cp) * (kelvin - chem::T0);
	const double ds = (steam().s0 - liquid_water().s0) + (steam().cp - liquid_water().cp) * std::log(kelvin / chem::T0);
	return chem::P_STANDARD * std::exp(-(dh - kelvin * ds) / (chem::GAS_CONSTANT * kelvin));
}

double boiling_point(double pressure_pa, double water_activity) {
	const double a = water_activity > 0.0 ? water_activity : 1.0;
	return root([&](double t) { return a * saturation_pressure(t) - pressure_pa; }, 250.0, 650.0);
}

double Bubbles::extinction_at(double z_m) const {
	double out = gas_extinction;
	if (extinction_floor > 0.0 && end_m > 0.0 && z_m >= 0.0 && z_m < end_m) {
		out += extinction_floor * std::pow(1.0 - z_m / end_m, exponent);
	}
	return out;
}

double floor_flux(const Floor &f, double floor_c) {
	return floor_through(f, floor_c, 0.0).heat_w;
}

Through floor_through(const Floor &f, double middle_c, double resistance) {
	Through out;
	out.surface_c = middle_c;
	if (!(f.depth_m > 0.0) || !(f.floor_m2 > 0.0) || !(middle_c > f.bulk_c)) {
		return out;
	}
	const Water bulk = water_at(f.bulk_c);
	const double floor_pa = f.pressure_pa + bulk.rho * G * f.depth_m;
	const double a_w = f.water_activity > 0.0 ? f.water_activity : 1.0;
	// what the floor's surface at a temperature passes: convection, and past the onset Rohsenow's boiling (the onset
	// found once: no surface warmer than the glass's middle, and under its boiling point there none boils)
	double onset_k = 1e300, floor_k = 0.0;
	const Surface &surf = surface(f.surface);
	if (a_w * saturation_pressure(middle_c + KELVIN) > floor_pa) {
		floor_k = boiling_point(floor_pa, f.water_activity);
		onset_k = f.nucleating ? floor_k : root([&](double t) { return a_w * saturation_pressure(t) - floor_pa - 2.0 * surface_tension(t) / surf.cavity; }, floor_k, floor_k + 150.0);
	}
	const auto flux = [&](double t_c) {
		double q = convection(f.floor_m2, f.perimeter_m, t_c, f.bulk_c) * f.floor_m2 * (t_c - f.bulk_c);
		if (t_c + KELVIN > onset_k) {
			const double sat_c = floor_k - KELVIN, sigma = surface_tension(floor_k);
			const double h_fg = latent(floor_k), rho_v = floor_pa * molar_mass() / (chem::GAS_CONSTANT * floor_k);
			q += f.floor_m2 * rohsenow(t_c - sat_c, water_at(sat_c), sigma, h_fg, rho_v, surf);
		}
		return q;
	};
	if (!(resistance > 0.0)) {
		out.heat_w = flux(middle_c);
		return out;
	}
	// through half the glass in series: the surface where what crosses the glass is what the surface passes on (the
	// one between the bulk and the middle: the flux grows with the surface's temperature, what crosses falls)
	double lo = f.bulk_c, hi = middle_c;
	for (int k = 0; k < 60 && hi - lo > 1e-9; ++k) {
		const double mid = 0.5 * (lo + hi);
		(flux(mid) > (middle_c - mid) / resistance ? hi : lo) = mid;
	}
	out.surface_c = 0.5 * (lo + hi);
	out.heat_w = (middle_c - out.surface_c) / resistance;
	return out;
}

bool floor_nucleates(const Floor &f, double floor_c) {
	if (!(f.depth_m > 0.0)) {
		return false;
	}
	const double floor_pa = f.pressure_pa + water_at(f.bulk_c).rho * G * f.depth_m;
	const double a_w = f.water_activity > 0.0 ? f.water_activity : 1.0;
	if (a_w * saturation_pressure(floor_c + KELVIN) <= floor_pa) {
		return false; // under its boiling point: its nuclei condense
	}
	if (f.nucleating) {
		return true;
	}
	const double floor_k = boiling_point(floor_pa, f.water_activity);
	const Surface &surf = surface(f.surface);
	return floor_c + KELVIN > root([&](double t) { return a_w * saturation_pressure(t) - floor_pa - 2.0 * surface_tension(t) / surf.cavity; }, floor_k, floor_k + 150.0);
}

Bubbles boil(const Floor &f) {
	Bubbles b;
	b.floor_c = f.bulk_c;
	if (!(f.depth_m > 0.0) || !(f.floor_m2 > 0.0) || (!(f.heat_w > 0.0) && !(f.gas_m3_s > 0.0))) {
		return b; // (no heat through its floor and no gas: nothing to work out, every frame, for every vessel)
	}
	const double bulk_k = f.bulk_c + KELVIN;
	const Water bulk = water_at(f.bulk_c);
	const double floor_pa = f.pressure_pa + bulk.rho * G * f.depth_m;
	const double top_k = boiling_point(f.pressure_pa, f.water_activity), floor_k = boiling_point(floor_pa, f.water_activity);
	b.saturation_top_c = top_k - KELVIN;
	b.saturation_floor_c = floor_k - KELVIN;
	const Surface &surf = surface(f.surface);
	const double a_w = f.water_activity > 0.0 ? f.water_activity : 1.0;
	// the onset: the superheat at which the floor's largest cavity holds a nucleus, 2 sigma / r = p_v - p
	const double onset_k = root([&](double t) { return a_w * saturation_pressure(t) - floor_pa - 2.0 * surface_tension(t) / surf.cavity; }, floor_k, floor_k + 150.0);
	b.onset_c = onset_k - KELVIN;
	// a reaction's gas leaves from the floor in bubbles of its own, rising to the surface (it does not condense)
	if (f.gas_m3_s > 0.0) {
		const double sigma = surface_tension(bulk_k), d = departure(surf.contact_deg, sigma, bulk.rho);
		const double u = rise(d, bulk, sigma), n = f.gas_m3_s / (PI * d * d * d / 6.0) / (u * f.floor_m2);
		b.gas_diameter_m = d;
		b.gas_extinction = efficiency() * n * PI * d * d / 4.0;
		b.gas_holdup = std::min(n * PI * d * d * d / 6.0, 0.99);
	}
	if (!(f.heat_w > 0.0)) {
		return b;
	}
	const auto carried = [&](double t_c) { return convection(f.floor_m2, f.perimeter_m, t_c, f.bulk_c) * f.floor_m2 * (t_c - f.bulk_c); };
	// (Stage 53) a nucleating floor boils from its boiling point: past it the bubbles take their share
	const double floor_lo = f.bulk_c, onset = f.nucleating ? std::max(b.saturation_floor_c, f.bulk_c) : b.onset_c;
	if (f.heat_w <= carried(onset)) {
		// convection alone takes it: the floor stands where it carries the heat, and nothing boils
		b.floor_c = root([&](double t) { return carried(t) - f.heat_w; }, floor_lo, onset);
		return b;
	}
	// boiling: the floor's heat split between convection and the bubbles (Rohsenow's superheat over the floor's
	// boiling point, the liquid's properties there)
	const double sat_c = b.saturation_floor_c, sigma = surface_tension(floor_k);
	const Water at_sat = water_at(sat_c);
	const double h_fg = latent(floor_k), rho_v = floor_pa * molar_mass() / (chem::GAS_CONSTANT * floor_k);
	const auto boiled = [&](double t_c) { return f.floor_m2 * rohsenow(t_c - sat_c, at_sat, sigma, h_fg, rho_v, surf); };
	b.floor_c = root([&](double t) { return carried(t) + boiled(t) - f.heat_w; }, std::max(floor_lo, sat_c), onset + 200.0);
	b.boiling_w = std::min(boiled(b.floor_c), f.heat_w);
	if (!(b.boiling_w > 0.0)) {
		return b;
	}
	b.boiling = true;
	const double d0 = departure(surf.contact_deg, sigma, at_sat.rho - rho_v);
	b.diameter_m = d0;
	b.per_second = b.boiling_w / h_fg / rho_v / (PI * d0 * d0 * d0 / 6.0);
	// up the column: each bubble rises at its speed and, where the bulk is under its boiling point at that
	// height, gives it the latent heat of what condenses (Chen and Mayinger), shrinking
	const double h = f.depth_m, sub_floor = sat_c - f.bulk_c, sub_top = b.saturation_top_c - f.bulk_c;
	double z = 0.0, d = d0, area = 0.0; // area: the extinction's integral up the column
	const auto extinction = [&](double dd, double u) { return efficiency() * (b.per_second / (u * f.floor_m2)) * PI * dd * dd / 4.0; };
	const double u0 = rise(d0, bulk, sigma);
	b.extinction_floor = extinction(d0, u0);
	b.holdup_floor = std::min(b.per_second / (u0 * f.floor_m2) * PI * d0 * d0 * d0 / 6.0, 0.99);
	double previous = b.extinction_floor;
	for (int i = 0; i < 4000 && z < h; ++i) {
		const double u = rise(d, bulk, sigma);
		const double sub = sub_floor + (sub_top - sub_floor) * (z / h);
		double dz = h / 400.0;
		if (sub > 0.0) {
			const double shrink = 2.0 * condensing(d, u, bulk) * sub / (rho_v * h_fg); // m/s of diameter
			dz = std::min(dz, 0.02 * d0 * u / shrink);
			d -= shrink * dz / u;
		}
		z = std::min(z + dz, h);
		if (d <= 0.05 * d0) {
			area += 0.5 * previous * dz;
			b.end_m = z;
			break;
		}
		const double now = extinction(d, rise(d, bulk, sigma));
		area += 0.5 * (previous + now) * dz;
		previous = now;
		b.end_m = z;
	}
	b.surface = b.end_m >= h - 1e-12;
	// the profile as the shader reads it: (1 - z / end)^p with p keeping its integral up the column
	b.exponent = area > 0.0 ? std::clamp(b.extinction_floor * b.end_m / area - 1.0, 0.0, 20.0) : 0.0;
	return b;
}

std::string check_B1(const Floor &f, const Bubbles &b) {
	char text[240];
	if (!(b.end_m >= 0.0) || b.end_m > f.depth_m * (1.0 + 1e-9) || !(b.extinction_floor >= 0.0) || !(b.gas_extinction >= 0.0) || !(b.exponent >= 0.0)) {
		std::snprintf(text, sizeof text, "B1: the bubbles end at %g m in %g m of liquid, with extinctions %g and %g a metre", b.end_m, f.depth_m, b.extinction_floor, b.gas_extinction);
		return text;
	}
	if (!(b.holdup_floor >= 0.0 && b.holdup_floor < 1.0) || !(b.gas_holdup >= 0.0 && b.gas_holdup < 1.0)) {
		std::snprintf(text, sizeof text, "B1: a holdup outside [0, 1): %g, %g", b.holdup_floor, b.gas_holdup);
		return text;
	}
	if (b.boiling_w > std::max(f.heat_w, 0.0) * (1.0 + 1e-9) || (b.boiling && !(b.boiling_w > 0.0))) {
		std::snprintf(text, sizeof text, "B1: the bubbles take %g W of the %g W the floor took", b.boiling_w, f.heat_w);
		return text;
	}
	return "";
}

} // namespace lab::bubbles
