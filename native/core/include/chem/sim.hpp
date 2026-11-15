#pragma once

#include "chem/world.hpp"

#include <vector>

// The simulation: a world advanced one fixed step at a time by its processes
// (GAME_SPEC §4.5). Stage 2: heat conduction along the edges (§5). Every
// change goes through the world's two primitives.
namespace chem {

// One step of simulated time, in seconds. Time acceleration runs more steps
// per frame; it never changes the step (GAME_SPEC §3).
inline constexpr double STEP_SECONDS = 1.0 / 60.0;

class Simulation {
public:
	explicit Simulation(World world);

	World &world() { return world_; }
	const World &world() const { return world_; }

	// Advances one step and returns its trace. With checking on, a violated
	// invariant is a CHEM_ASSERT (abort in Debug, report and continue otherwise).
	StepTrace step();

	// Checking runs every invariant on every step. On by default where
	// CHEM_ASSERT aborts (Debug and sanitizer builds), off otherwise.
	void set_checking(bool on) { checking_ = on; }
	bool checking() const { return checking_; }

	// The heat processes' carried remainders, one per edge, in quanta: part of
	// the state (a save holds them).
	const std::vector<double> &heat_remainders() const { return heat_remainder_; }

private:
	void conduct_heat();

	World world_;
	std::vector<double> heat_remainder_;
	bool checking_;
};

} // namespace chem
