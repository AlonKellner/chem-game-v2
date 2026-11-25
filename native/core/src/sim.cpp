#include "chem/sim.hpp"

#include "chem/error.hpp"
#include "chem/invariants.hpp"
#include "chem/math.hpp"
#include "chem/thermo.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <utility>
#include <vector>

namespace chem {

Simulation::Simulation(World world, std::vector<Reaction> reactions) :
		world_(std::move(world)), checking_(CHEM_ASSERT_ABORTS != 0) {
	for (Reaction &r : reactions) {
		if (r.table() != &world_.species()) {
			continue;
		}
		if (r.kinetics().law == Kinetics::Law::FirstOrder || r.kinetics().law == Kinetics::Law::Surface) {
			reactions_.push_back(std::move(r));
		} else if (r.kinetics().law == Kinetics::Law::Equilibrium) {
			equilibria_.push_back(std::move(r));
		}
	}
}

namespace {

// The largest whole extent in [0, most], in the given direction, after which
// the affinity has not passed zero and the cell still has a temperature. The
// affinity keeps its sign at 0 (the caller checked) and falls along the
// extent, so this is the last quantum before the reaction's own equilibrium.
// A secant step from the two ends, a bisection every third: a few
// evaluations when the equilibrium is near, never more than a bisection's.
int64_t reach(const AffinityProbe &probe, bool forward, int64_t most) {
	const auto value = [&](int64_t extent, double &v) {
		double a = 0.0;
		if (!probe.at(static_cast<double>(forward ? extent : -extent), a)) {
			return false;
		}
		v = forward ? a : -a; // >= 0: the direction is kept
		return true;
	};
	const auto finite = [](double v) { return v - v == 0.0; };
	if (most <= 0) {
		return 0;
	}
	double v_lo = 0.0, v_hi = 0.0;
	value(0, v_lo);
	// the usual case, a cell already at its equilibrium: one quantum is too many
	bool ok_hi = value(1, v_hi);
	if (!(ok_hi && v_hi >= 0.0)) {
		return 0;
	}
	if (most == 1) {
		return 1;
	}
	int64_t lo = 1;
	v_lo = v_hi;
	ok_hi = value(most, v_hi);
	if (ok_hi && v_hi >= 0.0) {
		return most;
	}
	int64_t hi = most;
	for (int i = 0; hi - lo > 1; ++i) {
		int64_t middle = lo + (hi - lo) / 2;
		if (ok_hi && finite(v_lo) && finite(v_hi) && i % 3 != 2) {
			middle = lo + static_cast<int64_t>(static_cast<double>(hi - lo) * (v_lo / (v_lo - v_hi)));
		}
		middle = middle <= lo ? lo + 1 : middle >= hi ? hi - 1 : middle;
		double v = 0.0;
		const bool ok = value(middle, v);
		if (ok && v >= 0.0) {
			lo = middle;
			v_lo = v;
		} else {
			hi = middle;
			v_hi = v;
			ok_hi = ok;
		}
	}
	return lo;
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
// the last quantum before the affinity would change sign (reach()): a
// reaction never passes its own equilibrium, so each
// application raises the entropy (T2), and at equilibrium nothing moves and
// nothing is carried (D3).
SurfaceSteps surface_steps(const SpeciesTable &table, const CellState &cell, const Reaction &reaction, double temperature, double a, double mixing, const CellMeta *meta) {
	SurfaceSteps steps;
	const Kinetics &kinetics = reaction.kinetics();
	if (kinetics.law != Kinetics::Law::Surface || !(temperature > 0.0) || a == 0.0 || a != a) {
		return steps;
	}
	const double rt = GAS_CONSTANT * temperature;
	const bool forward = a > 0.0;
	const double drive = forward ? a / rt : -a / rt; // |A| / RT
	// at the surface: k(T) m_driver^q |f|
	const double driver = table.has(kinetics.driver) ? molality(table, cell, kinetics.driver) : 1.0; // its concentration (GAME_SPEC §7)
	steps.surface = kinetics.prefactor * exp(-kinetics.activation / rt) * (kinetics.driver_order == 1.0 ? driver : pow(driver, kinetics.driver_order)) * (1.0 - exp(-drive));
	steps.transport = HUGE_VAL;
	steps.both = steps.surface;
	// by transport: the driver, or else the reaction's dissolved species, carried to the surface or away from it
	SpeciesId carried = table.has(kinetics.driver) ? kinetics.driver : SpeciesId{};
	for (const Term &t : reaction.terms()) {
		if (!table.has(carried) && table.species(t.species).phase == Phase::Aqueous) {
			carried = t.species;
		}
	}
	const double d = particle_diameter(table, cell, kinetics.in);
	const double coefficient = table.has(carried) ? diffusion(table, carried, temperature) : 0.0;
	if (!(d > 0.0) || !(coefficient > 0.0)) {
		return steps; // no particles yet (they nucleate), or a species whose diffusion is not known
	}
	const int32_t nu_signed = reaction.nu(carried);
	const double nu = nu_signed < 0 ? -nu_signed : nu_signed;
	// its concentration in the solvent, mol/m^3
	double solvent = 0.0;
	for (size_t s = 0; s < cell.amount.size(); ++s) {
		const SpeciesDef &def = table.species(SpeciesId{ static_cast<int32_t>(s) });
		solvent += def.phase == Phase::Liquid ? static_cast<double>(cell.amount[s]) * MOL_PER_QUANTUM * def.molar_volume : 0.0;
	}
	if (!(solvent > 0.0)) {
		return steps;
	}
	// How far it is from C_eq, the concentration at which the reaction would be at equilibrium, all else held:
	// Newton's steps in ln n on the affinity (its activity, coefficients and all), each from the affinity's
	// slope there, until a step would move the gap by under 1%. Near equilibrium (growth, the usual case) one
	// step; with none of it yet (a crystal dissolving into pure water) it starts from one quantum's worth.
	const auto x = static_cast<size_t>(carried.index);
	const bool used = forward == (nu_signed < 0); // this direction uses it up: it is carried to the surface
	CellState here = cell;
	const int64_t n = here.amount[x] > 0 ? here.amount[x] : static_cast<int64_t>(nu);
	here.amount[x] = n;
	bool defined = false;
	double at = affinity(table, here, reaction.terms(), temperature, defined, meta); // every reading from the one function: their differences are what counts
	if (!defined || at != at || at == 0.0 || (at > 0.0) != forward) {
		return steps;
	}
	double ratio = 0.0; // ln (C_eq / C)
	if (at == HUGE_VAL || at == -HUGE_VAL) { // another species is absent: at equilibrium all of it is used, or none
		if (!used) {
			return steps;
		}
		ratio = -1e300;
	}
	for (int i = 0; i < 8 && ratio > -1e300; ++i) {
		const double from = static_cast<double>(here.amount[x]);
		const int64_t dn = here.amount[x] / 1000000 > 0 ? here.amount[x] / 1000000 : 1;
		here.amount[x] += dn;
		const double moved = affinity(table, here, reaction.terms(), temperature, defined, meta);
		const double slope = (moved - at) / log((from + static_cast<double>(dn)) / from); // J/mol per unit of ln n
		if (!defined || !(slope != 0.0) || slope != slope) {
			return steps;
		}
		const double step = -at / slope;
		ratio += step;
		if (ratio > 700.0) {
			return steps; // it carries away far faster than the surface could make it
		}
		if (ratio < -20.0 || (step < 0.02 && step > -0.02)) {
			break; // all of it used at equilibrium, or the gap within 1%
		}
		const double next = static_cast<double>(n) * exp(ratio);
		if (!(next >= 1.0) || next > 9.0e18) {
			break;
		}
		here.amount[x] = static_cast<int64_t>(next);
		at = affinity(table, here, reaction.terms(), temperature, defined, meta);
		if (!defined || at != at || at == HUGE_VAL || at == -HUGE_VAL) {
			return steps;
		}
	}
	const double c = static_cast<double>(cell.amount[x]) * MOL_PER_QUANTUM / solvent;
	const double c_eq = static_cast<double>(n) * MOL_PER_QUANTUM / solvent * (ratio < -700.0 ? 0.0 : exp(ratio));
	const double gap = c > c_eq ? c - c_eq : c_eq - c;
	steps.transport = 2.0 * mixing * coefficient / d * gap / nu;
	steps.transported = true;
	steps.both = steps.surface > 0.0 && steps.transport > 0.0 ? steps.surface * steps.transport / (steps.surface + steps.transport) : 0.0;
	return steps;
}

void Simulation::react() {
	const SpeciesTable &table = world_.species();
	const auto cells = static_cast<size_t>(world_.cell_count());
	reaction_remainder_.resize(cells * reactions_.size(), 0.0);
	nucleation_remainder_.resize(cells * reactions_.size(), 0.0);
	for (size_t c = 0; c < cells; ++c) {
		const CellId cell{ static_cast<int32_t>(c) };
		if (world_.meta(cell).reservoir) {
			continue;
		}
		const CellState &state = world_.snapshot_cell(cell);
		// (Stage 53) a cell of nothing but wall (a vessel's ring of glass): no reaction has anything there, as the
		// loop below would find for each
		bool any = false;
		for (size_t s = 0; s < state.amount.size() && !any; ++s) {
			any = state.amount[s] > 0 && !table.species(SpeciesId{ static_cast<int32_t>(s) }).wall;
		}
		if (!any) {
			for (size_t r = 0; r < reactions_.size(); ++r) {
				reaction_remainder_[c * reactions_.size() + r] = 0.0;
				nucleation_remainder_[c * reactions_.size() + r] = 0.0;
			}
			continue;
		}
		for (size_t r = 0; r < reactions_.size(); ++r) {
			const Reaction &reaction = reactions_[r];
			const Kinetics &kinetics = reaction.kinetics();
			const bool surface = kinetics.law == Kinetics::Law::Surface;
			double &remainder = reaction_remainder_[c * reactions_.size() + r];
			double &nucleation_remainder = nucleation_remainder_[c * reactions_.size() + r];
			const auto in = static_cast<size_t>(kinetics.in.index);
			const int64_t have = state.amount[in];
			// nothing of the solid, and nothing to nucleate it from: no probe needed
			if (have == 0 && !(surface && kinetics.nucleation > 0.0 && feasible_extent(state, reaction.terms(), true) > 0)) {
				remainder = 0.0;
				nucleation_remainder = 0.0;
				continue;
			}
			const AffinityProbe probe(table, state, reaction.terms(), thermal(cell), &world_.meta(cell));
			double a = 0.0;
			if (!probe.at(0.0, a) || a == 0.0 || a != a) {
				remainder = 0.0;
				nucleation_remainder = 0.0;
				continue;
			}
			const bool forward = a > 0.0;
			const int64_t most = feasible_extent(state, reaction.terms(), forward);
			if (most == 0) { // nothing to go that way with (an absent crystal to dissolve)
				remainder = 0.0;
				nucleation_remainder = 0.0;
				continue;
			}
			const double rt = GAS_CONSTANT * probe.temperature();
			const double factor = 1.0 - exp(forward ? -a / rt : a / rt); // |f|
			double wanted = 0.0; // quanta of extent, in the direction of the affinity
			int64_t born = 0; // new particles, each one quantum of the solid
			if (!surface) {
				const double rate = kinetics.prefactor * exp(-kinetics.activation / rt);
				wanted = static_cast<double>(have) / static_cast<double>(-reaction.nu(kinetics.in)) * (1.0 - exp(-rate * factor * step_seconds_));
			} else {
				const int32_t nu = reaction.nu(kinetics.in);
				const double per = static_cast<double>(nu < 0 ? -nu : nu);
				const bool consumed = forward == (nu < 0); // this direction uses the solid up
				const SpeciesDef &solid = table.species(kinetics.in);
				// mol/m^2/s of the solid: at its surface and by transport, in series (GAME_SPEC §7), as they are at the step's start
				const double flux = surface_steps(table, state, reaction, probe.temperature(), a, world_.meta(cell).mixing, &world_.meta(cell)).both * per;
				// N equal spheres of n mol: area = (36 pi)^(1/3) N^(1/3) (n v)^(2/3), so
				// dn/dt = -+ g n^(2/3) and n^(1/3) moves linearly: exact over the step
				const double g = 4.835975862049409 * cbrt(static_cast<double>(state.particles[in])) * cbrt(solid.molar_volume * solid.molar_volume) * flux;
				const double mol = static_cast<double>(have) * MOL_PER_QUANTUM;
				const double root = cbrt(mol);
				const double moved = g * step_seconds_ / 3.0;
				const double after = consumed ? (root > moved ? root - moved : 0.0) : root + moved;
				const double change = after * after * after - mol;
				wanted = (change < 0.0 ? -change : change) / MOL_PER_QUANTUM / per;
				// nucleation: where this direction makes the solid from a supersaturated solution
				if (!consumed && kinetics.nucleation > 0.0 && nu == 1) {
					double liquid = 0.0; // quanta of solvent
					for (size_t s = 0; s < state.amount.size(); ++s) {
						if (state.amount[s] > 0 && table.species(SpeciesId{ static_cast<int32_t>(s) }).phase == Phase::Liquid) {
							liquid += static_cast<double>(state.amount[s]);
						}
					}
					const double excess = a / rt < 700.0 ? exp(a / rt) - 1.0 : 1e300; // S - 1
					const double nuclei = kinetics.nucleation * liquid * MOL_PER_QUANTUM * 0.018015 * pow(excess, kinetics.nucleation_order) * step_seconds_ + nucleation_remainder;
					born = nuclei < 1e15 ? static_cast<int64_t>(nuclei) : 1000000000000000;
					nucleation_remainder = nuclei < 1e15 ? nuclei - static_cast<double>(born) : 0.0;
				} else {
					nucleation_remainder = 0.0;
				}
			}
			if ((remainder > 0.0) != forward) {
				remainder = 0.0;
			}
			const double total = (forward ? wanted : -wanted) + remainder;
			int64_t whole = total > -9.0e18 && total < 9.0e18 ? static_cast<int64_t>(total) : 0; // toward zero
			remainder = total - static_cast<double>(whole);
			int64_t size = (forward ? whole : -whole) + born;
			if (size > most) {
				size = most;
				remainder = 0.0;
			}
			const int64_t reached = reach(probe, forward, size);
			if (reached < size) {
				size = reached;
				remainder = 0.0;
			}
			if (size == 0) {
				continue;
			}
			// Particles (the world keeps a solid that appears as one lump): the
			// new nuclei are particles, and a solid made from a solid keeps its
			// grains (CuO from malachite).
			const int64_t grains = table.species(kinetics.in).phase == Phase::Solid ? state.particles[in] : 0;
			std::array<bool, MAX_TERMS> appears{};
			for (size_t t = 0; t < reaction.terms().size(); ++t) {
				const Term &term = reaction.terms()[t];
				appears[t] = (forward ? term.nu > 0 : term.nu < 0) && state.amount[static_cast<size_t>(term.species.index)] == 0;
			}
			const Refusal refused = transform(cell, reaction, forward ? size : -size);
			CHEM_ASSERT(refused == Refusal::None, refusal_text(refused));
			if (surface) {
				const int64_t nuclei = born < size ? born : size;
				if (nuclei > 0 && state.amount[in] > 0) {
					world_.set_particles(cell, kinetics.in, grains + nuclei);
				}
			} else if (grains > 0) {
				for (size_t t = 0; t < reaction.terms().size(); ++t) {
					const SpeciesDef &def = table.species(reaction.terms()[t].species);
					if (appears[t] && def.phase == Phase::Solid && !def.wall) {
						world_.set_particles(cell, reaction.terms()[t].species, grains);
					}
				}
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
	relaxing_.resize(edges.size());
	for (size_t i = 0; i < edges.size(); ++i) {
		const Edge edge = edges[i];
		double &remainder = heat_remainder_[i];
		if (!edge.open || !(edge.conductance > 0.0)) {
			remainder = 0.0;
			continue;
		}
		const CellMeta &ma = world_.meta(edge.a);
		const CellMeta &mb = world_.meta(edge.b);
		// each end's temperature and heat capacity, derived once
		const Thermal state_a = ma.reservoir ? Thermal{ true, 0.0, ma.reservoir_temperature } : thermal(edge.a);
		const Thermal state_b = mb.reservoir ? Thermal{ true, 0.0, mb.reservoir_temperature } : thermal(edge.b);
		if (!state_a.defined || !state_b.defined) {
			remainder = 0.0; // a cell without a temperature conducts nothing (T3 reports it)
			continue;
		}
		const double ta = state_a.temperature, tb = state_b.temperature;
		const double h = step_seconds_;
		const double dt = ta - tb;
		double heat = 0.0; // J, from a to b
		double equalising = 0.0; // J; unused between two reservoirs
		const bool bounded = !(ma.reservoir && mb.reservoir);
		if (!bounded) {
			heat = edge.conductance * dt * h;
		} else {
			const double ca = state_a.heat_capacity;
			const double cb = state_b.heat_capacity;
			const double ceff = ma.reservoir ? cb : mb.reservoir ? ca : ca * cb / (ca + cb);
			equalising = ceff * dt;
			Relaxing &kept = relaxing_[i];
			if (kept.conductance != edge.conductance || kept.capacity != ceff) {
				kept = Relaxing{ edge.conductance, ceff, 1.0 - exp(-edge.conductance * h / ceff) };
			}
			heat = equalising * kept.factor;
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
			const Refusal refused = transfer(EdgeId{ static_cast<int32_t>(i) }, edge.a, {}, Energy{ whole });
			CHEM_ASSERT(refused == Refusal::None || refused == Refusal::Overflow, refusal_text(refused));
			if (refused != Refusal::None) {
				remainder = 0.0;
			}
		}
	}
}

// Work (GAME_SPEC §11, Stage 32): along each edge with a power, that much
// energy a second from its a to its b, whatever their temperatures: a
// stirrer's work on the liquid it turns, which is heat there once the liquid
// has come to rest (C5: one entry of the ledger, as a burner's heat is). What
// is under a quantum is carried to the next step.
void Simulation::do_work() {
	const std::span<const Edge> edges = world_.edges();
	for (size_t i = 0; i < edges.size(); ++i) {
		const Edge edge = edges[i];
		if (!edge.open || edge.power == 0.0) {
			if (i < work_remainder_.size()) {
				work_remainder_[i] = 0.0;
			}
			continue;
		}
		if (work_remainder_.size() < edges.size()) {
			work_remainder_.resize(edges.size(), 0.0);
		}
		double &remainder = work_remainder_[i];
		const double total = edge.power * step_seconds_ / JOULE_PER_QUANTUM + remainder;
		constexpr double MOST = 1.0e18;
		const bool within = total > -MOST && total < MOST;
		const int64_t whole = static_cast<int64_t>(within ? total : total < 0.0 ? -MOST : MOST); // toward zero
		remainder = within ? total - static_cast<double>(whole) : 0.0;
		if (whole != 0) {
			const Refusal refused = transfer(EdgeId{ static_cast<int32_t>(i) }, edge.a, {}, Energy{ whole });
			CHEM_ASSERT(refused == Refusal::None || refused == Refusal::Overflow, refusal_text(refused));
			if (refused != Refusal::None) {
				remainder = 0.0;
			}
		}
	}
}

// Liquids run down the drains (GAME_SPEC §6): all of the upper cell's liquid,
// with what is dissolved in it, into the lower cell, carrying its enthalpy. A
// rigid lower cell takes only what fits; the rest stays above. Kinematic: no
// gravitational energy. A drain runs neither into nor out of a sealed cell
// (Stage 28): no gas could make way for the liquid.
void Simulation::drain_liquids() {
	const SpeciesTable &table = world_.species();
	const std::span<const Edge> edges = world_.edges();
	for (size_t i = 0; i < edges.size(); ++i) {
		const Edge edge = edges[i];
		if (!edge.open || !edge.drain || world_.meta(edge.a).reservoir || world_.meta(edge.b).reservoir || world_.meta(edge.a).sealed || world_.meta(edge.b).sealed) {
			continue;
		}
		const CellState &upper = world_.snapshot_cell(edge.a);
		const Thermal thermal = thermal_state(table, upper);
		if (!thermal.defined) {
			continue;
		}
		const bool solids = (edge.phases & phase_bit(Phase::Solid)) != 0;
		// (a drain made before Stage 31 passes solids by its phases alone: all of them, with the liquid)
		const double carried = !solids ? 0.0 : edge.carried > 0.0 || edge.solids_rate > 0.0 ? edge.carried : 1.0;
		double runs = 0.0, rests = 0.0; // m^3 of liquid, and of solids
		bool known = true;
		for (size_t s = 0; s < upper.amount.size(); ++s) {
			const SpeciesDef &def = table.species(SpeciesId{ static_cast<int32_t>(s) });
			if (upper.amount[s] > 0 && def.phase == Phase::Liquid && !def.wall) {
				known = known && def.molar_volume > 0.0;
				runs += static_cast<double>(upper.amount[s]) * MOL_PER_QUANTUM * def.molar_volume;
			}
			if (edge.solids_rate > 0.0 && upper.amount[s] > 0 && def.phase == Phase::Solid && !def.wall && def.molar_volume > 0.0) {
				rests += static_cast<double>(upper.amount[s]) * MOL_PER_QUANTUM * def.molar_volume;
			}
		}
		// the solids that run by themselves (Stage 31): at most their rate's volume this step, liquid or none
		double own = rests > 0.0 ? std::min(1.0, edge.solids_rate * step_seconds_ / rests) : 0.0;
		if (!known || (!(runs > 0.0) && !(own > 0.0))) {
			continue;
		}
		double fraction = runs > 0.0 ? 1.0 : 0.0;
		if (edge.rate >= 0.0 && runs > 0.0) { // a pour (Stage 27): at most its rate's volume this step
			const double most = edge.rate * step_seconds_;
			fraction = most >= runs ? 1.0 : most / runs;
		}
		const CellMeta &below = world_.meta(edge.b);
		if (below.rigid && world_.spill().index < 0) { // with nowhere to spill, the rest stays above
			const Fill fill = fill_of(table, world_.snapshot_cell(edge.b), T0);
			const double room = fill.defined ? below.volume - fill.condensed : 0.0;
			if (runs > 0.0) {
				fraction = std::min(fraction, room >= runs ? 1.0 : room > 0.0 ? room / runs : 0.0);
			}
			if (own > 0.0) {
				const double left = room - fraction * runs;
				own = std::min(own, left >= rests ? 1.0 : left > 0.0 ? left / rests : 0.0);
			}
		}
		// of each solid: the liquid's share of what it carries, and what runs by itself
		const double solid_fraction = own > 0.0 ? std::min(1.0, carried * fraction + own) : carried * fraction;
		std::vector<SpeciesAmount> amounts;
		for (size_t s = 0; s < upper.amount.size(); ++s) {
			const SpeciesId id{ static_cast<int32_t>(s) };
			const SpeciesDef &def = table.species(id);
			if (upper.amount[s] <= 0 || def.wall) {
				continue;
			}
			const bool flows = def.phase == Phase::Liquid || def.phase == Phase::Aqueous;
			if (!flows && !(solids && def.phase == Phase::Solid)) {
				continue;
			}
			const double share = flows ? fraction : solid_fraction;
			const int64_t moves = share >= 1.0 ? upper.amount[s] : static_cast<int64_t>(static_cast<double>(upper.amount[s]) * share);
			if (moves > 0) {
				amounts.push_back(SpeciesAmount{ id, Amount{ moves } });
			}
		}
		if (!amounts.empty()) {
			const Refusal refused = transfer(EdgeId{ static_cast<int32_t>(i) }, edge.a, amounts, enthalpy_carried(table, amounts, thermal.temperature));
			CHEM_ASSERT(refused == Refusal::None, refusal_text(refused));
			// what lands below (Stage 51): the liquid's volume and mass, for a column's jet
			for (const SpeciesAmount &m : amounts) {
				const SpeciesDef &def = table.species(m.species);
				if (def.phase == Phase::Liquid || def.phase == Phase::Aqueous) {
					const double mol = static_cast<double>(m.amount.quanta) * MOL_PER_QUANTUM;
					landed_m3_[static_cast<size_t>(edge.b.index)] += mol * def.molar_volume;
					landed_kg_[static_cast<size_t>(edge.b.index)] += mol * static_cast<double>(table.molar_mass_ug(m.species)) * 1e-9;
				}
			}
		}
	}
}

// Overflow (GAME_SPEC §6): a rigid cell that holds more solids and liquids
// than its volume spills the excess of its liquid, with what is dissolved in
// it, into the world's spill cell. After settling, which has by then pushed
// out all its gas: it is left exactly full of liquid.
void Simulation::overflow() {
	const CellId spill = world_.spill();
	if (spill.index < 0) {
		return;
	}
	const SpeciesTable &table = world_.species();
	bool spilled = false;
	for (int32_t c = 0; c < world_.cell_count(); ++c) {
		const CellId cell{ c };
		const CellMeta &meta = world_.meta(cell);
		if (!meta.rigid || meta.sealed) {
			continue; // a sealed cell lets nothing out (Stage 28)
		}
		const CellState &state = world_.snapshot_cell(cell);
		const Fill fill = fill_of(table, state, T0);
		if (!fill.defined || !(fill.condensed > meta.volume)) {
			continue;
		}
		double liquid = 0.0, solids = 0.0; // m^3
		for (size_t s = 0; s < state.amount.size(); ++s) {
			const SpeciesDef &def = table.species(SpeciesId{ static_cast<int32_t>(s) });
			if (state.amount[s] > 0 && !def.wall && (def.phase == Phase::Liquid || def.phase == Phase::Solid)) {
				(def.phase == Phase::Liquid ? liquid : solids) += static_cast<double>(state.amount[s]) * MOL_PER_QUANTUM * def.molar_volume;
			}
		}
		if (!(liquid > 0.0)) {
			continue; // solids alone: nothing can run out (V2 reports it)
		}
		// a share of the solution, what is dissolved in it as much as its liquid: of the solution's own volume
		// (its solutes' volume with it), so that what runs out is the excess (Stage 27: of the liquid's alone, a
		// solution spilled more than its excess, and left its vessel under-full)
		const double excess = fill.condensed - meta.volume, solution = std::max(fill.condensed - solids, liquid);
		const double fraction = excess < solution ? excess / solution : 1.0;
		std::vector<SpeciesAmount> amounts;
		for (size_t s = 0; s < state.amount.size(); ++s) {
			const SpeciesId id{ static_cast<int32_t>(s) };
			const SpeciesDef &def = table.species(id);
			if (state.amount[s] > 0 && !def.wall && (def.phase == Phase::Liquid || def.phase == Phase::Aqueous)) {
				// rounded up: what is left fits
				const double share = static_cast<double>(state.amount[s]) * fraction;
				auto out = static_cast<int64_t>(share);
				out += static_cast<double>(out) < share ? 1 : 0;
				amounts.push_back(SpeciesAmount{ id, Amount{ out < state.amount[s] ? out : state.amount[s] } });
			}
		}
		EdgeId edge;
		const std::span<const Edge> edges = world_.edges();
		for (size_t i = 0; i < edges.size(); ++i) {
			const Edge &e = edges[i];
			if (e.open && !e.drain && e.phases == PHASES_LIQUID && ((e.a == cell && e.b == spill) || (e.a == spill && e.b == cell))) {
				edge = EdgeId{ static_cast<int32_t>(i) };
			}
		}
		if (edge.index < 0 && world_.connect(cell, spill, PHASES_LIQUID, &edge) != Refusal::None) {
			continue;
		}
		const Refusal refused = transfer(edge, cell, amounts, enthalpy_carried(table, amounts, thermal(cell).temperature));
		CHEM_ASSERT(refused == Refusal::None, refusal_text(refused));
		spilled = true;
	}
	// (Stage 51) what spilled meets what the spill cell holds after this step's settling: it is brought to its fast
	// equilibria here, as every cell is at a step's end (P1)
	if (spilled && thermal(spill).defined) {
		project(spill);
	}
}

// Gas exchanged by diffusion along an edge that exchanges it (GAME_SPEC §6, Stage 50: an open vessel's mouth): each
// gas crosses from where its mole fraction is the higher, at 1 atm, D c G (x_a - x_b) mol/s, D its diffusion in air
// at the two cells' mean temperature, c the gas's molar density there, G the edge's area over length; over the step,
// the exact relaxation of the two cells' fractions toward each other (never past equal), with its own enthalpy.
void Simulation::exchange_gases() {
	exchanged_.clear();
	const SpeciesTable &table = world_.species();
	const std::span<const Edge> edges = world_.edges();
	for (size_t i = 0; i < edges.size(); ++i) {
		const Edge e = edges[i];
		if (!e.open || e.column || !(e.exchange > 0.0) || (e.phases & PHASES_GAS) == 0 || world_.meta(e.a).reservoir || world_.meta(e.b).reservoir) {
			continue;
		}
		const CellState a = world_.snapshot_cell(e.a), b = world_.snapshot_cell(e.b);
		double na = 0.0, nb = 0.0;
		for (size_t s = 0; s < a.amount.size(); ++s) {
			if (table.species(SpeciesId{ static_cast<int32_t>(s) }).phase == Phase::Gas) {
				na += static_cast<double>(a.amount[s]) * MOL_PER_QUANTUM;
				nb += static_cast<double>(b.amount[s]) * MOL_PER_QUANTUM;
			}
		}
		if (!(na > 0.0) || !(nb > 0.0)) {
			continue;
		}
		const double ta = thermal(e.a).temperature, tb = thermal(e.b).temperature, t = 0.5 * (ta + tb);
		if (!(t > 0.0)) {
			continue;
		}
		const double c = P_ATMOSPHERE / (GAS_CONSTANT * t), inverse = 1.0 / na + 1.0 / nb;
		for (size_t s = 0; s < a.amount.size(); ++s) {
			const SpeciesId id{ static_cast<int32_t>(s) };
			const double d = gas_diffusion(table, id, t);
			if (!(d > 0.0) || (a.amount[s] == 0 && b.amount[s] == 0)) {
				continue;
			}
			const double xa = static_cast<double>(a.amount[s]) * MOL_PER_QUANTUM / na, xb = static_cast<double>(b.amount[s]) * MOL_PER_QUANTUM / nb;
			const double mol = std::fabs(xa - xb) / inverse * (1.0 - chem::exp(-d * c * e.exchange * inverse * step_seconds_));
			const bool down = xa > xb; // from a to b
			const int64_t have = down ? a.amount[s] : b.amount[s];
			const int64_t q = std::min(static_cast<int64_t>(mol / MOL_PER_QUANTUM), have);
			if (q <= 0) {
				continue;
			}
			const CellId from = down ? e.a : e.b;
			const SpeciesAmount moved[] = { SpeciesAmount{ id, Amount{ q } } };
			const Refusal refused = transfer(EdgeId{ static_cast<int32_t>(i) }, from, moved, enthalpy_carried(table, moved, down ? ta : tb));
			CHEM_ASSERT(refused == Refusal::None, refusal_text(refused));
			exchanged_.push_back(Exchanged{ EdgeId{ static_cast<int32_t>(i) }, from, down ? e.b : e.a, id, down ? xa : xb, down ? xb : xa, down ? na : nb, down ? nb : na, q });
		}
	}
}

// The fast equilibria of one cell (GAME_SPEC §8): each reaction of the
// equilibrium law, in order, taken to the last quantum before its affinity
// would change sign, or to the end of what the cell holds (dry-out, total
// condensation). At fixed enthalpy: boiling cools the cell, condensing warms
// it. true when anything changed.
bool Simulation::project(CellId cell) {
	const SpeciesTable &table = world_.species();
	const CellState &state = world_.snapshot_cell(cell);
	bool changed = false;
	// (Stage 53) a cell of nothing but wall (a vessel's ring of glass) has no equilibrium to be at
	bool any = false;
	for (size_t s = 0; s < state.amount.size() && !any; ++s) {
		any = state.amount[s] > 0 && !table.species(SpeciesId{ static_cast<int32_t>(s) }).wall;
	}
	if (!any) {
		return false;
	}
	// several equilibria that share a species are taken in turn until none
	// moves; each is probed again only if the cell changed since it was last
	// probed (Stage 44: one that cannot move, or that moved last, is at rest
	// until another moves, so a cell with one moving equilibrium settles in
	// the first round)
	seen_.assign(equilibria_.size(), -1); // the cell's version at each one's last probe
	int version = 0;
	for (int round = 0; round < 16; ++round) {
		bool moved = false;
		for (size_t e = 0; e < equilibria_.size(); ++e) {
			const Reaction &reaction = equilibria_[e];
			if (seen_[e] == version) {
				continue; // nothing changed since it was probed, or since it moved
			}
			seen_[e] = version;
			if (feasible_extent(state, reaction.terms(), true) == 0 && feasible_extent(state, reaction.terms(), false) == 0) {
				continue; // none of what it would consume, either way
			}
			const AffinityProbe probe(table, state, reaction.terms(), thermal(cell), &world_.meta(cell));
			double a = 0.0;
			if (!probe.at(0.0, a) || a == 0.0 || a != a) {
				continue;
			}
			const bool forward = a > 0.0;
			const int64_t size = reach(probe, forward, feasible_extent(state, reaction.terms(), forward));
			if (size != 0) {
				// a solid that appears out of the solution comes as its grains (a precipitate, Stage 44), not one lump
				std::array<SpeciesId, MAX_TERMS> appears{};
				for (size_t t = 0; t < reaction.terms().size(); ++t) {
					const Term &term = reaction.terms()[t];
					const SpeciesDef &def = table.species(term.species);
					if ((forward ? term.nu > 0 : term.nu < 0) && def.phase == Phase::Solid && def.grain > 0.0 && def.molar_volume > 0.0 &&
							state.amount[static_cast<size_t>(term.species.index)] == 0) {
						appears[t] = term.species;
					}
				}
				const Refusal refused = transform(cell, reaction, forward ? size : -size);
				CHEM_ASSERT(refused == Refusal::None, refusal_text(refused));
				for (const SpeciesId s : appears) {
					if (table.has(s)) {
						const SpeciesDef &def = table.species(s);
						// N spheres of the grain's diameter in the volume made
						const double volume = static_cast<double>(state.amount[static_cast<size_t>(s.index)]) * MOL_PER_QUANTUM * def.molar_volume;
						const double grains = volume / (0.5235987755982988 * def.grain * def.grain * def.grain);
						world_.set_particles(cell, s, grains < 1.0 ? 1 : grains < 1e15 ? static_cast<int64_t>(grains) : 1000000000000000);
					}
				}
				moved = true;
				seen_[e] = ++version; // at its balance, in the cell as it is now
			}
		}
		changed = changed || moved;
		if (!moved) {
			break;
		}
	}
	return changed;
}

// One rigid cell made exactly full (GAME_SPEC §6): over-full, it expels the
// excess fraction of its gas to its parent, at its own composition and
// temperature (each species rounded up to a whole quantum); its temperature
// does not change, so it is then exactly full. Under-full by more than a
// quantum of gas, it draws gas of its parent's composition and temperature:
// the gas warms or cools on mixing, so the amount is the root of
// (n + x) R T(x) / P = free volume, a quadratic. If the parent holds less, it
// draws all there is and is marked starved. Moving gas carries its enthalpy.
// true when it drew.
bool Simulation::fill(CellId cell, CellId up, EdgeId edge, bool may_draw) {
	const SpeciesTable &table = world_.species();
	const auto c = static_cast<size_t>(cell.index);
	const CellMeta &meta = world_.meta(cell);
	const CellState &state = world_.snapshot_cell(cell);
	const Thermal thermal = this->thermal(cell);
	if (!thermal.defined || !(thermal.temperature > 0.0)) {
		return false;
	}
	const Fill fill = fill_of(table, state, thermal.temperature);
	if (!fill.defined) {
		return false;
	}
	const double free = meta.volume - fill.condensed;
	// as gas_excess() has it, to the last bit: T1 judges the expulsion by that number
	const double excess = fill.gas + fill.condensed - meta.volume;
	std::vector<SpeciesAmount> amounts;
	if (excess > 0.0 && fill.gas > 0.0) {
		const double fraction = excess < fill.gas ? excess / fill.gas : 1.0;
		for (size_t s = 0; s < state.amount.size(); ++s) {
			const SpeciesId id{ static_cast<int32_t>(s) };
			if (state.amount[s] > 0 && table.species(id).phase == Phase::Gas) {
				// rounded up: the cell ends a fraction of a quantum under-full,
				// which no draw can fill, so it is settled in this one step
				const double share = static_cast<double>(state.amount[s]) * fraction;
				auto out = static_cast<int64_t>(share);
				out += static_cast<double>(out) < share ? 1 : 0;
				amounts.push_back(SpeciesAmount{ id, Amount{ out < state.amount[s] ? out : state.amount[s] } });
			}
		}
		if (!amounts.empty()) {
			const Refusal refused = transfer(edge, cell, amounts, enthalpy_carried(table, amounts, thermal.temperature));
			CHEM_ASSERT(refused == Refusal::None, refusal_text(refused));
		}
		return false;
	}
	if (!may_draw || !(-excess > MOL_PER_QUANTUM * GAS_CONSTANT * thermal.temperature / P_ATMOSPHERE)) {
		return false; // full, to within what one quantum of gas would fill
	}
	const CellState &source = world_.snapshot_cell(up);
	const Thermal source_thermal = this->thermal(up);
	if (!source_thermal.defined) {
		return false;
	}
	// the parent's gas, and its heat capacity per mole
	double source_gas = 0.0, capacity = 0.0;
	for (size_t s = 0; s < source.amount.size(); ++s) {
		const SpeciesDef &def = table.species(SpeciesId{ static_cast<int32_t>(s) });
		if (source.amount[s] > 0 && def.phase == Phase::Gas) {
			const double mol = static_cast<double>(source.amount[s]) * MOL_PER_QUANTUM;
			source_gas += mol;
			capacity += mol * def.cp;
		}
	}
	if (!(source_gas > 0.0)) {
		starved_[c] = 1;
		return false;
	}
	capacity /= source_gas;
	// Drawing x mol gives (gas + x) mol at T(x) = (C T + x c Ts) / (C + x c):
	// the heat capacities weigh the two temperatures. Exactly full when
	//   (gas + x) (C T + x c Ts) R / P = free (C + x c),
	// a quadratic in x with one positive root (the cell is under-full).
	const double gas = fill.gas * P_ATMOSPHERE / (GAS_CONSTANT * thermal.temperature);
	const double g = GAS_CONSTANT / P_ATMOSPHERE;
	const double ct = thermal.heat_capacity * thermal.temperature;
	const double cts = capacity * source_thermal.temperature;
	const double qa = g * cts;
	const double qb = g * (ct + gas * cts) - free * capacity;
	const double qc = g * gas * ct - free * thermal.heat_capacity; // < 0
	const double root = std::sqrt(qb * qb - 4.0 * qa * qc);
	const double fills = qb >= 0.0 ? 2.0 * qc / (-qb - root) : (-qb + root) / (2.0 * qa);
	double draw = fills;
	if (!(fills <= source_gas)) {
		draw = source_gas; // all there is, and still under-full
		starved_[c] = 1;
	}
	const double fraction = draw / source_gas;
	for (size_t s = 0; s < source.amount.size(); ++s) {
		const SpeciesId id{ static_cast<int32_t>(s) };
		if (source.amount[s] > 0 && table.species(id).phase == Phase::Gas) {
			const auto in = static_cast<int64_t>(static_cast<double>(source.amount[s]) * fraction);
			if (in > 0) {
				amounts.push_back(SpeciesAmount{ id, Amount{ in < source.amount[s] ? in : source.amount[s] } });
			}
		}
	}
	if (amounts.empty()) {
		return false;
	}
	const Refusal refused = transfer(edge, up, amounts, enthalpy_carried(table, amounts, source_thermal.temperature));
	CHEM_ASSERT(refused == Refusal::None, refusal_text(refused));
	return true;
}

// The end of the step: every cell at its fast equilibria and every rigid cell
// exactly full (GAME_SPEC §6, §8; the demo's subset: every cell at one
// atmosphere, so gas flow is not a rate but this condition).
//
// The cells form a tree rooted at the elastic cells (breadth first along the
// open gas edges, in index order). Each cell, deepest first: its equilibria,
// then, if it is rigid, its volume against its parent. Expelling changes
// neither its temperature nor its composition, so its equilibria still hold.
// Drawing changes both, so they are taken again, and the volume again, until
// it draws no more (or eight times: then it is left under-full and marked
// starved, the step's limit on how fast a cell can pull gas in). A parent is
// settled after its children, and nothing a
// parent does afterwards changes a child, so one sweep leaves every cell at
// equilibrium and full.
void Simulation::settle() {
	const auto cells = static_cast<size_t>(world_.cell_count());
	starved_.assign(cells, 0);
	if (!tree_built_ || tree_version_ != world_.topology_version()) {
		build_tree();
	}
	settle_cells();
	// a column's layers (Stage 47), on no gas path: each at its fast equilibria too (a layer that boils makes steam
	// in it, which the column takes up at the next step)
	for (size_t c = 0; c < cells; ++c) {
		const CellId cell{ static_cast<int32_t>(c) };
		if (world_.meta(cell).layer && thermal(cell).defined) {
			project(cell);
		}
	}
}

// The gas tree: the cells breadth first from the elastic ones along the open
// gas edges, in index order. It changes only when an edge does.
void Simulation::build_tree() {
	const auto cells = static_cast<size_t>(world_.cell_count());
	const std::span<const Edge> edges = world_.edges();
	tree_built_ = true;
	tree_version_ = world_.topology_version();
	depth_.assign(cells, -1);
	parent_.assign(cells, -1);
	parent_edge_.assign(cells, -1);
	order_.clear();
	for (size_t c = 0; c < cells; ++c) {
		const CellMeta &m = world_.meta(CellId{ static_cast<int32_t>(c) });
		if (!m.reservoir && !m.rigid && !m.layer) { // (a layer is no root of the air: Stage 47)
			depth_[c] = 0;
			order_.push_back(c);
		}
	}
	for (size_t head = 0; head < order_.size(); ++head) {
		const size_t at = order_[head];
		for (size_t i = 0; i < edges.size(); ++i) {
			const Edge &e = edges[i];
			if (!e.open || (e.phases & PHASES_GAS) == 0 || e.column) { // (a column's edge is no gas path)
				continue;
			}
			const auto a = static_cast<size_t>(e.a.index);
			const auto b = static_cast<size_t>(e.b.index);
			if (a != at && b != at) {
				continue;
			}
			const size_t other = a == at ? b : a;
			if (depth_[other] == -1 && !world_.meta(CellId{ static_cast<int32_t>(other) }).reservoir) {
				depth_[other] = depth_[at] + 1;
				parent_[other] = static_cast<int32_t>(at);
				parent_edge_[other] = static_cast<int32_t>(i);
				order_.push_back(other);
			}
		}
	}
	// the sealed cells, group by group (Stage 28): none of them was reached
	sealed_order_.clear();
	sealed_groups_.clear();
	for (size_t c = 0; c < cells; ++c) {
		const CellMeta &m = world_.meta(CellId{ static_cast<int32_t>(c) });
		if (depth_[c] != -1 || m.reservoir || !m.rigid) {
			continue;
		}
		const size_t begin = sealed_order_.size();
		depth_[c] = 0;
		sealed_order_.push_back(c);
		for (size_t head = begin; head < sealed_order_.size(); ++head) {
			const size_t at = sealed_order_[head];
			for (size_t i = 0; i < edges.size(); ++i) {
				const Edge &e = edges[i];
				if (!e.open || (e.phases & PHASES_GAS) == 0 || (static_cast<size_t>(e.a.index) != at && static_cast<size_t>(e.b.index) != at)) {
					continue;
				}
				const auto other = static_cast<size_t>(static_cast<size_t>(e.a.index) == at ? e.b.index : e.a.index);
				if (depth_[other] == -1 && !world_.meta(CellId{ static_cast<int32_t>(other) }).reservoir) {
					depth_[other] = depth_[at] + 1;
					parent_[other] = static_cast<int32_t>(at);
					parent_edge_[other] = static_cast<int32_t>(i);
					sealed_order_.push_back(other);
				}
			}
		}
		sealed_groups_.emplace_back(begin, sealed_order_.size());
	}
}

// Two cells sealed together share one pressure (GAME_SPEC §6, Stage 28): gas
// runs from the one at the higher pressure into the other, at its own
// composition and temperature, carrying its enthalpy, as much as leaves the
// giver at no lower a pressure than the taker (to the last quantum: a
// bisection on the share of its gas). true when any moved.
bool Simulation::equalize(CellId cell, CellId up, EdgeId edge) {
	const SpeciesTable &table = world_.species();
	const Thermal a = thermal(cell), b = thermal(up);
	if (!a.defined || !b.defined || a.pressure == b.pressure) {
		return false;
	}
	const bool out = a.pressure > b.pressure;
	const CellId giver = out ? cell : up, taker = out ? up : cell;
	const double t_giver = out ? a.temperature : b.temperature;
	const CellState gives = world_.snapshot_cell(giver), takes = world_.snapshot_cell(taker);
	const CellMeta &mg = world_.meta(giver), &mt = world_.meta(taker);
	std::vector<SpeciesAmount> amounts;
	const auto share = [&](double f) {
		amounts.clear();
		for (size_t s = 0; s < gives.amount.size(); ++s) {
			if (gives.amount[s] > 0 && table.species(SpeciesId{ static_cast<int32_t>(s) }).phase == Phase::Gas) {
				const auto n = static_cast<int64_t>(static_cast<double>(gives.amount[s]) * f);
				if (n > 0) {
					amounts.push_back(SpeciesAmount{ SpeciesId{ static_cast<int32_t>(s) }, Amount{ n < gives.amount[s] ? n : gives.amount[s] } });
				}
			}
		}
	};
	// the giver's pressure less the taker's, had the share f run
	const auto gap = [&](double f) {
		share(f);
		CellState g = gives, t = takes;
		for (const SpeciesAmount &x : amounts) {
			g.amount[static_cast<size_t>(x.species.index)] -= x.amount.quanta;
			t.amount[static_cast<size_t>(x.species.index)] += x.amount.quanta;
		}
		const Energy e = enthalpy_carried(table, amounts, t_giver);
		g.energy -= e.quanta;
		t.energy += e.quanta;
		const Thermal tg = thermal_state(table, g, &mg), tt = thermal_state(table, t, &mt);
		return tg.defined && tt.defined ? tg.pressure - tt.pressure : -1.0;
	};
	double lo = 0.0, hi = 1.0;
	if (gap(hi) >= 0.0) {
		lo = hi;
	} else {
		for (int i = 0; i < 60; ++i) {
			const double middle = 0.5 * (lo + hi);
			(gap(middle) >= 0.0 ? lo : hi) = middle;
		}
	}
	share(lo);
	if (amounts.empty()) {
		return false;
	}
	const Refusal refused = transfer(edge, giver, amounts, enthalpy_carried(table, amounts, t_giver));
	CHEM_ASSERT(refused == Refusal::None, refusal_text(refused));
	return true;
}

void Simulation::settle_cells() {
	// the breadth-first order is by depth, then by discovery: settle it backwards
	for (size_t k = order_.size(); k-- > 0;) {
		const size_t c = order_[k];
		const CellId cell{ static_cast<int32_t>(c) };
		project(cell);
		if (!world_.meta(cell).rigid) {
			continue;
		}
		const CellId up{ parent_[c] };
		const EdgeId edge{ parent_edge_[c] };
		constexpr int ROUNDS = 8;
		for (int round = 0; round < ROUNDS; ++round) {
			if (!fill(cell, up, edge)) {
				break; // full, or it expelled: its equilibria stand
			}
			if (!project(cell)) {
				fill(cell, up, edge); // what the draw's rounding left
				break;
			}
			if (round == ROUNDS - 1) {
				// a fast reaction in it consumes gas as fast as it arrives: it
				// has drawn its fill eight times this step. It may be left
				// under-full, never over-full.
				starved_[c] = 1;
				fill(cell, up, edge, false);
			}
		}
	}
	// the sealed cells (Stage 28): each at its equilibria, and a group at one pressure, deepest first, until no gas runs
	for (const auto &[begin, end] : sealed_groups_) {
		constexpr int ROUNDS = 8;
		for (int round = 0; round < ROUNDS; ++round) {
			bool moved = false;
			for (size_t k = end; k-- > begin;) {
				const size_t c = sealed_order_[k];
				project(CellId{ static_cast<int32_t>(c) });
				if (parent_[c] >= 0) {
					moved = equalize(CellId{ static_cast<int32_t>(c) }, CellId{ parent_[c] }, EdgeId{ parent_edge_[c] }) || moved;
				}
			}
			if (!moved) {
				break;
			}
		}
	}
}

// A cell's thermal state, derived once and kept until a primitive touches the
// cell: the same value thermal_state() would give, many times cheaper.
const Thermal &Simulation::thermal(CellId cell) {
	const auto c = static_cast<size_t>(cell.index);
	if (!thermal_valid_[c]) {
		const CellState &state = world_.snapshot_cell(cell);
		const CellMeta &meta = world_.meta(cell);
		if (thermal_key_.size() <= c) {
			thermal_key_.resize(c + 1);
		}
		ThermalKey &key = thermal_key_[c];
		// (Stage 53) derived again only if what it is derived from changed since: the same value, exactly
		if (!(key.known && key.energy == state.energy && key.volume == meta.volume && key.sealed == meta.sealed && key.rigid == meta.rigid &&
					key.reservoir == meta.reservoir && key.amount == state.amount)) {
			thermal_[c] = thermal_state(world_.species(), state, &meta);
			key.known = true;
			key.energy = state.energy;
			key.amount = state.amount;
			key.volume = meta.volume;
			key.sealed = meta.sealed;
			key.rigid = meta.rigid;
			key.reservoir = meta.reservoir;
		}
		thermal_valid_[c] = 1;
	}
	return thermal_[c];
}

Refusal Simulation::transfer(EdgeId edge, CellId from, std::span<const SpeciesAmount> amounts, Energy energy) {
	const Edge &e = world_.edges()[static_cast<size_t>(edge.index)];
	thermal_valid_[static_cast<size_t>(e.a.index)] = 0;
	thermal_valid_[static_cast<size_t>(e.b.index)] = 0;
	const Refusal refused = world_.transfer(edge, from, amounts, energy);
	if (refused == Refusal::None) { // the flows (§13.6), from a to b
		const size_t at = static_cast<size_t>(edge.index) * PROCESSES + static_cast<size_t>(process_);
		if (flows_.energy.size() <= at) {
			flows_.energy.resize(world_.edges().size() * PROCESSES, 0);
			flows_.grams.resize(world_.edges().size() * PROCESSES, 0.0);
		}
		const int64_t sign = from == e.a ? 1 : -1;
		flows_.energy[at] += sign * energy.quanta;
		double g = 0.0;
		for (const SpeciesAmount &x : amounts) {
			g += static_cast<double>(x.amount.quanta) * MOL_PER_QUANTUM * static_cast<double>(world_.species().molar_mass_ug(x.species)) * 1e-6;
		}
		flows_.grams[at] += static_cast<double>(sign) * g;
	}
	return refused;
}

Refusal Simulation::transform(CellId cell, const Reaction &reaction, int64_t extent) {
	thermal_valid_[static_cast<size_t>(cell.index)] = 0;
	const Refusal refused = world_.transform(cell, reaction, extent);
	if (refused == Refusal::None && !reactions_.empty() && &reaction >= reactions_.data() && &reaction < reactions_.data() + reactions_.size()) {
		const size_t r = static_cast<size_t>(&reaction - reactions_.data()), at = static_cast<size_t>(cell.index) * reactions_.size() + r;
		if (tally_.size() <= at) {
			tally_.resize(static_cast<size_t>(world_.cell_count()) * reactions_.size(), 0);
		}
		tally_[at] += extent;
	}
	if (refused == Refusal::None) { // the flows (§13.6): a rated reaction, or an equilibrium after them
		size_t r = SIZE_MAX;
		if (!reactions_.empty() && &reaction >= reactions_.data() && &reaction < reactions_.data() + reactions_.size()) {
			r = static_cast<size_t>(&reaction - reactions_.data());
		} else if (!equilibria_.empty() && &reaction >= equilibria_.data() && &reaction < equilibria_.data() + equilibria_.size()) {
			r = reactions_.size() + static_cast<size_t>(&reaction - equilibria_.data());
		}
		if (r != SIZE_MAX) {
			flows_.extent[static_cast<size_t>(cell.index) * flows_.reactions + r] += extent;
		}
	}
	return refused;
}

int64_t Simulation::tally(size_t reaction, CellId cell) const {
	const size_t at = static_cast<size_t>(cell.index) * reactions_.size() + reaction;
	return at < tally_.size() ? tally_[at] : 0;
}

// what was derived before the step may be stale: a host may have changed the world between steps
void Simulation::begin_step() {
	grow_columns(); // (Stage 47: before anything is sized by the cells)
	const auto cells = static_cast<size_t>(world_.cell_count());
	thermal_.resize(cells);
	thermal_valid_.assign(cells, 0);
	flows_.reactions = reactions_.size() + equilibria_.size();
	flows_.energy.assign(world_.edges().size() * PROCESSES, 0);
	flows_.grams.assign(world_.edges().size() * PROCESSES, 0.0);
	flows_.extent.assign(cells * flows_.reactions, 0);
	landed_m3_.assign(cells, 0.0);
	landed_kg_.assign(cells, 0.0);
	process_ = Process::Reaction;
	configure_columns();
}

void Simulation::restore_remainders(std::vector<double> heat, std::vector<double> reaction, std::vector<double> nucleation, std::vector<double> work) {
	work_remainder_ = std::move(work);
	heat_remainder_ = std::move(heat);
	reaction_remainder_ = std::move(reaction);
	nucleation_remainder_ = std::move(nucleation);
}

void Simulation::advance() {
	begin_step();
	if (world_.thermal()) {
		process_ = Process::Reaction;
		react();
		process_ = Process::Heat;
		conduct_heat();
		process_ = Process::Work;
		do_work();
		process_ = Process::Drain;
		drain_liquids();
		process_ = Process::Exchange;
		exchange_gases();
		process_ = Process::Column;
		stratify();
		process_ = Process::Settle;
		settle();
		process_ = Process::Overflow;
		overflow();
	}
	world_.skip_step();
	traced_ = false;
}

StepTrace Simulation::step() {
	if (!traced_) {
		world_.rebase(); // steps were skipped: this one's trace starts here
		traced_ = true;
	}
	begin_step();
	if (world_.thermal()) {
		process_ = Process::Reaction;
		react();
		process_ = Process::Heat;
		conduct_heat();
		process_ = Process::Work;
		do_work();
		process_ = Process::Drain;
		drain_liquids();
		process_ = Process::Exchange;
		exchange_gases();
		process_ = Process::Column;
		stratify();
		process_ = Process::Settle;
		settle();
		process_ = Process::Overflow;
		overflow();
	}
	StepTrace trace = world_.close_step();
	if (world_.thermal()) {
		trace.starved = starved_;
		trace.exchanged = exchanged_;
	}
	if (checking_) {
		const Violations violations = check_all(world_.species(), trace, equilibria_);
		CHEM_ASSERT(violations.empty(), violations.empty() ? "" : violations[0].c_str());
	}
	return trace;
}

} // namespace chem
