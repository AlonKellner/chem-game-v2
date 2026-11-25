// The planner (GAME_SPEC §13, Stage 60).
#include "lab/planner.hpp"

#include <cmath>

namespace lab::plan {

namespace {

int nu_of(const chem::Reaction &r, chem::SpeciesId s) {
	for (const chem::Term &t : r.terms()) {
		if (t.species.index == s.index) {
			return t.nu;
		}
	}
	return 0;
}

} // namespace

std::vector<int> makers(chem::SpeciesId species) {
	std::vector<int> out;
	const auto all = chem::builtin_reactions();
	for (size_t k = 0; k < all.size(); ++k) {
		if (nu_of(all[k], species) > 0) {
			out.push_back(static_cast<int>(k));
		}
	}
	return out;
}

Plan work_back(const chem::SpeciesTable &table, chem::SpeciesId target, double target_g, const std::vector<Step> &route) {
	Plan out;
	const auto all = chem::builtin_reactions();
	if (route.empty()) {
		out.why = "choose the reaction that makes it";
		return out;
	}
	if (!(target_g > 0.0)) {
		out.why = "a target is a mass, more than nothing";
		return out;
	}
	chem::SpeciesId wanted = target;
	double wanted_mol = target_g / (static_cast<double>(table.molar_mass_ug(target)) * 1e-6);
	for (size_t k = 0; k < route.size(); ++k) {
		const Step &step = route[k];
		if (step.reaction < 0 || step.reaction >= static_cast<int>(all.size())) {
			out.why = "a step names no reaction";
			return out;
		}
		if (!(step.yield > 0.0 && step.yield <= 1.0)) {
			out.why = "a yield is more than 0 and at most 100 %";
			return out;
		}
		const chem::Reaction &r = all[static_cast<size_t>(step.reaction)];
		const int made = nu_of(r, wanted);
		if (made <= 0) {
			out.why = "step " + std::to_string(k + 1) + "'s reaction does not make " + std::string(table.species(wanted).formula);
			return out;
		}
		const double extent = wanted_mol / static_cast<double>(made) / step.yield;
		out.extents.push_back(extent);
		out.made.push_back(wanted);
		// what the next step is asked for: the first reactant of this one that the next step's reaction makes
		chem::SpeciesId next{};
		const bool more = k + 1 < route.size();
		if (more && route[k + 1].reaction >= 0 && route[k + 1].reaction < static_cast<int>(all.size())) {
			for (const chem::Term &t : r.terms()) {
				if (t.nu < 0 && nu_of(all[static_cast<size_t>(route[k + 1].reaction)], t.species) > 0) {
					next = t.species;
					break;
				}
			}
			if (next.index < 0) {
				out.why = "step " + std::to_string(k + 2) + "'s reaction makes none of what step " + std::to_string(k + 1) + " needs";
				return out;
			}
		}
		for (const chem::Term &t : r.terms()) {
			if (t.nu >= 0) {
				continue;
			}
			Need n;
			n.species = t.species;
			n.formula = std::string(table.species(t.species).formula);
			n.mol = extent * static_cast<double>(-t.nu);
			n.g = n.mol * static_cast<double>(table.molar_mass_ug(t.species)) * 1e-6;
			n.step = static_cast<int>(k);
			n.carried = t.species.index == next.index;
			out.needs.push_back(n);
		}
		if (more) {
			wanted = next;
			wanted_mol = extent * static_cast<double>(-nu_of(r, next));
		}
	}
	out.ok = true;
	return out;
}

} // namespace lab::plan
