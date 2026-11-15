#include "lab/lab.hpp"

#include "chem/builtin_ids.hpp"
#include "chem/invariants.hpp"
#include "chem/sim.hpp"
#include "chem/thermo.hpp"
#include "chem/version.hpp"
#include "lab/sha256.hpp"

#include <nlohmann/json.hpp>

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

enum class Kind { Vessel, Bag, Burner };

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
};

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
	CellId air;
	CellId room;
	Totals genesis;
	bool check = true;
	bool broken = false;

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
		if (!check) {
			sim->advance();
			return true;
		}
		const StepTrace trace = sim->step();
		const Violations violations = check_all(table(), trace, sim->equilibria());
		if (!violations.empty()) {
			broken = true;
			why = "an invariant failed: " + violations[0];
			return false;
		}
		return true;
	}

	// connections change at a step boundary: take the step in progress to its end
	bool boundary(std::string &why) { return !world().mid_step() || step_once(why); }

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
		const bool ok = kind == Kind::Vessel ? is != Kind::Burner : is == kind; // "vessel": anything that holds matter
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

		for (const json &j : bench.at("items")) {
			Item item;
			item.id = j.at("id").get<std::string>();
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
	if (what == "heat") {
		int burner = -1, vessel = -1;
		if (!item(c, "burner", Kind::Burner, burner, why) || !item(c, "vessel", Kind::Vessel, vessel, why)) {
			return false;
		}
		Item &flame = items[static_cast<size_t>(burner)];
		if (flame.under >= 0) {
			why = "\"" + flame.id + "\" is already under \"" + items[static_cast<size_t>(flame.under)].id + "\"";
			return false;
		}
		if (world().connect(items[static_cast<size_t>(vessel)].cell, flame.cell, PHASES_NONE, &flame.heats, flame.full * flame.valve) != Refusal::None) {
			why = "the burner cannot go there";
			return false;
		}
		flame.under = vessel;
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
		if (!boundary(why)) {
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
		return true;
	}
	if (what == "under") {
		int thing = -1, of = -1;
		if (!item(c, "item", Kind::Vessel, thing, why) || !item(c, "of", Kind::Vessel, of, why)) {
			return false;
		}
		Item &upper = items[static_cast<size_t>(of)];
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
		x.submitted = true;
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
			j["valve"] = item.valve;
			j["under"] = item.under >= 0 ? json(m.items[static_cast<size_t>(item.under)].id) : json(nullptr);
			j["given_j"] = 0.0 - m.world().energy(item.cell).joule();
			items.push_back(j);
			continue;
		}
		j["kind"] = item.kind == Kind::Vessel ? "vessel" : "balloon";
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
