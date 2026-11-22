#pragma once

#include "chem/quantity.hpp"
#include "chem/species.hpp"

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

// The world: homogeneous cells that store only conserved quantities, and the
// two primitives that change them (GAME_SPEC §4.2, §4.3). Cell state is private
// to the World; `transfer` and `transform` are the only ways to change it, and
// neither can create or destroy matter or energy.
namespace chem {

struct CellId {
	int32_t index = -1;
	constexpr bool operator==(const CellId &) const = default;
};

struct EdgeId {
	int32_t index = -1;
	constexpr bool operator==(const EdgeId &) const = default;
};

// A copy of one cell's state: plain data, for traces, tests and saves.
struct CellState {
	std::vector<int64_t> amount; // quanta of each species of the table
	int64_t energy = 0; // quanta
	// How many particles each solid is in (GAME_SPEC §9): a powder's grains, a
	// crop of crystals. State, but not a conserved quantity: it changes by
	// nucleation and when a solid is used up, outside the two primitives. One
	// per species; empty in a state built by hand.
	std::vector<int64_t> particles = {};
	bool operator==(const CellState &) const = default;
};

struct Snapshot {
	std::vector<CellState> cells;
	bool operator==(const Snapshot &) const = default;
};

// A connection between two cells: what may pass, and how well it conducts heat.
struct Edge {
	CellId a;
	CellId b;
	PhaseMask phases = PHASES_NONE;
	bool open = true; // false once disconnected (ids are never reused)
	double conductance = 0.0; // W/K (GAME_SPEC §5: UA); 0 conducts no heat
	// A drain (GAME_SPEC §6): each step, the liquid of `a` and what is
	// dissolved in it run down into `b` (a condenser's outlet over a beaker).
	bool drain = false;
	// A drain's rate (Stage 27: a pour): at most this volume of liquid a
	// second (m^3/s), and with it the same share of the solids where the
	// edge passes them; below 0, all of it each step.
	double rate = -1.0;
};

// What a cell is, beside its state. A reservoir is an energy account at a
// fixed temperature (GAME_SPEC §4.2): it holds no matter, and its energy is
// what it has received (negative: what it has given).
//
// A cell of matter is elastic or rigid (GAME_SPEC §6, the demo's subset: every
// cell at one atmosphere). An elastic cell (a balloon, the open air) takes the
// volume its contents need. A rigid cell (a flask) has a fixed volume: gas
// leaves or enters it along its gas edges so that it is exactly full, and it
// must always have a gas path to an elastic cell.
struct CellMeta {
	bool reservoir = false;
	double reservoir_temperature = 0.0; // K
	bool rigid = false;
	double volume = 0.0; // m^3, of a rigid cell
	// How well the cell is stirred (GAME_SPEC §7): 1 at rest. It multiplies
	// the rates that transport limits (the surface law), never an equilibrium.
	double mixing = 1.0;
};

struct SpeciesAmount {
	SpeciesId species;
	Amount amount;
};

// One call of a primitive, as it was applied (GAME_SPEC §4.3: the ledger).
struct LedgerEntry {
	enum class Kind : uint8_t { Transfer, Transform };
	Kind kind = Kind::Transfer;
	EdgeId edge; // Transfer
	CellId from; // Transfer: the cell that gave; Transform: the cell
	CellId to; // Transfer: the cell that received
	// Transfer: what moved from -> to (each amount > 0).
	// Transform: the change of each species (signed: nu * extent).
	std::vector<SpeciesAmount> delta;
	Energy energy; // Transfer: the energy moved from -> to (signed)
	int64_t extent = 0; // Transform: the extent applied (delta = nu * extent)
};

// One step as the invariants see it (chem/invariants.hpp).
struct StepTrace {
	uint64_t step = 0;
	Snapshot before;
	Snapshot after;
	std::vector<LedgerEntry> ledger;
	std::vector<Edge> edges; // the topology during the step
	std::vector<CellMeta> meta; // one per cell
	bool thermal = false; // the world has temperatures: the thermal invariants apply
	// One per cell, set by the simulation: nonzero when a rigid cell was left
	// under-full this step, because the cell it draws from held no more gas,
	// or because a fast reaction in it consumed gas faster than eight fillings
	// a step (a partial vacuum, which the demo's subset does not model).
	std::vector<uint8_t> starved;
};

// Why a call changed nothing.
enum class Refusal : int32_t {
	None = 0,
	UnknownCell,
	UnknownEdge, // no such edge, or it was disconnected
	NotOnEdge, // the giving cell is not an end of the edge
	UnknownSpecies,
	PhaseNotAllowed, // the edge does not let this species' phase through
	NegativeAmount,
	Insufficient, // the cell does not hold that much
	Overflow, // the result does not fit the integer range
	ForeignReaction, // a reaction of another species table
	MidStep, // the topology changes at a step boundary only
	SameCell, // an edge joins two different cells
	Reservoir, // a reservoir holds energy, never matter
	BadValue, // a number that is negative or not finite, or a particle count for what is not a solid
	Wall, // a vessel's own material does not move
	WouldSeal, // a rigid cell would lose its last gas path to an elastic cell
};
const char *refusal_text(Refusal r);

// The rigid cells that have no path, along open edges that let gas through,
// to an elastic cell of matter (GAME_SPEC §6: V3). Empty when none is sealed.
std::vector<CellId> sealed_cells(std::span<const CellMeta> meta, std::span<const Edge> edges);

class World {
public:
	// The world's initial conditions. Matter and energy enter a world here and
	// nowhere else.
	class Builder {
	public:
		explicit Builder(const SpeciesTable &table);
		CellId add_cell(std::string name); // elastic
		// a cell of fixed volume, in m^3
		CellId add_rigid_cell(std::string name, double volume);
		// an energy account at a fixed temperature (K); makes the world thermal
		CellId add_reservoir(std::string name, double temperature);
		EdgeId add_edge(CellId a, CellId b, PhaseMask phases, double conductance = 0.0);
		// a liquid edge down which `from`'s liquid runs into `to` each step
		EdgeId add_drain(CellId from, CellId to);
		// an edge exactly as given: closed, a drain, any conductance (for a saved world)
		EdgeId add_edge_as(const Edge &edge);
		// Where what overflows goes (GAME_SPEC §6): an elastic cell. With one,
		// a rigid cell that holds more liquid than fits spills the excess into it.
		Builder &set_spill(CellId cell);
		Builder &set_mixing(CellId cell, double mixing);
		// the step the world resumes at (a saved world)
		Builder &set_step(uint64_t step);
		Builder &set_amount(CellId cell, SpeciesId species, Amount amount);
		Builder &set_energy(CellId cell, Energy energy);
		// how many particles a solid is in (default: one lump)
		Builder &set_particles(CellId cell, SpeciesId species, int64_t count);
		// The cell's energy is the enthalpy of its amounts at this temperature
		// (K), to the nearest quantum; makes the world thermal. Overrides
		// set_energy for the cell.
		Builder &set_temperature(CellId cell, double temperature);
		// Refuses (nullopt, the reason in *why): an unknown cell or species, a
		// negative amount, an edge from a cell to itself, a conductance that is
		// negative or not finite, matter in a reservoir, a temperature that is
		// not positive and finite or set in a cell that cannot have one (a
		// species without thermodynamic data, no heat capacity), totals that
		// do not fit the integer range. In a thermal world every cell must
		// have a temperature, and a cell with a dissolved species a liquid. A
		// particle count for a species that is not a solid, or less than one
		// for a solid that is there. A rigid cell: a volume that is not positive and
		// finite, a condensed species of unknown molar volume, contents that do
		// not fit, no gas path to an elastic cell.
		std::optional<World> build(std::string *why = nullptr) const;

	private:
		struct Set {
			CellId cell;
			SpeciesId species;
			Amount amount;
		};
		struct SetEnergy {
			CellId cell;
			Energy energy;
		};
		struct SetTemperature {
			CellId cell;
			double temperature;
		};
		const SpeciesTable *table_;
		std::vector<std::string> names_;
		std::vector<CellMeta> meta_;
		std::vector<SetTemperature> temperatures_;
		std::vector<Set> particles_; // amount: the count
		CellId spill_;
		uint64_t step_ = 0;
		std::vector<Edge> edges_;
		std::vector<Set> amounts_;
		std::vector<SetEnergy> energies_;
	};

	const SpeciesTable &species() const { return *table_; }
	int cell_count() const { return static_cast<int>(cells_.size()); }
	const std::string &cell_name(CellId cell) const { return names_[static_cast<size_t>(cell.index)]; }
	Amount amount(CellId cell, SpeciesId species) const;
	Energy energy(CellId cell) const;
	int64_t particles(CellId cell, SpeciesId species) const;
	const CellMeta &meta(CellId cell) const { return meta_[static_cast<size_t>(cell.index)]; }
	// true when the world has temperatures (a reservoir, or a temperature set when built)
	bool thermal() const { return thermal_; }
	// The cell's temperature in K: a reservoir's fixed one, else derived from
	// its state (chem/thermo.hpp). false when it has none.
	bool temperature(CellId cell, double &kelvin) const;
	std::span<const Edge> edges() const { return edges_; }
	Snapshot snapshot() const { return Snapshot{ cells_ }; }
	// true when a primitive has been applied since the last step closed
	bool mid_step() const { return !ledger_.empty(); }
	// changes whenever an edge is added, opened or closed (what is cached per topology is keyed on it)
	uint64_t topology_version() const { return topology_version_; }
	// one cell's state, to read (the processes derive temperatures from it)
	const CellState &snapshot_cell(CellId cell) const { return cells_[static_cast<size_t>(cell.index)]; }
	uint64_t step() const { return step_; }
	// where what overflows goes, or an invalid id
	CellId spill() const { return spill_; }

	// Primitive 1. Moves `amounts` and `energy` from `from` to the other end of
	// `edge`: one cell loses exactly what the other gains. `energy` may be
	// negative (heat flowing toward `from`). Refused, with the state untouched,
	// unless the edge is open and joins `from`, every species' phase may pass,
	// `from` holds every amount, and nothing overflows.
	Refusal transfer(EdgeId edge, CellId from, std::span<const SpeciesAmount> amounts, Energy energy);

	// Primitive 2. Applies amount += nu * extent to every species of a balanced
	// reaction inside one cell, energy unchanged. A negative extent runs it
	// backwards. Refused, with the state untouched, unless the cell holds what
	// is consumed and nothing overflows.
	Refusal transform(CellId cell, const Reaction &reaction, int64_t extent);

	// The topology changes between steps only (GAME_SPEC §4.5, step 1), except
	// that an edge may be added at any time: a step's trace holds the edges as
	// they are at its end, and one added during it is among them.
	Refusal connect(CellId a, CellId b, PhaseMask phases, EdgeId *made = nullptr, double conductance = 0.0);
	Refusal connect_drain(CellId from, CellId to, EdgeId *made = nullptr);
	// A drain's rate (m^3 of liquid a second; below 0, all of it), and whether
	// its solids go too (GAME_SPEC §6, Stage 27: a pour). Between steps.
	Refusal set_drain(EdgeId edge, double rate, bool solids);
	// refused (WouldSeal) when a rigid cell would be left with no gas path to an elastic cell
	Refusal disconnect(EdgeId edge);
	Refusal set_conductance(EdgeId edge, double conductance);
	// stirring: 1 at rest; between steps only
	Refusal set_mixing(CellId cell, double mixing);
	// A solid's particle count: not conserved, so not a primitive. The
	// primitives keep it at least one where the solid is and zero where it is
	// not; this sets how many. Refused (BadValue) for a species that is not a
	// solid, less than one where the solid is, or other than zero where it is not.
	Refusal set_particles(CellId cell, SpeciesId species, int64_t count);

	// Ends the step: the trace of everything since the last close (or since
	// the world was built), then the next step begins.
	StepTrace close_step();
	// Ends the step without building its trace (no copies of the state). The
	// next close_step() after one of these must follow a rebase().
	void skip_step();
	// Makes the present state the start of the step in progress, for a trace
	// after skipped steps. Only between steps.
	void rebase();

private:
	World() = default;
	bool known(CellId cell) const { return cell.index >= 0 && cell.index < cell_count(); }
	void tend_particles(CellState &cell, SpeciesId species);

	const SpeciesTable *table_ = nullptr;
	std::vector<std::string> names_;
	std::vector<CellMeta> meta_;
	bool thermal_ = false;
	std::vector<CellState> cells_;
	std::vector<Edge> edges_;
	std::vector<LedgerEntry> ledger_;
	Snapshot before_;
	uint64_t step_ = 0;
	uint64_t topology_version_ = 0;
	CellId spill_;
};

} // namespace chem
