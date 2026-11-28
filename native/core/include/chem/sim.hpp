#pragma once

#include "chem/thermo.hpp"
#include "chem/error.hpp"
#include "chem/world.hpp"

#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

// The simulation: a world advanced one fixed step at a time by its processes
// (GAME_SPEC §4.5): reactions inside the cells (§7, §9), heat conduction along the
// edges (§5), liquids down the drains (§6), then every cell taken to its fast
// equilibria (§8), every open rigid cell made exactly full and the cells
// sealed together at one pressure (§6), and what overflows spilled.
// Every change goes through the world's two primitives. The
// processes are applied in a fixed order, each from the state the last one
// left; each application conserves exactly and runs down its own driving force.
namespace chem {

// One step of simulated time, in seconds. Time acceleration runs more steps
// per frame; it never changes the step (GAME_SPEC §3).
inline constexpr double STEP_SECONDS = 1.0 / 60.0;

// The last step's flows (GAME_SPEC §13.6): what the step's primitives moved,
// summed by process. Kept whether the step was traced or not; not state.
enum class Process : uint8_t { Reaction = 0, Heat = 1, Drain = 2, Settle = 3, Overflow = 4, Work = 5, Column = 6, Exchange = 7 };
inline constexpr size_t PROCESSES = 8;
// a column grows by this many layers at a time (Stage 47; a design value)
inline constexpr int COLUMN_BLOCK = 4;
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
// A pour's stream (GAME_SPEC §6, Stage 51; docs/DATA_SOURCES.md). A round turbulent jet of volume flux `flux` (m^3/s)
// leaving at `speed` (m/s) entrains this much of what it passes, m^3/s a metre of its path: its flux grows as
// 0.32 Q0 x / d0 (Ricou and Spalding 1961), d0 the diameter of a jet of that flux and speed.
double jet_entrains(double flux, double speed);
// How far a jet of momentum flux `momentum` (m^4/s^2) driven into a denser liquid plunges, m, against its buoyancy
// flux `buoyancy` (g' times its volume flux, m^4/s^3): 2.46 M^(3/4) B^(-1/2) (Turner 1966's forced fountain)
double fountain_reach(double momentum, double buoyancy);

// (Stage 57) `dissipation`: the power stirring dissipates in the cell's liquid, W/kg: transport to a grain is Sano,
// Yamaguchi and Adachi's (1974), Sh = 2 + 0.4 (eps d^4 / nu^3)^(1/4) Sc^(1/3), 2 at rest
// (Stage 71) A surface reaction that carries no dissolved species (a pure liquid freezing, a solid melting) is
// limited instead by the heat its change gives out or takes in, carried between its surface and the liquid:
// Nu k / d (T - T_eq) / |dH| per m^2, T - T_eq = A T / dH, Nu as Sano's with Pr for Sc (2 at rest).
SurfaceSteps surface_steps(const SpeciesTable &table, const CellState &cell, const Reaction &reaction, double temperature, double a, double dissipation, const CellMeta *meta = nullptr);
// (Stage 87, GAME_SPEC §7) A solution law's rate, mol per kg of the cell's solvent per second, without its affinity
// factor: k(T) prod_j m_j^order_j times their activity factor (`rate_activity`, Brønsted and Bjerrum's), the molalities as
// `molality` reads them; 0 with no liquid.
double solution_rate(const SpeciesTable &table, const CellState &cell, const Kinetics &kinetics, double temperature);
// Its extent over a step of `seconds`, in quanta, in the direction `forward`: `factor` the affinity factor's size and
// `nu` the size of `in`'s coefficient. Forward, exact in `in`'s molality with the other species held (order 1:
// m e^(-nu k' t); order n: (m^(1-n) + (n-1) nu k' t)^(1/(1-n)), nought where that comes to nothing); backward, the
// rate as it is at the step's start.
double solution_extent(const SpeciesTable &table, const CellState &cell, const Kinetics &kinetics, int32_t nu, double rt, double factor, bool forward, double seconds);
// Primary nucleation per kg of solvent per second over `nucleation` (GAME_SPEC §9): (S - 1)^order, or (Stage 71) where
// the kinetics give a barrier, e^(-B / ln^2 S); `ln_s` = A / RT, 0 where it is not supersaturated
double primary_nucleation(const Kinetics &kinetics, double ln_s);

// (Stage 64) The world a builder states, each cell at its fast equilibria at its stated temperature: built, settled
// (`Simulation::equilibrate`), its settled amounts set again in the builder at the same temperatures, until settling
// moves no amount and no cell's temperature by more than 1e-6 K (at most 16 rounds; the last built). A bench's sulfuric acid, set
// as its ions, cooled 2.6 K at its first step as its bisulfate formed (Stage 63). The builder's other settings stand.
std::optional<World> build_settled(World::Builder builder, std::span<const Reaction> reactions, std::string *why = nullptr);

// (Stage 79) a piston's move in a step: its volume's change, m^3 outward, and the hand's force, N outward
struct PistonMove {
	double dv = 0.0, force = 0.0;
};

// (Stage 79, 89) A plunger's whole move in a step of `dt` with its space at `pressure` as the step ends, from the
// volume it began at: at the speed its forces give, between its stops; and the hand's force with it.
PistonMove piston_move(const Piston &p, double volume, double pressure, double dt);

namespace closure {
struct Closing; // (src/closure.cpp: a group of joined vessels as it is being closed)
}

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
	// The same into a trace kept from step to step, its buffers reused (Stage 68)
	void step(StepTrace &trace);
	// Advances one step without building its trace or checking it: the same
	// state, bit for bit, at a fraction of the cost.
	void advance();

	// (Stage 64) Every cell to its fast equilibria now, as a step settles, and nothing else: no time passes. What
	// `build_settled` reads a world's settled amounts by.
	void equilibrate();

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
	void restore_remainders(std::vector<double> heat, std::vector<double> reaction, std::vector<double> nucleation, std::vector<double> work = {});
	// what the work along each edge still owes, under a quantum (Stage 32): state, as the others are
	const std::vector<double> &work_remainders() const { return work_remainder_; }
	// the reactions of the equilibrium law (what invariant P1 is checked for)
	const std::vector<Reaction> &equilibria() const { return equilibria_; }
	// the reactions with a rate law, in the order the tally counts them
	const std::vector<Reaction> &rated() const { return reactions_; }
	// (Stage 89) how many times the closure did not bring a group's pressures within its tolerance (it then applies
	// nothing that round): none is expected; the tests ask. Not state.
	uint64_t closure_misses() const { return closure_misses_; }
	uint64_t closure_nested() const { return closure_nested_; } // (and how often Newton left it to the nested solve)

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
	// Puts back the last step's flows of a saved simulation (Stage 69): what was derived from them (a vessel's
	// boiling's bubbles, a reaction's rate) reads as it did before the save. Not state: the next step makes them
	// anew. Taken only where they fit the world's edges and cells (as many, or fewer: a command since may have added
	// some) and its reactions; else none, as before a step.
	void restore_flows(StepFlows flows);

	// A cell's temperature as World::temperature gives it (a reservoir's own, else its thermal state's), the same
	// value, from the thermal state the simulation keeps (Stage 72): derived again only when the cell's state or its
	// meta changed since, so the lab's walls and views read what the step derived
	bool temperature(CellId cell, double &kelvin) const;

private:
	void begin_step();
	const Thermal &thermal(CellId cell);
	const Thermal &kept_thermal(CellId cell) const;
	double column_kelvin(CellId cell); // a column's layer's temperature, T0 where it has none (strata.cpp)
	Refusal transfer(EdgeId edge, CellId from, std::span<const SpeciesAmount> amounts, Energy energy);
	Refusal transform(CellId cell, const Reaction &reaction, int64_t extent);
	void react();
	void conduct_heat();
	void do_work();
	void move_open_pistons(); // (Stage 79) an open vessel's plunger, by its forces at the room's pressure
	// (Stage 89, GAME_SPEC §6, Flow between vessels; src/closure.cpp) The closure: every sealed vessel with a plunger
	// or a conduit, with those it is joined to, at the pressure at which it is exactly full as the step ends: each
	// plunger's move and each conduit's crossing at those pressures, in rounds with the fast equilibria. true when
	// anything moved.
	bool close_vessels();
	bool close_group(const std::vector<CellId> &tops, const std::vector<size_t> &conduits, std::vector<CellId> &touched);
	bool close_pressures(closure::Closing &group) const;
	bool close_one(closure::Closing &group, size_t node) const;
	bool close_nested(closure::Closing &group, const std::vector<size_t> &free, size_t k) const;
	bool evaluate(closure::Closing &group, bool record) const;
	void drain_liquids();
	void exchange_gases(); // (Stage 50) each gas down its mole fraction along an edge that exchanges it
	void overflow();
	void settle();
	void build_tree();
	void settle_cells();
	bool project(CellId cell);
	bool solve_together(CellId cell); // (Stage 63) the solution's fast equilibria at once, an accelerator for project()
	bool fill(CellId cell, CellId up, EdgeId edge, bool may_draw = true);
	bool equalize(CellId cell, CellId up, EdgeId edge);
	// the vertical dimension (GAME_SPEC §6, Stage 47; src/strata.cpp): each vessel's column, top first, and the
	// edge from each layer up to the one over it
	struct Column {
		CellId top;
		std::vector<CellId> cells; // [0] the top, [1] the layer under it, ...
		std::vector<EdgeId> up; // up[i]: from cells[i + 1] to cells[i]
	};
	void find_columns();
	void grow_columns(); // at the step's start: a column with no spare layer that a steep step wants grows a block
	void configure_columns(); // at the step's start: the conductances between layers
	void stratify(); // in the step: gas up, solids down, layers merged, split, exchanging what is dissolved
	void stratify(const Column &column);
	std::vector<Column> columns_;
	uint64_t columns_version_ = UINT64_MAX;
	// (Stage 92) each cell's bubbles: the gas its reactions made last step rising through its liquid, W/kg (g u)
	std::vector<double> bubbling_;
	// (Stage 88, GAME_SPEC §6, Liquids that give) A sealed vessel sized: the one pressure at which its top holds what
	// its layers leave, each cell at it with the work of its change of volume (World::work); true when anything moved.
	// Each column's cells' revisions as it was last sized: one unchanged is sized already.
	bool size_vessel(const Column &column);
	void size_vessels();
	Refusal work(EdgeId edge, CellId from, Energy energy); // World::work, with the step's flows
	// (Stage 88) what was lifted out of a sealed vessel's layer into its top takes its volume with it: the layer's taken
	// volume less by `m3`, the top's more
	void lift_volume(CellId layer, CellId top, double m3);
	std::vector<std::vector<uint64_t>> sized_;

	World world_;
	std::vector<Reaction> reactions_; // with a rate law
	std::vector<Reaction> equilibria_; // of the equilibrium law
	std::vector<int> seen_; // project()'s scratch: the cell's version at each equilibrium's last probe
	// (Stage 81) each equilibrium's species it consumes forward and backward, as bits (a word every 64 species): one that
	// lacks one of each, either way, cannot move, and is passed without a call; and project()'s scratch, the cell's
	// species present
	std::vector<uint64_t> needs_forward_, needs_backward_, present_;
	size_t needs_words_ = 0;
	std::vector<int32_t> depth_, parent_, parent_edge_; // settle()'s tree, kept to spare the allocations
	std::vector<size_t> order_;
	// the sealed cells (Stage 28), each group breadth first from its first cell; a group's root has parent -1
	std::vector<size_t> sealed_order_;
	std::vector<std::pair<size_t, size_t>> sealed_groups_; // [begin, end) in sealed_order_
	bool tree_built_ = false;
	uint64_t tree_version_ = 0;
	std::vector<double> heat_remainder_;
	std::vector<double> work_remainder_; // per edge; empty while no edge has done work
	// what drained into each cell this step (Stage 51): its liquid's volume, m^3, and mass, kg (a column's jet)
	std::vector<double> landed_m3_, landed_kg_;
	std::vector<double> reaction_remainder_;
	std::vector<double> nucleation_remainder_;
	std::vector<int64_t> tally_; // cell * reactions + reaction
	StepFlows flows_;
	Process process_ = Process::Reaction; // what the primitives are applied for now
	std::vector<uint8_t> starved_;
	std::vector<Exchanged> exchanged_; // the step's (Stage 50)
	std::vector<Crossed> crossed_; // the step's crossings (Stage 89)
	mutable uint64_t closure_misses_ = 0, closure_nested_ = 0;
	bool timeless_ = false; // settling with no time passing (`equilibrate`): the closure stands
	mutable std::vector<Thermal> thermal_; // per cell, while thermal_valid_ (or its key holds: Stage 72)
	std::vector<uint8_t> thermal_valid_;
	bool in_step_ = false; // (Stage 72) thermal_valid_ is this step's: between begin_step's sizing and the step's close
	// what each cell's kept thermal state was derived from (Stage 53): the same state and meta give it again, so a
	// cell at rest (a vessel's ring of glass) is not derived anew each step. (Stage 75) By the cell's revision
	// (World::revision): the same revision is the same state and meta. Where an assertion aborts (Debug, the
	// sanitizers) the state and meta are kept too, and a revision that held while they changed (a write that drew none)
	// aborts.
	struct ThermalKey {
		bool known = false;
		uint64_t revision = 0;
#if CHEM_ASSERT_ABORTS
		int64_t energy = 0;
		std::vector<int64_t> amount;
		double volume = 0.0;
		bool sealed = false, rigid = false, reservoir = false;
#endif
		bool same(const World &world, CellId cell) const;
		void keep(const World &world, CellId cell);
	};
	mutable std::vector<ThermalKey> thermal_key_;
	// (Stage 75) each cell's sums over its matter (matter_sums), by its matter revision: a cell that only took or gave
	// energy (conduction) has its temperature from them, the same value
	struct KeptSums {
		bool known = false;
		uint64_t revision = 0;
		MatterSums sums;
	};
	mutable std::vector<KeptSums> sums_;
	// (Stage 75) each cell's mixtures (cell_mixtures), by its matter revision: its probes and its range read them
	struct KeptMixtures {
		bool known = false;
		uint64_t revision = 0;
		CellMixtures mixtures;
	};
	std::vector<KeptMixtures> mixtures_;
	const CellMixtures &mixtures(CellId cell);
	// (Stage 64) each cell's state when project() last found nothing to move: the same state is at rest again, exactly
	std::vector<ThermalKey> rest_key_;
	// each edge's 1 - exp(-G h / C) as last worked out, and for which G and C (Stage 53: the same again, exactly)
	struct Relaxing {
		double conductance = -1.0, capacity = -1.0, factor = 0.0;
	};
	std::vector<Relaxing> relaxing_;
	double step_seconds_ = STEP_SECONDS;
	bool traced_ = true; // the last step was step(), not advance()
	bool checking_;
};

} // namespace chem
