#pragma once

#include "lab/signals.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

// The engine-free lab layer (GAME_SPEC §13): a bench of equipment over one
// closed world, driven by commands. Everything a player can do is a command;
// a session is a list of them. The lab does no file access: a host hands it
// the bench as text.
namespace lab {

// "chem-game-v2 <GAME_SPEC version>"
std::string build_name();

// What a host or the screen needs of one item, measured from the state (the
// report as plain data).
struct SubstanceView {
	std::string species; // "water"
	std::string formula; // "H2O"
	char phase = 's'; // s, l, g; a for dissolved
	bool wall = false;
	double mol = 0.0;
	double g = 0.0;
	int64_t particles = 0;
	double size_um = 0.0;
};

struct ItemView {
	std::string id;
	std::string kind; // vessel, balloon, burner, tray; a signal part: timer, monitor, alarm
	std::string place; // cabinet, bench, on, under, outbox
	int col = -1, row = -1; // on the bench: its footprint's top-left square
	int w = 1, h = 1; // its footprint, in squares
	int on = -1; // index of the vessel whose mouth it is on
	int under = -1; // a burner: index of the vessel over it
	int holds = -1; // a vessel: index of what is on its mouth
	int drains_to = -1; // a vessel: index of what stands under its outlet
	// pouring (GAME_SPEC §13, Stage 27): the vessel it pours into (index; -1: none), held or upside down with a
	// cap; a held pour's tilt (its share of the full), the cap's opening
	int pours_into = -1;
	bool inverted = false;
	double tilt = 0.0, cap = 0.0;
	bool condenser = false; // a vessel with a coolant
	bool stirred = false;
	bool submitted = false;
	int slot = 0; // in the outbox: its slot, 1 to 6 (§13.5)
	bool burst = false;
	double valve = 0.0; // a burner
	double celsius = 0.0;
	double volume_ml = 0.0; // a vessel's own
	double capacity_ml = 0.0; // a balloon's
	double wall_mm = 0.0; // a vessel's wall, how thick: what is seen through it bends by it (§12.3)
	double floor_mm = 0.0; // and its floor, glass all through: seen from the side it bends as a rod does
	std::string glassware; // a vessel made to a standard (§12.3, Stage 24): its kind (lab/glassware.hpp), or none
	double nominal_ml = 0.0; // its nominal capacity: its name, and how large it is drawn
	double gas_ml = 0.0;
	double condensed_ml = 0.0;
	double liquid_ml = 0.0;
	double solid_ml = 0.0; // its solids, not its wall: what rests on its floor (Stage 26: not what is condensed less what is liquid, which counts a solute's volume as solid)
	std::vector<SubstanceView> contents;
	Part part; // a signal part (§13.3): its settings and state
};

// What the lab says happened (GAME_SPEC §13.4), at a level the player may
// silence: a detail, an event, a warning, a danger.
struct Notice {
	enum Level : int32_t { Detail = 0, Event = 1, Warning = 2, Danger = 3 };
	int64_t step = 0;
	Level level = Detail;
	std::string item; // what it is about; "" for the lab itself
	std::string says;
	bool operator==(const Notice &) const = default;
};
const char *level_name(Notice::Level level); // "detail", "event", "warning", "danger"
bool level_of(std::string_view name, Notice::Level &level);

// A goal (GAME_SPEC §13.5): what should be handed in to a slot of the outbox,
// as terms on what is there; met while a slot holds an item and every term
// holds of it, measured live.
constexpr int OUTBOX_SLOTS = 6;
struct GoalTerm {
	Quantity quantity; // no item: whatever is handed in to the slot
	Comparison comparison; // never "reaches": a goal is a state, not an event
	bool operator==(const GoalTerm &) const = default;
};
struct Goal {
	std::string id; // the bench's ("co2"), or "goal1" for the player's
	std::string name;
	int slot = 1; // 1 to 6
	bool game = false; // the bench's: locked
	std::vector<GoalTerm> terms;
	bool met = false; // now
	bool operator==(const Goal &) const = default;
};

struct LabView {
	int64_t step = 0;
	int cols = 0, rows = 0; // the bench's grid, in squares
	bool conserved = true; // every total is what it was at the start
	double mass_g = 0.0;
	double energy_j = 0.0;
	std::vector<ItemView> items; // the equipment, then the signal parts, in the order they were made
	std::vector<std::string> events; // "the balloon burst at 12.5 s"
	// signals (§13.3)
	std::vector<Wire> wires;
	std::vector<Rig> rigs; // in the order they were made
	std::vector<ControlSetting> controls; // every control's last setting, and who made it
	std::vector<std::string> ringing; // the alarms ringing
	std::vector<Notice> notices; // oldest first (the last 2000)
	std::vector<Goal> goals; // §13.5: in the order they were made, the bench's first
};

// The debug panel (GAME_SPEC §13.6): what an item holds, its flows in the
// last step, and each reaction's account of itself.
struct SpeciesDebug {
	std::string species, formula; // "water", "H2O(l)"
	char phase = 's'; // s, l, g; a for dissolved
	double mol = 0.0, g = 0.0;
	double fraction = 1.0; // its mole fraction in its own mixture: of the gas, of the solution; 1 for a solid
	double molality = 0.0; // a solute's, mol/kg of water
	double activity = 0.0;
};
struct FlowDebug {
	std::string with; // the other end: an item, "room", "air", "tray", "<item>.coolant"
	std::string process; // heat, drain, settle, overflow
	double watts = 0.0; // into the item
	double grams_per_s = 0.0; // into the item
};
struct ReactionDebug {
	std::string id, equation, explain;
	double rate_mol_s = 0.0; // the last step's extent, forward positive
	bool weighed = false; // its affinity is defined here
	double affinity_kj = 0.0; // now, kJ/mol (may be infinite)
	std::string limit; // what limits it
};
struct ItemDebug {
	std::string item;
	bool found = false;
	bool has_temperature = false;
	double celsius = 0.0, heat_capacity = 0.0; // J/K
	std::vector<SpeciesDebug> species;
	std::vector<FlowDebug> flows;
	std::vector<ReactionDebug> reactions;
};

class Lab {
public:
	// A bench (data/benches/*.json, "chem-bench/1"): the room, and the
	// equipment with what it holds. null, with the reason in `why`, when the
	// text is not a bench or describes a world that cannot be built.
	static std::unique_ptr<Lab> open(std::string_view bench_json, std::string &why);
	~Lab();

	// One command, a JSON object with a "do" (GAME_SPEC §13 lists them).
	// false with the reason in `why` when it is refused: the state is then as
	// it was, except that a command which changes connections first takes the
	// lab to a step boundary (at most one step). Also false when an invariant
	// failed on the way: failed() is then true and the lab takes no more.
	bool command(std::string_view command_json, std::string &why);

	// Advances whole steps of 1/60 s; with `stop_on_ring`, it stops at the end
	// of a step in which an alarm started ringing (§13.3).
	bool advance(int64_t steps, std::string &why, bool stop_on_ring = false);

	// The whole lab as text ("chem-save/1"): every amount, energy and particle
	// count, the connections, the processes' carried remainders, where
	// everything stands. load() gives a lab that continues exactly as this one
	// would have: the same digests, step for step (GAME_SPEC §18: D2).
	std::string save() const;
	static std::unique_ptr<Lab> load(std::string_view save_json, std::string &why);

	// Every invariant on every step (GAME_SPEC §18). On by default.
	void set_checking(bool on);
	bool failed() const;

	int64_t step() const;
	// An alarm started ringing in the last step (§13.3).
	bool rang() const;
	// What is where, measured, as plain data.
	LabView view() const;
	// An item from the inside (GAME_SPEC §13.6): its composition, its flows in
	// the last step, its reactions and what limits each.
	ItemDebug debug(const std::string &item) const;
	// What is where, measured (JSON; GAME_SPEC §13: reports are functions of state).
	std::string report() const;
	// SHA-256 of the whole state: amounts, energies, particles, the processes'
	// carried remainders, the connections, the step. Equal digests are equal states.
	std::string digest() const;

private:
	struct Impl;
	explicit Lab(std::unique_ptr<Impl> impl);
	std::unique_ptr<Impl> impl_;
};

} // namespace lab
