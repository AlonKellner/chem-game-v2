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
enum class PartKind : int32_t { Timer = 0, Monitor = 1, Alarm = 2, Circuit = 3 };
const char *part_kind_name(PartKind kind); // "timer", "monitor", "alarm", "circuit"
bool part_kind_of(std::string_view name, PartKind &kind);

struct Part {
	std::string id; // "monitor1"
	PartKind kind = PartKind::Timer;
	int col = -1, row = -1; // its square: on the bench, or inside its circuit
	std::string in; // the circuit it is in; "" on the bench
	int inner_w = 0, inner_h = 0; // a circuit's own grid, in squares

	// a monitor: what it watches, once set
	bool watching = false;
	Quantity quantity;
	Comparison comparison;
	// a timer: its preset and what is left of it, in steps of 1/60 s
	int64_t preset = 5 * 60 * 60;
	int64_t left = 5 * 60 * 60;

	// its state at the end of the last step
	std::optional<double> last; // a monitor's last reading
	bool own = false; // its own condition: a monitor's comparison, a timer at zero, an alarm's input
	bool out = false; // its signal: its own condition, while its inputs let it (an alarm sends none)
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
	std::optional<double> on, off, any; // `any`: the setting on a change
	bool seen = false; // the signal it last acted on
	bool operator==(const Rig &) const = default;
};

struct RigAction {
	size_t rig = 0; // its index
	double value = 0.0;
};

struct SignalStep {
	std::vector<RigAction> actions; // in the order the rigs were made: the newest acts last, and wins
	std::vector<std::string> rang; // the alarms that started ringing
};

// What a monitor reads: a quantity now, none when it is not one of the lab's.
using QuantityReader = std::function<std::optional<double>(const Quantity &)>;

// One step's signals (§13.3), at the step's end. Synchronous: every part reads
// its inputs' signals as they were at the end of the step before, and all are
// updated together, so any wiring, loops included, is well defined. A part
// with no inputs is enabled; with inputs, while any of them is on (inputs in
// parallel are OR; a chain is AND). Then each rig whose source's signal, as
// it was at the end of the step before, changed acts, if it has a setting for
// that change: every link, a wire or a rig, takes one step.
SignalStep evaluate(std::vector<Part> &parts, const std::vector<Wire> &wires, std::vector<Rig> &rigs, const QuantityReader &read, int64_t step);

// Whether a part's inputs let it signal, from their signals `before`.
bool enabled(const std::string &id, const std::vector<Wire> &wires, const std::vector<Part> &before);

// A timer reset (by hand or by a rig): back to its preset, its signal off.
void reset(Part &timer);

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
