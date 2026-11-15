#pragma once

#include "chem/world.hpp"

#include <vector>

// The simulation: a world advanced one fixed step at a time by its processes
// (GAME_SPEC §4.5): reactions inside the cells (§7), then heat conduction along
// the edges (§5). Every change goes through the world's two primitives. The
// processes are applied in a fixed order, each from the state the last one
// left; each application conserves exactly and runs down its own driving force.
namespace chem {

// One step of simulated time, in seconds. Time acceleration runs more steps
// per frame; it never changes the step (GAME_SPEC §3).
inline constexpr double STEP_SECONDS = 1.0 / 60.0;

class Simulation {
public:
	// `reactions` are the ones that run on their own, in every cell, in this
	// order: those with kinetics (the others are ignored). They must be
	// reactions of the world's species table.
	explicit Simulation(World world, std::vector<Reaction> reactions = {});

	World &world() { return world_; }
	const World &world() const { return world_; }

	// Advances one step and returns its trace. With checking on, a violated
	// invariant is a CHEM_ASSERT (abort in Debug, report and continue otherwise).
	StepTrace step();

	// Checking runs every invariant on every step. On by default where
	// CHEM_ASSERT aborts (Debug and sanitizer builds), off otherwise.
	void set_checking(bool on) { checking_ = on; }
	bool checking() const { return checking_; }

	// The step, in seconds of simulated time. The game never changes it
	// (GAME_SPEC §3); the convergence tests do.
	void set_step_seconds(double seconds) { step_seconds_ = seconds; }
	double step_seconds() const { return step_seconds_; }

	// The processes' carried remainders, in quanta: part of the state (a save
	// holds them). Heat: one per edge. Reactions: one per cell and reaction.
	const std::vector<double> &heat_remainders() const { return heat_remainder_; }
	const std::vector<double> &reaction_remainders() const { return reaction_remainder_; }

private:
	void react();
	void conduct_heat();

	World world_;
	std::vector<Reaction> reactions_;
	std::vector<double> heat_remainder_;
	std::vector<double> reaction_remainder_;
	double step_seconds_ = STEP_SECONDS;
	bool checking_;
};

} // namespace chem
