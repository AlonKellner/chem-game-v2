// (Stage 89, GAME_SPEC §6, Flow between vessels) The closure: every sealed vessel that has a plunger or a conduit,
// with those it is joined to, brought to the pressures at which each is exactly full as the step ends.
//
// A node is a sealed vessel (a sealed rigid cell and the layers under it, at one pressure: Stage 88); an end is a
// cell that is not sealed, at the room's pressure. The unknown is each node's pressure as the step ends. Given the
// pressures, the nodes are taken from the highest down (what crosses a conduit runs down the pressures, so the
// highest only gives): each conduit's crossing by its law at the two end pressures, what stands at the giver's port
// carrying its enthalpy at the giver's end state; each plunger's move by its forces at its node's end pressure
// (`piston_move`), its hand's work with it; and the volume each node's contents then need at its pressure. A node's
// residual is the room its boundary gives less the room its contents need: rising with its own pressure, falling
// with a neighbour's. One node: a bracket and regula falsi. Several: Newton, each step kept only if it lowers the
// largest residual, and sweeps of one-dimensional solves if that fails.
//
// Each piece is a process the second law allows whatever the step: a vessel's content brought to its end pressure
// against that pressure, constant (an expansion or a compression against a constant outer pressure), and the share
// that leaves pushed out at it; what arrives throttled down to the taker's.
#include "chem/conduit.hpp"
#include "chem/sim.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace chem {
namespace closure {

constexpr double LEAST = 1.0; // Pa: a void's (as a vessel's sizing has it)
constexpr double MOST = 1.0e13;
constexpr int ROUNDS = 4;

struct Part { // a cell of a node: its top, or a layer under it
	CellId id;
	Content content;
	double energy = 0.0, volume = 0.0, kelvin = T0; // J (E = U + P_atm V), m^3, K: as the round begins
	// what stands at a port in it: its liquid with what is dissolved in it, and its gas
	Content liquid, gas;
	std::vector<SpeciesAmount> liquid_amounts, gas_amounts;
	double liquid_kg = 0.0, gas_kg = 0.0, liquid_density = 0.0, liquid_viscosity = 0.0, gas_viscosity = 0.0, gas_molar = 0.0, gas_gamma = 0.0;
	// (Stage 94) where its solids and liquids lie in its vessel, m over the floor, as the round begins
	double bottom = 0.0, top = 0.0;
	// an evaluation's
	double t = T0, sized = 0.0, liquid_left = 0.0, gas_left = 0.0;
	bool is_sized = false;
};

struct Node {
	CellId top;
	bool fixed = false; // an end, at the room's pressure
	bool piston = false;
	Piston p;
	std::vector<Part> parts; // [0] its top, then its layers down
	std::vector<EdgeId> up; // up[i]: from parts[i + 1] to parts[i]
	double whole = 0.0, start = 0.0; // m^3: now, and as the step began
	double pressure = P_ATMOSPHERE, tolerance = 0.0;
	// (Stage 94) its heights, m: its floor over the bench's datum; with a shape (a column's), its liquid's surface and
	// its brim over its floor and its cross-section at the surface, m^2. With none it has no heights and no head.
	bool shaped = false, weighed = false; // (weighed: gravity's edge to it)
	double height = 0.0, surface = 0.0, brim = 0.0, area = 0.0;
	// an evaluation's
	Content in;
	double in_enthalpy = 0.0, grown = 0.0, work = 0.0, residual = 0.0;
	bool done = false;
	PistonMove mv;
	bool at_least = false; // at the least pressure with room to spare: a void
	bool tight = false; // closed as far as its pressure can be told (inside a nested solve), not to its tolerance
};

struct Link {
	size_t edge = 0;
	int a = -1, b = -1; // its ends' nodes
	Conduit conduit;
	double port_a = PORT_TOP, port_b = PORT_TOP; // each end's port's height over its vessel's floor (Stage 94)
	bool liquid = false, gas = false, dissolved = false;
	bool heads = false; // both ends weighed: heights and heads count (else as Stage 89: a foot or a top, no weight)
};

struct Crossing {
	size_t link = 0;
	int from = -1, to = -1, part = 0;
	bool liquid = false;
	double share = 0.0, kelvin = T0, from_pa = 0.0, to_pa = 0.0, rate = 0.0, seconds = 0.0;
	// (Stage 94) the giver's own pressure (its enthalpy's), what the rate takes off its own drive, the fall from the
	// giver's surface to the taker's, m, and the mass, kg; carried: its enthalpy is in the taker already
	double at_pa = 0.0, give = 0.0, fall = 0.0, mass = 0.0, drive = 0.0;
	bool carried = false;
};

static bool none(const Content &c) {
	return !(c.capacity > 0.0);
}

struct Closing {
	std::vector<Node> nodes;
	std::vector<Link> links;
	std::vector<Crossing> crossings;
	std::vector<int> order;
};

// What crosses the link from `giver` at the pressures set, for the evaluation: each share out of its part, its
// enthalpy and its amounts into the taker's top. `record`: kept for the apply.
static void cross(Closing &g, size_t l, double seconds);

} // namespace closure

using namespace closure;

bool Simulation::close_vessels() {
	const auto cells = static_cast<size_t>(world_.cell_count());
	const std::span<const Edge> edges = world_.edges();
	// the vessels: sealed tops with a plunger or a conduit, and the conduits' other ends
	const auto top_of = [&](size_t c) {
		const CellMeta &m = world_.meta(CellId{ static_cast<int32_t>(c) });
		return m.rigid && m.sealed && !m.layer && !m.reservoir && !m.frozen;
	};
	const auto end_of = [&](size_t c) {
		const CellMeta &m = world_.meta(CellId{ static_cast<int32_t>(c) });
		return !m.layer && !m.reservoir && !m.frozen;
	};
	std::vector<int> group(cells, -1); // each vessel's group: the least cell of those joined to it
	std::vector<size_t> conduits, between_open;
	const auto find = [&](size_t c) {
		while (group[c] != static_cast<int>(c)) {
			c = static_cast<size_t>(group[c]);
		}
		return c;
	};
	bool any = false;
	for (size_t c = 0; c < cells; ++c) {
		if (top_of(c)) {
			group[c] = static_cast<int>(c);
			any = any || world_.meta(CellId{ static_cast<int32_t>(c) }).piston.area > 0.0;
		}
	}
	for (size_t i = 0; i < edges.size(); ++i) {
		const Edge &e = edges[i];
		if (!e.open || e.frozen || e.column || e.drain || e.conduit.count <= 0 || (e.phases & (PHASES_GAS | phase_bit(Phase::Liquid))) == 0) {
			continue;
		}
		const auto a = static_cast<size_t>(e.a.index), b = static_cast<size_t>(e.b.index);
		if (!end_of(a) || !end_of(b)) {
			continue;
		}
		if (!top_of(a) && !top_of(b)) {
			// (Stage 94) between two vessels open to the room: nothing to solve, each at the room's pressure, but their
			// liquids' heads run what stands at its ports (communicating vessels, a siphon)
			between_open.push_back(i);
			any = true;
			continue;
		}
		conduits.push_back(i);
		any = true;
		if (top_of(a) && top_of(b)) {
			const size_t ra = find(a), rb = find(b);
			group[std::max(ra, rb)] = static_cast<int>(std::min(ra, rb));
		}
	}
	if (!any) {
		return false;
	}
	find_columns();
	// The step's crossings and moves, found together, once. Then every cell it touched to its fast equilibria; and
	// where any moved (a vapour condensing as it is squeezed), each vessel with a plunger again, alone: its plunger's
	// whole move at the pressure it now ends at, less what it has moved. What the equilibria leave across a conduit is
	// the next step's to cross.
	bool moved = false;
	std::vector<CellId> touched;
	for (size_t root = 0; root < cells; ++root) {
		if (group[root] != static_cast<int>(root)) {
			continue;
		}
		std::vector<CellId> tops;
		bool acts = false; // a plunger or a conduit in it
		for (size_t c = root; c < cells; ++c) {
			if (group[c] >= 0 && find(c) == root) {
				tops.push_back(CellId{ static_cast<int32_t>(c) });
				acts = acts || world_.meta(tops.back()).piston.area > 0.0;
			}
		}
		std::vector<size_t> own;
		for (const size_t i : conduits) {
			const auto a = static_cast<size_t>(edges[i].a.index), b = static_cast<size_t>(edges[i].b.index);
			if ((top_of(a) && find(a) == root) || (top_of(b) && find(b) == root)) {
				own.push_back(i);
			}
		}
		if (acts || !own.empty()) {
			moved = close_group(tops, own, touched) || moved;
		}
	}
	for (const size_t i : between_open) {
		std::vector<CellId> unused; // (open cells are the open tree's to settle, after)
		moved = close_group({}, { i }, unused) || moved;
	}
	for (int round = 1; round < ROUNDS && !touched.empty(); ++round) {
		bool settled = true;
		std::vector<CellId> again;
		for (const CellId c : touched) {
			if (thermal(c).defined && project(c)) {
				settled = false;
				again.push_back(c);
			}
		}
		if (settled) {
			break;
		}
		moved = true;
		touched.clear();
		for (size_t c = 0; c < cells; ++c) {
			const CellId top{ static_cast<int32_t>(c) };
			if (top_of(c) && world_.meta(top).piston.area > 0.0) {
				// (only where a cell of its vessel moved)
				bool its = false;
				for (const Column &col : columns_) {
					if (col.top == top) {
						for (const CellId cell : col.cells) {
							its = its || std::find(again.begin(), again.end(), cell) != again.end();
						}
					}
				}
				its = its || std::find(again.begin(), again.end(), top) != again.end();
				if (its) {
					close_group({ top }, {}, touched);
				}
			}
		}
	}
	return moved;
}

namespace closure {

// the part's state at the node's pressure, holding what it holds with what came in
static bool size_part(const SpeciesTable &table, Node &node, size_t i) {
	Part &part = node.parts[i];
	const double over = node.pressure - P_ATMOSPHERE;
	Content content = part.content;
	double enthalpy = part.energy + over * part.volume;
	if (i == 0) {
		content.add(node.in);
		enthalpy = part.energy + node.in_enthalpy + node.work + over * (part.volume + node.grown);
	}
	part.sized = 0.0;
	part.t = part.kelvin;
	if (none(content)) {
		return true; // nothing in it: no room taken at any pressure
	}
	const AtPressure at = at_pressure(table, content, enthalpy, node.pressure, part.kelvin);
	if (!at.defined) {
		return false;
	}
	part.t = at.temperature;
	part.sized = at.volume;
	return true;
}

constexpr double GRAVITY = 9.80665; // m/s^2, standard (as the column's)

// (Stage 94) A port's height over its vessel's floor as it counts: the top is the brim; a vessel with no shape has
// no heights.
static double port_height(const Node &node, double port) {
	return node.shaped ? std::min(port, node.brim) : 0.0;
}

// The weight of the liquid over a port, Pa: g times each layer's density over what of it lies above the port.
static double head_at(const Node &node, double port) {
	if (!node.shaped || !(port < node.surface)) {
		return 0.0;
	}
	double head = 0.0;
	for (const Part &part : node.parts) {
		if (part.top > port && part.liquid_kg > 0.0) {
			head += GRAVITY * part.liquid_density * (part.top - std::max(part.bottom, port));
		}
	}
	return head;
}

// What stands at a node's port, in the order it gives it (Stage 89; by heights, Stage 94): `most`, the share of each
// part's that may go.
struct Stands {
	int part = 0;
	bool liquid = false;
	double most = 1.0;
};

static size_t stands_at(const Node &node, double port, const Link &link, bool heads, std::array<Stands, 64> &stands) {
	size_t count = 0;
	const int n = static_cast<int>(node.parts.size());
	const bool has_gas = node.parts[0].gas_kg > 0.0;
	const auto liquids = [&](bool upward, int from) {
		for (int k = 0; k < n && count < stands.size(); ++k) {
			const int i = upward ? n - 1 - k : k;
			if ((upward ? i <= from : i >= from) && node.parts[static_cast<size_t>(i)].liquid_kg > 0.0) {
				stands[count++] = Stands{ i, true, 1.0 };
			}
		}
	};
	if (heads && node.shaped) {
		if (port < node.surface) {
			// under the surface: the layer the port lies in (what of it is over the port), then those over it as they
			// come down to it, then the gas; what lies under the port stays
			if (link.liquid) {
				int at = -1;
				for (int i = n - 1; i >= 0; --i) {
					if (node.parts[static_cast<size_t>(i)].liquid_kg > 0.0 && node.parts[static_cast<size_t>(i)].top > port) {
						at = i;
						break;
					}
				}
				if (at >= 0) {
					const Part &part = node.parts[static_cast<size_t>(at)];
					const double thick = part.top - part.bottom;
					stands[count++] = Stands{ at, true, thick > 0.0 ? std::clamp((part.top - std::max(part.bottom, port)) / thick, 0.0, 1.0) : 1.0 };
					liquids(true, at - 1);
				}
			}
			if (link.gas && has_gas && count < stands.size()) {
				stands[count++] = Stands{ 0, false, 1.0 };
			}
		} else if (link.gas && has_gas) {
			stands[count++] = Stands{ 0, false, 1.0 };
		} else if (link.liquid && !has_gas) {
			liquids(false, 0);
		}
		return count;
	}
	// no heights (Stage 89): at a foot its liquid from the lowest layer up, then its gas; at a top its gas, or with
	// none its liquid from the top down
	if (port < PORT_TOP) {
		if (link.liquid) {
			liquids(true, n - 1);
		}
		if (link.gas && has_gas && count < stands.size()) {
			stands[count++] = Stands{ 0, false, 1.0 };
		}
	} else if (link.gas && has_gas) {
		stands[count++] = Stands{ 0, false, 1.0 };
	} else if (link.liquid && !has_gas) {
		liquids(false, 0);
	}
	return count;
}

// A link's crossings at the pressures set (the evaluation's first part: they need the pressures and the heads
// alone). From the end whose port stands the higher in pressure, its weight over it counted, less the weight of
// what crosses over the rise between the two ports; each parcel's rate the conduit's law's with what the crossing
// itself takes off the head (it lowers the surface it leaves and raises the one it joins).
static void cross(Closing &g, size_t l, double seconds) {
	const Link &link = g.links[l];
	Node &a = g.nodes[static_cast<size_t>(link.a)], &b = g.nodes[static_cast<size_t>(link.b)];
	const bool heads = link.heads;
	std::array<Stands, 64> stands_a{}, stands_b{};
	const size_t count_a = stands_at(a, link.port_a, link, heads, stands_a), count_b = stands_at(b, link.port_b, link, heads, stands_b);
	const double head_a = heads ? head_at(a, link.port_a) : 0.0, head_b = heads ? head_at(b, link.port_b) : 0.0;
	const double rise = heads ? (b.height + port_height(b, link.port_b)) - (a.height + port_height(a, link.port_a)) : 0.0; // b's port over a's
	// what drives a parcel of a density from one end to the other, Pa (a gas's weight left out)
	const auto drive = [&](bool from_a, const Stands &what) {
		const Node &from = from_a ? a : b, &to = from_a ? b : a;
		const double density = what.liquid ? from.parts[static_cast<size_t>(what.part)].liquid_density : 0.0;
		return (from.pressure + (what.liquid ? (from_a ? head_a : head_b) : 0.0)) - (to.pressure + (from_a ? head_b : head_a)) - density * GRAVITY * (from_a ? rise : -rise);
	};
	const double drive_a = count_a > 0 ? drive(true, stands_a[0]) : 0.0, drive_b = count_b > 0 ? drive(false, stands_b[0]) : 0.0;
	if (!(drive_a > 0.0) && !(drive_b > 0.0)) {
		return;
	}
	const bool from_a = drive_a >= drive_b;
	const int giver = from_a ? link.a : link.b, taker = from_a ? link.b : link.a;
	Node &from = from_a ? a : b, &to = from_a ? b : a;
	const std::array<Stands, 64> &stands = from_a ? stands_a : stands_b;
	const size_t count = from_a ? count_a : count_b;
	const bool back = !from_a;
	const double to_port = from_a ? link.port_b : link.port_a, to_head = from_a ? head_b : head_a;
	const bool to_under = heads && to.shaped && to_port < to.surface; // the taker's port under its surface
	double left = seconds;
	for (size_t k = 0; k < count && left > 0.0; ++k) {
		Part &part = from.parts[static_cast<size_t>(stands[k].part)];
		const bool liquid = stands[k].liquid;
		const double push = drive(from_a, stands[k]);
		if (!(push > 0.0)) {
			break;
		}
		double give = 0.0;
		if (liquid && heads) {
			give = part.liquid_density * GRAVITY * seconds * ((from.shaped && from.area > 0.0 ? 1.0 / from.area : 0.0) + (to_under && to.area > 0.0 ? 1.0 / to.area : 0.0));
		}
		const double to_pa = to.pressure + to_head, from_pa = to_pa + push;
		const double rate = liquid ? conduit_rate(link.conduit, part.liquid_density * push, part.liquid_viscosity, back, give)
								   : gas_rate(link.conduit, from_pa, to_pa, part.kelvin, part.gas_molar, part.gas_gamma, part.gas_viscosity, back);
		if (!(rate > 0.0)) {
			break;
		}
		double &there = liquid ? part.liquid_left : part.gas_left;
		const double whole = liquid ? part.liquid_kg : part.gas_kg;
		const double most = std::max(there - (1.0 - stands[k].most) * whole, 0.0); // (what of it lies over the port)
		const double mass = std::min(most, rate * left);
		if (!(mass > 0.0)) {
			continue;
		}
		there -= mass;
		left -= mass / rate;
		Crossing x;
		x.link = l;
		x.from = giver;
		x.to = taker;
		x.part = stands[k].part;
		x.liquid = liquid;
		x.share = mass / whole;
		x.from_pa = from_pa;
		x.to_pa = to_pa;
		x.at_pa = from.pressure;
		x.rate = rate;
		x.seconds = mass / rate;
		x.give = give;
		x.mass = mass;
		x.drive = push;
		if (liquid && heads && to.weighed) {
			// from the giver's surface (with no shape, its floor) to the taker's
			x.fall = (from.height + (from.shaped ? from.surface : 0.0)) - (to.height + (to.shaped ? to.surface : 0.0));
		}
		g.crossings.push_back(x);
	}
}

// A crossing's parcel carried: out of its giver's part at the temperature that part stands at, into its taker's top
// with its enthalpy at the giver's state and what gravity gives it falling.
static void carry(const SpeciesTable &table, Closing &g, Crossing &x) {
	Node &from = g.nodes[static_cast<size_t>(x.from)], &to = g.nodes[static_cast<size_t>(x.to)];
	Part &part = from.parts[static_cast<size_t>(x.part)];
	if (!x.carried) {
		x.kelvin = part.is_sized ? part.t : part.kelvin; // (a giver not yet sized, in a ring of crossings: as it began)
		Content leaves;
		leaves.add(x.liquid ? part.liquid : part.gas, x.share);
		const Held held = held_at(table, leaves, x.kelvin, x.at_pa);
		to.in.add(leaves);
		to.in_enthalpy += held.enthalpy + x.mass * GRAVITY * x.fall;
		x.carried = true;
	}
}

// its volume out of its giver's part, once that part is sized
static void leave(const SpeciesTable &table, Closing &g, const Crossing &x) {
	Part &part = g.nodes[static_cast<size_t>(x.from)].parts[static_cast<size_t>(x.part)];
	Content leaves;
	leaves.add(x.liquid ? part.liquid : part.gas, x.share);
	part.sized -= held_at(table, leaves, x.kelvin, x.at_pa).volume;
}

// how far from closed: the largest residual over its tolerance (a void's, at the least pressure, is closed)
static double merit(const Closing &g) {
	double worst = 0.0;
	for (const Node &node : g.nodes) {
		if (!node.fixed && !node.at_least) {
			worst = std::max(worst, std::fabs(node.residual) / node.tolerance);
		}
	}
	return worst;
}

} // namespace closure

// Every node's residual at the pressures set. false where a state has no model.
bool Simulation::evaluate(Closing &g, bool) const {
	const SpeciesTable &table = world_.species();
	const size_t n = g.nodes.size();
	for (Node &node : g.nodes) {
		node.in = Content{};
		node.in_enthalpy = 0.0;
		node.done = false;
		for (Part &part : node.parts) {
			part.liquid_left = part.liquid_kg;
			part.gas_left = part.gas_kg;
			part.is_sized = false;
			part.t = part.kelvin;
			part.sized = 0.0;
		}
	}
	// what crosses each conduit: by the pressures and the heads alone
	g.crossings.clear();
	for (size_t l = 0; l < g.links.size(); ++l) {
		cross(g, l, step_seconds_);
	}
	// then each node sized, a giver before its taker (what it gives is at the temperature it ends at): the nodes no
	// unsized node gives to, in turn; in a ring of crossings, the first left as it stands (what a giver not yet sized
	// gives, at the temperature it began at)
	for (size_t placed = 0; placed < n; ++placed) {
		int next = -1;
		for (size_t i = 0; i < n && next < 0; ++i) {
			if (g.nodes[i].done) {
				continue;
			}
			bool waits = false;
			for (const Crossing &x : g.crossings) {
				waits = waits || (x.to == static_cast<int>(i) && !g.nodes[static_cast<size_t>(x.from)].done);
			}
			if (!waits) {
				next = static_cast<int>(i);
			}
		}
		for (size_t i = 0; i < n && next < 0; ++i) {
			if (!g.nodes[i].done) {
				next = static_cast<int>(i);
			}
		}
		Node &node = g.nodes[static_cast<size_t>(next)];
		for (Crossing &x : g.crossings) { // (what comes to it: all of it in before it is sized)
			if (x.to == next) {
				carry(table, g, x);
			}
		}
		if (!node.fixed) {
			node.mv = PistonMove{};
			node.grown = node.work = 0.0;
			if (node.piston) {
				node.mv = piston_move(node.p, node.start, node.pressure, step_seconds_);
				node.grown = node.mv.dv - node.p.moved;
				node.work = node.mv.force * node.mv.dv / node.p.area - node.p.worked;
			}
			for (size_t k = 0; k < node.parts.size(); ++k) {
				if (!size_part(table, node, k)) {
					return false;
				}
				node.parts[k].is_sized = true;
			}
		} else {
			for (Part &part : node.parts) {
				part.is_sized = true;
			}
		}
		for (Crossing &x : g.crossings) { // (what leaves it: at the temperature it now stands at)
			if (x.from == next) {
				carry(table, g, x);
				leave(table, g, x);
			}
		}
		node.done = true;
		if (!node.fixed) {
			double takes = 0.0, scale = std::fabs(node.whole + node.grown);
			for (const Part &part : node.parts) {
				takes += part.sized;
				scale += std::fabs(part.sized);
			}
			node.residual = node.whole + node.grown - takes;
			// (to 1e-10 of the volumes it is the difference of: a syringe pulled from next to nothing in a step is
			// closed to what it comes to, not to what it began at)
			node.tolerance = node.tight ? 0.0 : 0.5e-10 * scale + 1e-18;
			node.at_least = node.pressure <= LEAST && node.residual > 0.0;
		}
	}
	return true;
}

namespace closure {

// The root of a node's residual in its own pressure, `at(pressure, residual)` giving the residual there (false: no
// model): rising with the pressure, so a bracket from where it stands, then regula falsi (Illinois'), which no kink
// troubles (a plunger at the edge of its friction, a port run dry). With room to spare at the least pressure it
// stands there: a void. false where a state has no model.
template <typename At>
static bool root_of(Node &node, const At &at) {
	double lo = std::clamp(node.pressure, LEAST, MOST), hi = lo, f_lo = 0.0, f_hi = 0.0;
	if (!at(lo, f_lo)) {
		return false;
	}
	f_hi = f_lo;
	if (std::fabs(f_lo) <= node.tolerance) {
		return true;
	}
	if (f_lo < 0.0) { // its contents need more room than it has here: a higher pressure
		double reach = 1e-6 * lo;
		while (f_hi < 0.0 && hi < MOST) {
			lo = hi;
			f_lo = f_hi;
			hi = std::min(hi + reach, MOST);
			reach *= 4.0;
			if (!at(hi, f_hi)) {
				return false;
			}
		}
		if (f_hi < 0.0) {
			return true; // (no pressure holds it: the most)
		}
	} else {
		double reach = 1e-6 * hi;
		while (f_lo > 0.0 && lo > LEAST) {
			hi = lo;
			f_hi = f_lo;
			lo = std::max(lo - reach, LEAST);
			reach *= 4.0;
			if (!at(lo, f_lo)) {
				return false;
			}
		}
		if (f_lo > 0.0) {
			return true; // (room to spare at the least pressure: a void)
		}
	}
	int side = 0;
	for (int k = 0; k < 200 && hi - lo > 1e-15 * hi; ++k) {
		double middle = (lo * f_hi - hi * f_lo) / (f_hi - f_lo);
		if (!(middle > lo && middle < hi)) {
			middle = 0.5 * (lo + hi);
		}
		double f = 0.0;
		if (!at(middle, f)) {
			return false;
		}
		if (std::fabs(f) <= node.tolerance) {
			return true;
		}
		if (f < 0.0) {
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
	double f = 0.0;
	return at(std::fabs(f_lo) < std::fabs(f_hi) ? lo : hi, f);
}

} // namespace closure

// One node's pressure with the others' as they are.
bool Simulation::close_one(Closing &g, size_t i) const {
	Node &node = g.nodes[i];
	return root_of(node, [&](double pressure, double &residual) {
		node.pressure = pressure;
		if (!evaluate(g, false)) {
			return false;
		}
		residual = node.residual;
		return true;
	});
}

// The nodes `free[k]` on closed, each in turn inside the one before it: free[k]'s pressure the root of its residual
// with every later one closed at each pressure tried. Exact, and as slow as the product of their solves: for what
// Newton leaves. (A node's residual with the later ones closed still rises with its pressure: what it gives them
// comes back as their pressures.)
bool Simulation::close_nested(Closing &g, const std::vector<size_t> &free, size_t k) const {
	if (k + 1 == free.size()) {
		return close_one(g, free[k]);
	}
	// (the later ones closed as far as their pressures can be told: closed to their own tolerances only, what they
	// leave open is more than this one's where it is the stiffer, a little liquid next to much gas)
	Node &node = g.nodes[free[k]];
	for (size_t later = k + 1; later < free.size(); ++later) {
		g.nodes[free[later]].tight = true;
	}
	const bool ok = root_of(node, [&](double pressure, double &residual) {
		node.pressure = pressure;
		if (!close_nested(g, free, k + 1)) {
			return false;
		}
		residual = node.residual;
		return true;
	});
	if (k == 0) {
		for (const size_t i : free) {
			g.nodes[i].tight = false;
		}
	}
	return ok;
}

// The group's pressures. false where a state has no model, or where they were not found (a miss: nothing is then
// applied, and the step's next round or the next step finds them).
bool Simulation::close_pressures(Closing &g) const {
	std::vector<size_t> free;
	for (size_t i = 0; i < g.nodes.size(); ++i) {
		if (!g.nodes[i].fixed) {
			free.push_back(i);
		}
	}
	if (free.empty()) {
		return evaluate(g, false);
	}
	if (free.size() == 1) {
		return close_one(g, free[0]);
	}
	const size_t n = free.size();
	if (!evaluate(g, false)) {
		return false;
	}
	std::vector<double> jacobian(n * n), residual(n), step(n), kept(n);
	for (int iteration = 0; iteration < 60 && merit(g) > 1.0; ++iteration) {
		for (size_t r = 0; r < n; ++r) {
			residual[r] = g.nodes[free[r]].residual;
			kept[r] = g.nodes[free[r]].pressure;
		}
		const double before = merit(g);
		// the Jacobian by differences, each toward where its residual sends its pressure (a residual above 0: down), over
		// a billionth of it: the slope where it stands, not across a kink (a plunger at rest stands a hair off the edge of
		// its friction, and Newton's step with the slope there lowers every residual; with a slope across the edge it need
		// lower none); a node standing at a void is held there
		std::vector<uint8_t> held(n, 0);
		for (size_t c = 0; c < n; ++c) {
			held[c] = g.nodes[free[c]].at_least ? 1 : 0;
		}
		bool ok = true;
		for (size_t c = 0; c < n && ok; ++c) {
			// (and no further than a quarter of the way to a neighbour's pressure: where two meet, what crosses turns
			// about, a gas one way and a liquid the other)
			double h = 1e-9 * kept[c];
			for (const Link &link : g.links) {
				const int me = static_cast<int>(free[c]);
				if (link.a == me || link.b == me) {
					const double gap = std::fabs(g.nodes[static_cast<size_t>(link.a == me ? link.b : link.a)].pressure - kept[c]);
					if (gap > 0.0) {
						h = std::min(h, 0.25 * gap);
					}
				}
			}
			h = std::max(h, 1e-13 * kept[c]);
			if (residual[c] > 0.0 && kept[c] - h >= LEAST) {
				h = -h;
			}
			g.nodes[free[c]].pressure = kept[c] + h;
			ok = evaluate(g, false);
			for (size_t r = 0; r < n; ++r) {
				jacobian[r * n + c] = (g.nodes[free[r]].residual - residual[r]) / h;
			}
			g.nodes[free[c]].pressure = kept[c];
		}
		if (!ok) {
			return false;
		}
		for (size_t r = 0; r < n; ++r) {
			step[r] = -residual[r];
			if (held[r]) {
				for (size_t c = 0; c < n; ++c) {
					jacobian[r * n + c] = r == c ? 1.0 : 0.0;
					jacobian[c * n + r] = r == c ? 1.0 : 0.0;
				}
				step[r] = 0.0;
			}
		}
		// Gauss's elimination with a pivot by rows
		bool singular = false;
		for (size_t k = 0; k < n && !singular; ++k) {
			size_t pivot = k;
			for (size_t r = k + 1; r < n; ++r) {
				if (std::fabs(jacobian[r * n + k]) > std::fabs(jacobian[pivot * n + k])) {
					pivot = r;
				}
			}
			if (!(std::fabs(jacobian[pivot * n + k]) > 0.0)) {
				singular = true;
				break;
			}
			if (pivot != k) {
				for (size_t c = 0; c < n; ++c) {
					std::swap(jacobian[k * n + c], jacobian[pivot * n + c]);
				}
				std::swap(step[k], step[pivot]);
			}
			for (size_t r = k + 1; r < n; ++r) {
				const double f = jacobian[r * n + k] / jacobian[k * n + k];
				for (size_t c = k; c < n; ++c) {
					jacobian[r * n + c] -= f * jacobian[k * n + c];
				}
				step[r] -= f * step[k];
			}
		}
		bool better = false;
		if (!singular) {
			for (size_t k = n; k-- > 0;) {
				for (size_t c = k + 1; c < n; ++c) {
					step[k] -= jacobian[k * n + c] * step[c];
				}
				step[k] /= jacobian[k * n + k];
			}
			double scale = 1.0;
			for (int halving = 0; halving < 12 && !better; ++halving, scale *= 0.5) {
				for (size_t r = 0; r < n; ++r) {
					g.nodes[free[r]].pressure = std::clamp(kept[r] + scale * step[r], LEAST, MOST);
				}
				if (!evaluate(g, false)) {
					return false;
				}
				better = merit(g) < before;
			}
		}
		if (!better) {
			for (size_t r = 0; r < n; ++r) {
				g.nodes[free[r]].pressure = kept[r];
			}
			if (!evaluate(g, false)) {
				return false;
			}
			break;
		}
	}
	// what Newton left: each node closed inside the one before it (two or three), or sweeps of each one's own solve
	if (merit(g) > 1.0) {
		++closure_nested_;
		if (n <= 3) {
			if (!close_nested(g, free, 0) || !evaluate(g, false)) {
				return false;
			}
		}
		for (int sweep = 0; sweep < 200 && merit(g) > 1.0; ++sweep) {
			for (const size_t i : free) {
				if (!close_one(g, i)) {
					return false;
				}
			}
			if (!evaluate(g, false)) {
				return false;
			}
		}
	}
	if (merit(g) > 1.0) {
		++closure_misses_;
		return false;
	}
	return true;
}

// One group closed: its nodes gathered as they stand, its pressures found, and the step's moves and crossings
// applied. true when anything was. `touched`: every cell of it, for the fast equilibria that follow.
bool Simulation::close_group(const std::vector<CellId> &tops, const std::vector<size_t> &conduits, std::vector<CellId> &touched) {
	const SpeciesTable &table = world_.species();
	const std::span<const Edge> edges = world_.edges();
	Closing g;
	const auto column_of = [&](CellId top) -> const Column * {
		for (const Column &col : columns_) {
			if (col.top == top) {
				return &col;
			}
		}
		return nullptr;
	};
	const auto node_of = [&](CellId top) {
		for (size_t i = 0; i < g.nodes.size(); ++i) {
			if (g.nodes[i].top == top) {
				return static_cast<int>(i);
			}
		}
		return -1;
	};
	const auto add = [&](CellId top, bool fixed) {
		Node node;
		node.top = top;
		node.fixed = fixed;
		const CellMeta &m = world_.meta(top);
		const Column *col = column_of(top);
		if (!fixed && col != nullptr) {
			size_vessel(*col); // (at its one pressure as the round begins: its layers' work since booked)
		}
		std::vector<CellId> ids{ top };
		if (col != nullptr) {
			ids = col->cells;
			node.up = col->up;
		}
		for (const CellId id : ids) {
			Part part;
			part.id = id;
			const CellState &x = world_.snapshot_cell(id);
			const CellMeta &pm = world_.meta(id);
			part.content = content_of(table, x);
			part.energy = static_cast<double>(x.energy) * JOULE_PER_QUANTUM;
			part.volume = pm.volume;
			const Thermal &t = thermal(id);
			part.kelvin = t.defined && t.temperature > 0.0 ? t.temperature : T0;
			for (size_t s = 0; s < x.amount.size(); ++s) {
				const SpeciesId species{ static_cast<int32_t>(s) };
				const SpeciesDef &d = table.species(species);
				if (x.amount[s] <= 0 || d.wall) {
					continue;
				}
				const double kg = static_cast<double>(x.amount[s]) * MOL_PER_QUANTUM * static_cast<double>(table.molar_mass_ug(species)) * 1e-9;
				if (d.phase == Phase::Liquid || d.phase == Phase::Aqueous) {
					part.liquid_amounts.push_back(SpeciesAmount{ species, Amount{ x.amount[s] } });
					part.liquid_kg += kg;
				} else if (d.phase == Phase::Gas) {
					part.gas_amounts.push_back(SpeciesAmount{ species, Amount{ x.amount[s] } });
					part.gas_kg += kg;
				}
			}
			const bool solvent = std::any_of(part.liquid_amounts.begin(), part.liquid_amounts.end(), [&](const SpeciesAmount &a) { return table.species(a.species).phase == Phase::Liquid; });
			if (!solvent) { // (what is dissolved goes with its liquid: with none, nothing stands there to run)
				part.liquid_amounts.clear();
				part.liquid_kg = 0.0;
			}
			const double pressure = fixed || !t.defined || !(t.pressure > LEAST) ? P_ATMOSPHERE : t.pressure;
			if (part.liquid_kg > 0.0) {
				part.liquid = content_of(table, part.liquid_amounts);
				const double m3 = held_at(table, part.liquid, part.kelvin, pressure).volume;
				part.liquid_density = m3 > 0.0 ? part.liquid_kg / m3 : 0.0;
				part.liquid_viscosity = solvent_viscosity(table, part.kelvin);
			}
			if (part.gas_kg > 0.0) {
				part.gas = content_of(table, part.gas_amounts);
				part.gas_molar = part.gas_kg / part.gas.gas;
				const double cp = part.gas.capacity / part.gas.gas;
				part.gas_gamma = cp > GAS_CONSTANT ? cp / (cp - GAS_CONSTANT) : 0.0;
				part.gas_viscosity = gas_viscosity(table, part.kelvin);
			}
			node.parts.push_back(std::move(part));
		}
		// (Stage 94) its heights: its floor's, and with a shape where its solids and liquids lie, from the floor up
		node.height = m.height;
		node.weighed = world_.gravity_edge(top).index >= 0;
		node.shaped = m.column_volume > 0.0 && (m.column_area > 0.0 || m.column_shape.size() >= 2);
		if (node.shaped) {
			const double pressure = fixed || !thermal(top).defined || !(thermal(top).pressure > LEAST) ? P_ATMOSPHERE : thermal(top).pressure;
			double below = 0.0;
			for (size_t k = node.parts.size(); k-- > 0;) {
				Part &part = node.parts[k];
				Content condensed = part.content;
				condensed.gas = 0.0;
				const double m3 = none(condensed) ? 0.0 : held_at(table, condensed, part.kelvin, pressure).volume;
				part.bottom = column_height(m, below);
				part.top = column_height(m, below + m3);
				below += m3;
			}
			node.surface = node.parts[0].top;
			node.brim = column_height(m, m.column_volume);
			node.area = column_area(m, node.surface);
		}
		if (!fixed) {
			node.whole = world_.piston_volume(top);
			node.piston = m.piston.area > 0.0;
			node.p = m.piston;
			node.start = node.whole - m.piston.moved;
			node.tolerance = 1e-10 * node.whole + 1e-18; // (an evaluation sets it to the volumes it then holds)
			const Thermal &t = thermal(top);
			node.pressure = t.defined && t.pressure > LEAST ? std::min(t.pressure, MOST) : LEAST;
		}
		g.nodes.push_back(std::move(node));
		return static_cast<int>(g.nodes.size() - 1);
	};
	for (const CellId top : tops) {
		add(top, false);
	}
	for (const size_t i : conduits) {
		const Edge &e = edges[i];
		Link link;
		link.edge = i;
		link.conduit = e.conduit;
		link.port_a = e.port_a;
		link.port_b = e.port_b;
		link.liquid = (e.phases & phase_bit(Phase::Liquid)) != 0;
		link.dissolved = (e.phases & phase_bit(Phase::Aqueous)) != 0;
		link.gas = (e.phases & PHASES_GAS) != 0;
		link.a = node_of(e.a);
		link.b = node_of(e.b);
		if (link.a < 0) {
			link.a = add(e.a, true);
		}
		if (link.b < 0) {
			link.b = add(e.b, true);
		}
		link.heads = g.nodes[static_cast<size_t>(link.a)].weighed && g.nodes[static_cast<size_t>(link.b)].weighed;
		g.links.push_back(link);
	}
	for (const Node &node : g.nodes) {
		for (const Part &part : node.parts) {
			if (!part.content.known) {
				return false; // (a species with no thermodynamic data: no state to close)
			}
		}
	}
	if (!close_pressures(g) || !evaluate(g, true)) {
		return false;
	}
	for (const Node &node : g.nodes) {
		for (const Part &part : node.parts) {
			if (!node.fixed) {
				touched.push_back(part.id);
			}
		}
	}
	bool applied = false;
	// each plunger's move, its hand's work with it (W1)
	for (const Node &node : g.nodes) {
		if (!node.piston) {
			continue;
		}
		const Process was = process_;
		process_ = Process::Work;
		const double joule = node.mv.force * node.mv.dv / node.p.area - node.p.worked;
		const int64_t whole = std::llround(joule / JOULE_PER_QUANTUM);
		bool ok = true;
		if (whole != 0) {
			const Edge &e = edges[static_cast<size_t>(node.p.work.index)];
			const Refusal refused = transfer(node.p.work, e.a == node.top ? e.b : e.a, {}, Energy{ whole });
			CHEM_ASSERT(refused == Refusal::None, refusal_text(refused));
			ok = refused == Refusal::None;
		}
		if (ok) {
			const Refusal refused = world_.move_piston(node.top, node.whole + node.grown, node.mv.force, node.pressure, static_cast<double>(whole) * JOULE_PER_QUANTUM);
			CHEM_ASSERT(refused == Refusal::None, refusal_text(refused));
			thermal_valid_[static_cast<size_t>(node.top.index)] = 0;
			applied = applied || whole != 0 || node.grown != 0.0;
		}
		process_ = was;
	}
	// each crossing, from the highest pressure down: up its giver's layers with its volume (Stage 88), then across
	for (const Crossing &x : g.crossings) {
		const Link &link = g.links[x.link];
		Node &from = g.nodes[static_cast<size_t>(x.from)], &to = g.nodes[static_cast<size_t>(x.to)];
		const Part &part = from.parts[static_cast<size_t>(x.part)];
		std::vector<SpeciesAmount> amounts;
		const CellState &now = world_.snapshot_cell(part.id);
		for (const SpeciesAmount &a : x.liquid ? part.liquid_amounts : part.gas_amounts) {
			if (x.liquid && !link.dissolved && table.species(a.species).phase == Phase::Aqueous) {
				continue;
			}
			const int64_t there = now.amount[static_cast<size_t>(a.species.index)];
			const int64_t n = std::min(there, x.share >= 1.0 ? a.amount.quanta : static_cast<int64_t>(static_cast<double>(a.amount.quanta) * x.share));
			if (n > 0) {
				amounts.push_back(SpeciesAmount{ a.species, Amount{ n } });
			}
		}
		neutral_part(table, amounts);
		if (amounts.empty()) {
			continue;
		}
		const Content leaves = content_of(table, amounts);
		const Held held = held_at(table, leaves, x.kelvin, x.at_pa);
		const Energy carried{ std::llround(held.enthalpy / JOULE_PER_QUANTUM) };
		bool ok = true;
		if (x.part > 0) {
			const double over = world_.meta(part.id).pressed ? x.at_pa - P_ATMOSPHERE : 0.0;
			const Energy lifted{ std::llround((held.enthalpy - over * held.volume) / JOULE_PER_QUANTUM) };
			for (size_t k = static_cast<size_t>(x.part); k-- > 0 && ok;) {
				const Refusal refused = transfer(from.up[k], from.parts[k + 1].id, amounts, lifted);
				CHEM_ASSERT(refused == Refusal::None, refusal_text(refused));
				ok = refused == Refusal::None;
			}
			if (ok) {
				lift_volume(part.id, from.top, held.volume);
			}
		}
		if (!ok) {
			continue;
		}
		const Refusal refused = transfer(EdgeId{ static_cast<int32_t>(link.edge) }, from.top, amounts, carried);
		CHEM_ASSERT(refused == Refusal::None, refusal_text(refused));
		if (refused != Refusal::None) {
			continue;
		}
		world_.mark_crossing();
		double kg = 0.0;
		for (const SpeciesAmount &a : amounts) {
			kg += static_cast<double>(a.amount.quanta) * MOL_PER_QUANTUM * static_cast<double>(table.molar_mass_ug(a.species)) * 1e-9;
		}
		// (Stage 94) what gravity gives it falling from its giver's surface to its taker's, or takes lifting it
		const int64_t weight = x.fall != 0.0 ? std::llround(kg * GRAVITY * x.fall / JOULE_PER_QUANTUM) : 0;
		if (weight != 0) {
			const EdgeId gravity = world_.gravity_edge(to.top);
			const Process was = process_;
			process_ = Process::Work;
			const Refusal weighed = gravity.index >= 0 ? transfer(gravity, world_.gravity_cell(), {}, Energy{ weight }) : Refusal::UnknownEdge;
			CHEM_ASSERT(weighed == Refusal::None, refusal_text(weighed));
			process_ = was;
		}
		Crossed row{ EdgeId{ static_cast<int32_t>(link.edge) }, from.top, to.top, x.from_pa, x.to_pa, x.kelvin, kg, x.rate, x.seconds, carried.quanta, x.liquid,
			x.liquid ? part.liquid_density : part.gas_molar, x.liquid ? part.liquid_viscosity : part.gas_viscosity, x.liquid ? 0.0 : part.gas_gamma, part.kelvin };
		row.at_pa = x.at_pa;
		row.give = x.give;
		row.fall = x.fall;
		row.drive = x.drive;
		row.weight = weight;
		crossed_.push_back(row);
		applied = true;
	}
	// each vessel sized: the pressure found, its layers at it
	for (const Node &node : g.nodes) {
		if (node.fixed) {
			continue;
		}
		if (const Column *col = column_of(node.top); col != nullptr && size_vessel(*col)) {
			applied = true;
		}
		for (const Part &part : node.parts) {
			thermal_valid_[static_cast<size_t>(part.id.index)] = 0;
		}
	}
	return applied;
}

} // namespace chem
