#include "lab/lab.hpp"

#include "chem/builtin_ids.hpp"
#include "chem/invariants.hpp"
#include "chem/sim.hpp"
#include "chem/thermo.hpp"
#include "chem/version.hpp"
#include "lab/sha256.hpp"

#include <nlohmann/json.hpp>

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
	int col = -1, row = -1; // on the bench
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

} // namespace

struct Lab::Impl {
	std::optional<Simulation> sim;
	std::vector<Item> items;
	std::vector<Event> events;
	int cols = 12, rows = 4; // the bench's grid
	CellId air;
	CellId room;
	Totals genesis;
	bool check = true;
	bool broken = false;
	// a command has moved matter since the last step ended: a step is in
	// progress, and a change of connections waits for its end
	bool mid_step = false;
	bool untraced = false; // the last step was unchecked

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

	// the bench cell an item stands on, or -1
	int standing_at(int col, int row) const {
		for (size_t i = 0; i < items.size(); ++i) {
			if (items[i].place == Place::Bench && items[i].col == col && items[i].row == row) {
				return static_cast<int>(i);
			}
		}
		return -1;
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
	if (!bench.is_object() || bench.value("format", "") != "chem-bench/1") {
		why = "the bench's \"format\" is not \"chem-bench/1\"";
		return nullptr;
	}
	auto impl = std::make_unique<Impl>();
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
			if (j.contains("at")) {
				item.place = Place::Bench;
				item.col = j.at("at").at(0).get<int>();
				item.row = j.at("at").at(1).get<int>();
				if (item.col < 0 || item.col >= impl->cols || item.row < 0 || item.row >= impl->rows || impl->standing_at(item.col, item.row) >= 0) {
					why = "item \"" + item.id + "\" stands off the bench's grid, or where something else stands";
					return nullptr;
				}
			}
			if (item.id.empty() || impl->find(item.id) >= 0) {
				why = "an item's id is empty or used twice: \"" + item.id + "\"";
				return nullptr;
			}
			const std::string kind = j.at("kind").get<std::string>();
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
		if (col < 0 || col >= cols || row < 0 || row >= rows) {
			why = "that is off the bench";
			return false;
		}
		const int there = standing_at(col, row);
		if (there >= 0 && there != thing) {
			why = "\"" + items[static_cast<size_t>(there)].id + "\" stands there";
			return false;
		}
		x.place = Place::Bench;
		x.col = col;
		x.row = row;
		return true;
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
		const Item &v = items[static_cast<size_t>(flame.under)];
		flame.place = Place::Cabinet;
		for (int col = 0; col < cols && flame.place == Place::Cabinet; ++col) {
			if (standing_at(col, v.row) < 0) {
				flame.place = Place::Bench;
				flame.col = col;
				flame.row = v.row;
			}
		}
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
		for (int col = 0; col < cols && x.place == Place::Cabinet; ++col) {
			if (standing_at(col, v.row) < 0) {
				x.place = Place::Bench;
				x.col = col;
				x.row = v.row;
			}
		}
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
				other.place = Place::Bench;
				other.col = x.col;
				other.row = x.row;
			}
		}
		x.submitted = true;
		x.place = Place::Outbox;
		x.col = x.row = -1;
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

bool Lab::advance(int64_t steps, std::string &why) {
	if (impl_->broken) {
		why = "an invariant failed earlier: the lab takes no more commands";
		return false;
	}
	for (int64_t i = 0; i < steps; ++i) {
		if (!impl_->step_once(why)) {
			return false;
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

int64_t Lab::step() const {
	return static_cast<int64_t>(impl_->world().step());
}

LabView Lab::view() const {
	const Impl &m = *impl_;
	const SpeciesTable &table = m.table();
	static const char PHASE[] = { 's', 'l', 'g', 'a' };
	LabView out;
	out.step = step();
	out.cols = m.cols;
	out.rows = m.rows;
	for (const Item &item : m.items) {
		ItemView v;
		v.id = item.id;
		v.kind = KIND_NAME[static_cast<int>(item.kind)];
		v.place = PLACE_NAME[static_cast<int>(item.place)];
		v.col = item.col;
		v.row = item.row;
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
		out.items.push_back(v);
	}
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
				{ "row", item.row }, { "capacity", bits(item.capacity) }, { "burst", item.burst } });
	}
	bench["items"] = list;
	json happened = json::array();
	for (const Event &e : events) {
		happened.push_back({ { "step", e.step }, { "what", e.what }, { "item", e.item } });
	}
	bench["events"] = happened;
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
