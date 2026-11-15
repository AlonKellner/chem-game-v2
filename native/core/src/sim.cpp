#include "chem/sim.hpp"

#include "chem/error.hpp"
#include "chem/invariants.hpp"
#include "chem/math.hpp"
#include "chem/thermo.hpp"

#include <cstdint>
#include <utility>

namespace chem {

Simulation::Simulation(World world, std::vector<Reaction> reactions) :
		world_(std::move(world)), checking_(CHEM_ASSERT_ABORTS != 0) {
	for (Reaction &r : reactions) {
		if (r.kinetics().law != Kinetics::Law::None && r.table() == &world_.species()) {
			reactions_.push_back(std::move(r));
		}
	}
}

namespace {

// The most a reaction can advance (forward) or retreat (!forward) before a
// species it consumes runs out, in quanta of extent.
int64_t feasible(const CellState &cell, const Reaction &reaction, bool forward) {
	int64_t most = INT64_MAX;
	for (const Term &t : reaction.terms()) {
		const int64_t per = forward ? -t.nu : t.nu; // > 0 for the species this direction consumes
		if (per > 0) {
			const int64_t can = cell.amount[static_cast<size_t>(t.species.index)] / per;
			most = can < most ? can : most;
		}
	}
	return most;
}

// true when, after `extent` of the reaction at fixed enthalpy, the cell still
// has a temperature and the affinity has not passed zero.
bool keeps_direction(const SpeciesTable &table, const CellState &cell, const Reaction &reaction, int64_t extent, bool forward) {
	CellState trial = cell;
	for (const Term &t : reaction.terms()) {
		trial.amount[static_cast<size_t>(t.species.index)] += static_cast<int64_t>(t.nu) * extent;
	}
	const Thermal thermal = thermal_state(table, trial);
	if (!thermal.defined || !(thermal.temperature > 0.0)) {
		return false;
	}
	bool defined = false;
	const double a = affinity(table, trial, reaction.terms(), thermal.temperature, defined);
	return defined && (forward ? a >= 0.0 : a <= 0.0);
}

} // namespace

// Reactions (GAME_SPEC §7). In each cell, each reaction in order, from the
// state the last one left:
//
//   extent = (n_in / |nu_in|) (1 - e^(-k |f| h)) sign(f),   k = prefactor e^(-Ea / RT)
//
// the exact solution of first-order decay over the step, where f is the
// affinity factor: 1 - e^(-A/RT) for A > 0, -(1 - e^(A/RT)) for A < 0. It is
// in [-1, 1], has the sign of the affinity A and vanishes with it, so the
// reaction runs down its own driving force (T1), stops at equilibrium and
// runs backwards past it.
//
// The extent becomes whole quanta through the carried remainder (dropped when
// the direction changes). It is capped at what the cell can supply, and at
// the last quantum before the affinity would change sign, found by bisection
// on trial states: a reaction never passes its own equilibrium, so each
// application raises the entropy (T2), and at equilibrium nothing moves and
// nothing is carried (D3).
void Simulation::react() {
	const SpeciesTable &table = world_.species();
	const auto cells = static_cast<size_t>(world_.cell_count());
	reaction_remainder_.resize(cells * reactions_.size(), 0.0);
	for (size_t c = 0; c < cells; ++c) {
		const CellId cell{ static_cast<int32_t>(c) };
		if (world_.meta(cell).reservoir) {
			continue;
		}
		for (size_t r = 0; r < reactions_.size(); ++r) {
			const Reaction &reaction = reactions_[r];
			const Kinetics &kinetics = reaction.kinetics();
			double &remainder = reaction_remainder_[c * reactions_.size() + r];
			const CellState &state = world_.snapshot_cell(cell);
			const Thermal thermal = thermal_state(table, state);
			bool defined = false;
			const double a = thermal.defined && thermal.temperature > 0.0 ? affinity(table, state, reaction.terms(), thermal.temperature, defined) : 0.0;
			const int64_t have = state.amount[static_cast<size_t>(kinetics.in.index)];
			if (!defined || a == 0.0 || a != a || have == 0) {
				remainder = 0.0;
				continue;
			}
			const bool forward = a > 0.0;
			const double rt = GAS_CONSTANT * thermal.temperature;
			const double factor = 1.0 - exp(forward ? -a / rt : a / rt); // |f|
			const double rate = kinetics.prefactor * exp(-kinetics.activation / rt);
			const double wanted = static_cast<double>(have) / static_cast<double>(-reaction.nu(kinetics.in)) * (1.0 - exp(-rate * factor * step_seconds_));
			if ((remainder > 0.0) != forward) {
				remainder = 0.0;
			}
			const double total = (forward ? wanted : -wanted) + remainder;
			int64_t whole = static_cast<int64_t>(total); // toward zero; |total| <= have + 1
			remainder = total - static_cast<double>(whole);
			const int64_t most = feasible(state, reaction, forward);
			if ((forward ? whole : -whole) > most) {
				whole = forward ? most : -most;
				remainder = 0.0;
			}
			if (whole != 0 && !keeps_direction(table, state, reaction, whole, forward)) {
				int64_t keeps = 0; // an extent that keeps the direction
				int64_t passes = forward ? whole : -whole; // one that does not
				while (passes - keeps > 1) {
					const int64_t middle = keeps + (passes - keeps) / 2;
					if (keeps_direction(table, state, reaction, forward ? middle : -middle, forward)) {
						keeps = middle;
					} else {
						passes = middle;
					}
				}
				whole = forward ? keeps : -keeps;
				remainder = 0.0;
			}
			if (whole != 0) {
				const Refusal refused = world_.transform(cell, reaction, whole);
				CHEM_ASSERT(refused == Refusal::None, refusal_text(refused));
			}
		}
	}
}

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
		const double h = step_seconds_;
		const double dt = ta - tb;
		double heat = 0.0; // J, from a to b
		double equalising = 0.0; // J; unused between two reservoirs
		const bool bounded = !(ma.reservoir && mb.reservoir);
		if (!bounded) {
			heat = edge.conductance * dt * h;
		} else {
			const double ca = ma.reservoir ? 0.0 : thermal_state(table, world_.snapshot_cell(edge.a)).heat_capacity;
			const double cb = mb.reservoir ? 0.0 : thermal_state(table, world_.snapshot_cell(edge.b)).heat_capacity;
			const double ceff = ma.reservoir ? cb : mb.reservoir ? ca : ca * cb / (ca + cb);
			equalising = ceff * dt;
			heat = equalising * (1.0 - exp(-edge.conductance * h / ceff));
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
		react();
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
