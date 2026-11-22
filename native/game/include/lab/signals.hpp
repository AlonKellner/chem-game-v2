#pragma once

#include "lab/quantity.hpp"

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// Signals (GAME_SPEC §13.3): parts on the bench that watch the lab and act on
// it. A timer counts down; a monitor compares a quantity; an alarm alerts the
// player. Parts are wired to each other and rigged to the lab's controls.
// Engine-free, and pure: the lab measures and applies, these decide.
namespace lab {

// A circuit holds other parts, circuits too, compactly on one square (§13.3);
// it signals nothing itself: what is in it signals as it would out of it.
// A notice part signals when the lab writes a notice at or above its level
// (§13.4), about its item if it names one, its words containing its text if
// it has one (Stage 28), for its hold: so many steps from the one it hears it
// (one, unless set), another restarting it. A writer writes a notice of its
// own (its level, its words) when its input turns on, or off, or either
// (Stage 28): with notice parts, the lab's notices are a bus parts post to and
// hear from.
// A controller holds a quantity it senses at a target, by a level from 0 to 1
// its rigs follow (Stage 26).
enum class PartKind : int32_t { Timer = 0, Monitor = 1, Alarm = 2, Circuit = 3, Notice = 4, Writer = 5, Controller = 6 };
constexpr int PART_KINDS = 7;
constexpr double STEP_SECONDS = 1.0 / 60.0;
constexpr size_t MAX_SAYS = 80; // a writer's words, in bytes
const char *part_kind_name(PartKind kind); // "timer", "monitor", "alarm", "circuit", "notice", "writer", "controller"
bool part_kind_of(std::string_view name, PartKind &kind);
// whether a part sends a signal: an alarm and a writer only take one, a circuit's parts send theirs
bool sends(PartKind kind);
// whether a part senses an item (§13.3, Stage 26): a monitor, a controller
bool senses(PartKind kind);

struct Part {
	std::string id; // "monitor1"
	PartKind kind = PartKind::Timer;
	int col = -1, row = -1; // its square: on the bench, or inside its circuit
	std::string in; // the circuit it is in; "" on the bench
	int inner_w = 0, inner_h = 0; // a circuit's own grid, in squares
	int notice_level = 2; // a notice part's or a writer's: 0 detail, 1 event, 2 warning, 3 danger; its item is quantity.item ("" for any; a writer's own id)
	std::string says; // a writer's words ("" for "<id> was signalled")
	// a notice part's (Stage 28): the text a notice's words must contain to be heard ("" any); how long it signals
	// for a notice it hears, in steps, from that step on (another restarts it); the steps of that still to go
	std::string contains;
	int64_t hold = 1, held = 0;
	// a writer's (Stage 28): what of its input writes: 0 its turning on, 1 its turning off, 2 either
	int writes = 0;

	// a monitor: what it watches, once set
	bool watching = false;
	Quantity quantity;
	Comparison comparison;
	// a timer: its preset and what is left of it, in steps of 1/60 s
	int64_t preset = 5 * 60 * 60;
	int64_t left = 5 * 60 * 60;

	// a controller (Stage 26): what it holds the quantity at, and how fast (0 gentle, 1 normal, 2 fast); set
	// when `watching`. What its trial found (`tuned`): the process and the PI gains; its PWM period, in steps.
	double target = 0.0;
	int speed = 1;
	bool tuned = false;
	double gain = 0.0, tau = 0.0, theta = 0.0; // the process (lab/control.hpp: ProcessModel); tau 0: integrating
	double kc = 0.0, ti = 0.0;
	int64_t period = 0;
	// its state: the error's integral (the unit times seconds), its level, and its PWM: the step in the period,
	// the period's steps on (from its level at the period's start, `duty`), and whether this step is on
	double integral = 0.0, level = 0.0, duty = 0.0;
	int64_t phase = 0, on_steps = 0;
	bool pulse = false;
	// what it controls as its trial found it (Stage 28): the sensed item's contents' mass (g) and heat capacity
	// (J/K), and the materials there (a bit for each species of a milligram or more); changed by a tenth, or a
	// material come or gone, it tunes again. How long it has been held at a limit short of its target, in steps,
	// and whether it has said so.
	double basis_g = 0.0, basis_cp = 0.0;
	uint64_t basis_has = 0;
	int64_t pinned = 0;
	bool pinned_said = false;

	// its state at the end of the last step
	std::optional<double> last; // a monitor's last reading
	bool own = false; // its own condition: a monitor's comparison, a timer at zero, an alarm's or a writer's input
	bool out = false; // its signal: its own condition, while its inputs let it (an alarm and a writer send none)
	bool ringing = false; // an alarm, from the step its input turned on until acknowledged
	int64_t rang_at = -1;

	bool operator==(const Part &) const = default;
};

// A part's signal into another part's input.
struct Wire {
	std::string from, to;
	bool operator==(const Wire &) const = default;
};

// A part's signal into a control of the lab: a vessel's `stir` (1 stir, 0
// stop), a burner's `valve` (0 to 1), a timer's `reset` (1). Each setting is
// a value, or none to leave the control as it is.
struct Rig {
	std::string from, item, control;
	bool change = false; // set on either change of the signal; otherwise on its turning on and off
	bool follow = false; // a controller's: the control follows its level (a valve) or its PWM (stirring), every step
	std::optional<double> on, off, any; // `any`: the setting on a change
	bool seen = false; // the signal it last acted on
	bool operator==(const Rig &) const = default;
};

struct RigAction {
	size_t rig = 0; // its index
	double value = 0.0;
	bool follow = false; // what a controller's rig follows: applied where the control differs, and noted by nobody
};

struct SignalStep {
	std::vector<RigAction> actions; // in the order the rigs were made: the newest acts last, and wins
	std::vector<std::string> rang; // the alarms that started ringing
	std::vector<std::string> written; // the writers whose input turned on: each writes its notice
};

// What a monitor reads: a quantity now, none when it is not one of the lab's.
using QuantityReader = std::function<std::optional<double>(const Quantity &)>;
// What a notice part reads: whether the lab wrote a notice since the step
// before at or above `level`, about `item` ("" for any), its words containing
// `contains` ("" for any words).
using NoticeReader = std::function<bool(int level, const std::string &item, const std::string &contains)>;
constexpr size_t MAX_CONTAINS = 40; // a notice part's text, in bytes
const char *writes_name(int writes); // "on", "off", "change"
bool writes_of(std::string_view name, int &writes);

// One step's signals (§13.3), at the step's end. Synchronous: every part reads
// its inputs' signals as they were at the end of the step before, and all are
// updated together, so any wiring, loops included, is well defined. A part
// with no inputs is enabled; with inputs, while any of them is on (inputs in
// parallel are OR; a chain is AND). Then each rig whose source's signal, as
// it was at the end of the step before, changed acts, if it has a setting for
// that change: every link, a wire or a rig, takes one step.
SignalStep evaluate(std::vector<Part> &parts, const std::vector<Wire> &wires, std::vector<Rig> &rigs, const QuantityReader &read, int64_t step,
		const NoticeReader &noticed = {});

// Whether a part's inputs let it signal, from their signals `before`.
bool enabled(const std::string &id, const std::vector<Wire> &wires, const std::vector<Part> &before);

// A timer reset (by hand or by a rig): back to its preset, its signal off.
void reset(Part &timer);

// A controller's tuning found, or lost: its gains and PWM period from the process (Stage 26), its integral
// and level from nothing.
void retune(Part &controller, bool tuned);

// Invariant G3 (GAME_SPEC §18): a controller's level is within 0 and 1, and 0 while its inputs, as they were
// before, do not let it or it is unset or untuned; its own condition is its level above 0. Empty when it holds.
std::vector<std::string> check_G3(const std::vector<Part> &parts, const std::vector<Wire> &wires, const std::vector<Part> &before);
// Invariant G4: a controller's PWM is on, in each period, for the steps its level at the period's start
// gives: its on steps are that share of the period, rounded, and each step is on while the step's place in
// the period is under them.
std::vector<std::string> check_G4(const std::vector<Part> &parts);

// Invariant G2 (GAME_SPEC §18): every part's signal is its own condition
// while its inputs, as they were before, let it. Empty when it holds.
std::vector<std::string> check_G2(const std::vector<Part> &parts, const std::vector<Wire> &wires, const std::vector<Part> &before);

// The last setting of each control of the lab, and who made it (G1).
struct ControlSetting {
	std::string item, control;
	double value = 0.0;
	std::string by; // a part's id, or "player"
	int64_t step = 0;
	bool operator==(const ControlSetting &) const = default;
};
// Invariant G1: every control is as its last setting left it. `now` reads a
// control's value in the lab; none for a control that is not there.
std::vector<std::string> check_G1(const std::vector<ControlSetting> &settings, const std::function<std::optional<double>(const std::string &item, const std::string &control)> &now);

} // namespace lab
