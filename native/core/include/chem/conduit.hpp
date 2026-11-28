#pragma once

#include <array>

// (Stage 89, GAME_SPEC §6, Flow between vessels) A conduit: what joins two vessels and holds back what crosses it.
// Round pieces in series (a nozzle, a tube, a nozzle), each a bore and a length; its law from its geometry alone,
// pure. A mass rate m through it takes the drop
//   rho dP = sum over its pieces of m^2 / (2 A^2) (64 x + N12(a, b) + e),  x = L / (D Re)
// 64 x: Poiseuille's drop. Over it, the piece's entrance, seen two ways: a, attached (what Shah's laminar developing
// flow has over Poiseuille's, `entrance_excess`: 1.25 heads long, the boundary layer's short); b, separated (a
// contraction's loss, 0.5 (1 - A / A_before) where it narrows, and what Churchill's friction, `friction_factor`, has
// over Poiseuille's times L / D: nothing until it is turbulent). N12 their 12-norm: whichever loses more, joined as
// Churchill joins his regimes. e: Borda and Carnot's expansion out of it, (1 - A / A_after)^2 (into a vessel: 1). The
// Reynolds number m D / (A mu) holds no density, so the right side is one function for a liquid and a gas:
// `conduit_drive`.
namespace chem {

struct Piece {
	double bore = 0.0, length = 0.0; // m
	bool operator==(const Piece &) const = default;
};

inline constexpr int MAX_PIECES = 4;

struct Conduit {
	std::array<Piece, MAX_PIECES> pieces{}; // from the edge's a to its b
	int count = 0; // none: nothing holds back what crosses (the limit of a wide one)
	bool operator==(const Conduit &) const = default;
};

// every piece of a bore and a length above 0 and finite, no more than MAX_PIECES (none is good: no conduit)
bool good(const Conduit &conduit);

// The correlations' own numbers (data/flow.json, embedded by chem_datagen).
struct FlowLaw {
	double laminar = 0.0, join = 0.0, log = 0.0, smooth = 0.0, smooth_power = 0.0, transition = 0.0, power = 0.0; // Churchill 1977
	double entrance = 0.0, excess = 0.0, developed = 0.0, c = 0.0; // Shah 1978
	double contraction = 0.0; // a sharp-edged contraction's loss
};
const FlowLaw &builtin_flow();

// Churchill's friction factor (Darcy's) of a smooth round pipe at a Reynolds number above 0: 64 / Re laminar, the
// transition and the turbulent law in one.
double friction_factor(double reynolds);

// Shah's drop, over rho v^2 / 2, of laminar flow developing from a uniform profile over x = L / (D Re) > 0: the
// boundary layer's 13.76 sqrt(x) short, 64 x + 1.25 long; and what it has over Poiseuille's 64 x (never below 0).
double entrance_drop(double x);
double entrance_excess(double x);

// rho dP (kg/m^3 Pa) that a mass rate (kg/s, at least 0) takes through the conduit at a viscosity (Pa s), from its
// first piece to its last (`back`: from its last to its first). Rising with the rate. 0 with no conduit.
double conduit_drive(const Conduit &conduit, double rate, double viscosity, bool back = false);

// The rate (kg/s) that rho dP = `drive` pushes through it: the root of the above. 0 at no drive, and with no conduit
// or no viscosity (nothing to say how fast). (Stage 94) `give`: what a rate takes off its own drive, kg/m^3 Pa per
// kg/s (a liquid's crossing lowers the surface it leaves and raises the one it joins: rho g dt (1/A_from + 1/A_to)
// over a step): the root of conduit_drive(m) + give m = drive, so a crossing never passes the level it runs to.
double conduit_rate(const Conduit &conduit, double drive, double viscosity, bool back = false, double give = 0.0);

// A liquid's rate (kg/s) from a vessel at `from` Pa into one at `to` (0 unless from > to): at its density.
double liquid_rate(const Conduit &conduit, double from, double to, double density, double viscosity, bool back = false, double give = 0.0);

// A gas's rate (kg/s) from `from` Pa into `to`: at the density of the mean pressure at the giver's temperature,
// rho = M (from + to) / (2 R T) (the isothermal pipe's M (from^2 - to^2) / (2 R T), less its acceleration), and never
// above the choked rate through its narrowest section, A from sqrt(gamma M / (R T)) (2 / (gamma + 1))^((gamma + 1) /
// (2 (gamma - 1))). `molar_mass` kg/mol, `gamma` its heat capacities' ratio.
double gas_rate(const Conduit &conduit, double from, double to, double kelvin, double molar_mass, double gamma, double viscosity, bool back = false);

} // namespace chem
