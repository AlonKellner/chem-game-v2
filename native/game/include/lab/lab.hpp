#pragma once

#include "lab/signals.hpp"

#include <cstdint>
#include <memory>
#include <optional>
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
	double ml = 0.0; // its volume (Stage 28): a solid's or a liquid's own, a solute's apparent, a gas's share of the gas
};

// A thermal camera's settings (§13, Stage 48): where its sensor's frame is, and how it reads and shows what it sees.
// The frame is over its own screen (see-through), or fixed on the room (frame_room) with its top-left at (frame_x,
// frame_y) in the bench's squares from the bench's top-left corner. It reads by `emissivity`, the room it reflects at
// `reflected_c` (none: the room's own); shows by `palette` (data/thermal.json), the hottest and coldest marked
// (`markers`); over its gain's range, or `locked` to level_c +- span_c / 2; its gain high (-20 to 150 C), low (0 to
// 650 C) or auto (high, and low above it).
struct ThermalSettings {
	bool frame_room = false;
	double frame_x = 0.0, frame_y = 0.0;
	std::string palette = "ironbow";
	double emissivity = 0.95;
	std::optional<double> reflected_c;
	bool markers = true;
	bool locked = false;
	double level_c = 60.0, span_c = 80.0;
	std::string gain = "auto";
};

// One layer of a vessel's column (GAME_SPEC §6, Stage 47), as the picture and the readout show it: its liquid's
// volume and its solids', its temperature, and what it holds
struct LayerView {
	double liquid_ml = 0.0, solid_ml = 0.0, celsius = 0.0;
	std::vector<SubstanceView> contents;
};

struct ItemView {
	std::string id;
	std::string kind; // vessel, balloon, burner, tray, stopper, viewer (Stage 42), thermal (Stage 48), balance (Stage 50); a signal part: timer, monitor, alarm
	std::string place; // cabinet, bench, on, under, outbox, trash; a signal part: bench, circuit, trash
	int col = -1, row = -1; // on the bench: its footprint's top-left square; in the trash: the square it goes back to (none: the cabinet)
	int w = 1, h = 1; // its footprint, in squares
	int on = -1; // index of the vessel whose mouth it is on
	int under = -1; // a burner, a balance (Stage 50): index of the vessel over it
	int holds = -1; // a vessel: index of what is on its mouth
	int drains_to = -1; // a vessel: index of what stands under its outlet
	// pouring (GAME_SPEC §13, Stage 27): the vessel it pours into (index; -1: none), held or upside down with a
	// cap; a held pour's tilt (its share of the full), the cap's opening
	int pours_into = -1;
	bool inverted = false;
	double tilt = 0.0, cap = 0.0;
	// what a held pour's tilt is in the room (Stage 31): the vessel's turn about its lip, radians (its tilt's share
	// of the turn at which it is poured out), and how far its liquid stands over the lip, cm (0 or less: not at it)
	double turn = 0.0, head_cm = 0.0;
	// what runs down its pour now: liquid with what is dissolved and suspended in it, and sediment by itself (its
	// solids' own volume)
	double pours_ml_s = 0.0, pours_solid_ml_s = 0.0;
	bool condenser = false; // a vessel with a coolant
	bool stirred = false;
	// A stirred liquid's turning (GAME_SPEC §11, Stage 32): its kinetic energy and the most it can hold, J; its
	// vortex's share (0 to 1) of the largest that fits; and the share of its solids the liquid holds up (the rest
	// lies as sediment)
	double swirl_j = 0.0, swirl_most_j = 0.0, vortex = 0.0, suspended = 0.0;
	bool submitted = false;
	int slot = 0; // in the outbox: its slot, 1 to 6 (§13.5)
	bool burst = false; // a balloon, burst; a vessel, broken (its glass burst, Stage 28)
	// pressure (GAME_SPEC §13, Stage 28): its own (an open one's the room's), kPa; sealed (no way out for its gas);
	// an upright bottle's screw cap on; what it gives way at, sealed (its stopper's hold over its neck, or its
	// glass's), kPa; a stopper's hold, N
	double pressure_kpa = 101.325;
	bool sealed = false, screwed = false;
	double gives_way_kpa = 0.0;
	double hold_n = 0.0;
	double valve = 0.0; // a burner
	double celsius = 0.0;
	// its liquid's pH (GAME_SPEC §9, Stage 43): -log10 a(H+); none without a liquid
	std::optional<double> ph;
	// boiling's bubbles (GAME_SPEC §11, Stage 45), derived from the last step, not state: its floor's boundary
	// layer's temperature, and the boiling point there; bubbles of steam leaving it, and whether they reach the
	// surface; how high bubbles reach over the floor, cm (the liquid's depth where they reach the surface; a
	// reaction's gas reaches it); a steam bubble's size as it leaves, mm; the liquid's depth, cm; what the optical
	// pass reads: the steam bubbles' extinction a cm at the floor, falling as (1 - z / end)^exponent to where they
	// end (bubbles_end_cm over the floor), and a reaction's gas's, the same at every height
	double floor_c = 0.0, floor_boils_c = 0.0;
	bool boiling = false, bubbles_surface = false;
	double bubbles_cm = 0.0, bubble_mm = 0.0, depth_cm = 0.0;
	double bubble_extinction = 0.0, bubble_exponent = 0.0, bubbles_end_cm = 0.0, gas_extinction = 0.0;
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
	// a vessel's column while it is layered (Stage 47), from the top down (its own cell's liquid first); empty while
	// it is mixed. `contents` and the rest are the whole column's.
	std::vector<LayerView> layers;
	// its wall's temperatures (Stage 53), °C: its floor first, then its rings up to its brim; empty for a vessel whose
	// wall is in its own cell (one made as no glassware)
	std::vector<double> wall_c;
	Part part; // a signal part (§13.3): its settings and state
	// a spectral viewer (§13, Stage 42): where its probe is fixed: on an item or a part (probe_on, its id), at
	// (probe_u, probe_v) of it in its own frame (0 to 1 across and down its rectangle, upright); on the room
	// (probe_room), at (probe_u, probe_v) in the bench's squares from its top-left corner; else in its holder
	std::string probe_on;
	bool probe_room = false;
	double probe_u = 0.0, probe_v = 0.0;
	ThermalSettings thermal; // a thermal camera's (Stage 48)
	// readings (GAME_SPEC §13, Readings; Stage 50): a vessel's label; its thermometer's reading, its pH meter's (with
	// a liquid), its marks' (on glassware with marks, which are `marks_ml` apart), each the truth with its instrument's
	// offset, to its resolution
	std::string label;
	std::optional<double> read_c, read_ph, read_ml, read_kpa; // (read_kpa: its gauge, Stage 54)
	double marks_ml = 0.0;
	// volumetric glass (Stage 52): its class A tolerance and its scale's divisions, mL; a mark's true volume (a pipette's,
	// a volumetric flask's); a burette's reading down from its zero line, the truth (`read_ml` the instrument's)
	double tolerance_ml = 0.0, graduation_ml = 0.0;
	std::optional<double> mark_ml, scale_ml;
	// a balance (Stage 50): its model and what it states; its load's conventional mass (what I1 holds its display
	// to), its tare; its display (none: off the bench), and whether its load is past its capacity
	std::string model;
	double readability_g = 0.0, capacity_g = 0.0, linearity_g = 0.0;
	double gross_g = 0.0, tare_g = 0.0;
	std::optional<double> display_g;
	// a spectrophotometer (Stage 55): the wavelength set, nm; whether it has a blank and the wavelength it was taken at;
	// what it reads (none without a blank, or past its range: `overload`)
	double nm = 0.0, blank_nm = 0.0;
	bool blanked = false;
	std::optional<double> absorbance;
	std::optional<double> holder_c; // its cell holder's set temperature, C (Stage 56); none: off
	bool overload = false;
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
// The trash (GAME_SPEC §13, Stage 34): what `trash` puts away and `restore` brings back where it was, the
// newest first. It holds this many elements; one more, and the oldest leaves it for good: an item to the
// cabinet (what holds matter is never destroyed), a signal part scrapped.
constexpr int TRASH_MOST = 64;
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

// The notebook (GAME_SPEC §13, Stage 54): what was read and when. An entry is a moment's reading of a quantity, by its
// label; `instrument` when an instrument read it (by instruments), not the truth.
struct NoteEntry {
	int64_t step = 0;
	std::string label;
	Quantity quantity;
	double value = 0.0;
	std::string unit;
	bool instrument = false;
	bool operator==(const NoteEntry &) const = default;
};
// A logger: a quantity recorded every `every_s` seconds, next at step `next`
struct NoteLogger {
	std::string label;
	Quantity quantity;
	double every_s = 1.0;
	int64_t next = 0;
	bool operator==(const NoteLogger &) const = default;
};

struct LabView {
	int64_t step = 0;
	int cols = 0, rows = 0; // the bench's grid, in squares
	bool conserved = true; // every total is what it was at the start
	double mass_g = 0.0;
	double energy_j = 0.0;
	double room_celsius = 20.0; // the room's air (Stage 46: what the long-wave range sees as its light)
	// readings (GAME_SPEC §13, Readings; Stage 50): by instruments (the readout shows them, not what a vessel holds);
	// the seed they are drawn from; the room's air's density, kg/m^3 (what a balance's load displaces)
	bool instruments = false;
	uint64_t seed = 0;
	double air_kg_m3 = 0.0;
	// the equipment, then the signal parts, in the order they were made (one restored from the trash comes last),
	// then the parts in the trash (Stage 34: place "trash"; with a circuit, what it holds, their `part.in` its id)
	std::vector<ItemView> items;
	// Supplies (GAME_SPEC §13, Stage 61): what the bench gives without a square, each a template no part of the room's
	// world, as it is when taken out (place "supplies"), in the bench's order; and how many copies each has had
	std::vector<ItemView> supplies;
	std::vector<int> copies;
	// the trash's elements, oldest first, as indices into `items`: the last is the newest, what `restore` brings
	// back; a circuit is one element, with what it holds
	std::vector<int> trash;
	std::vector<std::string> events; // "the balloon burst at 12.5 s"
	// signals (§13.3)
	std::vector<Wire> wires;
	std::vector<Rig> rigs; // in the order they were made
	std::vector<ControlSetting> controls; // every control's last setting, and who made it
	std::vector<std::string> ringing; // the alarms ringing
	std::vector<Notice> notices; // oldest first (the last 2000)
	std::vector<Goal> goals; // §13.5: in the order they were made, the bench's first
	std::vector<NoteEntry> notebook; // (Stage 54) oldest first
	std::vector<NoteLogger> loggers;
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
	// Stage 28: a vessel's pressure and its pour, or a signal part's state (`part`), as lines
	bool part = false;
	std::vector<std::string> facts;
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
	// A stamp of the lab's state (Stage 68): another after every command or advance, whatever it did, and no two labs'
	// alike in one process. A view taken at a stamp is the view at every moment the stamp stands (a screen takes it
	// once).
	uint64_t stamp() const;
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
	// the id of the last copy taken out of Supplies (Stage 61; none: empty)
	std::string last_taken() const;

private:
	struct Impl;
	explicit Lab(std::unique_ptr<Impl> impl);
	std::unique_ptr<Impl> impl_;
};

} // namespace lab
