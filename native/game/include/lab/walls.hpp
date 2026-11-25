#pragma once
// A vessel's wall as rings (GAME_SPEC §6, Stage 53), pure: a glassware vessel's glass cut into its floor and rings up
// to its brim, each one's glass and areas from the vessel's shape; and how fast a wall passes heat to what it
// touches: natural convection on a vertical wall (Churchill and Chu, as Lienhard eqs. 8.13a-b give them), into a
// liquid (water's properties, data/solutions.json's solvent) or into air (data/walls.json: Lienhard's Table A.6).
#include "lab/glassware.hpp"

#include <string_view>
#include <vector>

namespace lab::walls {

// a ring of a vessel's wall, or its floor (the first): heights over the base, cm; its glass, cm^3; the inside and
// outside surfaces it has, cm^2; the cross-section of glass where it meets the one over it, cm^2; and its inside
// radius at its middle, cm (a ring's) or its floor's inside radius (the floor's); how thick its glass is between
// its inside and its outside, cm (Stage 53: heat crosses half of it to either side); and of its outside, the floor's
// base, cm^2 (a disc facing down; the rest of its outside, and all of a ring's, stands as a wall)
struct Ring {
	double from = 0.0, to = 0.0;
	double glass_cm3 = 0.0;
	double inside_cm2 = 0.0, outside_cm2 = 0.0;
	double across_cm2 = 0.0;
	double radius = 0.0;
	double thickness_cm = 0.0;
	double base_cm2 = 0.0;
	bool floor = false;
};

// how many rings a vessel's wall is cut into, and how tall each (data/walls.json: about 1 cm, at least 4, at most
// 24), over the floor; with the floor first
std::vector<Ring> rings_of(const glass::Vessel &vessel);

// the inside wall's area between two heights over the base, cm^2 (2 pi r dy, the wall's slope aside)
double inside_between(const glass::Vessel &vessel, double from, double to);

// Churchill and Chu's mean Nusselt number of a vertical wall (Lienhard eq. 8.13a below Ra = 1e9, laminar; eq. 8.13b
// above it)
double nusselt(double rayleigh, double prandtl);

// air at 1 atm at a temperature (Lienhard's Table A.6, linear between its rows, held beyond them)
struct Air {
	double k = 0.0, nu = 0.0, alpha = 0.0, pr = 0.0;
};
Air air_at(double kelvin);

// a vertical wall's heat transfer coefficient, W/m^2 K, over a height (m) wetted by water at one temperature
// against the wall at another (properties at the film's), and the same into air; at least the conduction limit
// (Nu's own constant) where they are alike
double into_water(double height_m, double wall_k, double fluid_k);
double into_air(double height_m, double wall_k, double fluid_k);

// A horizontal disc's heat transfer coefficient into air facing down, W/m^2 K (a vessel's base, Stage 53), of a
// diameter (m), at a temperature against the air's: warmer than the air its boundary layer is stable (Kadambi and
// Drake, Lienhard eq. 8.39: Nu_D = 0.82 Ra_D^(1/5) Pr^0.034); colder, unstable (Raithby and Hollands over L* = A/P =
// D/4: Lienhard eqs. 8.37a-b below Ra = 1e7, eq. 8.35 above)
double under_disc(double diameter_m, double wall_k, double air_k);

// A wall species' total hemispherical emittance at a temperature (data/walls.json: glass from Lienhard's Table 10.1,
// smooth glass 0.94 at 40 C, Pyrex's 0.94 to 0.74 over 260 to 540 C; linear between rows, held beyond them); 0 for
// a species with none (it radiates nothing: its loss to the room is the bench's, Stage 53)
double emittance(std::string_view wall, double kelvin);

// Radiation's heat transfer coefficient between a grey surface of an emittance and the room around it, W/m^2 K:
// e sigma (T_w^2 + T_r^2)(T_w + T_r), Lienhard's eq. 1.34 factored as in eq. 2.28 but exact, with a small body's
// transfer factor in a large room its own emittance (eqs. 1.35, 2.31)
double radiation(double emittance, double wall_k, double room_k);

// A powder bed's effective thermal conductivity, W/m K: Zehner and Schlünder's (1970) stagnant bed, its grains of
// conductivity `solid` in a fluid of `fluid` (the gas, or the liquid over them), its porosity `porosity`, its shape
// constant C (1.25 for spheres, 1.40 for broken grains; B = C ((1 - eps) / eps)^(10/9)), as Kandula (NASA, eqs. 9a-c,
// 10) gives it; a solid no more conductive than its fluid conducts as the fluid
inline constexpr double BROKEN = 1.40;
double bed_conductivity(double fluid, double solid, double porosity, double shape = BROKEN);

} // namespace lab::walls
