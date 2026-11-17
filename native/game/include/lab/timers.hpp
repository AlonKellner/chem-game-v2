#pragma once

#include "lab/conditions.hpp"
#include "lab/lab.hpp"

#include <array>
#include <optional>
#include <string>
#include <vector>

// The player's timers (GAME_SPEC §13.3): a time limit and value conditions,
// read from the lab's view after every quarter of a lab-second; the first to
// hold pauses the clock. Engine-free, and they only read the lab.
namespace lab {

struct TimerCondition {
	Quantity quantity;
	Comparison comparison;
};

struct Timer {
	std::vector<TimerCondition> conditions; // at most MAX_CONDITIONS
	bool all = false; // the value conditions all hold, or any one
	bool limited = true;
	double limit_s = 300.0; // lab seconds from arming

	// its state
	bool fired = false;
	double armed_at_s = 0.0;
	std::vector<std::optional<double>> last; // each condition's last reading, for "reaches"
	std::string fired_by; // what fired it: "flask: H2O(l) (g) below 10.000 g (now 9.982 g)", "5:00 passed"
	std::string item_fired; // the item to show, "" for the time limit alone
};

// What a timer would be, as the editor holds it while the player writes it:
// the values as typed.
struct TimerDraft {
	std::string item;
	std::vector<TimerCondition> conditions;
	std::vector<std::array<std::string, 2>> text; // each condition's values, as typed
	bool all = false;
	bool limited = true;
	std::string limit_text = "5:00";
};

class Timers {
public:
	static constexpr int MAX_TIMERS = 5;
	static constexpr int MAX_CONDITIONS = 3;
	static constexpr int64_t READ_STEPS = 15; // a quarter of a lab-second

	// A draft made a timer, armed now. false, with the reason, when it cannot be.
	bool arm(const TimerDraft &draft, const LabView &view, std::string &why);
	void rearm(int index, const LabView &view);
	void remove(int index);
	// Reads every armed timer; the first that holds is fired and its index
	// returned, -1 when none does.
	int poll(const LabView &view);
	bool armed() const;
	const std::vector<Timer> &list() const { return timers_; }

	// A draft's fields read as numbers: the values of each condition, the time limit.
	static bool read(const TimerDraft &draft, Timer &out, std::string &why);
	// "5:00" -> 300; "90" -> 90; "1:30.5" -> 90.5
	static bool seconds_of(const std::string &text, double &seconds);
	static std::string clock_of(double seconds); // 300 -> "5:00"
	// "flask: H2O(l) (g) below 10.000 g"
	static std::string describe(const LabView &view, const TimerCondition &c);

private:
	std::vector<Timer> timers_;
};

} // namespace lab
