#pragma once

#include "chem/species.hpp"

#include <string>
#include <vector>

// The planner (GAME_SPEC §13, Stage 60; docs/CURRICULUM.md S3), pure: a target (a species, its mass) and the route the
// student chooses to it, from the target back: each step a reaction that makes what the step before it needs, and the
// yield the student expects of it. Worked back, every step's reaction runs as far as its product is needed over its
// yield, and every reactant it uses is needed in turn: the one the next step makes is carried down the route, the
// others are bought (the acid, the base, the water).
namespace lab::plan {

struct Step {
	int reaction = -1; // into chem::builtin_reactions()
	double yield = 1.0; // a share, (0, 1]
};

struct Need {
	chem::SpeciesId species;
	std::string formula;
	double mol = 0.0, g = 0.0;
	int step = 0; // the step that needs it
	bool carried = false; // made by the next step (an intermediate), not bought
};

struct Plan {
	bool ok = false;
	std::string why;
	std::vector<double> extents; // each step's, mol of its reaction
	std::vector<chem::SpeciesId> made; // what each step makes for the one before it (the target for the first)
	std::vector<Need> needs; // every reactant of every step, in the route's order
};

// The route from `target` back. A step's reaction must make what it is asked for: the target, or a reactant of the
// step before it (the first such reactant that reaction makes).
Plan work_back(const chem::SpeciesTable &table, chem::SpeciesId target, double target_g, const std::vector<Step> &route);

// The reactions that make a species (the planner's choices for a step)
std::vector<int> makers(chem::SpeciesId species);

} // namespace lab::plan
