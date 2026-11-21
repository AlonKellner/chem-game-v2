#pragma once

#include "chem/thermo.hpp"
#include "chem/world.hpp"

#include <vector>

// The simulation: a world advanced one fixed step at a time by its processes
// (GAME_SPEC §4.5): reactions inside the cells (§7, §9), heat conduction along the
// edges (§5), liquids down the drains (§6), then every cell taken to its fast
// equilibria (§8) and every rigid cell made exactly full (§6), and what
// overflows spilled.
// Every change goes through the world's two primitives. The
// processes are applied in a fixed order, each from the state the last one
// left; each application conserves exactly and runs down its own driving force.
namespace chem {

// One step of simulated time, in seconds. Time acceleration runs more steps
// per frame; it never changes the step (GAME_SPEC §3).
inline constexpr double STEP_SECONDS = 1.0 / 60.0;

// The last step's flows (GAME_SPEC §13.6): what the step's primitives moved,
// summed by process. Kept whether the step was traced or not; not state.
enum class Process : uint8_t { Reaction = 0, Heat = 1, Drain = 2, Settle = 3, Overflow = 4 };
inline constexpr size_t PROCESSES = 5;
struct StepFlows {
	// per edge and process (edge * PROCESSES + process): from the edge's a to its b
	std::vector<int64_t> energy; // quanta
	std::vector<double> grams;
	// per cell and reaction (cell * reactions + r; the rated ones, then the
	// equilibria): the extent applied, in quanta, forward positive
	std::vector<int64_t> extent;
	size_t reactions = 0; // rated + equilibria
};

// A surface reaction's two steps in a cell (GAME_SPEC §7), each in moles of
// its extent per m^2 of its solid per second, in the direction of the
// affinity `a` (J/mol) at `temperature`: at the surface, k(T) m_driver^q |f|;
// by transport, (Sh D / d)(C - C_eq) / nu, with Sh = 2 * mixing (infinity:
// nothing limits transport); and the two in series. 0 for another law.
struct SurfaceSteps {
	double surface = 0.0;
	double transport = 0.0;
	double both = 0.0;
	bool transported = false; // a species is carried, and transport has a finite rate
};
SurfaceSteps surface_steps(const SpeciesTable &table, const CellState &cell, const Reaction &reaction, double temperature, double a, double mixing);

class Simulation {
public:
	// `reactions` are the ones that run on their own, in every cell, in this
	// order: those with a rate law, and those of the equilibrium law (the
	// others are ignored). They must be reactions of the world's species table.
	explicit Simulation(World world, std::vector<Reaction> reactions = {});

	World &world() { return world_; }
	const World &world() const { return world_; }

	// Advances one step and returns its trace. With checking on, a violated
	// invariant is a CHEM_ASSERT (abort in Debug, report and continue otherwise).
	StepTrace step();
	// Advances one step without building its trace or checking it: the same
	// state, bit for bit, at a fraction of the cost.
	void advance();

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
	const std::vector<double> &nucleation_remainders() const { return nucleation_remainder_; }
	// puts back the remainders of a saved simulation, as the three above gave them
	void restore_remainders(std::vector<double> heat, std::vector<double> reaction, std::vector<double> nucleation);
	// the reactions of the equilibrium law (what invariant P1 is checked for)
	const std::vector<Reaction> &equilibria() const { return equilibria_; }
	// the reactions with a rate law, in the order the tally counts them
	const std::vector<Reaction> &rated() const { return reactions_; }

	// How far each reaction with a rate law ran in each cell since the tally was
	// last cleared, in quanta of extent (forward positive): what has happened,
	// for the lab's notices (GAME_SPEC §13.4); not state, never in a digest.
	int64_t tally(size_t reaction, CellId cell) const;
	void clear_tally() { tally_.assign(tally_.size(), 0); }
	// all of it, cell by cell, and back (a save of the lab's notices holds it)
	const std::vector<int64_t> &tally_all() const { return tally_; }
	void restore_tally(std::vector<int64_t> tally) { tally_ = std::move(tally); }

	// The last step's flows (GAME_SPEC §13.6), empty before the first step.
	const StepFlows &flows() const { return flows_; }

private:
	void begin_step();
	const Thermal &thermal(CellId cell);
	Refusal transfer(EdgeId edge, CellId from, std::span<const SpeciesAmount> amounts, Energy energy);
	Refusal transform(CellId cell, const Reaction &reaction, int64_t extent);
	void react();
	void conduct_heat();
	void drain_liquids();
	void overflow();
	void settle();
	void build_tree();
	void settle_cells();
	bool project(CellId cell);
	bool fill(CellId cell, CellId up, EdgeId edge, bool may_draw = true);

	World world_;
	std::vector<Reaction> reactions_; // with a rate law
	std::vector<Reaction> equilibria_; // of the equilibrium law
	std::vector<int32_t> depth_, parent_, parent_edge_; // settle()'s tree, kept to spare the allocations
	std::vector<size_t> order_;
	bool tree_built_ = false;
	uint64_t tree_version_ = 0;
	std::vector<double> heat_remainder_;
	std::vector<double> reaction_remainder_;
	std::vector<double> nucleation_remainder_;
	std::vector<int64_t> tally_; // cell * reactions + reaction
	StepFlows flows_;
	Process process_ = Process::Reaction; // what the primitives are applied for now
	std::vector<uint8_t> starved_;
	std::vector<Thermal> thermal_; // per cell, while thermal_valid_
	std::vector<uint8_t> thermal_valid_;
	double step_seconds_ = STEP_SECONDS;
	bool traced_ = true; // the last step was step(), not advance()
	bool checking_;
};

} // namespace chem
