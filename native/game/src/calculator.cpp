// The calculator (GAME_SPEC §13, Stage 58).
#include "lab/calculator.hpp"

#include "chem/formula.hpp"
#include "chem/quantity.hpp"
#include "chem/thermo.hpp"
#include "chem/world.hpp"

#include <algorithm>
#include <cmath>

namespace lab::calc {

MolarMass molar_mass(const chem::SpeciesTable &table, std::string_view formula) {
	MolarMass out;
	chemdata::AtomCounts atoms;
	if (!chemdata::parse_formula(formula, atoms, out.why)) {
		out.why = "not a formula: " + out.why;
		return out;
	}
	if (atoms.empty()) {
		out.why = "a formula, such as CuSO4.5H2O";
		return out;
	}
	double total = 0.0;
	std::vector<std::pair<std::string, double>> masses;
	for (int e = 0; e < table.element_count(); ++e) {
		const chem::ElementDef &el = table.element(e);
		const auto at = atoms.find(std::string(el.symbol));
		if (at == atoms.end()) {
			continue;
		}
		const double m = static_cast<double>(at->second) * static_cast<double>(el.mass_ug_per_mol) * 1e-6;
		total += m;
		masses.emplace_back(std::string(el.symbol), m);
		out.atoms.emplace_back(std::string(el.symbol), at->second);
	}
	for (const auto &[symbol, count] : atoms) {
		if (std::none_of(out.atoms.begin(), out.atoms.end(), [&](const auto &a) { return a.first == symbol; })) {
			out.why = "the tables know no " + symbol + ": its atomic weight is not in data/elements.json";
			out.atoms.clear();
			return out;
		}
	}
	out.ok = true;
	out.g_per_mol = total;
	for (const auto &[symbol, m] : masses) {
		out.shares.emplace_back(symbol, 100.0 * m / total);
	}
	return out;
}

double moles(double g, double g_per_mol) {
	return g_per_mol > 0.0 ? g / g_per_mol : 0.0;
}

double grams(double mol, double g_per_mol) {
	return mol * g_per_mol;
}

double solution_grams(double molar, double ml, double g_per_mol) {
	return molar * ml / 1000.0 * g_per_mol;
}

double dilution_ml(double c1, double c2, double v2_ml) {
	return c1 > 0.0 ? c2 * v2_ml / c1 : 0.0;
}

namespace {

std::string phase_of(chem::Phase p) {
	return p == chem::Phase::Solid ? "s" : p == chem::Phase::Liquid ? "l" : p == chem::Phase::Gas ? "g" : "aq";
}

std::string term_text(const chem::SpeciesTable &table, chem::SpeciesId id) {
	const chem::SpeciesDef &def = table.species(id);
	return std::string(def.formula) + "(" + phase_of(def.phase) + ")";
}

} // namespace

std::string equation(const chem::SpeciesTable &table, const chem::Reaction &reaction) {
	std::string left, right;
	for (const chem::Term &t : reaction.terms()) {
		std::string &side = t.nu < 0 ? left : right;
		const int n = t.nu < 0 ? -t.nu : t.nu;
		side += (side.empty() ? "" : " + ") + (n > 1 ? std::to_string(n) + " " : std::string()) + term_text(table, t.species);
	}
	return left + " -> " + right;
}

Stoichiometry stoichiometry(const chem::SpeciesTable &table, const chem::Reaction &reaction, const std::vector<std::optional<double>> &given_g) {
	Stoichiometry out;
	const auto terms = reaction.terms();
	out.extent = -1.0;
	for (size_t k = 0; k < terms.size(); ++k) {
		Stoichiometry::Term t;
		t.formula = term_text(table, terms[k].species);
		t.nu = terms[k].nu;
		t.g_per_mol = static_cast<double>(table.molar_mass_ug(terms[k].species)) * 1e-6;
		t.given_g = k < given_g.size() ? given_g[k] : std::nullopt;
		if (t.given_g && !(*t.given_g >= 0.0)) {
			out.why = "an amount is a mass, 0 or more grams";
			return out;
		}
		if (t.given_g && t.nu < 0) {
			t.allows = *t.given_g / t.g_per_mol / static_cast<double>(-t.nu);
			if (out.extent < 0.0 || *t.allows < out.extent) {
				out.extent = *t.allows;
				out.limiting = static_cast<int>(k);
			}
		}
		out.terms.push_back(t);
	}
	if (out.limiting < 0) {
		// no reactant given: a product's mass says how far it went
		for (size_t k = 0; k < out.terms.size(); ++k) {
			if (out.terms[k].given_g && out.terms[k].nu > 0) {
				out.extent = *out.terms[k].given_g / out.terms[k].g_per_mol / static_cast<double>(out.terms[k].nu);
				out.limiting = static_cast<int>(k);
				break;
			}
		}
	}
	if (out.limiting < 0) {
		out.why = "give the mass of a reactant (or of a product)";
		return out;
	}
	for (Stoichiometry::Term &t : out.terms) {
		t.mol = out.extent * static_cast<double>(t.nu < 0 ? -t.nu : t.nu);
		t.g = t.mol * t.g_per_mol;
	}
	out.ok = true;
	return out;
}

double solubility(const chem::SpeciesTable &table, const chem::Reaction &crystallization, chem::SpeciesId dissolved, double kelvin) {
	// a kilogram of water and m mol/kg dissolved: where crystallizing turns from running back to running on
	const auto affinity_at = [&](double m, bool &defined) {
		chem::CellState state;
		state.amount.assign(static_cast<size_t>(table.species_count()), 0);
		bool ok = false;
		state.amount[static_cast<size_t>(table.find("water").index)] = chem::amount_from_mol(1000.0 / 18.015, ok).quanta;
		chem::put_amount(table, state, dissolved, chem::amount_from_mol(m, ok).quanta); // a salt as its ions (Stage 63)
		return chem::affinity(table, state, crystallization.terms(), kelvin, defined);
	};
	// up from a dilute solution, doubling until crystals would grow (far past the activity model's range, at tens of
	// mol/kg, its affinity turns back: it is not searched there)
	double lo = 1e-4, hi = 0.25;
	bool defined = false;
	if (!(affinity_at(lo, defined) < 0.0) || !defined) {
		return 0.0;
	}
	while (!(affinity_at(hi, defined) > 0.0)) {
		if (!defined || hi > 16.0) {
			return 0.0;
		}
		lo = hi;
		hi *= 2.0;
	}
	for (int i = 0; i < 60; ++i) {
		const double mid = std::sqrt(lo * hi);
		(affinity_at(mid, defined) > 0.0 ? hi : lo) = mid;
	}
	return std::sqrt(lo * hi);
}

double percent_yield(double actual_g, double theoretical_g) {
	return theoretical_g > 0.0 ? 100.0 * actual_g / theoretical_g : 0.0;
}

} // namespace lab::calc
