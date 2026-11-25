// The vertical dimension (GAME_SPEC §6, Stage 47): a vessel's column of layers. Its top is the vessel's own rigid
// cell (its wall, its gas, its uppermost liquid); under it are layer cells, each a homogeneous layer of liquid with
// what is dissolved in it and its solids, empty while the column is mixed. Each step:
//   - gas in a layer rises to the layer over it (it is a bubble);
//   - the layers in use are kept together under the top: a layer with nothing over it rises into its place;
//   - solids sink to the lowest layer of liquid;
//   - two layers merge where the lower is the lighter (it overturns) or where they are alike (one is enough), and
//     the whole column is one cell while its top is stirred;
//   - two layers that stay exchange what is dissolved in them (Fick's law across the distance between their
//     middles) and heat (by conduction, the edge's conductance set at the step's start);
//   - where neighbours differ steeply and a layer is spare, the thicker splits in two halves, which then evolve
//     apart: a gradient is resolved by thin homogeneous layers, never by a cell that is not one;
//   - the top is the size its layers leave it.
// Every move is a transfer along the column's own edges: conserved, and checked as any other.
#include "chem/sim.hpp"

#include "chem/error.hpp"
#include "chem/math.hpp"

#include "chem/thermo.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

namespace chem {

namespace {

// how much two layers differ, in units of what is worth a layer of its own: a kelvin, or a thousandth of the
// solution's mass in any dissolved species (GAME_SPEC §6: design values)
constexpr double SPLIT_ABOVE = 1.0, MERGE_BELOW = 0.1;
constexpr double KELVIN_UNIT = 1.0, FRACTION_UNIT = 1e-3;
constexpr double OVERTURN_BELOW = 1e-9; // the lower lighter by more than this share overturns
constexpr double THINNEST = 2.5e-4; // m: no layer is split below twice this (a diffusion zone is sub-millimetre for minutes)
// Stirred (Stage 49): of the work a stirrer's turning dissipates, this share raises the column's potential energy
// (Linden 1979: the mixing efficiency of stirred stratified fluids, at most 0.2 +- 0.05; Osborn 1980's 0.2;
// docs/DATA_SOURCES.md)
constexpr double MIXING_EFFICIENCY = 0.2;
constexpr double GRAVITY = 9.80665; // m/s^2, standard
// A pour's stream (Stage 51; docs/DATA_SOURCES.md): a round turbulent jet's volume flux grows as 0.32 Q0 x / d0
// (Ricou and Spalding 1961), so by 0.32 sqrt(pi / 4) sqrt(M) a metre of its path, M = Q0 u0 its momentum flux; one
// driven into a denser liquid rises no further than 2.46 M^(3/4) B^(-1/2) (Turner 1966's forced fountain), B its
// buoyancy flux
constexpr double JET_ENTRAINMENT = 0.32, FOUNTAIN_HEIGHT = 2.46;

bool holds(const SpeciesTable &table, const CellState &c, Phase phase) {
	for (size_t s = 0; s < c.amount.size(); ++s) {
		const SpeciesDef &d = table.species(SpeciesId{ static_cast<int32_t>(s) });
		if (c.amount[s] > 0 && !d.wall && d.phase == phase) {
			return true;
		}
	}
	return false;
}

bool empty(const CellState &c) {
	for (const int64_t a : c.amount) {
		if (a != 0) {
			return false;
		}
	}
	return true;
}

// the volume of its liquid with what is dissolved in it, m^3 (the table's molar volumes)
double solution_m3(const SpeciesTable &table, const CellState &c) {
	double v = 0.0;
	for (size_t s = 0; s < c.amount.size(); ++s) {
		const SpeciesDef &d = table.species(SpeciesId{ static_cast<int32_t>(s) });
		if (c.amount[s] > 0 && !d.wall && (d.phase == Phase::Liquid || d.phase == Phase::Aqueous)) {
			v += static_cast<double>(c.amount[s]) * MOL_PER_QUANTUM * d.molar_volume;
		}
	}
	return v;
}

// each dissolved species' share of its solution's mass
std::vector<double> fractions(const SpeciesTable &table, const CellState &c) {
	std::vector<double> w(c.amount.size(), 0.0);
	double total = 0.0;
	for (size_t s = 0; s < c.amount.size(); ++s) {
		const SpeciesDef &d = table.species(SpeciesId{ static_cast<int32_t>(s) });
		if (c.amount[s] > 0 && !d.wall && (d.phase == Phase::Liquid || d.phase == Phase::Aqueous)) {
			const double g = static_cast<double>(c.amount[s]) * MOL_PER_QUANTUM * static_cast<double>(table.molar_mass_ug(SpeciesId{ static_cast<int32_t>(s) }));
			total += g;
			if (d.phase == Phase::Aqueous) {
				w[s] = g;
			}
		}
	}
	for (double &x : w) {
		x = total > 0.0 ? x / total : 0.0;
	}
	return w;
}

double contrast(const SpeciesTable &table, const CellState &a, double ta, const CellState &b, double tb) {
	double out = std::fabs(ta - tb) / KELVIN_UNIT;
	const std::vector<double> wa = fractions(table, a), wb = fractions(table, b);
	for (size_t s = 0; s < wa.size(); ++s) {
		out = std::max(out, std::fabs(wa[s] - wb[s]) / FRACTION_UNIT);
	}
	return out;
}

} // namespace

double jet_entrains(double flux, double speed) {
	return flux > 0.0 && speed > 0.0 ? JET_ENTRAINMENT * 0.8862269254527580 * std::sqrt(flux * speed) : 0.0; // 0.32 Q0 / d0 = 0.32 sqrt(pi / 4) sqrt(M)
}

double fountain_reach(double momentum, double buoyancy) {
	return momentum > 0.0 && buoyancy > 0.0 ? FOUNTAIN_HEIGHT * chem::pow(momentum, 0.75) / std::sqrt(buoyancy) : 0.0;
}

void Simulation::find_columns() {
	if (columns_version_ == world_.topology_version()) {
		return;
	}
	columns_version_ = world_.topology_version();
	columns_.clear();
	const std::span<const Edge> edges = world_.edges();
	const auto cells = static_cast<size_t>(world_.cell_count());
	std::vector<int32_t> below(cells, -1), below_edge(cells, -1);
	std::vector<uint8_t> is_lower(cells, 0);
	for (size_t i = 0; i < edges.size(); ++i) {
		const Edge &e = edges[i];
		if (e.open && e.column) {
			below[static_cast<size_t>(e.b.index)] = e.a.index;
			below_edge[static_cast<size_t>(e.b.index)] = static_cast<int32_t>(i);
			is_lower[static_cast<size_t>(e.a.index)] = 1;
		}
	}
	for (size_t c = 0; c < cells; ++c) {
		// the head of a column: its top, over a layer or not yet (a vessel's column has none until it is layered)
		if (is_lower[c] || !(world_.meta(CellId{ static_cast<int32_t>(c) }).column_volume > 0.0)) {
			continue;
		}
		Column column;
		column.top = CellId{ static_cast<int32_t>(c) };
		column.cells.push_back(column.top);
		for (size_t at = c; below[at] >= 0 && column.cells.size() <= cells;) {
			column.up.push_back(EdgeId{ below_edge[at] });
			at = static_cast<size_t>(below[at]);
			column.cells.push_back(CellId{ static_cast<int32_t>(at) });
		}
		columns_.push_back(std::move(column));
	}
}

// At the step's start (Stage 47): a column with no spare layer, whose steep step wants one, grows by a block of
// COLUMN_BLOCK empty layers under its bottom. From the state alone, so a saved lab grows as an unsaved one does.
void Simulation::grow_columns() {
	find_columns();
	const SpeciesTable &table = world_.species();
	std::vector<CellId> grow;
	for (const Column &col : columns_) {
		const size_t n = col.cells.size();
		if (n > 1 && empty(world_.snapshot_cell(col.cells[n - 1]))) {
			continue; // a spare one there
		}
		const double area = world_.meta(col.top).column_area;
		for (size_t i = 0; i + 1 < n && area > 0.0; ++i) {
			const CellState &u = world_.snapshot_cell(col.cells[i]), &l = world_.snapshot_cell(col.cells[i + 1]);
			const Thermal tu = thermal_state(table, u, &world_.meta(col.cells[i])), tl = thermal_state(table, l);
			if (tu.defined && tl.defined && holds(table, u, Phase::Liquid) && holds(table, l, Phase::Liquid) &&
					contrast(table, u, tu.temperature, l, tl.temperature) > SPLIT_ABOVE &&
					std::max(solution_m3(table, u), solution_m3(table, l)) / area > 2.0 * THINNEST) {
				grow.push_back(col.cells[n - 1]);
				break;
			}
		}
	}
	for (CellId bottom : grow) {
		for (int k = 0; k < COLUMN_BLOCK; ++k) {
			bottom = world_.add_layer_under(bottom);
		}
	}
}

// At the step's start, before anything moves: two layers of liquid conduct heat to each other across the distance
// between their middles, k A / dz (the solvent's conductivity, data/solutions.json); a layer with none, nothing.
void Simulation::configure_columns() {
	find_columns();
	const SpeciesTable &table = world_.species();
	for (const Column &col : columns_) {
		const double area = world_.meta(col.top).column_area;
		for (size_t i = 0; i + 1 < col.cells.size(); ++i) {
			const CellState &upper = world_.snapshot_cell(col.cells[i]), &lower = world_.snapshot_cell(col.cells[i + 1]);
			double g = 0.0;
			const Thermal &tu = thermal(col.cells[i]), &tl = thermal(col.cells[i + 1]);
			if (area > 0.0 && tu.defined && tl.defined && holds(table, upper, Phase::Liquid) && holds(table, lower, Phase::Liquid)) {
				const double dz = 0.5 * (solution_m3(table, upper) + solution_m3(table, lower)) / area;
				const double k = solvent_at(table, 0.5 * (tu.temperature + tl.temperature)).k;
				g = dz > 0.0 ? k * area / dz : 0.0;
			}
			if (world_.edges()[static_cast<size_t>(col.up[i].index)].conductance != g) {
				world_.set_conductance(col.up[i], g);
			}
		}
	}
}

void Simulation::stratify() {
	find_columns();
	for (const Column &col : columns_) {
		stratify(col);
	}
}

void Simulation::stratify(const Column &col) {
	const SpeciesTable &table = world_.species();
	const size_t n = col.cells.size();
	if (n < 2) {
		return;
	}
	const auto state = [&](size_t i) -> const CellState & { return world_.snapshot_cell(col.cells[i]); };
	const auto kelvin = [&](size_t i) { return thermal(col.cells[i]).temperature; };
	// what of a cell is of these phases (never its wall), with its enthalpy at its temperature
	const auto move = [&](size_t from, size_t to, PhaseMask phases, double share) {
		const CellState &c = state(from);
		std::vector<SpeciesAmount> amounts;
		for (size_t s = 0; s < c.amount.size(); ++s) {
			const SpeciesDef &d = table.species(SpeciesId{ static_cast<int32_t>(s) });
			if (c.amount[s] > 0 && !d.wall && (phases & phase_bit(d.phase)) != 0) {
				const auto q = share >= 1.0 ? c.amount[s] : static_cast<int64_t>(static_cast<double>(c.amount[s]) * share);
				if (q > 0) {
					amounts.push_back(SpeciesAmount{ SpeciesId{ static_cast<int32_t>(s) }, Amount{ q } });
				}
			}
		}
		if (amounts.empty()) {
			return;
		}
		const EdgeId edge = col.up[std::min(from, to)];
		// all of a layer goes with all its energy, so it is left empty and holding none
		size_t there = 0;
		for (const int64_t a : c.amount) {
			there += a != 0 ? 1 : 0;
		}
		bool every = there == amounts.size();
		for (const SpeciesAmount &m : amounts) {
			every = every && m.amount.quanta == c.amount[static_cast<size_t>(m.species.index)];
		}
		const bool all = from > 0 && every;
		const Energy energy = all ? Energy{ c.energy } : enthalpy_carried(table, amounts, kelvin(from));
		const Refusal refused = transfer(edge, col.cells[from], amounts, energy);
		CHEM_ASSERT(refused == Refusal::None, refusal_text(refused));
	};
	const auto liquid = [&](size_t i) { return holds(table, state(i), Phase::Liquid); };
	const PhaseMask solids = phase_bit(Phase::Solid), gas = phase_bit(Phase::Gas);
	// gas rises, from the bottom up
	for (size_t i = n - 1; i >= 1; --i) {
		if (holds(table, state(i), Phase::Gas)) {
			move(i, i - 1, gas, 1.0);
		}
	}
	// the layers in use stay together under the top: one with no liquid over it rises into that place
	for (bool again = true; again;) {
		again = false;
		for (size_t i = 1; i < n; ++i) {
			if (!empty(state(i)) && !liquid(i - 1) && (i == 1 || empty(state(i - 1)))) {
				move(i, i - 1, PHASES_ALL & ~gas, 1.0);
				again = true;
			}
		}
	}
	// solids sink to the lowest liquid
	for (size_t i = 0; i + 1 < n; ++i) {
		if (holds(table, state(i), Phase::Solid) && liquid(i + 1)) {
			move(i, i + 1, solids, 1.0);
		}
	}
	// layers merge where the lower is the lighter, or where two are alike and no steep step is beside them
	const auto steep = [&](size_t i) { // between i and i + 1
		return i + 1 < n && liquid(i) && liquid(i + 1) && contrast(table, state(i), kelvin(i), state(i + 1), kelvin(i + 1)) > SPLIT_ABOVE;
	};
	for (int round = 0; round < static_cast<int>(n) * 2; ++round) {
		bool merged = false;
		for (size_t i = 0; i + 1 < n && !merged; ++i) {
			if (!liquid(i) || !liquid(i + 1)) {
				continue;
			}
			double up = 0.0, down = 0.0;
			if (!liquid_density(table, state(i), kelvin(i), up) || !liquid_density(table, state(i + 1), kelvin(i + 1), down)) {
				continue;
			}
			// (a part in a billion: what rounding leaves between two halves of one layer is no instability)
			const bool overturns = down < up * (1.0 - OVERTURN_BELOW);
			const bool alike = contrast(table, state(i), kelvin(i), state(i + 1), kelvin(i + 1)) < MERGE_BELOW && !(i > 0 && steep(i - 1)) && !steep(i + 1);
			if (overturns || alike) {
				move(i + 1, i, PHASES_ALL, 1.0);
				// the layers under it rise a place
				for (size_t k = i + 2; k < n; ++k) {
					if (!empty(state(k))) {
						move(k, k - 1, PHASES_ALL, 1.0);
					}
				}
				merged = true;
			}
		}
		if (!merged) {
			break;
		}
	}
	// Mixed by work (Stage 49, Stage 51): of the work a stirrer's turning or a pour's stream dissipates in the column,
	// MIXING_EFFICIENCY raises its potential energy. Two layers that exchange a volume v (each mixed as it lands) raise
	// it by g (rho_lower - rho_upper) v d, d the distance between their middles; at V_u V_l / (V_u + V_l) they are one,
	// and merge. The work is spent at one interface after another, from the bottom up (a stirrer stands on the floor)
	// or from the top down (a stream lands on the surface): where it pays for less, the two exchange what it pays for,
	// and the work is spent.
	const double area_m2 = world_.meta(col.top).column_area;
	const auto mix = [&](double budget, bool from_top) {
		for (size_t guard = 0; budget > 0.0 && area_m2 > 0.0 && guard < 2 * n; ++guard) {
			size_t i = n; // the interface: between i and i + 1, both liquid
			for (size_t k = 0; k + 1 < n; ++k) {
				const size_t at = from_top ? k : n - 2 - k;
				if (liquid(at) && liquid(at + 1)) {
					i = at;
					break;
				}
			}
			if (i == n) {
				break;
			}
			double up = 0.0, down = 0.0;
			const double vu = solution_m3(table, state(i)), vl = solution_m3(table, state(i + 1));
			if (!liquid_density(table, state(i), kelvin(i), up) || !liquid_density(table, state(i + 1), kelvin(i + 1), down) || !(vu > 0.0) || !(vl > 0.0)) {
				break;
			}
			const double whole = vu * vl / (vu + vl); // the volume exchanged that makes them one
			const double per_m3 = GRAVITY * (down > up ? down - up : 0.0) * 0.5 * (vu + vl) / area_m2; // J a m^3 exchanged
			const double pays = per_m3 > 0.0 ? budget / per_m3 : whole;
			if (pays >= whole) {
				// one: the lower up into the upper, the layers under it up a place
				budget -= per_m3 * whole;
				move(i + 1, i, PHASES_ALL & ~gas, 1.0);
				for (size_t k = i + 2; k < n; ++k) {
					if (!empty(state(k))) {
						move(k, k - 1, PHASES_ALL, 1.0);
					}
				}
				continue;
			}
			// an exchange: v of each into the other, of its liquid and what is dissolved in it
			const PhaseMask wet = phase_bit(Phase::Liquid) | phase_bit(Phase::Aqueous);
			const double tu = kelvin(i), tl = kelvin(i + 1);
			const auto share_of = [&](size_t from, double share) {
				std::vector<SpeciesAmount> amounts;
				const CellState &c = state(from);
				for (size_t s = 0; s < c.amount.size(); ++s) {
					const SpeciesDef &d = table.species(SpeciesId{ static_cast<int32_t>(s) });
					const auto q = static_cast<int64_t>(static_cast<double>(c.amount[s]) * share);
					if (c.amount[s] > 0 && !d.wall && (wet & phase_bit(d.phase)) != 0 && q > 0) {
						amounts.push_back(SpeciesAmount{ SpeciesId{ static_cast<int32_t>(s) }, Amount{ q } });
					}
				}
				return amounts;
			};
			const std::vector<SpeciesAmount> sinks = share_of(i, pays / vu), rises = share_of(i + 1, pays / vl);
			if (!sinks.empty()) {
				const Refusal refused = transfer(col.up[i], col.cells[i], sinks, enthalpy_carried(table, sinks, tu));
				CHEM_ASSERT(refused == Refusal::None, refusal_text(refused));
			}
			if (!rises.empty()) {
				const Refusal refused = transfer(col.up[i], col.cells[i + 1], rises, enthalpy_carried(table, rises, tl));
				CHEM_ASSERT(refused == Refusal::None, refusal_text(refused));
			}
			budget = 0.0;
		}
	};
	// stirred (Stage 49): what the turning dissipates, from the bottom up
	mix(MIXING_EFFICIENCY * world_.meta(col.top).stirring * step_seconds_, false);
	// A pour's stream (Stage 51): what drained into the top this step landed at meta(top).landing: its kinetic energy,
	// half its mass times that speed squared, is the work it does on the column. A jet of volume flux q and momentum
	// flux M = q u, it passes the top (where it lands; it entrains the top's own liquid, its volume flux growing), then
	// each layer under it: one no denser than the jet's mixture it passes whole, a denser one it plunges into no further
	// than a forced fountain rises (Turner), its buoyancy flux g' q against the first denser layer's. Along its path it
	// entrains at Ricou and Spalding's rate and carries what it entrains up into the top, paying for the lift from the
	// work; what is left of the work mixes the layers from the top down.
	{
		const size_t top_at = static_cast<size_t>(col.top.index);
		const double speed = world_.meta(col.top).landing;
		const double q = top_at < landed_m3_.size() ? landed_m3_[top_at] / step_seconds_ : 0.0;
		if (q > 0.0 && speed > 0.0 && area_m2 > 0.0 && liquid(0)) {
			double budget = MIXING_EFFICIENCY * 0.5 * landed_kg_[top_at] * speed * speed; // J
			const double momentum = q * speed, per_metre = jet_entrains(q, speed); // m^2/s
			const double jet_density = landed_kg_[top_at] / landed_m3_[top_at];
			double top_density = 0.0;
			const double top_m3 = solution_m3(table, state(0));
			const bool top_known = liquid_density(table, state(0), kelvin(0), top_density);
			double flux = q + per_metre * (top_known ? top_m3 / area_m2 : 0.0);
			const double mixture = (q * jet_density + (flux - q) * top_density) / flux;
			double reach = -1.0; // how far it plunges, once it meets a denser layer (m)
			// the layers between the top and the one it entrains: their height and their density times it (a cubic
			// metre lifted from layer k into the top raises the column by g [sum over them of (rho_k - rho_j) h_j +
			// (rho_k - rho_top) h_top / 2]: the layers between sink as layer k thins)
			const double top_h = top_m3 / area_m2;
			double between_h = 0.0, between_mass = 0.0;
			for (size_t k = 1; k < n && liquid(k) && top_known && budget > 0.0; ++k) {
				double d = 0.0;
				const double v = solution_m3(table, state(k));
				if (!liquid_density(table, state(k), kelvin(k), d) || !(v > 0.0)) {
					break;
				}
				const double h = v / area_m2;
				if (d > mixture && reach < 0.0) {
					const double buoyancy = GRAVITY * (d - mixture) / d * flux;
					reach = fountain_reach(momentum, buoyancy);
				}
				const double length = reach < 0.0 ? h : std::min(h, reach);
				// what it entrains of this layer, as far as the work pays to lift it to the top's middle
				const double per_m3 = std::max(GRAVITY * (d * between_h - between_mass + (d - top_density) * 0.5 * top_h), 0.0);
				double lift = std::min(per_metre * length * step_seconds_, v);
				if (per_m3 > 0.0) {
					lift = std::min(lift, budget / per_m3);
				}
				budget -= per_m3 * lift;
				// up the column edge by edge into the top, its enthalpy with it
				std::vector<SpeciesAmount> lifted;
				const CellState &c = state(k);
				for (size_t s = 0; s < c.amount.size(); ++s) {
					const SpeciesDef &def = table.species(SpeciesId{ static_cast<int32_t>(s) });
					const auto amount = static_cast<int64_t>(static_cast<double>(c.amount[s]) * (lift / v));
					if (c.amount[s] > 0 && !def.wall && (def.phase == Phase::Liquid || def.phase == Phase::Aqueous) && amount > 0) {
						lifted.push_back(SpeciesAmount{ SpeciesId{ static_cast<int32_t>(s) }, Amount{ amount } });
					}
				}
				if (!lifted.empty()) {
					const Energy carried = enthalpy_carried(table, lifted, kelvin(k));
					for (size_t j2 = k; j2 >= 1; --j2) {
						const Refusal refused = transfer(col.up[j2 - 1], col.cells[j2], lifted, carried);
						CHEM_ASSERT(refused == Refusal::None, refusal_text(refused));
					}
				}
				flux += per_metre * length;
				between_h += h;
				between_mass += d * h;
				if (reach >= 0.0) {
					reach -= length;
					if (!(reach > 0.0)) {
						break;
					}
				}
			}
			mix(budget, true);
		}
	}
	// what is dissolved moves down its gradient between two layers that stay (Fick's law across the distance between
	// their middles), no further than to equal concentrations
	const double area = world_.meta(col.top).column_area;
	const double dt = step_seconds_;
	for (size_t i = 0; i + 1 < n && area > 0.0; ++i) {
		if (!liquid(i) || !liquid(i + 1)) {
			continue;
		}
		const double vu = solution_m3(table, state(i)), vl = solution_m3(table, state(i + 1));
		if (!(vu > 0.0) || !(vl > 0.0)) {
			continue;
		}
		const double dz = 0.5 * (vu + vl) / area, t = 0.5 * (kelvin(i) + kelvin(i + 1));
		std::vector<SpeciesAmount> down_moves, up_moves;
		for (size_t s = 0; s < state(i).amount.size(); ++s) {
			const SpeciesId id{ static_cast<int32_t>(s) };
			if (table.species(id).phase != Phase::Aqueous) {
				continue;
			}
			const double cu = static_cast<double>(state(i).amount[s]) * MOL_PER_QUANTUM / vu, cl = static_cast<double>(state(i + 1).amount[s]) * MOL_PER_QUANTUM / vl;
			const double d = diffusion(table, id, t);
			if (!(d > 0.0) || cu == cl) {
				continue;
			}
			const double most = std::fabs(cu - cl) / (1.0 / vu + 1.0 / vl) * 0.5; // half the way to equal, at most
			const double mol = std::min(d * area * std::fabs(cu - cl) / dz * dt, most);
			const auto q = static_cast<int64_t>(mol / MOL_PER_QUANTUM);
			if (q > 0) {
				(cu > cl ? down_moves : up_moves).push_back(SpeciesAmount{ id, Amount{ q } });
			}
		}
		if (!down_moves.empty()) {
			const Refusal refused = transfer(col.up[i], col.cells[i], down_moves, enthalpy_carried(table, down_moves, kelvin(i)));
			CHEM_ASSERT(refused == Refusal::None, refusal_text(refused));
		}
		if (!up_moves.empty()) {
			const Refusal refused = transfer(col.up[i], col.cells[i + 1], up_moves, enthalpy_carried(table, up_moves, kelvin(i + 1)));
			CHEM_ASSERT(refused == Refusal::None, refusal_text(refused));
		}
	}
	// a steep step and a spare layer: the thicker neighbour splits in two halves (one split a step)
	if (empty(state(n - 1)) && area > 0.0) {
		size_t at = n;
		double steepest = SPLIT_ABOVE;
		for (size_t i = 0; i + 1 < n; ++i) {
			if (liquid(i) && liquid(i + 1)) {
				const double c = contrast(table, state(i), kelvin(i), state(i + 1), kelvin(i + 1));
				if (c > steepest) {
					steepest = c;
					at = i;
				}
			}
		}
		if (at < n) {
			const size_t thick = solution_m3(table, state(at)) >= solution_m3(table, state(at + 1)) ? at : at + 1;
			if (solution_m3(table, state(thick)) / area > 2.0 * THINNEST) {
				// every layer under it down a place, then half of it into the place it leaves
				for (size_t k = n - 1; k > thick + 1; --k) {
					if (!empty(state(k - 1))) {
						move(k - 1, k, PHASES_ALL, 1.0);
					}
				}
				move(thick, thick + 1, PHASES_ALL & ~gas, 0.5);
			}
		}
	}
	// the top is the size its layers leave it
	const CellMeta &top = world_.meta(col.top);
	double below = 0.0;
	for (size_t i = 1; i < n; ++i) {
		const Fill f = fill_of(table, state(i), T0);
		below += f.defined ? f.condensed : 0.0;
	}
	const double size = top.column_volume - below;
	if (size != top.volume && size > 0.0) {
		const Refusal refused = world_.resize_column_top(col.top, size);
		CHEM_ASSERT(refused == Refusal::None, refusal_text(refused));
	}
}

} // namespace chem
