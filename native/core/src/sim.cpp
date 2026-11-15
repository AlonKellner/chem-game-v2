#include "chem/sim.hpp"

#include "chem/error.hpp"
#include "chem/invariants.hpp"
#include "chem/math.hpp"
#include "chem/thermo.hpp"

#include <cstdint>
#include <utility>

namespace chem {

Simulation::Simulation(World world) :
		world_(std::move(world)), checking_(CHEM_ASSERT_ABORTS != 0) {}

// Heat conduction (GAME_SPEC §5). Each open edge with a conductance relaxes
// its two cells toward each other by the exact solution of the two-body
// problem over the step:
//
//   Q = Ceff (Ta - Tb) (1 - e^(-UA h / Ceff)),   Ceff = Ca Cb / (Ca + Cb)
//
// (a reservoir's capacity is infinite: Ceff is the other cell's). The edges
// are relaxed in index order, each from the state the previous one left: any
// conductance and any step are stable, and each application moves heat from
// the hotter cell to the colder (T1) without passing their common temperature.
//
// Q becomes whole quanta through the edge's carried remainder. The remainder
// is dropped when the temperature difference changes sign, and the transfer
// is capped at the whole quanta of Ceff (Ta - Tb), the heat that would
// equalise the pair; below one quantum of that, nothing moves and nothing is
// carried, so a world at rest stays bit-identical (D3).
void Simulation::conduct_heat() {
	const std::span<const Edge> edges = world_.edges();
	heat_remainder_.resize(edges.size(), 0.0);
	const SpeciesTable &table = world_.species();
	for (size_t i = 0; i < edges.size(); ++i) {
		const Edge edge = edges[i];
		double &remainder = heat_remainder_[i];
		if (!edge.open || !(edge.conductance > 0.0)) {
			remainder = 0.0;
			continue;
		}
		const CellMeta &ma = world_.meta(edge.a);
		const CellMeta &mb = world_.meta(edge.b);
		double ta = 0.0, tb = 0.0;
		if (!world_.temperature(edge.a, ta) || !world_.temperature(edge.b, tb)) {
			remainder = 0.0; // a cell without a temperature conducts nothing (T3 reports it)
			continue;
		}
		const double dt = ta - tb;
		double heat = 0.0; // J, from a to b
		double equalising = 0.0; // J; unused between two reservoirs
		const bool bounded = !(ma.reservoir && mb.reservoir);
		if (!bounded) {
			heat = edge.conductance * dt * STEP_SECONDS;
		} else {
			const double ca = ma.reservoir ? 0.0 : thermal_state(table, world_.snapshot_cell(edge.a)).heat_capacity;
			const double cb = mb.reservoir ? 0.0 : thermal_state(table, world_.snapshot_cell(edge.b)).heat_capacity;
			const double ceff = ma.reservoir ? cb : mb.reservoir ? ca : ca * cb / (ca + cb);
			equalising = ceff * dt;
			heat = equalising * (1.0 - exp(-edge.conductance * STEP_SECONDS / ceff));
		}
		const double quanta = heat / JOULE_PER_QUANTUM;
		if ((remainder > 0.0 && quanta < 0.0) || (remainder < 0.0 && quanta > 0.0) || quanta == 0.0) {
			remainder = 0.0;
		}
		const double total = quanta + remainder;
		// One step moves at most 1e18 quanta (1e9 J) along an edge: the range
		// of the energy integers (GAME_SPEC §3), far beyond any lab.
		constexpr double MOST = 1.0e18;
		const bool within = total > -MOST && total < MOST;
		int64_t whole = static_cast<int64_t>(within ? total : total < 0.0 ? -MOST : MOST); // toward zero
		remainder = within ? total - static_cast<double>(whole) : 0.0;
		if (bounded) {
			const double limit = (equalising < 0.0 ? -equalising : equalising) / JOULE_PER_QUANTUM;
			const int64_t cap = limit < 9.0e18 ? static_cast<int64_t>(limit) : INT64_MAX;
			if (cap == 0) {
				whole = 0;
				remainder = 0.0;
			} else if (whole > cap) {
				whole = cap;
				remainder = 0.0;
			} else if (whole < -cap) {
				whole = -cap;
				remainder = 0.0;
			}
		}
		if (whole != 0) {
			// Overflow: an energy account at the end of the integer range takes
			// no more; the heat stays where it is.
			const Refusal refused = world_.transfer(EdgeId{ static_cast<int32_t>(i) }, edge.a, {}, Energy{ whole });
			CHEM_ASSERT(refused == Refusal::None || refused == Refusal::Overflow, refusal_text(refused));
			if (refused != Refusal::None) {
				remainder = 0.0;
			}
		}
	}
}

StepTrace Simulation::step() {
	if (world_.thermal()) {
		conduct_heat();
	}
	StepTrace trace = world_.close_step();
	if (checking_) {
		const Violations violations = check_all(world_.species(), trace);
		CHEM_ASSERT(violations.empty(), violations.empty() ? "" : violations[0].c_str());
	}
	return trace;
}

} // namespace chem
