#include "lab/timers.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace lab {

namespace {

double now_of(const LabView &view) {
	return static_cast<double>(view.step) / 60.0;
}

bool number_of(const std::string &text, double &out) {
	if (text.empty()) {
		return false;
	}
	char *end = nullptr;
	out = std::strtod(text.c_str(), &end);
	return end != nullptr && *end == '\0' && std::isfinite(out);
}

} // namespace

bool Timers::seconds_of(const std::string &text, double &seconds) {
	const size_t colon = text.find(':');
	if (colon == std::string::npos) {
		return number_of(text, seconds) && seconds >= 0.0;
	}
	double minutes = 0.0, rest = 0.0;
	if (!number_of(text.substr(0, colon), minutes) || !number_of(text.substr(colon + 1), rest) || minutes < 0.0 || rest < 0.0 || rest >= 60.0) {
		return false;
	}
	seconds = 60.0 * minutes + rest;
	return true;
}

std::string Timers::clock_of(double seconds) {
	seconds = seconds < 0.0 ? 0.0 : seconds;
	const auto whole = static_cast<int64_t>(std::floor(seconds + 0.5));
	char buffer[32];
	std::snprintf(buffer, sizeof buffer, "%lld:%02lld", static_cast<long long>(whole / 60), static_cast<long long>(whole % 60));
	return buffer;
}

std::string Timers::describe(const LabView &view, const TimerCondition &c) {
	std::string s = label(view, c.quantity) + " " + op_name(c.comparison.op) + " " + shown(c.quantity, c.comparison.a);
	if (two_values(c.comparison.op)) {
		s += " and " + shown(c.quantity, c.comparison.b);
	}
	return s;
}

bool Timers::read(const TimerDraft &draft, Timer &out, std::string &why) {
	out = Timer{};
	if (draft.conditions.size() > static_cast<size_t>(MAX_CONDITIONS)) {
		why = "a timer has at most three value conditions";
		return false;
	}
	for (size_t i = 0; i < draft.conditions.size(); ++i) {
		TimerCondition c = draft.conditions[i];
		const std::array<std::string, 2> text = i < draft.text.size() ? draft.text[i] : std::array<std::string, 2>{};
		if (!valid(c.quantity, why)) {
			return false;
		}
		if (!number_of(text[0], c.comparison.a) || (two_values(c.comparison.op) && !number_of(text[1], c.comparison.b))) {
			why = "a value is not a number";
			return false;
		}
		if (!valid(c.comparison, why)) {
			return false;
		}
		out.conditions.push_back(c);
	}
	out.all = draft.all;
	out.limited = draft.limited;
	if (!out.limited && out.conditions.empty()) {
		why = "a timer without a value condition needs its time limit";
		return false;
	}
	if (out.limited && (!seconds_of(draft.limit_text, out.limit_s) || !(out.limit_s > 0.0))) {
		why = "the time limit is not a time (m:ss)";
		return false;
	}
	return true;
}

bool Timers::arm(const TimerDraft &draft, const LabView &view, std::string &why) {
	if (static_cast<int>(timers_.size()) >= MAX_TIMERS) {
		why = "five timers at most: remove one first";
		return false;
	}
	Timer t;
	if (!read(draft, t, why)) {
		return false;
	}
	timers_.push_back(std::move(t));
	rearm(static_cast<int>(timers_.size()) - 1, view);
	return true;
}

void Timers::rearm(int index, const LabView &view) {
	if (index < 0 || index >= static_cast<int>(timers_.size())) {
		return;
	}
	Timer &t = timers_[static_cast<size_t>(index)];
	t.fired = false;
	t.fired_by.clear();
	t.item_fired.clear();
	t.armed_at_s = now_of(view);
	t.last.clear();
	for (const TimerCondition &c : t.conditions) {
		t.last.push_back(measure(view, c.quantity));
	}
}

void Timers::remove(int index) {
	if (index >= 0 && index < static_cast<int>(timers_.size())) {
		timers_.erase(timers_.begin() + index);
	}
}

bool Timers::armed() const {
	for (const Timer &t : timers_) {
		if (!t.fired) {
			return true;
		}
	}
	return false;
}

int Timers::poll(const LabView &view) {
	const double now = now_of(view);
	int first = -1;
	for (size_t i = 0; i < timers_.size(); ++i) {
		Timer &t = timers_[i];
		if (t.fired) {
			continue;
		}
		// every condition is read, so each keeps its last reading
		int held = 0, first_held = -1;
		std::vector<std::optional<double>> readings;
		for (size_t c = 0; c < t.conditions.size(); ++c) {
			const std::optional<double> value = measure(view, t.conditions[c].quantity);
			const bool holds = value && meets(t.conditions[c].comparison, *value, c < t.last.size() ? t.last[c] : std::nullopt);
			if (holds) {
				++held;
				first_held = first_held < 0 ? static_cast<int>(c) : first_held;
			}
			readings.push_back(value);
		}
		t.last = readings;
		const bool by_values = !t.conditions.empty() && (t.all ? held == static_cast<int>(t.conditions.size()) : held > 0);
		const bool by_time = t.limited && now - t.armed_at_s >= t.limit_s - 1e-9;
		if (first < 0 && (by_values || by_time)) {
			t.fired = true;
			if (by_values) {
				const TimerCondition &c = t.conditions[static_cast<size_t>(first_held)];
				t.fired_by = describe(view, c) + " (now " + shown(c.quantity, *readings[static_cast<size_t>(first_held)]) + ")";
				if (t.all && t.conditions.size() > 1) {
					t.fired_by = "all its conditions hold: " + t.fired_by;
				}
				t.item_fired = c.quantity.item;
			} else {
				t.fired_by = clock_of(t.limit_s) + " passed";
				t.item_fired = t.conditions.empty() ? "" : t.conditions.front().quantity.item;
			}
			first = static_cast<int>(i);
		}
	}
	return first;
}

} // namespace lab
