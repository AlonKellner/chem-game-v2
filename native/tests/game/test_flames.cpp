// The experiment flames (GAME_SPEC §13.7, Stage 90): seven metals in a burner's flame by eye and through the spectral
// viewer, potassium in a dimmed room, sodium hiding potassium, two unknowns; played through the screen with its own
// gestures, strict and free, at once and at a person's pace, and its report checked.
#include "lab/conditions.hpp"
#include "lab/notebook.hpp"
#include "lab/tutorial.hpp"
#include "lab/ui.hpp"

#include <doctest/doctest.h>

#include "pupil.hpp"

#include <cstdio>
#include <string>
#include <vector>

using lab::TutorialView;
using pupil::Pupil;

namespace {

void completed(Pupil &p) {
	REQUIRE(p.tutorial().state == TutorialView::Complete);
	CHECK(p.ui->lab().view().conserved);
	CHECK(p.drawn("Tutorial complete"));
	for (const std::string &r : p.tutorial().results) {
		MESSAGE(r);
	}
}

double truth_of(Pupil &p, const std::string &expression) {
	double v = 0.0;
	std::string why;
	REQUIRE_MESSAGE(lab::evaluate(expression, p.ui->lab().view(), v, why), why);
	return v;
}

// each step's gestures played and the lab waited on, to the step titled `title` (not played)
void follow_to(Pupil &p, const std::string &title) {
	for (int guard = 0; guard < 100 && p.tutorial().state == TutorialView::Running && p.tutorial().title != title; ++guard) {
		const int at = p.tutorial().step;
		for (const lab::TutorialGesture &g : p.ui->tutorial()->gestures()) {
			p.play(g);
		}
		for (int f = 0; f < 36000 && p.tutorial().step == at; ++f) {
			p.ui->frame(f < 600 ? 1.0 / 60.0 : 1.0);
		}
		REQUIRE_MESSAGE(p.tutorial().step != at, "stuck at " << p.tutorial().title);
	}
	REQUIRE(p.tutorial().title == title);
}

// the viewer's lines, read on its readout once it has read every nanometre (the flame held: a step that asks for a
// gesture holds the clock)
std::string lines_seen(Pupil &p) {
	p.click("item:viewer");
	for (int f = 0; f < 1800 && (f < 30 || !p.drawn("settled: every nanometre")); ++f) {
		p.ui->frame(1.0 / 60.0);
		p.ui->optics(); // (the probe reads in the optical pass)
		p.ui->draw();
	}
	const std::string text = p.ui->draw().text;
	const size_t at = text.find("lines at");
	REQUIRE(at != std::string::npos);
	return text.substr(at, text.find(" nm", at) - at);
}

} // namespace

TEST_CASE("experiment flames: followed with its own gestures, strict, the viewer lists each metal's lines, potassium's beside sodium's in the mixture, and the report holds (Stage 90)") {
	Pupil p("data/benches/flames.json", "data/tutorials/flames.json");
	// sodium: its D lines
	follow_to(p, "Na's line in the notebook");
	const std::string sodium = lines_seen(p);
	MESSAGE("sodium: " << sodium);
	CHECK((sodium.find("589") != std::string::npos || sodium.find("590") != std::string::npos));
	// lithium: 671
	follow_to(p, "Li's line in the notebook");
	const std::string lithium = lines_seen(p);
	MESSAGE("lithium: " << lithium);
	CHECK(lithium.find("671") != std::string::npos);
	// potassium, in the dimmed room: 766 (the lights at 10 %)
	follow_to(p, "K's line in the notebook");
	CHECK(p.ui->room_level() <= 0.1);
	const std::string potassium = lines_seen(p);
	MESSAGE("potassium: " << potassium);
	CHECK((potassium.find("766") != std::string::npos || potassium.find("767") != std::string::npos));
	// the bands' peaks: calcium, strontium, barium, copper
	for (const char *const step : { "Ca's band in the notebook", "Sr's band in the notebook", "Ba's band in the notebook", "Cu's band in the notebook" }) {
		follow_to(p, step);
		p.click("item:viewer");
		for (int f = 0; f < 1800 && (f < 30 || !p.drawn("settled: every nanometre")); ++f) {
			p.ui->frame(1.0 / 60.0);
			p.ui->optics();
			p.ui->draw();
		}
		const std::string text = p.ui->draw().text;
		size_t at = text.find("peak ");
		while (at != std::string::npos && !(at + 5 < text.size() && text[at + 5] >= '0' && text[at + 5] <= '9')) {
			at = text.find("peak ", at + 1);
		}
		REQUIRE(at != std::string::npos);
		MESSAGE(std::string(step) << ": " << text.substr(at, text.find(':', at) - at) << "; " << lines_seen(p));
	}
	// the mixture: yellow, and potassium's lines beside sodium's
	follow_to(p, "The mixture's lines in the notebook");
	const std::string mixture = lines_seen(p);
	MESSAGE("the mixture: " << mixture);
	CHECK((mixture.find("589") != std::string::npos || mixture.find("590") != std::string::npos));
	CHECK((mixture.find("766") != std::string::npos || mixture.find("767") != std::string::npos));
	pupil::follow_through(p, 600);
	completed(p);
	// the report: the lines read, a photon's energy, the choices
	const std::vector<double> typed = { 589.0, 671.0, 766.0, 6.02214076e23 * 6.62607015e-34 * 299792458.0 / 589.0e-9 / 1000.0 };
	for (size_t k = 0; k < typed.size(); ++k) {
		p.click("report:" + std::to_string(k));
		for (int b = 0; b < 20; ++b) {
			p.ui->key(lab::Ui::KeyBackspace);
		}
		char number[32];
		std::snprintf(number, sizeof number, "%.6g", typed[k]);
		p.type(number);
	}
	p.click("report:4:1"); // strontium
	p.click("report:5:1"); // copper
	p.click("report:6:0"); // the mixture: sodium and potassium
	p.click("report:6:2");
	static const char *const METALS[] = { "sodium_ion", "lithium_ion", "potassium_ion", "calcium_ion", "strontium_ion", "barium_ion", "copper_ion" };
	for (const int k : { 7, 8 }) {
		const std::string unknown = k == 7 ? "unknown_x" : "unknown_y";
		for (int m = 0; m < 7; ++m) {
			if (truth_of(p, unknown + "." + METALS[m] + ".mol") > 0.0) {
				MESSAGE(unknown << ": " << std::string(METALS[m]));
				p.click("report:" + std::to_string(k) + ":" + std::to_string(m));
			}
		}
	}
	p.click("report:check");
	p.ui->frame(1.0 / 60.0);
	CHECK(!p.drawn("not quite"));
	CHECK(!p.drawn("yours is off by"));
}

TEST_CASE("experiment flames: played as a person plays it, strict, it completes (Stage 90)") {
	Pupil p("data/benches/flames.json", "data/tutorials/flames.json");
	pupil::play_as_person(p, 600);
	completed(p);
}

TEST_CASE("experiment flames: followed with its own gestures, free, it completes (Stage 90)") {
	Pupil p("data/benches/flames.json", "data/tutorials/flames.json", false);
	pupil::follow_through(p, 600);
	completed(p);
}

TEST_CASE("flames: a flame's load is measured whole or one element's, a tutorial reads the room's lights, and the probe dropped in a lit flame rests on its burner (Stage 90)") {
	// a quantity: the flame's load whole, sodium's alone; no element "Xx"
	std::string why;
	lab::Quantity q;
	q.item = "burner";
	q.what = "flame_umol";
	CHECK(lab::valid(q, why));
	q.measure = "Na";
	CHECK(lab::valid(q, why));
	q.measure = "Xx";
	CHECK(!lab::valid(q, why));
	CHECK(why.find("symbol") != std::string::npos);
	Pupil p("data/benches/flames.json", "data/tutorials/flames.json", false);
	// the lights: a check of 10 % or less holds only once they are down that far
	const lab::LabView &bench = p.ui->lab().view();
	auto t = lab::Tutorial::open(R"({"format": "chem-tutorial/1", "steps": [{"title": "down", "text": "", "checks": [{"text": "dim", "when": {"lights_at_most": 0.1}}]},
		{"title": "up", "text": "", "checks": [{"text": "full", "when": {"lights_at_least": 1.0}}]}]})",
			bench, why);
	REQUIRE_MESSAGE(t != nullptr, why);
	CHECK(lab::Tutorial::open(R"({"format": "chem-tutorial/1", "steps": [{"title": "x", "text": "", "checks": [{"text": "x", "when": {"lights_at_most": 0}}]}]})", bench, why) == nullptr);
	lab::TutorialScreen screen;
	screen.lights = 0.3;
	t->follow(bench, screen);
	CHECK(t->view().step == 0);
	screen.lights = 0.1;
	t->follow(bench, screen);
	CHECK(t->view().step == 1);
	screen.lights = 1.0;
	t->follow(bench, screen);
	CHECK(t->view().state == TutorialView::Complete);
	// the probe: dropped in the lit burner's flame, on the burner; the load of sodium alone after a sodium test
	std::string bad;
	REQUIRE(p.ui->lab().command(R"({"do": "valve", "burner": "burner", "to": 1})", bad));
	p.ui->frame(1.0 / 60.0);
	p.drag("probe:viewer", "flame:burner");
	p.ui->frame(1.0 / 60.0);
	for (const lab::ItemView &v : p.ui->lab().view().items) {
		if (v.id == "viewer") {
			CHECK(v.probe_on == "burner");
			CHECK(v.probe_v < 0.0); // (over the burner's body: in its flame)
		}
	}
	REQUIRE(p.ui->lab().command(R"({"do": "flame_test", "burner": "burner", "from": "sodium"})", bad));
	lab::Quantity na{ "burner", "flame_umol", "Na" }, k{ "burner", "flame_umol", "K" }, all{ "burner", "flame_umol", "g" };
	CHECK(*lab::measure(p.ui->lab().view(), na) > 1.0);
	CHECK(*lab::measure(p.ui->lab().view(), k) == 0.0);
	CHECK(*lab::measure(p.ui->lab().view(), all) == doctest::Approx(*lab::measure(p.ui->lab().view(), na)));
}
