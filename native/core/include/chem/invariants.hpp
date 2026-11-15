#pragma once

#include "chem/species.hpp"
#include "chem/world.hpp"

#include <array>
#include <cstdint>
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

// The thermal invariants: they apply to a trace of a world with temperatures
// (trace.thermal), and return nothing otherwise.

// T1. Every process runs down its own driving force, as the cells were when
// it was applied (the ledger replayed from the step's start). Heat flows from
// the hotter cell to the colder: for a transfer of energy alone,
// energy * (T_from - T_to) > 0. A reaction advances only while its affinity
// is positive and retreats only while it is negative: extent * A > 0.
Violations check_T1(const SpeciesTable &table, const StepTrace &trace);
// T2. The world's entropy does not decrease: the sum over the cells that
// changed of (S_after - S_before) >= -(1e-9 J/K + 4e-16 * the sum of their |S|),
// the bound of the float64 rounding in the sum.
Violations check_T2(const SpeciesTable &table, const StepTrace &trace);
// T3. Every cell has a temperature: thermodynamic data for every species it
// holds, a positive heat capacity, and T > 0.
Violations check_T3(const SpeciesTable &table, const StepTrace &trace);
// S1. No NaN or infinity: every temperature and entropy of the trace is finite.
Violations check_S1(const SpeciesTable &table, const StepTrace &trace);

// The change of the world's entropy over the step, in J/K (what T2 bounds).
double entropy_change(const SpeciesTable &table, const StepTrace &trace);

// Every check, in the order above. A trace of the wrong shape (cell or species
// counts that disagree) yields one "SHAPE" violation and nothing else.
Violations check_all(const SpeciesTable &table, const StepTrace &trace);

} // namespace chem
