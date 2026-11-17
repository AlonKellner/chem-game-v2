#include "lab/lab.hpp"

#include "chem/builtin_ids.hpp"
#include "chem/invariants.hpp"
#include "chem/sim.hpp"
#include "chem/thermo.hpp"
#include "chem/version.hpp"
#include "lab/conditions.hpp"
#include "lab/sha256.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <optional>
#include <utility>
#include <vector>

namespace lab {

using json = nlohmann::json;
using namespace chem;

std::string build_name() {
	return std::string("chem-game-v2 ") + chem::SPEC_VERSION;
}

namespace {

constexpr double STIRRED = 3.0; // a stirred vessel's mixing (GAME_SPEC §7)
// what a hand moves between two vessels: liquid with what is dissolved in it, and solids; never gas
constexpr PhaseMask HANDLED = static_cast<PhaseMask>(PHASES_LIQUID | phase_bit(Phase::Solid));

enum class Kind { Vessel, Bag, Burner, Tray };
enum class Place { Cabinet, Bench, On, Under, Outbox }; // On: on a vessel's mouth. Under: a burner under a vessel

struct Item {
	std::string id;
	Kind kind = Kind::Vessel;
	CellId cell; // a vessel's or a bag's contents; a burner's flame
	EdgeId mouth; // a vessel's opening to the room's air, while open
	double full = 0.0; // a burner's conductance with its valve fully open, W/K
	EdgeId heats; // a burner: its edge to the vessel over it
	int under = -1; // a burner: that vessel
	double valve = 0.0;
	int on = -1; // a bag or a condenser: the vessel whose mouth it is on
	EdgeId joint; // its edge to that vessel
	int holds = -1; // a vessel: what is on its mouth
	int drains_to = -1; // a vessel: what stands under its outlet
	bool stirred = false;
	bool submitted = false;
	Place place = Place::Cabinet;
	int col = -1, row = -1; // on the bench: its footprint's top-left square
	int w = 1, h = 1; // its footprint, in squares (GAME_SPEC §13)
	double capacity = 0.0; // a balloon: the volume at which it bursts, m^3
	bool burst = false;
};

struct Event {
	int64_t step = 0;
	std::string what;
	std::string item;
};

// a double as its bits, so that a save holds it exactly
std::string bits(double v) {
	static const char digits[] = "0123456789abcdef";
	uint64_t u = 0;
	static_assert(sizeof u == sizeof v);
	std::memcpy(&u, &v, sizeof u);
	std::string out = "f";
	for (int shift = 60; shift >= 0; shift -= 4) {
		out += digits[(u >> shift) & 0xf];
	}
	return out;
}

bool from_bits(const json &j, double &v) {
	if (!j.is_string()) {
		return false;
	}
	const std::string text = j.get<std::string>();
	if (text.size() != 17 || text[0] != 'f') {
		return false;
	}
	uint64_t u = 0;
	for (size_t i = 1; i < text.size(); ++i) {
		const char ch = text[i];
		const int digit = ch >= '0' && ch <= '9' ? ch - '0' : ch >= 'a' && ch <= 'f' ? ch - 'a' + 10 : -1;
		if (digit < 0) {
			return false;
		}
		u = u << 4 | static_cast<uint64_t>(digit);
	}
	std::memcpy(&v, &u, sizeof v);
	return true;
}

const char *const KIND_NAME[] = { "vessel", "balloon", "burner", "tray" };
const char *const PLACE_NAME[] = { "cabinet", "bench", "on", "under", "outbox" };

double number(const json &j, const char *key, double fallback) {
	return j.contains(key) && j.at(key).is_number() ? j.at(key).get<double>() : fallback;
}

double mol_of(const SpeciesTable &table, SpeciesId id, const json &amount) {
	if (amount.contains("mol")) {
		return amount.at("mol").get<double>();
	}
	return amount.at("g").get<double>() / (static_cast<double>(table.molar_mass_ug(id)) * 1e-6);
}

// An item's footprint on the bench, in squares of the grid (GAME_SPEC §13):
// room for its body, what stands on its mouth starts above it, and a vessel's
// stand over a burner. A bench may give its own ("size").
void default_size(const std::string &kind, double volume_ml, bool condenser, int &w, int &h) {
	if (kind == "burner") {
		w = h = 3; // the burner and its valve beside it
	} else if (kind == "balloon") {
		w = 4;
		h = 5;
	} else if (condenser) {
		w = 2;
		h = 5;
	} else if (volume_ml < 100.0) {
		w = 2;
		h = 6;
	} else {
		w = 3;
		h = 7;
	}
}

} // namespace

struct Lab::Impl {
	std::optional<Simulation> sim;
	std::vector<Item> items;
	std::vector<Event> events;
	int cols = 28, rows = 15; // the bench's grid, in squares
	CellId air;
	CellId room;
	Totals genesis;
	bool check = true;
	bool broken = false;
	// a command has moved matter since the last step ended: a step is in
	// progress, and a change of connections waits for its end
	bool mid_step = false;
	bool untraced = false; // the last step was unchecked
	// signals (GAME_SPEC §13.3)
	std::vector<Part> parts;
	std::vector<Wire> wires;
	std::vector<Rig> rigs;
	std::vector<ControlSetting> controls;
	int64_t made[4] = { 0, 0, 0, 0 }; // the parts made of each kind, for their names
	bool rang = false; // an alarm started ringing in the last step

	const SpeciesTable &table() const { return sim->world().species(); }
	World &world() { return sim->world(); }
	const World &world() const { return sim->world(); }

	int find(const std::string &id) const {
		for (size_t i = 0; i < items.size(); ++i) {
			if (items[i].id == id) {
				return static_cast<int>(i);
			}
		}
		return -1;
	}

	int find_part(const std::string &id) const {
		for (size_t i = 0; i < parts.size(); ++i) {
			if (parts[i].id == id) {
				return static_cast<int>(i);
			}
		}
		return -1;
	}
	// the name of what standing_in found: an item, or a part after the items
	const std::string &name_of(int index) const {
		return index < static_cast<int>(items.size()) ? items[static_cast<size_t>(index)].id : parts[static_cast<size_t>(index) - items.size()].id;
	}

	// ------------------------------------------------------------- signals (§13.3)

	// a monitor's reading of one item, from what the view would show of it
	std::optional<double> reading(const Quantity &q) const {
		const int i = find(q.item);
		if (i < 0) {
			return std::nullopt;
		}
		LabView one;
		one.items.push_back(item_view(items[static_cast<size_t>(i)]));
		return measure(one, q);
	}

	// a control's value now; none when it is not one of the lab's
	std::optional<double> control_now(const std::string &item_id, const std::string &control) const {
		const int i = find(item_id);
		if (control == "stir" && i >= 0 && items[static_cast<size_t>(i)].kind != Kind::Burner && items[static_cast<size_t>(i)].kind != Kind::Tray) {
			return items[static_cast<size_t>(i)].stirred ? 1.0 : 0.0;
		}
		if (control == "valve" && i >= 0 && items[static_cast<size_t>(i)].kind == Kind::Burner) {
			return items[static_cast<size_t>(i)].valve;
		}
		return std::nullopt;
	}

	void record(const std::string &item_id, const std::string &control, double value, const std::string &by) {
		for (ControlSetting &c : controls) {
			if (c.item == item_id && c.control == control) {
				c = ControlSetting{ item_id, control, value, by, static_cast<int64_t>(world().step()) };
				return;
			}
		}
		controls.push_back(ControlSetting{ item_id, control, value, by, static_cast<int64_t>(world().step()) });
	}

	// a control set, at a step boundary, by a rig or by the player's command;
	// false when it cannot be set now (a vessel off the bench is not stirred)
	bool set_control(const std::string &item_id, const std::string &control, double value, const std::string &by) {
		if (control == "reset") {
			const int t = find_part(item_id);
			if (t < 0 || parts[static_cast<size_t>(t)].kind != PartKind::Timer) {
				return false;
			}
			reset(parts[static_cast<size_t>(t)]);
			record(item_id, control, value, by);
			return true;
		}
		const int i = find(item_id);
		if (i < 0) {
			return false;
		}
		Item &x = items[static_cast<size_t>(i)];
		if (control == "stir") {
			if (x.kind == Kind::Burner || x.kind == Kind::Tray || x.place == Place::Cabinet || x.place == Place::Outbox) {
				return false;
			}
			const bool on = value > 0.5;
			world().set_mixing(x.cell, on ? STIRRED : 1.0);
			x.stirred = on;
			record(item_id, control, on ? 1.0 : 0.0, by);
			return true;
		}
		if (control == "valve" && x.kind == Kind::Burner) {
			if (x.under >= 0) {
				world().set_conductance(x.heats, x.full * value);
			}
			x.valve = value;
			record(item_id, control, value, by);
			return true;
		}
		return false;
	}

	// the signals at a step's end; false when an invariant fails
	bool signals_step(std::string &why) {
		rang = false;
		if (parts.empty()) {
			return true;
		}
		const std::vector<Part> before = parts;
		const SignalStep done = evaluate(parts, wires, rigs, [this](const Quantity &q) { return reading(q); }, static_cast<int64_t>(world().step()));
		std::vector<std::string> failing;
		if (check) {
			failing = check_G2(parts, wires, before);
		}
		for (const RigAction &a : done.actions) {
			const Rig &rig = rigs[a.rig];
			set_control(rig.item, rig.control, a.value, rig.from);
		}
		rang = !done.rang.empty();
		if (check && failing.empty()) {
			failing = check_G1(controls, [this](const std::string &item_id, const std::string &control) { return control_now(item_id, control); });
		}
		if (!failing.empty()) {
			broken = true;
			why = "an invariant failed: " + failing[0];
			return false;
		}
		return true;
	}

	// a rig's setting for its control, from a command: true or "stir"/false or "stop", a valve's 0 to 1, true or "reset"
	static bool setting_of(const std::string &control, const json &j, std::optional<double> &out, std::string &why) {
		if (j.is_null()) {
			out.reset(); // leave it as it is
			return true;
		}
		if (control == "stir" && (j.is_boolean() || (j.is_string() && (j == "stir" || j == "stop")))) {
			out = (j.is_boolean() ? j.get<bool>() : j == "stir") ? 1.0 : 0.0;
			return true;
		}
		if (control == "valve" && j.is_number() && j.get<double>() >= 0.0 && j.get<double>() <= 1.0) {
			out = j.get<double>();
			return true;
		}
		if (control == "reset" && ((j.is_boolean() && j.get<bool>()) || j == "reset")) {
			out = 1.0;
			return true;
		}
		why = "a " + control + " is set " + (control == "stir" ? "to true (stir) or false (stop)" : control == "valve" ? "between 0 and 1" : "to true (reset)") + ", or null to leave it";
		return false;
	}

	// a part named by a command's field, of the kind wanted (any kind when none)
	bool part(const json &c, const char *key, std::optional<PartKind> kind, int &index, std::string &why) const {
		if (!c.contains(key) || !c.at(key).is_string()) {
			why = std::string("the command needs \"") + key + "\"";
			return false;
		}
		const std::string id = c.at(key).get<std::string>();
		index = find_part(id);
		if (index < 0) {
			why = "there is no part \"" + id + "\" on the bench";
			return false;
		}
		if (kind && parts[static_cast<size_t>(index)].kind != *kind) {
			why = "\"" + id + "\" is not a " + part_kind_name(*kind);
			return false;
		}
		return true;
	}

	// the signals as JSON: for a report (plain numbers), or a save (`exact`: numbers as their bits)
	json signals_json(bool exact) const {
		static const char *const OPS[] = { "above", "below", "at_least", "at_most", "reaches", "between", "outside" };
		const auto num = [exact](double v) { return exact ? json(bits(v)) : json(v); };
		const auto maybe = [&](const std::optional<double> &v) { return v ? num(*v) : json(nullptr); };
		json out;
		json list = json::array();
		for (const Part &p : parts) {
			json j{ { "id", p.id }, { "kind", part_kind_name(p.kind) }, { "at", { p.col, p.row } }, { "in", p.in }, { "own", p.own }, { "out", p.out } };
			if (p.kind == PartKind::Circuit) {
				j["inner"] = { p.inner_w, p.inner_h };
			}
			if (p.kind == PartKind::Monitor) {
				j["watching"] = p.watching;
				j["quantity"] = { { "item", p.quantity.item }, { "what", p.quantity.what }, { "measure", p.quantity.measure } };
				j["compare"] = OPS[static_cast<int>(p.comparison.op)];
				j["values"] = { num(p.comparison.a), num(p.comparison.b) };
				j["last"] = maybe(p.last);
			} else if (p.kind == PartKind::Timer) {
				j["preset"] = p.preset;
				j["left"] = p.left;
			} else {
				j["ringing"] = p.ringing;
				j["rang_at"] = p.rang_at;
			}
			list.push_back(j);
		}
		out["parts"] = list;
		json w = json::array();
		for (const Wire &x : wires) {
			w.push_back({ { "from", x.from }, { "to", x.to } });
		}
		out["wires"] = w;
		json r = json::array();
		for (const Rig &x : rigs) {
			r.push_back({ { "from", x.from }, { "item", x.item }, { "control", x.control }, { "change", x.change }, { "on", maybe(x.on) }, { "off", maybe(x.off) }, { "any", maybe(x.any) },
					{ "seen", x.seen } });
		}
		out["rigs"] = r;
		json k = json::array();
		for (const ControlSetting &x : controls) {
			k.push_back({ { "item", x.item }, { "control", x.control }, { "value", num(x.value) }, { "by", x.by }, { "step", x.step } });
		}
		out["controls"] = k;
		out["made"] = { made[0], made[1], made[2], made[3] };
		return out;
	}

	// a save's signals, back
	bool signals_from(const json &j, std::string &why) {
		const auto exact = [&](const json &v, double &out) { return from_bits(v, out) || (why = "the save's signals hold a number that is not one", false); };
		const auto maybe = [&](const json &v, std::optional<double> &out) {
			if (v.is_null()) {
				out.reset();
				return true;
			}
			double d = 0.0;
			if (!exact(v, d)) {
				return false;
			}
			out = d;
			return true;
		};
		for (const json &x : j.at("parts")) {
			Part p;
			p.id = x.at("id").get<std::string>();
			if (!part_kind_of(x.at("kind").get<std::string>(), p.kind)) {
				why = "the save holds a part of no kind";
				return false;
			}
			p.col = x.at("at").at(0).get<int>();
			p.row = x.at("at").at(1).get<int>();
			p.in = x.value("in", std::string());
			if (x.contains("inner")) {
				p.inner_w = x.at("inner").at(0).get<int>();
				p.inner_h = x.at("inner").at(1).get<int>();
			}
			p.own = x.at("own").get<bool>();
			p.out = x.at("out").get<bool>();
			if (p.kind == PartKind::Monitor) {
				p.watching = x.at("watching").get<bool>();
				const json &q = x.at("quantity");
				p.quantity = Quantity{ q.at("item").get<std::string>(), q.at("what").get<std::string>(), q.at("measure").get<std::string>() };
				if (!op_of(x.at("compare").get<std::string>(), p.comparison.op) || !exact(x.at("values").at(0), p.comparison.a) || !exact(x.at("values").at(1), p.comparison.b) ||
						!maybe(x.at("last"), p.last)) {
					why = why.empty() ? "the save holds a monitor that cannot be read" : why;
					return false;
				}
			} else if (p.kind == PartKind::Timer) {
				p.preset = x.at("preset").get<int64_t>();
				p.left = x.at("left").get<int64_t>();
			} else {
				p.ringing = x.at("ringing").get<bool>();
				p.rang_at = x.at("rang_at").get<int64_t>();
			}
			parts.push_back(p);
		}
		for (const json &x : j.at("wires")) {
			wires.push_back(Wire{ x.at("from").get<std::string>(), x.at("to").get<std::string>() });
		}
		for (const json &x : j.at("rigs")) {
			Rig r;
			r.from = x.at("from").get<std::string>();
			r.item = x.at("item").get<std::string>();
			r.control = x.at("control").get<std::string>();
			r.change = x.at("change").get<bool>();
			r.seen = x.at("seen").get<bool>();
			if (!maybe(x.at("on"), r.on) || !maybe(x.at("off"), r.off) || !maybe(x.at("any"), r.any)) {
				return false;
			}
			rigs.push_back(r);
		}
		for (const json &x : j.at("controls")) {
			ControlSetting k;
			k.item = x.at("item").get<std::string>();
			k.control = x.at("control").get<std::string>();
			k.by = x.at("by").get<std::string>();
			k.step = x.at("step").get<int64_t>();
			if (!exact(x.at("value"), k.value)) {
				return false;
			}
			controls.push_back(k);
		}
		for (size_t k = 0; k < 4 && k < j.at("made").size(); ++k) {
			made[k] = j.at("made").at(k).get<int64_t>();
		}
		return true;
	}

	// ---- circuits (§13.3)

	// the part on a square of a circuit's own grid, other than `except`; -1 for none
	int inside_at(const std::string &circuit, int col, int row, int except = -1) const {
		for (size_t i = 0; i < parts.size(); ++i) {
			if (static_cast<int>(i) != except && parts[i].in == circuit && parts[i].col == col && parts[i].row == row) {
				return static_cast<int>(i);
			}
		}
		return -1;
	}
	// a part, and everything a circuit holds, and what that holds
	std::vector<std::string> held_by(const std::string &id) const {
		std::vector<std::string> out{ id };
		for (size_t k = 0; k < out.size(); ++k) {
			for (const Part &p : parts) {
				if (p.in == out[k]) {
					out.push_back(p.id);
				}
			}
		}
		return out;
	}
	// a square free for a part: on the bench, or on the grid of the circuit it is to stand in
	bool square_free(const std::string &circuit, int col, int row, int except_part, std::string &why) const {
		if (circuit.empty()) {
			const int there = standing_in(col, row, 1, 1, except_part < 0 ? -1 : static_cast<int>(items.size()) + except_part);
			if (!on_grid(col, row, 1, 1) || there >= 0) {
				why = there >= 0 ? "\"" + name_of(there) + "\" stands there" : "that is off the bench";
				return false;
			}
			return true;
		}
		const int c = find_part(circuit);
		if (c < 0 || parts[static_cast<size_t>(c)].kind != PartKind::Circuit) {
			why = "there is no circuit \"" + circuit + "\"";
			return false;
		}
		const Part &k = parts[static_cast<size_t>(c)];
		const int there = inside_at(circuit, col, row, except_part);
		if (col < 0 || row < 0 || col >= k.inner_w || row >= k.inner_h || there >= 0) {
			why = there >= 0 ? "\"" + parts[static_cast<size_t>(there)].id + "\" stands there" : "that is off the circuit's grid";
			return false;
		}
		return true;
	}

	// one step; false when an invariant fails
	bool step_once(std::string &why) {
		// A checked step needs its trace to start where the last one ended. After
		// unchecked steps, with a pour already applied in this one, it cannot:
		// this step runs unchecked and the next is checked.
		const bool traceable = !(untraced && mid_step);
		mid_step = false;
		if (!check || !traceable) {
			sim->advance();
			untraced = true;
		} else {
			untraced = false;
			const StepTrace trace = sim->step();
			const Violations violations = check_all(table(), trace, sim->equilibria());
			if (!violations.empty()) {
				broken = true;
				why = "an invariant failed: " + violations[0];
				return false;
			}
		}
		if (!signals_step(why)) {
			return false;
		}
		burst_balloons();
		return true;
	}

	// A balloon filled past its capacity bursts (GAME_SPEC §13): it comes off
	// whatever it is on, and its gas is the room's. Between steps.
	void burst_balloons() {
		for (size_t i = 0; i < items.size(); ++i) {
			Item &bag = items[i];
			if (bag.kind != Kind::Bag || bag.burst || !(bag.capacity > 0.0)) {
				continue;
			}
			const CellState &cell = world().snapshot_cell(bag.cell);
			const double t = kelvin(bag.cell);
			const Fill fill = fill_of(table(), cell, t);
			if (!(fill.gas + fill.condensed > bag.capacity)) {
				continue;
			}
			bag.burst = true;
			events.push_back(Event{ static_cast<int64_t>(world().step()), "burst", bag.id });
			if (bag.on >= 0) {
				Item &v = items[static_cast<size_t>(bag.on)];
				world().connect(v.cell, air, PHASES_GAS, &v.mouth);
				world().disconnect(bag.joint);
				v.holds = -1;
				bag.on = -1;
				bag.joint = EdgeId{};
				bag.place = Place::Bench;
				bag.col = v.col;
				bag.row = v.row;
			}
			std::vector<SpeciesAmount> gas;
			for (size_t s = 0; s < cell.amount.size(); ++s) {
				if (cell.amount[s] > 0 && table().species(SpeciesId{ static_cast<int32_t>(s) }).phase == Phase::Gas) {
					gas.push_back(SpeciesAmount{ SpeciesId{ static_cast<int32_t>(s) }, Amount{ cell.amount[s] } });
				}
			}
			EdgeId torn;
			if (!gas.empty() && world().connect(bag.cell, air, PHASES_GAS, &torn) == Refusal::None) {
				world().transfer(torn, bag.cell, gas, enthalpy_carried(table(), gas, t));
				mid_step = true;
			}
		}
	}

	// what stands on any of the squares of a footprint at (col, row), other
	// than `except`; -1 for nothing
	int standing_in(int col, int row, int w, int h, int except = -1) const {
		for (size_t i = 0; i < items.size(); ++i) {
			const Item &o = items[i];
			if (static_cast<int>(i) == except || o.place != Place::Bench || o.col < 0) {
				continue;
			}
			if (col < o.col + o.w && o.col < col + w && row < o.row + o.h && o.row < row + h) {
				return static_cast<int>(i);
			}
		}
		for (size_t i = 0; i < parts.size(); ++i) {
			const Part &o = parts[i];
			const int index = static_cast<int>(items.size() + i);
			if (index != except && o.in.empty() && col <= o.col && o.col < col + w && row <= o.row && o.row < row + h) {
				return index;
			}
		}
		return -1;
	}
	bool on_grid(int col, int row, int w, int h) const { return col >= 0 && row >= 0 && col + w <= cols && row + h <= rows; }

	// Sets an item down beside another (GAME_SPEC §13): on the first free
	// footprint, rows by how far their foot is from the other's foot, then
	// columns by how far from it, the left first. false when nowhere is free.
	bool set_beside(int index, const Item &near) {
		Item &x = items[static_cast<size_t>(index)];
		const int foot = near.col >= 0 ? near.row + near.h : rows;
		const int centre = near.col >= 0 ? near.col : 0;
		for (int dr = 0; dr <= rows; ++dr) {
			for (const int row : { foot - x.h - dr, foot - x.h + dr }) {
				for (int dc = 0; dc <= cols; ++dc) {
					for (const int col : { centre - dc, centre + dc }) {
						if (on_grid(col, row, x.w, x.h) && standing_in(col, row, x.w, x.h, index) < 0) {
							x.place = Place::Bench;
							x.col = col;
							x.row = row;
							return true;
						}
					}
				}
			}
		}
		return false;
	}

	// true when the item is on the bench, in use
	bool on_bench(const Item &x, std::string &why) const {
		if (x.place == Place::Cabinet || x.place == Place::Outbox) {
			why = "\"" + x.id + "\" is " + (x.place == Place::Cabinet ? "in the cabinet" : "in the outbox") + ", not on the bench";
			return false;
		}
		return true;
	}

	json save() const;

	ItemView item_view(const Item &item) const {
		const Impl &m = *this;
		const SpeciesTable &table = m.table();
		static const char PHASE[] = { 's', 'l', 'g', 'a' };
		ItemView v;
		v.id = item.id;
		v.kind = KIND_NAME[static_cast<int>(item.kind)];
		v.place = PLACE_NAME[static_cast<int>(item.place)];
		v.col = item.col;
		v.row = item.row;
		v.w = item.w;
		v.h = item.h;
		v.on = item.on;
		v.under = item.under;
		v.holds = item.holds;
		v.drains_to = item.drains_to;
		v.stirred = item.stirred;
		v.submitted = item.submitted;
		v.burst = item.burst;
		v.valve = item.valve;
		v.capacity_ml = item.capacity * 1e6;
		if (item.kind != Kind::Burner) {
			const double kelvin = m.kelvin(item.cell);
			v.celsius = kelvin - 273.15;
			const CellState &cell = m.world().snapshot_cell(item.cell);
			const Fill fill = fill_of(table, cell, kelvin);
			v.gas_ml = fill.gas * 1e6;
			v.condensed_ml = fill.condensed * 1e6;
			v.volume_ml = m.world().meta(item.cell).volume * 1e6;
			for (size_t s = 0; s < cell.amount.size(); ++s) {
				if (cell.amount[s] == 0) {
					continue;
				}
				const SpeciesId id{ static_cast<int32_t>(s) };
				const SpeciesDef &def = table.species(id);
				SubstanceView row;
				row.species = std::string(def.id);
				row.formula = std::string(def.formula);
				row.phase = PHASE[static_cast<int>(def.phase)];
				row.wall = def.wall;
				row.mol = static_cast<double>(cell.amount[s]) * MOL_PER_QUANTUM;
				row.g = m.grams(item.cell, id);
				if (def.phase == Phase::Solid && !def.wall) {
					row.particles = cell.particles[s];
					row.size_um = particle_diameter(table, cell, id) * 1e6;
				}
				if (def.phase == Phase::Liquid && !def.wall) {
					v.liquid_ml += row.mol * def.molar_volume * 1e6;
				}
				v.contents.push_back(row);
			}
			// a vessel with a coolant is a condenser: one of its edges goes to a reservoir that is neither the room nor a burner
			for (const Edge &e : m.world().edges()) {
				const CellId other = e.a == item.cell ? e.b : e.b == item.cell ? e.a : CellId{};
				if (other.index >= 0 && e.open && m.world().meta(other).reservoir && !(other == m.room)) {
					bool burner = false;
					for (const Item &b : m.items) {
						burner = burner || (b.kind == Kind::Burner && b.cell == other);
					}
					v.condenser = v.condenser || !burner;
				}
			}
		} else {
			v.celsius = m.world().meta(item.cell).reservoir_temperature - 273.15;
		}
		return v;
	}

	// connections change at a step boundary: take the step in progress to its end
	bool boundary(std::string &why) { return !mid_step || step_once(why); }

	double kelvin(CellId cell) const {
		double t = 0.0;
		world().temperature(cell, t);
		return t;
	}

	double grams(CellId cell, SpeciesId s) const { return world().amount(cell, s).mol() * static_cast<double>(table().molar_mass_ug(s)) * 1e-6; }

	// the item named by a command's field, of the kind wanted
	bool item(const json &c, const char *key, Kind kind, int &index, std::string &why) const {
		if (!c.contains(key) || !c.at(key).is_string()) {
			why = std::string("the command needs \"") + key + "\"";
			return false;
		}
		const std::string id = c.at(key).get<std::string>();
		index = find(id);
		if (index < 0) {
			why = "there is no \"" + id + "\" on the bench";
			return false;
		}
		const Kind is = items[static_cast<size_t>(index)].kind;
		const bool ok = kind == Kind::Vessel ? is == Kind::Vessel || is == Kind::Bag : is == kind; // "vessel": anything a hand moves that holds matter
		if (!ok) {
			why = "\"" + id + "\" is not " + (kind == Kind::Burner ? "a burner" : kind == Kind::Bag ? "a balloon" : "a vessel");
			return false;
		}
		return true;
	}

	// moves `amounts` from one item's cell to another's, along a handling edge
	bool move(const Item &from, const Item &to, const std::vector<SpeciesAmount> &amounts, std::string &why) {
		if (amounts.empty()) {
			why = "there is nothing of that in \"" + from.id + "\"";
			return false;
		}
		// does it fit?
		const CellMeta &meta = world().meta(to.cell);
		if (meta.rigid) {
			double adds = 0.0;
			for (const SpeciesAmount &a : amounts) {
				const SpeciesDef &def = table().species(a.species);
				if (def.phase == Phase::Liquid || def.phase == Phase::Solid) {
					adds += a.amount.mol() * def.molar_volume;
				}
			}
			const Fill fill = fill_of(table(), world().snapshot_cell(to.cell), T0);
			if (!fill.defined || fill.condensed + adds > meta.volume) {
				why = "that does not fit in \"" + to.id + "\"";
				return false;
			}
		}
		EdgeId edge;
		const std::span<const Edge> edges = world().edges();
		for (size_t i = 0; i < edges.size(); ++i) {
			const Edge &e = edges[i];
			const bool joins = (e.a == from.cell && e.b == to.cell) || (e.a == to.cell && e.b == from.cell);
			if (e.open && joins && e.phases == HANDLED && !e.drain) {
				edge = EdgeId{ static_cast<int32_t>(i) };
			}
		}
		if (edge.index < 0 && world().connect(from.cell, to.cell, HANDLED, &edge) != Refusal::None) {
			why = "the two cannot be joined";
			return false;
		}
		const Refusal refused = world().transfer(edge, from.cell, amounts, enthalpy_carried(table(), amounts, kelvin(from.cell)));
		if (refused != Refusal::None) {
			why = refusal_text(refused);
			return false;
		}
		mid_step = true;
		return true;
	}

	bool run(const json &c, std::string &why);
};

Lab::Lab(std::unique_ptr<Impl> impl) :
		impl_(std::move(impl)) {}
Lab::~Lab() = default;

std::unique_ptr<Lab> Lab::open(std::string_view bench_json, std::string &why) {
	json bench;
	try {
		bench = json::parse(bench_json, nullptr, true, true);
	} catch (const std::exception &e) {
		why = std::string("the bench is not JSON: ") + e.what();
		return nullptr;
	}
	const std::string format = bench.is_object() ? bench.value("format", "") : "";
	if (format != "chem-bench/1" && format != "chem-bench/2") {
		why = "the bench's \"format\" is not \"chem-bench/2\" (or \"chem-bench/1\")";
		return nullptr;
	}
	// chem-bench/1: a grid of cells, every item on one; /2: squares, an item on a footprint of them
	const bool squares = format == "chem-bench/2";
	auto impl = std::make_unique<Impl>();
	if (!squares) {
		impl->cols = 12;
		impl->rows = 4;
	}
	const SpeciesTable &table = builtin_species();
	World::Builder b(table);
	bool ok = true;
	const auto amount = [&ok](double mol) {
		bool fits = false;
		const Amount a = amount_from_mol(mol, fits);
		ok = ok && fits;
		return a;
	};
	try {
		const json &room = bench.at("room");
		const double room_kelvin = room.at("celsius").get<double>() + 273.15;
		impl->room = b.add_reservoir("room", room_kelvin);
		impl->air = b.add_cell("air");
		const double air_mol = number(room, "air_mol", 1000.0);
		b.set_amount(impl->air, builtin::nitrogen, amount(0.79 * air_mol)).set_amount(impl->air, builtin::oxygen, amount(0.21 * air_mol));
		b.set_temperature(impl->air, room_kelvin);
		b.add_edge(impl->air, impl->room, PHASES_NONE, 1000.0);

		if (bench.contains("grid")) {
			impl->cols = bench.at("grid").at(0).get<int>();
			impl->rows = bench.at("grid").at(1).get<int>();
			if (impl->cols < 1 || impl->rows < 1) {
				why = "the bench's grid has at least one column and one row";
				return nullptr;
			}
		}
		for (const json &j : bench.at("items")) {
			Item item;
			item.id = j.at("id").get<std::string>();
			if (item.id.empty() || impl->find(item.id) >= 0) {
				why = "an item's id is empty or used twice: \"" + item.id + "\"";
				return nullptr;
			}
			const std::string kind = j.at("kind").get<std::string>();
			if (squares) {
				default_size(kind, number(j, "volume_ml", 0.0), j.contains("coolant"), item.w, item.h);
			}
			if (squares && j.contains("size")) {
				item.w = j.at("size").at(0).get<int>();
				item.h = j.at("size").at(1).get<int>();
				if (item.w < 1 || item.h < 1) {
					why = "item \"" + item.id + "\": a size is at least one square each way";
					return nullptr;
				}
			}
			if (j.contains("at")) {
				item.place = Place::Bench;
				item.col = j.at("at").at(0).get<int>();
				item.row = j.at("at").at(1).get<int>();
				if (!impl->on_grid(item.col, item.row, item.w, item.h) || impl->standing_in(item.col, item.row, item.w, item.h) >= 0) {
					why = "item \"" + item.id + "\" stands off the bench's grid, or where something else stands";
					return nullptr;
				}
			}
			if (kind == "burner") {
				item.kind = Kind::Burner;
				item.cell = b.add_reservoir(item.id, j.at("celsius").get<double>() + 273.15);
				item.full = j.at("conductance").get<double>();
				impl->items.push_back(item);
				continue;
			}
			if (kind != "vessel" && kind != "balloon") {
				why = "item \"" + item.id + "\": a kind is vessel, balloon or burner";
				return nullptr;
			}
			item.kind = kind == "vessel" ? Kind::Vessel : Kind::Bag;
			item.capacity = item.kind == Kind::Bag ? number(j, "capacity_ml", 3000.0) * 1e-6 : 0.0;
			const double volume = item.kind == Kind::Vessel ? j.at("volume_ml").get<double>() * 1e-6 : 0.0;
			item.cell = item.kind == Kind::Vessel ? b.add_rigid_cell(item.id, volume) : b.add_cell(item.id);
			const SpeciesId wall = table.find(j.at("wall").get<std::string>());
			if (!table.has(wall) || !table.species(wall).wall) {
				why = "item \"" + item.id + "\": its wall is not a wall species";
				return nullptr;
			}
			b.set_amount(item.cell, wall, amount(j.at("wall_g").get<double>() / (static_cast<double>(table.molar_mass_ug(wall)) * 1e-6)));
			double condensed = 0.0;
			if (j.contains("contents")) {
				for (const auto &[name, spec] : j.at("contents").items()) {
					const SpeciesId id = table.find(name);
					if (!table.has(id) || table.species(id).wall) {
						why = "item \"" + item.id + "\": there is no species \"" + name + "\" to put in it";
						return nullptr;
					}
					const double mol = mol_of(table, id, spec);
					b.set_amount(item.cell, id, amount(mol));
					if (spec.contains("particles")) {
						b.set_particles(item.cell, id, spec.at("particles").get<int64_t>());
					}
					const SpeciesDef &def = table.species(id);
					if (def.phase == Phase::Liquid || def.phase == Phase::Solid) {
						condensed += mol * def.molar_volume;
					}
				}
			}
			if (item.kind == Kind::Vessel) {
				// open to the room: air fills what its contents leave
				const double air = P_ATMOSPHERE * (volume - condensed) / (GAS_CONSTANT * room_kelvin);
				if (air > 0.0) {
					b.set_amount(item.cell, builtin::nitrogen, amount(0.79 * air)).set_amount(item.cell, builtin::oxygen, amount(0.21 * air));
				}
				item.mouth = b.add_edge(item.cell, impl->air, PHASES_GAS);
			}
			b.set_temperature(item.cell, room_kelvin);
			b.add_edge(item.cell, impl->room, PHASES_NONE, number(j, "loss", 0.5));
			if (j.contains("coolant")) {
				const json &coolant = j.at("coolant");
				const CellId cold = b.add_reservoir(item.id + ".coolant", coolant.at("celsius").get<double>() + 273.15);
				b.add_edge(item.cell, cold, PHASES_NONE, coolant.at("conductance").get<double>());
			}
			impl->items.push_back(item);
		}
		// the tray under everything: where what overflows goes
		Item tray;
		tray.id = "tray";
		tray.kind = Kind::Tray;
		tray.place = Place::Bench;
		if (impl->find(tray.id) >= 0) {
			why = "\"tray\" is the bench's own";
			return nullptr;
		}
		tray.cell = b.add_cell("tray");
		b.set_amount(tray.cell, builtin::glass, amount(300.0 / 60.083)).set_temperature(tray.cell, room_kelvin);
		b.add_edge(tray.cell, impl->room, PHASES_NONE, 2.0);
		b.set_spill(tray.cell);
		impl->items.push_back(tray);
	} catch (const std::exception &e) {
		why = std::string("the bench is malformed: ") + e.what();
		return nullptr;
	}
	if (!ok) {
		why = "an amount on the bench does not fit the integer range";
		return nullptr;
	}
	std::optional<World> world = b.build(&why);
	if (!world) {
		why = "the bench cannot be built: " + why;
		return nullptr;
	}
	impl->sim.emplace(std::move(*world), std::vector<Reaction>(builtin_reactions().begin(), builtin_reactions().end()));
	impl->sim->set_checking(false); // the lab checks, and reports through its own status
	impl->genesis = totals(table, impl->world().snapshot());
	return std::unique_ptr<Lab>(new Lab(std::move(impl)));
}

bool Lab::Impl::run(const json &c, std::string &why) {
	if (!c.is_object() || !c.contains("do") || !c.at("do").is_string()) {
		why = "a command is an object with a \"do\"";
		return false;
	}
	const std::string what = c.at("do").get<std::string>();

	if (what == "wait") {
		const auto steps = static_cast<int64_t>(number(c, "seconds", 0.0) * 60.0 + 0.5);
		for (int64_t i = 0; i < steps; ++i) {
			if (!step_once(why)) {
				return false;
			}
		}
		return true;
	}
	if (what == "wait_until" && c.contains("rings")) {
		// until an alarm rings (§13.3)
		int at = -1;
		if (!part(c, "rings", PartKind::Alarm, at, why)) {
			return false;
		}
		const std::string alarm = parts[static_cast<size_t>(at)].id;
		const auto limit = static_cast<int64_t>(number(c, "timeout", 3600.0) * 60.0 + 0.5);
		for (int64_t i = 0;; ++i) {
			const int now = find_part(alarm);
			if (now >= 0 && parts[static_cast<size_t>(now)].ringing) {
				return true;
			}
			if (i >= limit) {
				why = "the alarm did not ring before the wait's timeout";
				return false;
			}
			if (!step_once(why)) {
				return false;
			}
		}
	}
	if (what == "wait_until") {
		int at = -1;
		if (!item(c, "item", Kind::Vessel, at, why)) {
			return false;
		}
		const CellId cell = items[static_cast<size_t>(at)].cell;
		SpeciesId species;
		if (c.contains("species")) {
			species = table().find(c.at("species").get<std::string>());
			if (!table().has(species)) {
				why = "there is no species \"" + c.at("species").get<std::string>() + "\"";
				return false;
			}
		}
		const auto reached = [&]() {
			if (c.contains("below_g")) {
				return grams(cell, species) < c.at("below_g").get<double>();
			}
			if (c.contains("above_g")) {
				return grams(cell, species) > c.at("above_g").get<double>();
			}
			if (c.contains("below_c")) {
				return kelvin(cell) - 273.15 < c.at("below_c").get<double>();
			}
			if (c.contains("above_c")) {
				return kelvin(cell) - 273.15 > c.at("above_c").get<double>();
			}
			return true;
		};
		if ((c.contains("below_g") || c.contains("above_g")) && !table().has(species)) {
			why = "a wait for an amount needs a \"species\"";
			return false;
		}
		const auto limit = static_cast<int64_t>(number(c, "timeout", 3600.0) * 60.0 + 0.5);
		for (int64_t i = 0; !reached(); ++i) {
			if (i >= limit) {
				why = "the wait's condition was not reached before its timeout";
				return false;
			}
			if (!step_once(why)) {
				return false;
			}
		}
		return true;
	}
	if (what == "place") {
		// from the cabinet onto the bench, or along it
		int thing = -1;
		if (c.contains("item") && c.at("item").is_string() && find(c.at("item").get<std::string>()) < 0 && find_part(c.at("item").get<std::string>()) >= 0) {
			// a signal part, along the bench
			Part &p = parts[static_cast<size_t>(find_part(c.at("item").get<std::string>()))];
			if (!c.contains("at") || !c.at("at").is_array() || c.at("at").size() != 2) {
				why = "a place is \"at\" [column, row]";
				return false;
			}
			const int col = c.at("at").at(0).get<int>(), row = c.at("at").at(1).get<int>();
			if (!square_free(p.in, col, row, find_part(p.id), why)) { // within the circuit it is in
				return false;
			}
			p.col = col;
			p.row = row;
			return true;
		}
		if (!c.contains("item") || !c.at("item").is_string() || (thing = find(c.at("item").get<std::string>())) < 0) {
			why = "there is no such item to place";
			return false;
		}
		Item &x = items[static_cast<size_t>(thing)];
		if (x.kind == Kind::Tray || (x.place != Place::Cabinet && x.place != Place::Bench) || x.holds >= 0 || x.drains_to >= 0) {
			why = "\"" + x.id + "\" cannot be moved as it is: it is joined to something, or handed in";
			return false;
		}
		for (const Item &other : items) {
			if (other.under == thing || other.drains_to == thing) {
				why = "\"" + x.id + "\" cannot be moved as it is: \"" + other.id + "\" depends on where it stands";
				return false;
			}
		}
		if (!c.contains("at") || !c.at("at").is_array() || c.at("at").size() != 2) {
			why = "a place is \"at\" [column, row]";
			return false;
		}
		const int col = c.at("at").at(0).get<int>(), row = c.at("at").at(1).get<int>();
		if (!on_grid(col, row, x.w, x.h)) {
			why = "that is off the bench";
			return false;
		}
		const int there = standing_in(col, row, x.w, x.h, thing);
		if (there >= 0) {
			why = "\"" + name_of(there) + "\" stands there";
			return false;
		}
		x.place = Place::Bench;
		x.col = col;
		x.row = row;
		return true;
	}
	if (what == "move") {
		// several things along the bench together, by the same offset: all of them or none (GAME_SPEC §13);
		// equipment and signal parts alike, numbered as standing_in numbers them
		if (!c.contains("items") || !c.at("items").is_array() || c.at("items").empty() || !c.contains("by") || !c.at("by").is_array() || c.at("by").size() != 2) {
			why = "a move is {\"items\": [...], \"by\": [columns, rows]}";
			return false;
		}
		const int dc = c.at("by").at(0).get<int>(), dr = c.at("by").at(1).get<int>();
		const int n = static_cast<int>(items.size());
		std::vector<int> moving;
		for (const json &name : c.at("items")) {
			int thing = name.is_string() ? find(name.get<std::string>()) : -1;
			if (thing < 0 && name.is_string() && find_part(name.get<std::string>()) >= 0) {
				thing = n + find_part(name.get<std::string>());
				if (!parts[static_cast<size_t>(thing - n)].in.empty()) {
					why = "\"" + name.get<std::string>() + "\" is in a circuit: it moves with it";
					return false;
				}
			}
			if (thing < 0) {
				why = "there is no such item to move: " + name.dump();
				return false;
			}
			if (std::find(moving.begin(), moving.end(), thing) != moving.end()) {
				why = "\"" + name_of(thing) + "\" is named twice";
				return false;
			}
			moving.push_back(thing);
		}
		const auto footprint = [&](int thing, int &col, int &row, int &w, int &h) {
			if (thing >= n) {
				const Part &p = parts[static_cast<size_t>(thing - n)];
				col = p.col;
				row = p.row;
				w = h = 1;
			} else {
				const Item &x = items[static_cast<size_t>(thing)];
				col = x.col;
				row = x.row;
				w = x.w;
				h = x.h;
			}
		};
		for (const int thing : moving) {
			if (thing < n) {
				const Item &x = items[static_cast<size_t>(thing)];
				if (x.kind == Kind::Tray || x.place != Place::Bench || x.holds >= 0 || x.drains_to >= 0) {
					why = "\"" + x.id + "\" cannot be moved as it is: it is not on the bench, or it is joined to something";
					return false;
				}
				for (const Item &other : items) {
					if (other.under == thing || other.drains_to == thing) {
						why = "\"" + x.id + "\" cannot be moved as it is: \"" + other.id + "\" depends on where it stands";
						return false;
					}
				}
			}
			int col = 0, row = 0, w = 0, h = 0;
			footprint(thing, col, row, w, h);
			if (!on_grid(col + dc, row + dr, w, h)) {
				why = "\"" + name_of(thing) + "\" would be off the bench";
				return false;
			}
		}
		for (const int thing : moving) {
			int col = 0, row = 0, w = 0, h = 0;
			footprint(thing, col, row, w, h);
			for (int other = 0; other < n + static_cast<int>(parts.size()); ++other) {
				if (std::find(moving.begin(), moving.end(), other) != moving.end() || (other < n && (items[static_cast<size_t>(other)].place != Place::Bench || items[static_cast<size_t>(other)].col < 0))) {
					continue;
				}
				int oc = 0, orow = 0, ow = 0, oh = 0;
				footprint(other, oc, orow, ow, oh);
				if (col + dc < oc + ow && oc < col + dc + w && row + dr < orow + oh && orow < row + dr + h) {
					why = "\"" + name_of(other) + "\" stands where \"" + name_of(thing) + "\" would go";
					return false;
				}
			}
		}
		for (const int thing : moving) {
			if (thing >= n) {
				parts[static_cast<size_t>(thing - n)].col += dc;
				parts[static_cast<size_t>(thing - n)].row += dr;
			} else {
				items[static_cast<size_t>(thing)].col += dc;
				items[static_cast<size_t>(thing)].row += dr;
			}
		}
		return true;
	}
	// ---- signals (§13.3)
	if (what == "part") {
		PartKind kind = PartKind::Timer;
		if (!c.contains("kind") || !c.at("kind").is_string() || !part_kind_of(c.at("kind").get<std::string>(), kind) || kind == PartKind::Circuit) {
			why = "a part's \"kind\" is timer, monitor or alarm (a circuit is made of parts: \"circuit\")";
			return false;
		}
		if (!c.contains("at") || !c.at("at").is_array() || c.at("at").size() != 2) {
			why = "a part is made \"at\" [column, row]";
			return false;
		}
		const int col = c.at("at").at(0).get<int>(), row = c.at("at").at(1).get<int>();
		const std::string in = c.value("in", std::string());
		if (!square_free(in, col, row, -1, why)) {
			return false;
		}
		Part p;
		p.kind = kind;
		p.col = col;
		p.row = row;
		p.in = in;
		int64_t &count = made[static_cast<int>(kind)];
		p.id = c.value("id", std::string());
		if (p.id.empty()) {
			do {
				p.id = std::string(part_kind_name(kind)) + std::to_string(++count);
			} while (find(p.id) >= 0 || find_part(p.id) >= 0);
		} else if (find(p.id) >= 0 || find_part(p.id) >= 0) {
			why = "\"" + p.id + "\" is taken";
			return false;
		}
		parts.push_back(p);
		return true;
	}
	if (what == "scrap") {
		int at = -1;
		if (!part(c, "part", std::nullopt, at, why)) {
			return false;
		}
		for (const std::string &id : held_by(parts[static_cast<size_t>(at)].id)) { // a circuit goes with all it holds
			std::erase_if(wires, [&](const Wire &w) { return w.from == id || w.to == id; });
			std::erase_if(rigs, [&](const Rig &r) { return r.from == id || (r.control == "reset" && r.item == id); });
			std::erase_if(controls, [&](const ControlSetting &s) { return s.item == id; });
			std::erase_if(parts, [&](const Part &p) { return p.id == id; });
		}
		return true;
	}
	if (what == "set") {
		int at = -1;
		if (!part(c, "part", std::nullopt, at, why)) {
			return false;
		}
		Part &p = parts[static_cast<size_t>(at)];
		if (p.kind == PartKind::Timer) {
			const double seconds = number(c, "seconds", -1.0);
			if (!(seconds > 0.0) || seconds > 360000.0) {
				why = "a timer is set to \"seconds\", more than none and at most 100 hours";
				return false;
			}
			p.preset = std::max<int64_t>(1, static_cast<int64_t>(seconds * 60.0 + 0.5));
			reset(p);
			return true;
		}
		if (p.kind != PartKind::Monitor) {
			why = std::string("a") + (p.kind == PartKind::Alarm ? "n alarm" : " circuit") + " has nothing to set";
			return false;
		}
		Quantity q;
		q.item = c.value("item", std::string());
		q.what = c.value("what", std::string());
		q.measure = c.value("measure", std::string("g"));
		if (find(q.item) < 0 || items[static_cast<size_t>(find(q.item))].kind == Kind::Tray) {
			why = "a monitor watches an item on the bench: there is no \"" + q.item + "\"";
			return false;
		}
		if (!valid(q, why)) {
			return false;
		}
		Comparison cmp;
		if (!c.contains("compare") || !c.at("compare").is_string() || !op_of(c.at("compare").get<std::string>(), cmp.op)) {
			why = "a monitor compares: \"compare\" is above, below, at_least, at_most, reaches, between or outside";
			return false;
		}
		if (two_values(cmp.op)) {
			if (!c.contains("values") || !c.at("values").is_array() || c.at("values").size() != 2 || !c.at("values")[0].is_number() || !c.at("values")[1].is_number()) {
				why = "\"" + c.at("compare").get<std::string>() + "\" compares with \"values\": [low, high]";
				return false;
			}
			cmp.a = c.at("values")[0].get<double>();
			cmp.b = c.at("values")[1].get<double>();
		} else {
			if (!c.contains("value") || !c.at("value").is_number()) {
				why = "\"" + c.at("compare").get<std::string>() + "\" compares with one \"value\"";
				return false;
			}
			cmp.a = c.at("value").get<double>();
		}
		if (!valid(cmp, why)) {
			return false;
		}
		p.watching = true;
		p.quantity = q;
		p.comparison = cmp;
		p.last.reset(); // "reaches" starts again
		return true;
	}
	if (what == "wire" || what == "unwire") {
		int from = -1, to = -1;
		if (!part(c, "from", std::nullopt, from, why) || !part(c, "to", std::nullopt, to, why)) {
			return false;
		}
		const Wire w{ parts[static_cast<size_t>(from)].id, parts[static_cast<size_t>(to)].id };
		const auto there = std::find(wires.begin(), wires.end(), w);
		if (what == "unwire") {
			if (there == wires.end()) {
				why = "no wire runs from \"" + w.from + "\" to \"" + w.to + "\"";
				return false;
			}
			wires.erase(there);
			return true;
		}
		if (parts[static_cast<size_t>(from)].kind == PartKind::Alarm) {
			why = "an alarm sends no signal: it has no output to wire";
			return false;
		}
		if (parts[static_cast<size_t>(from)].kind == PartKind::Circuit || parts[static_cast<size_t>(to)].kind == PartKind::Circuit) {
			why = "a circuit is wired by the parts in it: open it";
			return false;
		}
		if (there != wires.end()) {
			why = "a wire already runs from \"" + w.from + "\" to \"" + w.to + "\"";
			return false;
		}
		wires.push_back(w);
		return true;
	}
	if (what == "rig" || what == "unrig") {
		int from = -1;
		if (!part(c, "from", std::nullopt, from, why)) {
			return false;
		}
		Rig r;
		r.from = parts[static_cast<size_t>(from)].id;
		r.item = c.value("item", std::string());
		r.control = c.value("control", std::string());
		const auto same = [&](const Rig &o) { return o.from == r.from && o.item == r.item && o.control == r.control; };
		const auto there = std::find_if(rigs.begin(), rigs.end(), same);
		if (what == "unrig") {
			if (there == rigs.end()) {
				why = "no rig runs from \"" + r.from + "\" to the " + r.control + " of \"" + r.item + "\"";
				return false;
			}
			rigs.erase(there);
			return true;
		}
		if (parts[static_cast<size_t>(from)].kind == PartKind::Alarm || parts[static_cast<size_t>(from)].kind == PartKind::Circuit) {
			why = parts[static_cast<size_t>(from)].kind == PartKind::Alarm ? "an alarm sends no signal: it has no output to rig" : "a circuit is rigged by the parts in it: open it";
			return false;
		}
		const bool targets = r.control == "reset" ? find_part(r.item) >= 0 && parts[static_cast<size_t>(find_part(r.item))].kind == PartKind::Timer
								: control_now(r.item, r.control).has_value();
		if (!targets) {
			why = "there is no " + r.control + " on \"" + r.item + "\" to rig: a vessel's stir, a burner's valve, a timer's reset";
			return false;
		}
		if (there != rigs.end()) {
			why = "a rig already runs from \"" + r.from + "\" to the " + r.control + " of \"" + r.item + "\"";
			return false;
		}
		r.change = c.contains("change");
		if (r.change && (c.contains("on") || c.contains("off"))) {
			why = "a rig acts on its signal's \"change\", or on its turning \"on\" and \"off\": not both";
			return false;
		}
		if ((r.change && !setting_of(r.control, c.at("change"), r.any, why)) || (c.contains("on") && !setting_of(r.control, c.at("on"), r.on, why)) ||
				(c.contains("off") && !setting_of(r.control, c.at("off"), r.off, why))) {
			return false;
		}
		r.seen = parts[static_cast<size_t>(from)].out; // it acts from the source's next change
		rigs.push_back(r);
		return true;
	}
	if (what == "circuit") {
		// signal parts on the bench, circuits too, into a new circuit on a free square (§13.3)
		if (!c.contains("parts") || !c.at("parts").is_array() || c.at("parts").empty()) {
			why = "a circuit is made of \"parts\": [...]";
			return false;
		}
		std::vector<int> into;
		int left = 1 << 30, top = 1 << 30, right = -1, bottom = -1;
		for (const json &name : c.at("parts")) {
			const int at = name.is_string() ? find_part(name.get<std::string>()) : -1;
			if (at < 0 || !parts[static_cast<size_t>(at)].in.empty() || std::find(into.begin(), into.end(), at) != into.end()) {
				why = "a circuit is made of signal parts on the bench, each once: " + name.dump();
				return false;
			}
			const Part &p = parts[static_cast<size_t>(at)];
			into.push_back(at);
			left = std::min(left, p.col);
			top = std::min(top, p.row);
			right = std::max(right, p.col);
			bottom = std::max(bottom, p.row);
		}
		Part k;
		k.kind = PartKind::Circuit;
		k.inner_w = std::max(right - left + 1, 6);
		k.inner_h = std::max(bottom - top + 1, 4);
		k.id = c.value("id", std::string());
		if (k.id.empty()) {
			do {
				k.id = "circuit" + std::to_string(++made[static_cast<int>(PartKind::Circuit)]);
			} while (find(k.id) >= 0 || find_part(k.id) >= 0);
		} else if (find(k.id) >= 0 || find_part(k.id) >= 0) {
			why = "\"" + k.id + "\" is taken";
			return false;
		}
		// the parts leave the bench, keeping their layout inside; the circuit takes the free
		// square nearest their top-left: rows by distance from it, then columns, the left first
		for (const int at : into) {
			Part &p = parts[static_cast<size_t>(at)];
			p.in = k.id;
			p.col -= left;
			p.row -= top;
		}
		bool placed = false;
		for (int dr = 0; dr <= rows && !placed; ++dr) {
			for (const int row : { top - dr, top + dr }) {
				for (int dc = 0; dc <= cols && !placed; ++dc) {
					for (const int col : { left - dc, left + dc }) {
						if (!placed && on_grid(col, row, 1, 1) && standing_in(col, row, 1, 1) < 0) {
							k.col = col;
							k.row = row;
							placed = true;
						}
					}
				}
			}
		}
		if (!placed) {
			for (const int at : into) { // as they were
				Part &p = parts[static_cast<size_t>(at)];
				p.in.clear();
				p.col += left;
				p.row += top;
			}
			why = "there is no free square on the bench for the circuit";
			return false;
		}
		parts.push_back(k);
		return true;
	}
	if (what == "uncircuit") {
		// a circuit's parts back on the bench, as they lie in it, from its square or the nearest place they fit
		int at = -1;
		if (!part(c, "circuit", PartKind::Circuit, at, why)) {
			return false;
		}
		const Part k = parts[static_cast<size_t>(at)];
		if (!k.in.empty()) {
			why = "\"" + k.id + "\" is in a circuit: open that one first";
			return false;
		}
		std::vector<size_t> held;
		for (size_t i = 0; i < parts.size(); ++i) {
			if (parts[i].in == k.id) {
				held.push_back(i);
			}
		}
		const int self = static_cast<int>(items.size()) + at;
		const auto fits = [&](int col, int row) {
			for (const size_t i : held) {
				const int x = col + parts[i].col, y = row + parts[i].row;
				const int there = standing_in(x, y, 1, 1, self);
				if (!on_grid(x, y, 1, 1) || there >= 0) {
					return false;
				}
			}
			return true;
		};
		for (int dr = 0; dr <= rows; ++dr) {
			for (const int row : { k.row - dr, k.row + dr }) {
				for (int dc = 0; dc <= cols; ++dc) {
					for (const int col : { k.col - dc, k.col + dc }) {
						if (fits(col, row)) {
							for (const size_t i : held) {
								parts[i].in.clear();
								parts[i].col += col;
								parts[i].row += row;
							}
							parts.erase(parts.begin() + at);
							return true;
						}
					}
				}
			}
		}
		why = "there is no room on the bench for what \"" + k.id + "\" holds";
		return false;
	}
	if (what == "reset") {
		int at = -1;
		if (!part(c, "timer", PartKind::Timer, at, why)) {
			return false;
		}
		set_control(parts[static_cast<size_t>(at)].id, "reset", 1.0, "player");
		return true;
	}
	if (what == "ack") {
		// the player answers ringing alarms: the one named, or all of them
		if (c.contains("alarm")) {
			int at = -1;
			if (!part(c, "alarm", PartKind::Alarm, at, why)) {
				return false;
			}
			parts[static_cast<size_t>(at)].ringing = false;
			return true;
		}
		for (Part &p : parts) {
			p.ringing = false;
		}
		return true;
	}
	if (what == "stow" && c.contains("item") && c.at("item").is_string() && find(c.at("item").get<std::string>()) < 0 && find_part(c.at("item").get<std::string>()) >= 0) {
		return run(json{ { "do", "scrap" }, { "part", c.at("item") } }, why); // a part back in the drawer
	}
	if (what == "stow") {
		int thing = -1;
		if (!c.contains("item") || !c.at("item").is_string() || (thing = find(c.at("item").get<std::string>())) < 0) {
			why = "there is no such item to stow";
			return false;
		}
		Item &x = items[static_cast<size_t>(thing)];
		bool free = x.kind != Kind::Tray && x.place == Place::Bench && x.holds < 0 && x.drains_to < 0;
		for (const Item &other : items) {
			free = free && other.under != thing && other.drains_to != thing;
		}
		if (!free) {
			why = "\"" + x.id + "\" cannot go back in the cabinet as it is";
			return false;
		}
		x.place = Place::Cabinet;
		x.col = x.row = -1;
		return true;
	}
	if (what == "heat") {
		int burner = -1, vessel = -1;
		if (!item(c, "burner", Kind::Burner, burner, why) || !item(c, "vessel", Kind::Vessel, vessel, why)) {
			return false;
		}
		Item &flame = items[static_cast<size_t>(burner)];
		if (!on_bench(items[static_cast<size_t>(vessel)], why)) {
			return false;
		}
		if (flame.under >= 0) {
			why = "\"" + flame.id + "\" is already under \"" + items[static_cast<size_t>(flame.under)].id + "\"";
			return false;
		}
		if (world().connect(items[static_cast<size_t>(vessel)].cell, flame.cell, PHASES_NONE, &flame.heats, flame.full * flame.valve) != Refusal::None) {
			why = "the burner cannot go there";
			return false;
		}
		flame.under = vessel;
		flame.place = Place::Under;
		return true;
	}
	if (what == "unheat") {
		int burner = -1;
		if (!item(c, "burner", Kind::Burner, burner, why)) {
			return false;
		}
		Item &flame = items[static_cast<size_t>(burner)];
		if (flame.under < 0) {
			why = "\"" + flame.id + "\" is not under anything";
			return false;
		}
		if (!boundary(why)) {
			return false;
		}
		world().disconnect(flame.heats);
		// it goes back beside the vessel if there is room, else to the cabinet
		const Item v = items[static_cast<size_t>(flame.under)];
		flame.place = Place::Cabinet;
		flame.col = flame.row = -1;
		set_beside(burner, v);
		flame.under = -1;
		flame.heats = EdgeId{};
		return true;
	}
	if (what == "valve") {
		int burner = -1;
		if (!item(c, "burner", Kind::Burner, burner, why)) {
			return false;
		}
		const double to = number(c, "to", -1.0);
		if (!(to >= 0.0 && to <= 1.0)) {
			why = "a valve is set \"to\" between 0 and 1";
			return false;
		}
		Item &flame = items[static_cast<size_t>(burner)];
		if (flame.under >= 0) {
			if (!boundary(why)) {
				return false;
			}
			world().set_conductance(flame.heats, flame.full * to);
		}
		flame.valve = to;
		record(flame.id, "valve", to, "player");
		return true;
	}
	if (what == "stir") {
		int at = -1;
		if (!item(c, "vessel", Kind::Vessel, at, why)) {
			return false;
		}
		const bool on = c.value("on", true);
		if (!on_bench(items[static_cast<size_t>(at)], why) || !boundary(why)) {
			return false;
		}
		world().set_mixing(items[static_cast<size_t>(at)].cell, on ? STIRRED : 1.0);
		items[static_cast<size_t>(at)].stirred = on;
		record(items[static_cast<size_t>(at)].id, "stir", on ? 1.0 : 0.0, "player");
		return true;
	}
	if (what == "pour" || what == "tip") {
		int from = -1, to = -1;
		if (!item(c, "from", Kind::Vessel, from, why) || !item(c, "to", Kind::Vessel, to, why)) {
			return false;
		}
		if (from == to) {
			why = "a vessel is not poured into itself";
			return false;
		}
		if (items[static_cast<size_t>(from)].place == Place::Outbox || !on_bench(items[static_cast<size_t>(to)], why)) {
			if (why.empty()) {
				why = "\"" + items[static_cast<size_t>(from)].id + "\" has been handed in";
			}
			return false;
		}
		const CellState &source = world().snapshot_cell(items[static_cast<size_t>(from)].cell);
		std::vector<SpeciesAmount> amounts;
		if (what == "pour") {
			// the liquid, with what is dissolved in it: a volume of it, or all
			double volume = 0.0;
			for (size_t s = 0; s < source.amount.size(); ++s) {
				const SpeciesDef &def = table().species(SpeciesId{ static_cast<int32_t>(s) });
				if (source.amount[s] > 0 && def.phase == Phase::Liquid) {
					volume += static_cast<double>(source.amount[s]) * MOL_PER_QUANTUM * def.molar_volume;
				}
			}
			double fraction = 1.0;
			if (!c.value("all", false)) {
				const double wanted = number(c, "ml", -1.0) * 1e-6;
				if (!(wanted > 0.0)) {
					why = "a pour is of \"ml\" or \"all\"";
					return false;
				}
				if (wanted > volume) {
					why = "\"" + items[static_cast<size_t>(from)].id + "\" does not hold that much liquid";
					return false;
				}
				fraction = wanted / volume;
			}
			for (size_t s = 0; s < source.amount.size(); ++s) {
				const SpeciesId id{ static_cast<int32_t>(s) };
				const SpeciesDef &def = table().species(id);
				if (source.amount[s] > 0 && (def.phase == Phase::Liquid || def.phase == Phase::Aqueous)) {
					const int64_t moves = fraction >= 1.0 ? source.amount[s] : static_cast<int64_t>(static_cast<double>(source.amount[s]) * fraction);
					if (moves > 0) {
						amounts.push_back(SpeciesAmount{ id, Amount{ moves } });
					}
				}
			}
		} else {
			// one solid: a mass of it, or all
			if (!c.contains("species") || !c.at("species").is_string()) {
				why = "a tip names the \"species\"";
				return false;
			}
			const SpeciesId id = table().find(c.at("species").get<std::string>());
			if (!table().has(id) || table().species(id).phase != Phase::Solid || table().species(id).wall) {
				why = "\"" + c.at("species").get<std::string>() + "\" is not a solid that can be tipped";
				return false;
			}
			const int64_t have = source.amount[static_cast<size_t>(id.index)];
			int64_t moves = have;
			if (!c.value("all", false)) {
				const double g = number(c, "g", -1.0);
				if (!(g > 0.0)) {
					why = "a tip is of \"g\" or \"all\"";
					return false;
				}
				bool fits = false;
				moves = amount_from_mol(g / (static_cast<double>(table().molar_mass_ug(id)) * 1e-6), fits).quanta;
				if (!fits || moves > have) {
					why = "\"" + items[static_cast<size_t>(from)].id + "\" does not hold that much of it";
					return false;
				}
			}
			if (moves > 0) {
				amounts.push_back(SpeciesAmount{ id, Amount{ moves } });
			}
		}
		return move(items[static_cast<size_t>(from)], items[static_cast<size_t>(to)], amounts, why);
	}
	if (what == "attach") {
		int thing = -1, to = -1;
		if (!item(c, "item", Kind::Vessel, thing, why) || !item(c, "to", Kind::Vessel, to, why)) {
			return false;
		}
		Item &x = items[static_cast<size_t>(thing)];
		Item &v = items[static_cast<size_t>(to)];
		if (thing == to || v.kind != Kind::Vessel) {
			why = "something is attached to a vessel's mouth, not to itself or to a balloon";
			return false;
		}
		if (x.on >= 0) {
			why = "\"" + x.id + "\" is already on \"" + items[static_cast<size_t>(x.on)].id + "\"";
			return false;
		}
		if (x.burst) {
			why = "\"" + x.id + "\" has burst";
			return false;
		}
		if (x.place == Place::Outbox || !on_bench(v, why)) {
			if (why.empty()) {
				why = "\"" + x.id + "\" has been handed in";
			}
			return false;
		}
		if (v.holds >= 0 || v.mouth.index < 0) {
			why = "the mouth of \"" + v.id + "\" is taken";
			return false;
		}
		if (!boundary(why)) {
			return false;
		}
		EdgeId joint;
		world().connect(x.cell, v.cell, PHASES_GAS, &joint);
		if (world().disconnect(v.mouth) != Refusal::None) {
			world().disconnect(joint);
			why = "that would seal \"" + v.id + "\": \"" + x.id + "\" has no way out for its gas";
			return false;
		}
		x.on = to;
		x.joint = joint;
		x.place = Place::On;
		x.col = x.row = -1;
		v.holds = thing;
		v.mouth = EdgeId{};
		return true;
	}
	if (what == "detach") {
		int thing = -1;
		if (!item(c, "item", Kind::Vessel, thing, why)) {
			return false;
		}
		Item &x = items[static_cast<size_t>(thing)];
		if (x.on < 0) {
			why = "\"" + x.id + "\" is not on anything";
			return false;
		}
		Item &v = items[static_cast<size_t>(x.on)];
		if (!boundary(why)) {
			return false;
		}
		EdgeId mouth;
		world().connect(v.cell, air, PHASES_GAS, &mouth);
		if (world().disconnect(x.joint) != Refusal::None) {
			world().disconnect(mouth);
			why = "that would seal \"" + x.id + "\"";
			return false;
		}
		v.mouth = mouth;
		v.holds = -1;
		x.on = -1;
		x.joint = EdgeId{};
		// it is set down beside the vessel if there is room, else in the cabinet
		x.place = Place::Cabinet;
		x.col = x.row = -1;
		set_beside(thing, v);
		return true;
	}
	if (what == "under") {
		int thing = -1, of = -1;
		if (!item(c, "item", Kind::Vessel, thing, why) || !item(c, "of", Kind::Vessel, of, why)) {
			return false;
		}
		Item &upper = items[static_cast<size_t>(of)];
		if (!on_bench(items[static_cast<size_t>(thing)], why) || !on_bench(upper, why)) {
			return false;
		}
		if (thing == of || upper.drains_to >= 0) {
			why = "\"" + upper.id + "\" already has something under its outlet, or it is the same vessel";
			return false;
		}
		if (world().connect_drain(upper.cell, items[static_cast<size_t>(thing)].cell) != Refusal::None) {
			why = "it cannot stand there";
			return false;
		}
		upper.drains_to = thing;
		return true;
	}
	if (what == "submit") {
		int thing = -1;
		if (!item(c, "item", Kind::Vessel, thing, why)) {
			return false;
		}
		Item &x = items[static_cast<size_t>(thing)];
		if (x.on >= 0 || x.holds >= 0) {
			why = "\"" + x.id + "\" is still joined to something";
			return false;
		}
		if (x.place == Place::Cabinet) {
			why = "\"" + x.id + "\" is in the cabinet";
			return false;
		}
		// lifted off its burner, which stays where the vessel stood
		for (Item &other : items) {
			if (other.under == thing) {
				if (!boundary(why)) {
					return false;
				}
				world().disconnect(other.heats);
				other.under = -1;
				other.heats = EdgeId{};
			}
		}
		const Item was = x;
		x.submitted = true;
		x.place = Place::Outbox;
		x.col = x.row = -1;
		for (size_t i = 0; i < items.size(); ++i) {
			if (items[i].kind == Kind::Burner && items[i].place == Place::Under && items[i].under < 0) {
				items[i].place = Place::Cabinet;
				set_beside(static_cast<int>(i), was); // where the vessel stood, or beside it
			}
		}
		return true;
	}
	why = "there is no command \"" + what + "\"";
	return false;
}

bool Lab::command(std::string_view command_json, std::string &why) {
	if (impl_->broken) {
		why = "an invariant failed earlier: the lab takes no more commands";
		return false;
	}
	json c;
	try {
		c = json::parse(command_json, nullptr, true, true);
	} catch (const std::exception &e) {
		why = std::string("the command is not JSON: ") + e.what();
		return false;
	}
	try {
		return impl_->run(c, why);
	} catch (const std::exception &e) {
		why = std::string("the command is malformed: ") + e.what();
		return false;
	}
}

bool Lab::advance(int64_t steps, std::string &why, bool stop_on_ring) {
	if (impl_->broken) {
		why = "an invariant failed earlier: the lab takes no more commands";
		return false;
	}
	for (int64_t i = 0; i < steps; ++i) {
		if (!impl_->step_once(why)) {
			return false;
		}
		if (stop_on_ring && impl_->rang) {
			break;
		}
	}
	return true;
}

void Lab::set_checking(bool on) {
	impl_->check = on;
}

bool Lab::failed() const {
	return impl_->broken;
}

bool Lab::rang() const {
	return impl_->rang;
}

int64_t Lab::step() const {
	return static_cast<int64_t>(impl_->world().step());
}

LabView Lab::view() const {
	const Impl &m = *impl_;
	const SpeciesTable &table = m.table();
	LabView out;
	out.step = step();
	out.cols = m.cols;
	out.rows = m.rows;
	for (const Item &item : m.items) {
		out.items.push_back(m.item_view(item));
	}
	for (const Part &p : m.parts) {
		ItemView v;
		v.id = p.id;
		v.kind = part_kind_name(p.kind);
		v.place = p.in.empty() ? "bench" : "circuit";
		v.col = p.col;
		v.row = p.row;
		v.part = p;
		out.items.push_back(v);
		if (p.ringing) {
			out.ringing.push_back(p.id);
		}
	}
	out.wires = m.wires;
	out.rigs = m.rigs;
	out.controls = m.controls;
	const Totals now = totals(table, m.world().snapshot());
	out.conserved = now == m.genesis;
	out.mass_g = now.mass.grams();
	out.energy_j = static_cast<double>(now.energy) * JOULE_PER_QUANTUM;
	for (const Event &e : m.events) {
		char when[48];
		std::snprintf(when, sizeof when, " at %.1f s", static_cast<double>(e.step) * STEP_SECONDS);
		out.events.push_back("the " + e.item + (e.what == "burst" ? " burst" : " " + e.what) + when);
	}
	return out;
}

std::string Lab::report() const {
	const Impl &m = *impl_;
	const SpeciesTable &table = m.table();
	static const char *const PHASE[] = { "s", "l", "g", "aq" };
	json out;
	out["step"] = step();
	out["seconds"] = static_cast<double>(step()) * STEP_SECONDS;
	json items = json::array();
	for (const Item &item : m.items) {
		json j;
		j["id"] = item.id;
		if (item.kind == Kind::Burner) {
			j["kind"] = "burner";
			j["place"] = PLACE_NAME[static_cast<int>(item.place)];
			if (item.place == Place::Bench) {
				j["at"] = { item.col, item.row };
			}
			j["valve"] = item.valve;
			j["under"] = item.under >= 0 ? json(m.items[static_cast<size_t>(item.under)].id) : json(nullptr);
			j["given_j"] = 0.0 - m.world().energy(item.cell).joule();
			items.push_back(j);
			continue;
		}
		j["kind"] = KIND_NAME[static_cast<int>(item.kind)];
		j["place"] = PLACE_NAME[static_cast<int>(item.place)];
		if (item.place == Place::Bench && item.kind != Kind::Tray) {
			j["at"] = { item.col, item.row };
		}
		if (item.kind == Kind::Bag) {
			j["burst"] = item.burst;
			j["capacity_ml"] = item.capacity * 1e6;
		}
		j["submitted"] = item.submitted;
		j["stirred"] = item.stirred;
		j["on"] = item.on >= 0 ? json(m.items[static_cast<size_t>(item.on)].id) : json(nullptr);
		const double kelvin = m.kelvin(item.cell);
		j["celsius"] = kelvin - 273.15;
		const CellState &cell = m.world().snapshot_cell(item.cell);
		const Fill fill = fill_of(table, cell, kelvin);
		j["gas_ml"] = fill.gas * 1e6;
		j["condensed_ml"] = fill.condensed * 1e6;
		json contents = json::array();
		for (size_t s = 0; s < cell.amount.size(); ++s) {
			if (cell.amount[s] == 0) {
				continue;
			}
			const SpeciesId id{ static_cast<int32_t>(s) };
			const SpeciesDef &def = table.species(id);
			json row;
			row["species"] = std::string(def.id);
			row["formula"] = std::string(def.formula);
			row["phase"] = PHASE[static_cast<int>(def.phase)];
			row["wall"] = def.wall;
			row["mol"] = static_cast<double>(cell.amount[s]) * MOL_PER_QUANTUM;
			row["g"] = m.grams(item.cell, id);
			if (def.phase == Phase::Solid && !def.wall) {
				row["particles"] = cell.particles[s];
				row["size_um"] = particle_diameter(table, cell, id) * 1e6;
			}
			contents.push_back(row);
		}
		j["contents"] = contents;
		items.push_back(j);
	}
	out["items"] = items;
	out["grid"] = { m.cols, m.rows };
	json events = json::array();
	for (const Event &e : m.events) {
		events.push_back({ { "step", e.step }, { "what", e.what }, { "item", e.item } });
	}
	out["events"] = events;
	out["signals"] = m.signals_json(false);
	// the conservation laws, as integers
	const Totals now = totals(table, m.world().snapshot());
	json conserved;
	conserved["unchanged"] = now == m.genesis;
	conserved["mass_g"] = now.mass.grams();
	conserved["energy_quanta"] = now.energy;
	json elements;
	for (int e = 0; e < table.element_count(); ++e) {
		elements[std::string(table.element(e).symbol)] = now.atoms[static_cast<size_t>(e)];
	}
	conserved["atom_quanta"] = elements;
	out["conserved"] = conserved;
	return out.dump();
}

std::string Lab::digest() const {
	const Impl &m = *impl_;
	const World &world = m.world();
	Sha256 hash;
	hash.update_u64(world.step());
	for (int32_t c = 0; c < world.cell_count(); ++c) {
		const CellState &cell = world.snapshot_cell(CellId{ c });
		for (const int64_t v : cell.amount) {
			hash.update_i64(v);
		}
		hash.update_i64(cell.energy);
		for (const int64_t v : cell.particles) {
			hash.update_i64(v);
		}
		hash.update_double(world.meta(CellId{ c }).mixing);
	}
	for (const Edge &e : world.edges()) {
		hash.update_i64(e.a.index);
		hash.update_i64(e.b.index);
		hash.update_u64(static_cast<uint64_t>(e.phases) | (e.open ? 0x100u : 0u) | (e.drain ? 0x200u : 0u));
		hash.update_double(e.conductance);
	}
	// the processes' carried remainders are state too (GAME_SPEC §4.5)
	for (const std::vector<double> *carried : { &m.sim->heat_remainders(), &m.sim->reaction_remainders(), &m.sim->nucleation_remainders() }) {
		hash.update_u64(carried->size());
		for (const double v : *carried) {
			hash.update_double(v);
		}
	}
	// the signals, once there is a part (§13.3): what they will do is state too
	if (!m.parts.empty()) {
		const auto text = [&hash](const std::string &t) {
			hash.update_u64(t.size());
			hash.update(t.data(), t.size());
		};
		const auto maybe = [&hash](const std::optional<double> &v) {
			hash.update_u64(v ? 1 : 0);
			hash.update_double(v ? *v : 0.0);
		};
		for (const Part &p : m.parts) {
			if (p.kind == PartKind::Circuit) {
				continue; // where parts are kept, as where things stand, is not the world
			}
			text(p.id);
			hash.update_i64(static_cast<int64_t>(p.kind));
			hash.update_u64((p.watching ? 1u : 0u) | (p.own ? 2u : 0u) | (p.out ? 4u : 0u) | (p.ringing ? 8u : 0u));
			text(p.quantity.item);
			text(p.quantity.what);
			text(p.quantity.measure);
			hash.update_i64(p.comparison.op);
			hash.update_double(p.comparison.a);
			hash.update_double(p.comparison.b);
			hash.update_i64(p.preset);
			hash.update_i64(p.left);
			maybe(p.last);
			hash.update_i64(p.rang_at);
		}
		for (const Wire &w : m.wires) {
			text(w.from);
			text(w.to);
		}
		for (const Rig &r : m.rigs) {
			text(r.from);
			text(r.item);
			text(r.control);
			hash.update_u64((r.change ? 1u : 0u) | (r.seen ? 2u : 0u));
			maybe(r.on);
			maybe(r.off);
			maybe(r.any);
		}
	}
	return hash.hex();
}

} // namespace lab

// ------------------------------------------------------------- save and load

namespace lab {

namespace {

json doubles(const std::vector<double> &values) {
	json out = json::array();
	for (const double v : values) {
		out.push_back(bits(v));
	}
	return out;
}

bool to_doubles(const json &j, std::vector<double> &out) {
	if (!j.is_array()) {
		return false;
	}
	out.clear();
	for (const json &entry : j) {
		double v = 0.0;
		if (!from_bits(entry, v)) {
			return false;
		}
		out.push_back(v);
	}
	return true;
}

} // namespace

json Lab::Impl::save() const {
	const World &w = world();
	json out;
	out["format"] = "chem-save/1";
	out["build"] = build_name();
	out["step"] = w.step();
	out["species"] = w.species().species_count(); // a save is for the data it was made with
	json cells = json::array();
	for (int32_t c = 0; c < w.cell_count(); ++c) {
		const CellId id{ c };
		const CellMeta &meta = w.meta(id);
		const CellState &state = w.snapshot_cell(id);
		json cell;
		cell["name"] = w.cell_name(id);
		cell["reservoir"] = meta.reservoir;
		cell["reservoir_temperature"] = bits(meta.reservoir_temperature);
		cell["rigid"] = meta.rigid;
		cell["volume"] = bits(meta.volume);
		cell["mixing"] = bits(meta.mixing);
		cell["amount"] = state.amount;
		cell["energy"] = state.energy;
		cell["particles"] = state.particles;
		cells.push_back(cell);
	}
	out["cells"] = cells;
	json edges = json::array();
	for (const Edge &e : w.edges()) {
		edges.push_back({ { "a", e.a.index }, { "b", e.b.index }, { "phases", e.phases }, { "open", e.open }, { "drain", e.drain }, { "conductance", bits(e.conductance) } });
	}
	out["edges"] = edges;
	out["remainders"] = { { "heat", doubles(sim->heat_remainders()) }, { "reaction", doubles(sim->reaction_remainders()) }, { "nucleation", doubles(sim->nucleation_remainders()) } };
	json bench;
	bench["grid"] = { cols, rows };
	bench["mid_step"] = mid_step;
	bench["air"] = air.index;
	bench["room"] = room.index;
	json list = json::array();
	for (const Item &item : items) {
		list.push_back({ { "id", item.id }, { "kind", static_cast<int>(item.kind) }, { "cell", item.cell.index }, { "mouth", item.mouth.index }, { "full", bits(item.full) },
				{ "heats", item.heats.index }, { "under", item.under }, { "valve", bits(item.valve) }, { "on", item.on }, { "joint", item.joint.index }, { "holds", item.holds },
				{ "drains_to", item.drains_to }, { "stirred", item.stirred }, { "submitted", item.submitted }, { "place", static_cast<int>(item.place) }, { "col", item.col },
				{ "row", item.row }, { "w", item.w }, { "h", item.h }, { "capacity", bits(item.capacity) }, { "burst", item.burst } });
	}
	bench["items"] = list;
	json happened = json::array();
	for (const Event &e : events) {
		happened.push_back({ { "step", e.step }, { "what", e.what }, { "item", e.item } });
	}
	bench["events"] = happened;
	bench["signals"] = signals_json(true);
	out["bench"] = bench;
	return out;
}

std::string Lab::save() const {
	return impl_->save().dump();
}

std::unique_ptr<Lab> Lab::load(std::string_view save_json, std::string &why) {
	json saved;
	try {
		saved = json::parse(save_json, nullptr, true, true);
	} catch (const std::exception &e) {
		why = std::string("the save is not JSON: ") + e.what();
		return nullptr;
	}
	if (!saved.is_object() || saved.value("format", "") != "chem-save/1") {
		why = "the save's \"format\" is not \"chem-save/1\"";
		return nullptr;
	}
	const SpeciesTable &table = builtin_species();
	auto impl = std::make_unique<Impl>();
	std::vector<double> heat, reaction, nucleation;
	std::optional<World> world;
	try {
		if (saved.at("species").get<int>() != table.species_count()) {
			why = "the save was made with other data: it counts a different number of species";
			return nullptr;
		}
		World::Builder b(table);
		const auto species = static_cast<size_t>(table.species_count());
		for (const json &cell : saved.at("cells")) {
			double reservoir_temperature = 0.0, volume = 0.0, mixing = 1.0;
			if (!from_bits(cell.at("reservoir_temperature"), reservoir_temperature) || !from_bits(cell.at("volume"), volume) || !from_bits(cell.at("mixing"), mixing)) {
				why = "the save holds a number that is not one";
				return nullptr;
			}
			const std::string name = cell.at("name").get<std::string>();
			const CellId id = cell.at("reservoir").get<bool>() ? b.add_reservoir(name, reservoir_temperature)
					: cell.at("rigid").get<bool>() ? b.add_rigid_cell(name, volume) : b.add_cell(name);
			const auto amount = cell.at("amount").get<std::vector<int64_t>>();
			const auto particles = cell.at("particles").get<std::vector<int64_t>>();
			if (amount.size() != species || particles.size() != species) {
				why = "the save's cells do not match the species table";
				return nullptr;
			}
			for (size_t s = 0; s < species; ++s) {
				const SpeciesId sp{ static_cast<int32_t>(s) };
				if (amount[s] != 0) {
					b.set_amount(id, sp, Amount{ amount[s] });
				}
				if (particles[s] != 0) {
					b.set_particles(id, sp, particles[s]);
				}
			}
			b.set_energy(id, Energy{ cell.at("energy").get<int64_t>() });
			b.set_mixing(id, mixing);
		}
		for (const json &e : saved.at("edges")) {
			Edge edge;
			edge.a = CellId{ e.at("a").get<int32_t>() };
			edge.b = CellId{ e.at("b").get<int32_t>() };
			edge.phases = e.at("phases").get<PhaseMask>();
			edge.open = e.at("open").get<bool>();
			edge.drain = e.at("drain").get<bool>();
			if (!from_bits(e.at("conductance"), edge.conductance)) {
				why = "the save holds a number that is not one";
				return nullptr;
			}
			b.add_edge_as(edge);
		}
		b.set_step(saved.at("step").get<uint64_t>());
		const json &bench = saved.at("bench");
		impl->cols = bench.at("grid").at(0).get<int>();
		impl->rows = bench.at("grid").at(1).get<int>();
		impl->mid_step = bench.at("mid_step").get<bool>();
		impl->air = CellId{ bench.at("air").get<int32_t>() };
		impl->room = CellId{ bench.at("room").get<int32_t>() };
		for (const json &j : bench.at("items")) {
			Item item;
			item.id = j.at("id").get<std::string>();
			const int kind = j.at("kind").get<int>(), place = j.at("place").get<int>();
			if (kind < 0 || kind > static_cast<int>(Kind::Tray) || place < 0 || place > static_cast<int>(Place::Outbox)) {
				why = "the save names a kind of item or a place that there is not";
				return nullptr;
			}
			item.kind = static_cast<Kind>(kind);
			item.place = static_cast<Place>(place);
			item.cell = CellId{ j.at("cell").get<int32_t>() };
			item.mouth = EdgeId{ j.at("mouth").get<int32_t>() };
			item.heats = EdgeId{ j.at("heats").get<int32_t>() };
			item.joint = EdgeId{ j.at("joint").get<int32_t>() };
			item.under = j.at("under").get<int>();
			item.on = j.at("on").get<int>();
			item.holds = j.at("holds").get<int>();
			item.drains_to = j.at("drains_to").get<int>();
			item.stirred = j.at("stirred").get<bool>();
			item.submitted = j.at("submitted").get<bool>();
			item.col = j.at("col").get<int>();
			item.row = j.at("row").get<int>();
			item.w = j.value("w", 1);
			item.h = j.value("h", 1);
			item.burst = j.at("burst").get<bool>();
			if (!from_bits(j.at("full"), item.full) || !from_bits(j.at("valve"), item.valve) || !from_bits(j.at("capacity"), item.capacity)) {
				why = "the save holds a number that is not one";
				return nullptr;
			}
			if (item.kind == Kind::Tray) {
				b.set_spill(item.cell);
			}
			impl->items.push_back(item);
		}
		for (const json &e : bench.at("events")) {
			impl->events.push_back(Event{ e.at("step").get<int64_t>(), e.at("what").get<std::string>(), e.at("item").get<std::string>() });
		}
		if (bench.contains("signals") && !impl->signals_from(bench.at("signals"), why)) {
			return nullptr;
		}
		const json &remainders = saved.at("remainders");
		if (!to_doubles(remainders.at("heat"), heat) || !to_doubles(remainders.at("reaction"), reaction) || !to_doubles(remainders.at("nucleation"), nucleation)) {
			why = "the save holds a number that is not one";
			return nullptr;
		}
		world = b.build(&why);
	} catch (const std::exception &e) {
		why = std::string("the save is malformed: ") + e.what();
		return nullptr;
	}
	if (!world) {
		why = "the save cannot be restored: " + why;
		return nullptr;
	}
	const auto cells = static_cast<int32_t>(world->cell_count());
	const auto edge_count = static_cast<int32_t>(world->edges().size());
	const auto item_count = static_cast<int>(impl->items.size());
	for (const Item &item : impl->items) {
		const bool refs = item.cell.index >= 0 && item.cell.index < cells && item.mouth.index < edge_count && item.heats.index < edge_count && item.joint.index < edge_count &&
				item.under < item_count && item.on < item_count && item.holds < item_count && item.drains_to < item_count;
		if (!refs) {
			why = "the save's items point outside the world";
			return nullptr;
		}
	}
	if (impl->air.index < 0 || impl->air.index >= cells || impl->room.index < 0 || impl->room.index >= cells) {
		why = "the save's room is not in its world";
		return nullptr;
	}
	impl->sim.emplace(std::move(*world), std::vector<Reaction>(builtin_reactions().begin(), builtin_reactions().end()));
	impl->sim->set_checking(false);
	impl->sim->restore_remainders(std::move(heat), std::move(reaction), std::move(nucleation));
	impl->genesis = totals(table, impl->world().snapshot());
	return std::unique_ptr<Lab>(new Lab(std::move(impl)));
}

} // namespace lab
