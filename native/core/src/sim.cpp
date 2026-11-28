#include "chem/sim.hpp"

#include "chem/error.hpp"
#include "chem/invariants.hpp"
#include "chem/math.hpp"
#include "chem/thermo.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
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
		if (r.kinetics().law == Kinetics::Law::FirstOrder || r.kinetics().law == Kinetics::Law::Surface || r.kinetics().law == Kinetics::Law::Solution) {
			reactions_.push_back(std::move(r));
		} else if (r.kinetics().law == Kinetics::Law::Equilibrium) {
			equilibria_.push_back(std::move(r));
		}
	}
	// (Stage 81) what each equilibrium consumes each way, as bits
	needs_words_ = (static_cast<size_t>(world_.species().species_count()) + 63) / 64;
	needs_forward_.assign(equilibria_.size() * needs_words_, 0);
	needs_backward_.assign(equilibria_.size() * needs_words_, 0);
	present_.assign(needs_words_, 0);
	for (size_t e = 0; e < equilibria_.size(); ++e) {
		for (const Term &t : equilibria_[e].terms()) {
			const size_t i = static_cast<size_t>(t.species.index);
			(t.nu < 0 ? needs_forward_ : needs_backward_)[e * needs_words_ + i / 64] |= uint64_t{ 1 } << (i % 64);
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
// (Stage 80) `at_none`: the affinity at no extent, the caller's (in the direction, > 0), or 0 where it has none. With
// it the line through no extent and one quantum says where the affinity crosses zero; that quantum and the next are
// asked first (a shift of a few quanta, the usual case, in two evaluations), and the search goes on from them.
int64_t reach(const AffinityProbe &probe, bool forward, int64_t most, double at_none = 0.0) {
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
	// (Stage 70: the affinity at no extent is the caller's; it was asked here again, and the answer put aside unread)
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
	int64_t hi = most;
	bool bracketed = false; // (hi's value asked, and under 0 or undefined)
	if (at_none > 0.0 && finite(at_none) && finite(v_lo) && at_none > v_lo) {
		const double cross = at_none / (at_none - v_lo); // where the line crosses zero, in quanta
		if (cross < static_cast<double>(most - 1)) {
			const int64_t c = std::max<int64_t>(1, static_cast<int64_t>(cross));
			double v_c = v_lo;
			const bool ok_c = c == 1 || value(c, v_c);
			if (ok_c && v_c >= 0.0) {
				lo = c;
				v_lo = v_c;
				double v_next = 0.0;
				const bool ok_next = value(c + 1, v_next);
				if (!(ok_next && v_next >= 0.0)) {
					return c; // the last quantum before it passes zero
				}
				lo = c + 1;
				v_lo = v_next;
			} else {
				hi = c;
				v_hi = v_c;
				ok_hi = ok_c;
				bracketed = true;
			}
		}
	}
	if (!bracketed) {
		ok_hi = value(most, v_hi);
		if (ok_hi && v_hi >= 0.0) {
			return most;
		}
	}
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
// (Stage 57) how much faster a crystal's surface gives up its ions dissolving than it builds them in growing: large enough
// that transport sets the pace at any stirring (calibrated; docs/DATA_SOURCES.md, A crystal dissolving)
constexpr double DETACHMENT = 1000.0;

double primary_nucleation(const Kinetics &kinetics, double ln_s) {
	if (!(ln_s > 0.0)) {
		return 0.0;
	}
	if (kinetics.nucleation_barrier > 0.0) {
		const double exponent = kinetics.nucleation_barrier / (ln_s * ln_s);
		return exponent < 700.0 ? exp(-exponent) : 0.0;
	}
	const double excess = ln_s < 700.0 ? exp(ln_s) - 1.0 : 1e300; // S - 1
	return pow(excess, kinetics.nucleation_order);
}

namespace {

// (Stage 87) the cell's liquid, quanta (what `molality` reads a species' amount against)
double liquid_quanta(const SpeciesTable &table, const CellState &cell) {
	double liquid = 0.0;
	for (size_t s = 0; s < cell.amount.size(); ++s) {
		if (cell.amount[s] > 0) {
			const SpeciesDef &def = table.species(SpeciesId{ static_cast<int32_t>(s) });
			liquid += def.phase == Phase::Liquid && !def.wall ? static_cast<double>(cell.amount[s]) : 0.0;
		}
	}
	return liquid;
}

constexpr double WATER_KG_PER_MOL = 0.018015; // (molality's own: a kg of solvent is 55.51 mol of its liquid)

// k(T) times each species' molality to its order, but `skip`'s
double solution_rate_without(const CellState &cell, const Kinetics &kinetics, double temperature, double liquid, SpeciesId skip) {
	double rate = kinetics.prefactor * exp(-kinetics.activation / (GAS_CONSTANT * temperature));
	for (int32_t o = 0; o < kinetics.order_count; ++o) {
		const RateOrder &order = kinetics.orders[static_cast<size_t>(o)];
		if (order.species == skip || order.order == 0.0) {
			continue;
		}
		const double m = static_cast<double>(cell.amount[static_cast<size_t>(order.species.index)]) / (liquid * WATER_KG_PER_MOL);
		rate *= order.order == 1.0 ? m : pow(m, order.order);
	}
	return rate;
}

double order_of(const Kinetics &kinetics, SpeciesId species) {
	for (int32_t o = 0; o < kinetics.order_count; ++o) {
		if (kinetics.orders[static_cast<size_t>(o)].species == species) {
			return kinetics.orders[static_cast<size_t>(o)].order;
		}
	}
	return 0.0;
}

} // namespace

double solution_rate(const SpeciesTable &table, const CellState &cell, const Kinetics &kinetics, double temperature) {
	const double liquid = liquid_quanta(table, cell);
	if (!(liquid > 0.0) || !(temperature > 0.0)) {
		return 0.0;
	}
	const double rest = solution_rate_without(cell, kinetics, temperature, liquid, kinetics.in) * exp(rate_activity(table, cell, kinetics));
	const double n = order_of(kinetics, kinetics.in);
	const double m = static_cast<double>(cell.amount[static_cast<size_t>(kinetics.in.index)]) / (liquid * WATER_KG_PER_MOL);
	return rest * (n == 1.0 ? m : pow(m, n));
}

double solution_extent(const SpeciesTable &table, const CellState &cell, const Kinetics &kinetics, int32_t nu, double rt, double factor, bool forward, double seconds) {
	const double liquid = liquid_quanta(table, cell);
	if (!(liquid > 0.0) || nu <= 0) {
		return 0.0;
	}
	const double temperature = rt / GAS_CONSTANT;
	const double per = liquid * WATER_KG_PER_MOL; // quanta of a species per mol/kg of it
	const double m = static_cast<double>(cell.amount[static_cast<size_t>(kinetics.in.index)]) / per;
	const double n = order_of(kinetics, kinetics.in);
	// k', mol/kg/s per m^n: the activities as they are at the step's start (Brønsted and Bjerrum's salt effect)
	const double held = solution_rate_without(cell, kinetics, temperature, liquid, kinetics.in) * exp(rate_activity(table, cell, kinetics)) * factor;
	if (!forward) {
		return held * (n == 1.0 ? m : pow(m, n)) * seconds * per;
	}
	const double decay = static_cast<double>(nu) * held * seconds;
	double after = 0.0;
	if (n == 1.0) {
		after = m * exp(-decay);
	} else {
		const double base = pow(m, 1.0 - n) + (n - 1.0) * decay;
		after = base > 0.0 ? pow(base, 1.0 / (1.0 - n)) : 0.0; // (an order under 1 runs out in a finite time)
	}
	return (m - after) / static_cast<double>(nu) * per;
}

SurfaceSteps surface_steps(const SpeciesTable &table, const CellState &cell, const Reaction &reaction, double temperature, double a, double dissipation, const CellMeta *meta) {
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
	// (Stage 57) a crystal dissolving into its solution: its surface gives its ions up a thousand times as fast as it
	// builds them in (a detachment, no lattice to make), so transport sets the pace; the surface's factor still takes
	// the rate to nothing at equilibrium (D3)
	if (kinetics.dissolves_by_transport && forward == (reaction.nu(kinetics.in) < 0)) {
		steps.surface *= DETACHMENT;
		steps.both = steps.surface;
	}
	steps.transport = HUGE_VAL;
	steps.both = steps.surface;
	// by transport: the driver, or else the reaction's dissolved species, carried to the surface or away from it; an
	// ion with the reaction's other ions on its side, in their proportion (Stage 63: an electrolyte moves whole, at
	// Nernst and Hartley's diffusion, sum nu / sum (nu / D))
	// (Stage 65) without a driver, the dissolved species the solution has least of for its coefficient: the scarce ion's
	// gradient is the transport's (a salt dissolving into a solution that takes one of its ions away, sodium hydrogen
	// carbonate into an acid, its own ion a quantum beside a plentiful other); the first of equals, as before
	SpeciesId carried = table.has(kinetics.driver) ? kinetics.driver : SpeciesId{};
	if (!table.has(carried)) {
		double least = HUGE_VAL;
		for (const Term &t : reaction.terms()) {
			if (table.species(t.species).phase == Phase::Aqueous) {
				const double per = static_cast<double>(cell.amount[static_cast<size_t>(t.species.index)]) / static_cast<double>(t.nu < 0 ? -t.nu : t.nu);
				if (per < least) {
					least = per;
					carried = t.species;
				}
			}
		}
	}
	const double d = particle_diameter(table, cell, kinetics.in);
	if (!(d > 0.0)) {
		return steps; // no particles yet (they nucleate)
	}
	if (!table.has(carried)) {
		// (Stage 71) nothing dissolved to carry: a pure substance changing phase at its surface, limited by the heat its
		// change carries between the surface and the liquid round it
		double liquid = 0.0, dh = 0.0;
		for (size_t s = 0; s < cell.amount.size(); ++s) {
			liquid += cell.amount[s] > 0 && table.species(SpeciesId{ static_cast<int32_t>(s) }).phase == Phase::Liquid ? 1.0 : 0.0;
		}
		for (const Term &t : reaction.terms()) {
			const SpeciesDef &def = table.species(t.species);
			dh += t.nu * (def.hf + def.cp * (temperature - T0));
		}
		const SolventPoint w = solvent_at(table, temperature);
		if (!(liquid > 0.0) || !(dh * dh > 1.0) || !(w.k > 0.0) || !(w.nu > 0.0) || !(w.pr > 0.0)) {
			return steps; // in no liquid (heat reaches it as fast as the cell's), or no heat to carry
		}
		const double nusselt = 2.0 + (dissipation > 0.0 ? 0.4 * pow(dissipation * d * d * d * d / (w.nu * w.nu * w.nu), 0.25) * cbrt(w.pr) : 0.0);
		// J/mol of |A|: T - T_eq = A T / dH
		steps.transport = nusselt * w.k / d * (a > 0.0 ? a : -a) * temperature / (dh * dh);
		steps.transported = true;
		steps.both = steps.surface > 0.0 && steps.transport > 0.0 ? steps.surface * steps.transport / (steps.surface + steps.transport) : 0.0;
		return steps;
	}
	const int32_t nu_signed = reaction.nu(carried);
	const double nu = nu_signed < 0 ? -nu_signed : nu_signed;
	std::array<Term, MAX_TERMS> with{}; // the carried species and its fellows, each with its coefficient
	size_t fellows = 0;
	double coefficient = 0.0;
	{
		double count = 0.0, resistance = 0.0;
		for (const Term &t : reaction.terms()) {
			const SpeciesDef &def = table.species(t.species);
			const bool fellow = t.species == carried ||
					(table.species(carried).charge != 0 && def.charge != 0 && def.phase == Phase::Aqueous && (t.nu < 0) == (nu_signed < 0));
			if (!fellow) {
				continue;
			}
			const double dt = diffusion(table, t.species, temperature), n = t.nu < 0 ? -t.nu : t.nu;
			if (!(dt > 0.0)) {
				return steps; // a species whose diffusion is not known: transport puts no limit on it
			}
			count += n;
			resistance += n / dt;
			with[fellows++] = t;
		}
		coefficient = count / resistance;
	}
	if (!(coefficient > 0.0)) {
		return steps;
	}
	// its concentration in the solvent, mol/m^3
	double solvent = 0.0;
	for (size_t s = 0; s < cell.amount.size(); ++s) {
		if (cell.amount[s] == 0) {
			continue; // (Stage 75: it adds nothing, and its volume at T is two of Kell's evaluations)
		}
		const SpeciesDef &def = table.species(SpeciesId{ static_cast<int32_t>(s) });
		solvent += def.phase == Phase::Liquid ? static_cast<double>(cell.amount[s]) * MOL_PER_QUANTUM * molar_volume_at(table, def, temperature) : 0.0; // at T (Stage 67)
	}
	if (!(solvent > 0.0)) {
		return steps;
	}
	// How far it is from C_eq, the concentration at which the reaction would be at equilibrium, all else held:
	// Newton's steps in ln n on the affinity (its activity, coefficients and all), each from the affinity's
	// slope there, until a step would move the gap by under 1%. Near equilibrium (growth, the usual case) one
	// step; with none of it yet (a crystal dissolving into pure water) it starts from one quantum's worth. Its
	// fellows follow it by their coefficients.
	const auto x = static_cast<size_t>(carried.index);
	const bool used = forward == (nu_signed < 0); // this direction uses it up: it is carried to the surface
	CellState here = cell;
	const int64_t n = here.amount[x] > 0 ? here.amount[x] : static_cast<int64_t>(nu);
	const auto put = [&](double amount) { // the carried species at `amount`, its fellows by their share of the change
		const double change = amount - static_cast<double>(cell.amount[x]);
		for (size_t f = 0; f < fellows; ++f) {
			const auto j = static_cast<size_t>(with[f].species.index);
			const double moved = static_cast<double>(cell.amount[j]) + change * with[f].nu / nu_signed;
			here.amount[j] = j == x ? static_cast<int64_t>(amount) : moved < 1.0 ? 1 : static_cast<int64_t>(moved);
		}
	};
	put(static_cast<double>(n));
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
		const CellState held = here;
		put(from + static_cast<double>(dn));
		const double moved = affinity(table, here, reaction.terms(), temperature, defined, meta);
		here = held;
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
		put(next);
		at = affinity(table, here, reaction.terms(), temperature, defined, meta);
		if (!defined || at != at || at == HUGE_VAL || at == -HUGE_VAL) {
			return steps;
		}
	}
	const double c = static_cast<double>(cell.amount[x]) * MOL_PER_QUANTUM / solvent;
	const double c_eq = static_cast<double>(n) * MOL_PER_QUANTUM / solvent * (ratio < -700.0 ? 0.0 : exp(ratio));
	const double gap = c > c_eq ? c - c_eq : c_eq - c;
	// the film's: Sano, Yamaguchi and Adachi (1974), Sh = 2 + 0.4 (eps d^4 / nu^3)^(1/4) Sc^(1/3); 2 at rest (Stage 57)
	double sherwood = 2.0;
	double rho = 0.0; // at T (Stage 67)
	const double mu = solvent_viscosity(table, temperature);
	liquid_density(table, cell, temperature, rho);
	if (dissipation > 0.0 && mu > 0.0 && rho > 0.0) {
		const double kinematic = mu / rho;
		sherwood += 0.4 * pow(dissipation * d * d * d * d / (kinematic * kinematic * kinematic), 0.25) * pow(kinematic / coefficient, 1.0 / 3.0);
	}
	steps.transport = sherwood * coefficient / d * gap / nu;
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
		if (world_.meta(cell).reservoir || world_.meta(cell).frozen) {
			continue; // (Stage 61: a frozen cell takes part in nothing)
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
			// (Stage 87) a solution's rate is nought while a species it goes with is absent: no probe needed
			bool unordered = false;
			for (int32_t o = 0; o < kinetics.order_count; ++o) {
				const RateOrder &order = kinetics.orders[static_cast<size_t>(o)];
				unordered = unordered || (order.order > 0.0 && state.amount[static_cast<size_t>(order.species.index)] == 0);
			}
			// nothing of the solid, and nothing to nucleate it from: no probe needed
			if (unordered || (have == 0 && !(surface && kinetics.nucleation > 0.0 && feasible_extent(state, reaction.terms(), true) > 0))) {
				remainder = 0.0;
				nucleation_remainder = 0.0;
				continue;
			}
			const AffinityProbe probe(table, state, reaction.terms(), thermal(cell), &world_.meta(cell), mixtures(cell));
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
			if (kinetics.law == Kinetics::Law::Solution) {
				wanted = solution_extent(table, state, kinetics, -reaction.nu(kinetics.in), rt, factor, forward, step_seconds_);
			} else if (!surface) {
				const double rate = kinetics.prefactor * exp(-kinetics.activation / rt);
				wanted = static_cast<double>(have) / static_cast<double>(-reaction.nu(kinetics.in)) * (1.0 - exp(-rate * factor * step_seconds_));
			} else {
				const int32_t nu = reaction.nu(kinetics.in);
				const double per = static_cast<double>(nu < 0 ? -nu : nu);
				const bool consumed = forward == (nu < 0); // this direction uses the solid up
				const SpeciesDef &solid = table.species(kinetics.in);
				// mol/m^2/s of the solid: at its surface and by transport, in series (GAME_SPEC §7), as they are at the step's start
				const double dissipation = dissipation_per_kg(table, state, world_.meta(cell).stirring); // W/kg of its liquid
				const double flux = surface_steps(table, state, reaction, probe.temperature(), a, dissipation, &world_.meta(cell)).both * per;
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
				if (!consumed && (kinetics.nucleation > 0.0 || kinetics.secondary > 0.0) && nu == 1) {
					double liquid = 0.0; // quanta of solvent
					for (size_t s = 0; s < state.amount.size(); ++s) {
						if (state.amount[s] > 0 && table.species(SpeciesId{ static_cast<int32_t>(s) }).phase == Phase::Liquid) {
							liquid += static_cast<double>(state.amount[s]);
						}
					}
					const double excess = a / rt < 700.0 ? exp(a / rt) - 1.0 : 1e300; // S - 1
					// from the solution (primary), and (Stage 66) bred from the crystals there by the stirring (secondary:
					// fragments its turning chips off them, each grown from one quantum)
					const double primary = kinetics.nucleation > 0.0 ? kinetics.nucleation * liquid * MOL_PER_QUANTUM * 0.018015 * primary_nucleation(kinetics, a / rt) : 0.0;
					const double bred = kinetics.secondary > 0.0 && dissipation > 0.0 && have > 0
							? kinetics.secondary * dissipation * mol * static_cast<double>(table.molar_mass_ug(kinetics.in)) * 1e-9 * pow(excess, kinetics.secondary_order)
							: 0.0;
					const double nuclei = (primary + bred) * step_seconds_ + nucleation_remainder;
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
				// (Stage 71) a solid with a grain grows as a mush of crystals no larger than it (ice's dendrites in a
				// supercooled liquid): what it grows into is its grains
				const SpeciesDef &solid = table.species(kinetics.in);
				if (forward == (reaction.nu(kinetics.in) > 0) && solid.grain > 0.0 && solid.molar_volume > 0.0) {
					const double volume = static_cast<double>(world_.amount(cell, kinetics.in).quanta) * MOL_PER_QUANTUM * solid.molar_volume;
					const double needed = volume / (0.5235987755982988 * solid.grain * solid.grain * solid.grain);
					const int64_t now = world_.particles(cell, kinetics.in);
					if (needed > static_cast<double>(now) && needed < 1e15) {
						world_.set_particles(cell, kinetics.in, static_cast<int64_t>(needed) + 1);
					}
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

// (Stage 79, GAME_SPEC §6) Pistons: each plunger moved at the speed its forces give it this step (chem/world.hpp's
// Piston), every cell joined to its cell by open edges one space at one pressure P after the moves: an open one at
// the room's; a sealed one's the root of P = (its gas, sum of P_i V_i) / (its free volume after), each piston's move
// rising with P (its solids and liquids incompressible: the plungers stop where they fill it). The hand's work F dx
// along its edge from the mains is the only work: E = U + P_atm V holds the room's on the plunger's back and the gas's
// on its face, friction and drag are heat in the cell; F dx = (P_atm - P) dV + (c v + f)|dx| >= the reversible work,
// with P the pressure after (T2). Then the liquid a sealed column was pushed past runs along its liquid edges into
// the columns joined to it, until their pressures are one.
namespace {

// (Stage 79) the least room a sealed space's plungers leave its gas, a share of its volume: what its liquid may grow
// into as it warms within a step (a ten-thousandth: 0.4 K of water) and its vapour stands in
constexpr double PISTON_CLEARANCE = 1e-4;
// (Stage 79) the sub-steps a step's pistons and connections run in: each vessel its own, the exchange within the step
constexpr int PISTON_SUBSTEPS = 12;

PistonMove piston_move(const Piston &p, double volume, double pressure, double dt) {
	const double push = (pressure - P_ATMOSPHERE) * p.area; // the gas's and the room's, outward
	const auto speed = [&](double hand) { // m/s outward, by the forces with a hand's
		const double net = hand + push, over = std::fabs(net) - p.friction;
		return over > 0.0 ? std::copysign(over / p.drag, net) : 0.0;
	};
	const auto within = [&](double dv) { return std::clamp(volume + dv, p.least, p.most) - volume; };
	// the hand's force that moving at v takes (still: holding it against what friction does not)
	const auto takes = [&](double v) {
		if (v != 0.0) {
			return p.drag * v + std::copysign(p.friction, v) - push;
		}
		const double over = std::fabs(push) - p.friction;
		return over > 0.0 ? -std::copysign(over, push) : 0.0;
	};
	if (!p.held) {
		return { within(speed(0.0) * dt * p.area), 0.0 };
	}
	const double reach = p.hand_speed > 0.0 ? p.hand_speed * dt * p.area : std::numeric_limits<double>::infinity(); // (a hand's pace)
	const double want = within(std::clamp(p.target - volume, -reach, reach));
	const double need = takes(want / p.area / dt);
	if (std::fabs(need) <= p.hand_most) {
		return { want, need };
	}
	const double most = std::copysign(p.hand_most, need);
	const double free = speed(most) * dt * p.area, dv = within(free);
	if (dv == free) {
		return { dv, most }; // (still within its friction: it takes the rest)
	}
	const double at = takes(dv / p.area / dt); // at a stop: what moving there takes, the stop the rest
	return { dv, std::fabs(at) < p.hand_most ? at : most };
}

} // namespace

void Simulation::move_pistons() {
	const auto cells = static_cast<size_t>(world_.cell_count());
	const std::span<const Edge> edges = world_.edges();
	std::vector<CellId> pistons;
	for (size_t c = 0; c < cells; ++c) {
		const CellMeta &m = world_.meta(CellId{ static_cast<int32_t>(c) });
		if (m.piston.area > 0.0 && !m.frozen) {
			pistons.push_back(CellId{ static_cast<int32_t>(c) });
		}
	}
	// the connections between sealed vessels: open edges that pass gas or liquid between two sealed vessels' own cells
	// (not a column's, not a drain); a tube that holds back its flow (Edge::flow) lets through what it allows
	std::vector<size_t> joins;
	bool held_back = false, stiff = false; // (stiff: liquid may cross one: its vessels give only by their bubbles)
	for (size_t i = 0; i < edges.size(); ++i) {
		const Edge &e = edges[i];
		if (!e.open || e.drain || e.column || (e.phases & (PHASES_GAS | phase_bit(Phase::Liquid))) == 0) {
			continue;
		}
		const CellMeta &ma = world_.meta(e.a), &mb = world_.meta(e.b);
		if (ma.reservoir || mb.reservoir || ma.frozen || mb.frozen || !ma.rigid || !mb.rigid || !ma.sealed || !mb.sealed || ma.layer || mb.layer) {
			continue;
		}
		joins.push_back(i);
		held_back = held_back || e.flow > 0.0;
		stiff = stiff || ((e.phases & phase_bit(Phase::Liquid)) != 0 && e.foot != 0);
	}
	if (pistons.empty() && joins.empty()) {
		return;
	}
	world_.rest_pistons(); // (W1: each one's moves this step, none until it moves)
	find_columns();
	// hydraulic links: two vessels joined at their feet, both holding liquid there, each in no other such link: solved
	// together for the whole step (a liquid does not give: a push on one is the other's at once); their pistons and
	// their link then out of the sub-steps
	std::vector<int> links(cells, 0);
	std::vector<size_t> hydraulic;
	const auto holds_liquid_at_foot = [&](CellId top) {
		std::vector<CellId> column{ top };
		for (const Column &col : columns_) {
			if (col.top == top) {
				column = col.cells;
			}
		}
		for (const CellId c : column) {
			const CellState &x = world_.snapshot_cell(c);
			for (size_t s = 0; s < x.amount.size(); ++s) {
				const SpeciesDef &d = world_.species().species(SpeciesId{ static_cast<int32_t>(s) });
				if (x.amount[s] > 0 && !d.wall && d.phase == Phase::Liquid) {
					return true;
				}
			}
		}
		return false;
	};
	for (const size_t i : joins) {
		const Edge &e = edges[i];
		if (e.foot == 3 && (e.phases & phase_bit(Phase::Liquid)) != 0 && holds_liquid_at_foot(e.a) && holds_liquid_at_foot(e.b)) {
			hydraulic.push_back(i);
			++links[static_cast<size_t>(e.a.index)];
			++links[static_cast<size_t>(e.b.index)];
		}
	}
	std::erase_if(hydraulic, [&](size_t i) { return links[static_cast<size_t>(edges[i].a.index)] != 1 || links[static_cast<size_t>(edges[i].b.index)] != 1; });
	std::vector<uint8_t> solved(cells, 0);
	for (const size_t i : hydraulic) {
		if (solve_hydraulic(EdgeId{ static_cast<int32_t>(i) }, step_seconds_)) {
			solved[static_cast<size_t>(edges[i].a.index)] = solved[static_cast<size_t>(edges[i].b.index)] = 1;
		}
	}
	std::erase_if(pistons, [&](CellId c) { return solved[static_cast<size_t>(c.index)] != 0; });
	std::erase_if(joins, [&](size_t i) { return solved[static_cast<size_t>(edges[i].a.index)] != 0 && solved[static_cast<size_t>(edges[i].b.index)] != 0; });
	if (pistons.empty() && joins.empty()) {
		return;
	}
	held_back = std::any_of(joins.begin(), joins.end(), [&](size_t i) { return edges[i].flow > 0.0; });
	stiff = std::any_of(joins.begin(), joins.end(), [&](size_t i) { return (edges[i].phases & phase_bit(Phase::Liquid)) != 0 && edges[i].foot != 0; });
	// in sub-steps, each its pistons alone, exactly, then what its connections let through: each vessel its own, the
	// exchange between them within the step (GAME_SPEC §6, Pistons)
	const int substeps = pistons.empty() && !held_back ? 1 : stiff ? 4 * PISTON_SUBSTEPS : PISTON_SUBSTEPS;
	const double dt = step_seconds_ / substeps;
	for (int k = 0; k < substeps; ++k) {
		for (const CellId c : pistons) {
			move_piston_alone(c, dt);
		}
		for (const size_t i : joins) {
			run_join(EdgeId{ static_cast<int32_t>(i) }, dt);
		}
	}
}

// (Stage 79) A hydraulic link (GAME_SPEC §6, Pistons): two vessels joined at their feet by an edge that passes liquid,
// both holding liquid there (two syringes of water on a tube), solved together for `dt`. Each vessel stays its own:
// the unknown is the liquid x that crosses (from a to b, below 0 the other way); given x, each vessel's piston moves
// alone by its own sealed model on what it then holds (`plan_piston`), at its own pressure after; x is the root of
// x = G (P_a - P_b) dt / viscosity through a tube that holds back its flow, of P_a = P_b through one that does not.
// What crosses carries its enthalpy from its lowest liquid and the flow work of the giver's pressure after the move:
// so each vessel's gas is compressed or let expand at its own pressure after (the second law's, as a piston's), and
// the taker gains what the tube dissipates, (P_g - P_t) x. false when it is no link now (a side without liquid).
bool Simulation::solve_hydraulic(EdgeId edge, double dt) {
	const SpeciesTable &table = world_.species();
	const Edge e = world_.edges()[static_cast<size_t>(edge.index)];
	constexpr double NONE = std::numeric_limits<double>::infinity();
	struct Side {
		CellId top, low;
		std::vector<std::pair<EdgeId, CellId>> up; // each hop up its column: its edge, the cell it leaves
		std::vector<SpeciesAmount> liquid; // its lowest liquid, with what is dissolved in it
		double volume = 0.0, kelvin = T0;
	};
	const auto side_of = [&](CellId top, Side &side) {
		side = Side{};
		side.top = side.low = top;
		std::vector<CellId> column{ top };
		std::vector<EdgeId> up;
		for (const Column &col : columns_) {
			if (col.top == top) {
				column = col.cells;
				up = col.up;
			}
		}
		for (size_t i = column.size(); i-- > 0;) {
			const CellState &x = world_.snapshot_cell(column[i]);
			bool liquid = false;
			for (size_t s = 0; s < x.amount.size(); ++s) {
				const SpeciesDef &d = table.species(SpeciesId{ static_cast<int32_t>(s) });
				liquid = liquid || (x.amount[s] > 0 && !d.wall && d.phase == Phase::Liquid);
			}
			if (!liquid) {
				continue;
			}
			side.low = column[i];
			for (size_t k = i; k-- > 0;) {
				side.up.emplace_back(up[k], column[k + 1]);
			}
			for (size_t s = 0; s < x.amount.size(); ++s) {
				const SpeciesDef &d = table.species(SpeciesId{ static_cast<int32_t>(s) });
				if (x.amount[s] > 0 && !d.wall && (d.phase == Phase::Liquid || d.phase == Phase::Aqueous)) {
					side.liquid.push_back(SpeciesAmount{ SpeciesId{ static_cast<int32_t>(s) }, Amount{ x.amount[s] } });
				}
			}
			const Thermal sealed = thermal_state(table, x, &world_.meta(column[i]));
			const Thermal t = sealed.defined ? sealed : thermal_state(table, x);
			side.kelvin = t.defined && t.temperature > 0.0 ? t.temperature : T0;
			CellState only;
			only.amount.assign(x.amount.size(), 0);
			for (const SpeciesAmount &a : side.liquid) {
				only.amount[static_cast<size_t>(a.species.index)] = a.amount.quanta;
			}
			const Fill f = fill_of(table, only, side.kelvin);
			side.volume = f.defined ? f.condensed : 0.0;
			return side.volume > 0.0;
		}
		return false;
	};
	Side sa, sb;
	if (!side_of(e.a, sa) || !side_of(e.b, sb)) {
		return false;
	}
	const double mu = solvent_viscosity(table, 0.5 * (sa.kelvin + sb.kelvin));
	if (e.flow > 0.0 && !(mu > 0.0)) {
		return false;
	}
	// what crosses at x: its amounts, their volume and enthalpy, and the giver's pressure after (the flow work's)
	std::vector<SpeciesAmount> amounts;
	double volume = 0.0, pushed = P_ATMOSPHERE;
	Energy enthalpy{};
	PistonPlan pa, pb;
	const auto evaluate = [&](double x) {
		const bool out = x > 0.0;
		const Side &giver = out ? sa : sb, &taker = out ? sb : sa;
		amounts.clear();
		volume = 0.0;
		const double share = std::min(1.0, std::fabs(x) / giver.volume);
		for (const SpeciesAmount &a : giver.liquid) {
			const auto n = share >= 1.0 ? a.amount.quanta : static_cast<int64_t>(static_cast<double>(a.amount.quanta) * share);
			if (n > 0) {
				amounts.push_back(SpeciesAmount{ a.species, Amount{ n } });
			}
		}
		neutral_part(table, amounts);
		CellState only;
		only.amount.assign(static_cast<size_t>(table.species_count()), 0);
		for (const SpeciesAmount &a : amounts) {
			only.amount[static_cast<size_t>(a.species.index)] = a.amount.quanta;
		}
		const Fill f = fill_of(table, only, giver.kelvin);
		volume = f.defined ? f.condensed : 0.0;
		enthalpy = enthalpy_carried(table, amounts, giver.kelvin);
		// the giver, at the flow work of its own pressure after (found again with it: twice)
		CellState g = world_.snapshot_cell(giver.top), t = world_.snapshot_cell(taker.top);
		const auto flow = [&](double p) { return std::llround((p - P_ATMOSPHERE) * volume / JOULE_PER_QUANTUM); };
		PistonPlan pg = plan_piston(giver.top, g, 0.0, dt);
		for (int it = 0; it < 2; ++it) {
			pushed = pg.ok ? pg.pressure : P_ATMOSPHERE;
			CellState probe = g;
			double grown = 0.0;
			if (giver.low == giver.top) {
				for (const SpeciesAmount &a : amounts) {
					probe.amount[static_cast<size_t>(a.species.index)] -= a.amount.quanta;
				}
				probe.energy -= enthalpy.quanta + flow(pushed);
			} else {
				probe.energy -= flow(pushed); // (its top pays the flow work; its layers give the liquid)
				grown = volume;
			}
			pg = plan_piston(giver.top, probe, grown, dt);
		}
		pushed = pg.ok ? pg.pressure : pushed;
		for (const SpeciesAmount &a : amounts) {
			t.amount[static_cast<size_t>(a.species.index)] += a.amount.quanta;
		}
		t.energy += enthalpy.quanta + flow(pushed);
		const PistonPlan pt = plan_piston(taker.top, t, 0.0, dt);
		(out ? pa : pb) = pg;
		(out ? pb : pa) = pt;
		const double p_a = pa.ok ? pa.pressure : NONE, p_b = pb.ok ? pb.pressure : NONE;
		if (p_a == NONE && p_b == NONE) {
			return 0.0;
		}
		if (p_a == NONE) {
			return -NONE;
		}
		if (p_b == NONE) {
			return NONE;
		}
		return e.flow > 0.0 ? x - e.flow * (p_a - p_b) / mu * dt : p_b - p_a; // (rising with x)
	};
	double lo = -sb.volume, hi = sa.volume;
	const double r0 = evaluate(0.0);
	if (r0 == 0.0) {
		lo = hi = 0.0;
	} else if (r0 > 0.0) {
		hi = 0.0; // (it runs from b to a)
	} else {
		lo = 0.0;
	}
	for (int i = 0; i < 60 && hi - lo > 1e-15; ++i) {
		const double middle = 0.5 * (lo + hi);
		(evaluate(middle) <= 0.0 ? lo : hi) = middle;
	}
	const double x = std::fabs(lo) < std::fabs(hi) ? lo : hi; // (the nearer to none: the root, from its side)
	evaluate(x);
	if (!amounts.empty() && x != 0.0) {
		const bool out = x > 0.0;
		const Side &giver = out ? sa : sb, &taker = out ? sb : sa;
		for (const auto &[hop, from] : giver.up) {
			const Refusal refused = transfer(hop, from, amounts, enthalpy);
			CHEM_ASSERT(refused == Refusal::None, refusal_text(refused));
			if (refused != Refusal::None) {
				return true;
			}
		}
		const Energy carried{ enthalpy.quanta + std::llround((pushed - P_ATMOSPHERE) * volume / JOULE_PER_QUANTUM) };
		const Refusal refused = transfer(edge, giver.top, amounts, carried);
		CHEM_ASSERT(refused == Refusal::None, refusal_text(refused));
		thermal_valid_[static_cast<size_t>(giver.top.index)] = 0;
		thermal_valid_[static_cast<size_t>(taker.top.index)] = 0;
	}
	// each piston then alone, on what its vessel holds: what the solve found
	for (const CellId c : { e.a, e.b }) {
		if (world_.meta(c).piston.area > 0.0 && !world_.meta(c).frozen) {
			move_piston_alone(c, dt);
		}
	}
	return true;
}

// (Stage 79) One piston moved for `dt` by the forces on it, its vessel alone (GAME_SPEC §6, Pistons): open, at the room's
// pressure; sealed, at the root of P = its own pressure after the move, its hand's work in it, by its sealed model
// (exactly: a gas over a liquid squeezed small is stiffened by the liquid's own expansion, which no estimate sees). Its
// hand's work along its edge from the mains, F dx = (P_atm - P) dV + (c v + f) |dx|, P the pressure after: at least the
// reversible work of a compression, at most what an expansion gives (T2). `plan_piston` the same, on a vessel holding
// `state` in its own cell's volume grown by `grown` (what a hydraulic link's solve asks), applying nothing; a vessel
// with no piston its pressure as it is (none that holds: infinite).
Simulation::PistonPlan Simulation::plan_piston(CellId cell, const CellState &state, double grown, double dt) const {
	const SpeciesTable &table = world_.species();
	const CellMeta &m = world_.meta(cell);
	const Piston &p = m.piston;
	PistonPlan plan;
	const bool piston = p.area > 0.0 && !m.frozen;
	const double volume = piston ? world_.piston_volume(cell) : m.volume;
	if (!m.sealed) {
		plan.mv = piston ? piston_move(p, volume, P_ATMOSPHERE, dt) : PistonMove{};
		plan.ok = true;
		return plan;
	}
	bool defined = false; // (the probe of the last gap: a state its sealed model has)
	double at = 0.0;
	const auto gap = [&](double pr) {
		defined = false;
		plan.mv = piston ? piston_move(p, volume, pr, dt) : PistonMove{};
		CellMeta meta = m;
		meta.volume += grown + plan.mv.dv;
		if (!(meta.volume > 0.0)) {
			return -std::numeric_limits<double>::infinity();
		}
		CellState probe = state;
		if (piston) {
			probe.energy += std::llround(plan.mv.force * plan.mv.dv / p.area / JOULE_PER_QUANTUM);
		}
		if (std::all_of(probe.amount.begin(), probe.amount.end(), [](int64_t a) { return a == 0; })) {
			defined = true;
			at = 0.0;
			return pr; // (empty: a vacuum)
		}
		const Thermal t = thermal_state(table, probe, &meta);
		if (t.defined) {
			defined = t.free_volume > meta.volume * PISTON_CLEARANCE;
			at = t.pressure;
			return defined ? pr - t.pressure : -std::numeric_limits<double>::infinity();
		}
		// none: past where its sealed model holds it. Without room for its gas, above any pressure; with room, on the
		// side its gas's ideal pressure in that room says: squeezed past (a bubble too small), above; moved past where
		// its energy pays for the room (too little for what the room's pressure asks), below
		const Thermal open = thermal_state(table, probe);
		const Fill fill = open.defined ? fill_of(table, probe, open.temperature) : Fill{};
		const double room = fill.defined ? meta.volume - fill.condensed : 0.0;
		if (!(room > meta.volume * PISTON_CLEARANCE)) {
			return -std::numeric_limits<double>::infinity();
		}
		double gas = 0.0;
		for (size_t s = 0; s < probe.amount.size(); ++s) {
			const SpeciesDef &d = table.species(SpeciesId{ static_cast<int32_t>(s) });
			if (!d.wall && d.phase == Phase::Gas && probe.amount[s] > 0) {
				gas += static_cast<double>(probe.amount[s]) * MOL_PER_QUANTUM;
			}
		}
		return pr - gas * GAS_CONSTANT * open.temperature / room;
	};
	if (!piston) {
		gap(0.0);
		plan.ok = defined;
		plan.pressure = defined ? at : std::numeric_limits<double>::infinity();
		return plan;
	}
	double lo = 0.0, hi = 2.0 * P_ATMOSPHERE;
	while (!(gap(hi) >= 0.0) && hi < 1e13) {
		lo = hi;
		hi *= 2.0;
	}
	if (!(gap(hi) >= 0.0)) {
		plan.pressure = std::numeric_limits<double>::infinity(); // no pressure holds it
		return plan;
	}
	for (int i = 0; i < 64 && hi - lo > hi * 1e-13; ++i) {
		const double middle = 0.5 * (lo + hi);
		(gap(middle) >= 0.0 ? hi : lo) = middle;
	}
	plan.pressure = hi;
	gap(plan.pressure);
	if (!defined) {
		// past where its sealed model holds it (more room than its energy pays for: the forces would take it further than
		// it can go): no further than the last that holds
		plan.pressure = lo;
		gap(plan.pressure);
		if (!defined) {
			return plan;
		}
	}
	plan.ok = true;
	return plan;
}

void Simulation::move_piston_alone(CellId cell, double dt) {
	const CellMeta &m = world_.meta(cell);
	const Piston &p = m.piston;
	const double volume = world_.piston_volume(cell);
	const PistonPlan plan = plan_piston(cell, world_.snapshot_cell(cell), 0.0, dt);
	if (!plan.ok) {
		return; // no pressure holds it: as it is
	}
	const PistonMove mv = plan.mv;
	const double pressure = m.sealed ? plan.pressure : P_ATMOSPHERE;
	if (mv.dv == 0.0) {
		const Refusal held = world_.move_piston(cell, volume, mv.force, pressure, 0.0); // (still: what holds it there)
		CHEM_ASSERT(held == Refusal::None, refusal_text(held));
		return;
	}
	const double joule = mv.force * mv.dv / p.area;
	const int64_t whole = std::llround(joule / JOULE_PER_QUANTUM);
	if (whole != 0) {
		const Edge &e = world_.edges()[static_cast<size_t>(p.work.index)];
		const Refusal refused = transfer(p.work, e.a == cell ? e.b : e.a, {}, Energy{ whole });
		CHEM_ASSERT(refused == Refusal::None, refusal_text(refused));
		if (refused != Refusal::None) {
			return;
		}
	}
	const Refusal moved = world_.move_piston(cell, volume + mv.dv, mv.force, pressure, static_cast<double>(whole) * JOULE_PER_QUANTUM);
	CHEM_ASSERT(moved == Refusal::None, refusal_text(moved));
	thermal_valid_[static_cast<size_t>(cell.index)] = 0; // (its volume moved: derived again)
}

// (Stage 79) A connection between two sealed vessels for `dt` (GAME_SPEC §6): what runs is the giver's lowest liquid if
// it holds one there (else its gas), from the higher pressure to the lower, never past the two being one (each vessel's
// own, exactly); through a tube that holds back its flow, no more than it lets through at their difference (a liquid's
// G dP / viscosity, a gas's G (P_a^2 - P_b^2) / (2 viscosity R T)). A liquid carries its enthalpy and the flow work of
// the giver's pressure (its gas pushing it out), the taker gaining what the tube dissipates; a gas its own enthalpy.
void Simulation::run_join(EdgeId edge, double dt) {
	const SpeciesTable &table = world_.species();
	const Edge e = world_.edges()[static_cast<size_t>(edge.index)];
	constexpr double NONE = std::numeric_limits<double>::infinity();
	const auto holds_liquid = [&](const CellState &c) {
		for (size_t s = 0; s < c.amount.size(); ++s) {
			const SpeciesDef &d = table.species(SpeciesId{ static_cast<int32_t>(s) });
			if (c.amount[s] > 0 && !d.wall && d.phase == Phase::Liquid) {
				return true;
			}
		}
		return false;
	};
	// a vessel's own pressure holding `state`, its own cell's volume grown by `grown` (its layers gave that): empty, a
	// vacuum; with no state, none that holds where it has no room and a vacuum where it has
	const auto pressure_of = [&](CellId top, const CellState &state, double grown) {
		CellMeta meta = world_.meta(top);
		meta.volume += grown;
		if (!(meta.volume > 0.0)) {
			return NONE;
		}
		if (std::all_of(state.amount.begin(), state.amount.end(), [](int64_t a) { return a == 0; })) {
			return 0.0;
		}
		const Thermal t = thermal_state(table, state, &meta);
		return t.defined && t.free_volume > meta.volume * PISTON_CLEARANCE ? t.pressure : NONE; // (none: no room, or none that holds)
	};
	const auto kelvin_of = [&](CellId c) {
		const CellState &x = world_.snapshot_cell(c);
		const Thermal sealed = thermal_state(table, x, &world_.meta(c));
		const Thermal t = sealed.defined ? sealed : thermal_state(table, x);
		return t.defined && t.temperature > 0.0 ? t.temperature : T0;
	};
	for (int round = 0; round < 8; ++round) { // (a liquid in several layers: a layer a round)
		const double pa = pressure_of(e.a, world_.snapshot_cell(e.a), 0.0), pb = pressure_of(e.b, world_.snapshot_cell(e.b), 0.0);
		if (!(pa != pb) || (pa == NONE && pb == NONE)) {
			return;
		}
		const bool out = pa > pb;
		const CellId giver_top = out ? e.a : e.b, taker = out ? e.b : e.a;
		const double p_giver = out ? pa : pb, p_taker = out ? pb : pa;
		// its lowest liquid, and the column's edges up from it to its top
		CellId giver = giver_top;
		std::vector<std::pair<EdgeId, CellId>> up;
		for (const Column &col : columns_) {
			if (col.top == giver_top) {
				for (size_t i = col.cells.size(); i-- > 1;) {
					if (holds_liquid(world_.snapshot_cell(col.cells[i]))) {
						giver = col.cells[i];
						for (size_t k = i; k-- > 0;) {
							up.emplace_back(col.up[k], col.cells[k + 1]);
						}
						break;
					}
				}
			}
		}
		// (through its foot its liquid, while it holds one; through its top its gas)
		const bool at_foot = (e.foot & (giver_top == e.a ? 1u : 2u)) != 0;
		const bool liquid = at_foot && holds_liquid(world_.snapshot_cell(giver)) && (e.phases & phase_bit(Phase::Liquid)) != 0;
		if (!liquid) {
			if ((e.phases & PHASES_GAS) == 0) {
				return;
			}
			// its gas: as much as the tube lets through, in parcels down the true pressure only (run_gas)
			const CellState &x = world_.snapshot_cell(giver_top);
			double mol = 0.0;
			for (size_t s = 0; s < x.amount.size(); ++s) {
				mol += x.amount[s] > 0 && table.species(SpeciesId{ static_cast<int32_t>(s) }).phase == Phase::Gas ? static_cast<double>(x.amount[s]) * MOL_PER_QUANTUM : 0.0;
			}
			double most = mol;
			if (e.flow > 0.0) {
				const double kelvin = 0.5 * (kelvin_of(giver_top) + kelvin_of(taker));
				const double mu = gas_viscosity(table, kelvin);
				const double pg = std::min(p_giver, 1e9);
				most = mu > 0.0 ? std::min(mol, e.flow * (pg * pg - p_taker * p_taker) / (2.0 * mu * GAS_CONSTANT * kelvin) * dt) : 0.0;
			}
			if (most > 0.0) {
				run_gas({ giver_top, taker }, { most, -most }, { static_cast<size_t>(edge.index) }, true);
			}
			return;
		}
		const CellState gives = world_.snapshot_cell(giver), top_gives = world_.snapshot_cell(giver_top), takes = world_.snapshot_cell(taker);
		const double kelvin = kelvin_of(giver);
		const Fill was = fill_of(table, gives, kelvin);
		if (!was.defined) {
			return;
		}
		const double flow_pressure = p_giver < NONE ? p_giver : p_taker; // (the giver's own: what pushes it out)
		std::vector<SpeciesAmount> amounts;
		double volume = 0.0;
		const auto share = [&](double f) {
			amounts.clear();
			for (size_t s = 0; s < gives.amount.size(); ++s) {
				const SpeciesDef &d = table.species(SpeciesId{ static_cast<int32_t>(s) });
				if (gives.amount[s] > 0 && !d.wall && (d.phase == Phase::Liquid || d.phase == Phase::Aqueous)) {
					const auto n = f >= 1.0 ? gives.amount[s] : static_cast<int64_t>(static_cast<double>(gives.amount[s]) * f);
					if (n > 0) {
						amounts.push_back(SpeciesAmount{ SpeciesId{ static_cast<int32_t>(s) }, Amount{ n } });
					}
				}
			}
			neutral_part(table, amounts);
			CellState g = gives;
			for (const SpeciesAmount &a : amounts) {
				g.amount[static_cast<size_t>(a.species.index)] -= a.amount.quanta;
			}
			const Fill now = fill_of(table, g, kelvin);
			volume = now.defined ? was.condensed - now.condensed : 0.0;
		};
		const auto carried = [&]() {
			return Energy{ enthalpy_carried(table, amounts, kelvin).quanta + std::llround((flow_pressure - P_ATMOSPHERE) * volume / JOULE_PER_QUANTUM) };
		};
		// the giver's pressure less the taker's, had the share f run: falling with f
		const auto gap = [&](double f) {
			share(f);
			const Energy en = carried();
			CellState g = giver == giver_top ? gives : top_gives, t = takes;
			for (const SpeciesAmount &a : amounts) {
				if (giver == giver_top) {
					g.amount[static_cast<size_t>(a.species.index)] -= a.amount.quanta;
				}
				t.amount[static_cast<size_t>(a.species.index)] += a.amount.quanta;
			}
			if (giver == giver_top) {
				g.energy -= en.quanta;
			} else {
				g.energy -= en.quanta - enthalpy_carried(table, amounts, kelvin).quanta; // (its top pays the flow work)
			}
			t.energy += en.quanta;
			const double pg = pressure_of(giver_top, g, giver == giver_top ? 0.0 : volume), pt = pressure_of(taker, t, 0.0);
			if (pt == NONE) {
				return -1.0;
			}
			return pg == NONE ? 1.0 : pg - pt;
		};
		double lo = 0.0, hi = 1.0;
		if (gap(hi) >= 0.0) {
			lo = hi;
		} else {
			for (int i = 0; i < 50; ++i) {
				const double middle = 0.5 * (lo + hi);
				(gap(middle) >= 0.0 ? lo : hi) = middle;
			}
		}
		// through a tube: no more than it lets through at their difference now
		if (e.flow > 0.0 && lo > 0.0) {
			share(1.0);
			const double all = volume;
			const double mu = solvent_viscosity(table, kelvin);
			const double through = mu > 0.0 ? e.flow * (std::min(p_giver, 1e9) - p_taker) / mu * dt : 0.0;
			lo = all > 0.0 ? std::min(lo, through / all) : 0.0;
		}
		share(lo);
		if (amounts.empty()) {
			return;
		}
		const Energy energy = carried();
		const Energy enthalpy = enthalpy_carried(table, amounts, kelvin);
		for (const auto &[hop, from] : up) {
			const Refusal refused = transfer(hop, from, amounts, enthalpy);
			CHEM_ASSERT(refused == Refusal::None, refusal_text(refused));
			if (refused != Refusal::None) {
				return;
			}
		}
		const Refusal refused = transfer(edge, giver_top, amounts, energy);
		CHEM_ASSERT(refused == Refusal::None, refusal_text(refused));
		thermal_valid_[static_cast<size_t>(giver_top.index)] = 0;
		thermal_valid_[static_cast<size_t>(taker.index)] = 0;
		if (refused != Refusal::None || lo < 1.0) {
			return; // (stopped by the pressures or the tube, not by the layer's end)
		}
	}
}

// (Stage 79) Gas shared out between the sealed cells of a space its pistons moved: `over[i]` mol out of cell i (below
// 0: into it), along the gas edges among them, at the giver's composition and temperature, carrying its enthalpy (an
// ideal gas's carries its own flow work) (GAME_SPEC §6, Pistons).
void Simulation::run_gas(const std::vector<CellId> &cells, std::vector<double> over, const std::vector<size_t> &gas_edges, bool down_only) {
	const SpeciesTable &table = world_.species();
	const std::span<const Edge> edges = world_.edges();
	const auto index_of = [&](CellId c) {
		for (size_t i = 0; i < cells.size(); ++i) {
			if (cells[i] == c) {
				return static_cast<int>(i);
			}
		}
		return -1;
	};
	for (int pass = 0; pass < 8; ++pass) {
		bool ran = false;
		for (const size_t i : gas_edges) {
			const Edge &e = edges[i];
			const int ia = index_of(e.a), ib = index_of(e.b);
			if (ia < 0 || ib < 0) {
				continue;
			}
			const bool out = over[static_cast<size_t>(ia)] > 0.0 && over[static_cast<size_t>(ib)] < 0.0;
			const bool in = over[static_cast<size_t>(ib)] > 0.0 && over[static_cast<size_t>(ia)] < 0.0;
			if (!out && !in) {
				continue;
			}
			const int giver = out ? ia : ib, taker = out ? ib : ia;
			const CellId from = cells[static_cast<size_t>(giver)], to = cells[static_cast<size_t>(taker)];
			// in parcels of at most a twentieth of what it holds, each at its state as it is then: a vessel emptied
			// gives its later gas at its lower pressure and temperature (what stays expands as it would, pushing the
			// rest out), not all of it at its first state
			for (int parcel = 0; parcel < 64; ++parcel) {
				const CellState &x = world_.snapshot_cell(from);
				double level = std::numeric_limits<double>::infinity(); // mol: at most half of what would level them (down_only)
				if (down_only) {
					// (gas runs down its true pressure only: each parcel from the higher to the lower as they then are, no more
					// than half of what an ideal gas's reckoning says would bring them level, so none overshoots)
					const Thermal pg = thermal_state(table, x, &world_.meta(from)), pt = thermal_state(table, world_.snapshot_cell(to), &world_.meta(to));
					if (pg.defined && pt.defined && !(pg.pressure > pt.pressure)) {
						break;
					}
					if (pg.defined && pt.defined && pg.free_volume > 0.0 && pt.free_volume > 0.0) {
						level = 0.5 * (pg.pressure - pt.pressure) / (GAS_CONSTANT * (pg.temperature / pg.free_volume + pt.temperature / pt.free_volume));
					}
				}
				double mol = 0.0;
				for (size_t s = 0; s < x.amount.size(); ++s) {
					mol += x.amount[s] > 0 && table.species(SpeciesId{ static_cast<int32_t>(s) }).phase == Phase::Gas ? static_cast<double>(x.amount[s]) * MOL_PER_QUANTUM : 0.0;
				}
				const double moving = std::min({ over[static_cast<size_t>(giver)], -over[static_cast<size_t>(taker)], mol, 0.05 * mol + 1e-9, level });
				if (!(mol > 0.0) || !(moving > 0.0)) {
					break;
				}
				const double share = moving / mol;
				std::vector<SpeciesAmount> amounts;
				int64_t quanta = 0;
				for (size_t s = 0; s < x.amount.size(); ++s) {
					if (x.amount[s] > 0 && table.species(SpeciesId{ static_cast<int32_t>(s) }).phase == Phase::Gas) {
						const auto n = share >= 1.0 ? x.amount[s] : static_cast<int64_t>(static_cast<double>(x.amount[s]) * share);
						if (n > 0) {
							amounts.push_back(SpeciesAmount{ SpeciesId{ static_cast<int32_t>(s) }, Amount{ n } });
							quanta += n;
						}
					}
				}
				if (amounts.empty()) {
					break;
				}
				const Thermal sealed = thermal_state(table, x, &world_.meta(from));
				const Thermal t = sealed.defined ? sealed : thermal_state(table, x);
				const double kelvin = t.defined && t.temperature > 0.0 ? t.temperature : T0;
				const Refusal refused = transfer(EdgeId{ static_cast<int32_t>(i) }, from, amounts, enthalpy_carried(table, amounts, kelvin));
				CHEM_ASSERT(refused == Refusal::None, refusal_text(refused));
				if (refused != Refusal::None) {
					break;
				}
				const double moved = static_cast<double>(quanta) * MOL_PER_QUANTUM;
				over[static_cast<size_t>(giver)] -= moved;
				over[static_cast<size_t>(taker)] += moved;
				thermal_valid_[static_cast<size_t>(from.index)] = 0;
				thermal_valid_[static_cast<size_t>(to.index)] = 0;
				ran = true;
			}
		}
		if (!ran) {
			break;
		}
	}
}

// (Stage 79) Liquid run between the sealed cells of a space its pistons moved: `takes[i]` m^3 into cell i (below 0:
// out of it), along the liquid edges among them, from the lowest liquid of a giver's column up its layers to its top
// and across, each share's enthalpy from where it lay and, at its top, the flow work of the space's pressure over the
// room's, (P - P_atm) V: what the gas over it does pushing it out (GAME_SPEC §6, Pistons).
void Simulation::run_liquid(const std::vector<CellId> &cells, std::vector<double> takes, double pressure, const std::vector<size_t> &liquid_edges) {
	const SpeciesTable &table = world_.species();
	const std::span<const Edge> edges = world_.edges();
	const auto index_of = [&](CellId c) {
		for (size_t i = 0; i < cells.size(); ++i) {
			if (cells[i] == c) {
				return static_cast<int>(i);
			}
		}
		return -1;
	};
	const auto liquid_of = [&](const CellState &x) {
		std::vector<SpeciesAmount> out;
		for (size_t s = 0; s < x.amount.size(); ++s) {
			const SpeciesDef &d = table.species(SpeciesId{ static_cast<int32_t>(s) });
			if (x.amount[s] > 0 && !d.wall && (d.phase == Phase::Liquid || d.phase == Phase::Aqueous)) {
				out.push_back(SpeciesAmount{ SpeciesId{ static_cast<int32_t>(s) }, Amount{ x.amount[s] } });
			}
		}
		return out;
	};
	const auto kelvin_of = [&](CellId c) {
		const CellState &x = world_.snapshot_cell(c);
		const Thermal sealed = thermal_state(table, x, &world_.meta(c));
		const Thermal t = sealed.defined ? sealed : thermal_state(table, x);
		return t.defined && t.temperature > 0.0 ? t.temperature : T0;
	};
	// the volume of these amounts at kelvin
	const auto volume_of = [&](std::span<const SpeciesAmount> amounts, double kelvin) {
		CellState x;
		x.amount.assign(static_cast<size_t>(table.species_count()), 0);
		for (const SpeciesAmount &a : amounts) {
			x.amount[static_cast<size_t>(a.species.index)] = a.amount.quanta;
		}
		const Fill f = fill_of(table, x, kelvin);
		return f.defined ? f.condensed : 0.0;
	};
	for (int pass = 0; pass < 4; ++pass) {
		bool ran = false;
		for (const size_t i : liquid_edges) {
			const Edge &e = edges[i];
			const int ia = index_of(e.a), ib = index_of(e.b);
			if (ia < 0 || ib < 0) {
				continue;
			}
			const bool out = takes[static_cast<size_t>(ia)] < 0.0 && takes[static_cast<size_t>(ib)] > 0.0;
			const bool in = takes[static_cast<size_t>(ib)] < 0.0 && takes[static_cast<size_t>(ia)] > 0.0;
			if (!out && !in) {
				continue;
			}
			const int giver = out ? ia : ib, taker = out ? ib : ia;
			const CellId top = cells[static_cast<size_t>(giver)], to = cells[static_cast<size_t>(taker)];
			double left = std::min(-takes[static_cast<size_t>(giver)], takes[static_cast<size_t>(taker)]);
			// the giver's column, its lowest liquid first, each cell's hops up to its top
			std::vector<CellId> column{ top };
			std::vector<EdgeId> up;
			for (const Column &col : columns_) {
				if (col.top == top) {
					column = col.cells;
					up = col.up;
				}
			}
			for (size_t k = column.size(); k-- > 0 && left > 0.0;) {
				const CellId from = column[k];
				std::vector<SpeciesAmount> all = liquid_of(world_.snapshot_cell(from));
				if (all.empty()) {
					continue;
				}
				const double kelvin = kelvin_of(from);
				const double there = volume_of(all, kelvin);
				if (!(there > 0.0)) {
					continue;
				}
				const double share = std::min(1.0, left / there);
				std::vector<SpeciesAmount> amounts;
				for (const SpeciesAmount &a : all) {
					const auto n = share >= 1.0 ? a.amount.quanta : static_cast<int64_t>(static_cast<double>(a.amount.quanta) * share);
					if (n > 0) {
						amounts.push_back(SpeciesAmount{ a.species, Amount{ n } });
					}
				}
				neutral_part(table, amounts);
				if (amounts.empty()) {
					continue;
				}
				const double volume = volume_of(amounts, kelvin);
				const Energy enthalpy = enthalpy_carried(table, amounts, kelvin);
				bool ok = true;
				for (size_t h = k; h-- > 0 && ok;) { // up its layers to its top
					const Refusal refused = transfer(up[h], column[h + 1], amounts, enthalpy);
					CHEM_ASSERT(refused == Refusal::None, refusal_text(refused));
					ok = refused == Refusal::None;
				}
				const Energy work{ std::llround((pressure - P_ATMOSPHERE) * volume / JOULE_PER_QUANTUM) };
				const Refusal refused = ok ? transfer(EdgeId{ static_cast<int32_t>(i) }, top, amounts, Energy{ enthalpy.quanta + work.quanta }) : Refusal::BadValue;
				CHEM_ASSERT(refused == Refusal::None, refusal_text(refused));
				if (refused != Refusal::None) {
					break;
				}
				left -= volume;
				takes[static_cast<size_t>(giver)] += volume;
				takes[static_cast<size_t>(taker)] -= volume;
				thermal_valid_[static_cast<size_t>(top.index)] = 0;
				thermal_valid_[static_cast<size_t>(to.index)] = 0;
				ran = true;
			}
		}
		if (!ran) {
			break;
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
				runs += static_cast<double>(upper.amount[s]) * MOL_PER_QUANTUM * molar_volume_at(table, def, thermal.temperature); // at its temperature (Stage 67)
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
			const Thermal &at = this->thermal(edge.b);
			const Fill fill = fill_of(table, world_.snapshot_cell(edge.b), at.defined ? at.temperature : T0);
			// the room below at its own temperature; what runs in, at the upper's (Stage 67: a share of its volume there)
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
					landed_m3_[static_cast<size_t>(edge.b.index)] += mol * molar_volume_at(table, def, thermal.temperature);
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
	// what of a cell's solution runs out to spill `excess` m^3 of its condensed volume (at `kelvin`): a share of the
	// solution, what is dissolved in it as much as its liquid, of the solution's own volume (its solutes' volume with
	// it), so that what runs out is the excess (Stage 27: of the liquid's alone, a solution spilled more than its
	// excess, and left its vessel under-full); what it took, m^3
	const auto spill_from = [&](CellId cell, double excess, double kelvin) {
		const CellState &state = world_.snapshot_cell(cell);
		const Fill fill = fill_of(table, state, kelvin);
		double liquid = 0.0, solids = 0.0; // m^3
		for (size_t s = 0; s < state.amount.size(); ++s) {
			const SpeciesDef &def = table.species(SpeciesId{ static_cast<int32_t>(s) });
			if (state.amount[s] > 0 && !def.wall && (def.phase == Phase::Liquid || def.phase == Phase::Solid)) {
				(def.phase == Phase::Liquid ? liquid : solids) += static_cast<double>(state.amount[s]) * MOL_PER_QUANTUM * molar_volume_at(table, def, kelvin);
			}
		}
		if (!fill.defined || !(liquid > 0.0) || !(excess > 0.0)) {
			return 0.0; // solids alone: nothing can run out (V2 reports it)
		}
		const double solution = std::max(fill.condensed - solids, liquid);
		const double fraction = excess < solution ? excess / solution : 1.0;
		std::vector<SpeciesAmount> amounts;
		for (size_t s = 0; s < state.amount.size(); ++s) {
			const SpeciesId id{ static_cast<int32_t>(s) };
			const SpeciesDef &def = table.species(id);
			if (state.amount[s] > 0 && !def.wall && (def.phase == Phase::Liquid || def.phase == Phase::Aqueous)) {
				// rounded so that what is left fits: up, or (Stage 65) down for a solute whose apparent volume is below
				// nought (an ion's on the ions' footing: OH-, the carbonates), whose quantum more taken leaves more room
				const double share = static_cast<double>(state.amount[s]) * fraction;
				auto out = static_cast<int64_t>(share);
				out += static_cast<double>(out) < share && !(molar_volume_at(table, def, kelvin) < 0.0) ? 1 : 0;
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
			return 0.0;
		}
		const Refusal refused = transfer(edge, cell, amounts, enthalpy_carried(table, amounts, thermal(cell).temperature));
		CHEM_ASSERT(refused == Refusal::None, refusal_text(refused));
		spilled = true;
		return fraction * solution;
	};
	for (int32_t c = 0; c < world_.cell_count(); ++c) {
		const CellId cell{ c };
		const CellMeta &meta = world_.meta(cell);
		if (!meta.rigid || meta.sealed || meta.frozen) {
			continue; // a sealed cell lets nothing out (Stage 28); a frozen one does nothing (Stage 61)
		}
		const Thermal &at = thermal(cell);
		const double kelvin = at.defined ? at.temperature : T0; // its liquids grown to its temperature (Stage 67)
		const Fill fill = fill_of(table, world_.snapshot_cell(cell), kelvin);
		if (fill.defined && fill.condensed > meta.volume) {
			// (Stage 65) to a ten-billionth under its volume: a solution's solutes carried out leave its temperature a
			// hundred-millionth of a kelvin other, and its liquid grown by it, past its volume by a part in 1e12
			spill_from(cell, fill.condensed - meta.volume * (1.0 - 1e-10), kelvin);
		}
	}
	// (Stage 71) a column that holds more than its vessel (another stood in it, its liquids grown): its excess spills
	// from its topmost liquid down, what runs over the brim
	find_columns();
	for (const Column &col : columns_) {
		const CellMeta &top = world_.meta(col.top);
		if (top.sealed || !(top.column_volume > 0.0)) {
			continue;
		}
		const auto kelvin_of = [&](CellId c) {
			const Thermal &t = thermal(c);
			return t.defined && t.temperature > 0.0 ? t.temperature : T0;
		};
		double held = 0.0;
		for (const CellId c : col.cells) {
			const Fill f = fill_of(table, world_.snapshot_cell(c), kelvin_of(c));
			held += f.defined ? f.condensed : 0.0;
		}
		double excess = held - top.column_volume;
		if (!(excess > top.column_volume * 1e-9)) {
			continue; // within what L1 allows: as it was
		}
		for (size_t i = 0; i < col.cells.size() && excess > 0.0; ++i) {
			excess -= spill_from(col.cells[i], excess, kelvin_of(col.cells[i]));
		}
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
			if (a.amount[s] == 0 && b.amount[s] == 0) {
				continue; // (Stage 75: before its diffusion, a power, is worked out)
			}
			const SpeciesId id{ static_cast<int32_t>(s) };
			const double d = gas_diffusion(table, id, t);
			if (!(d > 0.0)) {
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
	// (Stage 64) a cell as it was when it was last found at rest is at rest: what follows depends on nothing else
	const auto c = static_cast<size_t>(cell.index);
	if (rest_key_.size() <= c) {
		rest_key_.resize(c + 1);
	}
	if (rest_key_[c].same(world_, cell)) {
		return false;
	}
	const auto at_rest = [&] { rest_key_[c].keep(world_, cell); };
	bool changed = false;
	// (Stage 53) a cell of nothing but wall (a vessel's ring of glass) has no equilibrium to be at
	bool any = false;
	for (size_t s = 0; s < state.amount.size() && !any; ++s) {
		any = state.amount[s] > 0 && !table.species(SpeciesId{ static_cast<int32_t>(s) }).wall;
	}
	if (!any) {
		at_rest();
		return false;
	}
	// several equilibria that share a species are taken in turn until none
	// moves; each is probed again only if the cell changed since it was last
	// probed (Stage 44: one that cannot move, or that moved last, is at rest
	// until another moves, so a cell with one moving equilibrium settles in
	// the first round)
	seen_.assign(equilibria_.size(), -1); // the cell's version at each one's last probe
	int version = 0;
	// (Stage 65) the cell's state after each round's joint solve: one met again is a cycle, the joint solve's balance
	// not the reactions' own (it holds traces a quantum would overshoot, at affinities they cannot run down), and the
	// rounds left are the reactions' own alone, which settle to their quanta
	std::vector<uint64_t> after_joint;
	bool joint = true;
	// (Stage 65: 64, from 16: a solution's carbon dioxide trading with its gas through water's ionisation and the
	// carbonates, their traces reach()'s alone, takes some thirty rounds to its balance after a pour. Stage 81: 256:
	// copper hydroxide dissolving in a little ammonia as its ammines, the solid project()'s alone and the ammines,
	// ammonia's protonation and water's ionisation joint, takes 130 to 250 rounds to its balance; a cell that settles
	// sooner stops sooner)
	int present_version = -1; // (Stage 81) the version present_ was taken at
	for (int round = 0; round < 256; ++round) {
		bool moved = false;
		for (size_t e = 0; e < equilibria_.size(); ++e) {
			const Reaction &reaction = equilibria_[e];
			if (seen_[e] == version) {
				continue; // nothing changed since it was probed, or since it moved
			}
			seen_[e] = version;
			if (present_version != version) {
				std::fill(present_.begin(), present_.end(), 0);
				for (size_t i = 0; i < state.amount.size(); ++i) {
					if (state.amount[i] > 0) {
						present_[i / 64] |= uint64_t{ 1 } << (i % 64);
					}
				}
				present_version = version;
			}
			bool can_forward = true, can_backward = true; // (Stage 81) all it consumes there, each way
			for (size_t w = 0; w < needs_words_; ++w) {
				can_forward = can_forward && (present_[w] & needs_forward_[e * needs_words_ + w]) == needs_forward_[e * needs_words_ + w];
				can_backward = can_backward && (present_[w] & needs_backward_[e * needs_words_ + w]) == needs_backward_[e * needs_words_ + w];
			}
			if (!can_forward && !can_backward) {
				continue; // none of what it would consume, either way
			}
			if (feasible_extent(state, reaction.terms(), true) == 0 && feasible_extent(state, reaction.terms(), false) == 0) {
				continue; // none of what it would consume, either way
			}
			if (stands(table, mixtures(cell), reaction.terms())) {
				continue; // (Stage 63) past the activity model's range: among dissolved species alone, it stands
			}
			const AffinityProbe probe(table, state, reaction.terms(), thermal(cell), &world_.meta(cell), mixtures(cell));
			double a = 0.0;
			if (!probe.at(0.0, a) || a == 0.0 || a != a) {
				continue;
			}
			const bool forward = a > 0.0;
			const int64_t size = reach(probe, forward, feasible_extent(state, reaction.terms(), forward), forward ? a : -a);
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
		// (Stage 63) equilibria that share a species, taken in turn, close on their joint balance slowly (copper's
		// hydrolysis, the bisulfate and water's ionisation trading hydrogen ions): once each has what it needs, they
		// are solved together, and the round after finishes each to its quantum
		if (joint && solve_together(cell)) {
			++version;
			uint64_t h = 1469598103934665603ull;
			for (const int64_t q : state.amount) {
				h = (h ^ static_cast<uint64_t>(q)) * 1099511628211ull;
			}
			h = (h ^ static_cast<uint64_t>(state.energy)) * 1099511628211ull;
			if (std::find(after_joint.begin(), after_joint.end(), h) != after_joint.end()) {
				joint = false;
			} else {
				after_joint.push_back(h);
			}
		}
	}
	if (!changed) {
		at_rest();
	}
	return changed;
}

// The solution's fast equilibria together (Stage 63, GAME_SPEC §8): each of the equilibrium law whose species are
// all there (Stage 64: a trace too), dissolved, the solvent or a gas in a cell at the room's pressure (Stage 64; a solid's is project()'s alone: it may run out), at the cell's
// temperature:
// a quasi-Newton step on their extents from the affinities themselves (Pitzer's and all), the slope the ideal
// solution's, A_r' = -RT sum_i nu_ri nu_si / n_i, damped so no amount dissolved falls below a tenth of what it was
// (a solid may be used up, its reaction then out of the system); until each is within a quantum of its balance. Its extents are then rounded and applied, each only while it runs down its
// own affinity (T1) and has what it uses; what is left is project()'s. true when anything moved.
constexpr int64_t FEW_QUANTA = 64;

bool Simulation::solve_together(CellId cell) {
	const SpeciesTable &table = world_.species();
	const CellState &state = world_.snapshot_cell(cell);
	constexpr size_t MOST = 16;
	std::array<size_t, MOST> which{};
	size_t k = 0;
	for (size_t e = 0; e < equilibria_.size() && k < MOST; ++e) {
		bool ok = true, dissolved = false;
		for (const Term &t : equilibria_[e].terms()) {
			const SpeciesDef &def = table.species(t.species);
			// (Stage 64) a dissolved trace is taken in too (H+ of a few hundred quanta in a carbonate at pH 10, which the
			// carbonic acid's balance draws through water's ionisation: alone, a round moved only those few hundred)
			// (Stage 64) and a gas the cell holds, in a cell at the room's pressure (a bubble's CO2 and its water's vapour
			// trade with the solution together; a sealed cell's pressure is its own: project()'s)
			const int64_t held = state.amount[static_cast<size_t>(t.species.index)];
			ok = ok && held > 0 && (def.phase != Phase::Gas || !world_.meta(cell).sealed);
			dissolved = dissolved || def.phase == Phase::Aqueous;
		}
		if (ok && dissolved && !stands(table, mixtures(cell), equilibria_[e].terms())) {
			which[k++] = e;
		}
	}
	if (k < 2) {
		return false; // one alone is project()'s
	}
	const Thermal &heat = thermal(cell);
	if (!heat.defined || !(heat.temperature > 0.0)) {
		return false;
	}
	const double t = heat.temperature, rt = GAS_CONSTANT * t;
	const CellMeta &meta = world_.meta(cell);
	CellState here = state;
	std::vector<double> x(state.amount.size());
	for (size_t s = 0; s < x.size(); ++s) {
		x[s] = static_cast<double>(state.amount[s]);
	}
	std::array<double, MOST> extent{}, a{}, step{};
	std::array<bool, MOST> active{};
	active.fill(true);
	std::array<size_t, MOST> on{}; // the active ones, in the system's order
	std::array<std::array<double, MOST + 1>, MOST> m{};
	bool settled = false;
	for (int iteration = 0; iteration < 32; ++iteration) {
		size_t n = 0;
		for (size_t r = 0; r < k; ++r) {
			if (active[r]) {
				on[n++] = r;
			}
		}
		// (Stage 75) here's mixtures once for the n of them
		std::array<std::span<const Term>, MOST> terms_of{};
		std::array<double, MOST> found{};
		std::array<bool, MOST> defined_of{};
		for (size_t i = 0; i < n; ++i) {
			terms_of[i] = equilibria_[which[on[i]]].terms();
		}
		affinities(table, here, std::span<const std::span<const Term>>(terms_of.data(), n), t, &meta, std::span<double>(found.data(), n), std::span<bool>(defined_of.data(), n));
		for (size_t i = 0; i < n; ++i) {
			a[on[i]] = found[i];
			if (!defined_of[i] || a[on[i]] != a[on[i]] || a[on[i]] == HUGE_VAL || a[on[i]] == -HUGE_VAL) {
				return false;
			}
		}
		// the ideal solution's slope, J/mol per quantum (a solid's activity is one: it adds nothing); a gas's, its
		// fraction's of the cell's gas, ln y = ln n - ln N (Stage 64)
		double gas_total = 0.0;
		for (size_t s = 0; s < x.size(); ++s) {
			if (table.species(SpeciesId{ static_cast<int32_t>(s) }).phase == Phase::Gas) {
				gas_total += x[s];
			}
		}
		bool within = true;
		for (size_t i = 0; i < n; ++i) {
			const Reaction &ri = equilibria_[which[on[i]]];
			for (size_t j = 0; j < n; ++j) {
				const Reaction &rj = equilibria_[which[on[j]]];
				double sum = 0.0, gas_i = 0.0, gas_j = 0.0;
				for (const Term &tr : ri.terms()) {
					const Phase phase = table.species(tr.species).phase;
					if (phase != Phase::Aqueous && phase != Phase::Gas) {
						continue;
					}
					sum += static_cast<double>(tr.nu) * static_cast<double>(rj.nu(tr.species)) / x[static_cast<size_t>(tr.species.index)];
					gas_i += phase == Phase::Gas ? static_cast<double>(tr.nu) : 0.0;
				}
				for (const Term &tr : rj.terms()) {
					gas_j += table.species(tr.species).phase == Phase::Gas ? static_cast<double>(tr.nu) : 0.0;
				}
				if (gas_total > 0.0) {
					sum -= gas_i * gas_j / gas_total;
				}
				m[i][j] = -rt * sum;
			}
			m[i][n] = -a[on[i]];
			within = within && std::fabs(a[on[i]]) <= std::fabs(m[i][i]); // within a quantum of it
		}
		if (within) {
			settled = true;
			break;
		}
		// Gauss's elimination with partial pivoting
		size_t dependent = MOST;
		double scale = 0.0; // the slopes' size, for what counts as no pivot
		for (size_t i = 0; i < n; ++i) {
			for (size_t j = 0; j < n; ++j) {
				scale = std::max(scale, std::fabs(m[i][j]));
			}
		}
		for (size_t c = 0; c < n; ++c) {
			size_t pivot = c;
			for (size_t r = c + 1; r < n; ++r) {
				if (std::fabs(m[r][c]) > std::fabs(m[pivot][c])) {
					pivot = r;
				}
			}
			if (!(std::fabs(m[pivot][c]) > 1e-12 * scale)) {
				dependent = on[c]; // the reactions are not independent: one of them leaves the system (its balance is the others')
				break;
			}
			std::swap(m[c], m[pivot]);
			for (size_t r = c + 1; r < n; ++r) {
				const double f = m[r][c] / m[c][c];
				for (size_t q = c; q <= n; ++q) {
					m[r][q] -= f * m[c][q];
				}
			}
		}
		if (dependent < MOST) {
			active[dependent] = false;
			continue;
		}
		for (size_t c = n; c-- > 0;) {
			double v = m[c][n];
			for (size_t q = c + 1; q < n; ++q) {
				v -= m[c][q] * step[on[q]];
			}
			step[on[c]] = v / m[c][c];
		}
		// damped: nothing dissolved below a tenth of what it holds; a solid may be used up exactly, and its reaction
		// then leaves the system
		const auto change_of = [&](size_t s) {
			double change = 0.0;
			for (size_t i = 0; i < n; ++i) {
				change += static_cast<double>(equilibria_[which[on[i]]].nu(SpeciesId{ static_cast<int32_t>(s) })) * step[on[i]];
			}
			return change;
		};
		double lambda = 1.0;
		size_t used_up = x.size();
		for (size_t s = 0; s < x.size(); ++s) {
			const double change = change_of(s);
			if (!(change < 0.0) || !(x[s] > 0.0)) {
				continue;
			}
			if (table.species(SpeciesId{ static_cast<int32_t>(s) }).phase == Phase::Solid) {
				if (x[s] / -change < lambda) {
					lambda = x[s] / -change;
					used_up = s;
				}
			} else if (0.9 * x[s] / -change < lambda) {
				lambda = 0.9 * x[s] / -change;
				used_up = x.size();
			}
		}
		for (size_t i = 0; i < n; ++i) {
			extent[on[i]] += lambda * step[on[i]];
		}
		for (size_t s = 0; s < x.size(); ++s) {
			const double change = change_of(s) * lambda;
			if (change != 0.0) {
				x[s] = s == used_up ? 0.0 : x[s] + change;
				here.amount[s] = x[s] > 0.0 ? std::max<int64_t>(1, std::llround(x[s])) : 0; // what is left is there
			}
		}
		// a solid used up, or a dissolved species down to a few quanta (the limit of its rounding): its reactions leave
		// the system, reach()'s alone
		for (size_t q = 0; q < x.size(); ++q) {
			const Phase phase = table.species(SpeciesId{ static_cast<int32_t>(q) }).phase;
			const bool gone = q == used_up || (x[q] < static_cast<double>(FEW_QUANTA) && x[q] < 0.5 * static_cast<double>(state.amount[q]) &&
					(phase == Phase::Aqueous || phase == Phase::Gas));
			if (!gone) {
				continue;
			}
			for (size_t i = 0; i < n; ++i) {
				if (equilibria_[which[on[i]]].nu(SpeciesId{ static_cast<int32_t>(q) }) != 0) {
					active[on[i]] = false;
				}
			}
		}
	}
	if (!settled) {
		return false;
	}
	std::array<int64_t, MOST> go{};
	int64_t most = 0;
	for (size_t r = 0; r < k; ++r) {
		go[r] = static_cast<int64_t>(std::llround(extent[r]));
		most = std::max(most, go[r] < 0 ? -go[r] : go[r]);
	}
	if (most <= 1) {
		return false; // a quantum here or there: project()'s own reach finishes it exactly
	}
	bool any = false;
	for (size_t pass = 0; pass < k; ++pass) {
		bool progressed = false;
		for (size_t r = 0; r < k; ++r) {
			if (go[r] == 0) {
				continue;
			}
			const Reaction &reaction = equilibria_[which[r]];
			const bool forward = go[r] > 0;
			if (feasible_extent(state, reaction.terms(), forward) < (forward ? go[r] : -go[r])) {
				continue;
			}
			const AffinityProbe probe(table, state, reaction.terms(), thermal(cell), &meta, mixtures(cell));
			double now = 0.0;
			if (!probe.at(0.0, now) || now == 0.0 || now != now || (now > 0.0) != forward) {
				continue;
			}
			const Refusal refused = transform(cell, reaction, go[r]);
			CHEM_ASSERT(refused == Refusal::None, refusal_text(refused));
			go[r] = 0;
			progressed = any = true;
		}
		if (!progressed) {
			break;
		}
	}
	return any;
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
void Simulation::equilibrate() {
	begin_step();
	if (world_.thermal()) {
		process_ = Process::Settle;
		settle();
	}
}

std::optional<World> build_settled(World::Builder builder, std::span<const Reaction> reactions, std::string *why) {
	std::optional<World> world = builder.build(why);
	for (int round = 0; world && world->thermal() && round < 16; ++round) {
		// the builder's own cells: settling grows a column's layers (empty), which the builder does not have
		const int cells = world->cell_count();
		Simulation sim(*world, std::vector<Reaction>(reactions.begin(), reactions.end()));
		sim.set_checking(false);
		sim.equilibrate();
		const World &settled = sim.world();
		bool moved = false;
		for (int c = 0; c < cells; ++c) {
			const CellId cell{ c };
			double before = 0.0, after = 0.0;
			if (world->temperature(cell, before) && settled.temperature(cell, after) && std::abs(after - before) > 1e-6) {
				moved = true;
			}
			// and nothing moved at all: a cell made exactly full (its gas drawn or expelled) is not yet the world returned
			moved = moved || world->snapshot_cell(cell).amount != settled.snapshot_cell(cell).amount;
		}
		if (!moved) {
			break;
		}
		const SpeciesTable &table = settled.species();
		for (int c = 0; c < cells; ++c) {
			const CellId cell{ c };
			const CellState &state = settled.snapshot_cell(cell);
			for (int k = 0; k < table.species_count(); ++k) {
				const SpeciesId s{ k };
				if (table.species(s).retired) {
					continue;
				}
				builder.set_amount(cell, s, Amount{ state.amount[static_cast<size_t>(k)] });
				if (table.species(s).phase == Phase::Solid && !table.species(s).wall) {
					builder.set_particles(cell, s, state.particles[static_cast<size_t>(k)]);
				}
			}
		}
		world = builder.build(why);
	}
	return world;
}

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
		if (world_.meta(cell).layer && !world_.meta(cell).frozen && thermal(cell).defined) {
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
		if (!m.reservoir && !m.rigid && !m.layer && !m.frozen) { // (a layer is no root of the air: Stage 47; a frozen cell none at all: Stage 61)
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
		if (depth_[c] != -1 || m.reservoir || !m.rigid || m.frozen) {
			continue;
		}
		const size_t begin = sealed_order_.size();
		depth_[c] = 0;
		sealed_order_.push_back(c);
		for (size_t head = begin; head < sealed_order_.size(); ++head) {
			const size_t at = sealed_order_[head];
			for (size_t i = 0; i < edges.size(); ++i) {
				const Edge &e = edges[i];
				// (Stage 79: a tube that holds back its flow runs its own, in the pistons' process: not at once here)
				if (!e.open || (e.phases & PHASES_GAS) == 0 || e.flow > 0.0 || (static_cast<size_t>(e.a.index) != at && static_cast<size_t>(e.b.index) != at)) {
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
			if (round == ROUNDS - 1) {
				// (Stage 79) still running after its rounds (two syringes far apart, one pulled empty): each at its
				// equilibria with what it holds at the end, the last of the gas to come in the next step (P1)
				for (size_t k = end; k-- > begin;) {
					project(CellId{ static_cast<int32_t>(sealed_order_[k]) });
				}
			}
		}
	}
}

// A cell's thermal state, derived once and kept until a primitive touches the
// cell: the same value thermal_state() would give, many times cheaper.
const Thermal &Simulation::thermal(CellId cell) {
	const auto c = static_cast<size_t>(cell.index);
	if (!thermal_valid_[c]) {
		kept_thermal(cell);
		thermal_valid_[c] = 1;
	}
	return thermal_[c];
}

// (Stage 53) derived again only if what it is derived from changed since: the same value, exactly
const Thermal &Simulation::kept_thermal(CellId cell) const {
	const auto c = static_cast<size_t>(cell.index);
	if (thermal_key_.size() <= c) {
		thermal_key_.resize(c + 1);
	}
	if (thermal_.size() <= c) {
		thermal_.resize(c + 1);
	}
	ThermalKey &key = thermal_key_[c];
	if (!key.same(world_, cell)) {
		if (sums_.size() <= c) {
			sums_.resize(c + 1);
		}
		KeptSums &kept = sums_[c];
		const CellState &state = world_.snapshot_cell(cell);
		if (!kept.known || kept.revision != world_.matter_revision(cell)) {
			kept.sums = matter_sums(world_.species(), state);
			kept.revision = world_.matter_revision(cell);
			kept.known = true;
		}
#if CHEM_ASSERT_ABORTS
		{
			const MatterSums now = matter_sums(world_.species(), state);
			CHEM_ASSERT(now.known == kept.sums.known && now.formation == kept.sums.formation && now.capacity == kept.sums.capacity,
					"a cell's matter changed under the same matter revision (World::touch)");
		}
#endif
		thermal_[c] = thermal_state(world_.species(), state, &world_.meta(cell), kept.sums);
		key.keep(world_, cell);
	}
	return thermal_[c];
}

const CellMixtures &Simulation::mixtures(CellId cell) {
	const auto c = static_cast<size_t>(cell.index);
	if (mixtures_.size() <= c) {
		mixtures_.resize(c + 1);
	}
	KeptMixtures &kept = mixtures_[c];
	if (!kept.known || kept.revision != world_.matter_revision(cell)) {
		kept.mixtures = cell_mixtures(world_.species(), world_.snapshot_cell(cell));
		kept.revision = world_.matter_revision(cell);
		kept.known = true;
	}
#if CHEM_ASSERT_ABORTS
	{
		const CellMixtures now = cell_mixtures(world_.species(), world_.snapshot_cell(cell));
		CHEM_ASSERT(now.gas == kept.mixtures.gas && now.liquid == kept.mixtures.liquid && now.particles == kept.mixtures.particles,
				"a cell's matter changed under the same matter revision (World::touch)");
	}
#endif
	return kept.mixtures;
}

bool Simulation::ThermalKey::same(const World &world, CellId cell) const {
	if (!known || revision != world.revision(cell)) {
		return false;
	}
#if CHEM_ASSERT_ABORTS
	const CellState &state = world.snapshot_cell(cell);
	const CellMeta &meta = world.meta(cell);
	CHEM_ASSERT(energy == state.energy && volume == meta.volume && sealed == meta.sealed && rigid == meta.rigid && reservoir == meta.reservoir &&
						amount == state.amount,
			"a cell changed under the same revision: a write to it drew none (World::touch)");
#endif
	return true;
}

void Simulation::ThermalKey::keep(const World &world, CellId cell) {
	known = true;
	revision = world.revision(cell);
#if CHEM_ASSERT_ABORTS
	const CellState &state = world.snapshot_cell(cell);
	const CellMeta &meta = world.meta(cell);
	energy = state.energy;
	amount = state.amount;
	volume = meta.volume;
	sealed = meta.sealed;
	rigid = meta.rigid;
	reservoir = meta.reservoir;
#endif
}

bool Simulation::temperature(CellId cell, double &kelvin) const {
	if (cell.index < 0 || cell.index >= world_.cell_count()) {
		return false;
	}
	const CellMeta &meta = world_.meta(cell);
	if (meta.reservoir) {
		kelvin = meta.reservoir_temperature;
		return true;
	}
	const Thermal &t = kept_thermal(cell);
	kelvin = t.temperature;
	return t.defined;
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
	in_step_ = false;
	grow_columns(); // (Stage 47: before anything is sized by the cells)
	const auto cells = static_cast<size_t>(world_.cell_count());
	thermal_.resize(cells);
	thermal_valid_.assign(cells, 0);
	in_step_ = true;
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
		// (Stage 79) the pistons first: every other process this step at their volumes
		process_ = Process::Work;
		move_pistons();
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
	in_step_ = false;
}

void Simulation::restore_flows(StepFlows flows) {
	const size_t edges = world_.edges().size() * PROCESSES, cells = static_cast<size_t>(world_.cell_count());
	const size_t reactions = reactions_.size() + equilibria_.size();
	// (as the step left them: a command since may have added an edge or a cell, which no flow of that step crossed)
	if (flows.reactions != reactions || flows.energy.size() > edges || flows.grams.size() != flows.energy.size() || flows.energy.size() % PROCESSES != 0 ||
			flows.extent.size() > cells * reactions || (reactions > 0 && flows.extent.size() % reactions != 0)) {
		flows_ = StepFlows{};
		return;
	}
	flows_ = std::move(flows);
}

StepTrace Simulation::step() {
	StepTrace trace;
	step(trace);
	return trace;
}

void Simulation::step(StepTrace &trace) {
	if (!traced_) {
		world_.rebase(); // steps were skipped: this one's trace starts here
		traced_ = true;
	}
	begin_step();
	if (world_.thermal()) {
		// (Stage 79) the pistons first: every other process this step at their volumes
		process_ = Process::Work;
		move_pistons();
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
	world_.close_step(trace);
	in_step_ = false;
	if (world_.thermal()) {
		trace.starved = starved_;
		trace.exchanged = exchanged_;
	}
	if (checking_) {
		const Violations violations = check_all(world_.species(), trace, equilibria_);
		CHEM_ASSERT(violations.empty(), violations.empty() ? "" : violations[0].c_str());
	}
}

} // namespace chem
