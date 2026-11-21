// The tutorial (GAME_SPEC §13.2): the demo as steps with checks read from the
// lab, played here with the pointer and nothing but its own gestures.
#include "lab/conditions.hpp"
#include "lab/tutorial.hpp"
#include "lab/ui.hpp"

#include <doctest/doctest.h>

#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>

using lab::ItemView;
using lab::LabView;
using lab::TutorialView;

namespace {

std::string read(const std::string &relative) {
	std::ifstream in(std::string(CHEM_REPO_ROOT) + "/" + relative);
	REQUIRE_MESSAGE(static_cast<bool>(in), relative);
	std::ostringstream out;
	out << in.rdbuf();
	return out.str();
}

struct Pupil {
	std::unique_ptr<lab::Ui> ui;

	Pupil() {
		std::string why;
		ui = lab::Ui::open(read("data/benches/lab.json"), why);
		REQUIRE_MESSAGE(ui != nullptr, why);
		REQUIRE_MESSAGE(ui->tutor(read("data/tutorials/demo.json"), why), why);
	}

	const TutorialView &tutorial() const { return ui->tutorial()->view(); }

	void at(const std::string &what, float &x, float &y) {
		REQUIRE_MESSAGE(ui->locate(what, x, y), "not on the screen: " << what << " (step " << tutorial().step + 1 << ": " << tutorial().title << ")");
	}
	void click(const std::string &what) {
		float x = 0, y = 0;
		if ((what.rfind("valve:", 0) == 0 || what.rfind("stir:", 0) == 0) && !ui->locate(what, x, y)) {
			// an item's controls are shown for the item chosen (Stage 22): it is chosen first
			const std::string item = what.substr(what.find(':') + 1);
			click("item:" + item.substr(0, item.find('@')));
		}
		at(what, x, y);
		ui->pointer(x, y, false);
		ui->pointer(x, y, true);
		ui->pointer(x, y, false);
	}
	void drag(const std::string &what, const std::string &to) {
		float x0 = 0, y0 = 0, x1 = 0, y1 = 0;
		at(what, x0, y0);
		at(to, x1, y1);
		ui->pointer(x0, y0, false);
		ui->pointer(x0, y0, true);
		ui->pointer((x0 + x1) / 2, (y0 + y1) / 2, true);
		ui->pointer(x1, y1, true);
		ui->pointer(x1, y1, false);
	}
	void type(const std::string &text) {
		for (const char c : text) {
			ui->key(lab::Ui::KeyText, static_cast<uint32_t>(c));
		}
	}
	// a paused clock (a timer fired) is played on, as the card says
	void resume() {
		if (ui->paused()) {
			click("pause");
		}
	}
	void play(const lab::TutorialGesture &g) {
		if (!g.type.empty()) {
			type(g.type);
		} else if (g.drag) {
			drag(g.what, g.to);
		} else {
			click(g.what);
		}
	}
	ItemView item(const std::string &id) {
		for (const ItemView &v : ui->lab().view().items) {
			if (v.id == id) {
				return v;
			}
		}
		FAIL("no item " << id);
		return ItemView{};
	}
	double grams(const std::string &id, const std::string &species) {
		for (const lab::SubstanceView &s : item(id).contents) {
			if (s.species == species) {
				return s.g;
			}
		}
		return 0.0;
	}
	// frames of 1/60 s until `done`, at most `seconds` of the lab's time
	template <typename Done>
	void watch(Done done, double seconds) {
		const int64_t until = ui->lab().step() + static_cast<int64_t>(seconds * 60.0);
		while (!done()) {
			resume();
			REQUIRE_MESSAGE(ui->lab().step() < until, "waited too long on step " << tutorial().step + 1 << ": " << tutorial().title);
			REQUIRE_MESSAGE((ui->tutorial()->waiting() || tutorial().state != TutorialView::Running),
					"step " << tutorial().step + 1 << " (" << tutorial().title << "): the clock waits for a gesture its gestures did not make; " << ui->message());
			ui->frame(1.0 / 60.0);
			REQUIRE_MESSAGE(!ui->lab().failed(), ui->message());
		}
	}
	// ---- as a person plays: a second and a half to read and reach before each
	// gesture, the pointer moved across the screen frame by frame, the clock running
	void frames(int n) {
		for (int i = 0; i < n; ++i) {
			ui->frame(1.0 / 60.0);
		}
	}
	// where a person lets go: on an item, its middle; in the outbox, on whatever was handed in last
	void target(const std::string &what, float &x, float &y) {
		if (what == "outbox") {
			for (const ItemView &v : ui->lab().view().items) {
				if (v.place == "outbox" && ui->locate("item:" + v.id, x, y)) {
					return; // over it
				}
			}
		}
		at(what, x, y);
	}
	void move(float x0, float y0, float x1, float y1, bool down) {
		for (int i = 1; i <= 20; ++i) {
			const float f = static_cast<float>(i) / 20.0f;
			ui->pointer(x0 + (x1 - x0) * f, y0 + (y1 - y0) * f, down);
			ui->frame(1.0 / 60.0);
		}
	}
	void person(const lab::TutorialGesture &g) {
		frames(90); // reading the card, finding the thing
		if (!g.type.empty()) {
			for (const char c : g.type) {
				ui->key(lab::Ui::KeyText, static_cast<uint32_t>(c));
				frames(12);
			}
			return;
		}
		float x0 = 0, y0 = 0, x1 = 0, y1 = 0;
		if (!g.drag && g.what.rfind("valve:", 0) == 0) {
			// a slider: taken at its knob, where it is now, and dragged
			const std::string burner = g.what.substr(6, g.what.find('@') - 6);
			at("valve:" + burner + "@" + std::to_string(item(burner).valve), x0, y0);
			at(g.what, x1, y1);
		} else if (g.drag) {
			at(g.what, x0, y0);
			target(g.to, x1, y1);
		} else {
			at(g.what, x0, y0);
			x1 = x0;
			y1 = y0;
		}
		move(x0 - 200, y0 - 100, x0, y0, false);
		ui->pointer(x0, y0, true);
		ui->frame(1.0 / 60.0);
		move(x0, y0, x1, y1, true);
		ui->pointer(x1, y1, false);
		ui->frame(1.0 / 60.0);
	}

	bool drawn(const std::string &s) {
		const lab::DrawList &list = ui->draw();
		return list.text.find(s) != std::string::npos;
	}
};

// a lab view by hand, for the conditions alone
LabView view_of(std::initializer_list<ItemView> items) {
	LabView v;
	v.items = items;
	return v;
}

std::unique_ptr<lab::Tutorial> tutorial_of(const std::string &steps_and_more, const LabView &bench, std::string &why) {
	return lab::Tutorial::open("{\"format\": \"chem-tutorial/1\", " + steps_and_more + "}", bench, why);
}

} // namespace

TEST_CASE("tutorial: followed with its own gestures, step by step, it completes the demo (Stage 12)") {
	Pupil p;
	const int steps = p.tutorial().steps;
	REQUIRE(steps > 20);
	CHECK(p.drawn(p.tutorial().title));
	int played = 0;
	while (p.tutorial().state == TutorialView::Running && p.tutorial().step < steps) {
		const int step = p.tutorial().step;
		const std::vector<lab::TutorialGesture> gestures = p.ui->tutorial()->gestures();
		for (const lab::TutorialGesture &g : gestures) {
			p.play(g);
			CHECK_MESSAGE(p.ui->message().empty(), "step " << step + 1 << ": " << p.ui->message());
		}
		played += static_cast<int>(gestures.size());
		// then the lab is waited on, as a player would
		p.watch([&] { return p.tutorial().step != step || p.tutorial().state != TutorialView::Running; }, 3 * 3600); // the crystals take two hours
		CHECK(p.tutorial().state != TutorialView::Failed);
		if (p.tutorial().state == TutorialView::Failed) {
			FAIL(p.tutorial().failure);
		}
	}
	CHECK(played > 25);
	REQUIRE(p.tutorial().state == TutorialView::Complete);
	CHECK(p.tutorial().step == steps);
	for (const lab::TutorialCheck &c : p.tutorial().checks) {
		CHECK_MESSAGE(c.done, c.text);
	}
	// GAME_SPEC §17: done when
	CHECK(p.item("balloon").submitted);
	CHECK(p.item("beaker").submitted);
	CHECK(p.item("flask").submitted);
	CHECK(p.grams("balloon", "carbon_dioxide") > 0.8);
	CHECK(p.grams("beaker", "water") > 35.0);
	CHECK(p.grams("flask", "copper_sulfate_pentahydrate") > 8.9);
	CHECK(p.ui->lab().view().conserved);
	REQUIRE(p.tutorial().results.size() == 4);
	CHECK(p.drawn("Tutorial complete"));
	CHECK(p.drawn("crystals in the flask: "));
	CHECK(p.ui->tutorial()->gestures().empty());
	for (const std::string &r : p.tutorial().results) {
		MESSAGE(r);
	}
	MESSAGE("the tutorial: " << steps << " steps, " << played << " gestures, " << p.ui->lab().step() / 60 << " s of the lab");
	// and reset begins it again
	p.click("reset");
	CHECK(p.tutorial().state == TutorialView::Running);
	CHECK(p.tutorial().step == 0);
	CHECK(p.drawn("Set out the flask"));
}

TEST_CASE("tutorial: played as a person plays it, slowly, at the speed it asks for, it completes (Stage 12)") {
	Pupil p;
	const int steps = p.tutorial().steps;
	int64_t step_limit = 0;
	while (p.tutorial().state == TutorialView::Running && p.tutorial().step < steps) {
		const int step = p.tutorial().step;
		const std::string title = p.tutorial().title;
		for (const lab::TutorialGesture &g : std::vector<lab::TutorialGesture>(p.ui->tutorial()->gestures())) {
			p.person(g);
			INFO("step " << step + 1 << ": " << title);
			REQUIRE_MESSAGE(!p.ui->lab().failed(), "step " << step + 1 << " (" << title << "): " << p.ui->message());
			CHECK_MESSAGE(p.ui->message().empty(), p.ui->message());
			if (p.tutorial().state != TutorialView::Running) {
				break;
			}
		}
		// then waits, looking at the card every half second
		step_limit = p.ui->lab().step() + 3 * 3600 * 60; // the longest step: the crystals, two hours
		while (p.tutorial().step == step && p.tutorial().state == TutorialView::Running) {
			REQUIRE_MESSAGE(p.ui->lab().step() < step_limit, "waited too long on step " << step + 1 << ": " << title);
			p.frames(45); // reading the card
			p.resume();
			const int64_t before = p.ui->lab().step();
			if (const char *keep = std::getenv("CHEM_TUTORIAL_SAVE")) { // the state before each half second, for a failure's diagnosis
				std::ofstream(keep) << p.ui->lab().save();
			}
			p.frames(30);
			REQUIRE_MESSAGE(!p.ui->lab().failed(), "step " << step + 1 << " (" << title << "): " << p.ui->message());
			if (p.ui->lab().step() == before && p.tutorial().step == step) {
				FAIL("step " << step + 1 << " (" << title << "): the clock stands, waiting for a gesture its gestures did not make; " << p.ui->message());
			}
		}
		if (p.tutorial().state == TutorialView::Failed) {
			FAIL("step " << step + 1 << " (" << title << "): " << p.tutorial().failure);
		}
	}
	REQUIRE(p.tutorial().state == TutorialView::Complete);
	CHECK(p.grams("flask", "copper_sulfate_pentahydrate") > 8.0);
	for (const std::string &r : p.tutorial().results) {
		MESSAGE(r);
	}
}

TEST_CASE("tutorial: the screen points at what the step asks for, and stops once it is done (Stage 12)") {
	Pupil p;
	CHECK(p.tutorial().step == 0);
	const auto guides = [&] {
		int n = 0;
		for (const lab::DrawCommand &c : p.ui->draw().commands) {
			n += (c.color & 0xffffff00u) == 0xffc83c00u ? 1 : 0;
		}
		return n;
	};
	CHECK(guides() >= 4); // the flask, the cell, the arrow's line and its head
	p.drag("item:flask", "square:11,12");
	CHECK(p.tutorial().step == 1);
	CHECK(p.drawn("A burner under the flask"));
	p.drag("item:burner", "item:flask");
	p.drag("item:jar", "square:4,13");
	p.drag("item:jar", "item:flask"); // the panel: now it points at the tip button
	CHECK(p.tutorial().step == 3);
	CHECK(guides() == 1);
	p.click("panel:tip:malachite");
	CHECK(p.tutorial().step == 4);
	// a later step done early is passed when it is reached
	p.drag("item:balloon", "item:flask");
	p.click("valve:burner@1");
	p.click("speed:600");
	CHECK(p.tutorial().step == 7); // waiting on the malachite: nothing to point at
	CHECK(p.ui->tutorial()->gestures().empty());
	CHECK(guides() == 0);
	REQUIRE(p.tutorial().checks.size() == 1);
	CHECK(p.tutorial().checks[0].now == "5.000 g");
	CHECK(p.drawn("(now 5.000 g)"));
}

TEST_CASE("tutorial: only the step's own gestures are taken, and nothing else changes the lab (Stage 12)") {
	Pupil p;
	const auto refused = [&](const std::string &digest) {
		CHECK(p.ui->message().find("Not now") != std::string::npos);
		CHECK(p.ui->lab().digest() == digest);
	};
	std::string before = p.ui->lab().digest();
	p.drag("item:burner", "square:11,12"); // the flask comes first
	refused(before);
	CHECK(p.item("burner").place == "cabinet");
	p.drag("item:jar", "outbox");
	refused(before);
	p.drag("item:flask", "cabinet"); // the right item, the wrong place
	refused(before);
	p.drag("item:flask", "square:11,12");
	CHECK(p.tutorial().step == 1);
	CHECK(p.ui->message().empty());
	before = p.ui->lab().digest();
	p.drag("item:burner", "square:19,12"); // onto the bench, not under the flask
	refused(before);
	p.drag("item:burner", "item:flask");
	p.drag("item:jar", "square:4,13");
	before = p.ui->lab().digest();
	p.click("valve:burner@1"); // the malachite first
	refused(before);
	p.drag("item:jar", "item:flask");
	p.click("panel:tip:malachite");
	p.drag("item:acid", "square:7,12"); // the acid's time is the second stage
	refused(p.ui->lab().digest());
	CHECK(p.item("acid").place == "cabinet");
	p.drag("item:balloon", "item:flask");
	p.click("valve:burner@1");
	CHECK(p.tutorial().step == 6);
	// the clock waits while a gesture is asked for
	const int64_t at = p.ui->lab().step();
	p.ui->frame(5.0);
	CHECK(p.ui->lab().step() == at);
	CHECK(p.drawn("The clock waits for you."));
	p.click("speed:600"); // the clock is always the player's
	CHECK(p.tutorial().step == 7);
	CHECK(p.ui->tutorial()->waiting());
	p.ui->frame(1.0 / 60.0);
	CHECK(p.ui->lab().step() == at + 600);
	CHECK(p.drawn("The clock runs"));
	// while the lab is waited on, nothing but the clock
	before = p.ui->lab().digest();
	p.drag("item:balloon", "outbox");
	refused(before);
	CHECK(p.ui->message().find("wait for the lab") != std::string::npos);
	// and when the wait ends the clock stops within a second of the lab, for the next gesture
	p.watch([&] { return p.tutorial().step == 8; }, 1800);
	CHECK(p.grams("flask", "malachite") < 0.005);
	const int64_t stood = p.ui->lab().step();
	p.ui->frame(1.0);
	CHECK(p.ui->lab().step() == stood);
	p.drag("item:balloon", "outbox"); // the first free slot without a goal: not the one the step asks for
	CHECK_FALSE(p.item("balloon").submitted);
	p.drag("item:balloon", "slot:1");
	CHECK(p.item("balloon").submitted);
	CHECK(p.item("balloon").slot == 1);
	// reset is the player's too
	p.click("reset");
	CHECK(p.tutorial().step == 0);
	CHECK(p.item("balloon").place == "cabinet");
}

TEST_CASE("tutorial: a failure ends the run until a restart (Stage 12)") {
	// the lab driven by its commands, past the screen's guard: what the failures say
	std::string why;
	const auto played = [&](std::initializer_list<const char *> commands) {
		std::unique_ptr<lab::Lab> l = lab::Lab::open(read("data/benches/lab.json"), why);
		REQUIRE(l != nullptr);
		std::unique_ptr<lab::Tutorial> t = lab::Tutorial::open(read("data/tutorials/demo.json"), l->view(), why);
		REQUIRE_MESSAGE(t != nullptr, why);
		for (const char *c : commands) {
			REQUIRE_MESSAGE(l->command(c, why), c << ": " << why);
			t->follow(l->view(), 1.0, l->failed());
		}
		return std::make_pair(std::move(l), std::move(t));
	};
	{
		auto [l, t] = played({ R"({"do": "place", "item": "jar", "at": [3, 9]})", R"({"do": "submit", "item": "jar"})" });
		CHECK(t->view().state == TutorialView::Failed);
		CHECK(t->view().failure.find("Only the balloon") != std::string::npos);
		CHECK(t->gestures().empty());
		t->follow(l->view(), 1.0, false); // it stays failed
		CHECK(t->view().state == TutorialView::Failed);
		t->restart();
		CHECK(t->view().state == TutorialView::Running);
		CHECK(t->view().step == 0);
	}
	{
		auto [l, t] = played({ R"({"do": "place", "item": "flask", "at": [10, 8]})", R"({"do": "place", "item": "jar", "at": [3, 9]})",
				R"({"do": "tip", "from": "jar", "to": "flask", "species": "malachite", "all": true})", R"({"do": "place", "item": "acid", "at": [6, 8]})",
				R"({"do": "pour", "from": "acid", "to": "flask", "ml": 50})" });
		CHECK(t->view().state == TutorialView::Failed);
		CHECK(t->view().failure.find("never gets hot enough") != std::string::npos);
	}
	{
		auto [l, t] = played({ R"({"do": "place", "item": "beaker", "at": [18, 8]})", R"({"do": "place", "item": "acid", "at": [6, 8]})",
				R"({"do": "pour", "from": "acid", "to": "beaker", "ml": 50})", R"({"do": "submit", "item": "beaker"})" });
		CHECK(t->view().state == TutorialView::Failed);
		CHECK(t->view().failure.find("more than water") != std::string::npos);
	}
}

TEST_CASE("tutorial: conditions read the lab's view (Stage 12)") {
	ItemView flask, burner, balloon;
	flask.id = "flask";
	flask.kind = "vessel";
	flask.place = "bench";
	flask.celsius = 25.0;
	flask.liquid_ml = 50.0;
	flask.contents = { { "malachite", "Cu2CO3(OH)2", 's', false, 0.01, 2.0, 1000, 10.0 },
		{ "copper_sulfate_pentahydrate", "CuSO4.5H2O", 's', false, 0.03, 8.0, 5000, 200.0 },
		{ "nitrogen", "N2", 'g', false, 0.003, 0.084, 0, 0.0 },
		{ "carbon_dioxide", "CO2", 'g', false, 0.001, 0.044, 0, 0.0 },
		{ "glass", "SiO2", 's', true, 1.0, 100.0, 0, 0.0 } };
	burner.id = "burner";
	burner.kind = "burner";
	burner.place = "under";
	burner.under = 0;
	burner.valve = 0.5;
	balloon.id = "balloon";
	balloon.kind = "balloon";
	balloon.place = "on";
	balloon.on = 0;
	const LabView bench = view_of({ flask, burner, balloon });
	const auto passes = [&](const std::string &when) {
		std::string why;
		auto t = tutorial_of("\"steps\": [{\"title\": \"t\", \"text\": \"x\", \"checks\": [{\"text\": \"c\", \"when\": " + when + "}]}]", bench, why);
		REQUIRE_MESSAGE(t != nullptr, why << " in " << when);
		t->follow(bench, 60.0, false);
		return t->view().state == TutorialView::Complete;
	};
	CHECK(passes(R"({"item": "flask", "place": "bench"})"));
	CHECK_FALSE(passes(R"({"item": "flask", "place": "cabinet"})"));
	CHECK(passes(R"({"item": "burner", "under": "flask"})"));
	CHECK(passes(R"({"item": "balloon", "on": "flask"})"));
	CHECK_FALSE(passes(R"({"item": "balloon", "on": "burner"})"));
	CHECK(passes(R"({"item": "burner", "valve_at_least": 0.5, "valve_at_most": 0.5})"));
	CHECK_FALSE(passes(R"({"item": "burner", "valve_at_least": 0.6})"));
	CHECK(passes(R"({"item": "flask", "above_c": 24, "below_c": 26})"));
	CHECK(passes(R"({"item": "flask", "liquid_above_ml": 45, "liquid_below_ml": 55})"));
	CHECK(passes(R"({"item": "flask", "species": "malachite", "above_g": 1.9, "below_g": 2.1})"));
	CHECK(passes(R"({"item": "flask", "species": "copper_oxide", "below_g": 0.001})")); // absent is none
	CHECK(passes(R"({"item": "flask", "species": "copper_sulfate_pentahydrate", "purity_above": 0.79})")); // 8 of 10 g; the walls and the gas not counted
	CHECK_FALSE(passes(R"({"item": "flask", "species": "copper_sulfate_pentahydrate", "purity_above": 0.81})"));
	CHECK(passes(R"({"item": "flask", "holds_only": ["malachite", "copper_sulfate_pentahydrate", "nitrogen", "carbon_dioxide"]})"));
	CHECK_FALSE(passes(R"({"item": "flask", "holds_only": ["malachite", "copper_sulfate_pentahydrate", "nitrogen"]})"));
	CHECK(passes(R"({"speed_at_least": 60})"));
	CHECK_FALSE(passes(R"({"speed_at_least": 600})"));
	CHECK(passes(R"({"conserved": true})"));
	CHECK(passes(R"({"not": {"item": "flask", "stirred": true}})"));
	CHECK(passes(R"({"any": [{"item": "flask", "stirred": true}, {"item": "flask", "submitted": false}]})"));
	CHECK_FALSE(passes(R"({"all": [{"item": "flask", "stirred": false}, {"item": "flask", "burst": true}]})"));
}

TEST_CASE("tutorial: one the bench cannot follow is refused, with the reason (Stage 12)") {
	ItemView flask;
	flask.id = "flask";
	const LabView bench = view_of({ flask });
	const auto refused = [&](const std::string &body, const std::string &reason) {
		std::string why;
		const bool none = tutorial_of(body, bench, why) == nullptr;
		CHECK_MESSAGE(none, body);
		CHECK_MESSAGE(why.find(reason) != std::string::npos, why);
	};
	const std::string step = R"("steps": [{"title": "t", "text": "x", "checks": [{"text": "c", "when": )";
	refused(R"("steps": [])", "no steps");
	refused(step + R"({"item": "beaker", "place": "bench"}}]}])", "no item \"beaker\"");
	refused(step + R"({"item": "flask", "colour": "blue"}}]}])", "no key \"colour\"");
	refused(step + R"({"item": "flask", "place": "shelf"}}]}])", "no place");
	refused(step + R"({"item": "flask", "species": "gold"}}]}])", "no species");
	refused(step + R"({"item": "flask", "species": "water"}}]}])", "no \"above_g\"");
	refused(step + R"({"place": "bench"}}]}])", "names no \"item\"");
	refused(step + R"({"item": "flask", "above_g": 1}}]}])", "names no \"species\"");
	refused(R"("steps": [{"title": "t", "text": "x", "checks": []}])", "has no checks");
	refused(R"("steps": [{"title": "t", "text": "x", "checks": [{"text": "c", "when": {"item": "flask", "place": "bench"}}], "play": [{"tap": "x"}]}])", "a gesture");
	std::string why;
	CHECK(lab::Tutorial::open("{\"format\": \"chem-tutorial/2\"}", bench, why) == nullptr);
	CHECK(why.find("chem-tutorial/1") != std::string::npos);
	// the game's own tutorial on its own bench
	std::unique_ptr<lab::Ui> ui = lab::Ui::open(read("data/benches/lab.json"), why);
	REQUIRE(ui != nullptr);
	CHECK_MESSAGE(ui->tutor(read("data/tutorials/demo.json"), why), why);
	// and not on a bench without its items
	CHECK_FALSE(lab::Tutorial::open(read("data/tutorials/demo.json"), bench, why));
}

TEST_CASE("conditions: purity and a balloon's fill are quantities, and any quantity compares (Stage 13)") {
	ItemView flask, balloon;
	flask.id = "flask";
	flask.kind = "vessel";
	flask.place = "bench";
	flask.contents = { { "malachite", "Cu2CO3(OH)2", 's', false, 0.01, 2.0, 1000, 10.0 },
		{ "copper_sulfate_pentahydrate", "CuSO4.5H2O", 's', false, 0.03, 8.0, 5000, 200.0 },
		{ "nitrogen", "N2", 'g', false, 0.003, 0.084, 0, 0.0 },
		{ "carbon_dioxide", "CO2", 'g', false, 0.001, 0.044, 0, 0.0 },
		{ "glass", "SiO2", 's', true, 1.0, 100.0, 0, 0.0 } };
	balloon.id = "balloon";
	balloon.kind = "balloon";
	balloon.place = "bench";
	balloon.capacity_ml = 3000.0;
	balloon.gas_ml = 750.0;
	const LabView bench = view_of({ flask, balloon });
	using lab::Quantity;
	// a solid's share of the solids and liquids by mass, the walls and the gas not counted
	CHECK(*lab::measure(bench, Quantity{ "flask", "copper_sulfate_pentahydrate", "purity" }) == doctest::Approx(80.0));
	// a gas's share of the gas by amount
	CHECK(*lab::measure(bench, Quantity{ "flask", "carbon_dioxide", "purity" }) == doctest::Approx(25.0));
	CHECK(*lab::measure(bench, Quantity{ "flask", "copper_oxide", "purity" }) == 0.0);
	CHECK(*lab::measure(bench, Quantity{ "balloon", "fill", "g" }) == doctest::Approx(25.0));
	CHECK_FALSE(lab::measure(bench, Quantity{ "flask", "fill", "g" })); // a vessel has no capacity
	CHECK(lab::label(bench, Quantity{ "flask", "copper_sulfate_pentahydrate", "purity" }) == "flask: CuSO4.5H2O(s) purity (%)");
	CHECK(lab::shown(Quantity{ "flask", "copper_sulfate_pentahydrate", "purity" }, 99.95) == "99.950 %");
	CHECK(lab::quantities_of(bench.items[1]).back() == Quantity{ "balloon", "fill", "g" });

	const auto passes = [&](const std::string &when) {
		std::string why;
		auto t = tutorial_of("\"steps\": [{\"title\": \"t\", \"text\": \"x\", \"checks\": [{\"text\": \"c\", \"when\": " + when + "}]}]", bench, why);
		REQUIRE_MESSAGE(t != nullptr, why << " in " << when);
		t->follow(bench, 60.0, false);
		return t->view().state == TutorialView::Complete;
	};
	CHECK(passes(R"({"item": "flask", "quantity": {"what": "copper_sulfate_pentahydrate", "measure": "purity"}, "compare": "at_least", "value": 80})"));
	CHECK_FALSE(passes(R"({"item": "flask", "quantity": {"what": "copper_sulfate_pentahydrate", "measure": "purity"}, "compare": "above", "value": 80.5})"));
	CHECK(passes(R"({"quantity": {"item": "balloon", "what": "fill"}, "compare": "between", "values": [20, 30]})"));
	CHECK(passes(R"({"item": "flask", "quantity": {"what": "copper_sulfate_pentahydrate", "measure": "count"}, "compare": "outside", "values": [0, 4999]})"));
	CHECK_FALSE(passes(R"({"quantity": {"item": "flask", "what": "fill"}, "compare": "below", "value": 100})")); // not a quantity of a vessel: never holds

	const auto refused = [&](const std::string &when, const std::string &reason) {
		std::string why;
		const bool none = tutorial_of("\"steps\": [{\"title\": \"t\", \"text\": \"x\", \"checks\": [{\"text\": \"c\", \"when\": " + when + "}]}]", bench, why) == nullptr;
		CHECK_MESSAGE(none, when);
		CHECK_MESSAGE(why.find(reason) != std::string::npos, why);
	};
	refused(R"({"item": "flask", "quantity": {"what": "water"}, "value": 1})", "a comparison is");
	refused(R"({"item": "flask", "quantity": {"what": "water"}, "compare": "nearly", "value": 1})", "no comparison");
	refused(R"({"item": "flask", "quantity": {"what": "water"}, "compare": "between", "value": 1})", "values");
	refused(R"({"item": "flask", "quantity": {"what": "water"}, "compare": "between", "values": [3, 1]})", "low end");
	refused(R"({"item": "flask", "quantity": {"what": "water", "measure": "taste"}, "compare": "above", "value": 1})", "\"purity\"");
	refused(R"({"quantity": {"what": "water"}, "compare": "above", "value": 1})", "names no \"item\"");
}

TEST_CASE("tutorial: in the part editor only the step's own settings are taken, and a wrong one cannot be set (Stage 13)") {
	Pupil p;
	// played as the tutorial asks, up to the step titled `title`
	const auto play_to = [&](const std::string &title) {
		while (p.tutorial().title != title) {
			REQUIRE(p.tutorial().state == TutorialView::Running);
			const int step = p.tutorial().step;
			for (const lab::TutorialGesture &g : std::vector<lab::TutorialGesture>(p.ui->tutorial()->gestures())) {
				p.play(g);
			}
			p.watch([&] { return p.tutorial().step != step; }, 3600);
		}
	};
	const auto editing = [&] {
		float x = 0, y = 0;
		return p.ui->locate("edit:set", x, y);
	};
	const auto monitor = [&] { return p.item("monitor1").part; };
	// what the tutorial's pointer outlines: is `what` inside it
	const auto pointed = [&](const std::string &what) {
		float x = 0, y = 0;
		REQUIRE(p.ui->locate(what, x, y));
		int outlines = 0;
		bool inside = false;
		for (const lab::DrawCommand &c : p.ui->draw().commands) {
			if (c.type == lab::DrawCommand::Outline && c.width == 3.0f && (c.color & 0xffffff00u) == 0xffc83c00u) {
				++outlines;
				inside = inside || (x >= c.x && x <= c.x + c.w && y >= c.y && y <= c.y + c.h);
			}
		}
		return outlines == 1 && inside;
	};
	play_to("Watch the flask's water");
	CHECK(pointed("edit:kind:material")); // the flask is sensed: then the kind, the material, its measure (Stage 26)
	float hidden_x = 0, hidden_y = 0;
	CHECK_FALSE(p.ui->locate("edit:material:water", hidden_x, hidden_y)); // not shown yet: no kind is chosen
	p.click("edit:kind:material");
	CHECK(pointed("edit:material:water"));
	p.click("edit:material:water");
	CHECK(pointed("edit:measure:g"));
	p.click("edit:measure:g");
	REQUIRE(p.tutorial().title == "Below 12 g");
	REQUIRE(editing());
	CHECK(pointed("edit:value:0")); // "below" is already chosen: the value, until it reads 12
	p.click("edit:value:0");
	p.type("1");
	CHECK(pointed("edit:value:0"));
	p.type("2");
	CHECK(pointed("edit:set")); // only now the button
	p.ui->key(lab::Ui::KeyBackspace);
	CHECK(pointed("edit:value:0"));
	p.click("edit:compare:above"); // not what the step asks: refused, below stays chosen
	CHECK(p.ui->message().find("Not now") != std::string::npos);
	p.click("edit:nudge:1");
	p.click("edit:cancel");
	p.ui->key(lab::Ui::KeyEscape);
	CHECK(editing()); // it cannot be closed half set
	p.click("edit:value:0");
	p.type("15"); // the 5 is not the step's 12
	CHECK(p.ui->message().find("Not that") != std::string::npos);
	p.click("edit:set"); // 1 g: the step's check would not hold
	CHECK(p.ui->message().find("the monitor signals below 12 g of water") != std::string::npos);
	CHECK(editing());
	CHECK_FALSE(monitor().watching);
	p.type("2");
	p.click("edit:set");
	CHECK(p.ui->message().empty());
	CHECK(monitor().watching);
	CHECK(monitor().comparison.a == 12.0);
	CHECK(p.tutorial().title == "Rig it to the burner");
	play_to("Close the valve when it signals");
	REQUIRE(editing());
	CHECK(pointed("edit:on:value")); // its row's value, until it is set to 0 %
	p.click("edit:set"); // "when on" left as it is: the valve would never close
	CHECK(p.ui->message().find("the burner closes when the monitor signals") != std::string::npos);
	CHECK(p.ui->lab().view().rigs.empty());
	p.click("edit:off:value"); // the other row
	p.click("edit:form:change");
	CHECK(p.ui->message().find("Not now") != std::string::npos);
	p.click("edit:on:value");
	p.type("5");
	p.type("0");
	CHECK(pointed("edit:set"));
	p.click("edit:set");
	REQUIRE(p.ui->lab().view().rigs.size() == 1);
	const lab::Rig rig = p.ui->lab().view().rigs[0];
	CHECK(*rig.on == 0.0);
	CHECK_FALSE(rig.off.has_value());
	CHECK(p.tutorial().title == "An alarm");
}
