// Signals (GAME_SPEC §13.3): timers, monitors and alarms, wired to each other
// and rigged to the lab's controls.
#include "lab/lab.hpp"
#include "lab/signals.hpp"

#include <doctest/doctest.h>
#ifdef __APPLE__
#include <libproc.h>
#include <sys/resource.h>
#include <unistd.h>
#endif

#include <algorithm>
#include <chrono>
#include <ctime>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

using lab::Comparison;
using lab::Part;
using lab::PartKind;
using lab::Quantity;
using lab::Rig;
using lab::Wire;

namespace {

std::string read(const std::string &relative) {
	std::ifstream in(std::string(CHEM_REPO_ROOT) + "/" + relative);
	REQUIRE_MESSAGE(static_cast<bool>(in), relative);
	std::ostringstream out;
	out << in.rdbuf();
	return out.str();
}

Part timer(const char *id, int64_t steps) {
	Part p;
	p.id = id;
	p.kind = PartKind::Timer;
	p.preset = p.left = steps;
	return p;
}

Part monitor(const char *id, Comparison::Op op, double a, double b = 0.0) {
	Part p;
	p.id = id;
	p.kind = PartKind::Monitor;
	p.watching = true;
	p.quantity = Quantity{ "flask", "celsius", "g" };
	p.comparison = Comparison{ op, a, b };
	return p;
}

Part alarm(const char *id) {
	Part p;
	p.id = id;
	p.kind = PartKind::Alarm;
	return p;
}

const Part &of(const std::vector<Part> &parts, const std::string &id) {
	for (const Part &p : parts) {
		if (p.id == id) {
			return p;
		}
	}
	FAIL("no part " << id);
	return parts.front();
}

// Signals alone, over steps: a reading per step, and the rigs' resets applied as the lab applies them.
struct Board {
	std::vector<Part> parts;
	std::vector<Wire> wires;
	std::vector<Rig> rigs;
	double value = 0.0; // what every monitor reads
	int64_t step = 0;
	std::vector<lab::RigAction> acted;
	std::vector<std::string> rang;

	void run(int64_t steps = 1) {
		for (int64_t i = 0; i < steps; ++i) {
			++step;
			const std::vector<Part> before = parts;
			const lab::SignalStep s = lab::evaluate(parts, wires, rigs, [this](const Quantity &) { return std::optional<double>(value); }, step);
			REQUIRE(lab::check_G2(parts, wires, before).empty());
			for (const lab::RigAction &a : s.actions) {
				acted.push_back(a);
				const Rig &r = rigs[a.rig];
				if (r.control == "reset") {
					for (Part &p : parts) {
						if (p.id == r.item) {
							lab::reset(p);
						}
					}
				}
			}
			rang.insert(rang.end(), s.rang.begin(), s.rang.end());
		}
	}
	bool out(const char *id) const { return of(parts, id).out; }
};

std::unique_ptr<lab::Lab> bench(const char *file = "data/benches/demo.json") {
	std::string why;
	auto made = lab::Lab::open(read(file), why);
	REQUIRE_MESSAGE(made != nullptr, why);
	return made;
}

void must(lab::Lab &l, const std::string &command) {
	std::string why;
	REQUIRE_MESSAGE(l.command(command, why), command << ": " << why);
}

std::string refused(lab::Lab &l, const std::string &command) {
	std::string why;
	const std::string before = l.digest();
	CHECK_MESSAGE(!l.command(command, why), command);
	CHECK(l.digest() == before);
	return why;
}

const lab::ItemView &item(const lab::LabView &v, const std::string &id) {
	for (const lab::ItemView &i : v.items) {
		if (i.id == id) {
			return i;
		}
	}
	FAIL("no item " << id);
	return v.items.front();
}

} // namespace

TEST_CASE("signals: a timer counts down a step at a time and stays on at zero until reset (Stage 13)") {
	Board b;
	b.parts = { timer("t", 3) };
	b.run(2);
	CHECK(of(b.parts, "t").left == 1);
	CHECK_FALSE(b.out("t"));
	b.run();
	CHECK(of(b.parts, "t").left == 0);
	CHECK(b.out("t"));
	b.run(100);
	CHECK(b.out("t")); // latched
	lab::reset(b.parts[0]);
	CHECK(of(b.parts, "t").left == 3);
	CHECK_FALSE(b.out("t"));
	b.run(3);
	CHECK(b.out("t"));
}

TEST_CASE("signals: a monitor compares its quantity every step; reaches is a crossing (Stage 13)") {
	Board b;
	b.parts = { monitor("below", Comparison::Below, 10), monitor("between", Comparison::Between, 5, 10), monitor("reaches", Comparison::Reaches, 50) };
	b.value = 20;
	b.run();
	CHECK_FALSE(b.out("below"));
	CHECK_FALSE(b.out("reaches"));
	b.value = 9;
	b.run();
	CHECK(b.out("below"));
	CHECK(b.out("between"));
	b.value = 49;
	b.run();
	CHECK_FALSE(b.out("reaches"));
	b.value = 51; // crossed between two readings
	b.run();
	CHECK(b.out("reaches"));
	b.run(); // and only then
	CHECK_FALSE(b.out("reaches"));
	// a monitor not yet set signals nothing
	Part unset;
	unset.id = "unset";
	unset.kind = PartKind::Monitor;
	b.parts.push_back(unset);
	b.run();
	CHECK_FALSE(b.out("unset"));
}

TEST_CASE("signals: inputs in parallel are OR, a chain is AND, and a part with none is free (Stage 13)") {
	// a and b feed m; m's own condition always holds
	for (const bool a_on : { false, true }) {
		for (const bool b_on : { false, true }) {
			Board k;
			k.parts = { timer("a", a_on ? 1 : 1000), timer("b", b_on ? 1 : 1000), monitor("m", Comparison::AtLeast, 0) };
			k.wires = { Wire{ "a", "m" }, Wire{ "b", "m" } };
			k.run(3);
			CHECK_MESSAGE(k.out("m") == (a_on || b_on), a_on << b_on); // parallel: OR
		}
	}
	Board chain;
	chain.parts = { timer("a", 1), monitor("m", Comparison::Below, 10) }; // a chain: a, then m
	chain.wires = { Wire{ "a", "m" } };
	chain.value = 20;
	chain.run(5);
	CHECK_FALSE(chain.out("m")); // a is on, m's own condition is not: AND
	chain.value = 5;
	chain.run();
	CHECK(chain.out("m"));
	Board free;
	free.parts = { monitor("m", Comparison::Below, 10) };
	free.value = 5;
	free.run();
	CHECK(free.out("m")); // no inputs: it signals freely
}

TEST_CASE("signals: synchronous: a chain of N answers in N steps, whatever order its parts were made in (Stage 13)") {
	// a timer at zero, then four monitors that always hold, each wired from the last
	const std::vector<std::string> names = { "t", "m1", "m2", "m3", "m4" };
	std::vector<Part> made = { timer("t", 1), monitor("m1", Comparison::AtLeast, 0), monitor("m2", Comparison::AtLeast, 0), monitor("m3", Comparison::AtLeast, 0),
		monitor("m4", Comparison::AtLeast, 0) };
	std::vector<Wire> wires;
	for (size_t i = 1; i < names.size(); ++i) {
		wires.push_back(Wire{ names[i - 1], names[i] });
	}
	std::map<std::string, std::vector<bool>> first;
	for (const bool reversed : { false, true }) {
		Board b;
		b.parts = made;
		if (reversed) {
			std::reverse(b.parts.begin(), b.parts.end());
		}
		b.wires = wires;
		std::map<std::string, std::vector<bool>> seen;
		for (int s = 0; s < 6; ++s) {
			b.run();
			for (const std::string &n : names) {
				seen[n].push_back(of(b.parts, n).out);
			}
		}
		CHECK(seen["t"] == std::vector<bool>{ true, true, true, true, true, true });
		CHECK(seen["m4"] == std::vector<bool>{ false, false, false, false, true, true }); // the fifth step: four links after the timer
		if (reversed) {
			CHECK(seen == first);
		} else {
			first = seen;
		}
	}
}

TEST_CASE("signals: loops are allowed: two timers set each other off, a timer resets itself (Stage 13)") {
	Board b;
	// A (10 s) and B (5 s): A's turning on resets B, B's resets A
	b.parts = { timer("A", 600), timer("B", 300) };
	Rig ab;
	ab.from = "A";
	ab.item = "B";
	ab.control = "reset";
	ab.on = 1.0;
	Rig ba = ab;
	ba.from = "B";
	ba.item = "A";
	b.rigs = { ab, ba };
	// over an hour of lab time, every period of A is 10 s off and 5 s on, to the step
	std::vector<int64_t> rises, falls;
	bool was = false;
	for (int64_t s = 0; s < 60 * 3600; ++s) {
		b.run();
		const bool now = b.out("A");
		if (now && !was) {
			rises.push_back(b.step);
		}
		if (!now && was) {
			falls.push_back(b.step);
		}
		was = now;
	}
	REQUIRE(rises.size() > 200);
	for (size_t i = 1; i < rises.size(); ++i) {
		CHECK(rises[i] - rises[i - 1] == 902); // a period of 15 s, and a step for each of its two links
		CHECK(falls[i - 1] - rises[i - 1] == 302); // on for 5 s, and its two links
	}
	// a timer rigged to its own reset: a pulse of one step every preset and one
	Board p;
	p.parts = { timer("T", 60) };
	Rig self;
	self.from = "T";
	self.item = "T";
	self.control = "reset";
	self.on = 1.0;
	p.rigs = { self };
	int pulses = 0;
	for (int s = 0; s < 61 * 10; ++s) {
		p.run();
		pulses += p.out("T") ? 1 : 0;
	}
	CHECK(pulses == 10); // on for one step: the rig reads it at the next, and resets it
	CHECK(p.acted.size() == 10); // every 61 steps
}

TEST_CASE("signals: an alarm rings on its input turning on, and stays ringing until answered (Stage 13)") {
	Board b;
	b.parts = { monitor("m", Comparison::Below, 10), alarm("a"), alarm("lonely") };
	b.wires = { Wire{ "m", "a" } };
	b.value = 20;
	b.run(3);
	CHECK_FALSE(of(b.parts, "a").ringing);
	b.value = 5;
	b.run(); // m turns on at this step's end; the alarm reads it at the next
	CHECK_FALSE(of(b.parts, "a").ringing);
	b.run();
	CHECK(of(b.parts, "a").ringing);
	CHECK(b.rang == std::vector<std::string>{ "a" });
	b.parts[1].ringing = false; // answered
	b.run(10);
	CHECK_FALSE(of(b.parts, "a").ringing); // the input stayed on: no new ring
	b.value = 20;
	b.run(3);
	b.value = 5;
	b.run(2);
	CHECK(of(b.parts, "a").ringing); // on again: rings again
	CHECK_FALSE(of(b.parts, "lonely").ringing); // an alarm with no input never rings
}

TEST_CASE("signals: rigs set a control on events, and the last event wins (Stage 13)") {
	auto l = bench();
	must(*l, R"({"do": "heat", "burner": "burner", "vessel": "flask"})");
	must(*l, R"({"do": "part", "kind": "timer", "at": [0, 0]})");
	must(*l, R"({"do": "part", "kind": "timer", "at": [1, 0]})");
	must(*l, R"({"do": "set", "part": "timer1", "seconds": 1})");
	must(*l, R"({"do": "set", "part": "timer2", "seconds": 2})");
	// timer1 on: valve 0.6; off: leave. timer2 on: valve 0.3
	must(*l, R"({"do": "rig", "from": "timer1", "item": "burner", "control": "valve", "on": 0.6})");
	must(*l, R"({"do": "rig", "from": "timer2", "item": "burner", "control": "valve", "on": 0.3, "off": null})");
	must(*l, R"({"do": "rig", "from": "timer1", "item": "flask", "control": "stir", "change": true})");
	CHECK(refused(*l, R"({"do": "rig", "from": "timer1", "item": "burner", "control": "valve", "on": 0.5})").find("already") != std::string::npos);
	CHECK(refused(*l, R"({"do": "rig", "from": "timer2", "item": "flask", "control": "stir", "change": true, "on": true})").find("not both") != std::string::npos);
	CHECK(refused(*l, R"({"do": "rig", "from": "timer2", "item": "flask", "control": "valve", "on": 0.5})").find("no valve") != std::string::npos);
	CHECK(refused(*l, R"({"do": "rig", "from": "timer2", "item": "burner", "control": "stir", "on": true})").find("no stir") != std::string::npos);
	CHECK(refused(*l, R"({"do": "rig", "from": "timer2", "item": "jar", "control": "stir", "on": 3})").find("true (stir)") != std::string::npos);
	std::string why;
	const auto valve_by = [&]() {
		const lab::LabView v = l->view();
		const auto setting = std::find_if(v.controls.begin(), v.controls.end(), [](const lab::ControlSetting &c) { return c.item == "burner" && c.control == "valve"; });
		REQUIRE(setting != v.controls.end());
		return *setting;
	};
	REQUIRE(l->advance(60, why)); // timer1 at zero at the end of step 60
	CHECK(item(l->view(), "burner").valve == 0.0);
	REQUIRE(l->advance(1, why)); // its rigs read it a step later
	lab::LabView v = l->view();
	CHECK(item(v, "burner").valve == 0.6); // timer1 on
	CHECK(item(v, "flask").stirred); // its change: stir
	CHECK(valve_by().by == "timer1");
	CHECK(valve_by().step == 61);
	must(*l, R"({"do": "valve", "burner": "burner", "to": 0.9})"); // the player: another input
	CHECK(valve_by().by == "player");
	REQUIRE(l->advance(59, why)); // step 120: timer2 at zero
	CHECK(item(l->view(), "burner").valve == 0.9); // the player's, until the next event
	REQUIRE(l->advance(1, why));
	CHECK(item(l->view(), "burner").valve == 0.3); // timer2 on: the next event wins over the player
	CHECK(valve_by().by == "timer2");
	CHECK(valve_by().step == 121);
	// both turning on in the same step: the newest rig acts last, and wins
	must(*l, R"({"do": "reset", "timer": "timer1"})");
	must(*l, R"({"do": "set", "part": "timer2", "seconds": 1})"); // set: a reset too
	must(*l, R"({"do": "valve", "burner": "burner", "to": 0.9})");
	REQUIRE(l->advance(61, why));
	CHECK(item(l->view(), "burner").valve == 0.3);
	CHECK(valve_by().by == "timer2");
	CHECK(item(l->view(), "flask").stirred); // its change, both ways: stir
	// every rig stays: none was ever taken off
	CHECK(l->view().rigs.size() == 3);
	must(*l, R"({"do": "unrig", "from": "timer1", "item": "flask", "control": "stir"})");
	CHECK(l->view().rigs.size() == 2);
}

TEST_CASE("signals: parts are made from the drawer, wired, set and scrapped, and refused with the reason (Stage 13)") {
	auto l = bench();
	const std::string none = l->digest();
	must(*l, R"({"do": "part", "kind": "monitor", "at": [0, 0]})");
	must(*l, R"({"do": "part", "kind": "alarm", "at": [1, 0]})");
	must(*l, R"({"do": "part", "kind": "timer", "at": [2, 0], "id": "egg"})");
	CHECK(refused(*l, R"({"do": "part", "kind": "monitor", "at": [0, 0]})").find("\"monitor1\" stands there") != std::string::npos);
	CHECK(refused(*l, R"({"do": "part", "kind": "monitor", "at": [9, 9]})").find("\"flask\" stands there") != std::string::npos);
	CHECK(refused(*l, R"({"do": "part", "kind": "monitor", "at": [28, 0]})").find("off the bench") != std::string::npos);
	CHECK(refused(*l, R"({"do": "part", "kind": "kettle", "at": [5, 0]})").find("timer, monitor, alarm, notice or writer") != std::string::npos);
	CHECK(refused(*l, R"({"do": "part", "kind": "timer", "at": [5, 0], "id": "flask"})").find("taken") != std::string::npos);
	CHECK(refused(*l, R"({"do": "place", "item": "flask", "at": [0, 0]})").find("monitor1") != std::string::npos);
	must(*l, R"({"do": "wire", "from": "monitor1", "to": "alarm1"})");
	must(*l, R"({"do": "wire", "from": "monitor1", "to": "monitor1"})"); // a loop on itself is well defined
	CHECK(refused(*l, R"({"do": "wire", "from": "monitor1", "to": "alarm1"})").find("already") != std::string::npos);
	CHECK(refused(*l, R"({"do": "wire", "from": "alarm1", "to": "monitor1"})").find("no output") != std::string::npos);
	CHECK(refused(*l, R"({"do": "rig", "from": "alarm1", "item": "flask", "control": "stir", "on": true})").find("no output") != std::string::npos);
	must(*l, R"({"do": "unwire", "from": "monitor1", "to": "monitor1"})");
	must(*l, R"({"do": "set", "part": "monitor1", "item": "flask", "what": "celsius", "compare": "above", "value": 25})");
	must(*l, R"({"do": "set", "part": "monitor1", "item": "flask", "what": "copper_sulfate_pentahydrate", "measure": "purity", "compare": "between", "values": [99, 100]})");
	CHECK(refused(*l, R"({"do": "set", "part": "monitor1", "item": "kettle", "what": "celsius", "compare": "above", "value": 25})").find("no \"kettle\"") != std::string::npos);
	CHECK(refused(*l, R"({"do": "set", "part": "monitor1", "item": "flask", "what": "gold", "compare": "above", "value": 25})").find("gold") != std::string::npos);
	CHECK(refused(*l, R"({"do": "set", "part": "monitor1", "item": "flask", "what": "celsius", "compare": "between", "value": 25})").find("[low, high]") != std::string::npos);
	CHECK(refused(*l, R"({"do": "set", "part": "monitor1", "item": "flask", "what": "celsius", "compare": "between", "values": [30, 25]})").find("low end") != std::string::npos);
	CHECK(refused(*l, R"({"do": "set", "part": "egg", "seconds": 0})").find("seconds") != std::string::npos);
	CHECK(refused(*l, R"({"do": "set", "part": "alarm1", "seconds": 3})").find("nothing to set") != std::string::npos);
	must(*l, R"({"do": "set", "part": "egg", "seconds": 180})");
	CHECK(item(l->view(), "egg").part.preset == 180 * 60);
	must(*l, R"({"do": "move", "items": ["monitor1", "alarm1", "egg", "burner"], "by": [1, 0]})"); // with equipment, together
	CHECK(item(l->view(), "egg").col == 3);
	CHECK(item(l->view(), "burner").col == 21);
	must(*l, R"({"do": "place", "item": "egg", "at": [27, 0]})");
	must(*l, R"({"do": "rig", "from": "monitor1", "item": "egg", "control": "reset", "on": true})");
	// scrapped: its wires and rigs go with it, and so does a rig to its reset
	must(*l, R"({"do": "scrap", "part": "monitor1"})");
	CHECK(l->view().wires.empty());
	CHECK(l->view().rigs.empty());
	must(*l, R"({"do": "stow", "item": "alarm1"})"); // a part stowed goes back to the drawer
	must(*l, R"({"do": "scrap", "part": "egg"})");
	CHECK(l->view().items.size() == 9);
	CHECK(l->digest() == none); // no parts: the digest is the world's alone
	must(*l, R"({"do": "part", "kind": "monitor", "at": [0, 0]})");
	CHECK(item(l->view(), "monitor2").kind == "monitor"); // names are not used again
}

TEST_CASE("signals: a monitor rigged to the valve closes the burner at 10 g of water, and an alarm stops the clock there (Stage 13)") {
	auto l = bench();
	for (const char *c : { R"({"do": "pour", "from": "acid", "to": "flask", "ml": 60})", R"({"do": "heat", "burner": "burner", "vessel": "flask"})",
				 R"({"do": "part", "kind": "monitor", "at": [0, 0]})", R"({"do": "part", "kind": "alarm", "at": [1, 0]})",
				 R"({"do": "set", "part": "monitor1", "item": "flask", "what": "water", "compare": "below", "value": 10})",
				 R"({"do": "rig", "from": "monitor1", "item": "burner", "control": "valve", "on": 0, "off": null})", R"({"do": "wire", "from": "monitor1", "to": "alarm1"})",
				 R"({"do": "valve", "burner": "burner", "to": 1})" }) {
		must(*l, c);
	}
	l->set_checking(false);
	std::string why;
	int64_t frames = 0;
	// at 600x: 600 steps a frame, stopping where the alarm rings
	while (l->view().ringing.empty()) {
		REQUIRE_MESSAGE(++frames < 2000, "the alarm never rang");
		REQUIRE(l->advance(600, why, true));
	}
	const lab::LabView v = l->view();
	CHECK(item(v, "burner").valve == 0.0); // closed by the rig the step the water went below 10 g
	double water = 0.0;
	for (const lab::SubstanceView &s : item(v, "flask").contents) {
		water += s.species == "water" ? s.g : 0.0;
	}
	CHECK(water < 10.0);
	CHECK(water > 9.9);
	CHECK(v.ringing == std::vector<std::string>{ "alarm1" });
	CHECK(item(v, "alarm1").part.rang_at == v.step); // it stopped in the step it rang
	must(*l, R"({"do": "ack"})");
	CHECK(l->view().ringing.empty());
	MESSAGE("the burner closed at " << water << " g of water, " << v.step / 60.0 << " s in");
}

TEST_CASE("signals: a lab with signals saves and loads, and goes on digest for digest; a wait can wait for a ring (Stage 13)") {
	auto l = bench();
	for (const char *c : { R"({"do": "heat", "burner": "burner", "vessel": "flask"})", R"({"do": "valve", "burner": "burner", "to": 0.5})",
				 R"({"do": "part", "kind": "timer", "at": [0, 0]})", R"({"do": "set", "part": "timer1", "seconds": 7.5})",
				 R"({"do": "part", "kind": "monitor", "at": [1, 0]})", R"({"do": "set", "part": "monitor1", "item": "flask", "what": "celsius", "compare": "reaches", "value": 23})",
				 R"({"do": "part", "kind": "alarm", "at": [2, 0]})", R"({"do": "wire", "from": "monitor1", "to": "alarm1"})", R"({"do": "wire", "from": "timer1", "to": "monitor1"})",
				 R"({"do": "rig", "from": "timer1", "item": "flask", "control": "stir", "on": true, "off": false})",
				 R"({"do": "rig", "from": "monitor1", "item": "timer1", "control": "reset", "change": true})" }) {
		must(*l, c);
	}
	std::string why;
	REQUIRE(l->advance(200, why)); // mid-countdown
	auto again = lab::Lab::load(l->save(), why);
	REQUIRE_MESSAGE(again != nullptr, why);
	CHECK(again->digest() == l->digest());
	for (int i = 0; i < 40; ++i) {
		REQUIRE(l->advance(30, why));
		REQUIRE(again->advance(30, why));
		REQUIRE(again->digest() == l->digest());
	}
	CHECK(again->view().controls == l->view().controls);
	// a session waits for an alarm
	auto w = bench();
	for (const char *c : { R"({"do": "part", "kind": "timer", "at": [0, 0]})", R"({"do": "set", "part": "timer1", "seconds": 2})", R"({"do": "part", "kind": "alarm", "at": [1, 0]})",
				 R"({"do": "wire", "from": "timer1", "to": "alarm1"})" }) {
		must(*w, c);
	}
	CHECK_FALSE(w->command(R"({"do": "wait_until", "rings": "alarm1", "timeout": 1})", why));
	CHECK(why.find("did not ring") != std::string::npos);
	must(*w, R"({"do": "wait_until", "rings": "alarm1", "timeout": 10})");
	CHECK(w->step() == 121); // the timer at zero after 120 steps, the alarm reads it one step later
}

TEST_CASE("signals: G1 and G2 fire on a corrupted state (Stage 13)") {
	// G2: a signal that is not its own condition while its inputs let it
	std::vector<Part> parts = { timer("t", 1), monitor("m", Comparison::AtLeast, 0) };
	const std::vector<Wire> wires = { Wire{ "t", "m" } };
	std::vector<Part> before = parts;
	CHECK(lab::check_G2(parts, wires, before).empty());
	parts[1].own = true;
	parts[1].out = true; // its input, the timer, was off before
	CHECK_FALSE(lab::check_G2(parts, wires, before).empty());
	parts[1] = before[1];
	parts[0].own = true; // at zero, but not signalling
	CHECK_FALSE(lab::check_G2(parts, wires, before).empty());
	Part a = alarm("a");
	a.out = true; // an alarm sends nothing
	CHECK_FALSE(lab::check_G2({ a }, {}, { a }).empty());
	// G1: a control not as its last setting left it
	const std::vector<lab::ControlSetting> settings = { { "burner", "valve", 0.5, "player", 10 }, { "t", "reset", 1.0, "m", 12 } };
	CHECK(lab::check_G1(settings, [](const std::string &, const std::string &) { return std::optional<double>(0.5); }).empty());
	CHECK_FALSE(lab::check_G1(settings, [](const std::string &, const std::string &) { return std::optional<double>(0.4); }).empty());
}

// (Stage 74) the work a run does, as the instructions the process retired: the same on a fast core and a slow one,
// under any load (the thread's own time, Stage 13 to 73, was a core's speed too: a gate's crowded machine moved the
// thread to its efficiency cores, two to three times slower, and three gates in five failed); elsewhere, the thread's
// own time
static double work_done() {
#ifdef __APPLE__
	rusage_info_v4 r{};
	if (proc_pid_rusage(getpid(), RUSAGE_INFO_V4, reinterpret_cast<rusage_info_t *>(&r)) == 0 && r.ri_instructions > 0) {
		return static_cast<double>(r.ri_instructions);
	}
#endif
	timespec t{};
	clock_gettime(CLOCK_THREAD_CPUTIME_ID, &t);
	return static_cast<double>(t.tv_sec) * 1e9 + static_cast<double>(t.tv_nsec);
}

TEST_CASE("signals: three monitors cost little at 600x (Stage 13)") {
	struct Cost {
		double work = 0.0, ms = 0.0; // a frame's at 600x: 600 steps
	};
	const auto cost = [](bool with) {
		auto l = bench();
		must(*l, R"({"do": "pour", "from": "acid", "to": "flask", "ml": 60})");
		if (with) {
			for (const char *c : { R"({"do": "part", "kind": "monitor", "at": [0, 0]})", R"({"do": "part", "kind": "monitor", "at": [1, 0]})",
						 R"({"do": "part", "kind": "monitor", "at": [2, 0]})", R"({"do": "set", "part": "monitor1", "item": "flask", "what": "water", "compare": "below", "value": 1})",
						 R"({"do": "set", "part": "monitor2", "item": "flask", "what": "celsius", "compare": "above", "value": 500})",
						 R"({"do": "set", "part": "monitor3", "item": "flask", "what": "copper_sulfate_aq", "measure": "purity", "compare": "above", "value": 99})" }) {
				must(*l, c);
			}
		}
		l->set_checking(false);
		std::string why;
		const auto thread_ms = [] {
			timespec t{};
			clock_gettime(CLOCK_THREAD_CPUTIME_ID, &t);
			return static_cast<double>(t.tv_sec) * 1e3 + static_cast<double>(t.tv_nsec) * 1e-6;
		};
		const double start = thread_ms(), done = work_done();
		REQUIRE(l->advance(6000, why));
		return Cost{ (work_done() - done) / 10.0, (thread_ms() - start) / 10.0 };
	};
	cost(true); // warm
	Cost without{ 1e300, 1e300 }, with{ 1e300, 1e300 };
	for (int k = 0; k < 3; ++k) {
		const Cost a = cost(false), b = cost(true);
		without = Cost{ std::min(without.work, a.work), std::min(without.ms, a.ms) };
		with = Cost{ std::min(with.work, b.work), std::min(with.ms, b.ms) };
	}
	MESSAGE("a frame at 600x: " << without.ms << " ms, with three monitors " << with.ms << " ms; their work " << 100.0 * (with.work / without.work - 1.0)
					<< " % of the frame's");
	// three monitors add under a tenth to a frame's work (Stage 74: 5.9 %; Stage 13's bound, 3 ms of a frame of 30, by
	// the work and not the clock). A sanitizer's build weighs the monitors' code otherwise (9.6 %): reported there
#ifndef CHEM_SANITIZED
	CHECK(with.work / without.work - 1.0 < 0.10);
#endif
}

TEST_CASE("signals: a circuit holds parts as they lie, and gives them back; what is in it signals as it would out of it (Stage 14)") {
	const auto made = [](bool collapse) {
		auto l = bench();
		for (const char *c : { R"({"do": "heat", "burner": "burner", "vessel": "flask"})", R"({"do": "valve", "burner": "burner", "to": 0.4})",
					 R"({"do": "part", "kind": "timer", "at": [3, 1]})", R"({"do": "set", "part": "timer1", "seconds": 2})",
					 R"({"do": "part", "kind": "monitor", "at": [5, 2]})", R"({"do": "set", "part": "monitor1", "item": "flask", "what": "celsius", "compare": "above", "value": 30})",
					 R"({"do": "part", "kind": "alarm", "at": [6, 3]})", R"({"do": "wire", "from": "timer1", "to": "monitor1"})", R"({"do": "wire", "from": "monitor1", "to": "alarm1"})",
					 R"({"do": "rig", "from": "monitor1", "item": "burner", "control": "valve", "on": 0.1})" }) {
			must(*l, c);
		}
		if (collapse) {
			must(*l, R"({"do": "circuit", "parts": ["timer1", "monitor1"]})");
		}
		return l;
	};
	auto loose = made(false), kept = made(true);
	lab::LabView v = kept->view();
	CHECK(item(v, "circuit1").kind == "circuit");
	CHECK(item(v, "circuit1").col == 3); // the free square nearest their top-left: the timer's, freed
	CHECK(item(v, "circuit1").row == 1);
	CHECK(item(v, "circuit1").part.inner_w == 6); // at least 6 x 4 inside
	CHECK(item(v, "circuit1").part.inner_h == 4);
	CHECK(item(v, "timer1").place == "circuit");
	CHECK(item(v, "timer1").part.in == "circuit1");
	CHECK(item(v, "monitor1").col == 2); // as they lay: two right, one down of the timer
	CHECK(item(v, "monitor1").row == 1);
	CHECK(kept->digest() == loose->digest()); // where parts are kept is not the world
	std::string why;
	for (int i = 0; i < 20; ++i) { // (Stage 53: 40 s, its flame's heat into the flask through its floor's glass)
		REQUIRE(loose->advance(120, why));
		REQUIRE(kept->advance(120, why));
		REQUIRE(kept->digest() == loose->digest());
	}
	CHECK(item(kept->view(), "alarm1").part.ringing); // through the circuit's monitor
	CHECK(item(kept->view(), "burner").valve == 0.1);
	// the bench sees the circuit's square only: the timer's old square is the circuit's, the monitor's free
	must(*kept, R"({"do": "part", "kind": "alarm", "at": [5, 2]})");
	CHECK(refused(*kept, R"({"do": "part", "kind": "alarm", "at": [3, 1]})").find("\"circuit1\" stands there") != std::string::npos);
	// inside it: made, placed, refused
	must(*kept, R"({"do": "part", "kind": "timer", "at": [5, 3], "in": "circuit1"})");
	CHECK(item(kept->view(), "timer2").part.in == "circuit1");
	CHECK(refused(*kept, R"({"do": "part", "kind": "timer", "at": [6, 0], "in": "circuit1"})").find("off the circuit's grid") != std::string::npos);
	CHECK(refused(*kept, R"({"do": "place", "item": "timer2", "at": [2, 1]})").find("\"monitor1\" stands there") != std::string::npos);
	must(*kept, R"({"do": "place", "item": "timer2", "at": [1, 3]})");
	CHECK(refused(*kept, R"({"do": "move", "items": ["timer1"], "by": [1, 0]})").find("moves with it") != std::string::npos);
	CHECK(refused(*kept, R"({"do": "wire", "from": "timer1", "to": "circuit1"})").find("open it") != std::string::npos);
	CHECK(refused(*kept, R"({"do": "rig", "from": "circuit1", "item": "flask", "control": "stir", "on": true})").find("open it") != std::string::npos);
	CHECK(refused(*kept, R"({"do": "part", "kind": "circuit", "at": [9, 0]})").find("made of parts") != std::string::npos);
	CHECK(refused(*kept, R"({"do": "circuit", "parts": ["timer2"]})").find("on the bench") != std::string::npos);
	// moved as one square; saved and loaded with all it holds
	must(*kept, R"({"do": "move", "items": ["circuit1"], "by": [0, 3]})");
	auto again = lab::Lab::load(kept->save(), why);
	REQUIRE_MESSAGE(again != nullptr, why);
	CHECK(again->view().items.size() == kept->view().items.size());
	CHECK(item(again->view(), "circuit1").part.inner_w == 6);
	CHECK(item(again->view(), "timer2").part.in == "circuit1");
	CHECK(again->digest() == kept->digest());
	// given back: from its square, as they lay in it
	must(*kept, R"({"do": "uncircuit", "circuit": "circuit1"})");
	v = kept->view();
	CHECK(item(v, "timer1").place == "bench");
	CHECK(item(v, "timer1").col == 3);
	CHECK(item(v, "timer1").row == 4);
	CHECK(item(v, "monitor1").col == 5);
	CHECK(item(v, "timer2").row == 7);
	CHECK(std::none_of(v.items.begin(), v.items.end(), [](const lab::ItemView &i) { return i.kind == "circuit"; }));
}

TEST_CASE("signals: circuits in circuits, scrapped with all they hold, and given back only where there is room (Stage 14)") {
	std::string why;
	auto l = lab::Lab::open(R"({"format": "chem-bench/2", "grid": [4, 1], "room": {"celsius": 20}, "items": []})", why);
	REQUIRE_MESSAGE(l != nullptr, why);
	for (const char *c : { R"({"do": "part", "kind": "timer", "at": [0, 0]})", R"({"do": "part", "kind": "alarm", "at": [1, 0]})", R"({"do": "wire", "from": "timer1", "to": "alarm1"})",
				 R"({"do": "circuit", "parts": ["timer1", "alarm1"]})", R"({"do": "part", "kind": "monitor", "at": [1, 0]})", R"({"do": "circuit", "parts": ["circuit1", "monitor1"]})" }) {
		must(*l, c);
	}
	lab::LabView v = l->view();
	CHECK(item(v, "circuit2").col == 0);
	CHECK(item(v, "circuit1").part.in == "circuit2");
	CHECK(item(v, "timer1").part.in == "circuit1");
	CHECK(refused(*l, R"({"do": "uncircuit", "circuit": "circuit1"})").find("open that one first") != std::string::npos);
	must(*l, R"({"do": "uncircuit", "circuit": "circuit2"})"); // the inner circuit back on the bench, a circuit still
	CHECK(item(l->view(), "circuit1").place == "bench");
	// two squares in a row are needed for the timer and the alarm: none is left
	must(*l, R"({"do": "part", "kind": "monitor", "at": [2, 0]})");
	CHECK(refused(*l, R"({"do": "uncircuit", "circuit": "circuit1"})").find("no room") != std::string::npos);
	CHECK(item(l->view(), "timer1").part.in == "circuit1");
	// scrapped with all it holds, wires too
	must(*l, R"({"do": "scrap", "part": "circuit1"})");
	v = l->view();
	CHECK(v.wires.empty());
	CHECK(std::none_of(v.items.begin(), v.items.end(), [](const lab::ItemView &i) { return i.id == "timer1" || i.id == "alarm1" || i.id == "circuit1"; }));
	CHECK(v.items.size() == 3); // the tray, and the two monitors
}
