// The tutorial (GAME_SPEC §13.2): the demo as steps with checks read from the
// lab, played here with the pointer and nothing but its own gestures.
#include "lab/tutorial.hpp"
#include "lab/ui.hpp"

#include <doctest/doctest.h>

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
	void play(const lab::TutorialGesture &g) {
		if (g.drag) {
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
			REQUIRE_MESSAGE(ui->lab().step() < until, "waited too long on step " << tutorial().step + 1 << ": " << tutorial().title);
			ui->frame(1.0 / 60.0);
			REQUIRE_MESSAGE(!ui->lab().failed(), ui->message());
		}
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
		p.watch([&] { return p.tutorial().step != step || p.tutorial().state != TutorialView::Running; }, 3600);
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
	p.drag("item:flask", "cell:3,1");
	CHECK(p.tutorial().step == 1);
	CHECK(p.drawn("A burner under the flask"));
	p.drag("item:burner", "item:flask");
	p.drag("item:jar", "cell:1,1");
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

TEST_CASE("tutorial: a failure ends the run until a reset (Stage 12)") {
	SUBCASE("handing in the wrong thing") {
		Pupil p;
		p.drag("item:jar", "cell:1,1");
		p.drag("item:jar", "outbox");
		CHECK(p.tutorial().state == TutorialView::Failed);
		CHECK(p.tutorial().failure.find("Only the balloon") != std::string::npos);
		CHECK(p.drawn("This run cannot be completed"));
		CHECK(p.ui->tutorial()->gestures().empty());
		p.drag("item:flask", "cell:3,1"); // nothing more is passed
		CHECK(p.tutorial().state == TutorialView::Failed);
		p.click("reset");
		CHECK(p.tutorial().state == TutorialView::Running);
		CHECK(p.tutorial().step == 0);
		p.drag("item:flask", "cell:3,1");
		CHECK(p.tutorial().step == 1);
	}
	SUBCASE("acid on the malachite before it has broken down") {
		Pupil p;
		p.drag("item:flask", "cell:3,1");
		p.drag("item:jar", "cell:1,1");
		p.drag("item:jar", "item:flask");
		p.click("panel:tip:malachite");
		p.drag("item:acid", "cell:2,1");
		p.drag("item:acid", "item:flask");
		p.click("panel:pour");
		CHECK(p.tutorial().state == TutorialView::Failed);
		CHECK(p.tutorial().failure.find("never gets hot enough") != std::string::npos);
	}
	SUBCASE("the beaker handed in with acid in it") {
		Pupil p;
		p.drag("item:beaker", "cell:5,1");
		p.drag("item:acid", "cell:2,1");
		p.drag("item:acid", "item:beaker");
		p.click("panel:pour");
		p.drag("item:beaker", "outbox");
		CHECK(p.tutorial().state == TutorialView::Failed);
		CHECK(p.tutorial().failure.find("more than water") != std::string::npos);
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
