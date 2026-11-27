#pragma once
// Boiling's bubbles (GAME_SPEC §11, Stage 45): what a vessel's heated floor does, pure. A liquid heated from below
// is unstable, and natural convection keeps its bulk at one temperature; what is not uniform is a thin boundary
// layer against the hot floor, which can stand above the boiling point while the bulk is below it. Bubbles start
// there, rise, and give their heat to a bulk cooler than their own boiling point until they are gone, or reach
// the surface. A bubble lives a tenth of a second and the temperatures change over minutes, so the bubbles are
// derived each step from the heat the floor took, the bulk's temperature and the liquid's depth: a view of the
// state, like a vortex (§11), not state. Where the heat goes is the same either way: the bulk is one cell.
// The data: data/boiling.json (docs/DATA_SOURCES.md, "Boiling's bubbles (Stage 45)").
#include <string>
#include <string_view>

namespace lab::bubbles {

// saturated liquid water at a temperature (Lienhard, Table A.3: the core's solvent, chem::solvent_at), read between
// rows, held beyond the ends
struct Water {
	double rho = 0.0; // kg/m^3
	double cp = 0.0; // J/kg K
	double k = 0.0; // W/m K
	double alpha = 0.0; // m^2/s
	double nu = 0.0; // m^2/s
	double pr = 0.0;
	double beta = 0.0; // 1/K
};
Water water_at(double celsius);

// bubbles in water as the optics has them (data/optics/media.json's scatterers: GAME_SPEC §12): their extinction
// efficiency, and their asymmetry parameter g (what they scatter goes mostly forward, so the haze they make of the
// room's light is their extinction times 1 - g)
double efficiency();
double asymmetry();

// water's surface tension against its vapour, N/m (Lienhard eq. 9.2a, IAPWS)
double surface_tension(double kelvin);

// a floor's surface: its largest cavities (m), the contact angle that sizes a departing bubble (degrees), and
// Rohsenow's C_sf with its Prandtl exponent
struct Surface {
	double cavity = 0.0, contact_deg = 0.0, csf = 0.0, s = 1.0;
};
// by name (data/boiling.json's surfaces); an unknown one is glass
const Surface &surface(std::string_view name);

// natural convection from a floor heated from below into the liquid over it (walls::unstable_plate: Lienhard eqs.
// 8.37a-b below Ra = 1e7, Raithby and Hollands; eq. 8.35 above), the length A / P (Lloyd and Moran): W/m^2 K, the
// properties at the film's mean temperature. 0 where the floor is no warmer.
double convection(double floor_m2, double perimeter_m, double floor_c, double bulk_c);

// nucleate boiling's heat flux at a wall superheat (Rohsenow, Lienhard eq. 9.4), W/m^2; the liquid's properties
// at its saturation, sigma N/m, h_fg J/kg, rho_v kg/m^3
double rohsenow(double superheat, const Water &liquid, double sigma, double h_fg, double rho_v, const Surface &surface);

// a bubble's diameter as it leaves the floor (Fritz: 0.0208 theta sqrt(sigma / g drho)), m
double departure(double contact_deg, double sigma, double drho);

// a bubble's terminal rise in a pure liquid (Baz-Rodriguez et al. 2012, eq. 1: the viscous and the surface-tension
// limits combined), m/s
double rise(double d, const Water &liquid, double sigma);

// a steam bubble condensing as it rises (Chen and Mayinger 1992: Nu = 0.185 Re^0.7 Pr^0.5), W/m^2 K
double condensing(double d, double u, const Water &liquid);

// What the floor and the bulk are, for one vessel. Water's saturation as the species table has it (the
// model's own boiling point), its activity in the solution lowering it.
struct Floor {
	double heat_w = 0.0; // the heat its floor took this step, W
	double floor_m2 = 0.0, perimeter_m = 0.0; // the inside floor's area and its edge
	double depth_m = 0.0; // the liquid over the floor
	double bulk_c = 0.0; // the bulk's temperature
	double pressure_pa = 101325.0; // over the liquid's surface
	double water_activity = 1.0;
	std::string surface = "glass";
	// a gas a reaction gives off in the liquid, m^3/s at the bulk's temperature and pressure (Stage 44's fizz)
	double gas_m3_s = 0.0;
	// its cavities already hold nuclei (Stage 53): it boiled, and has not since cooled under its boiling point. A
	// nucleating floor boils from its boiling point, not from the onset (boiling's hysteresis: a cavity that has
	// nucleated keeps its vapour)
	bool nucleating = false;
};

// The heat a floor at a temperature passes into the liquid over it, W (Stage 53: a vessel's floor ring against its
// lowest liquid): convection, and past the onset where its cavities hold a nucleus Rohsenow's boiling too, as boil()
// splits it. A floor no warmer than the bulk passes none this way.
double floor_flux(const Floor &f, double floor_c);
// The same through half the floor's glass (Stage 53): its middle at a temperature, the glass's resistance between
// its middle and its inside surface (K/W over the whole floor); the surface where what crosses the glass is what the
// surface passes on, and that heat, W
struct Through {
	double heat_w = 0.0, surface_c = 0.0;
};
Through floor_through(const Floor &f, double middle_c, double resistance);
// whether a floor at a temperature nucleates (Stage 53): past the onset it starts; under its boiling point it stops;
// between, it goes on as it was (f.nucleating)
bool floor_nucleates(const Floor &f, double floor_c);

struct Bubbles {
	double floor_c = 0.0; // the boundary layer's, against the floor
	double saturation_floor_c = 0.0, saturation_top_c = 0.0; // the boiling point at the floor and at the surface
	double onset_c = 0.0; // where the floor's cavities hold a nucleus
	bool boiling = false; // bubbles of steam leave the floor
	double boiling_w = 0.0; // the floor's heat the bubbles take: what is not carried by convection
	double diameter_m = 0.0; // a steam bubble's as it leaves the floor
	double per_second = 0.0; // steam bubbles leaving the floor
	double end_m = 0.0; // how high they reach: where they are gone, or the surface
	bool surface = false; // they reach the surface
	// what is seen (§12): the bubbles' extinction a metre (their efficiency, 2, times their cross-section a cubic metre), at the
	// floor, falling as (1 - z / end)^exponent to where they end; and a reaction's gas, the same at every height
	double extinction_floor = 0.0, exponent = 0.0, gas_extinction = 0.0;
	double holdup_floor = 0.0; // the steam's share of the liquid's volume at the floor
	double gas_diameter_m = 0.0, gas_holdup = 0.0;
	// the steam bubbles' extinction at a height over the floor, and the gas's; a metre
	double extinction_at(double z_m) const;
};

// The table's own saturation of water: its vapour pressure at a temperature, Pa, and the temperature at which a
// solution of that activity boils under a pressure, K
double saturation_pressure(double kelvin);
double boiling_point(double pressure_pa, double water_activity);

Bubbles boil(const Floor &floor);

// B1 (GAME_SPEC §18): the bubbles lie in the liquid: where they end is from the floor to the surface, their
// extinction and holdup are no less than nothing, the holdup under one, and the steam's heat no more than the
// floor took; "" when it holds
std::string check_B1(const Floor &floor, const Bubbles &b);

} // namespace lab::bubbles
