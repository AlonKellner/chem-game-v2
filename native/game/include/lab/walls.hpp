#pragma once
// A vessel's wall as rings (GAME_SPEC §6, Stage 53), pure: a glassware vessel's glass cut into its floor and rings up
// to its brim, each one's glass and areas from the vessel's shape; and how fast a wall passes heat to what it
// touches: natural convection on a vertical wall (Churchill and Chu, as Lienhard eqs. 8.13a-b give them), into a
// liquid (water's properties, data/solutions.json's solvent) or into air (data/walls.json: Lienhard's Table A.6).
#include "lab/glassware.hpp"

#include <vector>

namespace lab::walls {

// a ring of a vessel's wall, or its floor (the first): heights over the base, cm; its glass, cm^3; the inside and
// outside surfaces it has, cm^2; the cross-section of glass where it meets the one over it, cm^2; and its inside
// radius at its middle, cm (a ring's) or its floor's inside radius (the floor's)
struct Ring {
	double from = 0.0, to = 0.0;
	double glass_cm3 = 0.0;
	double inside_cm2 = 0.0, outside_cm2 = 0.0;
	double across_cm2 = 0.0;
	double radius = 0.0;
	bool floor = false;
};

// how many rings a vessel's wall is cut into, and how tall each (data/walls.json: about 5 mm, at least 4, at most
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

// A powder bed's effective thermal conductivity, W/m K: Zehner and Schlünder's (1970) stagnant bed, its grains of
// conductivity `solid` in a fluid of `fluid` (the gas, or the liquid over them), its porosity `porosity`, its shape
// constant C (1.25 for spheres, 1.40 for broken grains; B = C ((1 - eps) / eps)^(10/9)), as Kandula (NASA, eqs. 9a-c,
// 10) gives it; a solid no more conductive than its fluid conducts as the fluid
inline constexpr double BROKEN = 1.40;
double bed_conductivity(double fluid, double solid, double porosity, double shape = BROKEN);

} // namespace lab::walls
