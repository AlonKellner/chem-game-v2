#pragma once

#include "chem/species.hpp"
#include "chem/world.hpp"

#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

// The invariant kernel (GAME_SPEC §18): each invariant is a pure function over
// a step's trace, returning one message per violation, prefixed with its ID.
// The primitives make these hold by construction; the checks are the
// independent witness, and native/tests/test_invariants.cpp proves each one
// fires on a corrupted trace.
namespace chem {

// A sum of amount * molar mass, exactly: value = hi * 2^32 + lo, 0 <= lo < 2^32,
// in units of 1e-18 g (an amount quantum of 1e-12 mol times 1e-6 g/mol). The
// products exceed an int64, so the sum is kept in two limbs.
struct ExactMass {
	int64_t hi = 0;
	int64_t lo = 0;
	constexpr bool operator==(const ExactMass &) const = default;
	// adds quanta * mass_ug (0 <= mass_ug < 2^30)
	constexpr void add(int64_t quanta, int64_t mass_ug) {
		const int64_t a = quanta >> 32; // arithmetic shift: quanta = a * 2^32 + b
		const int64_t b = quanta & 0xffffffff;
		lo += b * mass_ug;
		hi += a * mass_ug + (lo >> 32);
		lo &= 0xffffffff;
	}
	constexpr double grams() const { return (static_cast<double>(hi) * 4294967296.0 + static_cast<double>(lo)) * 1e-18; }
};

// What a closed world keeps: the atoms of each element, the mass, the charge
// and the energy, summed over every cell.
struct Totals {
	std::array<int64_t, MAX_ELEMENTS> atoms{}; // quanta of atoms of each element
	ExactMass mass;
	int64_t charge = 0; // quanta of elementary charge
	int64_t energy = 0; // quanta
	bool operator==(const Totals &) const = default;
};

// The totals of a snapshot. Sums wrap modulo 2^64, so a corrupted snapshot is
// compared, not undefined; a world's own totals fit (World::Builder::build).
Totals totals(const SpeciesTable &table, const Snapshot &snapshot);
// true when every total fits an int64 without wrapping
bool totals_fit(const SpeciesTable &table, const Snapshot &snapshot);

using Violations = std::vector<std::string>;

// C1. The world's total of each element is constant.
Violations check_C1(const SpeciesTable &table, const StepTrace &trace);
// C2. The world's mass is constant.
Violations check_C2(const SpeciesTable &table, const StepTrace &trace);
// C3. The world's energy is constant.
Violations check_C3(const SpeciesTable &table, const StepTrace &trace);
// C4. The world's charge is constant.
Violations check_C4(const SpeciesTable &table, const StepTrace &trace);
// C5. Ledger closure: the step's primitive calls sum to its state change.
Violations check_C5(const SpeciesTable &table, const StepTrace &trace);
// C6. Every amount is non-negative.
Violations check_C6(const SpeciesTable &table, const StepTrace &trace);
// C7. A transfer runs along an open edge, between its two cells, and moves only
// phases the edge lets through.
Violations check_C7(const SpeciesTable &table, const StepTrace &trace);
// C8. A transform's change is balanced in every element and in charge.
Violations check_C8(const SpeciesTable &table, const StepTrace &trace);
// The totals of what crossed the world's boundary (Stage 61), summed and taken away exactly (as a world's own totals
// wrap, so do these: a corrupted ledger is compared, not undefined)
Totals plus(const Totals &a, const Totals &b);
Totals minus(const Totals &a, const Totals &b);
// C9 (Stage 61: the world open at its boundary). What the world holds is what it began with, plus what was supplied
// to it, less what was retired from it: every element, the mass, the charge and the energy, exactly.
Violations check_C9(const Totals &start, const Totals &supplied, const Totals &retired, const Totals &now);

// The thermal invariants: they apply to a trace of a world with temperatures
// (trace.thermal), and return nothing otherwise.

// T1. Every process runs down its own driving force, as the cells were when
// it was applied (the ledger replayed from the step's start). Heat flows from
// the hotter cell to the colder: for a transfer of energy alone,
// energy * (T_from - T_to) > 0. A reaction advances only while its affinity
// is positive and retreats only while it is negative: extent * A > 0. Gas
// moves only out of a rigid cell that is over-full or into one that is
// under-full; between cells sealed together, only from the higher pressure
// to the lower (Stage 28).
Violations check_T1(const SpeciesTable &table, const StepTrace &trace);
// T2. The world's entropy does not decrease: the sum over the cells that
// changed of (S_after - S_before) >= -(1e-9 J/K + 4e-16 * the sum of their |S|),
// the bound of the float64 rounding in the sum.
Violations check_T2(const SpeciesTable &table, const StepTrace &trace);
// T3. Every cell has a temperature: thermodynamic data for every species it
// holds, a positive heat capacity, and T > 0.
Violations check_T3(const SpeciesTable &table, const StepTrace &trace);
// X1. A solid has particles exactly when it is there: count > 0 where its
// amount is > 0, 0 where it is 0 (states built by hand, with no counts, pass).
Violations check_X1(const SpeciesTable &table, const StepTrace &trace);
// X2. A cell that holds a dissolved species holds a liquid to hold it.
Violations check_X2(const SpeciesTable &table, const StepTrace &trace);
// S1. No NaN or infinity: every temperature and entropy of the trace is finite.
Violations check_S1(const SpeciesTable &table, const StepTrace &trace);

// V1. An open rigid cell is exactly full: gas + solids and liquids = its volume,
// within the volume of one quantum of each gas species plus 1e-9 of the cell's.
// Under-full is allowed only where the trace marks the cell starved.
Violations check_V1(const SpeciesTable &table, const StepTrace &trace);
// V3. A rigid cell is marked sealed exactly when it has no path, along open
// gas edges, to an elastic cell (Stage 28: the world keeps the mark), and a
// sealed cell that holds gas leaves it room: its pressure is defined.
Violations check_V3(const SpeciesTable &table, const StepTrace &trace);

// V2. No rigid cell holds more solids and liquids than its volume.
Violations check_V2(const SpeciesTable &table, const StepTrace &trace);
// P1. Every cell is at its fast equilibria. For each reaction of the
// equilibrium law: nothing is left to go in the direction of its affinity
// (liquid absent: its vapour at or below saturation; vapour absent: nothing
// would condense), or the affinity is within what rounding leaves. Rounding:
// gas moved after the equilibrium was taken is whole quanta, a quantum per
// species (N = the table's species + 1), which shifts the affinity by up to
// 2 N R T / (the cell's gas in quanta); or N quanta of the reaction itself
// spend it.
Violations check_P1(const SpeciesTable &table, const StepTrace &trace, std::span<const Reaction> equilibria);

// The change of the world's entropy over the step, in J/K (what T2 bounds).
double entropy_change(const SpeciesTable &table, const StepTrace &trace);

// L1 (Stage 47): a vessel's column holds together: the layers in use are one run under its top (nothing under an
// empty layer), and the top is no larger than the vessel, nor does the column hold more than fits in it.
Violations check_L1(const SpeciesTable &table, const StepTrace &trace);

// E1 (Stage 50): a gas exchanged along an edge (an open vessel's mouth) moves only down its mole fraction's difference,
// along an open edge that exchanges gas and joins the two cells, and no further than to equal fractions.
Violations check_E1(const SpeciesTable &table, const StepTrace &trace);

// Every check, in the order above. A trace of the wrong shape (cell or species
// counts that disagree) yields one "SHAPE" violation and nothing else.
// `equilibria`: the reactions of the equilibrium law the simulation runs (P1).
Violations check_all(const SpeciesTable &table, const StepTrace &trace, std::span<const Reaction> equilibria = {});

} // namespace chem
