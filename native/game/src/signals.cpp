#include "lab/signals.hpp"

#include "lab/conditions.hpp"

#include <cmath>

namespace lab {

const char *part_kind_name(PartKind kind) {
	switch (kind) {
		case PartKind::Timer:
			return "timer";
		case PartKind::Monitor:
			return "monitor";
		case PartKind::Alarm:
			return "alarm";
		case PartKind::Circuit:
			return "circuit";
		case PartKind::Notice:
			return "notice";
		case PartKind::Writer:
			return "writer";
	}
	return "?";
}

bool part_kind_of(std::string_view name, PartKind &kind) {
	for (int k = 0; k < PART_KINDS; ++k) {
		if (name == part_kind_name(static_cast<PartKind>(k))) {
			kind = static_cast<PartKind>(k);
			return true;
		}
	}
	return false;
}

bool sends(PartKind kind) {
	return kind != PartKind::Alarm && kind != PartKind::Writer && kind != PartKind::Circuit;
}

namespace {

const Part *part_of(const std::vector<Part> &parts, const std::string &id) {
	for (const Part &p : parts) {
		if (p.id == id) {
			return &p;
		}
	}
	return nullptr;
}

// any input on, and whether there is one
void inputs_of(const std::string &id, const std::vector<Wire> &wires, const std::vector<Part> &before, bool &any_input, bool &any_on) {
	any_input = any_on = false;
	for (const Wire &w : wires) {
		if (w.to != id) {
			continue;
		}
		any_input = true;
		const Part *from = part_of(before, w.from);
		any_on = any_on || (from != nullptr && from->out);
	}
}

} // namespace

bool enabled(const std::string &id, const std::vector<Wire> &wires, const std::vector<Part> &before) {
	bool any_input = false, any_on = false;
	inputs_of(id, wires, before, any_input, any_on);
	return !any_input || any_on;
}

void reset(Part &timer) {
	timer.left = timer.preset;
	timer.own = false;
	timer.out = false;
}

SignalStep evaluate(std::vector<Part> &parts, const std::vector<Wire> &wires, std::vector<Rig> &rigs, const QuantityReader &read, int64_t step, const NoticeReader &noticed) {
	SignalStep out;
	const std::vector<Part> before = parts;
	for (Part &p : parts) {
		bool any_input = false, any_on = false;
		inputs_of(p.id, wires, before, any_input, any_on);
		const bool on = !any_input || any_on;
		switch (p.kind) {
			case PartKind::Monitor: {
				std::optional<double> value;
				if (p.watching) {
					value = read(p.quantity);
				}
				p.own = value.has_value() && meets(p.comparison, *value, p.last);
				p.last = value;
				p.out = p.own && on;
				break;
			}
			case PartKind::Timer:
				if (p.left > 0) {
					--p.left;
				}
				p.own = p.left == 0;
				p.out = p.own && on;
				break;
			case PartKind::Alarm: {
				const bool was = p.own;
				p.own = any_on; // an alarm with no input never rings
				if (p.own && !was) {
					p.ringing = true;
					p.rang_at = step;
					out.rang.push_back(p.id);
				}
				p.out = false;
				break;
			}
			case PartKind::Circuit:
				p.own = p.out = false; // what is in it signals
				break;
			case PartKind::Notice:
				p.own = noticed && noticed(p.notice_level, p.quantity.item);
				p.out = p.own && on;
				break;
			case PartKind::Writer: {
				const bool was = p.own;
				p.own = any_on; // a writer with no input never writes
				if (p.own && !was) {
					out.written.push_back(p.id);
				}
				p.out = false;
				break;
			}
		}
	}
	// a rig is an input too: it reads its source's signal as it was at the end of the step before
	for (size_t r = 0; r < rigs.size(); ++r) {
		Rig &rig = rigs[r];
		const Part *source = part_of(before, rig.from);
		const bool now = source != nullptr && source->out;
		if (now == rig.seen) {
			continue;
		}
		rig.seen = now;
		const std::optional<double> setting = rig.change ? rig.any : now ? rig.on : rig.off;
		if (setting) {
			out.actions.push_back(RigAction{ r, *setting });
		}
	}
	return out;
}

std::vector<std::string> check_G2(const std::vector<Part> &parts, const std::vector<Wire> &wires, const std::vector<Part> &before) {
	std::vector<std::string> broken;
	for (const Part &p : parts) {
		const bool expected = sends(p.kind) && p.own && enabled(p.id, wires, before);
		if (p.out != expected) {
			broken.push_back("G2: the signal of \"" + p.id + "\" is not its own condition while its inputs let it");
		}
	}
	return broken;
}

std::vector<std::string> check_G1(const std::vector<ControlSetting> &settings, const std::function<std::optional<double>(const std::string &, const std::string &)> &now) {
	std::vector<std::string> broken;
	for (const ControlSetting &s : settings) {
		if (s.control == "reset") {
			continue; // an event, not a state
		}
		const std::optional<double> v = now(s.item, s.control);
		if (v && std::fabs(*v - s.value) > 1e-12) {
			broken.push_back("G1: the " + s.control + " of \"" + s.item + "\" is not as " + s.by + " last set it");
		}
	}
	return broken;
}

} // namespace lab
