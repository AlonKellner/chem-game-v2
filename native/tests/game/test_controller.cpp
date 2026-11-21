// Controllers (GAME_SPEC §13.3, Stage 26): a controller senses a quantity and holds it at its target, by a
// level its rigs follow; it tunes itself by trying what it controls on a copy of the lab.
#include "lab/lab.hpp"
#include "lab/signals.hpp"

#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

namespace {

// 100 g of water in the conical flask, over the burner
const char *BENCH = R"({"format": "chem-bench/2", "grid": [28, 15], "room": {"celsius": 20, "air_mol": 1000},
	"items": [{"id": "flask", "kind": "vessel", "volume_ml": 250, "wall": "glass", "glassware": "conical_flask", "wall_g": 100, "loss": 0.9,
			"contents": {"water": {"g": 100}}},
		{"id": "burner", "kind": "burner", "celsius": 1927, "conductance": 0.25}]})";

std::unique_ptr<lab::Lab> heated() {
	std::string why;
	auto l = lab::Lab::open(BENCH, why);
	REQUIRE_MESSAGE(l != nullptr, why);
	for (const char *c : { R"({"do": "place", "item": "flask", "at": [10, 6]})", R"({"do": "heat", "burner": "burner", "vessel": "flask"})" }) {
		REQUIRE_MESSAGE(l->command(c, why), c << ": " << why);
	}
	return l;
}

void ok(lab::Lab &l, const std::string &c) {
	std::string why;
	REQUIRE_MESSAGE(l.command(c, why), c << ": " << why);
}

const lab::ItemView &item(const lab::LabView &v, const std::string &id) {
	const auto at = std::find_if(v.items.begin(), v.items.end(), [&](const lab::ItemView &x) { return x.id == id; });
	REQUIRE(at != v.items.end());
	return *at;
}

struct Run {
	double overshoot = 0.0; // above the target, at most
	double settled = 1e9; // the first time from which it stays within a degree
	double last_error = 0.0; // the largest error over the last ten minutes
};

// held at 60 C at a speed, an hour, every invariant checked each step (G3 and G4 among them)
Run hold(const char *speed) {
	auto l = heated();
	ok(*l, R"({"do": "part", "kind": "controller", "at": [1, 1]})");
	ok(*l, R"({"do": "sense", "part": "controller1", "item": "flask"})");
	ok(*l, R"({"do": "rig", "from": "controller1", "item": "burner", "control": "valve"})");
	ok(*l, std::string(R"({"do": "set", "part": "controller1", "what": "celsius", "target": 60, "speed": ")") + speed + "\"}");
	const lab::Part c = item(l->view(), "controller1").part;
	REQUIRE(c.tuned);
	CHECK(c.kc > 0.0); // the burner heats: more level, more heat
	CHECK(c.ti > 0.0);
	bool noted = false;
	for (const lab::Notice &n : l->view().notices) {
		noted = noted || n.says.find("controller1 tuned") != std::string::npos;
	}
	CHECK(noted);
	Run run;
	std::string why;
	for (int second = 1; second <= 3600; ++second) {
		REQUIRE_MESSAGE(l->advance(60, why), why);
		const double t = item(l->view(), "flask").celsius, e = std::fabs(t - 60.0);
		run.overshoot = std::max(run.overshoot, t - 60.0);
		if (e > 1.0) {
			run.settled = 1e9;
		} else if (run.settled > 1e8) {
			run.settled = second;
		}
		if (second > 3000) {
			run.last_error = std::max(run.last_error, e);
		}
	}
	MESSAGE(std::string(speed) << ": overshoot " << run.overshoot << " C, within a degree from " << run.settled << " s, then within " << run.last_error << " C; gains " << c.kc << ", " << c.ti
				  << " s; the process " << c.gain << (c.tau > 0.0 ? "" : " a second") << ", tau " << c.tau << " s, theta " << c.theta << " s");
	return run;
}

} // namespace

TEST_CASE("controller: water in the flask held at 60 C by the burner, at each speed: settled within a degree, faster when asked (Stage 26)") {
	const Run gentle = hold("gentle"), normal = hold("normal"), fast = hold("fast");
	for (const Run *r : { &gentle, &normal, &fast }) {
		CHECK(r->settled < 3000.0);
		CHECK(r->last_error < 1.0);
	}
	CHECK(gentle.overshoot < 2.0);
	CHECK(normal.overshoot < 5.0);
	CHECK(fast.settled <= normal.settled);
	CHECK(normal.settled <= gentle.settled);
}

TEST_CASE("controller: set, rigged and tuned; its rigs follow; untuned with the reason; it acts only while let, and saved and loaded it continues exactly (Stage 26)") {
	auto l = heated();
	std::string why;
	ok(*l, R"({"do": "part", "kind": "controller", "at": [1, 1]})");
	// a target and a speed; a timer's reset cannot follow a level; a rig of a controller has no settings
	CHECK_FALSE(l->command(R"({"do": "set", "part": "controller1", "item": "flask", "what": "celsius", "compare": "above", "value": 1})", why));
	CHECK(why.find("target") != std::string::npos);
	ok(*l, R"({"do": "set", "part": "controller1", "item": "flask", "what": "celsius", "target": 50})");
	CHECK_FALSE(item(l->view(), "controller1").part.tuned); // nothing rigged: nothing to try
	ok(*l, R"({"do": "part", "kind": "timer", "at": [2, 1]})");
	CHECK_FALSE(l->command(R"({"do": "rig", "from": "controller1", "item": "timer1", "control": "reset"})", why));
	CHECK_FALSE(l->command(R"({"do": "rig", "from": "controller1", "item": "burner", "control": "valve", "on": 1})", why));
	ok(*l, R"({"do": "rig", "from": "controller1", "item": "burner", "control": "valve"})");
	CHECK(item(l->view(), "controller1").part.tuned);
	CHECK(l->view().rigs.back().follow);
	// stirring, by PWM: the flask's stirring changes nothing it senses: untuned, with the reason
	auto s = heated();
	ok(*s, R"({"do": "part", "kind": "controller", "at": [1, 1]})");
	ok(*s, R"({"do": "set", "part": "controller1", "item": "flask", "what": "celsius", "target": 50})");
	ok(*s, R"({"do": "rig", "from": "controller1", "item": "flask", "control": "stir"})");
	CHECK_FALSE(item(s->view(), "controller1").part.tuned);
	CHECK(s->view().notices.back().says.find("is not tuned") != std::string::npos);
	// the valve follows its level, a step behind; the controller writes no notice each step
	const size_t notices = l->view().notices.size();
	REQUIRE(l->advance(600, why));
	const lab::LabView v = l->view();
	CHECK(item(v, "burner").valve == doctest::Approx(item(v, "controller1").part.level).epsilon(0.05));
	CHECK(item(v, "burner").valve > 0.0);
	CHECK(v.notices.size() == notices);
	// gated: a wire in from a timer that has not reached zero holds its level at 0, and the valve follows
	ok(*l, R"({"do": "set", "part": "timer1", "seconds": 1000})");
	ok(*l, R"({"do": "wire", "from": "timer1", "to": "controller1"})");
	REQUIRE(l->advance(5, why));
	CHECK(item(l->view(), "controller1").part.level == 0.0);
	CHECK(item(l->view(), "burner").valve == 0.0);
	ok(*l, R"({"do": "unwire", "from": "timer1", "to": "controller1"})");
	// saved and loaded, it continues as it would have
	REQUIRE(l->advance(300, why));
	auto copy = lab::Lab::load(l->save(), why);
	REQUIRE_MESSAGE(copy != nullptr, why);
	REQUIRE(l->advance(600, why));
	REQUIRE(copy->advance(600, why));
	CHECK(copy->digest() == l->digest());
}

TEST_CASE("controller: G3 and G4 fire on a corrupted controller (Stage 26)") {
	lab::Part c;
	c.id = "controller1";
	c.kind = lab::PartKind::Controller;
	c.watching = c.tuned = true;
	c.period = 10;
	c.level = 0.4;
	c.own = true;
	const std::vector<lab::Part> before{ c };
	CHECK(lab::check_G3({ c }, {}, before).empty());
	lab::Part over = c;
	over.level = 1.2;
	CHECK_FALSE(lab::check_G3({ over }, {}, before).empty());
	lab::Part untuned = c;
	untuned.tuned = false;
	CHECK_FALSE(lab::check_G3({ untuned }, {}, before).empty());
	// its PWM: four steps on of ten for 0.4; this step the third of the period, on
	c.duty = 0.4;
	c.on_steps = 4;
	c.phase = 3;
	c.pulse = true;
	CHECK(lab::check_G4({ c }).empty());
	lab::Part late = c;
	late.phase = 6; // this step the sixth: off, but it is on
	CHECK_FALSE(lab::check_G4({ late }).empty());
	lab::Part counted = c;
	counted.on_steps = 5;
	CHECK_FALSE(lab::check_G4({ counted }).empty());
}
