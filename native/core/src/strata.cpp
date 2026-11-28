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
// solution's mass in what is dissolved, summed over its species (GAME_SPEC §6: design values)
constexpr double SPLIT_ABOVE = 1.0, MERGE_BELOW = 0.1;
constexpr double KELVIN_UNIT = 1.0, FRACTION_UNIT = 1e-3;
constexpr double OVERTURN_BELOW = 1e-9; // the lower lighter by more than this share overturns
constexpr double TRACE_SHARE = 1e-3; // (Stage 63) an ion carrying less of a layer's conductance than this is a trace
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

// the volume of its liquid with what is dissolved in it at its temperature, m^3 (the table's molar volumes, grown
// with the solvent: Stage 67)
double solution_m3(const SpeciesTable &table, const CellState &c, double kelvin) {
	double v = 0.0;
	for (size_t s = 0; s < c.amount.size(); ++s) {
		const SpeciesDef &d = table.species(SpeciesId{ static_cast<int32_t>(s) });
		if (c.amount[s] > 0 && !d.wall && (d.phase == Phase::Liquid || d.phase == Phase::Aqueous)) {
			v += static_cast<double>(c.amount[s]) * MOL_PER_QUANTUM * d.molar_volume;
		}
	}
	return v != 0.0 ? v * expansion(table, kelvin) : 0.0;
}

// a cell's temperature for its volume: its own, or 25 C where it has none (an empty layer)
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
	// what is dissolved: the sum of the differences of its species' shares (Stage 63: a salt is its ions, and the sum
	// is the salt's own difference whichever way it is split)
	const std::vector<double> wa = fractions(table, a), wb = fractions(table, b);
	double dissolved = 0.0;
	for (size_t s = 0; s < wa.size(); ++s) {
		dissolved += std::fabs(wa[s] - wb[s]);
	}
	return std::max(out, dissolved / FRACTION_UNIT);
}

// A column's heights (Stage 51): the height over its floor under which its inside holds a volume, and its
// cross-section at a height. By its shape (the inside's volume at heights a rise apart) where it has one; past its
// last point, as its last segment; a cylinder of its area where it has none.
double height_of(const CellMeta &m, double volume) {
	const std::vector<double> &v = m.column_shape;
	if (v.size() < 2 || !(m.column_rise > 0.0)) {
		return m.column_area > 0.0 ? volume / m.column_area : 0.0;
	}
	if (!(volume > v.front())) {
		return 0.0;
	}
	const size_t k = std::min<size_t>(static_cast<size_t>(std::upper_bound(v.begin(), v.end(), volume) - v.begin()), v.size() - 1) - 1;
	const double span = v[k + 1] - v[k];
	return (static_cast<double>(k) + (span > 0.0 ? (volume - v[k]) / span : 0.0)) * m.column_rise;
}

double area_at(const CellMeta &m, double height) {
	const std::vector<double> &v = m.column_shape;
	if (v.size() < 2 || !(m.column_rise > 0.0)) {
		return m.column_area;
	}
	const auto k = static_cast<size_t>(std::clamp(height / m.column_rise, 0.0, static_cast<double>(v.size() - 2)));
	const double a = (v[k + 1] - v[k]) / m.column_rise;
	return a > 0.0 ? a : m.column_area;
}

// Each cell of a column, from the top (0) down: the heights over the floor of its liquid's bottom, middle (by
// volume) and top, stacked from the bottom up; and the cross-section across the interface under it
struct Levels {
	std::vector<double> bottom, middle, top, across;
	double thick(size_t i) const { return top[i] - bottom[i]; }
};

// (Stage 72) each cell's temperature from the simulation's kept thermal state, `kelvin(cell)`: T0 where it has none
template <typename Kelvin>
Levels levels_of(const SpeciesTable &table, const World &world, CellId top_cell, const std::vector<CellId> &cells, Kelvin kelvin) {
	const CellMeta &m = world.meta(top_cell);
	const size_t n = cells.size();
	Levels out{ std::vector<double>(n), std::vector<double>(n), std::vector<double>(n), std::vector<double>(n) };
	double below = 0.0;
	for (size_t k = n; k-- > 0;) {
		const double v = solution_m3(table, world.snapshot_cell(cells[k]), kelvin(cells[k]));
		out.bottom[k] = height_of(m, below);
		out.middle[k] = height_of(m, below + 0.5 * v);
		out.top[k] = height_of(m, below + v);
		out.across[k] = area_at(m, out.bottom[k]);
		below += v;
	}
	return out;
}

} // namespace

double Simulation::column_kelvin(CellId cell) {
	const Thermal &t = in_step_ ? thermal(cell) : kept_thermal(cell);
	return t.defined && t.temperature > 0.0 ? t.temperature : T0;
}

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
		if (is_lower[c] || !(world_.meta(CellId{ static_cast<int32_t>(c) }).column_volume > 0.0) || world_.meta(CellId{ static_cast<int32_t>(c) }).frozen) {
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
		const Levels at = levels_of(table, world_, col.top, col.cells, [this](CellId c) { return column_kelvin(c); });
		for (size_t i = 0; i + 1 < n && area > 0.0; ++i) {
			const CellState &u = world_.snapshot_cell(col.cells[i]), &l = world_.snapshot_cell(col.cells[i + 1]);
			const Thermal tu = thermal_state(table, u, &world_.meta(col.cells[i])), tl = thermal_state(table, l);
			if (tu.defined && tl.defined && holds(table, u, Phase::Liquid) && holds(table, l, Phase::Liquid) &&
					contrast(table, u, tu.temperature, l, tl.temperature) > SPLIT_ABOVE &&
					std::max(at.thick(i), at.thick(i + 1)) > 2.0 * THINNEST) {
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
		const Levels at = levels_of(table, world_, col.top, col.cells, [this](CellId c) { return column_kelvin(c); });
		for (size_t i = 0; i + 1 < col.cells.size(); ++i) {
			const CellState &upper = world_.snapshot_cell(col.cells[i]), &lower = world_.snapshot_cell(col.cells[i + 1]);
			double g = 0.0;
			const Thermal &tu = thermal(col.cells[i]), &tl = thermal(col.cells[i + 1]);
			if (area > 0.0 && tu.defined && tl.defined && holds(table, upper, Phase::Liquid) && holds(table, lower, Phase::Liquid)) {
				const double dz = at.middle[i] - at.middle[i + 1]; // (Stage 51: by the column's shape)
				const double k = solvent_at(table, 0.5 * (tu.temperature + tl.temperature)).k;
				g = dz > 0.0 ? k * at.across[i] / dz : 0.0;
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
	// what its volumes are read at (Stage 67): its temperature, or 25 C where it has none (an empty layer)
	const auto volume_kelvin = [&](size_t i) {
		const Thermal &t = thermal(col.cells[i]);
		return t.defined && t.temperature > 0.0 ? t.temperature : T0;
	};
	// (Stage 71) a layer's liquid's density at its temperature, kg/m^3 (0 with none), and a solid's own
	const auto density_of = [&](size_t i) {
		double rho = 0.0;
		return liquid_density(table, state(i), volume_kelvin(i), rho) ? rho : 0.0;
	};
	const auto solid_density = [&](SpeciesId s) {
		const SpeciesDef &d = table.species(s);
		return d.molar_volume > 0.0 ? static_cast<double>(table.molar_mass_ug(s)) * 1e-9 / d.molar_volume : 0.0;
	};
	// (Stage 88) in a sealed vessel what moves between its cells carries its enthalpy at the giver's state: the table's
	// and what the vessel's pressure adds (its top's own, a layer's the one it stands at)
	const bool pressed = world_.meta(col.top).sealed;
	// (at the temperature given: read where it was read before this stage, so an open vessel's is the same to the bit)
	const auto carried_from = [&](size_t from, std::span<const SpeciesAmount> amounts, double at_kelvin) {
		Energy energy = enthalpy_carried(table, amounts, at_kelvin);
		if (pressed) {
			const double over = from == 0 ? thermal(col.top).pressure - P_ATMOSPHERE : world_.meta(col.cells[from]).over;
			energy.quanta += std::llround(enthalpy_over(table, amounts, at_kelvin, over) / JOULE_PER_QUANTUM);
		}
		return energy;
	};
	enum class Solids { All, Sinking, Floating };
	// what of a cell is of these phases (never its wall), with its enthalpy at its temperature; of its solids (Stage
	// 71) those that sink into the layer `to`'s liquid (any out of a layer with none), or those that float on it
	const auto move = [&](size_t from, size_t to, PhaseMask phases, double share, Solids which = Solids::All) {
		const CellState &c = state(from);
		const double rho_to = which == Solids::All ? 0.0 : density_of(to);
		const bool held_up = holds(table, c, Phase::Liquid);
		std::vector<SpeciesAmount> amounts;
		for (size_t s = 0; s < c.amount.size(); ++s) {
			const SpeciesDef &d = table.species(SpeciesId{ static_cast<int32_t>(s) });
			if (d.phase == Phase::Solid && which != Solids::All) {
				const bool lighter = solid_density(SpeciesId{ static_cast<int32_t>(s) }) < rho_to;
				if (which == Solids::Sinking ? held_up && lighter : !lighter) {
					continue;
				}
			}
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
		const Energy energy = all ? Energy{ c.energy } : carried_from(from, amounts, kelvin(from));
		const Refusal refused = transfer(edge, col.cells[from], amounts, energy);
		CHEM_ASSERT(refused == Refusal::None, refusal_text(refused));
		// (Stage 88) all of a sealed vessel's layer goes with what it had taken of the vessel, too: its energy is its
		// own in that volume
		const double taken = world_.meta(col.cells[from]).volume;
		if (all && pressed && refused == Refusal::None && taken != 0.0) {
			const CellMeta &into = world_.meta(col.cells[to]);
			Refusal moved = world_.press_layer(col.cells[from], world_.meta(col.cells[from]).over, 0.0);
			if (moved == Refusal::None) {
				moved = to == 0 ? world_.resize_column_top(col.top, std::min(into.volume + taken, into.column_volume)) : world_.press_layer(col.cells[to], into.over, into.volume + taken);
			}
			CHEM_ASSERT(moved == Refusal::None, refusal_text(moved));
			thermal_valid_[static_cast<size_t>(col.cells[from].index)] = 0;
			thermal_valid_[static_cast<size_t>(col.cells[to].index)] = 0;
		}
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
	// solids sink to the lowest liquid; (Stage 71) one lighter than the liquid floats up to the topmost (ice on water)
	for (size_t i = 0; i + 1 < n; ++i) {
		if (holds(table, state(i), Phase::Solid) && liquid(i + 1)) {
			move(i, i + 1, solids, 1.0, Solids::Sinking);
		}
	}
	for (size_t i = n - 1; i >= 1; --i) {
		if (holds(table, state(i), Phase::Solid) && liquid(i) && liquid(i - 1)) {
			move(i, i - 1, solids, 1.0, Solids::Floating);
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
			const double vu = solution_m3(table, state(i), volume_kelvin(i)), vl = solution_m3(table, state(i + 1), volume_kelvin(i + 1));
			if (!liquid_density(table, state(i), kelvin(i), up) || !liquid_density(table, state(i + 1), kelvin(i + 1), down) || !(vu > 0.0) || !(vl > 0.0)) {
				break;
			}
			const double whole = vu * vl / (vu + vl); // the volume exchanged that makes them one
			const Levels at = levels_of(table, world_, col.top, col.cells, [this](CellId c) { return column_kelvin(c); });
			const double per_m3 = GRAVITY * (down > up ? down - up : 0.0) * (at.middle[i] - at.middle[i + 1]); // J a m^3 exchanged
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
				const Refusal refused = transfer(col.up[i], col.cells[i], sinks, carried_from(i, sinks, tu));
				CHEM_ASSERT(refused == Refusal::None, refusal_text(refused));
			}
			if (!rises.empty()) {
				const Refusal refused = transfer(col.up[i], col.cells[i + 1], rises, carried_from(i + 1, rises, tl));
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
			const bool top_known = liquid_density(table, state(0), kelvin(0), top_density);
			const Levels at = levels_of(table, world_, col.top, col.cells, [this](CellId c) { return column_kelvin(c); });
			double flux = q + per_metre * (top_known ? at.thick(0) : 0.0);
			const double mixture = (q * jet_density + (flux - q) * top_density) / flux;
			double reach = -1.0; // how far it plunges, once it meets a denser layer (m)
			// the layers between the top and the one it entrains: their height and their density times it (a cubic
			// metre lifted from layer k into the top raises the column by g [sum over them of (rho_k - rho_j) h_j +
			// (rho_k - rho_top) h_top / 2]: the layers between sink as layer k thins)
			const double top_h = 2.0 * (at.middle[0] - at.bottom[0]); // (twice its middle's height over its bottom)
			double between_h = 0.0, between_mass = 0.0;
			for (size_t k = 1; k < n && liquid(k) && top_known && budget > 0.0; ++k) {
				double d = 0.0;
				const double v = solution_m3(table, state(k), volume_kelvin(k));
				if (!liquid_density(table, state(k), kelvin(k), d) || !(v > 0.0)) {
					break;
				}
				const double h = at.thick(k);
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
					const Energy carried = carried_from(k, lifted, kelvin(k));
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
	// their middles); an ion with its field (Stage 63: Nernst and Planck's at no current, J_i = D_i (g_i - z_i c_i phi),
	// g_i its difference of concentration, c_i their mean, phi what carries no net charge), so a fast ion drags its
	// slow counter-ion and none runs ahead of it. A species moves no further than half the way to equal concentrations
	// (the ions together, by one factor); each direction's ions made whole by a carrier of the opposite charge that
	// moves both ways (what moves is neutral matter, `neutral_part`)
	const double area = world_.meta(col.top).column_area;
	const double dt = step_seconds_;
	const size_t species_n = static_cast<size_t>(table.species_count());
	std::vector<double> flux(species_n);
	std::vector<char> scaled(species_n);
	std::vector<double> d_of(species_n); // (Stage 75) each species' diffusion at the interface's temperature, once
	for (size_t i = 0; i + 1 < n && area > 0.0; ++i) {
		if (!liquid(i) || !liquid(i + 1)) {
			continue;
		}
		const double vu = solution_m3(table, state(i), volume_kelvin(i)), vl = solution_m3(table, state(i + 1), volume_kelvin(i + 1));
		if (!(vu > 0.0) || !(vl > 0.0)) {
			continue;
		}
		const Levels at = levels_of(table, world_, col.top, col.cells, [this](CellId c) { return column_kelvin(c); });
		const double dz = at.middle[i] - at.middle[i + 1], across = at.across[i], t = 0.5 * (kelvin(i) + kelvin(i + 1));
		if (!(dz > 0.0)) {
			continue;
		}
		const CellState &upper = state(i), &lower = state(i + 1);
		const auto conc = [&](const CellState &c, size_t s, double v) { return static_cast<double>(c.amount[s]) * MOL_PER_QUANTUM / v; };
		double field_num = 0.0, field_den = 0.0;
		// (Stage 75) a species neither layer holds adds nothing (its terms are signed zeros, its move none): it is passed
		// over, and what diffuses has its diffusion worked out once
		for (size_t s = 0; s < species_n; ++s) {
			const SpeciesId id{ static_cast<int32_t>(s) };
			const SpeciesDef &def = table.species(id);
			flux[s] = 0.0;
			scaled[s] = 0;
			d_of[s] = 0.0;
			if (def.phase != Phase::Aqueous || (upper.amount[s] == 0 && lower.amount[s] == 0)) {
				continue;
			}
			d_of[s] = diffusion(table, id, t);
			if (def.charge == 0) {
				continue;
			}
			const double d = d_of[s];
			if (!(d > 0.0)) {
				continue;
			}
			const double cu = conc(upper, s, vu), cl = conc(lower, s, vl), z = def.charge;
			field_num += z * d * (cu - cl);
			field_den += z * z * d * 0.5 * (cu + cl);
		}
		const double phi = field_den > 0.0 ? field_num / field_den : 0.0;
		double ions_scale = 1.0;
		for (size_t s = 0; s < species_n; ++s) {
			const SpeciesId id{ static_cast<int32_t>(s) };
			const SpeciesDef &def = table.species(id);
			if (def.phase != Phase::Aqueous || (upper.amount[s] == 0 && lower.amount[s] == 0)) {
				continue;
			}
			const double d = d_of[s];
			if (!(d > 0.0)) {
				continue;
			}
			const double cu = conc(upper, s, vu), cl = conc(lower, s, vl), z = def.charge;
			const double drive = (cu - cl) - z * 0.5 * (cu + cl) * phi;
			const double mol = d * across * drive / dz * dt; // downward
			const double most = std::fabs(cu - cl) / (1.0 / vu + 1.0 / vl) * 0.5; // half the way to equal
			// an ion that carries a share of the current under TRACE_SHARE (a trace: H+ and OH- in a salt) is held to its
			// own bound, as a neutral species is; the rest by the one factor, so no current runs
			const bool trace = !(z * z * d * 0.5 * (cu + cl) >= TRACE_SHARE * field_den);
			if (def.charge == 0 || trace) {
				const double bound = mol * (cu - cl) >= 0.0 ? most : static_cast<double>((mol > 0.0 ? upper : lower).amount[s]) * MOL_PER_QUANTUM * 0.5;
				flux[s] = mol > 0.0 ? std::min(mol, bound) : -std::min(-mol, bound);
			} else {
				flux[s] = mol;
				scaled[s] = 1;
				// down its own gradient: no further than half the way; against it (pulled by the field): what it holds
				const double cap = mol * (cu - cl) > 0.0 ? most : static_cast<double>((mol > 0.0 ? upper : lower).amount[s]) * MOL_PER_QUANTUM * 0.5;
				if (std::fabs(mol) > cap) {
					ions_scale = std::min(ions_scale, cap / std::fabs(mol));
				}
			}
		}
		std::vector<SpeciesAmount> down_moves, up_moves;
		for (size_t s = 0; s < species_n; ++s) {
			const double mol = scaled[s] ? flux[s] * ions_scale : flux[s];
			const auto q = static_cast<int64_t>(std::fabs(mol) / MOL_PER_QUANTUM);
			if (q > 0) {
				(mol > 0.0 ? down_moves : up_moves).push_back(SpeciesAmount{ SpeciesId{ static_cast<int32_t>(s) }, Amount{ q } });
			}
		}
		// each direction made neutral by a carrier that moves both ways: the charged species of the sign each lacks that
		// both layers hold the most of
		const auto carry = [&](std::vector<SpeciesAmount> &moves, const CellState &from) {
			int64_t q = 0;
			for (const SpeciesAmount &m : moves) {
				q += table.species(m.species).charge * m.amount.quanta;
			}
			if (q == 0) {
				return;
			}
			size_t best = species_n;
			int64_t most = 0;
			for (size_t s = 0; s < species_n; ++s) {
				const SpeciesDef &def = table.species(SpeciesId{ static_cast<int32_t>(s) });
				if (def.phase != Phase::Aqueous || def.charge * q >= 0 || (q % def.charge) != 0) {
					continue;
				}
				int64_t held = std::min(upper.amount[s], lower.amount[s]);
				for (const SpeciesAmount &m : moves) {
					if (static_cast<size_t>(m.species.index) == s) {
						held = std::min(held, from.amount[s] - m.amount.quanta);
					}
				}
				if (held > most) {
					most = held;
					best = s;
				}
			}
			if (best == species_n) {
				return; // what is left is trimmed as it moves
			}
			const int64_t k = std::min(q / -static_cast<int64_t>(table.species(SpeciesId{ static_cast<int32_t>(best) }).charge), most);
			for (SpeciesAmount &m : moves) {
				if (static_cast<size_t>(m.species.index) == best) {
					m.amount.quanta += k;
					return;
				}
			}
			moves.push_back(SpeciesAmount{ SpeciesId{ static_cast<int32_t>(best) }, Amount{ k } });
		};
		carry(down_moves, upper);
		carry(up_moves, lower);
		if (!down_moves.empty()) {
			const Refusal refused = transfer(col.up[i], col.cells[i], down_moves, carried_from(i, down_moves, kelvin(i)));
			CHEM_ASSERT(refused == Refusal::None, refusal_text(refused));
		}
		if (!up_moves.empty()) {
			const Refusal refused = transfer(col.up[i], col.cells[i + 1], up_moves, carried_from(i + 1, up_moves, kelvin(i + 1)));
			CHEM_ASSERT(refused == Refusal::None, refusal_text(refused));
		}
	}
	// a steep step and a spare layer: the thicker neighbour splits in two halves (one split a step); the steepest
	// step whose thicker neighbour can still be halved (Stage 63: a step resolved to the thinnest layers beside it
	// leaves the next steepest its turn)
	if (empty(state(n - 1)) && area > 0.0) {
		size_t at = n, thick = n;
		double steepest = SPLIT_ABOVE;
		const Levels levels = levels_of(table, world_, col.top, col.cells, [this](CellId c) { return column_kelvin(c); });
		for (size_t i = 0; i + 1 < n; ++i) {
			if (liquid(i) && liquid(i + 1)) {
				const double c = contrast(table, state(i), kelvin(i), state(i + 1), kelvin(i + 1));
				const size_t thicker = solution_m3(table, state(i), volume_kelvin(i)) >= solution_m3(table, state(i + 1), volume_kelvin(i + 1)) ? i : i + 1;
				if (c > steepest && levels.thick(thicker) > 2.0 * THINNEST) {
					steepest = c;
					at = i;
					thick = thicker;
				}
			}
		}
		if (at < n) {
			// every layer under it down a place, then half of it into the place it leaves
			for (size_t k = n - 1; k > thick + 1; --k) {
				if (!empty(state(k - 1))) {
					move(k - 1, k, PHASES_ALL, 1.0);
				}
			}
			move(thick, thick + 1, PHASES_ALL & ~gas, 0.5);
		}
	}
	// (Stage 88) a sealed vessel is sized: its layers at its pressure, its top what they leave there
	if (pressed) {
		size_vessel(col);
		return;
	}
	// the top is the size its layers leave it
	const CellMeta &top = world_.meta(col.top);
	double below = 0.0;
	for (size_t i = 1; i < n; ++i) {
		const Fill f = fill_of(table, state(i), volume_kelvin(i));
		below += f.defined ? f.condensed : 0.0;
	}
	const double size = top.column_volume - below;
	if (size != top.volume && size > 0.0) {
		const Refusal refused = world_.resize_column_top(col.top, size);
		CHEM_ASSERT(refused == Refusal::None, refusal_text(refused));
	}
}

// (Stage 88) Work between two cells of a sealed vessel (World::work), in the step's flows as a transfer's energy is.
Refusal Simulation::work(EdgeId edge, CellId from, Energy energy) {
	const Edge &e = world_.edges()[static_cast<size_t>(edge.index)];
	thermal_valid_[static_cast<size_t>(e.a.index)] = 0;
	thermal_valid_[static_cast<size_t>(e.b.index)] = 0;
	const Refusal refused = world_.work(edge, from, energy);
	if (refused == Refusal::None) {
		const size_t at = static_cast<size_t>(edge.index) * PROCESSES + static_cast<size_t>(process_);
		if (flows_.energy.size() <= at) {
			flows_.energy.resize(world_.edges().size() * PROCESSES, 0);
			flows_.grams.resize(world_.edges().size() * PROCESSES, 0.0);
		}
		flows_.energy[at] += (from == e.a ? 1 : -1) * energy.quanta;
	}
	return refused;
}

void Simulation::size_vessels() {
	find_columns();
	for (const Column &col : columns_) {
		size_vessel(col);
	}
}

// (Stage 88, GAME_SPEC §6, Liquids that give) A sealed vessel sized. Its layers stand at its pressure, each taking the
// volume it needs there; its top holds what they leave. Since it was last sized each layer has grown or shrunk at the
// pressure it stood at, p: that work, -p (V_now - V_taken), is booked between it and the top first. Then the one
// pressure P at which the whole is full: each cell, holding its energy in its volume now, takes at P the volume V'
// with the work -(P - P_atm) (V' - V_now) in it (thermo's at_pressure: its enthalpy at P what stands), and P is the
// root of sum V'(P) = the whole (falling with P: bracketed, then regula falsi). With no gas in it and more room than
// its liquids take at any pressure it is at next to none (LEAST: a void, in its top). The work between its cells is
// work along its column's edges (World::work), what one gives the other takes: nothing is made.
bool Simulation::size_vessel(const Column &col) {
	const SpeciesTable &table = world_.species();
	const size_t n = col.cells.size();
	const CellMeta &top = world_.meta(col.top);
	if (!top.sealed || top.frozen || n < 2 || !(top.column_volume > 0.0)) {
		return false;
	}
	const auto index = static_cast<size_t>(&col - columns_.data());
	if (sized_.size() != columns_.size()) {
		sized_.assign(columns_.size(), {});
	}
	std::vector<uint64_t> &kept = sized_[index];
	bool same = kept.size() == n;
	for (size_t i = 0; same && i < n; ++i) {
		same = kept[i] == world_.revision(col.cells[i]);
	}
	if (same) {
		return false; // as it was when it was last sized
	}
	constexpr double LEAST = 1.0; // Pa
	const double whole = top.column_volume;
	struct Part {
		bool holds = false;
		double energy = 0.0, volume = 0.0, pending = 0.0; // J (E = U + P_atm V, in its volume now), m^3, J
		double sized = 0.0; // m^3, at the pressure found
	};
	std::vector<Part> parts(n);
	double below = 0.0, pending = 0.0;
	for (size_t i = 1; i < n; ++i) {
		const CellId c = col.cells[i];
		const CellMeta &m = world_.meta(c);
		const CellState &x = world_.snapshot_cell(c);
		if (!m.pressed) {
			return false;
		}
		Part &part = parts[i];
		part.holds = !empty(x);
		if (!part.holds) {
			continue; // (an empty layer takes none: it gave what it had taken with what it held)
		}
		const Thermal &t = thermal(c);
		const Fill f = t.defined ? fill_of(table, x, t.temperature, &m) : Fill{};
		if (!f.defined) {
			return false;
		}
		part.volume = f.condensed + f.gas;
		part.pending = -m.over * (part.volume - m.volume);
		part.energy = static_cast<double>(x.energy) * JOULE_PER_QUANTUM + part.pending;
		below += part.volume;
		pending += part.pending;
	}
	{
		const CellState &x = world_.snapshot_cell(col.top);
		Part &part = parts[0];
		part.holds = !empty(x);
		part.volume = whole - below; // what its layers leave it now
		part.pending = -pending; // (the layers' work on it, as they grew)
		part.energy = static_cast<double>(x.energy) * JOULE_PER_QUANTUM + part.pending;
	}
	// what its cells take together at a pressure (none: -1)
	const auto takes = [&](double pressure) {
		double sum = 0.0;
		for (size_t i = 0; i < n; ++i) {
			Part &part = parts[i];
			part.sized = 0.0;
			if (!part.holds) {
				continue;
			}
			const AtPressure at = at_pressure(table, world_.snapshot_cell(col.cells[i]), part.energy, part.volume, pressure);
			if (!at.defined) {
				return -1.0;
			}
			part.sized = at.volume;
			sum += at.volume;
		}
		return sum;
	};
	const Thermal &now = thermal(col.top);
	double lo = now.defined && now.pressure > LEAST ? now.pressure : P_ATMOSPHERE, hi = lo;
	double at_lo = takes(lo), at_hi = at_lo;
	if (at_lo < 0.0) {
		return false;
	}
	if (at_lo > whole) { // it needs more room than it has here: a higher pressure
		while (at_hi > whole && hi < 1e13) {
			lo = hi;
			at_lo = at_hi;
			hi *= 2.0;
			at_hi = takes(hi);
			if (at_hi < 0.0) {
				return false;
			}
		}
	} else {
		while (at_lo < whole && lo > LEAST) {
			hi = lo;
			at_hi = at_lo;
			lo = std::max(0.5 * lo, LEAST);
			at_lo = takes(lo);
			if (at_lo < 0.0) {
				return false;
			}
		}
	}
	double pressure = at_lo <= whole ? lo : hi; // (a void at the least pressure; or where it fits)
	if (at_lo > whole && at_hi < whole) {
		// regula falsi between them (Illinois'), the volume falling with the pressure
		double f_lo = at_lo - whole, f_hi = at_hi - whole;
		int side = 0;
		for (int i = 0; i < 100 && hi - lo > 1e-13 * hi; ++i) {
			double middle = (lo * f_hi - hi * f_lo) / (f_hi - f_lo);
			if (!(middle > lo && middle < hi)) {
				middle = 0.5 * (lo + hi);
			}
			const double at = takes(middle);
			if (at < 0.0) {
				return false;
			}
			const double f = at - whole;
			if (f == 0.0) {
				lo = hi = middle;
				break;
			}
			if (f > 0.0) {
				lo = middle;
				f_lo = f;
				f_hi *= side == 1 ? 0.5 : 1.0;
				side = 1;
			} else {
				hi = middle;
				f_hi = f;
				f_lo *= side == -1 ? 0.5 : 1.0;
				side = -1;
			}
		}
		pressure = hi; // (where it fits, from the side that does)
	}
	if (takes(pressure) < 0.0) {
		return false;
	}
	// each layer's energy moved by the work it gave as it grew and takes coming to the pressure; the top's the rest
	const double over = pressure - P_ATMOSPHERE;
	std::vector<int64_t> moves(n, 0);
	double layers = 0.0;
	for (size_t i = 1; i < n; ++i) {
		const Part &part = parts[i];
		if (part.holds) {
			moves[i] = std::llround((part.pending - over * (part.sized - part.volume)) / JOULE_PER_QUANTUM);
			layers += part.sized;
		}
	}
	bool moved = false;
	int64_t running = 0; // what the cells from i down take together: from the cell over them
	for (size_t i = n - 1; i >= 1; --i) {
		running += moves[i];
		if (running != 0) {
			const Refusal refused = work(col.up[i - 1], col.cells[i - 1], Energy{ running });
			CHEM_ASSERT(refused == Refusal::None, refusal_text(refused));
			if (refused != Refusal::None) {
				return moved;
			}
			moved = true;
		}
	}
	for (size_t i = 1; i < n; ++i) {
		const CellMeta &m = world_.meta(col.cells[i]);
		const double taken = parts[i].holds ? parts[i].sized : 0.0;
		if (m.over != over || m.volume != taken) {
			const Refusal refused = world_.press_layer(col.cells[i], over, taken);
			CHEM_ASSERT(refused == Refusal::None, refusal_text(refused));
			thermal_valid_[static_cast<size_t>(col.cells[i].index)] = 0;
			moved = true;
		}
	}
	const double left = whole - layers;
	if (left > 0.0 && left != top.volume) {
		const Refusal refused = world_.resize_column_top(col.top, left);
		CHEM_ASSERT(refused == Refusal::None, refusal_text(refused));
		thermal_valid_[static_cast<size_t>(col.top.index)] = 0;
		moved = true;
	}
	kept.resize(n);
	for (size_t i = 0; i < n; ++i) {
		kept[i] = world_.revision(col.cells[i]);
	}
	return moved;
}

} // namespace chem
