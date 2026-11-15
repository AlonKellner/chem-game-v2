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
	bool operator==(const CellState &) const = default;
};

struct Snapshot {
	std::vector<CellState> cells;
	bool operator==(const Snapshot &) const = default;
};

// A connection between two cells: what may pass. Heat passes any open edge.
struct Edge {
	CellId a;
	CellId b;
	PhaseMask phases = PHASES_NONE;
	bool open = true; // false once disconnected (ids are never reused)
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
};

// One step as the invariants see it (chem/invariants.hpp).
struct StepTrace {
	uint64_t step = 0;
	Snapshot before;
	Snapshot after;
	std::vector<LedgerEntry> ledger;
	std::vector<Edge> edges; // the topology during the step
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
};
const char *refusal_text(Refusal r);

class World {
public:
	// The world's initial conditions. Matter and energy enter a world here and
	// nowhere else.
	class Builder {
	public:
		explicit Builder(const SpeciesTable &table);
		CellId add_cell(std::string name);
		EdgeId add_edge(CellId a, CellId b, PhaseMask phases);
		Builder &set_amount(CellId cell, SpeciesId species, Amount amount);
		Builder &set_energy(CellId cell, Energy energy);
		// Refuses (nullopt, the reason in *why): an unknown cell or species, a
		// negative amount, an edge from a cell to itself, totals that do not
		// fit the integer range.
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
		const SpeciesTable *table_;
		std::vector<std::string> names_;
		std::vector<Edge> edges_;
		std::vector<Set> amounts_;
		std::vector<SetEnergy> energies_;
	};

	const SpeciesTable &species() const { return *table_; }
	int cell_count() const { return static_cast<int>(cells_.size()); }
	const std::string &cell_name(CellId cell) const { return names_[static_cast<size_t>(cell.index)]; }
	Amount amount(CellId cell, SpeciesId species) const;
	Energy energy(CellId cell) const;
	std::span<const Edge> edges() const { return edges_; }
	Snapshot snapshot() const { return Snapshot{ cells_ }; }
	uint64_t step() const { return step_; }

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

	// The topology changes between steps only (GAME_SPEC §4.5, step 1).
	Refusal connect(CellId a, CellId b, PhaseMask phases, EdgeId *made = nullptr);
	Refusal disconnect(EdgeId edge);

	// Ends the step: the trace of everything since the last close (or since
	// the world was built), then the next step begins.
	StepTrace close_step();

private:
	World() = default;
	bool known(CellId cell) const { return cell.index >= 0 && cell.index < cell_count(); }

	const SpeciesTable *table_ = nullptr;
	std::vector<std::string> names_;
	std::vector<CellState> cells_;
	std::vector<Edge> edges_;
	std::vector<LedgerEntry> ledger_;
	Snapshot before_;
	uint64_t step_ = 0;
};

} // namespace chem
