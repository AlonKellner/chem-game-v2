#pragma once

#include "chem/species.hpp"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

// The calculator (GAME_SPEC §13, Stage 58; docs/CURRICULUM.md S2), pure: the sums a first-year student sets up, worked
// out from the simulation's own tables. The student chooses what to work out and gives its numbers; the calculator
// says what follows, and how. A formula is typed as the tables spell one ("CuSO4.5H2O", "Cu2CO3(OH)2"): its molar mass
// from the elements' standard atomic weights, any formula of the elements the tables know.
namespace lab::calc {

// A formula's molar mass, g/mol, and each element's share of it, by mass
struct MolarMass {
	bool ok = false;
	std::string why; // when not
	double g_per_mol = 0.0;
	std::vector<std::pair<std::string, double>> shares; // symbol, % by mass, in the order the tables list them
	std::vector<std::pair<std::string, int>> atoms; // symbol, count
};
MolarMass molar_mass(const chem::SpeciesTable &table, std::string_view formula);

// n = m / M, m = n M
double moles(double grams, double g_per_mol);
double grams(double mol, double g_per_mol);
// The mass of a solid (of molar mass M) that makes `ml` of a solution `molar` mol/L in its formula unit: c V M
double solution_grams(double molar, double ml, double g_per_mol);
// A dilution: the volume of the stock (c1) that, made up to v2, gives c2: c1 V1 = c2 V2
double dilution_ml(double c1, double c2, double v2_ml);

// A reaction's stoichiometry: the amounts given of some of its terms (g; none for a term not given), and what follows:
// for each reactant given, the extent it alone allows; the limiting one (the least extent); at that extent, each term's
// moles and grams used or made
struct Stoichiometry {
	bool ok = false;
	std::string why;
	struct Term {
		std::string formula;
		int nu = 0; // signed: reactants negative
		double g_per_mol = 0.0;
		std::optional<double> given_g;
		std::optional<double> allows; // a reactant given: the extent it alone allows, mol
		double mol = 0.0, g = 0.0; // at the limiting extent: used (a reactant) or made (a product)
	};
	std::vector<Term> terms;
	double extent = 0.0; // mol of the reaction
	int limiting = -1; // the term that limits it
};
Stoichiometry stoichiometry(const chem::SpeciesTable &table, const chem::Reaction &reaction, const std::vector<std::optional<double>> &given_g);
// A yield: what was got of a product against its theoretical amount, %
double percent_yield(double actual_g, double theoretical_g);

// The model's own solubility (the reference's table): the molality of `dissolved` in water at which `crystallization`
// (dissolved + water -> a solid) is at equilibrium, mol/kg; 0 if none is found up to 16 mol/kg
double solubility(const chem::SpeciesTable &table, const chem::Reaction &crystallization, chem::SpeciesId dissolved, double kelvin);

// A reaction's equation as the tables' formulas spell it: "CuO(s) + H2SO4(aq) -> CuSO4(aq) + H2O(l)"
std::string equation(const chem::SpeciesTable &table, const chem::Reaction &reaction);

} // namespace lab::calc
