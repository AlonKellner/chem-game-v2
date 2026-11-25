#include "lab/lab.hpp"
#include "lab/bubbles.hpp"
#include "lab/contents.hpp"
#include "lab/control.hpp"
#include "lab/glassware.hpp"
#include "lab/instruments.hpp"
#include "lab/notebook.hpp"
#include "lab/spectro.hpp"

#include "chem/builtin_ids.hpp"
#include "chem/error.hpp"
#include "chem/invariants.hpp"
#include "chem/sim.hpp"
#include "chem/thermo.hpp"
#include "chem/version.hpp"
#include "lab/conditions.hpp"
#include "lab/sha256.hpp"
#include "lab/thermal.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <limits>
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

constexpr double STIRRED = 3.0; // a stirred vessel's mixing (GAME_SPEC §7); since Stage 57 it changes no rate (kept in saves)
constexpr double STIRRED_W_KG = 0.1; // a stirred beaker's dissipation, W/kg, for "stirring would make it faster" (Stage 57)
// A stirrer (GAME_SPEC §11, Stage 32): the work it does on the liquid a second, W, and how long a liquid left
// alone keeps turning, s (dE/dt = P - E / tau). The game's choices: a small stir bar at a few hundred turns a
// minute; a beaker of water stops turning in a few seconds.
constexpr double STIR_WATTS = 0.01, SWIRL_SECONDS = 1.5;
// A source of work is a reservoir so hot that its energy carries no entropy (the limit of a heat reservoir as
// its temperature grows without bound): what it gives is work, and the second law holds as it is checked (T1, T2).
constexpr double WORK_KELVIN = 1.0e300;
constexpr double GENTLE_POUR = 2e-6; // m^3/s: a gentle pour's trickle down the wall (a design value)
// a pouring lip, an outlet, a cap over the mouth it runs into, m (Stage 51: the screen's 10 pixels at its scale; a
// design value)
constexpr double LIP_GAP = 0.01;
constexpr double WATER_VISCOSITY = 1.0016e-3; // Pa s, at 20 C (docs/DATA_SOURCES.md): what a settling grain falls through
constexpr int TRIAL_SECONDS = 3600;
constexpr size_t NOTEBOOK_MOST = 2000; // the notebook keeps its last this many entries (Stage 54)
constexpr int TRIAL_PER_STEP = 2; // a trial's seconds run at each of the lab's steps (Stage 39): what a step costs is bounded
constexpr double ORIFICE_CD = 0.61; // a sharp-edged orifice's discharge coefficient (GAME_SPEC §13, Stage 27; docs/DATA_SOURCES.md)
constexpr double GRAVITY = 9.80665; // m/s^2, standard
constexpr double PI = 3.14159265358979323846; // a controller's trial, at most (GAME_SPEC §13.3, Stage 26)
// what a hand moves between two vessels: liquid with what is dissolved in it, and solids; never gas
constexpr PhaseMask HANDLED = static_cast<PhaseMask>(PHASES_LIQUID | phase_bit(Phase::Solid));

// a stopper (Stage 28): rubber that seals a neck, an elastic cell of its rubber; a viewer (Stage 42): the spectral
// viewer, an instrument that holds no matter (no cell, no edge: it changes nothing in the world)
// a balance (Stage 50): an instrument under a vessel, as a burner is, with no cell and no edge
enum class Kind { Vessel, Bag, Burner, Tray, Stopper, Viewer, Thermal, Balance, Spectro };
// On: on a vessel's mouth. Under: a burner or a balance (Stage 50) under a vessel. Trash (Stage 34): put away, to be restored; what it holds stays in the world
enum class Place { Cabinet, Bench, On, Under, Outbox, Trash };

struct Item {
	std::string id;
	Kind kind = Kind::Vessel;
	CellId cell; // a vessel's or a bag's contents; a burner's flame
	// pouring (Stage 47): 0 nothing yet; 1 it has laid its liquid on the other's as a layer of its own (Stage 51: every
	// pour, its stream then mixing in what it entrains); and whether it runs down the wall (a gentle pour: it lands
	// at no speed), for a scripted pour's while
	int landed = 0;
	bool gentle = false;
	EdgeId mouth; // a vessel's opening to the room's air, while open
	double full = 0.0; // a burner's conductance with its valve fully open, W/K
	EdgeId heats; // a burner: its edge to the vessel over it
	int under = -1; // a burner: that vessel
	double valve = 0.0;
	int on = -1; // a bag or a condenser: the vessel whose mouth it is on
	EdgeId joint; // its edge to that vessel
	int holds = -1; // a vessel: what is on its mouth
	int drains_to = -1; // a vessel: what stands under its outlet
	// pouring (GAME_SPEC §13, Stage 27): in a pouring position over a vessel, held or upside down with a cap; the
	// drain it pours down; a held pour's tilt (its share of the full tilt), the cap's opening
	int pours_into = -1;
	EdgeId pour;
	bool inverted = false;
	double tilt = 0.0, cap = 0.0;
	double pour_left_m3 = -1.0; // a scripted pour's liquid still to run (below 0: no limit); not saved: a command waits it out
	bool stirred = false;
	// A stirred liquid's turning (GAME_SPEC §11, Stage 32): the edge its stirrer's work comes along, from the
	// mains; its kinetic energy, J; the share of its solids it holds up. And what they were last worked out
	// for (worked out again when the vessel holds something else; saved with the rest, so a lab loaded goes on
	// exactly): the most energy it can hold, the power that holds all its solids up, W, how fast they settle
	// (a share a second).
	EdgeId stirs;
	double swirl = 0.0, suspended = 0.0;
	double swirl_for = std::numeric_limits<double>::quiet_NaN(), swirl_most = 0.0, swirl_lifts = 0.0, settles = 0.0;
	// its vortex's share as last worked out, and for what (a view asks every step while a session waits)
	mutable double share = 0.0, share_energy = -1.0, share_for = -1.0, share_held_up = -1.0;
	bool submitted = false;
	int slot = 0; // in the outbox: its slot, 1 to 6 (GAME_SPEC §13.5)
	Place place = Place::Cabinet;
	int col = -1, row = -1; // on the bench: its footprint's top-left square
	int w = 1, h = 1; // its footprint, in squares (GAME_SPEC §13)
	double capacity = 0.0; // a balloon: the volume at which it bursts, m^3
	double wall_mm = 0.0; // a vessel: how thick its wall is, for what is seen through it (GAME_SPEC §12.3); its heat is its wall's mass
	double floor_mm = 0.0; // and its floor: glass all through, seen from the side
	// a vessel made as a piece of glassware (GAME_SPEC §12.3, Stage 24): its kind and nominal capacity; its
	// cell holds the glassware's capacity to the brim, and its wall and floor are the glassware's
	std::string glassware;
	double nominal_ml = 0.0;
	bool burst = false; // a balloon, burst; a vessel, its glass burst (Stage 28): broken, empty, in the cabinet
	// pressure (GAME_SPEC §13, Stage 28): a stopper's hold (the force its friction holds it in a neck by, N); an
	// upright bottle's screw cap on, sealing it
	double hold_n = 0.0;
	bool screwed = false;
	// a viewer (Stage 42): where its probe is fixed (ItemView: probe_on, probe_room, probe_u, probe_v)
	std::string probe_on;
	bool probe_room = false;
	double probe_u = 0.0, probe_v = 0.0;
	ThermalSettings thermal; // a thermal camera (Stage 48): its frame and settings
	// a balance (Stage 50): its model (data/instruments.json) and its tare, a conventional mass, g; a vessel's label
	std::string model, label;
	double tare = 0.0;
	// a vessel's thermometer by instruments (Stage 52): what it reads, K, following its vessel with its own lag; NaN until
	// it has read anything
	double read_kelvin = std::numeric_limits<double>::quiet_NaN();
	// a spectrophotometer (Stage 55): the wavelength set, nm; its blank's signal (0: none yet) and the wavelength it was
	// set to then
	double nm = 600.0, blank = 0.0, blank_nm = 0.0;
	// its cell holder's temperature (Stage 56), C; NaN: off. It heats the cuvette in its well along `heats`, from the
	// mains
	double holder_c = std::numeric_limits<double>::quiet_NaN();
};

// an instrument a vessel stands on, joined to nothing in the world (Stage 50: a balance; Stage 55: a spectrophotometer,
// its cuvette in its well)
bool stands_under(Kind k) {
	return k == Kind::Balance || k == Kind::Spectro;
}

// a vessel with an outlet at its foot (Stage 52: a burette, a pipette): its glassware, or null
const glass::Vessel *outlet_of(const Item &x) {
	const glass::Vessel *g = x.kind == Kind::Vessel && !x.glassware.empty() ? glass::find(x.glassware, x.nominal_ml) : nullptr;
	return g != nullptr && g->outlet ? g : nullptr;
}

// whether an item holds matter of its own (a burner's cell is its flame, a viewer has none)
bool holds_matter(const Item &x) {
	return x.kind != Kind::Burner && x.kind != Kind::Viewer && x.kind != Kind::Thermal && !stands_under(x.kind);
}

struct Event {
	int64_t step = 0;
	std::string what;
	std::string item;
};

// One element of the trash (GAME_SPEC §13, Stage 34): an item, or a signal part with all it holds. An item
// remembers the square it stood on (none: it came from the cabinet); a part's square is its own.
struct Binned {
	std::string id;
	bool part = false;
	int col = -1, row = -1;
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

const char *const KIND_NAME[] = { "vessel", "balloon", "burner", "tray", "stopper", "viewer", "thermal", "balance", "spectrophotometer" };
const char *const PLACE_NAME[] = { "cabinet", "bench", "on", "under", "outbox", "trash" };

double number(const json &j, const char *key, double fallback) {
	return j.contains(key) && j.at(key).is_number() ? j.at(key).get<double>() : fallback;
}

// an amount as the bench gives it: a number, or (Stage 54: an unknown) a range [low, high] drawn once from the seed
double amount_of(const json &v, uint64_t seed, const std::string &key) {
	if (v.is_array() && v.size() == 2) {
		const double lo = v.at(0).get<double>(), hi = v.at(1).get<double>();
		return lo + (hi - lo) * 0.5 * (1.0 + instruments::share(seed, key));
	}
	return v.get<double>();
}

double mol_of(const SpeciesTable &table, SpeciesId id, const json &amount, uint64_t seed = 0, const std::string &key = "") {
	if (amount.contains("mol")) {
		return amount_of(amount.at("mol"), seed, key);
	}
	return amount_of(amount.at("g"), seed, key) / (static_cast<double>(table.molar_mass_ug(id)) * 1e-6);
}

// An item's footprint on the bench, in squares of the grid (GAME_SPEC §13):
// room for its body, what stands on its mouth starts above it, and a vessel's
// stand over a burner. A bench may give its own ("size").
void default_size(const std::string &kind, double volume_ml, bool condenser, int &w, int &h, const std::string &glassware = "") {
	// tall glassware (Stage 52): a burette, a pipette, a cylinder, a volumetric flask stand on narrow, tall footprints
	if (kind == "vessel" && (glassware == "burette" || glassware == "pipette" || glassware == "measuring_cylinder" || glassware == "volumetric_flask")) {
		w = 2;
		h = glassware == "burette" ? 12 : glassware == "pipette" ? 11 : volume_ml > 50.0 ? 8 : 6;
		return;
	}
	if (kind == "viewer") {
		w = 7; // the spectral viewer (Stage 42): its screen, and its probe's holder beside it
		h = 4;
	} else if (kind == "thermal") {
		w = 5; // a thermal camera (Stage 48): its case and screen
		h = 4;
	} else if (kind == "balance") {
		w = 4; // a balance (Stage 50): its pan and its display
		h = 2;
	} else if (kind == "spectrophotometer") {
		w = 5; // a spectrophotometer (Stage 55): its well and its displays
		h = 3;
	} else if (kind == "burner") {
		w = h = 3; // the burner and its valve beside it
	} else if (kind == "stopper") {
		w = h = 1;
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

const char *level_name(Notice::Level level) {
	static const char *const NAMES[] = { "detail", "event", "warning", "danger" };
	return NAMES[static_cast<int>(level) & 3];
}

bool level_of(std::string_view name, Notice::Level &level) {
	for (int k = 0; k < 4; ++k) {
		if (name == level_name(static_cast<Notice::Level>(k))) {
			level = static_cast<Notice::Level>(k);
			return true;
		}
	}
	return false;
}

namespace {

std::string text_of(const std::string &format, double v) {
	char buffer[256];
	std::snprintf(buffer, sizeof buffer, format.c_str(), v);
	return buffer;
}
std::string text_of(const std::string &format, double v, double w) {
	char buffer[256];
	std::snprintf(buffer, sizeof buffer, format.c_str(), v, w);
	return buffer;
}

// a species as the readout names it: "CuSO4.5H2O(s)"
std::string named(const SpeciesTable &table, SpeciesId s) {
	const SpeciesDef &def = table.species(s);
	static const char *const PHASE[] = { "s", "l", "g", "aq" };
	return std::string(def.formula) + "(" + PHASE[static_cast<int>(def.phase)] + ")";
}

} // namespace

struct Lab::Impl {
	std::optional<Simulation> sim;
	std::vector<Item> items;
	std::vector<Event> events;
	int cols = 28, rows = 15; // the bench's grid, in squares
	// readings (GAME_SPEC §13, Stage 50): by instruments, or the truth; the seed the instruments' offsets and a hand's
	// misses are drawn from
	bool instruments = false;
	uint64_t seed = 0;
	// the notebook (Stage 54): what was recorded, and the loggers
	std::vector<NoteEntry> notebook;
	std::vector<NoteLogger> loggers;
	CellId air;
	CellId room;
	CellId mains; // what does the stirrers' work (Stage 32); none in a save from before it
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
	int64_t made[PART_KINDS] = {}; // the parts made of each kind, for their names
	// the trash (GAME_SPEC §13, Stage 34): what was put in it, oldest first. An item in it is in the world as one
	// in the cabinet is. A part in it, with all it holds, is out of `parts`: nothing evaluates it and it signals
	// nothing; the wires and the resets among what a circuit holds are kept beside it
	std::vector<Binned> trash;
	std::vector<Part> binned;
	std::vector<Wire> binned_wires;
	std::vector<Rig> binned_rigs;
	bool rang = false; // an alarm started ringing in the last step
	// goals (GAME_SPEC §13.5): the bench's, then the player's; `met` as last noticed
	std::vector<Goal> goals;
	int64_t goals_made = 0;
	// notices (GAME_SPEC §13.4): what happened, and what the detectors last saw
	std::vector<Notice> notices;
	size_t notices_read = 0; // what the notice parts have read
	std::vector<uint8_t> present; // item * species: a solid, liquid or dissolved species is there
	std::vector<uint8_t> running; // item * reaction with a rate law: it runs there
	std::vector<uint8_t> boiling, full; // per item
	std::vector<uint8_t> pressed, flowing; // per item (Stage 28): a sealed vessel near what gives way; a pour running
	std::vector<double> water_g; // per item (Stage 28): its liquid water at the last reading
	std::vector<uint8_t> touched; // per item: matter moved in or out by hand since the last reading
	double tray_g = 0.0; // what the tray held at the last reading
	bool restored_notices = false; // a load found them in its save

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
	int find_binned(const std::string &id) const {
		for (size_t i = 0; i < binned.size(); ++i) {
			if (binned[i].id == id) {
				return static_cast<int>(i);
			}
		}
		return -1;
	}
	// a name in use: an item's, a part's, or that of a part in the trash
	bool taken(const std::string &id) const { return find(id) >= 0 || find_part(id) >= 0 || find_binned(id) >= 0; }
	// the name of what standing_in found: an item, or a part after the items
	const std::string &name_of(int index) const {
		return index < static_cast<int>(items.size()) ? items[static_cast<size_t>(index)].id : parts[static_cast<size_t>(index) - items.size()].id;
	}

	// ------------------------------------------------------------- signals (§13.3)

	// a monitor's reading of one item, from what the view would show of it
	std::optional<double> reading(const Quantity &q) const {
		if (q.what == "met") { // a goal (§13.5)
			const int g = find_goal(q.item);
			return g >= 0 ? std::optional<double>(goal_met(goals[static_cast<size_t>(g)]) ? 1.0 : 0.0) : std::nullopt;
		}
		const int i = find(q.item);
		if (i < 0) {
			return std::nullopt;
		}
		LabView one;
		one.items.push_back(item_view(items[static_cast<size_t>(i)], false, q.what == "bubbles_cm"));
		return measure(one, q);
	}

	// ---- the notebook (GAME_SPEC §13, Stage 54)
	// what is read of a quantity now, as the notebook writes it: by instruments an instrument's reading, or none
	std::optional<double> note_reading(const Quantity &q, bool &instrument) const {
		const int i = find(q.item);
		instrument = false;
		if (i < 0) {
			return std::nullopt;
		}
		LabView one;
		one.instruments = instruments;
		one.seed = seed;
		one.items.push_back(item_view(items[static_cast<size_t>(i)], false, q.what == "bubbles_cm"));
		return lab::reading(one, q, instrument);
	}
	bool record_note(const Quantity &q, const std::string &label, std::string &why) {
		bool instrument = false;
		const std::optional<double> v = note_reading(q, instrument);
		if (!v) {
			why = instruments ? "no instrument reads " + label_of(q) + ": a student sees only what instruments read" : "there is no " + label_of(q) + " to read";
			return false;
		}
		notebook.push_back(NoteEntry{ static_cast<int64_t>(world().step()), label.empty() ? label_of(q) : label, q, *v, unit(q), instrument });
		if (notebook.size() > NOTEBOOK_MOST) {
			notebook.erase(notebook.begin());
		}
		return true;
	}
	static std::string label_of(const Quantity &q) { return label(LabView{}, q); }
	static bool quantity_of_command(const json &c, Quantity &q, std::string &why) {
		if (!c.contains("item") || !c.at("item").is_string() || !c.contains("what") || !c.at("what").is_string()) {
			why = "a reading names its \"item\" and \"what\"";
			return false;
		}
		q.item = c.at("item").get<std::string>();
		q.what = c.at("what").get<std::string>();
		q.measure = c.value("measure", "g");
		return true;
	}
	// the loggers whose time has come record (at the end of a step)
	void follow_loggers() {
		for (NoteLogger &l : loggers) {
			if (static_cast<int64_t>(world().step()) >= l.next) {
				std::string no;
				record_note(l.quantity, l.label, no);
				const int64_t n = std::max<int64_t>(1, std::llround(l.every_s / STEP_SECONDS));
				l.next = (static_cast<int64_t>(world().step()) / n + 1) * n; // (the next whole multiple)
			}
		}
	}

	// ---- goals (§13.5)
	int find_goal(const std::string &id) const {
		for (size_t g = 0; g < goals.size(); ++g) {
			if (goals[g].id == id) {
				return static_cast<int>(g);
			}
		}
		return -1;
	}
	int goal_in(int slot) const {
		for (size_t g = 0; g < goals.size(); ++g) {
			if (goals[g].slot == slot) {
				return static_cast<int>(g);
			}
		}
		return -1;
	}
	// what is handed in to a slot; -1 for nothing
	int held_in(int slot) const {
		for (size_t i = 0; i < items.size(); ++i) {
			if (items[i].place == Place::Outbox && items[i].slot == slot) {
				return static_cast<int>(i);
			}
		}
		return -1;
	}
	// met now: a slot holds an item, and every term holds of it
	bool goal_met(const Goal &g) const {
		const int i = held_in(g.slot);
		if (i < 0 || g.terms.empty()) {
			return false;
		}
		for (const GoalTerm &t : g.terms) {
			const std::optional<double> v = reading(Quantity{ items[static_cast<size_t>(i)].id, t.quantity.what, t.quantity.measure });
			if (!v || !meets(t.comparison, *v, std::nullopt)) {
				return false;
			}
		}
		return true;
	}
	// a goal becoming met, or no longer met, is news
	void detect_goals(bool quiet) {
		for (Goal &g : goals) {
			const bool now = goal_met(g);
			if (now != g.met) {
				g.met = now;
				if (!quiet) {
					note(now ? Notice::Event : Notice::Warning, g.id, (now ? "goal met: " : "goal no longer met: ") + g.name);
				}
			}
		}
	}
	// a goal's terms, read: a quantity of what is handed in, compared
	static bool terms_from(const json &j, std::vector<GoalTerm> &out, std::string &why) {
		if (!j.is_array() || j.empty() || j.size() > 8) {
			why = "a goal has \"terms\": one to eight of {\"quantity\": {\"what\", \"measure\"}, \"compare\", \"value\" | \"values\"}";
			return false;
		}
		out.clear();
		for (const json &t : j) {
			GoalTerm term;
			if (!t.is_object() || !t.contains("quantity") || !t.at("quantity").is_object() || !t.at("quantity").contains("what") || !t.at("quantity").at("what").is_string()) {
				why = "a goal's term names a \"quantity\": {\"what\", \"measure\"}";
				return false;
			}
			term.quantity.what = t.at("quantity").at("what").get<std::string>();
			term.quantity.measure = t.at("quantity").value("measure", std::string("g"));
			if (term.quantity.what == "met" || !valid(term.quantity, why)) {
				why = why.empty() ? "a goal's term is a quantity of what is handed in" : why;
				return false;
			}
			if (!t.contains("compare") || !t.at("compare").is_string() || !op_of(t.at("compare").get<std::string>(), term.comparison.op) || term.comparison.op == Comparison::Reaches) {
				why = "a goal's term compares: \"compare\" is above, below, at_least, at_most, between or outside";
				return false;
			}
			const char *key = two_values(term.comparison.op) ? "values" : "value";
			if (two_values(term.comparison.op) ? !(t.contains("values") && t.at("values").is_array() && t.at("values").size() == 2 && t.at("values")[0].is_number() && t.at("values")[1].is_number())
											   : !(t.contains("value") && t.at("value").is_number())) {
				why = std::string("a goal's term compares with \"") + key + "\"";
				return false;
			}
			term.comparison.a = two_values(term.comparison.op) ? t.at("values")[0].get<double>() : t.at("value").get<double>();
			term.comparison.b = two_values(term.comparison.op) ? t.at("values")[1].get<double>() : 0.0;
			if (!valid(term.comparison, why)) {
				return false;
			}
			out.push_back(term);
		}
		return true;
	}
	static json terms_json(const std::vector<GoalTerm> &terms, bool exact) {
		static const char *const OPS[] = { "above", "below", "at_least", "at_most", "reaches", "between", "outside" };
		json out = json::array();
		for (const GoalTerm &t : terms) {
			json j{ { "quantity", { { "what", t.quantity.what }, { "measure", t.quantity.measure } } }, { "compare", OPS[static_cast<int>(t.comparison.op)] } };
			const auto num = [exact](double v) { return exact ? json(bits(v)) : json(v); };
			if (two_values(t.comparison.op)) {
				j["values"] = { num(t.comparison.a), num(t.comparison.b) };
			} else {
				j["value"] = num(t.comparison.a);
			}
			out.push_back(j);
		}
		return out;
	}
	static bool slot_of(const json &c, int &slot, std::string &why) {
		if (!c.contains("slot") || !c.at("slot").is_number_integer() || c.at("slot").get<int>() < 1 || c.at("slot").get<int>() > OUTBOX_SLOTS) {
			why = "a \"slot\" of the outbox is 1 to 6";
			return false;
		}
		slot = c.at("slot").get<int>();
		return true;
	}
	json goals_json(bool exact) const {
		json list = json::array();
		for (const Goal &g : goals) {
			list.push_back({ { "id", g.id }, { "name", g.name }, { "slot", g.slot }, { "game", g.game }, { "terms", terms_json(g.terms, exact) }, { "met", exact ? g.met : goal_met(g) } });
		}
		return list;
	}
	bool goals_from(const json &list, int64_t count, std::string &why) {
		for (const json &x : list) {
			Goal g;
			g.id = x.at("id").get<std::string>();
			g.name = x.at("name").get<std::string>();
			g.slot = x.at("slot").get<int>();
			g.game = x.at("game").get<bool>();
			g.met = x.at("met").get<bool>();
			for (const json &t : x.at("terms")) {
				json plain = t; // the values back from their bits
				const auto back = [&](json &v) {
					double d = 0.0;
					if (!from_bits(v, d)) {
						return false;
					}
					v = d;
					return true;
				};
				if ((plain.contains("value") && !back(plain["value"])) || (plain.contains("values") && (!back(plain["values"][0]) || !back(plain["values"][1])))) {
					why = "the save holds a number that is not one";
					return false;
				}
				std::vector<GoalTerm> one;
				if (!terms_from(json::array({ plain }), one, why)) {
					return false;
				}
				g.terms.push_back(one[0]);
			}
			goals.push_back(g);
		}
		goals_made = count;
		return true;
	}

	// a control's value now; none when it is not one of the lab's
	// an upright bottle's screw cap (Stage 28): one that fits it, nothing else on its mouth, not upside down
	bool screw_cap(int i) const {
		if (i < 0) {
			return false;
		}
		const Item &x = items[static_cast<size_t>(i)];
		const glass::Vessel *g = x.kind == Kind::Vessel ? glass::find(x.glassware, x.nominal_ml) : nullptr;
		return g != nullptr && g->screw_cap && !x.burst && x.pours_into < 0 && (x.holds < 0 || x.screwed) && x.place != Place::Outbox;
	}
	std::optional<double> control_now(const std::string &item_id, const std::string &control) const {
		const int i = find(item_id);
		if (control == "cap" && i >= 0 && items[static_cast<size_t>(i)].pours_into >= 0 && items[static_cast<size_t>(i)].inverted) {
			return items[static_cast<size_t>(i)].cap; // an upside-down vessel's cap (Stage 27)
		}
		if (control == "cap" && i >= 0 && outlet_of(items[static_cast<size_t>(i)]) != nullptr) {
			return items[static_cast<size_t>(i)].cap; // a burette's stopcock (Stage 52)
		}
		if (control == "cap" && screw_cap(i)) {
			return items[static_cast<size_t>(i)].screwed ? 0.0 : 1.0; // upright: on (0, sealed) or off (1)
		}
		if (control == "stir" && i >= 0 && holds_matter(items[static_cast<size_t>(i)]) && items[static_cast<size_t>(i)].kind != Kind::Tray) {
			return items[static_cast<size_t>(i)].stirred ? 1.0 : 0.0;
		}
		if (control == "valve" && i >= 0 && items[static_cast<size_t>(i)].kind == Kind::Burner) {
			return items[static_cast<size_t>(i)].valve;
		}
		return std::nullopt;
	}

	// a control that is another now (Stage 28: a cap upside down, an orifice, and upright, a screw cap): its last setting gone
	void forget(const std::string &item_id, const std::string &control) {
		controls.erase(std::remove_if(controls.begin(), controls.end(), [&](const ControlSetting &c) { return c.item == item_id && c.control == control; }), controls.end());
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

	bool trial = false; // a copy a controller's trial runs (Stage 28)

	// what a signal part is doing, for the debug panel (Stage 28)
	std::vector<std::string> part_facts(const Part &p) const {
		std::vector<std::string> out;
		const LabView none;
		const auto value = [&](const std::optional<double> &v) { return v ? shown(p.quantity, *v) : std::string("nothing"); };
		switch (p.kind) {
			case PartKind::Timer:
				out.push_back(text_of("%.1f s left", static_cast<double>(p.left) * STEP_SECONDS) + text_of(" of %.1f s", static_cast<double>(p.preset) * STEP_SECONDS));
				break;
			case PartKind::Monitor:
				if (!p.watching) {
					out.push_back("not set: it senses nothing");
					break;
				}
				out.push_back("senses " + label(none, p.quantity) + ", " + op_name(p.comparison.op) + " " + shown(p.quantity, p.comparison.a) +
						(p.comparison.op == Comparison::Between || p.comparison.op == Comparison::Outside ? " and " + shown(p.quantity, p.comparison.b) : std::string()));
				out.push_back("reads " + value(reading(p.quantity)) + (p.own ? ": its condition holds" : ": its condition does not hold"));
				break;
			case PartKind::Notice:
				out.push_back(std::string("hears ") + level_name(static_cast<Notice::Level>(p.notice_level)) + " and above" + (p.quantity.item.empty() ? "" : ", about " + p.quantity.item) +
						(p.contains.empty() ? "" : ", containing \"" + p.contains + "\""));
				out.push_back(text_of("signals for %.2f s after one", static_cast<double>(p.hold) * STEP_SECONDS) + (p.out ? text_of(": on, %.2f s to go", static_cast<double>(p.held) * STEP_SECONDS) : ": off"));
				break;
			case PartKind::Writer:
				out.push_back(std::string("writes a ") + level_name(static_cast<Notice::Level>(p.notice_level)) + " when its input turns " + (p.writes == 2 ? "on or off" : writes_name(p.writes)) +
						(p.quantity.item.empty() ? "" : ", about " + p.quantity.item));
				out.push_back("its words: " + (p.says.empty() ? p.id + " was signalled" : p.says));
				break;
			case PartKind::Alarm:
				out.push_back(p.ringing ? "ringing" : "quiet");
				break;
			case PartKind::Circuit:
				out.push_back("it holds parts; what is in it signals");
				break;
			case PartKind::Controller: {
				static const char *const SPEEDS[] = { "gentle", "normal", "fast" };
				if (!p.watching) {
					out.push_back("not set: it senses nothing");
					break;
				}
				const std::string u = unit(p.quantity);
				out.push_back("holds " + label(none, p.quantity) + " at " + shown(p.quantity, p.target) + ", " + SPEEDS[std::clamp(p.speed, 0, 2)]);
				if (!p.tuned) {
					out.push_back(p.trying ? "trying what it controls, to tune itself" : "not tuned: rig what it controls, and set it");
					break;
				}
				if (p.trying) {
					out.push_back("trying what it controls again: its gains held until the trial's");
				}
				out.push_back(p.tau > 0.0 ? "its trial found: " + text_of("%.4g ", p.gain) + u + " full on, " + text_of("over %.0f s", p.tau) + text_of(", after %.1f s", p.theta)
										  : "its trial found: " + text_of("%.4g ", p.gain) + u + " a second full on" + text_of(", after %.1f s", p.theta));
				out.push_back(text_of("gain %.4g", p.kc) + text_of(", integral time %.0f s", p.ti) + text_of(", PWM period %.0f s", static_cast<double>(p.period) * STEP_SECONDS));
				const std::optional<double> v = reading(p.quantity);
				out.push_back("reads " + value(v) + (v ? text_of(", error %.4g", p.target - *v) : std::string()) + text_of(", integral %.4g", p.integral) +
						text_of(", level %.3f", p.level) + text_of(" (PWM on %.0f", static_cast<double>(p.on_steps)) + text_of(" of %.0f steps)", static_cast<double>(p.period)));
				out.push_back(text_of("tuned on %.1f g", p.basis_g) + text_of(" and %.0f J/K: tunes again past a tenth of either, or a material come or gone", p.basis_cp));
				if (p.pinned > 0) {
					out.push_back(text_of("held at a limit for %.0f s", static_cast<double>(p.pinned) * STEP_SECONDS));
				}
				break;
			}
		}
		return out;
	}

	// what a controller's sensed item holds, as re-tuning compares it (Stage 28): its contents' mass, its heat
	// capacity, the materials there
	void basis_of(const std::string &id, double &g, double &cp, uint64_t &has) const {
		g = cp = 0.0;
		has = 0;
		const int i = find(id);
		if (i < 0 || !holds_matter(items[static_cast<size_t>(i)])) {
			return;
		}
		const CellId cell = items[static_cast<size_t>(i)].cell;
		const CellState &state = world().snapshot_cell(cell);
		for (size_t s = 0; s < state.amount.size() && s < 64; ++s) {
			const SpeciesDef &def = table().species(SpeciesId{ static_cast<int32_t>(s) });
			const double grams_here = grams(cell, SpeciesId{ static_cast<int32_t>(s) });
			if (!def.wall && def.phase != Phase::Gas) {
				g += grams_here;
				has |= grams_here >= 0.001 ? uint64_t{ 1 } << s : 0;
			}
		}
		const Thermal t = thermal_state(table(), state, &world().meta(cell));
		cp = t.defined ? t.heat_capacity : 0.0;
	}

	// Each reading (Stage 28): a controller tunes again where what it controls has changed since its trial (by a
	// tenth of its contents' mass or of its heat capacity, or a material come or gone), once no pour into it or
	// from it runs, its level carried over (no bump: its integral set to give it).
	void watch_controllers() {
		if (trial) {
			return;
		}
		for (size_t k = 0; k < parts.size(); ++k) {
			Part &p = parts[k];
			if (p.kind != PartKind::Controller || !p.tuned || !p.watching || p.trying) {
				continue;
			}
			const int sensed = find(p.quantity.item);
			bool pouring = false; // not while it is poured into or from: once it settles
			for (const Item &x : items) {
				pouring = pouring || (x.pours_into >= 0 && (x.pours_into == sensed || &x - items.data() == sensed));
			}
			if (pouring) {
				continue;
			}
			double g = 0.0, cp = 0.0;
			uint64_t has = 0;
			basis_of(p.quantity.item, g, cp, has);
			const auto off = [](double now, double was) { return std::fabs(now - was) > 0.1 * std::fabs(was) && std::fabs(now - was) > 1e-9; };
			if (off(g, p.basis_g) || off(cp, p.basis_cp) || has != p.basis_has) {
				note(Notice::Detail, p.id, p.id + " tunes again: what it controls changed");
				tune(static_cast<int>(k), true); // its gains held until the trial's
			}
		}
	}

	// A controller tunes itself (§13.3, Stage 26): what it controls, tried on two copies of the lab as it
	// stands, every control rigged from it held full on in one and off in the other, read each second until the
	// difference reaches half again the distance to the target, or levels off, or an hour passes; fitted, and
	// SIMC's gains for its speed. Untuned, with the reason, when there is nothing to try or nothing moves.
	// The trial runs beside the lab, TRIAL_PER_STEP of its seconds at each of the lab's steps (Stage 39): a
	// controller set or rigged is untuned until it ends; one tuned before (`again`, what it controls changed)
	// keeps its gains until the new, then takes them with its level carried over.
	struct Trial {
		std::string part;
		std::unique_ptr<Lab> copies[2];
		std::vector<double> t, r; // each second's difference, on less off
		double distance = 0.0; // from the target, as it started
		double basis_g = 0.0, basis_cp = 0.0; // what it was tried on
		uint64_t basis_has = 0;
		bool again = false;
	};
	std::vector<Trial> trials;

	int trial_of(const std::string &part) const {
		for (size_t k = 0; k < trials.size(); ++k) {
			if (trials[k].part == part) {
				return static_cast<int>(k);
			}
		}
		return -1;
	}
	void drop_trial(const std::string &part) {
		std::erase_if(trials, [&](const Trial &t) { return t.part == part; });
		const int at = find_part(part);
		if (at >= 0) {
			parts[static_cast<size_t>(at)].trying = false;
		}
	}

	void tune(int at, bool again = false) {
		Part &p = parts[static_cast<size_t>(at)];
		drop_trial(p.id);
		const auto untune = [&] {
			retune(p, false);
			p.gain = p.tau = p.theta = 0.0;
		};
		if (!again) {
			untune();
		}
		std::vector<Rig> mine;
		for (const Rig &r : rigs) {
			if (r.from == p.id && r.follow) {
				mine.push_back(r);
			}
		}
		if (!p.watching || mine.empty()) {
			untune();
			return; // tuned once it is set and rigged
		}
		const std::optional<double> start = reading(p.quantity);
		std::string why;
		const std::string saved = start ? save().dump() : std::string();
		Trial tried;
		tried.part = p.id;
		tried.again = again;
		for (std::unique_ptr<Lab> &copy : tried.copies) {
			copy = saved.empty() ? nullptr : Lab::load(saved, why);
		}
		const auto untuned = [&](const std::string &reason) {
			untune();
			note(Notice::Warning, p.id, p.id + " is not tuned: " + reason);
		};
		if (!tried.copies[0] || !tried.copies[1]) {
			untuned(start ? "the lab could not be copied to try it" : "it reads nothing of " + p.quantity.item);
			return;
		}
		for (int k = 0; k < 2; ++k) {
			Lab &l = *tried.copies[k];
			l.set_checking(false);
			bool ok = l.command(json{ { "do", "scrap" }, { "part", p.id } }.dump(), why); // its rigs go with it
			l.impl_->trial = true; // a copy tries; it does not tune its own controllers again (Stage 28)
			for (const Rig &r : mine) {
				const json c = r.control == "valve" ? json{ { "do", "valve" }, { "burner", r.item }, { "to", k == 0 ? 1.0 : 0.0 } }
						: r.control == "cap"        ? json{ { "do", "cap" }, { "item", r.item }, { "to", k == 0 ? 1.0 : 0.0 } }
													: json{ { "do", "stir" }, { "vessel", r.item }, { "on", k == 0 } };
				ok = ok && l.command(c.dump(), why);
			}
			if (!ok) {
				untuned("what it controls could not be tried: " + why);
				return;
			}
		}
		tried.distance = std::fabs(p.target - *start);
		basis_of(p.quantity.item, tried.basis_g, tried.basis_cp, tried.basis_has);
		p.trying = true;
		trials.push_back(std::move(tried));
	}

	// a trial's next second; true when it has ended
	static bool trial_second(Trial &trial, const Quantity &q) {
		const auto second = static_cast<int>(trial.t.size()) + 1;
		std::string why;
		if (second > TRIAL_SECONDS || !trial.copies[0]->advance(60, why) || !trial.copies[1]->advance(60, why)) {
			return true;
		}
		const std::optional<double> on = trial.copies[0]->impl_->reading(q), off = trial.copies[1]->impl_->reading(q);
		if (!on || !off) {
			return true;
		}
		trial.t.push_back(second);
		trial.r.push_back(*on - *off);
		const double moved = std::fabs(trial.r.back());
		// far enough to hold the target, or levelled off: a third of the trial moved it by a hundredth
		const size_t third = trial.r.size() * 2 / 3;
		return (second >= 10 && trial.distance > 0.0 && moved >= 1.5 * trial.distance) ||
				(second >= 60 && moved > 0.0 && std::fabs(trial.r.back() - trial.r[third]) < 0.01 * moved);
	}

	// every trial on by its seconds for this step, and those ended fitted (Stage 39)
	void run_trials() {
		for (size_t k = 0; k < trials.size();) {
			const int at = find_part(trials[k].part);
			if (at < 0 || !parts[static_cast<size_t>(at)].watching) {
				trials.erase(trials.begin() + static_cast<std::ptrdiff_t>(k));
				continue;
			}
			bool ended = false;
			for (int s = 0; s < TRIAL_PER_STEP && !ended; ++s) {
				ended = trial_second(trials[k], parts[static_cast<size_t>(at)].quantity);
			}
			if (!ended) {
				++k;
				continue;
			}
			const Trial ended_one = std::move(trials[k]);
			trials.erase(trials.begin() + static_cast<std::ptrdiff_t>(k));
			parts[static_cast<size_t>(at)].trying = false;
			trial_ended(at, ended_one);
		}
	}

	// a trial ended: fitted, and SIMC's gains for its speed
	void trial_ended(int at, const Trial &tried) {
		Part &p = parts[static_cast<size_t>(at)];
		const double level = p.level;
		const bool tuned_before = p.tuned;
		ProcessModel m;
		if (!fit_process(tried.t, tried.r, m)) {
			retune(p, false);
			p.gain = p.tau = p.theta = 0.0;
			note(Notice::Warning, p.id, p.id + " is not tuned: nothing it controls moves " + label(LabView{}, p.quantity));
			return;
		}
		const double span = m.integrating ? tried.t.back() : m.tau;
		const PiGains g = simc(m, closed_loop_time(m, static_cast<Speed>(p.speed), span), 2.0 * STEP_SECONDS);
		p.gain = m.gain;
		p.tau = m.integrating ? 0.0 : m.tau;
		p.theta = m.theta;
		retune(p, true);
		p.kc = g.kc;
		p.ti = g.ti;
		p.period = std::clamp<int64_t>(std::llround(span / 10.0 / STEP_SECONDS), 600, 36000); // a tenth of its time: 10 s to 10 min
		p.basis_g = tried.basis_g;
		p.basis_cp = tried.basis_cp;
		p.basis_has = tried.basis_has;
		const std::string u = unit(p.quantity);
		note(Notice::Event, p.id,
				p.id + " tuned: " + (m.integrating ? text_of("%.3g ", m.gain) + u + " a second, full on" : text_of("%.3g ", m.gain) + u + " full on, " + text_of("over %.0f s", m.tau)) +
						text_of(", after %.0f s; ", m.theta) + text_of("gain %.3g, ", g.kc) + text_of("integral time %.0f s", g.ti));
		if (tried.again && tuned_before) { // no bump: its integral set to give the level it had
			const std::optional<double> value = reading(p.quantity);
			if (value && p.kc != 0.0 && p.ti > 0.0) {
				p.integral = p.ti * (level / p.kc - (p.target - *value));
				p.level = level;
			}
		}
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
			if (!holds_matter(x) || x.kind == Kind::Tray || x.place == Place::Cabinet || x.place == Place::Outbox || x.place == Place::Trash) {
				return false;
			}
			const bool on = value > 0.5;
			world().set_mixing(x.cell, on ? STIRRED : 1.0);
			x.stirred = on;
			record(item_id, control, on ? 1.0 : 0.0, by);
			return true;
		}
		if (control == "cap" && ((x.pours_into >= 0 && x.inverted) || outlet_of(x) != nullptr)) { // (Stage 52: an outlet's stopcock)
			x.cap = std::clamp(value, 0.0, 1.0);
			record(item_id, control, x.cap, by);
			return true;
		}
		if (control == "cap" && screw_cap(i)) { // upright: screwed on, it seals the bottle; off, open to the room
			const bool shut = value < 0.5;
			if (shut != x.screwed) {
				if (shut) {
					if (x.mouth.index < 0 || world().disconnect(x.mouth) != Refusal::None) {
						return false; // mid-step, or not at the room's pressure yet
					}
					x.mouth = EdgeId{};
				} else {
					world().connect(x.cell, air, PHASES_GAS, &x.mouth);
				}
				x.screwed = shut;
			}
			record(item_id, control, shut ? 0.0 : 1.0, by);
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

	// a notice part's text in a notice's words, whatever their case (Stage 28)
	static bool contains_text(const std::string &says, const std::string &text) {
		const auto lower = [](std::string v) {
			for (char &ch : v) {
				ch = ch >= 'A' && ch <= 'Z' ? static_cast<char>(ch - 'A' + 'a') : ch;
			}
			return v;
		};
		return lower(says).find(lower(text)) != std::string::npos;
	}

	// the signals at a step's end; false when an invariant fails
	bool signals_step(std::string &why) {
		rang = false;
		if (parts.empty()) {
			return true;
		}
		const std::vector<Part> before = parts;
		const size_t unread = notices_read;
		const auto noticed = [this, unread](int level, const std::string &about, const std::string &contains) {
			for (size_t k = unread; k < notices.size(); ++k) {
				if (static_cast<int>(notices[k].level) >= level && (about.empty() || notices[k].item == about) && (contains.empty() || contains_text(notices[k].says, contains))) {
					return true;
				}
			}
			return false;
		};
		const SignalStep done = evaluate(parts, wires, rigs, [this](const Quantity &q) { return reading(q); }, static_cast<int64_t>(world().step()), noticed);
		for (Part &p : parts) { // a controller held at a limit short of its target (Stage 28)
			if (p.kind != PartKind::Controller || !p.tuned || !p.watching) {
				continue;
			}
			const std::optional<double> value = reading(p.quantity);
			const double e = value ? p.target - *value : 0.0;
			const bool all_on = p.level >= 1.0 - 1e-12 && p.kc * e > 0.0, none = p.level <= 1e-12 && p.kc * e < 0.0;
			p.pinned = all_on || none ? p.pinned + 1 : 0;
			if (p.pinned == 0) {
				p.pinned_said = false;
			} else if (p.pinned >= 60 * 60 && !p.pinned_said && !trial) {
				p.pinned_said = true;
				note(Notice::Warning, p.id, p.id + " cannot hold " + label(LabView{}, p.quantity) + text_of(" at %.4g", p.target) + ": what it controls is " + (all_on ? "full on" : "off") +
								text_of(", and it reads %.4g", value ? *value : 0.0));
			}
		}
		notices_read = notices.size();
		for (size_t k = 0; k < parts.size() && k < before.size(); ++k) {
			if (parts[k].kind == PartKind::Timer && parts[k].own && !before[k].own) {
				note(Notice::Detail, parts[k].id, parts[k].id + " reached zero");
			}
		}
		for (const std::string &alarm : done.rang) {
			note(Notice::Event, alarm, alarm + " rang");
		}
		for (const std::string &writer : done.written) { // a writer's own notice: heard from the next step
			const Part &w = parts[static_cast<size_t>(find_part(writer))];
			note(static_cast<Notice::Level>(w.notice_level), w.quantity.item.empty() ? w.id : w.quantity.item, w.says.empty() ? w.id + " was signalled" : w.says);
		}
		std::vector<std::string> failing;
		if (check) {
			failing = check_G2(parts, wires, before);
			if (failing.empty()) {
				failing = check_G3(parts, wires, before);
			}
			if (failing.empty()) {
				failing = check_G4(parts);
			}
		}
		for (const RigAction &a : done.actions) {
			const Rig &rig = rigs[a.rig];
			if (a.follow) {
				// a controller's rig: the control follows it where it differs, and nobody notes each step (Stage 26)
				const std::optional<double> now = control_now(rig.item, rig.control);
				if (now && std::fabs(*now - a.value) > 1e-12) {
					set_control(rig.item, rig.control, a.value, rig.from);
				}
				continue;
			}
			if (set_control(rig.item, rig.control, a.value, rig.from)) {
				note(Notice::Detail, rig.item,
						rig.from + (rig.control == "valve" ? text_of(" set the valve of " + rig.item + " to %.0f%%", 100.0 * a.value)
										: rig.control == "stir" ? (a.value > 0.5 ? " started stirring " : " stopped stirring ") + rig.item
																: " reset " + rig.item));
			}
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
		if ((control == "valve" || control == "cap") && j.is_number() && j.get<double>() >= 0.0 && j.get<double>() <= 1.0) {
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
		json out = links_json(parts, wires, rigs, exact);
		json k = json::array();
		for (const ControlSetting &x : controls) {
			k.push_back({ { "item", x.item }, { "control", x.control }, { "value", exact ? json(bits(x.value)) : json(x.value) }, { "by", x.by }, { "step", x.step } });
		}
		out["controls"] = k;
		out["made"] = json::array();
		for (const int64_t n : made) {
			out["made"].push_back(n);
		}
		return out;
	}
	// parts, with the wires and the rigs from them: the lab's, or the trash's (Stage 34)
	static json links_json(const std::vector<Part> &parts, const std::vector<Wire> &wires, const std::vector<Rig> &rigs, bool exact) {
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
			if (p.kind == PartKind::Notice || p.kind == PartKind::Writer) {
				j["level"] = level_name(static_cast<Notice::Level>(p.notice_level));
				j["about"] = p.quantity.item;
			}
			if (p.kind == PartKind::Writer) {
				j["says"] = p.says;
				if (p.writes != 0) {
					j["writes"] = writes_name(p.writes);
				}
			}
			if (p.kind == PartKind::Notice && (!p.contains.empty() || p.hold != 1 || p.held != 0)) { // Stage 28
				j["contains"] = p.contains;
				j["hold"] = p.hold;
				j["held"] = p.held;
			}
			if (p.kind == PartKind::Controller) {
				j["watching"] = p.watching;
				j["quantity"] = { { "item", p.quantity.item }, { "what", p.quantity.what }, { "measure", p.quantity.measure } };
				j["target"] = num(p.target);
				j["speed"] = p.speed;
				j["tuned"] = p.tuned;
				j["process"] = { num(p.gain), num(p.tau), num(p.theta) };
				j["gains"] = { num(p.kc), num(p.ti) };
				j["period"] = p.period;
				j["state"] = { num(p.integral), num(p.level), num(p.duty) };
				j["pwm"] = { p.phase, p.on_steps };
				j["pulse"] = p.pulse;
				if (p.tuned) { // Stage 28: what it was tuned on, and how long it has been held at a limit
					j["basis"] = { num(p.basis_g), num(p.basis_cp), p.basis_has };
					j["pinned"] = { p.pinned, p.pinned_said };
				}
			} else if (p.kind == PartKind::Monitor) {
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
			if (x.follow) {
				r.back()["follow"] = true;
			}
		}
		out["rigs"] = r;
		return out;
	}

	// a save's signals, back
	bool signals_from(const json &j, std::string &why) {
		if (!links_from(j, parts, wires, rigs, why)) {
			return false;
		}
		for (const json &x : j.at("controls")) {
			ControlSetting k;
			k.item = x.at("item").get<std::string>();
			k.control = x.at("control").get<std::string>();
			k.by = x.at("by").get<std::string>();
			k.step = x.at("step").get<int64_t>();
			if (!from_bits(x.at("value"), k.value)) {
				why = "the save's signals hold a number that is not one";
				return false;
			}
			controls.push_back(k);
		}
		for (size_t k = 0; k < static_cast<size_t>(PART_KINDS) && k < j.at("made").size(); ++k) {
			made[k] = j.at("made").at(k).get<int64_t>();
		}
		return true;
	}
	// a save's parts, wires and rigs, back: the lab's, or the trash's (Stage 34)
	static bool links_from(const json &j, std::vector<Part> &parts, std::vector<Wire> &wires, std::vector<Rig> &rigs, std::string &why) {
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
			if (p.kind == PartKind::Notice || p.kind == PartKind::Writer) {
				Notice::Level level = Notice::Warning;
				level_of(x.value("level", std::string("warning")), level);
				p.notice_level = static_cast<int>(level);
				p.quantity.item = x.value("about", std::string());
				p.says = x.value("says", std::string());
				writes_of(x.value("writes", std::string("on")), p.writes);
				p.contains = x.value("contains", std::string());
				p.hold = x.value("hold", int64_t{ 1 });
				p.held = x.value("held", int64_t{ 0 });
			}
			p.own = x.at("own").get<bool>();
			p.out = x.at("out").get<bool>();
			if (p.kind == PartKind::Controller) {
				p.watching = x.at("watching").get<bool>();
				const json &q = x.at("quantity");
				p.quantity = Quantity{ q.at("item").get<std::string>(), q.at("what").get<std::string>(), q.at("measure").get<std::string>() };
				p.speed = x.at("speed").get<int>();
				p.tuned = x.at("tuned").get<bool>();
				p.period = x.at("period").get<int64_t>();
				p.phase = x.at("pwm").at(0).get<int64_t>();
				p.on_steps = x.at("pwm").at(1).get<int64_t>();
				p.pulse = x.at("pulse").get<bool>();
				if (x.contains("basis") && (!exact(x.at("basis").at(0), p.basis_g) || !exact(x.at("basis").at(1), p.basis_cp))) {
					why = "the save holds a controller that cannot be read";
					return false;
				}
				p.basis_has = x.contains("basis") ? x.at("basis").at(2).get<uint64_t>() : 0;
				p.pinned = x.contains("pinned") ? x.at("pinned").at(0).get<int64_t>() : 0;
				p.pinned_said = x.contains("pinned") && x.at("pinned").at(1).get<bool>();
				if (!exact(x.at("target"), p.target) || !exact(x.at("process").at(0), p.gain) || !exact(x.at("process").at(1), p.tau) || !exact(x.at("process").at(2), p.theta) ||
						!exact(x.at("gains").at(0), p.kc) || !exact(x.at("gains").at(1), p.ti) || !exact(x.at("state").at(0), p.integral) || !exact(x.at("state").at(1), p.level) ||
						!exact(x.at("state").at(2), p.duty)) {
					why = why.empty() ? "the save holds a controller that cannot be read" : why;
					return false;
				}
			} else if (p.kind == PartKind::Monitor) {
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
			r.follow = x.value("follow", false);
			if (!maybe(x.at("on"), r.on) || !maybe(x.at("off"), r.off) || !maybe(x.at("any"), r.any)) {
				return false;
			}
			rigs.push_back(r);
		}
		return true;
	}

	// ---- the trash (GAME_SPEC §13, Stage 34)

	// in the trash: an item, a part, or a part in a circuit that is
	bool in_trash(const std::string &id) const {
		const int i = find(id);
		return (i >= 0 && items[static_cast<size_t>(i)].place == Place::Trash) || find_binned(id) >= 0;
	}
	// its place in the trash's history; -1 for what is not an element of it
	int trash_at(const std::string &id) const {
		for (size_t k = 0; k < trash.size(); ++k) {
			if (trash[k].id == id) {
				return static_cast<int>(k);
			}
		}
		return -1;
	}
	// the first of a command's names that is in the trash
	bool names_trashed(const json &c, std::string &id) const {
		for (const char *key : { "item", "vessel", "burner", "from", "to", "over", "of", "part", "timer", "alarm", "circuit", "in" }) {
			if (c.contains(key) && c.at(key).is_string() && in_trash(c.at(key).get<std::string>())) {
				id = c.at(key).get<std::string>();
				return true;
			}
		}
		for (const char *key : { "items", "parts" }) {
			for (const json &name : c.contains(key) && c.at(key).is_array() ? c.at(key) : json::array()) {
				if (name.is_string() && in_trash(name.get<std::string>())) {
					id = name.get<std::string>();
					return true;
				}
			}
		}
		return false;
	}
	// A part into the trash, with all it holds: out of the lab's parts, its settings kept. Its wires and rigs come
	// off the lab and are kept beside it, those to anything outside it too (Stage 38: until then those were taken
	// off for good, as `scrap` takes them). It signals nothing there: its signal off, an alarm quiet, a monitor's
	// last reading forgotten, a controller untuned (its rigs are off, as after `unrig`); a timer keeps what is
	// left of it.
	void bin_part(const std::string &id) {
		const std::vector<std::string> held = held_by(parts, id);
		const auto inside = [&](const std::string &name) { return std::find(held.begin(), held.end(), name) != held.end(); };
		for (const Wire &w : wires) {
			if (inside(w.from) || inside(w.to)) {
				binned_wires.push_back(w);
			}
		}
		for (Rig r : rigs) {
			if (inside(r.from) || (r.control == "reset" && inside(r.item))) {
				r.seen = false; // its source's signal is off: it acts from its next change
				binned_rigs.push_back(r);
			}
		}
		std::erase_if(wires, [&](const Wire &w) { return inside(w.from) || inside(w.to); });
		std::erase_if(rigs, [&](const Rig &r) { return inside(r.from) || (r.control == "reset" && inside(r.item)); });
		std::erase_if(controls, [&](const ControlSetting &s) { return inside(s.item); });
		for (const std::string &name : held) {
			Part p = parts[static_cast<size_t>(find_part(name))];
			p.own = p.out = p.ringing = false;
			p.held = 0;
			p.last.reset();
			if (p.kind == PartKind::Controller) {
				drop_trial(name); // (Stage 39)
				retune(p, false);
				p.trying = false;
				p.gain = p.tau = p.theta = 0.0;
				p.pinned = 0;
				p.pinned_said = false;
			}
			binned.push_back(p);
			std::erase_if(parts, [&](const Part &q) { return q.id == name; });
		}
	}
	// A part out of the trash, with all it holds: among the lab's parts again (the last of them). With
	// `scrapped`, gone for good instead. Then every wire and rig kept in the trash whose two ends are on the lab
	// again comes back (Stage 38): the part's own, and those to what was outside it, now or once that is
	// restored too; those to what is gone go with it. A controller rigged again tunes itself, as when it is rigged.
	void unbin_part(const std::string &id, bool scrapped) {
		const std::vector<std::string> held = held_by(binned, id);
		for (const std::string &name : held) {
			if (!scrapped) {
				parts.push_back(binned[static_cast<size_t>(find_binned(name))]);
			}
			std::erase_if(binned, [&](const Part &q) { return q.id == name; });
		}
		// (an end that is nowhere, on the lab or in the trash, is gone)
		const auto live = [&](const std::string &name) { return find_part(name) >= 0 || (find(name) >= 0 && !in_trash(name)); };
		const auto gone = [&](const std::string &name) { return find_part(name) < 0 && find_binned(name) < 0 && find(name) < 0; };
		std::vector<int> tunes;
		for (const Wire &w : binned_wires) {
			if (live(w.from) && live(w.to)) {
				wires.push_back(w);
			}
		}
		for (const Rig &r : binned_rigs) {
			if (live(r.from) && live(r.item)) {
				rigs.push_back(r);
				const int from = find_part(r.from);
				if (r.follow && std::find(tunes.begin(), tunes.end(), from) == tunes.end()) {
					tunes.push_back(from);
				}
			}
		}
		std::erase_if(binned_wires, [&](const Wire &w) { return (live(w.from) && live(w.to)) || gone(w.from) || gone(w.to); });
		std::erase_if(binned_rigs, [&](const Rig &r) { return (live(r.from) && live(r.item)) || gone(r.from) || gone(r.item); });
		for (const int at : tunes) {
			if (parts[static_cast<size_t>(at)].watching) {
				tune(at);
			}
		}
	}
	// an element leaves the trash's history
	void forget_trash(const std::string &id) {
		std::erase_if(trash, [&](const Binned &b) { return b.id == id; });
	}
	// Past its capacity the oldest leaves the trash for good: an item to the cabinet (it holds matter, which
	// nothing destroys), a part scrapped with all it holds.
	void trim_trash() {
		while (trash.size() > static_cast<size_t>(TRASH_MOST)) {
			const Binned oldest = trash.front();
			trash.erase(trash.begin());
			if (oldest.part) {
				unbin_part(oldest.id, true);
				note(Notice::Detail, "", "the trash is full: " + oldest.id + " left it for good");
			} else {
				items[static_cast<size_t>(find(oldest.id))].place = Place::Cabinet;
				note(Notice::Detail, oldest.id, "the trash is full: " + oldest.id + " went to the cabinet");
			}
		}
	}
	// the trash as JSON: its history, and the parts in it with what is kept beside them
	json trash_json(bool exact) const {
		json out = links_json(binned, binned_wires, binned_rigs, exact);
		json list = json::array();
		for (const Binned &b : trash) {
			list.push_back({ { "id", b.id }, { "part", b.part }, { "at", { b.col, b.row } } });
		}
		out["list"] = list;
		return out;
	}
	bool trash_from(const json &j, std::string &why) {
		for (const json &x : j.at("list")) {
			trash.push_back(Binned{ x.at("id").get<std::string>(), x.at("part").get<bool>(), x.at("at").at(0).get<int>(), x.at("at").at(1).get<int>() });
		}
		if (!links_from(j, binned, binned_wires, binned_rigs, why)) {
			return false;
		}
		for (const Binned &b : trash) {
			const int i = find(b.id);
			if (b.part ? find_binned(b.id) < 0 : i < 0 || items[static_cast<size_t>(i)].place != Place::Trash) {
				why = "the save's trash holds \"" + b.id + "\", which is not in it";
				return false;
			}
		}
		return true;
	}

	// ---- drains (§6, §13): a vessel under another's outlet leaves it when either is moved
	// away, stowed or handed in; the drain comes off at a step boundary
	// the volume of a cell's liquid species, m^3 (what a pour's amount is of)
	double liquid_m3(CellId cell) const { return liquid_m3(world().snapshot_cell(cell)); }
	double liquid_m3(const CellState &state) const {
		double volume = 0.0;
		for (size_t s = 0; s < state.amount.size(); ++s) {
			const SpeciesDef &def = table().species(SpeciesId{ static_cast<int32_t>(s) });
			if (state.amount[s] > 0 && def.phase == Phase::Liquid && !def.wall) {
				volume += static_cast<double>(state.amount[s]) * MOL_PER_QUANTUM * def.molar_volume;
			}
		}
		return volume;
	}
	// A vessel into a pouring position over another (GAME_SPEC §13, Stage 27): held, or upside down with its cap
	// shut; a drain from it into the other, its rate set each step
	bool start_pour(int thing, int over, bool inverted, std::string &why) {
		Item &x = items[static_cast<size_t>(thing)];
		const Item &t = items[static_cast<size_t>(over)];
		if (thing == over) {
			why = "a vessel is not poured into itself";
			return false;
		}
		if (x.kind != Kind::Vessel || t.kind != Kind::Vessel || glass::find(x.glassware, x.nominal_ml) == nullptr) {
			why = "a vessel made as glassware pours, into a vessel";
			return false;
		}
		if (x.place == Place::Outbox) { // from the bench, or straight from the cabinet, as a burner or a balloon is used
			why = "\"" + x.id + "\" has been handed in";
			return false;
		}
		if (!on_bench(t, why)) {
			return false;
		}
		if (x.holds >= 0 || x.drains_to >= 0) {
			why = "\"" + x.id + "\" has \"" + items[static_cast<size_t>(x.holds >= 0 ? x.holds : x.drains_to)].id + "\" on it or under it: take it away first";
			return false;
		}
		for (const Item &b : items) {
			if (inverted && (b.kind == Kind::Burner || stands_under(b.kind)) && b.under == thing) { // a held pour lifts it a moment; upside down it would stand there
				why = "\"" + x.id + "\" stands over \"" + b.id + "\": take it away first";
				return false;
			}
		}
		if (t.pours_into == thing) {
			why = "\"" + t.id + "\" is pouring into \"" + x.id + "\"";
			return false;
		}
		if (sealed(t) || (sealed(x) && !x.screwed)) { // nothing pours in or out of what is sealed (Stage 28)
			why = "\"" + (sealed(t) ? t.id : x.id) + "\" is sealed: open it first";
			return false;
		}
		if (!end_pour(static_cast<size_t>(thing), why) || !boundary(why)) {
			return false;
		}
		if (x.screwed) { // a capped bottle tipped over another: its cap off first (upside down, the cap is the orifice)
			world().connect(x.cell, air, PHASES_GAS, &x.mouth);
			x.screwed = false;
		}
		EdgeId edge;
		if (world().connect_drain(x.cell, t.cell, &edge) != Refusal::None) {
			why = "\"" + x.id + "\" cannot pour into \"" + t.id + "\"";
			return false;
		}
		world().set_drain(edge, 0.0, false);
		x.pours_into = over;
		x.pour = edge;
		x.inverted = inverted;
		x.tilt = x.cap = 0.0;
		forget(x.id, "cap"); // the cap it has now is another control (Stage 28)
		return true;
	}

	// A pour ends (GAME_SPEC §13, Stage 27): its drain off, the vessel upright on its own square again
	bool end_pour(size_t u, std::string &why) {
		Item &x = items[u];
		if (x.pours_into < 0) {
			return true;
		}
		if (!boundary(why)) {
			return false;
		}
		world().disconnect(x.pour);
		x.pours_into = -1;
		x.landed = 0;
		x.pour = EdgeId{};
		x.inverted = false;
		x.tilt = x.cap = 0.0;
		forget(x.id, "cap");
		return true;
	}
	bool end_pours(int thing, std::string &why) {
		for (size_t u = 0; u < items.size(); ++u) {
			if (items[u].pours_into >= 0 && (static_cast<int>(u) == thing || items[u].pours_into == thing) && !end_pour(u, why)) {
				return false;
			}
		}
		return true;
	}
	double solid_m3(CellId cell) const { return solid_m3(world().snapshot_cell(cell)); }
	double solid_m3(const CellState &state) const {
		double volume = 0.0;
		for (size_t s = 0; s < state.amount.size(); ++s) {
			const SpeciesDef &def = table().species(SpeciesId{ static_cast<int32_t>(s) });
			if (state.amount[s] > 0 && def.phase == Phase::Solid && !def.wall) {
				volume += static_cast<double>(state.amount[s]) * MOL_PER_QUANTUM * def.molar_volume;
			}
		}
		return volume;
	}
	// The share of a vessel's solids its liquid holds up (the rest lies as sediment), and its vortex's share of
	// the largest that fits (GAME_SPEC §13)
	double suspended(const Item &x) const { return x.suspended; }
	// (g: the liquid with what is dissolved in it)
	double liquid_g(CellId cell) const { return liquid_g(world().snapshot_cell(cell)); }
	double liquid_g(const CellState &state) const {
		double g = 0.0;
		for (size_t s = 0; s < state.amount.size(); ++s) {
			const SpeciesId id{ static_cast<int32_t>(s) };
			const SpeciesDef &def = table().species(id);
			if (state.amount[s] > 0 && !def.wall && (def.phase == Phase::Liquid || def.phase == Phase::Aqueous)) {
				g += static_cast<double>(state.amount[s]) * MOL_PER_QUANTUM * static_cast<double>(table().molar_mass_ug(id)) * 1e-6;
			}
		}
		return g;
	}
	// what lies in a vessel as the contents' model takes it: the liquid with what is dissolved and suspended in
	// it, and the sediment's own volume, cm^3
	void lying_in(const Item &x, double &mixture, double &sediment) const {
		const Fill held = fill(x);
		const double condensed = held.defined ? held.condensed * 1e6 : 0.0, solid = solid_m3(x) * 1e6;
		sediment = (1.0 - x.suspended) * solid;
		mixture = liquid_m3(x) > 0.0 ? std::max(condensed - sediment, 0.0) : 0.0;
	}
	double turn_of(const Item &x) const {
		const glass::Vessel *g = glass::find(x.glassware, x.nominal_ml);
		return g != nullptr && x.pours_into >= 0 && !x.inverted ? x.tilt * contents::full_tilt(*g, 1) : x.pours_into >= 0 && x.inverted ? PI : 0.0;
	}
	contents::Swirl swirl_of(const Item &x) const {
		const glass::Vessel *g = glass::find(x.glassware, x.nominal_ml);
		if (g == nullptr || !(x.swirl > 0.0)) {
			return contents::Swirl{};
		}
		double mixture = 0.0, sediment = 0.0;
		lying_in(x, mixture, sediment);
		return contents::swirl(*g, turn_of(x), mixture, sediment, liquid_g(x), x.swirl);
	}
	// (worked out again when what it holds has changed (its signature, set_stirring's), or what it holds up by a
	// thousandth, or its energy by a hundredth; between, its share goes as the cube root of its energy, as it
	// does where the depth under it is the same. Stage 36: a turning liquid's energy changes at every step, and a
	// share is a dozen searches of how the contents lie)
	double vortex(const Item &x) const {
		if (!(x.swirl > 0.0)) {
			return 0.0;
		}
		if (!(x.share_energy > 0.0) || std::fabs(x.swirl - x.share_energy) > 1e-2 * x.share_energy || x.swirl_for != x.share_for || !(std::fabs(x.suspended - x.share_held_up) <= 1e-3)) {
			x.share = swirl_of(x).share;
			x.share_energy = x.swirl;
			x.share_for = x.swirl_for;
			x.share_held_up = x.suspended;
		}
		return x.share < 1.0 ? std::min(1.0, x.share * std::cbrt(x.swirl / x.share_energy)) : 1.0;
	}
	// A stirred liquid's turning, each step (GAME_SPEC §11, Stage 32). The stirrer does work on the liquid, which
	// the mains give (the ledger's entry: it is heat in the liquid once the turning has died down, and the liquid
	// is taken to have it at once). The turning's energy: dE/dt = P - E / tau, never more than the liquid can
	// hold. What the turning holds up: grains fall through the liquid at Stokes' speed, and holding them up takes
	// the power they would give up falling, their weight in the liquid times that speed; the turning has its
	// dissipation, E / tau, to give, and holds up that share of them. Fine powder stays up in a liquid barely
	// turning; coarse heavy grains ask for more than a stirrer gives. Let go, they settle at their speed.
	void set_stirring() {
		static const double keeps = std::exp(-chem::STEP_SECONDS / SWIRL_SECONDS);
		for (Item &x : items) {
			// (most vessels, most steps: not stirred, at rest, nothing to do)
			const bool working = x.stirs.index >= 0 && world().edges()[static_cast<size_t>(x.stirs.index)].power != 0.0;
			if (x.kind != Kind::Vessel || (!x.stirred && !working && !(x.swirl > 0.0) && !(x.suspended > 0.0))) {
				continue;
			}
			const bool turning = x.stirred && liquid_m3(x) > 0.0;
			if (x.stirs.index >= 0 && turning != working) {
				world().set_power(x.stirs, turning ? STIR_WATTS : 0.0);
			}
			if (!turning && !(x.swirl > 0.0) && !(x.suspended > 0.0)) {
				continue;
			}
			const glass::Vessel *g = glass::find(x.glassware, x.nominal_ml);
			const Fill held = fill(x);
			const double condensed = held.defined ? held.condensed * 1e6 : 0.0;
			const double solid = solid_m3(x) * 1e6, liquid = liquid_m3(x) * 1e6;
			if (!(liquid > 0.0)) {
				x.swirl = 0.0;
				x.suspended = 0.0;
				world().set_stirring(x.cell, 0.0);
				continue;
			}
			// what the liquid can hold, and what its solids take: worked out when it holds something else (within
			// a part in a thousand), and when it is tilted
			// (a signature of the three: each part in two hundred of its volume, each fiftieth of its solids, each
			// thousandth of a radian is another)
			const double holds = 1.0 + std::floor(200.0 * std::log(std::max(condensed, 1e-9))) + 1e4 * std::floor(50.0 * std::log(std::max(solid, 1e-9))) + 1e9 * std::floor(1000.0 * turn_of(x));
			if (holds != x.swirl_for) {
				x.swirl_for = holds;
				const double mass = liquid_g(x), density = mass / std::max(condensed - solid, 1e-9); // g/cm^3
				double depth = 0.0; // cm, of liquid under the surface's middle
				if (g != nullptr) {
					const contents::Swirl most = contents::swirl(*g, turn_of(x), std::max(condensed - solid, 0.0), 0.0, mass, 0.0);
					x.swirl_most = most.most;
					depth = most.depth;
				} else {
					depth = std::cbrt(condensed); // a vessel made to no shape: as deep as it is wide
					x.swirl_most = mass * 1e-3 * GRAVITY * depth * 1e-2 / 3.0;
				}
				// its solids: the power that holds them up (their weight in the liquid times the speed they fall at),
				// and how fast they settle through the liquid's depth
				x.swirl_lifts = 0.0;
				x.settles = 0.0;
				const CellState &state = world().snapshot_cell(x.cell);
				double fall = 0.0, weight = 0.0; // m/s, by each solid's volume
				for (size_t s = 0; s < state.amount.size(); ++s) {
					const SpeciesId id{ static_cast<int32_t>(s) };
					const SpeciesDef &def = table().species(id);
					if (state.amount[s] <= 0 || def.wall || def.phase != Phase::Solid || !(def.molar_volume > 0.0)) {
						continue;
					}
					const double volume = static_cast<double>(state.amount[s]) * MOL_PER_QUANTUM * def.molar_volume; // m^3
					const double heavier = std::max(static_cast<double>(table().molar_mass_ug(id)) * 1e-9 / def.molar_volume - density * 1e3, 0.0); // kg/m^3
					const double grain = particle_diameter(table(), state, id); // m
					const double falls = heavier * GRAVITY * grain * grain / (18.0 * WATER_VISCOSITY); // m/s (Stokes)
					x.swirl_lifts += heavier * GRAVITY * volume * falls;
					fall += volume * falls;
					weight += volume;
				}
				x.settles = weight > 0.0 && depth > 0.0 ? (fall / weight) / (depth * 1e-2) : 0.0;
			}
			x.swirl = std::min(x.swirl * keeps + (turning ? STIR_WATTS * SWIRL_SECONDS * (1.0 - keeps) : 0.0), x.swirl_most);
			if (x.swirl < 1e-9) {
				x.swirl = 0.0;
			}
			// what the turning dissipates, which mixes a layered column as far as it pays (§6, Stage 49)
			world().set_stirring(x.cell, x.swirl / SWIRL_SECONDS);
			if (!(solid > 0.0)) {
				x.suspended = 0.0;
				continue;
			}
			const double gives = x.swirl / SWIRL_SECONDS; // W: what the turning dissipates
			const double holds_up = x.swirl_lifts > 0.0 ? std::min(1.0, gives / x.swirl_lifts) : (x.swirl > 0.0 ? 1.0 : 0.0);
			if (holds_up > x.suspended) {
				x.suspended += (holds_up - x.suspended) * (1.0 - keeps); // stirred up as fast as the liquid turns
				if (holds_up - x.suspended < 1e-3) {
					x.suspended = holds_up; // (the last thousandth: there at once)
				}
			} else {
				x.suspended = std::max(holds_up, x.suspended - x.settles * chem::STEP_SECONDS);
			}
			if (x.suspended < 1e-6) {
				x.suspended = 0.0; // (the last of a haze: nothing to see, and the sediment is whole again)
			}
		}
	}
	// A pour by hand (GAME_SPEC §13, Stage 31), from what lies in the vessel at its tilt (lab/contents.hpp): what
	// stands over the lip runs over the brim as over a weir, and never more in a step than stands over the lip.
	// The liquid takes with it what is dissolved and what is suspended in it; the sediment runs by the same law
	// once its flat section stands over the lip. m^3/s: of the liquid's own volume, and of the solids' own.
	struct ByHand {
		double turn = 0.0, head_cm = 0.0, liquid = 0.0, carried = 0.0, solids = 0.0, runs_ml_s = 0.0;
	};
	ByHand by_hand(const Item &x, const glass::Vessel &g) const {
		ByHand p;
		p.turn = x.tilt * contents::full_tilt(g, 1);
		const Fill held = fill(x);
		if (!held.defined) {
			return p;
		}
		const double condensed = held.condensed * 1e6, solid = solid_m3(x) * 1e6, share = suspended(x);
		const double sediment = (1.0 - share) * solid, mixture = std::max(condensed - sediment, 0.0);
		if (!(sediment > 0.0) && !(x.swirl > 0.0)) {
			// liquid alone, at rest: from the vessel's table at this turn (Stage 33; contents::plain_pour), so that a
			// step's cost is a lookup and a frame's does not grow with the clock's speed
			const contents::PlainPour plain = contents::plain_pour(g, p.turn, mixture, 1);
			p.head_cm = mixture > 0.0 ? plain.head : 0.0;
			if (plain.head > 0.0 && mixture > 0.0) {
				const double over = std::max(mixture - plain.lip_holds, 0.0);
				p.runs_ml_s = std::min(contents::over_brim(plain.brim, p.turn, plain.head), over / chem::STEP_SECONDS);
				p.liquid = p.runs_ml_s * (liquid_m3(x) * 1e6 / mixture) * 1e-6;
				p.carried = share;
			}
			return p;
		}
		// How the contents lie is worked out for the liquid to the nearest 256th of the vessel's capacity, the
		// sediment to the nearest 1024th and the vortex to the nearest 256th of its share (Stage 36): between, the
		// liquid's surface stands over it by what is more over the surface's area, and what is more of either runs.
		// A step's cost is then most often a lookup while a pour runs, and the answer is what the contents are,
		// whatever came before.
		// (the last sixteenth of the vessel's capacity of liquid, and of a sixteenth of that of sediment, exactly:
		// where little is left the anchor would be as much again as what lies there)
		const double unit = g.capacity() / 256.0;
		const double near_mixture = mixture < 16.0 * unit ? mixture : std::round(mixture / unit) * unit;
		const double near_sediment = sediment < 4.0 * unit ? sediment : std::round(sediment / (unit / 4.0)) * (unit / 4.0);
		const double near_vortex = x.swirl > 0.0 ? std::round(256.0 * vortex(x)) / 256.0 : 0.0;
		const contents::Lying l = contents::lying(g, p.turn, near_mixture, near_sediment, near_vortex, 1);
		const double more = l.liquid && l.surface_area > 0.0 ? (mixture - near_mixture) / l.surface_area : 0.0; // cm, up
		const double head = l.head + more;
		p.head_cm = l.liquid ? head : 0.0;
		const double lip_holds = contents::below(g, p.turn, l.lip_level), brim = std::fabs(l.lip_x);
		if (l.liquid && head > 0.0 && mixture > 0.0) {
			const double over = std::max(contents::below(g, p.turn, l.liquid_level) - lip_holds + (mixture - near_mixture), 0.0);
			p.runs_ml_s = std::min(contents::over_brim(brim, p.turn, head), over / chem::STEP_SECONDS);
			p.liquid = p.runs_ml_s * (liquid_m3(x) * 1e6 / mixture) * 1e-6; // (the drain's rate is of the liquid itself)
			p.carried = share;
		}
		if (l.solid && l.solid_head > 0.0) {
			const double over = std::max(contents::below(g, p.turn, l.solid_level) - lip_holds + (sediment - near_sediment) / contents::PACKING, 0.0);
			p.solids = contents::PACKING * std::min(contents::over_brim(brim, p.turn, l.solid_head), over / chem::STEP_SECONDS) * 1e-6;
		}
		return p;
	}
	// Whether anything can run over a held vessel's lip at its tilt: not while all that is in it is less than
	// the vessel holds below its lip there (Stage 33). The lab asks at every step, 300 of them a frame at 600
	// times the clock's speed, and how the contents lie is a search of sixty volumes: a vessel held turned and
	// not yet pouring ran the screen at ten frames a second. What it holds below its lip is one volume, the same
	// at every step of a frame (kept for the tilt it was asked at). A stirred liquid stands higher at the wall
	// than its volume says, and is asked in full; so is one within a part in a thousand million of its lip.
	bool may_run_over(const Item &x, const glass::Vessel &g) const {
		if (x.swirl > 0.0) {
			return true;
		}
		const Fill held = fill(x); // (the whole vessel: a layered one's column, Stage 47)
		if (!held.defined) {
			return false; // (as by_hand: nothing runs)
		}
		struct Held {
			const glass::Vessel *vessel = nullptr;
			double turn = 0.0, holds = 0.0;
		};
		thread_local Held last;
		const double turn = x.tilt * contents::full_tilt(g, 1);
		if (last.vessel != &g || last.turn != turn) {
			last = Held{ &g, turn, contents::held_at(g, turn, 1) };
		}
		return held.condensed * 1e6 >= last.holds * (1.0 - 1.0e-9);
	}
	// a pour's rate this step: a held one's by what stands over its lip at its tilt, a script's the vessel's own,
	// an upside-down one's an orifice's under its head
	void set_pours() {
		// what lands as a layer moves after every drain's rate is set (a drain's rate is set
		// before the step's first transfer)
		std::vector<int> lay;
		for (Item &x : items) {
			if (x.pours_into < 0) {
				continue;
			}
			const glass::Vessel *g = glass::find(x.glassware, x.nominal_ml);
			double rate = 0.0, solids = 0.0, solids_rate = 0.0;
			if (g != nullptr && !x.inverted && x.pour_left_m3 >= 0.0) {
				rate = x.tilt * g->pour_ml_s * 1e-6; // a script's pour: an amount at the vessel's rate, decanted
			} else if (g != nullptr && !x.inverted && !may_run_over(x, *g)) {
				rate = 0.0; // (nothing stands over its lip: what by_hand would say, without the search)
			} else if (g != nullptr && !x.inverted) {
				const ByHand p = by_hand(x, *g);
				rate = p.liquid;
				solids = p.carried;
				solids_rate = p.solids;
			} else if (g != nullptr) {
				const Fill held = fill(x);
				const double head_m = held.defined ? g->head_inverted(held.condensed * 1e6) / 100.0 : 0.0;
				const double bore_m = g->cap_bore / 100.0;
				rate = x.cap * ORIFICE_CD * (PI * bore_m * bore_m / 4.0) * std::sqrt(2.0 * GRAVITY * head_m);
				solids = 1.0; // upside down, what rests lies on the cap
			}
			const double nominal = rate; // (what it pours at, not its last step's remainder)
			if (x.pour_left_m3 >= 0.0) {
				rate = std::min(rate, x.pour_left_m3 / chem::STEP_SECONDS); // a scripted pour's last step: exactly what is left
			}
			// what it pours lands as a layer of its own on the liquid there (Stage 47; Stage 51: every pour, its stream
			// then mixing in what it entrains, §6)
			(void)nominal;
			if (rate > 0.0 && x.landed == 0) {
				lay.push_back(x.pours_into);
				x.landed = 1;
			}
			world().set_drain(x.pour, rate, solids, solids_rate);
		}
		for (const int t : lay) {
			push_surface(items[static_cast<size_t>(t)]);
		}
		set_landings();
	}
	// The speed what runs into each vessel lands at (Stage 51): a stream falls from a lip, an outlet or a cap held
	// LIP_GAP over its mouth to its liquid's surface; a gentle pour runs down the wall and lands at none
	void set_landings() {
		const auto fall = [&](const Item &t) {
			const glass::Vessel *g = glass::find(t.glassware, t.nominal_ml);
			if (g == nullptr) {
				return 0.0;
			}
			const Fill held = fill(t);
			const double level = held.defined ? g->level(held.condensed * 1e6) : g->floor;
			return std::sqrt(2.0 * GRAVITY * (std::max(g->height - level, 0.0) / 100.0 + LIP_GAP));
		};
		std::vector<double> speed(items.size(), 0.0);
		for (const Item &x : items) {
			if (x.kind != Kind::Vessel) {
				continue;
			}
			if (x.pours_into >= 0) {
				speed[static_cast<size_t>(x.pours_into)] = std::max(speed[static_cast<size_t>(x.pours_into)], x.gentle ? 0.0 : fall(items[static_cast<size_t>(x.pours_into)]));
			}
			if (x.drains_to >= 0) {
				speed[static_cast<size_t>(x.drains_to)] = std::max(speed[static_cast<size_t>(x.drains_to)], fall(items[static_cast<size_t>(x.drains_to)]));
			}
		}
		for (size_t i = 0; i < items.size(); ++i) {
			if (items[i].kind == Kind::Vessel && world().meta(items[i].cell).landing != speed[i]) {
				world().set_landing(items[i].cell, speed[i]);
			}
		}
	}

	// the column's edge from a layer up to the cell over it
	EdgeId column_edge(CellId lower, CellId upper) const {
		const std::span<const Edge> edges = world().edges();
		for (size_t i = 0; i < edges.size(); ++i) {
			if (edges[i].open && edges[i].column && edges[i].a == lower && edges[i].b == upper) {
				return EdgeId{ static_cast<int32_t>(i) };
			}
		}
		return EdgeId{};
	}
	// A vessel's liquid pushed down a place (Stage 47), so that what lands on it next is a layer of its own: its
	// layers each down one (the lowest must be spare), then its own cell's liquid, with what is dissolved and lies in
	// it, into the layer under it. Nothing to push, or no spare layer: nothing is done.
	void push_surface(Item &t) {
		if (!(world().meta(t.cell).column_volume > 0.0) || !(liquid_m3(t.cell) > 0.0)) {
			return;
		}
		std::vector<CellId> layers = layers_of(t);
		if (layers.empty() || layered_cell(layers.back())) {
			// no spare layer: the column grows a block (Stage 47: as many as it needs, a few at a time)
			CellId bottom = layers.empty() ? t.cell : layers.back();
			for (int k = 0; k < COLUMN_BLOCK; ++k) {
				bottom = world().add_layer_under(bottom);
			}
			layers = layers_of(t);
		}
		const size_t n = layers.size();
		const auto all_of = [&](CellId from, CellId to, EdgeId edge) {
			const CellState &c = world().snapshot_cell(from);
			std::vector<SpeciesAmount> amounts;
			for (size_t s = 0; s < c.amount.size(); ++s) {
				if (c.amount[s] != 0) {
					amounts.push_back(SpeciesAmount{ SpeciesId{ static_cast<int32_t>(s) }, Amount{ c.amount[s] } });
				}
			}
			if (!amounts.empty()) {
				const Refusal refused = world().transfer(edge, from, amounts, Energy{ c.energy });
				CHEM_ASSERT(refused == Refusal::None, refusal_text(refused));
			}
			(void)to;
		};
		for (size_t k = n - 1; k >= 1; --k) {
			if (layered_cell(layers[k - 1])) {
				all_of(layers[k - 1], layers[k], column_edge(layers[k], layers[k - 1]));
			}
		}
		const CellState &top = world().snapshot_cell(t.cell);
		std::vector<SpeciesAmount> down;
		for (size_t s = 0; s < top.amount.size(); ++s) {
			const SpeciesDef &def = table().species(SpeciesId{ static_cast<int32_t>(s) });
			if (top.amount[s] > 0 && !def.wall && def.phase != Phase::Gas) {
				down.push_back(SpeciesAmount{ SpeciesId{ static_cast<int32_t>(s) }, Amount{ top.amount[s] } });
			}
		}
		const Refusal refused = world().transfer(column_edge(layers[0], t.cell), t.cell, down, enthalpy_carried(table(), down, kelvin(t.cell)));
		CHEM_ASSERT(refused == Refusal::None, refusal_text(refused));
		mid_step = true; // (moved before the step: a trace begun after it cannot start where the last one ended)
	}
	// By instruments, each vessel's thermometer follows it a step (Stage 52): first order, its probe's time constant
	void follow_thermometers() {
		if (!instruments) {
			return;
		}
		const double tau = instruments::thermometer().time_constant_s;
		for (Item &x : items) {
			if (x.kind != Kind::Vessel || x.burst) {
				continue;
			}
			const double t = kelvin(x);
			x.read_kelvin = std::isnan(x.read_kelvin) ? t : instruments::lagged(x.read_kelvin, t, chem::STEP_SECONDS, tau);
		}
	}

	// The true volume at a piece's mark (Stage 52): its nominal, off by its class's tolerance as drawn from the seed
	double mark_ml(const Item &x) const {
		const glass::Vessel *g = x.glassware.empty() ? nullptr : glass::find(x.glassware, x.nominal_ml);
		return g == nullptr || !g->mark ? 0.0 : g->nominal_ml + g->tolerance_ml * instruments::share(seed, x.id + ":mark");
	}

	// A vessel with an outlet at its foot on another's mouth (Stage 52) drains into it through its tip, as far open as its
	// stopcock: Torricelli's under its liquid's head over the tip, through a sharp-edged orifice of the tip's bore
	void set_outlets() {
		for (Item &x : items) {
			const glass::Vessel *g = outlet_of(x);
			if (g == nullptr || x.drains_to < 0 || x.place != Place::On) {
				continue;
			}
			const EdgeId edge = drain_from(x, items[static_cast<size_t>(x.drains_to)].cell);
			if (edge.index < 0) {
				continue;
			}
			const Fill held = fill(x);
			const double head_m = held.defined ? g->level(held.condensed * 1e6) / 100.0 : 0.0, bore_m = g->tip_bore / 100.0;
			const double rate = x.cap * ORIFICE_CD * (PI * bore_m * bore_m / 4.0) * std::sqrt(2.0 * GRAVITY * std::max(head_m, 0.0));
			world().set_drain(edge, rate, 0.0, 0.0);
		}
	}

	// An open vessel's gas exchanges with the room's air by diffusion through its mouth (GAME_SPEC §6, Stage 50): its
	// mouth's edge passes each gas as a tube from what the vessel holds up to its brim would (Stefan's), the area over
	// the length inverted from the integral of dz / A(z). Set at a step boundary, again only when it has changed.
	void set_exchanges() {
		for (Item &x : items) {
			if (x.kind != Kind::Vessel || x.burst || x.mouth.index < 0 || x.glassware.empty() || !world().edges()[static_cast<size_t>(x.mouth.index)].open) {
				continue;
			}
			const glass::Vessel *g = glass::find(x.glassware, x.nominal_ml);
			if (g == nullptr) {
				continue;
			}
			double held = 0.0; // cm^3 of solids and liquids, the column's whole
			std::vector<CellId> cells = layers_of(x);
			cells.push_back(x.cell);
			for (const CellId c : cells) {
				held += fill_of(table(), world().snapshot_cell(c), kelvin(c)).condensed * 1e6;
			}
			const double path = g->mouth_path(held); // 1/cm, from its table
			const double exchange = 0.01 / path; // cm to m
			const double now = world().edges()[static_cast<size_t>(x.mouth.index)].exchange;
			if (std::fabs(exchange - now) > 1e-3 * exchange) {
				world().set_exchange(x.mouth, exchange);
			}
		}
	}

	// the lowest layer of a vessel's column that holds anything; its own cell where none does (Stage 47)
	CellId lowest_cell(const Item &v) const {
		CellId out = v.cell;
		for (const CellId c : layers_of(v)) {
			if (layered_cell(c)) {
				out = c;
			}
		}
		return out;
	}
	// a drain from a vessel's column (its own cell or a layer) into a cell, if there is one
	EdgeId drain_from(const Item &v, CellId into) const {
		std::vector<CellId> cells = layers_of(v);
		cells.push_back(v.cell);
		const std::span<const Edge> edges = world().edges();
		for (size_t e = 0; e < edges.size(); ++e) {
			if (edges[e].open && edges[e].drain && edges[e].b == into && std::find(cells.begin(), cells.end(), edges[e].a) != cells.end()) {
				return EdgeId{ static_cast<int32_t>(e) };
			}
		}
		return EdgeId{};
	}
	// a drain moved to start from another cell, its rate kept
	EdgeId redrain(EdgeId edge, CellId from) {
		const Edge old = world().edges()[static_cast<size_t>(edge.index)];
		world().disconnect(edge);
		EdgeId moved;
		if (world().connect_drain(from, old.b, &moved) != Refusal::None) {
			return EdgeId{};
		}
		world().set_drain(moved, old.rate, old.carried, old.solids_rate);
		return moved;
	}
	// What lies under a layered vessel follows its lowest layer, at a step boundary: a burner heats it (Stage 47);
	// an outlet at its bottom, and the cap of one upside down, draw from it (Stage 49), as a pour over its lip
	// draws from its top
	void follow_columns() {
		for (Item &x : items) {
			if (x.kind != Kind::Vessel || (x.drains_to < 0 && !(x.pours_into >= 0 && x.inverted))) {
				continue;
			}
			const CellId lowest = lowest_cell(x);
			if (x.pours_into >= 0 && x.inverted && x.pour.index >= 0 && world().edges()[static_cast<size_t>(x.pour.index)].a != lowest) {
				x.pour = redrain(x.pour, lowest);
			}
			if (x.drains_to >= 0) {
				const EdgeId outlet = drain_from(x, items[static_cast<size_t>(x.drains_to)].cell);
				if (outlet.index >= 0 && world().edges()[static_cast<size_t>(outlet.index)].a != lowest) {
					redrain(outlet, lowest);
				}
			}
		}
		for (Item &b : items) {
			if (b.kind != Kind::Burner || b.under < 0 || b.heats.index < 0) {
				continue;
			}
			const Item &v = items[static_cast<size_t>(b.under)];
			CellId target = v.cell;
			for (const CellId c : layers_of(v)) {
				if (layered_cell(c)) {
					target = c;
				}
			}
			const Edge &e = world().edges()[static_cast<size_t>(b.heats.index)];
			if (e.a == target) {
				continue;
			}
			const double conductance = e.conductance;
			world().disconnect(b.heats);
			if (world().connect(target, b.cell, PHASES_NONE, &b.heats, conductance) != Refusal::None) {
				b.heats = EdgeId{};
			}
		}
	}

	bool take_off_drains(int thing, std::string &why) {
		if (!end_pours(thing, why)) { // a pour ends where a drain comes off (Stage 27)
			return false;
		}
		for (size_t u = 0; u < items.size(); ++u) {
			if (items[u].drains_to >= 0 && (static_cast<int>(u) == thing || items[u].drains_to == thing) && !take_off_drain(u, why)) {
				return false;
			}
		}
		return true;
	}
	// the drain from one vessel's outlet
	bool take_off_drain(size_t u, std::string &why) {
		Item &upper = items[u];
		const EdgeId outlet = drain_from(upper, items[static_cast<size_t>(upper.drains_to)].cell); // (from its lowest layer, Stage 49)
		if (outlet.index >= 0) {
			if (!boundary(why)) {
				return false;
			}
			world().disconnect(outlet);
		}
		upper.drains_to = -1;
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
	// a part, and everything a circuit holds, and what that holds, among the lab's parts or the trash's
	static std::vector<std::string> held_by(const std::vector<Part> &among, const std::string &id) {
		std::vector<std::string> out{ id };
		for (size_t k = 0; k < out.size(); ++k) {
			for (const Part &p : among) {
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

	// ---- notices (§13.4)

	static constexpr size_t MAX_NOTICES = 2000;
	static constexpr int64_t READ_EVERY = 15; // the detectors read a quarter of a lab-second at a time

	void note(Notice::Level level, const std::string &about, const std::string &says) {
		notices.push_back(Notice{ static_cast<int64_t>(world().step()), level, about, says });
		if (notices.size() > MAX_NOTICES) {
			const size_t drop = notices.size() - MAX_NOTICES / 2;
			notices.erase(notices.begin(), notices.begin() + static_cast<std::ptrdiff_t>(drop));
			notices_read = notices_read > drop ? notices_read - drop : 0;
		}
	}

	// the detectors' reading of the state, written as notices where it changed;
	// with `quiet`, only read (the bench as it opens, or loads)
	void detect(bool quiet) {
		const SpeciesTable &t = table();
		const size_t species = static_cast<size_t>(t.species_count()), rated = sim->rated().size();
		present.resize(items.size() * species, 0);
		running.resize(items.size() * rated, 0);
		boiling.resize(items.size(), 0);
		full.resize(items.size(), 0);
		pressed.resize(items.size(), 0);
		flowing.resize(items.size(), 0);
		water_g.resize(items.size(), 0.0);
		touched.resize(items.size(), 0);
		const std::span<const ReactionNote> notes = builtin_reaction_notes();
		for (size_t i = 0; i < items.size(); ++i) {
			const Item &x = items[i];
			if (!holds_matter(x)) {
				continue;
			}
			if (x.kind == Kind::Tray) {
				// all it holds, its gas too: what condenses or boils in it is no spill (Stage 28: a burst's hot water)
				double held = 0.0;
				for (size_t k = 0; k < species; ++k) {
					const SpeciesDef &def = t.species(SpeciesId{ static_cast<int32_t>(k) });
					held += !def.wall ? grams(x.cell, SpeciesId{ static_cast<int32_t>(k) }) : 0.0;
				}
				if (!quiet && held > tray_g + 0.01) {
					note(Notice::Warning, "tray", text_of("%.2f g spilled into the tray: a vessel overflowed, or broke", held - tray_g));
				}
				tray_g = held;
				continue;
			}
			const bool by_hand = touched[i] != 0;
			touched[i] = 0;
			// what is there: a solid, liquid or dissolved species appearing, or used up
			for (size_t k = 0; k < species; ++k) {
				const SpeciesId s{ static_cast<int32_t>(k) };
				const SpeciesDef &def = t.species(s);
				if (def.wall || def.phase == Phase::Gas) {
					continue;
				}
				uint8_t &was = present[i * species + k];
				const double g = grams(x, s);
				if (!was && g >= 0.001) {
					was = 1;
					if (!quiet && !by_hand) {
						note(Notice::Event, x.id, named(t, s) + " appeared in " + x.id);
					}
				} else if (was && g < 0.0001) {
					was = 0;
					if (!quiet && !by_hand) {
						note(Notice::Event, x.id, named(t, s) + " is used up in " + x.id);
					}
				}
			}
			// a reaction with a rate law starting or stopping there (moving more than a ten-millionth of a mole a reading, or none)
			for (size_t r = 0; r < rated; ++r) {
				const int64_t extent = sim->tally(r, x.cell);
				const double mol = std::fabs(static_cast<double>(extent)) * MOL_PER_QUANTUM;
				uint8_t &on = running[i * rated + r];
				const Reaction &reaction = sim->rated()[r];
				const ReactionNote *n = nullptr;
				for (const ReactionNote &candidate : notes) {
					n = candidate.id == reaction.id() ? &candidate : n;
				}
				if (!on && mol >= 1e-7) {
					on = 1;
					if (!quiet) {
						const std::string says = n != nullptr && !n->says.empty() ? std::string(n->says) : std::string(reaction.id()) + " began";
						note(static_cast<Notice::Level>(n != nullptr ? n->level : 0), x.id, x.id + ": " + says);
					}
				} else if (on && mol < 1e-9) {
					on = 0;
					if (!quiet) {
						std::string name(reaction.id());
						std::replace(name.begin(), name.end(), '_', ' ');
						note(Notice::Detail, x.id, x.id + ": " + name + " stopped");
					}
				}
			}
			// boiling, and boiling dry: open, at 99.5 C; sealed (Stage 28), where its gas is nearly all steam, at
			// whatever temperature its pressure lets it
			const double kelvin_now = kelvin(x.cell), water = grams(x.cell, builtin::water);
			double steam_share = 0.0;
			if (sealed(x)) {
				double gas = 0.0;
				for (size_t k = 0; k < species; ++k) {
					gas += t.species(SpeciesId{ static_cast<int32_t>(k) }).phase == Phase::Gas ? world().amount(x.cell, SpeciesId{ static_cast<int32_t>(k) }).mol() : 0.0;
				}
				steam_share = gas > 0.0 ? world().amount(x.cell, builtin::steam).mol() / gas : 0.0;
			}
			// sealed, it boils while its water goes to steam under a gas of steam (more than 10 ug a reading: its vapour's
			// room is small, so heat raises it along its saturation and little goes to steam)
			const bool going = water < water_g[i] - 1e-5;
			water_g[i] = water;
			const bool hot = sealed(x) ? steam_share >= 0.97 && going : kelvin_now >= 273.15 + 99.5;
			const bool cooled = sealed(x) ? steam_share < 0.95 || !going : kelvin_now < 273.15 + 99.0;
			if (!boiling[i] && water >= 0.05 && hot) {
				boiling[i] = 1;
				if (!quiet) {
					note(Notice::Event, x.id, x.id + (sealed(x) ? text_of(" boils, sealed, at %.0f C", kelvin_now - 273.15) + text_of(" and %.0f kPa", pressure_pa(x) / 1000.0) : " boils"));
				}
			} else if (boiling[i] && (water < 0.05 || cooled)) {
				boiling[i] = 0;
				if (!quiet) {
					if (water < 0.05) {
						note(Notice::Warning, x.id, x.id + " boiled dry: nothing is left for crystals to grow from");
					} else {
						note(Notice::Event, x.id, x.id + " stopped boiling");
					}
				}
			}
			// a sealed vessel near what gives way (Stage 28): its stopper's hold, or its glass
			if (x.kind == Kind::Vessel && !x.burst) {
				const double over = sealed(x) ? pressure_pa(x) - P_ATMOSPHERE : 0.0, limit = gives_way(x);
				const bool stoppered = x.holds >= 0 && items[static_cast<size_t>(x.holds)].kind == Kind::Stopper && stopper_pops(items[static_cast<size_t>(x.holds)], x) <= glass_burst(x);
				if (!pressed[i] && over >= 0.8 * limit) {
					pressed[i] = 1;
					if (!quiet) {
						note(Notice::Warning, x.id, x.id + text_of(" is at %.0f kPa", (over + P_ATMOSPHERE) / 1000.0) + (stoppered ? ": its stopper pops" : ": its glass bursts")
										+ text_of(" past %.0f kPa", (limit + P_ATMOSPHERE) / 1000.0));
					}
				} else if (pressed[i] && over < 0.7 * limit) {
					pressed[i] = 0;
				}
			}
			// a pour running, stopping, and its vessel run dry (Stage 28)
			if (x.kind == Kind::Vessel) {
				double rate = 0.0;
				if (x.pours_into >= 0 && x.pour.index >= 0) {
					rate = world().edges()[static_cast<size_t>(x.pour.index)].rate;
				}
				const double left_ml = liquid_m3(x) * 1e6;
				const bool runs = x.pours_into >= 0 && rate > 0.0 && left_ml > 0.05;
				const std::string into = x.pours_into >= 0 ? items[static_cast<size_t>(x.pours_into)].id : std::string("it");
				if (!flowing[i] && runs) {
					flowing[i] = 1;
					if (!quiet) {
						note(Notice::Detail, x.id, x.id + " pours into " + into);
					}
				} else if (flowing[i] && !runs) {
					flowing[i] = 0;
					if (!quiet) {
						if (left_ml <= 0.05) {
							note(Notice::Event, x.id, x.id + " is empty: nothing more pours" + (x.pours_into >= 0 ? " into " + into : std::string()));
						} else {
							note(Notice::Detail, x.id, x.id + " stopped pouring");
						}
					}
				}
			}
			// a balloon nearly full
			if (x.kind == Kind::Bag && x.capacity > 0.0 && !x.burst) {
				const Fill fill = fill_of(t, world().snapshot_cell(x.cell), kelvin_now);
				const double share = (fill.gas + fill.condensed) / x.capacity;
				if (!full[i] && share >= 0.8) {
					full[i] = 1;
					if (!quiet) {
						note(Notice::Warning, x.id, x.id + text_of(" is %.0f%% full: past its capacity it bursts", 100.0 * share));
					}
				} else if (full[i] && share < 0.7) {
					full[i] = 0;
				}
			}
		}
		detect_goals(quiet);
		sim->clear_tally();
	}

	json water_bits() const {
		json out = json::array();
		for (const double g : water_g) {
			out.push_back(bits(g));
		}
		return out;
	}
	json notices_json(bool exact) const {
		json list = json::array();
		for (const Notice &n : notices) {
			list.push_back({ { "step", n.step }, { "level", level_name(n.level) }, { "item", n.item }, { "says", n.says } });
		}
		if (!exact) {
			return list;
		}
		return { { "list", list }, { "read", notices_read }, { "present", present }, { "running", running }, { "boiling", boiling }, { "full", full }, { "touched", touched },
			{ "tray_g", bits(tray_g) }, { "tally", sim->tally_all() }, { "pressed", pressed }, { "flowing", flowing }, { "water_g", water_bits() } };
	}
	bool notices_from(const json &j, std::string &why) {
		for (const json &n : j.at("list")) {
			Notice::Level level = Notice::Detail;
			if (!level_of(n.at("level").get<std::string>(), level)) {
				why = "the save holds a notice of no level";
				return false;
			}
			notices.push_back(Notice{ n.at("step").get<int64_t>(), level, n.at("item").get<std::string>(), n.at("says").get<std::string>() });
		}
		notices_read = j.at("read").get<size_t>();
		present = j.at("present").get<std::vector<uint8_t>>();
		running = j.at("running").get<std::vector<uint8_t>>();
		boiling = j.at("boiling").get<std::vector<uint8_t>>();
		full = j.at("full").get<std::vector<uint8_t>>();
		touched = j.at("touched").get<std::vector<uint8_t>>();
		pressed = j.value("pressed", std::vector<uint8_t>{});
		flowing = j.value("flowing", std::vector<uint8_t>{});
		water_g.clear();
		for (const json &w : j.value("water_g", json::array())) {
			double g = 0.0;
			if (!from_bits(w, g)) {
				why = "the save holds a number that is not one";
				return false;
			}
			water_g.push_back(g);
		}
		if (!from_bits(j.at("tray_g"), tray_g)) {
			why = "the save holds a number that is not one";
			return false;
		}
		restored_tally = j.at("tally").get<std::vector<int64_t>>();
		restored_notices = true;
		return true;
	}
	std::vector<int64_t> restored_tally; // put back once the simulation is made

	// one step; false when an invariant fails
	bool step_once(std::string &why) {
		// A checked step needs its trace to start where the last one ended. After
		// unchecked steps, with a pour already applied in this one, it cannot:
		// this step runs unchecked and the next is checked.
		// (Stage 47: after the pours are set, for a pour that lands as a layer moves the liquid there first)
		set_stirring();
		set_holders(); // (Stage 56)
		follow_columns(); // (Stage 49: before the pours' rates are set, a drain it moves among them)
		set_pours();
		set_outlets();
		set_exchanges();
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
				note(Notice::Danger, "", why + ": the lab stopped");
				return false;
			}
		}
		follow_thermometers();
		follow_loggers();
		relieve_pressure(); // its ledger empty: a burst takes its edges off (Stage 28)
		if (!trials.empty()) {
			run_trials();
		}
		if (world().step() % READ_EVERY == 0) {
			detect(false);
			watch_controllers();
		}
		if (!signals_step(why)) {
			note(Notice::Danger, "", why + ": the lab stopped");
			return false;
		}
		burst_balloons();
		return true;
	}

	// Pressure relieved (GAME_SPEC §13, Stage 28), right after a step, its ledger empty: a sealed vessel past what
	// seals it gives way. A stopper pops out (its hold over the neck's bore, before its glass would go): the mouth
	// open to the room again, the stopper beside it. Else its glass bursts: what is on it comes off, its gas goes
	// into the room's air, the rest into the tray, and it is broken: empty, in the cabinet.
	void relieve_pressure() {
		for (size_t i = 0; i < items.size(); ++i) {
			Item &v = items[i];
			if (v.kind != Kind::Vessel || v.burst || !sealed(v)) {
				continue;
			}
			const double over = pressure_pa(v) - P_ATMOSPHERE;
			if (!(over > gives_way(v))) {
				continue;
			}
			const double kpa = (over + P_ATMOSPHERE) / 1000.0;
			const bool stoppered = v.holds >= 0 && items[static_cast<size_t>(v.holds)].kind == Kind::Stopper;
			if (stoppered && stopper_pops(items[static_cast<size_t>(v.holds)], v) <= glass_burst(v)) {
				const int s = v.holds;
				Item &x = items[static_cast<size_t>(s)];
				world().connect(v.cell, air, PHASES_GAS, &v.mouth);
				v.holds = -1;
				x.on = -1;
				x.place = Place::Cabinet;
				x.col = x.row = -1;
				set_beside(s, v);
				events.push_back(Event{ static_cast<int64_t>(world().step()), "popped", x.id });
				note(Notice::Warning, v.id, x.id + " popped out of " + v.id + text_of(" at %.0f kPa", kpa));
				continue;
			}
			shatter(i, kpa);
		}
	}
	void shatter(size_t i, double kpa) {
		Item &v = items[i];
		const double kelvin_before = kelvin(v.cell);
		if (v.holds >= 0) { // what is on its mouth comes off: a stopper, or a condenser sealed with it
			Item &x = items[static_cast<size_t>(v.holds)];
			if (x.joint.index >= 0) {
				world().disconnect(x.joint);
			}
			x.on = -1;
			x.joint = EdgeId{};
			x.place = Place::Cabinet;
			x.col = x.row = -1;
			set_beside(v.holds, v);
			v.holds = -1;
		}
		leave_balances(static_cast<int>(i), v); // a balance under it is left where it stood (Stage 50)
		for (Item &b : items) { // a burner under it is set beside it
			if (b.kind == Kind::Burner && b.under == static_cast<int>(i)) {
				world().disconnect(b.heats);
				b.under = -1;
				b.heats = EdgeId{};
				b.place = Place::Cabinet;
				b.col = b.row = -1;
				set_beside(static_cast<int>(&b - items.data()), v);
			}
		}
		std::vector<CellId> own = layers_of(v); // (a drain may leave from its lowest layer: Stage 49)
		own.push_back(v.cell);
		const auto edges = world().edges();
		for (size_t e = 0; e < edges.size(); ++e) { // its drains, in and out
			if (edges[e].open && edges[e].drain && (std::find(own.begin(), own.end(), edges[e].a) != own.end() || edges[e].b == v.cell)) {
				world().disconnect(EdgeId{ static_cast<int32_t>(e) });
			}
		}
		for (Item &u : items) {
			u.drains_to = u.drains_to == static_cast<int>(i) ? -1 : u.drains_to;
		}
		v.drains_to = -1;
		v.screwed = false;
		if (v.mouth.index < 0) {
			world().connect(v.cell, air, PHASES_GAS, &v.mouth);
		}
		// its contents out: the gas into the room's air, the rest into the tray
		const CellState cell = world().snapshot_cell(v.cell);
		std::vector<SpeciesAmount> gas, rest;
		for (size_t s = 0; s < cell.amount.size(); ++s) {
			const SpeciesDef &def = table().species(SpeciesId{ static_cast<int32_t>(s) });
			if (cell.amount[s] > 0 && !def.wall) {
				(def.phase == Phase::Gas ? gas : rest).push_back(SpeciesAmount{ SpeciesId{ static_cast<int32_t>(s) }, Amount{ cell.amount[s] } });
			}
		}
		const double t = kelvin(v.cell); // opened: read as an expansion against the room
		if (!gas.empty()) {
			world().transfer(v.mouth, v.cell, gas, enthalpy_carried(table(), gas, t));
		}
		const int tray_at = find("tray");
		EdgeId spill;
		if (!rest.empty() && tray_at >= 0 && world().connect(v.cell, items[static_cast<size_t>(tray_at)].cell, HANDLED, &spill) == Refusal::None) {
			world().transfer(spill, v.cell, rest, enthalpy_carried(table(), rest, t));
		}
		mid_step = true;
		touched.resize(items.size(), 0);
		touched[i] = 1;
		v.burst = true;
		forget_trash(v.id); // burst in the trash (Stage 34), it leaves it: broken, in the cabinet, as any
		v.place = Place::Cabinet;
		v.col = v.row = -1;
		events.push_back(Event{ static_cast<int64_t>(world().step()), "burst", v.id });
		note(Notice::Danger, v.id, v.id + text_of(" burst at %.0f kPa", kpa) + text_of(" (%.0f C): its contents went into the room and the tray", kelvin_before - 273.15));
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
			note(Notice::Danger, bag.id, bag.id + " burst: it took more gas than it holds, and the gas went into the room");
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
	// A viewer's probe goes back to its holder when what it is fixed on has left the lab (a part scrapped, the trash
	// emptied), or when the viewer itself leaves the bench (Stage 42). What is in the trash or a closed circuit
	// keeps it: it comes back with them.
	void release_probes() {
		for (Item &v : items) {
			if (v.kind == Kind::Thermal && v.place != Place::Bench) {
				v.thermal.frame_room = false; // a camera off the bench: its frame goes back over its screen (Stage 48)
				continue;
			}
			if (v.kind != Kind::Viewer || (v.probe_on.empty() && !v.probe_room)) {
				continue;
			}
			const bool gone = !v.probe_on.empty() && find(v.probe_on) < 0 && find_part(v.probe_on) < 0 && find_binned(v.probe_on) < 0;
			if (gone || v.place == Place::Cabinet || v.place == Place::Trash || v.place == Place::Outbox) {
				v.probe_on.clear();
				v.probe_room = false;
				v.probe_u = v.probe_v = 0.0;
			}
		}
	}

	// A vessel lifted off a balance (Stage 50) leaves the balance where it stood: set down where the vessel was
	// (`was`, as it was before it moved), or beside it. A balance holds nothing in the world: no boundary.
	void leave_balances(int thing, const Item &was) {
		for (size_t i = 0; i < items.size(); ++i) {
			if (stands_under(items[i].kind) && items[i].under == thing) {
				items[i].under = -1;
				items[i].place = Place::Cabinet;
				items[i].col = items[i].row = -1;
				set_beside(static_cast<int>(i), was);
			}
		}
	}
	// The room's air's density, kg/m^3 (Stage 50): its own gas at 1 atm and its temperature
	double air_density() const {
		const CellState &a = world().snapshot_cell(air);
		double mol = 0.0, kg = 0.0;
		for (size_t s = 0; s < a.amount.size(); ++s) {
			const SpeciesId id{ static_cast<int32_t>(s) };
			if (a.amount[s] > 0 && table().species(id).phase == Phase::Gas) {
				const double n = static_cast<double>(a.amount[s]) * MOL_PER_QUANTUM;
				mol += n;
				kg += n * static_cast<double>(table().molar_mass_ug(id)) * 1e-9;
			}
		}
		return mol > 0.0 ? P_ATMOSPHERE * (kg / mol) / (GAS_CONSTANT * kelvin(air)) : 0.0;
	}
	// A balance's load (Stage 50): a vessel, what it holds and what is on its mouth; its mass, g, and its own
	// volume, cm^3 (its walls' by their densities, what it holds by what it takes up)
	void load_of(int thing, double &g, double &cm3) const {
		const Item &x = items[static_cast<size_t>(thing)];
		for (const SubstanceView &row : item_view(x, false, false).contents) {
			g += row.g;
			const double density = row.wall ? instruments::wall_density(row.species) : 0.0;
			cm3 += row.wall ? (density > 0.0 ? row.g / density : 0.0) : row.ml;
		}
		if (x.holds >= 0) {
			load_of(x.holds, g, cm3);
		}
	}
	// what a balance's load reads on it before its linearity: its conventional mass (OIML D 28), g; nothing while
	// the vessel is lifted off it to pour
	double gross_g(const Item &scale) const {
		if (scale.under < 0 || items[static_cast<size_t>(scale.under)].pours_into >= 0) {
			return 0.0;
		}
		double g = 0.0, cm3 = 0.0;
		load_of(scale.under, g, cm3);
		return instruments::conventional_g(g, cm3, air_density());
	}

	// What a spectrophotometer's beam passes through (GAME_SPEC §13, Stage 55): its cuvette's liquid at the beam's
	// height over its floor, the layer there if it is layered; air if the liquid stands lower or nothing is in its well
	spectro::Sample spectro_sample(const Item &x) const {
		spectro::Sample out;
		if (x.under < 0) {
			return out;
		}
		const Item &cuvette = items[static_cast<size_t>(x.under)];
		const glass::Vessel *g = glass::find(cuvette.glassware, cuvette.nominal_ml);
		if (g == nullptr || cuvette.pours_into >= 0) {
			return out;
		}
		const double y = g->floor + spectro::model().beam_mm / 10.0;
		out.path_cm = 2.0 * g->radius_inside(y);
		const double below = g->volume_below(y) * 1e-6; // m^3 under the beam
		std::vector<CellId> cells{ cuvette.cell }; // from the top down
		for (const CellId c : layers_of(cuvette)) {
			if (layered_cell(c)) {
				cells.push_back(c);
			}
		}
		double filled = 0.0;
		const double held = std::clamp(cuvette.suspended, 0.0, 1.0); // the share of its solids its stirring holds up (Stage 56)
		for (auto c = cells.rbegin(); c != cells.rend(); ++c) {
			const CellState &state = world().snapshot_cell(*c);
			const double liquid = liquid_m3(state), solid = solid_m3(state);
			if (filled + solid * (1.0 - held) > below) {
				out.opaque = true; // its settled solids stand at the beam
				return out;
			}
			filled += liquid + solid;
			if (filled > below && liquid > 0.0) {
				for (size_t s = 0; s < state.amount.size(); ++s) {
					const SpeciesId id{ static_cast<int32_t>(s) };
					const SpeciesDef &def = table().species(id);
					if (state.amount[s] > 0 && !def.wall && (def.phase == Phase::Liquid || def.phase == Phase::Aqueous)) {
						out.molar.emplace_back(id, static_cast<double>(state.amount[s]) * MOL_PER_QUANTUM / (liquid * 1000.0));
					}
					if (state.amount[s] > 0 && !def.wall && def.phase == Phase::Solid && held > 0.0 && s < state.particles.size() && state.particles[s] > 0) {
						// (Stage 56) its grains held up in the liquid, in the beam
						const double grains = static_cast<double>(state.particles[s]) * held / (liquid * 1e6);
						out.turbidity += spectro::turbidity(grains, chem::particle_diameter(table(), state, id) * 100.0);
					}
				}
				return out;
			}
		}
		return out;
	}
	// its detector's signal now: at its true wavelength, the one set plus its error drawn from the seed
	double spectro_signal(const Item &x) const {
		const spectro::Model &model = spectro::model();
		return spectro::signal(model, x.nm + model.wavelength_error_nm * instruments::share(seed, x.id + ":nm"), spectro_sample(x));
	}

	// The hand (GAME_SPEC §13, Readings; Stage 50): what it moves of an amount aimed at, `1 + hand u`, u drawn from
	// the seed, the step and the two vessels
	double by_hand(const json &c, const std::string &what, const Item &from, const Item &to) const {
		if (!c.value("about", false)) {
			return 1.0;
		}
		const std::string key = "hand:" + what + ":" + std::to_string(world().step()) + ":" + from.id + ":" + to.id;
		return 1.0 + instruments::hand() * instruments::share(seed, key);
	}

	// A spectrophotometer's cell holder (GAME_SPEC §13, Stage 56): while it is set and a cuvette stands in its well, a
	// heater along an edge from the mains to the cuvette, its power proportional to how far the cuvette is under the
	// set temperature, up to its most; it only heats (a cuvette cools by its own loss to the room). The edge is made
	// when it is wanted and taken off when it is not: a lab that never sets one is as before.
	void set_holders() {
		for (Item &x : items) {
			if (x.kind != Kind::Spectro) {
				continue;
			}
			const bool wanted = std::isfinite(x.holder_c) && x.under >= 0 && (x.place == Place::Under) && items[static_cast<size_t>(x.under)].pours_into < 0 &&
					mains.index >= 0;
			if (!wanted) {
				if (x.heats.index >= 0) {
					world().disconnect(x.heats);
					x.heats = EdgeId{};
				}
				continue;
			}
			const Item &cuvette = items[static_cast<size_t>(x.under)];
			// into its lowest layer (a column's, Stage 47): a holder warms the cell from its foot up, and warmth below
			// overturns through the column
			CellId lowest = cuvette.cell;
			for (const CellId c : layers_of(cuvette)) {
				if (layered_cell(c)) {
					lowest = c;
				}
			}
			if (x.heats.index >= 0 && world().edges()[static_cast<size_t>(x.heats.index)].b.index != lowest.index) {
				world().disconnect(x.heats); // another cuvette in its well, or another layer at its foot
				x.heats = EdgeId{};
			}
			if (x.heats.index < 0) {
				EdgeId edge;
				if (world().connect(mains, lowest, PHASES_NONE, &edge) != Refusal::None) {
					continue;
				}
				x.heats = edge;
			}
			const spectro::Model &model = spectro::model();
			const double under = x.holder_c + 273.15 - kelvin(cuvette);
			world().set_power(x.heats, std::clamp(model.holder_gain_w_k * under, 0.0, model.holder_most_w));
		}
	}

	// what stands under a vessel that keeps it where it is: a burner (a balance is left behind)
	int fixed_under(int thing) const {
		for (size_t i = 0; i < items.size(); ++i) {
			if (items[i].under == thing && !stands_under(items[i].kind)) {
				return static_cast<int>(i);
			}
		}
		return -1;
	}

	bool on_bench(const Item &x, std::string &why) const {
		if (x.place == Place::Cabinet || x.place == Place::Outbox || x.place == Place::Trash) {
			why = "\"" + x.id + "\" is " + (x.place == Place::Cabinet ? "in the cabinet" : x.place == Place::Trash ? "in the trash" : "in the outbox") + ", not on the bench";
			return false;
		}
		return true;
	}

	json save() const;

	// (`shaped`: with how what is in it lies, a pour by hand's and a vortex's searches; a reading of a quantity,
	// which reads none of them, leaves them out: at every step, for every monitor, at 600 times the clock's speed,
	// they were most of the frame. Stage 36; and its bubbles, `bubbling`, which only a reading of them needs, Stage 45)
	ItemView item_view(const Item &item, bool shaped = true, bool bubbling = true) const {
		const Impl &m = *this;
		const SpeciesTable &table = m.table();
		static const char PHASE[] = { 's', 'l', 'g', 'a' };
		ItemView v;
		v.id = item.id;
		v.kind = KIND_NAME[static_cast<int>(item.kind)];
		v.place = PLACE_NAME[static_cast<int>(item.place)];
		v.col = item.col;
		v.row = item.row;
		if (const int binned_at = item.place == Place::Trash ? trash_at(item.id) : -1; binned_at >= 0) {
			v.col = trash[static_cast<size_t>(binned_at)].col; // in the trash (Stage 34): the square it goes back to
			v.row = trash[static_cast<size_t>(binned_at)].row;
		}
		v.w = item.w;
		v.h = item.h;
		v.on = item.on;
		v.under = item.under;
		v.holds = item.holds;
		v.drains_to = item.drains_to;
		v.pours_into = item.pours_into;
		v.inverted = item.inverted;
		v.tilt = item.tilt;
		v.cap = item.cap;
		if (item.pours_into >= 0 && item.inverted && item.pour.index >= 0) {
			v.pours_ml_s = std::max(world().edges()[static_cast<size_t>(item.pour.index)].rate, 0.0) * 1e6;
		}
		if (item.pours_into >= 0 && !item.inverted && shaped) {
			if (const glass::Vessel *g = glass::find(item.glassware, item.nominal_ml); g != nullptr) {
				const ByHand p = by_hand(item, *g);
				v.turn = p.turn;
				v.head_cm = p.head_cm;
				v.pours_ml_s = item.pour_left_m3 >= 0.0 ? item.tilt * g->pour_ml_s : p.runs_ml_s;
				v.pours_solid_ml_s = item.pour_left_m3 >= 0.0 ? 0.0 : p.solids * 1e6;
			}
		}
		v.stirred = item.stirred;
		if (item.swirl > 0.0 || item.suspended > 0.0) {
			v.swirl_j = item.swirl;
			v.swirl_most_j = item.swirl_most;
			v.vortex = shaped ? vortex(item) : 0.0;
			v.suspended = item.suspended;
		}
		v.submitted = item.submitted;
		v.slot = item.slot;
		v.burst = item.burst;
		v.screwed = item.screwed;
		v.hold_n = item.hold_n;
		if (item.kind == Kind::Vessel) {
			v.pressure_kpa = m.pressure_pa(item) / 1000.0;
			v.sealed = m.sealed(item);
			v.gives_way_kpa = (m.gives_way(item) + P_ATMOSPHERE) / 1000.0;
		}
		v.valve = item.valve;
		v.capacity_ml = item.capacity * 1e6;
		v.wall_mm = item.wall_mm;
		v.floor_mm = item.floor_mm;
		v.glassware = item.glassware;
		v.nominal_ml = item.nominal_ml;
		v.probe_on = item.probe_on;
		v.probe_room = item.probe_room;
		v.probe_u = item.probe_u;
		v.probe_v = item.probe_v;
		v.thermal = item.thermal;
		if (item.kind == Kind::Balance) {
			// a balance (Stage 50): its model, its load's conventional mass, its display while it is on the bench
			const instruments::Balance &model = *instruments::balance(item.model);
			v.celsius = m.world().meta(m.room).reservoir_temperature - 273.15;
			v.model = item.model;
			v.readability_g = model.readability_g;
			v.capacity_g = model.capacity_g;
			v.linearity_g = model.linearity_g;
			v.gross_g = m.gross_g(item);
			v.tare_g = item.tare;
			v.overload = v.gross_g > model.capacity_g;
			if (item.place == Place::Bench || item.place == Place::Under) {
				v.display_g = instruments::display_g(model, v.gross_g, item.tare, instruments::share(m.seed, item.id + ":linearity"));
			}
		} else if (item.kind == Kind::Spectro) {
			// a spectrophotometer (Stage 55): its wavelength, its blank's, what it reads while it is on the bench
			v.celsius = m.world().meta(m.room).reservoir_temperature - 273.15;
			v.nm = item.nm;
			v.blank_nm = item.blank_nm;
			v.blanked = item.blank > 0.0;
			if (std::isfinite(item.holder_c)) {
				v.holder_c = item.holder_c; // (Stage 56)
			}
			if (v.blanked && (item.place == Place::Bench || item.place == Place::Under)) {
				v.absorbance = spectro::reading(spectro::model(), m.spectro_signal(item), item.blank);
				v.overload = !v.absorbance;
			}
		} else if (item.kind == Kind::Viewer || item.kind == Kind::Thermal) {
			v.celsius = m.world().meta(m.room).reservoir_temperature - 273.15; // it stands in the room's air
		} else if (item.kind != Kind::Burner) {
			// a layered vessel (Stage 47) reads as a whole: its column's contents, at the temperature they come to
			const bool layered = item.kind == Kind::Vessel && m.layered(item);
			const CellState column = layered ? m.whole(item) : CellState{};
			const double kelvin = layered ? m.kelvin(item) : m.kelvin(item.cell);
			v.celsius = kelvin - 273.15;
			const CellState &cell = layered ? column : m.world().snapshot_cell(item.cell);
			const Fill fill = layered ? m.fill(item) : fill_of(table, cell, kelvin, &m.world().meta(item.cell));
			if (layered) {
				m.layers_into(item, v);
			}
			if (double p = 0.0; item.kind == Kind::Vessel && chem::ph(table, cell, kelvin, p)) {
				v.ph = p;
			}
			if (item.kind == Kind::Vessel && (shaped || bubbling)) {
				m.bubbles_into(item, kelvin, v);
			}
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
				row.g = m.grams(item, id);
				if (def.phase == Phase::Gas) {
					double gas = 0.0;
					for (size_t k = 0; k < cell.amount.size(); ++k) {
						gas += table.species(SpeciesId{ static_cast<int32_t>(k) }).phase == Phase::Gas ? static_cast<double>(cell.amount[k]) : 0.0;
					}
					row.ml = gas > 0.0 ? fill.gas * 1e6 * static_cast<double>(cell.amount[s]) / gas : 0.0;
				} else if (!def.wall) {
					row.ml = row.mol * def.molar_volume * 1e6;
				}
				if (def.phase == Phase::Solid && !def.wall) {
					row.particles = cell.particles[s];
					row.size_um = particle_diameter(table, cell, id) * 1e6;
				}
				if (def.phase == Phase::Liquid && !def.wall) {
					v.liquid_ml += row.mol * def.molar_volume * 1e6;
				}
				if (def.phase == Phase::Solid && !def.wall) {
					v.solid_ml += row.mol * def.molar_volume * 1e6;
				}
				v.contents.push_back(row);
			}
			// a vessel with a coolant is a condenser: one of its edges goes to a reservoir that is neither the room nor a burner
			for (const Edge &e : m.world().edges()) {
				const CellId other = e.a == item.cell ? e.b : e.b == item.cell ? e.a : CellId{};
				if (other.index >= 0 && e.open && m.world().meta(other).reservoir && !(other == m.room) && !(other == m.mains)) {
					bool burner = false;
					for (const Item &b : m.items) {
						burner = burner || (b.kind == Kind::Burner && b.cell == other);
					}
					v.condenser = v.condenser || !burner;
				}
			}
			if (item.kind == Kind::Vessel) {
				// its instruments' readings (GAME_SPEC §13, Readings; Stage 50): the truth and an offset drawn from the seed
				v.label = item.label;
				const instruments::Meter t = instruments::thermometer(), ph = instruments::ph_meter();
				const double reads_c = m.instruments && !std::isnan(item.read_kelvin) ? item.read_kelvin - 273.15 : v.celsius; // (its lag, Stage 52)
				v.read_c = instruments::rounded(reads_c + t.accuracy * instruments::share(m.seed, item.id + ":thermometer"), t.resolution);
				if (v.ph) {
					v.read_ph = instruments::rounded(*v.ph + ph.accuracy * instruments::share(m.seed, item.id + ":ph"), ph.resolution);
				}
				const instruments::Meter gauge = instruments::gauge(); // (Stage 54)
				v.read_kpa = instruments::rounded(v.pressure_kpa + gauge.accuracy * instruments::share(m.seed, item.id + ":gauge"), gauge.resolution);
				const glass::Vessel *g = item.glassware.empty() ? nullptr : glass::find(item.glassware, item.nominal_ml);
				if (g != nullptr && g->marks_ml > 0.0) {
					v.marks_ml = g->marks_ml;
					v.read_ml = instruments::marks_ml(v.liquid_ml, g->marks_ml, g->marks_error, instruments::share(m.seed, item.id + ":marks"));
				}
				// volumetric glass (Stage 52): its class's tolerance; a mark's true volume; a burette's scale, read down from
				// its zero line; a graduated cylinder's; each read by instruments with its scale's error, in proportion to
				// how far along it the reading is, to a tenth of a division (one digit past the scale, as it is read)
				if (g != nullptr && g->tolerance_ml > 0.0) {
					v.tolerance_ml = g->tolerance_ml;
					v.graduation_ml = g->graduation_ml;
					if (g->mark) {
						v.mark_ml = m.mark_ml(item);
					}
					const double share = instruments::share(m.seed, item.id + ":scale");
					if (g->zero > 0.0) {
						v.scale_ml = g->volume_below(g->zero) - v.condensed_ml;
						v.read_ml = instruments::rounded(*v.scale_ml + g->tolerance_ml * share * std::clamp(*v.scale_ml / g->nominal_ml, 0.0, 1.0), g->graduation_ml / 10.0);
						v.marks_ml = g->graduation_ml;
					} else if (g->graduation_ml > 0.0) {
						v.read_ml = instruments::rounded(v.condensed_ml + g->tolerance_ml * share * std::clamp(v.condensed_ml / g->nominal_ml, 0.0, 1.0), g->graduation_ml / 10.0);
						v.marks_ml = g->graduation_ml;
					}
				}
			}
		} else {
			v.celsius = m.world().meta(item.cell).reservoir_temperature - 273.15;
		}
		return v;
	}

	// Boiling's bubbles (GAME_SPEC §11, Stage 45): from the heat its floor took from a burner in the last step,
	// its bulk, its liquid's depth, and the gas its reactions gave off in the liquid; an upright vessel of a
	// standard shape on the bench, holding water as a liquid
	void bubbles_into(const Item &item, double kelvin, ItemView &v) const {
		const CellState &cell = world().snapshot_cell(item.cell);
		if (item.place != Place::Bench || item.inverted || item.pours_into >= 0 || cell.amount[static_cast<size_t>(builtin::water.index)] <= 0) {
			return;
		}
		const glass::Vessel *g = glass::find(item.glassware, item.nominal_ml);
		const Fill fill = fill_of(table(), cell, kelvin, &world().meta(item.cell));
		if (g == nullptr || !fill.defined) {
			return;
		}
		bubbles::Floor f;
		const double r = g->radius_inside(g->floor + 1e-3) * 1e-2; // m
		f.floor_m2 = 3.14159265358979323846 * r * r;
		f.perimeter_m = 2.0 * 3.14159265358979323846 * r;
		f.depth_m = std::max(g->level(fill.condensed * 1e6) - g->floor, 0.0) * 1e-2;
		f.bulk_c = kelvin - 273.15;
		f.pressure_pa = pressure_pa(item);
		f.water_activity = activity(table(), cell, builtin::water, &world().meta(item.cell));
		const StepFlows &flows = sim->flows();
		const std::span<const Edge> edges = world().edges();
		for (const Item &b : items) {
			if (b.kind != Kind::Burner || b.heats.index < 0 || static_cast<size_t>(b.heats.index) >= edges.size()) {
				continue;
			}
			const Edge &e = edges[static_cast<size_t>(b.heats.index)];
			const size_t slot = static_cast<size_t>(b.heats.index) * PROCESSES + static_cast<size_t>(Process::Heat);
			const std::vector<CellId> column = layers_of(item);
			const bool ours = e.a == item.cell || e.b == item.cell || std::find(column.begin(), column.end(), e.a) != column.end();
			if (ours && slot < flows.energy.size()) {
				f.heat_w += (e.b == item.cell ? 1.0 : -1.0) * static_cast<double>(flows.energy[slot]) * JOULE_PER_QUANTUM / sim->step_seconds();
			}
		}
		// the gas its rated reactions made this step in the liquid, at its temperature and pressure
		double gas_mol = 0.0;
		for (size_t k = 0; k < sim->rated().size(); ++k) {
			const size_t slot = static_cast<size_t>(item.cell.index) * flows.reactions + k;
			if (slot >= flows.extent.size() || flows.extent[slot] == 0) {
				continue;
			}
			for (const Term &t : sim->rated()[k].terms()) {
				if (table().species(t.species).phase == Phase::Gas) {
					gas_mol += std::max(static_cast<double>(flows.extent[slot]) * t.nu, 0.0) * MOL_PER_QUANTUM;
				}
			}
		}
		f.gas_m3_s = gas_mol * GAS_CONSTANT * kelvin / f.pressure_pa / sim->step_seconds();
		const bubbles::Bubbles b = bubbles::boil(f);
		v.depth_cm = f.depth_m * 100.0;
		v.floor_c = b.floor_c;
		v.floor_boils_c = b.saturation_floor_c;
		v.boiling = b.boiling;
		v.bubbles_surface = (b.boiling && b.surface) || b.gas_extinction > 0.0;
		v.bubbles_cm = v.bubbles_surface ? v.depth_cm : b.boiling ? b.end_m * 100.0 : 0.0;
		v.bubble_mm = b.diameter_m * 1000.0;
		v.bubble_extinction = b.extinction_floor * 0.01; // a cm
		v.bubble_exponent = b.exponent;
		v.gas_extinction = b.gas_extinction * 0.01;
		v.bubbles_end_cm = b.end_m * 100.0;
	}

	// connections change at a step boundary: take the step in progress to its end
	bool boundary(std::string &why) { return !mid_step || step_once(why); }

	double kelvin(CellId cell) const {
		double t = 0.0;
		world().temperature(cell, t);
		return t;
	}

	// pressure (GAME_SPEC §13, Stage 28): an item's, Pa (an open one's is the room's)
	double pressure_pa(const Item &x) const {
		if (x.kind == Kind::Burner || x.cell.index < 0) {
			return P_ATMOSPHERE;
		}
		const Thermal t = thermal_state(table(), world().snapshot_cell(x.cell), &world().meta(x.cell));
		return t.defined ? t.pressure : P_ATMOSPHERE;
	}
	bool sealed(const Item &x) const { return x.cell.index >= 0 && world().meta(x.cell).sealed; }
	// how far over the room's a vessel's glass holds (Pa); what pushes a stopper out of a vessel's neck: its hold over the bore
	double glass_burst(const Item &v) const {
		const glass::Vessel *g = glass::find(v.glassware, v.nominal_ml);
		return g != nullptr ? g->burst : glass::burst_default();
	}
	double stopper_pops(const Item &stopper, const Item &v) const {
		const glass::Vessel *g = glass::find(v.glassware, v.nominal_ml);
		const double r = (g != nullptr ? g->mouth() : 0.5 * glass::seals()) / 100.0;
		return stopper.hold_n / (PI * r * r);
	}
	// what gives way first, sealed, and at what overpressure
	double gives_way(const Item &v) const {
		const bool stoppered = v.holds >= 0 && items[static_cast<size_t>(v.holds)].kind == Kind::Stopper;
		return stoppered ? std::min(stopper_pops(items[static_cast<size_t>(v.holds)], v), glass_burst(v)) : glass_burst(v);
	}

	double grams(CellId cell, SpeciesId s) const { return world().amount(cell, s).mol() * static_cast<double>(table().molar_mass_ug(s)) * 1e-6; }

	// a vessel's column's layers (GAME_SPEC §6, Stage 47), from the top down: the world's column edges under its cell
	std::vector<CellId> layers_of(const Item &x) const {
		std::vector<CellId> out;
		const std::span<const Edge> edges = world().edges();
		for (CellId at = x.cell;;) {
			CellId next{};
			for (const Edge &e : edges) {
				if (e.open && e.column && e.b == at) {
					next = e.a;
					break;
				}
			}
			if (next.index < 0) {
				return out;
			}
			out.push_back(next);
			at = next;
		}
	}

	// A vessel as a whole (GAME_SPEC §6, Stage 47): its own cell and the layers of its column, as one; its own cell
	// alone while no layer is in use (a mixed vessel reads as it always did)
	bool layered(const Item &x) const {
		for (const CellId c : layers_of(x)) {
			for (const int64_t a : world().snapshot_cell(c).amount) {
				if (a != 0) {
					return true;
				}
			}
		}
		return false;
	}
	CellState whole(const Item &x) const {
		CellState out = world().snapshot_cell(x.cell);
		for (const CellId c : layers_of(x)) {
			const CellState &l = world().snapshot_cell(c);
			for (size_t s = 0; s < out.amount.size(); ++s) {
				out.amount[s] += l.amount[s];
				out.particles[s] += l.particles[s];
			}
			out.energy += l.energy;
		}
		return out;
	}
	double liquid_m3(const Item &x) const { return layered(x) ? liquid_m3(whole(x)) : liquid_m3(x.cell); }
	double solid_m3(const Item &x) const { return layered(x) ? solid_m3(whole(x)) : solid_m3(x.cell); }
	double liquid_g(const Item &x) const { return layered(x) ? liquid_g(whole(x)) : liquid_g(x.cell); }
	double grams(const Item &x, SpeciesId s) const {
		if (!layered(x)) {
			return grams(x.cell, s);
		}
		double g = grams(x.cell, s);
		for (const CellId c : layers_of(x)) {
			g += grams(c, s);
		}
		return g;
	}
	// its column's layers for the view, from the top down: each one's liquid, solids, temperature and contents
	void layers_into(const Item &x, ItemView &v) const {
		static const char PHASE[] = { 's', 'l', 'g', 'a' };
		const auto one = [&](CellId c) {
			LayerView l;
			const CellState &state = world().snapshot_cell(c);
			l.liquid_ml = liquid_m3(state) * 1e6;
			l.solid_ml = solid_m3(state) * 1e6;
			l.celsius = kelvin(c) - 273.15;
			for (size_t s = 0; s < state.amount.size(); ++s) {
				const SpeciesId id{ static_cast<int32_t>(s) };
				const SpeciesDef &def = table().species(id);
				if (state.amount[s] <= 0 || def.wall || def.phase == Phase::Gas) {
					continue;
				}
				SubstanceView row;
				row.species = std::string(def.id);
				row.formula = std::string(def.formula);
				row.phase = PHASE[static_cast<int>(def.phase)];
				row.mol = static_cast<double>(state.amount[s]) * MOL_PER_QUANTUM;
				row.g = grams(c, id);
				row.ml = def.phase == Phase::Solid || def.phase == Phase::Liquid || def.phase == Phase::Aqueous ? row.mol * def.molar_volume * 1e6 : 0.0;
				l.contents.push_back(row);
			}
			return l;
		};
		v.layers.push_back(one(x.cell));
		for (const CellId c : layers_of(x)) {
			if (layered_cell(c)) {
				v.layers.push_back(one(c));
			}
		}
	}
	bool layered_cell(CellId c) const {
		for (const int64_t a : world().snapshot_cell(c).amount) {
			if (a != 0) {
				return true;
			}
		}
		return false;
	}
	// its temperature as a whole: what it would come to mixed (its heat over its heat capacity)
	double kelvin(const Item &x) const {
		if (!layered(x)) {
			return kelvin(x.cell);
		}
		const Thermal t = thermal_state(table(), whole(x));
		return t.defined ? t.temperature : kelvin(x.cell);
	}
	// what it holds as a whole, at its temperature as a whole: its own gas, every layer's solids and liquids
	Fill fill(const Item &x) const {
		if (!layered(x)) {
			return fill_of(table(), world().snapshot_cell(x.cell), kelvin(x.cell), &world().meta(x.cell));
		}
		Fill f = fill_of(table(), world().snapshot_cell(x.cell), kelvin(x.cell), &world().meta(x.cell));
		for (const CellId c : layers_of(x)) {
			const Fill l = fill_of(table(), world().snapshot_cell(c), kelvin(c));
			f.condensed += l.defined ? l.condensed : 0.0;
		}
		return f;
	}

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
			why = "\"" + id + "\" is not " + (kind == Kind::Burner ? "a burner" : kind == Kind::Bag ? "a balloon" : kind == Kind::Viewer ? "a spectral viewer" : kind == Kind::Thermal ? "a thermal camera" : kind == Kind::Balance ? "a balance" : kind == Kind::Spectro ? "a spectrophotometer" : "a vessel");
			return false;
		}
		if (is == Kind::Vessel && items[static_cast<size_t>(index)].burst) {
			why = "\"" + id + "\" is broken: its glass burst";
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

		if (bench.contains("readings")) { // (Stage 50)
			const json &r = bench.at("readings");
			if (!r.is_string() || (r.get<std::string>() != "truth" && r.get<std::string>() != "instruments")) {
				why = "a bench's readings are \"truth\" or \"instruments\"";
				return nullptr;
			}
			impl->instruments = r.get<std::string>() == "instruments";
		}
		if (bench.contains("seed")) {
			if (!bench.at("seed").is_number_unsigned()) {
				why = "a bench's seed is a whole number, 0 or more";
				return nullptr;
			}
			impl->seed = bench.at("seed").get<uint64_t>();
		}
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
				default_size(kind, number(j, "volume_ml", 0.0), j.contains("coolant"), item.w, item.h, j.value("glassware", ""));
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
			if (kind == "viewer" || kind == "thermal") {
				// the spectral viewer (Stage 42), a thermal camera (Stage 48): no matter, no cell, no edge
				item.kind = kind == "viewer" ? Kind::Viewer : Kind::Thermal;
				impl->items.push_back(item);
				continue;
			}
			if (kind == "balance") {
				// a balance (Stage 50): no matter, no cell, no edge; of a model of data/instruments.json
				item.kind = Kind::Balance;
				item.model = j.value("model", instruments::balances().front().name);
				if (instruments::balance(item.model) == nullptr) {
					why = "item \"" + item.id + "\": there is no balance of the model \"" + item.model + "\"";
					return nullptr;
				}
				impl->items.push_back(item);
				continue;
			}
			if (kind == "spectrophotometer") {
				// a spectrophotometer (Stage 55): no matter, no cell, no edge; set at a wavelength, not yet blanked
				item.kind = Kind::Spectro;
				item.nm = j.value("nm", 600.0);
				if (!(item.nm >= spectro::model().first_nm && item.nm <= spectro::model().last_nm)) {
					why = "item \"" + item.id + "\": its wavelength is within its range";
					return nullptr;
				}
				impl->items.push_back(item);
				continue;
			}
			if (kind != "vessel" && kind != "balloon" && kind != "stopper") {
				why = "item \"" + item.id + "\": a kind is vessel, balloon, stopper, burner, viewer, thermal, balance or spectrophotometer";
				return nullptr;
			}
			if (j.contains("label") && !j.at("label").is_string()) {
				why = "item \"" + item.id + "\": a label is text";
				return nullptr;
			}
			item.label = j.value("label", ""); // what it is called on its glass (Stage 50)
			item.kind = kind == "vessel" ? Kind::Vessel : kind == "stopper" ? Kind::Stopper : Kind::Bag;
			item.hold_n = item.kind == Kind::Stopper ? number(j, "hold_n", 30.0) : 0.0;
			item.capacity = item.kind == Kind::Bag ? number(j, "capacity_ml", 3000.0) * 1e-6 : 0.0;
			double volume = item.kind == Kind::Vessel ? j.at("volume_ml").get<double>() * 1e-6 : 0.0;
			item.nominal_ml = item.kind == Kind::Vessel ? j.at("volume_ml").get<double>() : 0.0;
			std::optional<glass::Vessel> made;
			if (item.kind == Kind::Vessel && j.contains("glassware")) {
				// GAME_SPEC §12.3, Stage 24: its standard's shape, and what that shape holds to the brim
				item.glassware = j.at("glassware").get<std::string>();
				std::string no;
				made = glass::make(item.glassware, item.nominal_ml, no);
				if (!made) {
					why = "item \"" + item.id + "\": " + no;
					return nullptr;
				}
				if (j.contains("wall_mm") || j.contains("floor_mm")) {
					why = "item \"" + item.id + "\": its glassware gives its wall and floor";
					return nullptr;
				}
				volume = made->capacity() * 1e-6;
			}
			item.cell = item.kind == Kind::Vessel ? b.add_rigid_cell(item.id, volume) : b.add_cell(item.id);
			std::vector<CellId> built; // its column's layers, as many as its bench lists (Stage 47)
			if (made) {
				// its column: no layer until it is layered (they are made as it needs them); its cross-section its floor's
				const double r = made->radius_inside(made->floor + 1e-3) * 1e-2;
				b.set_column(item.cell, 3.14159265358979323846 * r * r);
				// its shape (Stage 51): the inside's volume at heights over its floor, to its brim (a conical flask
				// narrows as it rises)
				constexpr int POINTS = 64;
				const double rise_cm = (made->height - made->floor) / (POINTS - 1);
				std::vector<double> shape(POINTS);
				for (int k = 0; k < POINTS; ++k) {
					shape[static_cast<size_t>(k)] = made->volume_below(made->floor + k * rise_cm) * 1e-6;
				}
				b.set_column_shape(item.cell, rise_cm * 1e-2, std::move(shape));
				if (j.contains("layers") && j.at("layers").is_array()) {
					CellId over = item.cell;
					for (size_t k = 1; k < j.at("layers").size(); ++k) {
						const CellId layer = b.add_layer_cell(item.id + ".layer" + std::to_string(k));
						b.add_column_edge(layer, over);
						built.push_back(layer);
						over = layer;
					}
				}
			}
			const SpeciesId wall = table.find(j.at("wall").get<std::string>());
			if (!table.has(wall) || !table.species(wall).wall) {
				why = "item \"" + item.id + "\": its wall is not a wall species";
				return nullptr;
			}
			b.set_amount(item.cell, wall, amount(j.at("wall_g").get<double>() / (static_cast<double>(table.molar_mass_ug(wall)) * 1e-6)));
			item.wall_mm = made ? made->wall * 10.0 : number(j, "wall_mm", 0.0);
			item.floor_mm = made ? made->floor * 10.0 : number(j, "floor_mm", 0.0);
			if (!(item.wall_mm >= 0.0) || !(item.floor_mm >= 0.0)) {
				why = "item \"" + item.id + "\": a wall is no thinner than nothing, nor a floor";
				return nullptr;
			}
			double condensed = 0.0;
			// what it holds; or, layered (Stage 47), what each layer holds from the top down, the first its own cell's
			std::vector<std::pair<CellId, const json *>> holds;
			if (j.contains("layers")) {
				if (j.contains("contents") || !j.at("layers").is_array() || j.at("layers").empty() || j.at("layers").size() > built.size() + 1) {
					why = "item \"" + item.id + "\": its layers are a list of what each holds, from the top down (a vessel of a standard shape), and no contents beside";
					return nullptr;
				}
				for (size_t k = 0; k < j.at("layers").size(); ++k) {
					holds.push_back({ k == 0 ? item.cell : built[k - 1], &j.at("layers")[k] });
				}
			} else if (j.contains("contents")) {
				holds.push_back({ item.cell, &j.at("contents") });
			}
			for (const auto &[into, what] : holds) {
				for (const auto &[name, spec] : what->items()) {
					const SpeciesId id = table.find(name);
					if (!table.has(id) || table.species(id).wall) {
						why = "item \"" + item.id + "\": there is no species \"" + name + "\" to put in it";
						return nullptr;
					}
					if (into != item.cell && table.species(id).phase == Phase::Gas) {
						why = "item \"" + item.id + "\": a layer under its top holds no gas";
						return nullptr;
					}
					const double mol = mol_of(table, id, spec, impl->seed, item.id + ":" + name); // (an unknown's range, Stage 54)
					b.set_amount(into, id, amount(mol));
					if (spec.contains("particles")) {
						b.set_particles(into, id, spec.at("particles").get<int64_t>());
					}
					const SpeciesDef &def = table.species(id);
					if (def.phase == Phase::Liquid || def.phase == Phase::Solid) {
						condensed += mol * def.molar_volume;
					}
				}
				if (into != item.cell) {
					b.set_temperature(into, room_kelvin);
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
		// the mains (Stage 32): what does the stirrers' work, along an edge to every vessel (no heat, no matter)
		impl->mains = b.add_reservoir("mains", WORK_KELVIN);
		for (Item &item : impl->items) {
			if (item.kind == Kind::Vessel) {
				item.stirs = b.add_edge(impl->mains, item.cell, PHASES_NONE, 0.0);
			}
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
	// the game's goals (§13.5): locked
	if (bench.contains("goals")) {
		if (!bench.at("goals").is_array()) {
			why = "the bench's \"goals\" are a list";
			return nullptr;
		}
		for (const json &j : bench.at("goals")) {
			Goal g;
			g.game = true;
			if (!j.is_object() || !j.contains("id") || !j.at("id").is_string() || j.at("id").get<std::string>().empty() || !j.contains("name") || !j.at("name").is_string()) {
				why = "a goal of the bench has an \"id\" and a \"name\"";
				return nullptr;
			}
			g.id = j.at("id").get<std::string>();
			g.name = j.at("name").get<std::string>();
			if (!Impl::slot_of(j, g.slot, why) || !Impl::terms_from(j.value("terms", json()), g.terms, why)) {
				why = "the bench's goal \"" + g.id + "\": " + why;
				return nullptr;
			}
			if (impl->find_goal(g.id) >= 0 || impl->goal_in(g.slot) >= 0 || impl->find(g.id) >= 0) {
				why = "the bench's goal \"" + g.id + "\" shares its id or its slot";
				return nullptr;
			}
			impl->goals.push_back(g);
		}
	}
	impl->detect(true); // what is there from the start is not news
	return std::unique_ptr<Lab>(new Lab(std::move(impl)));
}

bool Lab::Impl::run(const json &c, std::string &why) {
	if (!c.is_object() || !c.contains("do") || !c.at("do").is_string()) {
		why = "a command is an object with a \"do\"";
		return false;
	}
	const std::string what = c.at("do").get<std::string>();

	// what is in the trash (Stage 34) takes no command but those that take it out, a wait, and a rig taken off it
	if (!trash.empty() && what != "trash" && what != "restore" && what != "empty_trash" && what != "place" && what != "stow" && what != "wait" && what != "wait_until" && what != "unrig") {
		std::string id;
		if (names_trashed(c, id)) {
			why = "\"" + id + "\" is in the trash: restore it first";
			return false;
		}
	}
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
		const Item &held_item = items[static_cast<size_t>(at)]; // (its column as a whole, Stage 47)
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
				return grams(held_item, species) < c.at("below_g").get<double>();
			}
			if (c.contains("above_g")) {
				return grams(held_item, species) > c.at("above_g").get<double>();
			}
			if (c.contains("below_c")) {
				return kelvin(held_item) - 273.15 < c.at("below_c").get<double>();
			}
			if (c.contains("above_c")) {
				return kelvin(held_item) - 273.15 > c.at("above_c").get<double>();
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
		// from the cabinet or the trash onto the bench, or along it
		int thing = -1;
		if (c.contains("item") && c.at("item").is_string() && find_binned(c.at("item").get<std::string>()) >= 0) {
			// a signal part carried out of the trash, with all it holds, onto a free square of the bench (Stage 34)
			const std::string id = c.at("item").get<std::string>();
			if (trash_at(id) < 0) {
				why = "\"" + id + "\" is in \"" + binned[static_cast<size_t>(find_binned(id))].in + "\", in the trash: restore that";
				return false;
			}
			if (!c.contains("at") || !c.at("at").is_array() || c.at("at").size() != 2) {
				why = "a place is \"at\" [column, row]";
				return false;
			}
			const int col = c.at("at").at(0).get<int>(), row = c.at("at").at(1).get<int>();
			if (!square_free(std::string(), col, row, -1, why)) {
				return false;
			}
			unbin_part(id, false);
			forget_trash(id);
			Part &p = parts[static_cast<size_t>(find_part(id))];
			p.in.clear();
			p.col = col;
			p.row = row;
			return true;
		}
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
		if (x.kind == Kind::Vessel && x.burst) {
			why = "\"" + x.id + "\" is broken: its glass burst";
			return false;
		}
		if (x.kind == Kind::Tray || (x.place != Place::Cabinet && x.place != Place::Bench && x.place != Place::Trash) || x.holds >= 0) {
			why = "\"" + x.id + "\" cannot be moved as it is: it is joined to something, or handed in";
			return false;
		}
		if (const int fixed = fixed_under(thing); fixed >= 0) {
			why = "\"" + x.id + "\" cannot be moved as it is: \"" + items[static_cast<size_t>(fixed)].id + "\" depends on where it stands";
			return false;
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
		if (!take_off_drains(thing, why)) { // moved away from an outlet, or its outlet from what stood under it
			return false;
		}
		forget_trash(x.id); // carried out of the trash (Stage 34)
		const Item was = x;
		x.place = Place::Bench;
		x.col = col;
		x.row = row;
		leave_balances(thing, was); // lifted off a balance, which stays where it stood (Stage 50)
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
				if (x.kind == Kind::Tray || x.place != Place::Bench || x.holds >= 0) {
					why = "\"" + x.id + "\" cannot be moved as it is: it is not on the bench, or it is joined to something";
					return false;
				}
				if (const int fixed = fixed_under(thing); fixed >= 0) { // (a balance under it goes with it: Stage 50)
					why = "\"" + x.id + "\" cannot be moved as it is: \"" + items[static_cast<size_t>(fixed)].id + "\" depends on where it stands";
					return false;
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
		// a drain whose two ends both move stays; one with one end moving comes off
		const auto in_move = [&](int i) { return std::find(moving.begin(), moving.end(), i) != moving.end(); };
		for (size_t u = 0; u < items.size() && (dc != 0 || dr != 0); ++u) {
			const int lower = items[u].drains_to;
			if (lower >= 0 && in_move(static_cast<int>(u)) != in_move(lower) && !take_off_drain(u, why)) {
				return false;
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
			why = "a part's \"kind\" is timer, monitor, alarm, notice or writer (a circuit is made of parts: \"circuit\")";
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
		p.notice_level = kind == PartKind::Writer ? static_cast<int>(Notice::Event) : p.notice_level;
		p.col = col;
		p.row = row;
		p.in = in;
		int64_t &count = made[static_cast<int>(kind)];
		p.id = c.value("id", std::string());
		if (p.id.empty()) {
			do {
				p.id = std::string(part_kind_name(kind)) + std::to_string(++count);
			} while (taken(p.id));
		} else if (taken(p.id)) {
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
		for (const std::string &id : held_by(parts, parts[static_cast<size_t>(at)].id)) { // a circuit goes with all it holds
			std::erase_if(wires, [&](const Wire &w) { return w.from == id || w.to == id; });
			std::erase_if(rigs, [&](const Rig &r) { return r.from == id || (r.control == "reset" && r.item == id); });
			std::erase_if(controls, [&](const ControlSetting &s) { return s.item == id; });
			std::erase_if(parts, [&](const Part &p) { return p.id == id; });
		}
		return true;
	}
	if (what == "sense" || what == "unsense") {
		// what a monitor reads, drawn from its sensing port (§13.3, Stage 26): another item forgets its quantity
		int at = -1;
		if (!part(c, "part", std::nullopt, at, why)) {
			return false;
		}
		Part &p = parts[static_cast<size_t>(at)];
		if (!senses(p.kind)) {
			why = "\"" + p.id + "\" senses nothing: a monitor or a controller senses an item";
			return false;
		}
		if (what == "unsense") {
			p.quantity = Quantity{};
			p.watching = false;
			p.last.reset();
			retune(p, false);
			drop_trial(p.id); // (Stage 39)
			return true;
		}
		const std::string item = c.value("item", std::string());
		const int i = find(item);
		if (i < 0 || items[static_cast<size_t>(i)].kind == Kind::Tray) {
			why = "a monitor senses a vessel, a balloon or a burner: there is no \"" + item + "\"";
			return false;
		}
		if (p.quantity.item != item) {
			p.quantity = Quantity{ item, "", "g" };
			p.watching = false;
			p.last.reset();
			retune(p, false);
			drop_trial(p.id); // (Stage 39)
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
		if (p.kind == PartKind::Notice) {
			Notice::Level level = Notice::Warning;
			if (!c.contains("level") || !c.at("level").is_string() || !level_of(c.at("level").get<std::string>(), level)) {
				why = "a notice part is set to a \"level\": detail, event, warning or danger";
				return false;
			}
			const std::string about = c.value("item", std::string());
			if (!about.empty() && find(about) < 0 && find_part(about) < 0 && find_goal(about) < 0) {
				why = "there is no \"" + about + "\" for it to hear about";
				return false;
			}
			const std::string contains = c.value("contains", std::string());
			if (contains.size() > MAX_CONTAINS) {
				why = text_of("a notice part's text is at most %.0f characters", static_cast<double>(MAX_CONTAINS));
				return false;
			}
			const double seconds = number(c, "seconds", 1.0 / 60.0);
			if (!(seconds > 0.0) || seconds > 3600.0) {
				why = "a notice part signals for \"seconds\", more than none and at most an hour";
				return false;
			}
			p.notice_level = static_cast<int>(level);
			p.quantity.item = about;
			p.contains = contains;
			p.hold = std::max<int64_t>(1, static_cast<int64_t>(seconds * 60.0 + 0.5));
			p.held = 0;
			return true;
		}
		if (p.kind == PartKind::Writer) {
			Notice::Level level = Notice::Event;
			if (!c.contains("level") || !c.at("level").is_string() || !level_of(c.at("level").get<std::string>(), level)) {
				why = "a writer is set to a \"level\": detail, event, warning or danger";
				return false;
			}
			const std::string about = c.value("item", std::string()), says = c.value("says", std::string());
			if (!about.empty() && find(about) < 0 && find_part(about) < 0 && find_goal(about) < 0) {
				why = "there is no \"" + about + "\" for it to write about";
				return false;
			}
			if (says.size() > MAX_SAYS) {
				why = text_of("a writer's words are at most %.0f characters", static_cast<double>(MAX_SAYS));
				return false;
			}
			int writes = 0;
			if (c.contains("writes") && (!c.at("writes").is_string() || !writes_of(c.at("writes").get<std::string>(), writes))) {
				why = "a writer \"writes\" when its input turns \"on\", \"off\", or on either \"change\"";
				return false;
			}
			p.notice_level = static_cast<int>(level);
			p.quantity.item = about;
			p.says = says;
			p.writes = writes;
			return true;
		}
		if (p.kind != PartKind::Monitor && p.kind != PartKind::Controller) {
			why = std::string("a") + (p.kind == PartKind::Alarm ? "n alarm" : " circuit") + " has nothing to set";
			return false;
		}
		Quantity q;
		q.item = c.value("item", p.quantity.item); // what it senses, unless the command names an item: then it senses that
		q.what = c.value("what", std::string());
		q.measure = c.value("measure", std::string("g"));
		if (q.what == "met" ? find_goal(q.item) < 0 : find(q.item) < 0 || items[static_cast<size_t>(find(q.item))].kind == Kind::Tray) {
			why = q.what == "met"	? "a monitor reads \"met\" of a goal: there is no goal \"" + q.item + "\""
					: q.item.empty() ? "\"" + p.id + "\" senses nothing: sense an item first, or name its \"item\""
									 : "a monitor watches an item on the bench: there is no \"" + q.item + "\"";
			return false;
		}
		if (!valid(q, why)) {
			return false;
		}
		if (p.kind == PartKind::Controller) {
			// a target and a speed (Stage 26); then it tries what it controls, to tune itself
			if (q.what == "met") {
				why = "a controller holds a quantity of an item, not a goal";
				return false;
			}
			if (!c.contains("target") || !c.at("target").is_number() || !std::isfinite(c.at("target").get<double>())) {
				why = "a controller is set to a \"target\": the value it holds the quantity at";
				return false;
			}
			const std::string speed = c.value("speed", std::string("normal"));
			if (speed != "gentle" && speed != "normal" && speed != "fast") {
				why = "a controller's \"speed\" is gentle, normal or fast";
				return false;
			}
			p.watching = true;
			p.quantity = q;
			p.target = c.at("target").get<double>();
			p.speed = speed == "gentle" ? 0 : speed == "normal" ? 1 : 2;
			tune(at);
			return true;
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
		if (parts[static_cast<size_t>(from)].kind == PartKind::Alarm || parts[static_cast<size_t>(from)].kind == PartKind::Writer) {
			why = std::string(parts[static_cast<size_t>(from)].kind == PartKind::Alarm ? "an alarm" : "a writer") + " sends no signal: it has no output to wire";
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
			if (parts[static_cast<size_t>(from)].kind == PartKind::Controller) {
				tune(from); // what it controls is not what it was
			}
			return true;
		}
		if (!sends(parts[static_cast<size_t>(from)].kind)) {
			why = parts[static_cast<size_t>(from)].kind == PartKind::Circuit ? "a circuit is rigged by the parts in it: open it"
					: std::string(parts[static_cast<size_t>(from)].kind == PartKind::Alarm ? "an alarm" : "a writer") + " sends no signal: it has no output to rig";
			return false;
		}
		const bool targets = r.control == "reset" ? find_part(r.item) >= 0 && parts[static_cast<size_t>(find_part(r.item))].kind == PartKind::Timer
								: control_now(r.item, r.control).has_value();
		if (!targets) {
			why = "there is no " + r.control + " on \"" + r.item + "\" to rig: a vessel's stir, a burner's valve, an upside-down vessel's cap, a timer's reset";
			return false;
		}
		if (there != rigs.end()) {
			why = "a rig already runs from \"" + r.from + "\" to the " + r.control + " of \"" + r.item + "\"";
			return false;
		}
		if (parts[static_cast<size_t>(from)].kind == PartKind::Controller) {
			// it follows the controller's level: a valve to it, stirring by PWM (Stage 26)
			if (r.control != "valve" && r.control != "stir" && r.control != "cap") {
				why = "a controller's rig follows its level: a burner's valve, a cap's opening, or a vessel's stirring by PWM; a timer's reset cannot";
				return false;
			}
			if (c.contains("on") || c.contains("off") || c.contains("change")) {
				why = "a controller's rig has no settings: the control follows its level";
				return false;
			}
			if (there != rigs.end()) {
				why = "a rig already runs from \"" + r.from + "\" to the " + r.control + " of \"" + r.item + "\"";
				return false;
			}
			r.follow = true;
			rigs.push_back(r);
			tune(from);
			return true;
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
			} while (taken(k.id));
		} else if (taken(k.id)) {
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
			const bool binned_part = c.contains("item") && c.at("item").is_string() && find_binned(c.at("item").get<std::string>()) >= 0;
			why = binned_part ? "\"" + c.at("item").get<std::string>() + "\" is in the trash: restore it first" : "there is no such item to stow";
			return false;
		}
		Item &x = items[static_cast<size_t>(thing)];
		const bool free = x.kind != Kind::Tray && (x.place == Place::Bench || x.place == Place::Trash) && x.holds < 0 && fixed_under(thing) < 0; // from the trash too (Stage 34)
		if (!free) {
			why = "\"" + x.id + "\" cannot go back in the cabinet as it is";
			return false;
		}
		if (!take_off_drains(thing, why)) {
			return false;
		}
		forget_trash(x.id);
		const Item was = x;
		x.place = Place::Cabinet;
		x.col = x.row = -1;
		leave_balances(thing, was); // (Stage 50)
		return true;
	}
	if (what == "trash") {
		// into the trash (GAME_SPEC §13, Stage 34): an item from the bench or the cabinet, as it is stowed; a
		// signal part with all it holds, its settings kept. Past the trash's capacity the oldest leaves it.
		if (!c.contains("item") || !c.at("item").is_string()) {
			why = "the command needs \"item\"";
			return false;
		}
		const std::string id = c.at("item").get<std::string>();
		if (in_trash(id)) {
			why = "\"" + id + "\" is already in the trash";
			return false;
		}
		const int thing = find(id);
		if (thing < 0 && find_part(id) < 0) {
			why = "there is no such item to trash";
			return false;
		}
		if (thing < 0) {
			bin_part(id);
			trash.push_back(Binned{ id, true, -1, -1 });
			trim_trash();
			return true;
		}
		Item &x = items[static_cast<size_t>(thing)];
		std::string no;
		if (x.kind == Kind::Tray) {
			no = "the tray is the bench's own: it does not go in the trash";
		} else if (x.place == Place::Outbox) {
			no = "\"" + x.id + "\" has been handed in";
		} else if (x.place == Place::On || x.place == Place::Under) {
			const int other = x.place == Place::On ? x.on : x.under;
			no = "\"" + x.id + "\" is " + (x.place == Place::On ? "on" : "under") + " \"" + (other >= 0 ? items[static_cast<size_t>(other)].id : std::string("something")) + "\": take it away first";
		} else if (x.holds >= 0) {
			no = "\"" + x.id + "\" has \"" + items[static_cast<size_t>(x.holds)].id + "\" on it: take it off first";
		}
		if (const int fixed = fixed_under(thing); no.empty() && fixed >= 0) { // (a balance is left behind: Stage 50)
			no = "\"" + x.id + "\" stands over \"" + items[static_cast<size_t>(fixed)].id + "\": take the burner away first";
		}
		if (!no.empty()) {
			why = no;
			return false;
		}
		if (!take_off_drains(thing, why)) {
			return false;
		}
		trash.push_back(Binned{ x.id, false, x.place == Place::Bench ? x.col : -1, x.place == Place::Bench ? x.row : -1 });
		const Item was = x;
		x.place = Place::Trash;
		x.col = x.row = -1;
		leave_balances(thing, was); // (Stage 50)
		trim_trash();
		return true;
	}
	if (what == "empty_trash") {
		// the trash emptied (Stage 38): everything in it leaves for good, as the oldest does past its capacity: an
		// item to the cabinet (it holds matter, which nothing destroys), a part scrapped with all it holds
		if (trash.empty()) {
			why = "the trash is empty";
			return false;
		}
		const size_t count = trash.size();
		while (!trash.empty()) {
			const Binned oldest = trash.front();
			trash.erase(trash.begin());
			if (oldest.part) {
				unbin_part(oldest.id, true);
			} else {
				items[static_cast<size_t>(find(oldest.id))].place = Place::Cabinet;
			}
		}
		note(Notice::Detail, "", text_of("the trash was emptied: %.0f things, its items to the cabinet", static_cast<double>(count)));
		return true;
	}
	if (what == "restore") {
		// out of the trash, back where it was (Stage 34): the newest, or the one named
		if (c.contains("item") && !c.at("item").is_string()) {
			why = "a restore names an \"item\", or none: the newest in the trash";
			return false;
		}
		if (!c.contains("item") && trash.empty()) {
			why = "the trash is empty";
			return false;
		}
		const std::string id = c.contains("item") ? c.at("item").get<std::string>() : trash.back().id;
		const int at = trash_at(id);
		if (at < 0) {
			const int inner = find_binned(id);
			why = inner >= 0 ? "\"" + id + "\" is in \"" + binned[static_cast<size_t>(inner)].in + "\", in the trash: restore that" : "\"" + id + "\" is not in the trash";
			return false;
		}
		const Binned b = trash[static_cast<size_t>(at)];
		if (b.part) {
			// to its square, on the bench or in the circuit it was in; taken, it stays in the trash
			const Part &p = binned[static_cast<size_t>(find_binned(id))];
			std::string taken_by;
			if (!square_free(p.in, p.col, p.row, -1, taken_by)) {
				why = "\"" + id + "\" cannot go back where it was: " + taken_by + ". It stays in the trash: place it on a free square";
				return false;
			}
			unbin_part(id, false);
		} else {
			// to its square, or to the cabinet: where it came from, or because its square is taken
			Item &x = items[static_cast<size_t>(find(id))];
			const int there = b.col >= 0 ? standing_in(b.col, b.row, x.w, x.h) : -1;
			if (b.col >= 0 && there < 0 && on_grid(b.col, b.row, x.w, x.h) && !(x.kind == Kind::Vessel && x.burst)) {
				x.place = Place::Bench;
				x.col = b.col;
				x.row = b.row;
			} else {
				x.place = Place::Cabinet;
				if (there >= 0) {
					note(Notice::Detail, x.id, x.id + " went to the cabinet: \"" + name_of(there) + "\" stands where it stood");
				}
			}
		}
		trash.erase(trash.begin() + at);
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
		for (const Item &other : items) { // one thing under a vessel: a burner or a balance (Stage 50)
			if (other.under == vessel) {
				why = "\"" + items[static_cast<size_t>(vessel)].id + "\" stands over \"" + other.id + "\": take it away first";
				return false;
			}
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
	if (what == "weigh" || what == "insert") {
		// a balance under a vessel (GAME_SPEC §13, Stage 50), as a burner goes under one: from the bench or straight
		// from the cabinet; a spectrophotometer under a cuvette, the cuvette in its well (Stage 55). Neither joins
		// anything in the world.
		const bool weighs = what == "weigh";
		int balance = -1, vessel = -1;
		if (!item(c, weighs ? "balance" : "spectrophotometer", weighs ? Kind::Balance : Kind::Spectro, balance, why) || !item(c, weighs ? "vessel" : "cuvette", Kind::Vessel, vessel, why)) {
			return false;
		}
		Item &scale = items[static_cast<size_t>(balance)];
		const Item &v = items[static_cast<size_t>(vessel)];
		if (v.kind != Kind::Vessel) {
			why = weighs ? "a balance weighs a vessel, with what is on it" : "a spectrophotometer's well takes a cuvette";
			return false;
		}
		if (!weighs && v.glassware != "cuvette") {
			why = "only a cuvette fits \"" + scale.id + "\"'s well";
			return false;
		}
		if (!weighs && v.holds >= 0) {
			why = "\"" + v.id + "\" has \"" + items[static_cast<size_t>(v.holds)].id + "\" on it: the well's lid closes over a cuvette alone";
			return false;
		}
		if (v.place == Place::Outbox || v.place == Place::Trash || (v.place == Place::Cabinet && !c.value("onto", false))) {
			on_bench(v, why);
			return false;
		}
		if (scale.place == Place::Trash || scale.place == Place::Outbox) {
			why = "\"" + scale.id + "\" is " + (scale.place == Place::Trash ? "in the trash" : "handed in");
			return false;
		}
		if (scale.under >= 0) {
			why = "\"" + scale.id + "\" is already under \"" + items[static_cast<size_t>(scale.under)].id + "\"";
			return false;
		}
		for (const Item &other : items) {
			if (other.under == vessel) {
				why = "\"" + v.id + "\" stands over \"" + other.id + "\": take it away first";
				return false;
			}
		}
		if (v.pours_into >= 0) {
			why = "\"" + v.id + "\" is pouring: set it upright first";
			return false;
		}
		if (c.value("onto", false)) {
			// the vessel set on the balance where the balance stands (its foot on the balance's, centred on it), from the
			// bench or the cabinet; as `place` moves it
			if (scale.place != Place::Bench) {
				why = "\"" + scale.id + "\" is not standing on the bench to set anything on";
				return false;
			}
			Item &x = items[static_cast<size_t>(vessel)];
			if (x.holds >= 0 || x.drains_to >= 0 || (x.place != Place::Bench && x.place != Place::Cabinet)) {
				why = "\"" + x.id + "\" cannot be moved as it is: it is joined to something, or handed in";
				return false;
			}
			const int col = scale.col + (scale.w - x.w) / 2, row = scale.row + scale.h - x.h;
			if (!on_grid(col, row, x.w, x.h)) {
				why = "\"" + x.id + "\" does not fit on the bench over \"" + scale.id + "\"";
				return false;
			}
			scale.place = Place::Under; // (its squares are the vessel's now)
			const int there = standing_in(col, row, x.w, x.h, vessel);
			scale.place = Place::Bench;
			if (there >= 0) {
				why = "\"" + name_of(there) + "\" stands where \"" + x.id + "\" would stand on \"" + scale.id + "\"";
				return false;
			}
			if (!take_off_drains(vessel, why)) {
				return false;
			}
			forget_trash(x.id);
			x.place = Place::Bench;
			x.col = col;
			x.row = row;
		} else if (!on_bench(v, why)) {
			return false;
		}
		scale.under = vessel;
		scale.place = Place::Under;
		return true;
	}
	if (what == "unweigh" || what == "remove") {
		int balance = -1;
		if (!(what == "unweigh" ? item(c, "balance", Kind::Balance, balance, why) : item(c, "spectrophotometer", Kind::Spectro, balance, why))) {
			return false;
		}
		Item &scale = items[static_cast<size_t>(balance)];
		if (scale.under < 0) {
			why = "\"" + scale.id + "\" is not under anything";
			return false;
		}
		// it goes back beside the vessel if there is room, else to the cabinet
		const Item v = items[static_cast<size_t>(scale.under)];
		scale.place = Place::Cabinet;
		scale.col = scale.row = -1;
		set_beside(balance, v);
		scale.under = -1;
		return true;
	}
	if (what == "tune") {
		// (Stage 55) a spectrophotometer's wavelength, to the nanometre; its blank stays as it was taken
		int spectro = -1;
		if (!item(c, "spectrophotometer", Kind::Spectro, spectro, why)) {
			return false;
		}
		const double nm = std::round(number(c, "nm", -1.0));
		if (!(nm >= spectro::model().first_nm && nm <= spectro::model().last_nm)) {
			why = "a spectrophotometer is tuned to \"nm\" within " + std::to_string(static_cast<int>(spectro::model().first_nm)) + " to " + std::to_string(static_cast<int>(spectro::model().last_nm));
			return false;
		}
		items[static_cast<size_t>(spectro)].nm = nm;
		return true;
	}
	if (what == "holder") {
		// (Stage 56) its cell holder set to a temperature, or off ("celsius": null or absent)
		int spectro = -1;
		if (!item(c, "spectrophotometer", Kind::Spectro, spectro, why)) {
			return false;
		}
		const spectro::Model &model = spectro::model();
		double celsius = std::numeric_limits<double>::quiet_NaN();
		if (c.contains("celsius") && !c.at("celsius").is_null()) {
			celsius = number(c, "celsius", -1000.0);
			if (!(celsius >= model.holder_least_c && celsius <= model.holder_most_c)) {
				why = "a cell holder is set between " + std::to_string(static_cast<int>(model.holder_least_c)) + " and " + std::to_string(static_cast<int>(model.holder_most_c)) + " C, or off";
				return false;
			}
		}
		if (!boundary(why)) {
			return false;
		}
		items[static_cast<size_t>(spectro)].holder_c = celsius;
		return true;
	}
	if (what == "blank") {
		// (Stage 55) 100 % T through what stands in its well now, at the wavelength it is set to
		int spectro = -1;
		if (!item(c, "spectrophotometer", Kind::Spectro, spectro, why)) {
			return false;
		}
		Item &x = items[static_cast<size_t>(spectro)];
		if (!on_bench(x, why)) {
			return false;
		}
		if (x.under < 0) {
			why = "\"" + x.id + "\" has nothing in its well to blank with: a cuvette of the solvent first";
			return false;
		}
		x.blank = spectro_signal(x);
		x.blank_nm = x.nm;
		return true;
	}
	if (what == "tare") {
		// (Stage 50) its display to 0: the tare is its load's conventional mass now
		int balance = -1;
		if (!item(c, "balance", Kind::Balance, balance, why)) {
			return false;
		}
		Item &scale = items[static_cast<size_t>(balance)];
		if (!on_bench(scale, why)) {
			return false;
		}
		scale.tare = gross_g(scale);
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
	if (what == "hold" || what == "invert") {
		// a vessel in a pouring position over another: held, or upside down with its cap shut (§13, Stage 27)
		int thing = -1, over = -1;
		if (!item(c, "item", Kind::Vessel, thing, why) || !item(c, "over", Kind::Vessel, over, why)) {
			return false;
		}
		return start_pour(thing, over, what == "invert", why);
	}
	if (what == "unpour") {
		int thing = -1;
		if (!item(c, "item", Kind::Vessel, thing, why)) {
			return false;
		}
		if (items[static_cast<size_t>(thing)].pours_into < 0) {
			why = "\"" + items[static_cast<size_t>(thing)].id + "\" is not over a vessel";
			return false;
		}
		return end_pour(static_cast<size_t>(thing), why);
	}
	if (what == "cap") {
		int thing = -1;
		if (!item(c, "item", Kind::Vessel, thing, why)) {
			return false;
		}
		const double to = number(c, "to", -1.0);
		if (!(to >= 0.0 && to <= 1.0)) {
			why = "a cap is opened \"to\" 0 (shut) to 1 (open)";
			return false;
		}
		if (!control_now(items[static_cast<size_t>(thing)].id, "cap")) {
			why = "\"" + items[static_cast<size_t>(thing)].id + "\" has no cap: a bottle's screws on, upright; any vessel's opens, upside down over another";
			return false;
		}
		if (!boundary(why)) {
			return false;
		}
		if (!set_control(items[static_cast<size_t>(thing)].id, "cap", to, "player")) {
			why = "\"" + items[static_cast<size_t>(thing)].id + "\" is not at the room's pressure yet: it cannot be capped as it is";
			return false;
		}
		return true;
	}
	if (what == "pour") {
		// pouring is a control (§13, Stage 27): a held pour's tilt; or, from a script, an amount at full tilt, waited out
		int from = -1;
		if (!item(c, "from", Kind::Vessel, from, why)) {
			return false;
		}
		bool started = false;
		if (c.contains("to")) {
			int to = -1;
			if (!item(c, "to", Kind::Vessel, to, why)) {
				return false;
			}
			if (items[static_cast<size_t>(from)].pours_into != to) {
				if (!start_pour(from, to, false, why)) {
					return false;
				}
				started = true;
			}
		}
		Item &x = items[static_cast<size_t>(from)];
		touched.resize(items.size(), 0);
		touched[static_cast<size_t>(from)] = 1; // what a hand moves is not noticed as appearing
		if (x.pours_into >= 0) {
			touched[static_cast<size_t>(x.pours_into)] = 1;
		}
		if (x.pours_into < 0) {
			why = "\"" + x.id + "\" is not over a vessel: name one, \"to\"";
			return false;
		}
		if (x.inverted) {
			why = "upside down, \"" + x.id + "\" pours through its cap: open the cap";
			return false;
		}
		if (c.contains("rate")) {
			const double rate = number(c, "rate", -1.0);
			if (!(rate >= 0.0 && rate <= 1.0)) {
				why = "a held pour's \"rate\" is 0 to 1: its tilt's share of the full";
				return false;
			}
			x.tilt = rate;
			return true;
		}
		const double volume = liquid_m3(x); // (all of a layered vessel's, Stage 47)
		// (Stage 50: "about" an amount, what a hand pours of it, and at most what there is)
		double wanted = c.value("all", false) ? volume : number(c, "ml", -1.0) * 1e-6 * by_hand(c, "pour", x, items[static_cast<size_t>(x.pours_into)]);
		if (c.value("about", false) && wanted > volume) {
			wanted = volume;
		}
		if (!(wanted > 0.0)) {
			if (started) {
				end_pour(static_cast<size_t>(from), why);
			}
			why = c.value("all", false) ? "\"" + x.id + "\" holds no liquid" : "a pour is of a \"rate\", or of \"ml\" or \"all\"";
			return false;
		}
		if (wanted > volume * (1.0 + 1e-9)) {
			if (started) {
				end_pour(static_cast<size_t>(from), why);
			}
			why = "\"" + x.id + "\" does not hold that much liquid";
			return false;
		}
		// full tilt until that much has run (at most a minute of the lab past what its rate takes), then upright; or,
		// "gentle" (Stage 47), a trickle, laid on what is there as a layer of its own
		const double tilted = std::max(glass::find(x.glassware, x.nominal_ml)->pour_ml_s * 1e-6, 1e-12);
		const bool gentle = c.value("gentle", false);
		const double full_rate = gentle ? std::min(GENTLE_POUR, tilted) : tilted;
		x.gentle = gentle; // (down the wall: it lands at no speed, Stage 51)
		const int64_t most = static_cast<int64_t>((wanted / full_rate + 60.0) * 60.0);
		x.tilt = full_rate / tilted;
		double ran = 0.0; // what the drain was given to run: its rate's volume each step, not what evaporation took
		for (int64_t k = 0; k < most; ++k) {
			Item &y = items[static_cast<size_t>(from)];
			const double left = wanted - ran, there = liquid_m3(y);
			if (!(left > wanted * 1e-9) || there <= 0.0) {
				break;
			}
			y.pour_left_m3 = left;
			ran += std::min({ full_rate * chem::STEP_SECONDS, left, there });
			if (!step_once(why)) {
				return false;
			}
		}
		items[static_cast<size_t>(from)].pour_left_m3 = -1.0;
		items[static_cast<size_t>(from)].tilt = 0.0;
		items[static_cast<size_t>(from)].gentle = false;
		return !started || end_pour(static_cast<size_t>(from), why);
	}
	if (what == "pour" || what == "tip") {
		int from = -1, to = -1;
		if (!item(c, "from", Kind::Vessel, from, why) || !item(c, "to", Kind::Vessel, to, why)) {
			return false;
		}
		touched.resize(items.size(), 0);
		touched[static_cast<size_t>(from)] = touched[static_cast<size_t>(to)] = 1; // what a hand moves is not noticed as appearing
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
		if (sealed(items[static_cast<size_t>(from)]) || sealed(items[static_cast<size_t>(to)])) { // Stage 28
			why = "\"" + items[static_cast<size_t>(sealed(items[static_cast<size_t>(from)]) ? from : to)].id + "\" is sealed: open it first";
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
		} else if (!c.contains("species")) {
			// (Stage 50) the solids, each in proportion to its mass: a mass of them, or all
			double solids_g = 0.0;
			for (size_t s = 0; s < source.amount.size(); ++s) {
				const SpeciesId id{ static_cast<int32_t>(s) };
				if (source.amount[s] > 0 && table().species(id).phase == Phase::Solid && !table().species(id).wall) {
					solids_g += static_cast<double>(source.amount[s]) * MOL_PER_QUANTUM * static_cast<double>(table().molar_mass_ug(id)) * 1e-6;
				}
			}
			if (!(solids_g > 0.0)) {
				why = "\"" + items[static_cast<size_t>(from)].id + "\" holds no solid";
				return false;
			}
			double fraction = 1.0;
			if (!c.value("all", false)) {
				const double g = number(c, "g", -1.0) * by_hand(c, "tip", items[static_cast<size_t>(from)], items[static_cast<size_t>(to)]);
				if (!(g > 0.0)) {
					why = "a tip is of \"g\" or \"all\"";
					return false;
				}
				if (g > solids_g * (1.0 + 1e-9) && !c.value("about", false)) {
					why = "\"" + items[static_cast<size_t>(from)].id + "\" does not hold that much solid";
					return false;
				}
				fraction = std::min(1.0, g / solids_g);
			}
			for (size_t s = 0; s < source.amount.size(); ++s) {
				const SpeciesId id{ static_cast<int32_t>(s) };
				if (source.amount[s] > 0 && table().species(id).phase == Phase::Solid && !table().species(id).wall) {
					const int64_t moves = fraction >= 1.0 ? source.amount[s] : static_cast<int64_t>(static_cast<double>(source.amount[s]) * fraction);
					if (moves > 0) {
						amounts.push_back(SpeciesAmount{ id, Amount{ moves } });
					}
				}
			}
		} else {
			// one solid: a mass of it, or all
			if (!c.at("species").is_string()) {
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
				const double g = number(c, "g", -1.0) * by_hand(c, "tip", items[static_cast<size_t>(from)], items[static_cast<size_t>(to)]);
				if (!(g > 0.0)) {
					why = "a tip is of \"g\" or \"all\"";
					return false;
				}
				bool fits = false;
				moves = amount_from_mol(g / (static_cast<double>(table().molar_mass_ug(id)) * 1e-6), fits).quanta;
				if (fits && moves > have && c.value("about", false)) {
					moves = have; // (Stage 50) a hand takes what there is
				}
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
	if (what == "probe") {
		// the spectral viewer's probe (GAME_SPEC §13, Stage 42): fixed on an item or a part at a place of it in its own
		// frame ("on", "at": 0 to 1 across and down it), on the room ("at" alone: in the bench's squares from its
		// top-left corner), or back in its holder (neither). It reads always; this only says where.
		int at = -1;
		if (!item(c, "viewer", Kind::Viewer, at, why)) {
			return false;
		}
		Item &v = items[static_cast<size_t>(at)];
		if (!on_bench(v, why)) {
			return false;
		}
		double u = 0.5, w = 0.5;
		const bool placed = c.contains("at");
		if (placed) {
			const json &a = c.at("at");
			if (!a.is_array() || a.size() != 2 || !a.at(0).is_number() || !a.at(1).is_number() || !std::isfinite(a.at(0).get<double>()) || !std::isfinite(a.at(1).get<double>())) {
				why = "a probe's \"at\" is two numbers";
				return false;
			}
			u = a.at(0).get<double>();
			w = a.at(1).get<double>();
		}
		if (c.contains("on")) {
			if (!c.at("on").is_string()) {
				why = "a probe is put \"on\" an item or a part, by its id";
				return false;
			}
			const std::string on = c.at("on").get<std::string>();
			if (find_binned(on) >= 0) {
				why = "\"" + on + "\" is in the trash";
				return false;
			}
			if (find(on) < 0 && find_part(on) < 0) {
				why = "there is no \"" + on + "\" to put a probe on";
				return false;
			}
			v.probe_on = on;
			v.probe_room = false;
		} else {
			v.probe_on.clear();
			v.probe_room = placed;
		}
		v.probe_u = v.probe_on.empty() && !v.probe_room ? 0.0 : u;
		v.probe_v = v.probe_on.empty() && !v.probe_room ? 0.0 : w;
		return true;
	}
	if (what == "frame") {
		// a thermal camera's frame (GAME_SPEC §13, Stage 48): fixed on the room ("room": its top-left in the bench's
		// squares from the bench's top-left corner), or back over the camera's own screen ("home"). It reads always.
		int at = -1;
		if (!item(c, "item", Kind::Thermal, at, why)) {
			return false;
		}
		Item &v = items[static_cast<size_t>(at)];
		if (!on_bench(v, why)) {
			return false;
		}
		if (c.value("home", false)) {
			v.thermal.frame_room = false;
			v.thermal.frame_x = v.thermal.frame_y = 0.0;
			return true;
		}
		const json &a = c.contains("room") ? c.at("room") : json();
		if (!a.is_array() || a.size() != 2 || !a.at(0).is_number() || !a.at(1).is_number() || !std::isfinite(a.at(0).get<double>()) || !std::isfinite(a.at(1).get<double>())) {
			why = "a frame is put on the room at \"room\": two numbers (the bench's squares), or sent \"home\"";
			return false;
		}
		v.thermal.frame_room = true;
		v.thermal.frame_x = a.at(0).get<double>();
		v.thermal.frame_y = a.at(1).get<double>();
		return true;
	}
	if (what == "thermal") {
		// a thermal camera's settings (Stage 48): each given is set, each checked first; none changes the world
		int at = -1;
		if (!item(c, "item", Kind::Thermal, at, why)) {
			return false;
		}
		ThermalSettings t = items[static_cast<size_t>(at)].thermal;
		const auto number_in = [&](const char *key, double lo, double hi, double &to) {
			if (!c.contains(key)) {
				return true;
			}
			if (!c.at(key).is_number() || !(c.at(key).get<double>() >= lo && c.at(key).get<double>() <= hi)) {
				why = std::string("a camera's \"") + key + "\" is a number from " + text_of("%g", lo) + " to " + text_of("%g", hi);
				return false;
			}
			to = c.at(key).get<double>();
			return true;
		};
		const auto one_of = [&](const char *key, const std::vector<std::string> &names, std::string &to) {
			if (!c.contains(key)) {
				return true;
			}
			if (!c.at(key).is_string() || std::find(names.begin(), names.end(), c.at(key).get<std::string>()) == names.end()) {
				std::string list;
				for (const std::string &n : names) {
					list += (list.empty() ? "" : ", ") + n;
				}
				why = std::string("a camera's \"") + key + "\" is one of " + list;
				return false;
			}
			to = c.at(key).get<std::string>();
			return true;
		};
		std::string range = t.locked ? "lock" : "auto";
		double reflected = t.reflected_c.value_or(20.0);
		if (!one_of("palette", thermal_palette_names(), t.palette) || !number_in("emissivity", 0.1, 1.0, t.emissivity) || !number_in("level_c", -50.0, 2000.0, t.level_c) ||
				!number_in("span_c", 1.0, 2000.0, t.span_c) || !one_of("gain", { "auto", "high", "low" }, t.gain) || !one_of("range", { "auto", "lock" }, range)) {
			return false;
		}
		if (c.contains("reflected_c")) {
			if (c.at("reflected_c").is_string() && c.at("reflected_c").get<std::string>() == "room") {
				t.reflected_c.reset();
			} else if (number_in("reflected_c", -50.0, 500.0, reflected)) {
				t.reflected_c = reflected;
			} else {
				return false;
			}
		}
		if (c.contains("markers")) {
			if (!c.at("markers").is_boolean()) {
				why = "a camera's \"markers\" is true or false";
				return false;
			}
			t.markers = c.at("markers").get<bool>();
		}
		t.locked = range == "lock";
		items[static_cast<size_t>(at)].thermal = t;
		return true;
	}
	if (what == "attach") {
		int thing = -1, to = -1;
		const bool stopper = c.contains("item") && c.at("item").is_string() && find(c.at("item").get<std::string>()) >= 0
				&& items[static_cast<size_t>(find(c.at("item").get<std::string>()))].kind == Kind::Stopper;
		if (stopper) {
			thing = find(c.at("item").get<std::string>());
		}
		if ((!stopper && !item(c, "item", Kind::Vessel, thing, why)) || !item(c, "to", Kind::Vessel, to, why)) {
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
		if (const glass::Vessel *g = outlet_of(x); g != nullptr) {
			// (Stage 52) a vessel with an outlet at its foot stands on the mouth, its tip in it, and drains into the vessel
			// through its stopcock (a pipette has none: it delivers as it stands); the mouth stays open to the room
			if (x.place == Place::Outbox || x.place == Place::Trash || !on_bench(v, why)) {
				if (why.empty()) {
					why = "\"" + x.id + "\" is not on the bench";
				}
				return false;
			}
			if (v.holds >= 0) {
				why = "the mouth of \"" + v.id + "\" is taken";
				return false;
			}
			if (x.drains_to >= 0 || x.pours_into >= 0 || x.holds >= 0 || fixed_under(thing) >= 0) {
				why = "\"" + x.id + "\" is joined to something: take it off first";
				return false;
			}
			if (!boundary(why)) {
				return false;
			}
			if (world().connect_drain(x.cell, v.cell) != Refusal::None) {
				why = "\"" + x.id + "\" cannot stand there";
				return false;
			}
			const Item was = x;
			x.on = to;
			x.place = Place::On;
			x.col = x.row = -1;
			x.drains_to = to;
			v.holds = thing;
			leave_balances(thing, was);
			if (g->kind == "pipette") {
				x.cap = 1.0;
				record(x.id, "cap", 1.0, "player");
			}
			return true;
		}
		// adjacency (Stage 27): its fitting seals a neck, not a wide mouth
		if (const glass::Vessel *g = glass::find(v.glassware, v.nominal_ml); g != nullptr && 2.0 * g->mouth() > glass::seals()) {
			why = "\"" + x.id + "\" does not seal \"" + v.id + "\"'s " + text_of("%.0f mm mouth", 20.0 * g->mouth()) + text_of(": it fits a neck up to %.0f mm", 10.0 * glass::seals());
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
		if (x.kind == Kind::Stopper) { // a stopper seals the mouth (Stage 28): no way out for its gas
			for (const Item &u : items) {
				if (u.pours_into == to) {
					why = "\"" + u.id + "\" is pouring into \"" + v.id + "\"";
					return false;
				}
			}
			if (v.pours_into >= 0 && !end_pour(static_cast<size_t>(to), why)) {
				return false;
			}
			if (world().disconnect(v.mouth) != Refusal::None) {
				why = "\"" + v.id + "\" is not at the room's pressure yet: it cannot be sealed as it is";
				return false;
			}
			x.on = to;
			x.place = Place::On;
			x.col = x.row = -1;
			v.holds = thing;
			v.mouth = EdgeId{};
			return true;
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
		if (c.contains("item") && c.at("item").is_string() && find(c.at("item").get<std::string>()) >= 0
				&& items[static_cast<size_t>(find(c.at("item").get<std::string>()))].kind == Kind::Stopper) {
			thing = find(c.at("item").get<std::string>()); // a stopper pulled out
		} else if (!item(c, "item", Kind::Vessel, thing, why)) {
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
		if (outlet_of(x) != nullptr && x.drains_to >= 0) {
			// (Stage 52) a burette or a pipette lifted off: its drain off, its liquid stays in it (a pipette's tip held shut)
			if (!take_off_drain(static_cast<size_t>(thing), why)) {
				return false;
			}
			v.holds = -1;
			x.on = -1;
			x.place = Place::Cabinet;
			x.col = x.row = -1;
			set_beside(thing, v);
			return true;
		}
		EdgeId mouth;
		world().connect(v.cell, air, PHASES_GAS, &mouth);
		if (x.kind != Kind::Stopper && world().disconnect(x.joint) != Refusal::None) {
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
	if (what == "note") {
		// (Stage 55) a number the student writes in the notebook, under a label: no instrument's, no quantity's
		const std::string label = c.value("label", "");
		const double value = number(c, "value", std::numeric_limits<double>::quiet_NaN());
		if (label.empty() || label.size() > 24 || !std::isfinite(value)) {
			why = "a note is {\"label\" (at most 24 characters), \"value\" (a number), \"unit\"?}";
			return false;
		}
		notebook.push_back(NoteEntry{ static_cast<int64_t>(world().step()), label, Quantity{}, value, c.value("unit", ""), false });
		if (notebook.size() > NOTEBOOK_MOST) {
			notebook.erase(notebook.begin());
		}
		return true;
	}
	if (what == "record" || what == "log" || what == "unlog" || what == "erase") {
		// the notebook (GAME_SPEC §13, Stage 54): it acts on nothing in the world
		if (what == "erase") {
			if (c.contains("entry")) {
				const int64_t at = c.at("entry").is_number_integer() ? c.at("entry").get<int64_t>() : -1;
				if (at < 0 || at >= static_cast<int64_t>(notebook.size())) {
					why = "the notebook has no such entry";
					return false;
				}
				notebook.erase(notebook.begin() + at);
			} else {
				notebook.clear();
			}
			return true;
		}
		Quantity q;
		if (!quantity_of_command(c, q, why)) {
			return false;
		}
		const std::string named = c.value("label", "");
		if (what == "record") {
			return record_note(q, named, why);
		}
		const auto same = [&](const NoteLogger &l) { return l.quantity.item == q.item && l.quantity.what == q.what && l.quantity.measure == q.measure; };
		if (what == "unlog") {
			const auto at = std::find_if(loggers.begin(), loggers.end(), same);
			if (at == loggers.end()) {
				why = "nothing logs " + label_of(q);
				return false;
			}
			loggers.erase(at);
			return true;
		}
		const double every = number(c, "every", -1.0);
		if (!(every >= 1.0 && every <= 3600.0)) {
			why = "a logger records \"every\" 1 to 3600 seconds";
			return false;
		}
		bool instrument = false;
		if (!note_reading(q, instrument)) {
			why = instruments ? "no instrument reads " + label_of(q) : "there is no " + label_of(q) + " to read";
			return false;
		}
		loggers.erase(std::remove_if(loggers.begin(), loggers.end(), same), loggers.end());
		// (on the whole multiples of its interval: two loggers of one interval write at the same moments, a row a moment)
		const int64_t n = std::max<int64_t>(1, std::llround(every / STEP_SECONDS)), now = static_cast<int64_t>(world().step());
		loggers.push_back(NoteLogger{ named.empty() ? label_of(q) : named, q, every, (now + n - 1) / n * n });
		return true;
	}
	if (what == "fill") {
		// to the mark (Stage 52): a pipette draws, a volumetric flask is made up, from a vessel's liquid with what is
		// dissolved in it, until its own stands at its mark (its true volume there, within its class's tolerance)
		int thing = -1, from = -1;
		if (!item(c, "item", Kind::Vessel, thing, why) || !item(c, "from", Kind::Vessel, from, why)) {
			return false;
		}
		Item &x = items[static_cast<size_t>(thing)];
		const glass::Vessel *g = x.glassware.empty() ? nullptr : glass::find(x.glassware, x.nominal_ml);
		if (g == nullptr || !g->mark) {
			why = "\"" + x.id + "\" has no mark to fill to: a pipette or a volumetric flask has";
			return false;
		}
		if (thing == from) {
			why = "a vessel is not filled from itself";
			return false;
		}
		if (!on_bench(x, why) || !on_bench(items[static_cast<size_t>(from)], why)) {
			return false;
		}
		if (x.place != Place::Bench) {
			why = "\"" + x.id + "\" stands on something: take it off first";
			return false;
		}
		if (sealed(x) || sealed(items[static_cast<size_t>(from)])) {
			why = "\"" + items[static_cast<size_t>(sealed(x) ? thing : from)].id + "\" is sealed: open it first";
			return false;
		}
		const Fill held = fill(x);
		const double want = (mark_ml(x) - (held.defined ? held.condensed * 1e6 : 0.0)) * 1e-6; // m^3
		if (!(want > 0.0)) {
			why = "\"" + x.id + "\" stands at its mark already, or over it";
			return false;
		}
		const CellState &source = world().snapshot_cell(items[static_cast<size_t>(from)].cell);
		double solution = 0.0; // m^3: its liquid and what is dissolved in it
		for (size_t s = 0; s < source.amount.size(); ++s) {
			const SpeciesDef &def = table().species(SpeciesId{ static_cast<int32_t>(s) });
			if (source.amount[s] > 0 && !def.wall && (def.phase == Phase::Liquid || def.phase == Phase::Aqueous)) {
				solution += static_cast<double>(source.amount[s]) * MOL_PER_QUANTUM * def.molar_volume;
			}
		}
		if (want > solution * (1.0 - 1e-12)) {
			why = "\"" + items[static_cast<size_t>(from)].id + "\" does not hold enough liquid to fill \"" + x.id + "\" to its mark";
			return false;
		}
		const double fraction = want / solution;
		std::vector<SpeciesAmount> amounts;
		for (size_t s = 0; s < source.amount.size(); ++s) {
			const SpeciesId id{ static_cast<int32_t>(s) };
			const SpeciesDef &def = table().species(id);
			if (source.amount[s] > 0 && !def.wall && (def.phase == Phase::Liquid || def.phase == Phase::Aqueous)) {
				const auto moves = static_cast<int64_t>(std::llround(static_cast<double>(source.amount[s]) * fraction));
				if (moves > 0) {
					amounts.push_back(SpeciesAmount{ id, Amount{ std::min(moves, source.amount[s]) } });
				}
			}
		}
		touched.resize(items.size(), 0);
		touched[static_cast<size_t>(thing)] = touched[static_cast<size_t>(from)] = 1; // what a hand moves is not noticed as appearing
		return move(items[static_cast<size_t>(from)], x, amounts, why);
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
	if (what == "undrain") {
		// what an outlet runs into is taken away from it (Stage 34): the line comes off, the vessels stay
		int of = -1;
		if (!item(c, "of", Kind::Vessel, of, why)) {
			return false;
		}
		if (items[static_cast<size_t>(of)].drains_to < 0) {
			why = "\"" + items[static_cast<size_t>(of)].id + "\" has nothing under its outlet";
			return false;
		}
		return take_off_drain(static_cast<size_t>(of), why);
	}
	if (what == "goal" || what == "ungoal") {
		int slot = 0;
		if (!slot_of(c, slot, why)) {
			return false;
		}
		const int there = goal_in(slot);
		if (there >= 0 && goals[static_cast<size_t>(there)].game) {
			why = text_of("slot %.0f holds the game's goal \"", slot) + goals[static_cast<size_t>(there)].name + "\": it cannot be changed";
			return false;
		}
		if (what == "ungoal") {
			if (there < 0) {
				why = text_of("slot %.0f has no goal", slot);
				return false;
			}
			goals.erase(goals.begin() + there);
			return true;
		}
		Goal g;
		g.slot = slot;
		g.name = c.value("name", std::string());
		if (g.name.empty() || g.name.size() > MAX_SAYS) {
			why = text_of("a goal has a \"name\", at most %.0f characters", static_cast<double>(MAX_SAYS));
			return false;
		}
		if (!terms_from(c.value("terms", json()), g.terms, why)) {
			return false;
		}
		if (there >= 0) {
			g.id = goals[static_cast<size_t>(there)].id;
			g.met = goals[static_cast<size_t>(there)].met;
			goals[static_cast<size_t>(there)] = g;
		} else {
			do {
				g.id = "goal" + std::to_string(++goals_made);
			} while (find_goal(g.id) >= 0 || taken(g.id));
			goals.push_back(g);
		}
		detect_goals(false);
		return true;
	}
	if (what == "submit") {
		int thing = -1;
		if (!item(c, "item", Kind::Vessel, thing, why)) {
			return false;
		}
		int slot = 0;
		if (c.contains("slot")) {
			if (!slot_of(c, slot, why)) {
				return false;
			}
			if (held_in(slot) >= 0) {
				why = text_of("slot %.0f holds \"", slot) + items[static_cast<size_t>(held_in(slot))].id + "\"";
				return false;
			}
		} else { // the first free slot without a goal, else the first free one
			for (int k = 1; k <= OUTBOX_SLOTS && slot == 0; ++k) {
				slot = held_in(k) < 0 && goal_in(k) < 0 ? k : 0;
			}
			for (int k = 1; k <= OUTBOX_SLOTS && slot == 0; ++k) {
				slot = held_in(k) < 0 ? k : 0;
			}
			if (slot == 0) {
				why = "the outbox is full: every slot holds something";
				return false;
			}
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
		if (!take_off_drains(thing, why)) { // away from the outlet it stood under
			return false;
		}
		// lifted off its burner, which stays where the vessel stood (a balance too, Stage 50, which has no edge)
		for (Item &other : items) {
			if (other.under == thing && stands_under(other.kind)) {
				other.under = -1;
			} else if (other.under == thing) {
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
		x.slot = slot;
		x.col = x.row = -1;
		for (size_t i = 0; i < items.size(); ++i) {
			if ((items[i].kind == Kind::Burner || stands_under(items[i].kind)) && items[i].place == Place::Under && items[i].under < 0) {
				items[i].place = Place::Cabinet;
				set_beside(static_cast<int>(i), was); // where the vessel stood, or beside it
			}
		}
		detect_goals(false);
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
		const bool ran = impl_->run(c, why);
		impl_->release_probes();
		return ran;
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
	// the trash (Stage 34): its parts after the lab's, with what circuits among them hold; its history, oldest first
	for (const Part &p : m.binned) {
		ItemView v;
		v.id = p.id;
		v.kind = part_kind_name(p.kind);
		v.place = "trash";
		v.col = p.col;
		v.row = p.row;
		v.part = p;
		out.items.push_back(v);
	}
	for (const Binned &b : m.trash) {
		out.trash.push_back(b.part ? static_cast<int>(m.items.size() + m.parts.size()) + m.find_binned(b.id) : m.find(b.id));
	}
	out.wires = m.wires;
	out.rigs = m.rigs;
	out.controls = m.controls;
	out.notices = m.notices;
	out.goals = m.goals;
	for (Goal &g : out.goals) {
		g.met = m.goal_met(g); // live
	}
	const Totals now = totals(table, m.world().snapshot());
	out.conserved = now == m.genesis;
	out.room_celsius = m.world().meta(m.room).reservoir_temperature - 273.15; // GAME_SPEC §12.4 (Stage 46)
	out.instruments = m.instruments; // (Stage 50)
	out.notebook = m.notebook; // (Stage 54)
	out.loggers = m.loggers;
	out.seed = m.seed;
	out.air_kg_m3 = m.air_density();
	out.mass_g = now.mass.grams();
	out.energy_j = static_cast<double>(now.energy) * JOULE_PER_QUANTUM;
	for (const Event &e : m.events) {
		char when[48];
		std::snprintf(when, sizeof when, " at %.1f s", static_cast<double>(e.step) * STEP_SECONDS);
		out.events.push_back("the " + e.item + (e.what == "burst" ? " burst" : " " + e.what) + when);
	}
	return out;
}

namespace {

// what limits a reaction (GAME_SPEC §13.6), from its kinetics and the cell now
std::string limit_of(const SpeciesTable &table, const World &world, CellId cell, const Reaction &reaction, double a, double kelvin, double rate) {
	const std::span<const Term> terms = reaction.terms();
	const CellState &state = world.snapshot_cell(cell);
	const double rt = GAS_CONSTANT * kelvin;
	if (std::fabs(a) < 1e-3 * rt) {
		return "at equilibrium";
	}
	const bool forward = a > 0.0;
	const Kinetics &k = reaction.kinetics();
	const bool grows = k.law == Kinetics::Law::Surface && reaction.nu(k.in) > 0; // makes its solid: crystals
	// what it would consume either way, used up: forward, or both ways at once, it is short of a reactant
	SpeciesId short_forward, short_backward;
	for (const Term &t : terms) {
		SpeciesId &first = t.nu < 0 ? short_forward : short_backward;
		if (state.amount[static_cast<size_t>(t.species.index)] == 0 && !table.has(first)) {
			first = t.species;
		}
	}
	if (forward && table.has(short_forward)) {
		return "no " + named(table, short_forward) + " left";
	}
	if (!forward && table.has(short_backward)) {
		return table.has(short_forward) ? "no " + named(table, short_forward) + " left" : "would run backwards, but there is no " + named(table, short_backward);
	}
	if (k.law == Kinetics::Law::None) {
		return "it does not run on its own: no rate law";
	}
	if (k.law == Kinetics::Law::Equilibrium) {
		return "fast: held at its equilibrium every step";
	}
	const double s_ratio = a / rt < 700.0 ? std::exp(a / rt) : HUGE_VAL; // the supersaturation, for crystals
	if (grows) {
		const auto in = static_cast<size_t>(k.in.index);
		if (!forward) {
			return text_of("undersaturated (S = %.4f): the crystals dissolve", s_ratio);
		}
		if (state.amount[in] == 0 || state.particles[in] == 0) {
			double liquid = 0.0;
			for (size_t s = 0; s < state.amount.size(); ++s) {
				liquid += table.species(SpeciesId{ static_cast<int32_t>(s) }).phase == Phase::Liquid ? static_cast<double>(state.amount[s]) * MOL_PER_QUANTUM * 0.018015 : 0.0;
			}
			const double born = k.nucleation * liquid * std::pow(s_ratio - 1.0, k.nucleation_order);
			return text_of("supersaturated (S = %.4f): no crystals yet, %.3g starting a second", s_ratio, born);
		}
		return text_of("supersaturated (S = %.4f): growing on ", s_ratio) + text_of("%.0f crystals", static_cast<double>(state.particles[in]));
	}
	if (!forward) {
		return text_of("running backwards: %.3g mol/s", -rate);
	}
	const double f = 1.0 - std::exp(-a / rt);
	const double rate_constant = k.prefactor * std::exp(-k.activation / rt);
	if (k.law == Kinetics::Law::FirstOrder) {
		const double tau = 1.0 / (rate_constant * f);
		if (tau > 60.0 && k.activation > 0.0) {
			const double ln = std::log(k.prefactor * 60.0);
			return ln > 0.0 ? text_of("too cold: %.3g mol/s at %.0f C", rate, kelvin - 273.15) + text_of(", fast from about %.0f C", k.activation / (GAS_CONSTANT * ln) - 273.15)
							: text_of("too slow: %.3g mol/s, at any temperature", rate);
		}
		return text_of("fast: half of it goes in %.3g s", 0.6931471805599453 * tau);
	}
	// the surface law, using up its solid
	const SpeciesDef &solid = table.species(k.in);
	const auto in = static_cast<size_t>(k.in.index);
	const double mol = static_cast<double>(state.amount[in]) * MOL_PER_QUANTUM;
	const double area = 4.835975862049409 * std::cbrt(static_cast<double>(state.particles[in])) * std::pow(mol * solid.molar_volume, 2.0 / 3.0); // m^2
	// which of its two steps holds it back (GAME_SPEC §7): the reaction at the surface, or transport to it
	// (Stage 57: transport by the stirring's own dissipation, Sano's)
	const double dissipation = chem::dissipation_per_kg(table, state, world.meta(cell).stirring);
	const SurfaceSteps steps = surface_steps(table, state, reaction, kelvin, a, dissipation, &world.meta(cell));
	const bool by_transport = steps.transported && steps.transport < steps.surface;
	const bool alike = steps.transported && steps.transport < 3.0 * steps.surface && steps.surface < 3.0 * steps.transport;
	std::string says = alike ? "limited by its surface and by transport to it alike: " : by_transport ? "limited by transport to its surface: " : "limited at its surface: ";
	says += text_of("%.3g cm2 of ", area * 1e4) + named(table, k.in);
	if (table.has(k.driver)) {
		says += text_of(", %.3g mol/kg of ", molality(table, state, k.driver)) + named(table, k.driver);
	}
	if (!(dissipation > 0.0) && !(world.meta(cell).mixing > 1.0)) { // (a stirrer just switched on has dissipated nothing yet)
		const double faster = steps.both > 0.0 ? surface_steps(table, state, reaction, kelvin, a, STIRRED_W_KG, &world.meta(cell)).both / steps.both : 1.0;
		says += faster < 1.05 ? std::string("; stirring would hardly speed it") : text_of("; stirring would make it %.1f times faster", faster);
	} else {
		says += "; stirred";
	}
	return says;
}

std::string equation_of(const SpeciesTable &table, const Reaction &reaction) {
	std::string left, right;
	for (const Term &t : reaction.terms()) {
		std::string &side = t.nu < 0 ? left : right;
		const int n = t.nu < 0 ? -t.nu : t.nu;
		side += (side.empty() ? "" : " + ") + (n == 1 ? std::string() : std::to_string(n) + " ") + named(table, t.species);
	}
	return left + " -> " + right;
}

} // namespace

ItemDebug Lab::debug(const std::string &id) const {
	const Impl &m = *impl_;
	const SpeciesTable &table = m.table();
	const World &world = m.world();
	ItemDebug out;
	out.item = id;
	const int i = m.find(id);
	if (i < 0 && m.find_part(id) >= 0) { // a signal part (Stage 28): what it is doing
		out.found = true;
		out.part = true;
		out.facts = m.part_facts(m.parts[static_cast<size_t>(m.find_part(id))]);
		return out;
	}
	if (i < 0 && m.find_binned(id) >= 0) { // in the trash (Stage 34): what it is set to
		out.found = true;
		out.part = true;
		out.facts = m.part_facts(m.binned[static_cast<size_t>(m.find_binned(id))]);
		out.facts.insert(out.facts.begin(), "in the trash: it signals nothing");
		return out;
	}
	if (i < 0 || !holds_matter(m.items[static_cast<size_t>(i)])) {
		return out;
	}
	out.found = true;
	const Item &x = m.items[static_cast<size_t>(i)];
	const CellId cell = x.cell;
	const CellState &state = world.snapshot_cell(cell);
	const CellMeta &meta = world.meta(cell);
	const Thermal thermal = thermal_state(table, state, &meta);
	// its pressure, and its pour (Stage 28)
	if (x.kind == Kind::Vessel) {
		out.facts.push_back(m.sealed(x) ? text_of("sealed: %.1f kPa", thermal.pressure / 1000.0) + text_of(", giving way past %.0f kPa", (m.gives_way(x) + P_ATMOSPHERE) / 1000.0)
										: text_of("open to the room: %.1f kPa", P_ATMOSPHERE / 1000.0));
		// its liquid's pH, and what sets it (Stage 43)
		if (double p = 0.0; thermal.defined && chem::ph(table, state, thermal.temperature, p)) {
			out.facts.push_back(text_of("pH %.2f", p) + text_of(" (Kw %.3g at its temperature)", chem::water_ionisation(table, thermal.temperature)));
		}
		// its column (Stage 47): each layer, from the top
		if (m.layered(x)) {
			ItemView layered;
			m.layers_into(x, layered);
			std::string fact = text_of("layered, %d layers from the top:", static_cast<int>(layered.layers.size()));
			for (const LayerView &l : layered.layers) {
				fact += text_of(" %.2f mL", l.liquid_ml) + text_of(" at %.1f C;", l.celsius);
			}
			out.facts.push_back(fact);
		}
		// its floor and its bubbles (Stage 45)
		if (thermal.defined) {
			ItemView b;
			m.bubbles_into(x, thermal.temperature, b);
			if (b.depth_cm > 0.0 && (b.floor_c > thermal.temperature - 273.15 + 0.05 || b.boiling || b.gas_extinction > 0.0)) {
				std::string fact = text_of("its floor %.1f C (it boils at %.2f C there, ", b.floor_c, b.floor_boils_c) + text_of("under %.1f cm of liquid)", b.depth_cm);
				if (b.boiling) {
					fact += text_of(": bubbles of %.1f mm leave it", b.bubble_mm) + (b.bubbles_surface ? std::string(" and reach the surface") : text_of(" and are gone %.1f mm up, condensing in the cooler liquid", b.bubbles_end_cm * 10.0));
				}
				if (b.gas_extinction > 0.0) {
					fact += "; a gas rises through it in bubbles";
				}
				out.facts.push_back(fact);
			}
		}
		if (x.pours_into >= 0 && x.pour.index >= 0) {
			const double rate = world.edges()[static_cast<size_t>(x.pour.index)].rate * 1e6;
			const std::string into = m.items[static_cast<size_t>(x.pours_into)].id;
			if (x.inverted) {
				const glass::Vessel *g = glass::find(x.glassware, x.nominal_ml);
				const Fill fill = fill_of(table, state, thermal.temperature, &meta);
				const double head = g != nullptr && fill.defined ? g->head_inverted(fill.condensed * 1e6) : 0.0;
				out.facts.push_back("pours into " + into + text_of(" upside down: its cap %.0f%% open", 100.0 * x.cap) + text_of(" under %.1f cm of liquid", head) + text_of(": %.2f mL/s", rate));
			} else {
				const glass::Vessel *g = glass::find(x.glassware, x.nominal_ml);
				const Lab::Impl::ByHand p = g != nullptr ? m.by_hand(x, *g) : Lab::Impl::ByHand{};
				out.facts.push_back("pours into " + into + text_of(" held, turned %.0f degrees", p.turn * 180.0 / PI) +
						(p.head_cm > 0.0 ? text_of(", its liquid %.1f mm over the lip", 10.0 * p.head_cm) : std::string(", its liquid not at the lip")) + text_of(": %.2f mL/s", rate));
			}
		}
	}
	out.has_temperature = thermal.defined;
	out.celsius = thermal.temperature - 273.15;
	out.heat_capacity = thermal.heat_capacity;
	// what it holds
	double gas = 0.0, solution = 0.0, liquid = 0.0;
	for (size_t s = 0; s < state.amount.size(); ++s) {
		const SpeciesDef &def = table.species(SpeciesId{ static_cast<int32_t>(s) });
		const double n = static_cast<double>(state.amount[s]);
		gas += def.phase == Phase::Gas ? n : 0.0;
		solution += def.phase == Phase::Liquid || def.phase == Phase::Aqueous ? n : 0.0;
		liquid += def.phase == Phase::Liquid ? n : 0.0;
	}
	for (size_t s = 0; s < state.amount.size(); ++s) {
		const SpeciesId sid{ static_cast<int32_t>(s) };
		const SpeciesDef &def = table.species(sid);
		if (state.amount[s] == 0 || def.wall) {
			continue;
		}
		static const char PHASE[] = { 's', 'l', 'g', 'a' };
		SpeciesDebug d;
		d.species = std::string(def.id);
		d.formula = named(table, sid);
		d.phase = PHASE[static_cast<int>(def.phase)];
		const double n = static_cast<double>(state.amount[s]);
		d.mol = n * MOL_PER_QUANTUM;
		d.g = d.mol * static_cast<double>(table.molar_mass_ug(sid)) * 1e-6;
		d.fraction = def.phase == Phase::Gas ? n / gas : def.phase == Phase::Solid ? 1.0 : n / solution;
		d.molality = def.phase == Phase::Aqueous && liquid > 0.0 ? n / (liquid * MOL_PER_QUANTUM * 0.018015) * MOL_PER_QUANTUM : 0.0;
		d.activity = activity(table, state, sid, &meta);
		out.species.push_back(d);
	}
	// its flows in the last step, into it
	const StepFlows &f = m.sim->flows();
	const std::span<const Edge> edges = world.edges();
	static const char *const PROCESS[] = { "reaction", "heat", "drain", "settle", "overflow", "work", "column", "exchange" };
	for (size_t e = 0; e < edges.size() && (e + 1) * PROCESSES <= f.energy.size(); ++e) {
		if (edges[e].a != cell && edges[e].b != cell) {
			continue;
		}
		const double sign = edges[e].b == cell ? 1.0 : -1.0;
		const CellId other = edges[e].a == cell ? edges[e].b : edges[e].a;
		for (size_t p = 0; p < PROCESSES; ++p) {
			const int64_t q = f.energy[e * PROCESSES + p];
			const double g = f.grams[e * PROCESSES + p];
			if (q == 0 && g == 0.0) {
				continue;
			}
			bool poured = false; // a drain that is a pour (Stage 28)
			for (const Item &u : m.items) {
				poured = poured || (u.pours_into >= 0 && u.pour.index == static_cast<int32_t>(e));
			}
			out.flows.push_back(FlowDebug{ world.cell_name(other), poured && p == static_cast<size_t>(Process::Drain) ? "pour" : PROCESS[p],
					sign * static_cast<double>(q) * JOULE_PER_QUANTUM / STEP_SECONDS, sign * g / STEP_SECONDS });
		}
	}
	// each reaction that names a species it holds
	const std::span<const ReactionNote> notes = builtin_reaction_notes();
	const std::span<const Reaction> all = builtin_reactions();
	for (size_t r = 0; r < all.size(); ++r) {
		const Reaction &reaction = all[r];
		bool names = false;
		for (const Term &t : reaction.terms()) {
			names = names || state.amount[static_cast<size_t>(t.species.index)] > 0;
		}
		if (!names) {
			continue;
		}
		ReactionDebug d;
		d.id = std::string(reaction.id());
		d.equation = equation_of(table, reaction);
		d.explain = r < notes.size() ? std::string(notes[r].explain) : std::string();
		// the last step's extent: the simulation's rated reactions, then its equilibria
		size_t at = SIZE_MAX;
		for (size_t k = 0; k < m.sim->rated().size(); ++k) {
			at = m.sim->rated()[k].id() == reaction.id() ? k : at;
		}
		for (size_t k = 0; k < m.sim->equilibria().size(); ++k) {
			at = m.sim->equilibria()[k].id() == reaction.id() ? m.sim->rated().size() + k : at;
		}
		const size_t slot = static_cast<size_t>(cell.index) * f.reactions + at;
		if (at != SIZE_MAX && slot < f.extent.size()) {
			d.rate_mol_s = static_cast<double>(f.extent[slot]) * MOL_PER_QUANTUM / STEP_SECONDS;
		}
		if (!thermal.defined) {
			d.limit = "no temperature to weigh it at";
		} else {
			bool defined = false;
			const double a = affinity(table, state, reaction.terms(), thermal.temperature, defined, &meta);
			d.weighed = defined;
			d.affinity_kj = a / 1000.0;
			if (defined) {
				d.limit = limit_of(table, world, cell, reaction, a, thermal.temperature, d.rate_mol_s);
			} else { // not weighed: still short of a reactant, or with no rate law; else why it cannot be
				bool solute = false;
				for (const Term &t : reaction.terms()) {
					if (d.limit.empty() && t.nu < 0 && state.amount[static_cast<size_t>(t.species.index)] == 0) {
						d.limit = "no " + named(table, t.species) + " left";
					}
					solute = solute || table.species(t.species).phase == Phase::Aqueous;
				}
				if (d.limit.empty()) {
					d.limit = reaction.kinetics().law == Kinetics::Law::None ? "it does not run on its own: no rate law"
							: solute && liquid == 0.0								 ? "it cannot be weighed here: a solute with no liquid to hold it"
																					 : "it cannot be weighed here: a species it names has no thermodynamic data";
				}
			}
		}
		out.reactions.push_back(d);
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
		if (item.kind == Kind::Balance) {
			// a balance (Stage 50): where it stands, what it is under, its load's conventional mass, its tare, its display
			const ItemView v = m.item_view(item, false, false);
			j["kind"] = "balance";
			j["place"] = PLACE_NAME[static_cast<int>(item.place)];
			if (item.place == Place::Bench) {
				j["at"] = { item.col, item.row };
			}
			j["model"] = item.model;
			j["under"] = item.under >= 0 ? json(m.items[static_cast<size_t>(item.under)].id) : json(nullptr);
			j["gross_g"] = v.gross_g;
			j["tare_g"] = item.tare;
			j["display_g"] = v.display_g ? json(*v.display_g) : json(nullptr);
			items.push_back(j);
			continue;
		}
		if (item.kind == Kind::Spectro) {
			// a spectrophotometer (Stage 55): where it stands, what is in its well, its wavelength, its blank's, what it reads
			const ItemView v = m.item_view(item, false, false);
			j["kind"] = "spectrophotometer";
			j["place"] = PLACE_NAME[static_cast<int>(item.place)];
			if (item.place == Place::Bench) {
				j["at"] = { item.col, item.row };
			}
			j["under"] = item.under >= 0 ? json(m.items[static_cast<size_t>(item.under)].id) : json(nullptr);
			j["nm"] = item.nm;
			j["blank_nm"] = v.blanked ? json(item.blank_nm) : json(nullptr);
			j["absorbance"] = v.absorbance ? json(*v.absorbance) : json(nullptr);
			items.push_back(j);
			continue;
		}
		if (item.kind == Kind::Thermal) {
			// a thermal camera (Stage 48): where it stands, where its frame is, its settings
			const ThermalSettings &t = item.thermal;
			j["kind"] = "thermal";
			j["place"] = PLACE_NAME[static_cast<int>(item.place)];
			if (item.place == Place::Bench) {
				j["at"] = { item.col, item.row };
			}
			j["frame"] = t.frame_room ? json{ { "room", { t.frame_x, t.frame_y } } } : json("home");
			j["settings"] = { { "palette", t.palette }, { "emissivity", t.emissivity }, { "reflected_c", t.reflected_c ? json(*t.reflected_c) : json("room") }, { "markers", t.markers },
				{ "range", t.locked ? "lock" : "auto" }, { "level_c", t.level_c }, { "span_c", t.span_c }, { "gain", t.gain } };
			items.push_back(j);
			continue;
		}
		if (item.kind == Kind::Viewer) {
			// the spectral viewer (Stage 42): where it stands, and where its probe is fixed
			j["kind"] = "viewer";
			j["place"] = PLACE_NAME[static_cast<int>(item.place)];
			if (item.place == Place::Bench) {
				j["at"] = { item.col, item.row };
			}
			j["probe"] = !item.probe_on.empty() ? json{ { "on", item.probe_on }, { "at", { item.probe_u, item.probe_v } } }
						 : item.probe_room		? json{ { "at", { item.probe_u, item.probe_v } } }
												: json("holder");
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
		if (item.slot > 0) {
			j["slot"] = item.slot;
		}
		j["stirred"] = item.stirred;
		j["on"] = item.on >= 0 ? json(m.items[static_cast<size_t>(item.on)].id) : json(nullptr);
		const bool layered = item.kind == Kind::Vessel && m.layered(item); // (Stage 47: as a whole)
		const CellState column = layered ? m.whole(item) : CellState{};
		const double kelvin = layered ? m.kelvin(item) : m.kelvin(item.cell);
		j["celsius"] = kelvin - 273.15;
		const CellState &cell = layered ? column : m.world().snapshot_cell(item.cell);
		const Fill fill = layered ? m.fill(item) : fill_of(table, cell, kelvin);
		if (layered) {
			int used = 1; // its own cell, and each layer that holds something
			for (const CellId c : m.layers_of(item)) {
				used += m.layered_cell(c) ? 1 : 0;
			}
			j["layers"] = used;
		}
		if (double p = 0.0; item.kind == Kind::Vessel && chem::ph(table, cell, kelvin, p)) {
			j["ph"] = p; // Stage 43
		}
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
			row["g"] = m.grams(item, id);
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
	if (!m.trash.empty()) { // Stage 34
		out["trash"] = m.trash_json(false);
	}
	out["notices"] = m.notices_json(false);
	out["goals"] = m.goals_json(false);
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
	if (m.seed != 0) { // what a hand misses by is drawn from it (Stage 50): a bench without one digests as before
		hash.update_u64(m.seed);
	}
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
		if (e.rate >= 0.0) { // a pour's (Stage 27): a lab without one digests as before
			hash.update_double(e.rate);
		}
		if (e.solids_rate > 0.0 || (e.carried > 0.0 && e.carried < 1.0)) { // a pour by hand's (Stage 31)
			hash.update_double(e.carried);
			hash.update_double(e.solids_rate);
		}
		if (e.power != 0.0) { // a stirrer's work (Stage 32)
			hash.update_double(e.power);
		}
		if (e.exchange > 0.0) { // gas exchanged at a mouth (Stage 50)
			hash.update_double(e.exchange);
		}
	}
	for (const Item &x : m.items) { // a liquid's turning, and what it holds up (Stage 32)
		if (x.swirl > 0.0 || x.suspended > 0.0) {
			hash.update_double(x.swirl);
			hash.update_double(x.suspended);
		}
		if (std::isfinite(x.holder_c)) { // a cell holder's temperature (Stage 56): what it will heat to
			hash.update_double(x.holder_c);
		}
	}
	if (!m.sim->work_remainders().empty()) {
		hash.update_u64(m.sim->work_remainders().size());
		for (const double v : m.sim->work_remainders()) {
			hash.update_double(v);
		}
	}
	// the processes' carried remainders are state too (GAME_SPEC §4.5)
	for (const std::vector<double> *carried : { &m.sim->heat_remainders(), &m.sim->reaction_remainders(), &m.sim->nucleation_remainders() }) {
		hash.update_u64(carried->size());
		for (const double v : *carried) {
			hash.update_double(v);
		}
	}
	// the signals, once there is a part (§13.3): what they will do is state too
	const auto text = [&hash](const std::string &t) {
		hash.update_u64(t.size());
		hash.update(t.data(), t.size());
	};
	const auto maybe = [&hash](const std::optional<double> &v) {
		hash.update_u64(v ? 1 : 0);
		hash.update_double(v ? *v : 0.0);
	};
	const auto links = [&](const std::vector<Part> &parts, const std::vector<Wire> &wires, const std::vector<Rig> &rigs) {
		for (const Part &p : parts) {
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
			if (p.kind == PartKind::Notice || p.kind == PartKind::Writer) {
				hash.update_i64(p.notice_level); // what it hears, or writes, decides what is signalled
			}
			if (p.kind == PartKind::Notice && (!p.contains.empty() || p.hold != 1 || p.held != 0)) { // Stage 28: a part as before digests as before
				text(p.contains);
				hash.update_i64(p.hold);
				hash.update_i64(p.held);
			}
			if (p.kind == PartKind::Writer && p.writes != 0) {
				hash.update_i64(p.writes);
			}
			if (p.kind == PartKind::Controller) { // a bench without one digests as before (Stage 26)
				for (const double v : { p.target, p.gain, p.tau, p.theta, p.kc, p.ti, p.integral, p.level, p.duty }) {
					hash.update_double(v);
				}
				for (const int64_t v : { static_cast<int64_t>(p.speed), p.period, p.phase, p.on_steps }) {
					hash.update_i64(v);
				}
				hash.update_u64((p.tuned ? 1u : 0u) | (p.pulse ? 2u : 0u));
			}
		}
		for (const Wire &w : wires) {
			text(w.from);
			text(w.to);
		}
		for (const Rig &r : rigs) {
			text(r.from);
			text(r.item);
			text(r.control);
			hash.update_u64((r.change ? 1u : 0u) | (r.seen ? 2u : 0u) | (r.follow ? 4u : 0u));
			maybe(r.on);
			maybe(r.off);
			maybe(r.any);
		}
	};
	if (!m.parts.empty()) {
		links(m.parts, m.wires, m.rigs);
	}
	// a controller's trial as it runs (Stage 39): what it has read and its copies' own digests; a lab with
	// none digests as before
	for (const Lab::Impl::Trial &t : m.trials) {
		text("trial");
		text(t.part);
		for (const std::vector<double> *v : { &t.t, &t.r }) {
			hash.update_u64(v->size());
			for (const double x : *v) {
				hash.update_double(x);
			}
		}
		for (const double x : { t.distance, t.basis_g, t.basis_cp }) {
			hash.update_double(x);
		}
		hash.update_u64(t.basis_has);
		hash.update_u64(t.again ? 1 : 0);
		text(t.copies[0]->digest());
		text(t.copies[1]->digest());
	}
	// the trash, once it holds something (Stage 34): its order, where each element goes back to, and its
	// parts as they will be restored; a lab with an empty trash digests as before
	if (!m.trash.empty()) {
		text("trash");
		hash.update_u64(m.trash.size());
		for (const Binned &b : m.trash) {
			text(b.id);
			hash.update_u64(b.part ? 1 : 0);
			hash.update_i64(b.col);
			hash.update_i64(b.row);
		}
		hash.update_u64(m.binned.size());
		for (const Part &p : m.binned) { // a part's remembered place: its square, and the circuit it lies in
			text(p.id);
			text(p.in);
			hash.update_i64(p.col);
			hash.update_i64(p.row);
		}
		links(m.binned, m.binned_wires, m.binned_rigs);
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
		if (meta.layer) { // (Stage 47)
			cell["layer"] = true;
		}
		if (meta.column_volume > 0.0) {
			cell["column"] = { bits(meta.column_volume), bits(meta.column_area) };
			if (!meta.column_shape.empty()) { // its shape (Stage 51): its rise, then its volumes
				json shape = json::array({ bits(meta.column_rise) });
				for (const double v : meta.column_shape) {
					shape.push_back(bits(v));
				}
				cell["column_shape"] = std::move(shape);
			}
		}
		cell["amount"] = state.amount;
		cell["energy"] = state.energy;
		cell["particles"] = state.particles;
		cells.push_back(cell);
	}
	out["cells"] = cells;
	json edges = json::array();
	for (const Edge &e : w.edges()) {
		edges.push_back({ { "a", e.a.index }, { "b", e.b.index }, { "phases", e.phases }, { "open", e.open }, { "drain", e.drain }, { "conductance", bits(e.conductance) } });
		if (e.column) { // (Stage 47)
			edges.back()["column"] = true;
		}
		if (e.rate >= 0.0) {
			edges.back()["rate"] = bits(e.rate);
		}
		if (e.carried > 0.0 || e.solids_rate > 0.0) {
			edges.back()["carried"] = bits(e.carried);
			edges.back()["solids_rate"] = bits(e.solids_rate);
		}
		if (e.power != 0.0) {
			edges.back()["power"] = bits(e.power);
		}
		if (e.exchange > 0.0) { // (Stage 50)
			edges.back()["exchange"] = bits(e.exchange);
		}
	}
	out["edges"] = edges;
	out["remainders"] = { { "heat", doubles(sim->heat_remainders()) }, { "reaction", doubles(sim->reaction_remainders()) }, { "nucleation", doubles(sim->nucleation_remainders()) } };
	if (!sim->work_remainders().empty()) {
		out["remainders"]["work"] = doubles(sim->work_remainders());
	}
	json bench;
	bench["grid"] = { cols, rows };
	bench["mid_step"] = mid_step;
	bench["air"] = air.index;
	bench["room"] = room.index;
	bench["mains"] = mains.index;
	if (!notebook.empty() || !loggers.empty()) { // (Stage 54) a lab with nothing written down saves as before
		json pages = json::array(), logging = json::array();
		for (const NoteEntry &e : notebook) {
			pages.push_back({ { "step", e.step }, { "label", e.label }, { "item", e.quantity.item }, { "what", e.quantity.what }, { "measure", e.quantity.measure }, { "value", bits(e.value) },
				{ "unit", e.unit }, { "instrument", e.instrument } });
		}
		for (const NoteLogger &l : loggers) {
			logging.push_back({ { "label", l.label }, { "item", l.quantity.item }, { "what", l.quantity.what }, { "measure", l.quantity.measure }, { "every", bits(l.every_s) }, { "next", l.next } });
		}
		bench["notebook"] = { { "pages", pages }, { "loggers", logging } };
	}
	if (instruments || seed != 0) { // a bench of the truth and no seed saves as before (Stage 50)
		bench["readings"] = instruments ? "instruments" : "truth";
		bench["seed"] = seed;
	}
	json list = json::array();
	for (const Item &item : items) {
		list.push_back({ { "id", item.id }, { "kind", static_cast<int>(item.kind) }, { "cell", item.cell.index }, { "mouth", item.mouth.index }, { "full", bits(item.full) },
				{ "heats", item.heats.index }, { "under", item.under }, { "valve", bits(item.valve) }, { "on", item.on }, { "joint", item.joint.index }, { "holds", item.holds },
				{ "drains_to", item.drains_to }, { "stirred", item.stirred }, { "submitted", item.submitted }, { "place", static_cast<int>(item.place) }, { "col", item.col },
				{ "row", item.row }, { "w", item.w }, { "h", item.h }, { "capacity", bits(item.capacity) }, { "burst", item.burst }, { "slot", item.slot },
				{ "wall_mm", bits(item.wall_mm) }, { "floor_mm", bits(item.floor_mm) }, { "glassware", item.glassware }, { "nominal_ml", bits(item.nominal_ml) } });
		if (item.kind == Kind::Stopper) { // a lab without a stopper or a cap on saves as before (Stage 28)
			list.back()["hold_n"] = bits(item.hold_n);
		}
		if (item.screwed) {
			list.back()["screwed"] = true;
		}
		if (item.landed != 0) { // (Stage 47)
			list.back()["landed"] = item.landed;
		}
		if (item.stirs.index >= 0) { // (Stage 32)
			list.back()["stirs"] = item.stirs.index;
		}

		if (item.swirl > 0.0 || item.suspended > 0.0) {
			list.back()["swirl"] = bits(item.swirl);
			list.back()["suspended"] = bits(item.suspended);
			list.back()["swirl_holds"] = { bits(item.swirl_for), bits(item.swirl_most), bits(item.swirl_lifts), bits(item.settles) };
		}
		if (item.kind == Kind::Balance) { // (Stage 50)
			list.back()["balance"] = { { "model", item.model }, { "tare", bits(item.tare) } };
		}
		if (item.kind == Kind::Spectro) { // (Stage 55)
			list.back()["spectro"] = { bits(item.nm), bits(item.blank), bits(item.blank_nm) };
			if (std::isfinite(item.holder_c)) { // (Stage 56)
				list.back()["holder_c"] = bits(item.holder_c);
			}
		}
		if (!item.label.empty()) {
			list.back()["label"] = item.label;
		}
		if (outlet_of(item) != nullptr && item.pours_into < 0 && item.cap != 0.0) { // a burette's stopcock (Stage 52)
			list.back()["stopcock"] = bits(item.cap);
		}
		if (!std::isnan(item.read_kelvin)) { // its thermometer by instruments (Stage 52)
			list.back()["thermometer"] = bits(item.read_kelvin);
		}
		if (item.kind == Kind::Thermal) { // a camera's frame and settings (Stage 48)
			const ThermalSettings &t = item.thermal;
			list.back()["thermal"] = { { "room", t.frame_room }, { "at", { bits(t.frame_x), bits(t.frame_y) } }, { "palette", t.palette }, { "emissivity", bits(t.emissivity) },
				{ "reflected", t.reflected_c ? json(bits(*t.reflected_c)) : json(nullptr) }, { "markers", t.markers }, { "locked", t.locked }, { "level", bits(t.level_c) },
				{ "span", bits(t.span_c) }, { "gain", t.gain } };
		}
		if (item.kind == Kind::Viewer && (!item.probe_on.empty() || item.probe_room)) { // a lab without a viewer saves as before (Stage 42)
			list.back()["probe"] = { { "on", item.probe_on }, { "room", item.probe_room }, { "at", { bits(item.probe_u), bits(item.probe_v) } } };
		}
		if (item.pours_into >= 0) { // a lab without a pour saves as before (Stage 27)
			list.back()["pours"] = { { "into", item.pours_into }, { "edge", item.pour.index }, { "inverted", item.inverted }, { "tilt", bits(item.tilt) }, { "cap", bits(item.cap) } };
		}
	}
	bench["items"] = list;
	json happened = json::array();
	for (const Event &e : events) {
		happened.push_back({ { "step", e.step }, { "what", e.what }, { "item", e.item } });
	}
	bench["events"] = happened;
	bench["signals"] = signals_json(true);
	if (!trash.empty()) { // a lab with an empty trash saves as before (Stage 34)
		bench["trash"] = trash_json(true);
	}
	if (!trials.empty()) { // a controller's trial as it runs (Stage 39): its two copies whole
		json list_of = json::array();
		for (const Trial &t : trials) {
			list_of.push_back({ { "part", t.part }, { "copies", { json::parse(t.copies[0]->save()), json::parse(t.copies[1]->save()) } }, { "t", doubles(t.t) }, { "r", doubles(t.r) },
					{ "distance", bits(t.distance) }, { "basis", { bits(t.basis_g), bits(t.basis_cp), t.basis_has } }, { "again", t.again } });
		}
		bench["trials"] = list_of;
	}
	bench["notices"] = notices_json(true);
	bench["goals"] = { { "list", goals_json(true) }, { "made", goals_made } };
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
	std::vector<double> heat, reaction, nucleation, work;
	std::optional<World> world;
	try {
		// the tables grow only at their end (Stage 43): a save made with fewer species is the same table's first,
		// and those added since are absent from it
		const int saved_species = saved.at("species").get<int>();
		if (saved_species < 1 || saved_species > table.species_count()) {
			why = "the save was made with other data: it counts more species than the tables have";
			return nullptr;
		}
		World::Builder b(table);
		const auto species = static_cast<size_t>(saved_species);
		for (const json &cell : saved.at("cells")) {
			double reservoir_temperature = 0.0, volume = 0.0, mixing = 1.0;
			if (!from_bits(cell.at("reservoir_temperature"), reservoir_temperature) || !from_bits(cell.at("volume"), volume) || !from_bits(cell.at("mixing"), mixing)) {
				why = "the save holds a number that is not one";
				return nullptr;
			}
			const std::string name = cell.at("name").get<std::string>();
			const CellId id = cell.at("reservoir").get<bool>() ? b.add_reservoir(name, reservoir_temperature)
					: cell.at("rigid").get<bool>()			  ? b.add_rigid_cell(name, volume)
					: cell.value("layer", false)			  ? b.add_layer_cell(name)
															  : b.add_cell(name);
			if (cell.contains("column")) { // a column's top (Stage 47)
				double whole = 0.0, area = 0.0;
				if (!from_bits(cell.at("column").at(0), whole) || !from_bits(cell.at("column").at(1), area)) {
					why = "the save holds a number that is not one";
					return nullptr;
				}
				b.set_column(id, area, whole);
				if (cell.contains("column_shape") && cell.at("column_shape").is_array() && cell.at("column_shape").size() >= 3) {
					std::vector<double> shape;
					double rise = 0.0;
					bool read = from_bits(cell.at("column_shape").at(0), rise);
					for (size_t k = 1; k < cell.at("column_shape").size() && read; ++k) {
						double v = 0.0;
						read = from_bits(cell.at("column_shape").at(k), v);
						shape.push_back(v);
					}
					if (!read) {
						why = "the save holds a number that is not one";
						return nullptr;
					}
					b.set_column_shape(id, rise, std::move(shape));
				}
			}
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
			edge.column = e.value("column", false);
			if ((e.contains("rate") && !from_bits(e.at("rate"), edge.rate)) || (e.contains("carried") && !from_bits(e.at("carried"), edge.carried)) ||
					(e.contains("solids_rate") && !from_bits(e.at("solids_rate"), edge.solids_rate)) || (e.contains("power") && !from_bits(e.at("power"), edge.power)) ||
					(e.contains("exchange") && !from_bits(e.at("exchange"), edge.exchange))) {
				why = "the save holds a number that is not one";
				return nullptr;
			}
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
		impl->mains = CellId{ bench.value("mains", -1) };
		impl->instruments = bench.value("readings", "truth") == "instruments"; // (Stage 50)
		impl->seed = bench.value("seed", uint64_t{ 0 });
		if (bench.contains("notebook")) { // (Stage 54)
			for (const json &e : bench.at("notebook").at("pages")) {
				NoteEntry n{ e.at("step").get<int64_t>(), e.at("label").get<std::string>(), Quantity{ e.at("item").get<std::string>(), e.at("what").get<std::string>(), e.at("measure").get<std::string>() },
					0.0, e.at("unit").get<std::string>(), e.at("instrument").get<bool>() };
				if (!from_bits(e.at("value"), n.value)) {
					why = "the save holds a number that is not one";
					return nullptr;
				}
				impl->notebook.push_back(n);
			}
			for (const json &e : bench.at("notebook").at("loggers")) {
				NoteLogger l{ e.at("label").get<std::string>(), Quantity{ e.at("item").get<std::string>(), e.at("what").get<std::string>(), e.at("measure").get<std::string>() }, 1.0,
					e.at("next").get<int64_t>() };
				if (!from_bits(e.at("every"), l.every_s)) {
					why = "the save holds a number that is not one";
					return nullptr;
				}
				impl->loggers.push_back(l);
			}
		}
		for (const json &j : bench.at("items")) {
			Item item;
			item.id = j.at("id").get<std::string>();
			const int kind = j.at("kind").get<int>(), place = j.at("place").get<int>();
			if (kind < 0 || kind > static_cast<int>(Kind::Spectro) || place < 0 || place > static_cast<int>(Place::Trash)) {
				why = "the save names a kind of item or a place that there is not";
				return nullptr;
			}
			item.kind = static_cast<Kind>(kind);
			item.place = static_cast<Place>(place);
			item.cell = CellId{ j.at("cell").get<int32_t>() };
			item.landed = j.value("landed", 0);

			item.mouth = EdgeId{ j.at("mouth").get<int32_t>() };
			item.heats = EdgeId{ j.at("heats").get<int32_t>() };
			item.joint = EdgeId{ j.at("joint").get<int32_t>() };
			item.under = j.at("under").get<int>();
			item.on = j.at("on").get<int>();
			item.holds = j.at("holds").get<int>();
			item.drains_to = j.at("drains_to").get<int>();
			if (j.contains("pours")) {
				const json &pj = j.at("pours");
				item.pours_into = pj.at("into").get<int>();
				item.pour = EdgeId{ pj.at("edge").get<int32_t>() };
				item.inverted = pj.at("inverted").get<bool>();
				if (!from_bits(pj.at("tilt"), item.tilt) || !from_bits(pj.at("cap"), item.cap)) {
					why = "the save holds a number that is not one";
					return nullptr;
				}
			}
			item.stirred = j.at("stirred").get<bool>();
			item.stirs = EdgeId{ j.value("stirs", -1) };
			if ((j.contains("swirl") && !from_bits(j.at("swirl"), item.swirl)) || (j.contains("suspended") && !from_bits(j.at("suspended"), item.suspended)) ||
					(j.contains("swirl_holds") && (!from_bits(j.at("swirl_holds").at(0), item.swirl_for) || !from_bits(j.at("swirl_holds").at(1), item.swirl_most) ||
														  !from_bits(j.at("swirl_holds").at(2), item.swirl_lifts) || !from_bits(j.at("swirl_holds").at(3), item.settles)))) {
				why = "the save holds a number that is not one";
				return nullptr;
			}
			item.submitted = j.at("submitted").get<bool>();
			item.col = j.at("col").get<int>();
			item.row = j.at("row").get<int>();
			item.w = j.value("w", 1);
			item.h = j.value("h", 1);
			item.burst = j.at("burst").get<bool>();
			item.slot = j.value("slot", 0);
			item.glassware = j.value("glassware", "");
			item.screwed = j.value("screwed", false);
			item.label = j.value("label", "");
			if (j.contains("thermometer") && !from_bits(j.at("thermometer"), item.read_kelvin)) { // (Stage 52)
				why = "the save holds a number that is not one";
				return nullptr;
			}
			if (j.contains("stopcock") && !from_bits(j.at("stopcock"), item.cap)) { // (Stage 52)
				why = "the save holds a number that is not one";
				return nullptr;
			}
			if (j.contains("balance")) { // (Stage 50)
				item.model = j.at("balance").at("model").get<std::string>();
				if (instruments::balance(item.model) == nullptr || !from_bits(j.at("balance").at("tare"), item.tare)) {
					why = "the save names a balance that there is not, or holds a number that is not one";
					return nullptr;
				}
			}
			if (j.contains("spectro") && (!j.at("spectro").is_array() || j.at("spectro").size() != 3 || !from_bits(j.at("spectro").at(0), item.nm) ||
						!from_bits(j.at("spectro").at(1), item.blank) || !from_bits(j.at("spectro").at(2), item.blank_nm))) { // (Stage 55)
				why = "the save holds a number that is not one";
				return nullptr;
			}
			if (j.contains("holder_c") && !from_bits(j.at("holder_c"), item.holder_c)) { // (Stage 56)
				why = "the save holds a number that is not one";
				return nullptr;
			}
			if (item.kind == Kind::Balance && item.model.empty()) {
				why = "the save's balance has no model";
				return nullptr;
			}
			if (j.contains("thermal")) {
				const json &tj = j.at("thermal");
				ThermalSettings &t = item.thermal;
				double reflected = 0.0;
				t.frame_room = tj.at("room").get<bool>();
				t.palette = tj.at("palette").get<std::string>();
				t.markers = tj.at("markers").get<bool>();
				t.locked = tj.at("locked").get<bool>();
				t.gain = tj.at("gain").get<std::string>();
				if (!from_bits(tj.at("at").at(0), t.frame_x) || !from_bits(tj.at("at").at(1), t.frame_y) || !from_bits(tj.at("emissivity"), t.emissivity) || !from_bits(tj.at("level"), t.level_c) ||
						!from_bits(tj.at("span"), t.span_c) || (!tj.at("reflected").is_null() && !from_bits(tj.at("reflected"), reflected))) {
					why = "the save holds a number that is not one";
					return nullptr;
				}
				if (!tj.at("reflected").is_null()) {
					t.reflected_c = reflected;
				}
			}
			if (j.contains("probe")) {
				const json &pj = j.at("probe");
				item.probe_on = pj.at("on").get<std::string>();
				item.probe_room = pj.at("room").get<bool>();
				if (!from_bits(pj.at("at").at(0), item.probe_u) || !from_bits(pj.at("at").at(1), item.probe_v)) {
					why = "the save holds a number that is not one";
					return nullptr;
				}
			}
			if (j.contains("hold_n") && !from_bits(j.at("hold_n"), item.hold_n)) {
				why = "the save holds a number that is not one";
				return nullptr;
			}
			if ((j.contains("nominal_ml") && !from_bits(j.at("nominal_ml"), item.nominal_ml)) || !from_bits(j.at("full"), item.full) || !from_bits(j.at("valve"), item.valve) || !from_bits(j.at("capacity"), item.capacity)
					|| (j.contains("wall_mm") && !from_bits(j.at("wall_mm"), item.wall_mm)) || (j.contains("floor_mm") && !from_bits(j.at("floor_mm"), item.floor_mm))) {
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
		if (bench.contains("trash") && !impl->trash_from(bench.at("trash"), why)) {
			return nullptr;
		}
		for (const json &j : bench.value("trials", json::array())) { // (Stage 39)
			Lab::Impl::Trial t;
			t.part = j.at("part").get<std::string>();
			const int at = impl->find_part(t.part);
			for (size_t k = 0; k < 2; ++k) {
				t.copies[k] = Lab::load(j.at("copies").at(k).dump(), why);
				if (!t.copies[k]) {
					return nullptr;
				}
				t.copies[k]->set_checking(false);
				t.copies[k]->impl_->trial = true;
			}
			if (at < 0 || impl->parts[static_cast<size_t>(at)].kind != PartKind::Controller || !to_doubles(j.at("t"), t.t) || !to_doubles(j.at("r"), t.r) ||
					!from_bits(j.at("distance"), t.distance) || !from_bits(j.at("basis").at(0), t.basis_g) || !from_bits(j.at("basis").at(1), t.basis_cp)) {
				why = "the save holds a controller's trial that cannot be read";
				return nullptr;
			}
			t.basis_has = j.at("basis").at(2).get<uint64_t>();
			t.again = j.at("again").get<bool>();
			impl->parts[static_cast<size_t>(at)].trying = true;
			impl->trials.push_back(std::move(t));
		}
		for (const Item &x : impl->items) {
			if (x.place == Place::Trash && impl->trash_at(x.id) < 0) {
				why = "the save holds \"" + x.id + "\" in a trash that does not list it";
				return nullptr;
			}
		}
		if (bench.contains("notices") && !impl->notices_from(bench.at("notices"), why)) {
			return nullptr;
		}
		if (bench.contains("goals") && !impl->goals_from(bench.at("goals").at("list"), bench.at("goals").at("made").get<int64_t>(), why)) {
			return nullptr;
		}
		for (Item &x : impl->items) { // a save from before the slots: each in the first free one
			for (int k = 1; x.place == Place::Outbox && x.slot == 0 && k <= OUTBOX_SLOTS; ++k) {
				x.slot = impl->held_in(k) < 0 ? k : 0;
			}
		}
		const json &remainders = saved.at("remainders");
		if (!to_doubles(remainders.at("heat"), heat) || !to_doubles(remainders.at("reaction"), reaction) || !to_doubles(remainders.at("nucleation"), nucleation) ||
				(remainders.contains("work") && !to_doubles(remainders.at("work"), work))) {
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
		const bool refs = (item.kind == Kind::Viewer || item.kind == Kind::Thermal || stands_under(item.kind) ? item.cell.index < 0 : item.cell.index >= 0 && item.cell.index < cells) && item.mouth.index < edge_count && item.heats.index < edge_count && item.joint.index < edge_count && item.stirs.index < edge_count &&
				item.under < item_count && item.on < item_count && item.holds < item_count && item.drains_to < item_count;
		if (!refs) {
			why = "the save's items point outside the world";
			return nullptr;
		}
	}
	if (impl->air.index < 0 || impl->air.index >= cells || impl->room.index < 0 || impl->room.index >= cells || impl->mains.index >= cells) {
		why = "the save's room is not in its world";
		return nullptr;
	}
	impl->sim.emplace(std::move(*world), std::vector<Reaction>(builtin_reactions().begin(), builtin_reactions().end()));
	impl->sim->set_checking(false);
	impl->sim->restore_remainders(std::move(heat), std::move(reaction), std::move(nucleation), std::move(work));
	impl->genesis = totals(table, impl->world().snapshot());
	if (!impl->restored_notices) {
		impl->detect(true); // a save from before notices: what the detectors see from now on
	} else {
		impl->sim->restore_tally(std::move(impl->restored_tally));
	}
	return std::unique_ptr<Lab>(new Lab(std::move(impl)));
}

} // namespace lab
