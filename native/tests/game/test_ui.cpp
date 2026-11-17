// The screen (GAME_SPEC §13): a pointer in, lab commands and a draw list out.
// The demo is played here with nothing but the pointer.
#include "lab/ui.hpp"

#include <doctest/doctest.h>

#include <fstream>
#include <sstream>
#include <string>

using lab::ItemView;
using lab::LabView;

namespace {

std::string read(const std::string &relative) {
	std::ifstream in(std::string(CHEM_REPO_ROOT) + "/" + relative);
	REQUIRE_MESSAGE(static_cast<bool>(in), relative);
	std::ostringstream out;
	out << in.rdbuf();
	return out.str();
}

struct Player {
	std::unique_ptr<lab::Ui> ui;

	Player() {
		std::string why;
		ui = lab::Ui::open(read("data/benches/lab.json"), why);
		REQUIRE_MESSAGE(ui != nullptr, why);
	}

	void at(const std::string &what, float &x, float &y) {
		REQUIRE_MESSAGE(ui->locate(what, x, y), "not on the screen: " << what);
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
	// a click with keys held (lab::Ui::Mod)
	void click_with(const std::string &what, int mods) {
		float x = 0, y = 0;
		at(what, x, y);
		ui->pointer(x, y, false, mods);
		ui->pointer(x, y, true, mods);
		ui->pointer(x, y, false, mods);
	}
	// the pointer pressed at one point and moved to another as a hand does, a frame at a time
	void sweep(float x0, float y0, float x1, float y1) {
		ui->pointer(x0, y0, false);
		ui->pointer(x0, y0, true);
		for (int i = 1; i <= 20; ++i) {
			const float t = static_cast<float>(i) / 20.0f;
			ui->pointer(x0 + (x1 - x0) * t, y0 + (y1 - y0) * t, true);
			ui->frame(1.0 / 60.0);
		}
		ui->pointer(x1, y1, false);
	}
	ItemView item(const std::string &id) {
		const LabView view = ui->lab().view();
		for (const ItemView &v : view.items) {
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
	// frames of 1/60 s at the speed chosen, until `done` (a player watching)
	template <typename Done>
	void watch(Done done, double timeout_seconds) {
		const double frames = timeout_seconds * 60.0 / ui->speed();
		for (int frame = 0; !done(); ++frame) {
			REQUIRE_MESSAGE(frame < frames, "the player waited too long");
			ui->frame(1.0 / 60.0);
			REQUIRE_MESSAGE(!ui->lab().failed(), ui->message());
		}
	}
	void wait(double seconds) {
		const int frames = static_cast<int>(seconds * 60.0 / ui->speed() + 0.5);
		for (int frame = 0; frame < frames; ++frame) {
			ui->frame(1.0 / 60.0);
		}
		REQUIRE_MESSAGE(!ui->lab().failed(), ui->message());
	}
};

// every index of the draw list is inside it
void well_formed(const lab::DrawList &list) {
	REQUIRE(list.commands.size() > 20);
	for (const lab::DrawCommand &c : list.commands) {
		REQUIRE(c.type >= lab::DrawCommand::Rect);
		REQUIRE(c.type <= lab::DrawCommand::Text);
		if (c.type == lab::DrawCommand::Polygon) {
			REQUIRE(c.first >= 0);
			REQUIRE(c.count >= 3);
			REQUIRE(static_cast<size_t>(2 * (c.first + c.count)) <= list.points.size());
		}
		if (c.type == lab::DrawCommand::Text) {
			REQUIRE(c.first >= 0);
			REQUIRE(static_cast<size_t>(c.first + c.count) < list.text.size());
			REQUIRE(list.text[static_cast<size_t>(c.first + c.count)] == '\0');
		}
	}
}

} // namespace

TEST_CASE("ui: equipment is dragged from the cabinet onto the bench, along it, and back (Stage 9)") {
	Player p;
	CHECK(p.item("flask").place == "cabinet");
	p.drag("item:flask", "square:11,12"); // its body lands under the pointer: a 3 x 7 footprint
	CHECK(p.item("flask").place == "bench");
	CHECK(p.item("flask").col == 10);
	CHECK(p.item("flask").row == 8);
	CHECK(p.item("flask").w == 3);
	CHECK(p.item("flask").h == 7);
	p.drag("item:flask", "square:19,4"); // held by its body's square, (1, 4) of its footprint
	CHECK(p.item("flask").col == 18);
	CHECK(p.item("flask").row == 0);
	p.drag("item:beaker", "square:17,1"); // a footprint over the flask's
	CHECK(p.item("beaker").place == "cabinet");
	CHECK(p.item("beaker").place == "cabinet");
	CHECK(p.ui->message().find("stands there") != std::string::npos);
	p.drag("item:beaker", "item:flask"); // onto the flask itself: the pour panel, with nothing to pour
	p.click("panel:cancel");
	float x = 0, y = 0;
	CHECK_FALSE(p.ui->locate("panel:cancel", x, y)); // closed
	p.drag("item:beaker", "square:23,4");
	CHECK(p.item("beaker").place == "bench");
	p.drag("item:beaker", "cabinet");
	CHECK(p.item("beaker").place == "cabinet");
	CHECK(p.ui->message().empty());
	// a click without a drag moves nothing
	p.click("item:flask");
	CHECK(p.item("flask").col == 18);
	well_formed(p.ui->draw());
}

TEST_CASE("ui: a refusal is shown, and nothing changes (Stage 9)") {
	Player p;
	p.drag("item:flask", "square:11,12");
	p.drag("item:balloon", "item:flask");
	CHECK(p.item("balloon").place == "on");
	const std::string before = p.ui->lab().digest();
	p.drag("item:condenser", "item:flask"); // the mouth is taken
	CHECK(p.ui->message().find("taken") != std::string::npos);
	CHECK(p.item("condenser").place == "cabinet");
	p.drag("item:flask", "outbox"); // still joined
	CHECK(p.ui->message().find("joined") != std::string::npos);
	CHECK(p.item("flask").place == "bench");
	CHECK(p.ui->lab().digest() == before);
	p.drag("item:balloon", "square:3,4"); // off again, set down where it was dropped, held by its body
	CHECK(p.item("balloon").place == "bench");
	CHECK(p.item("balloon").col == 1);
	CHECK(p.ui->message().empty());
}

TEST_CASE("ui: the clock: pause, speeds, and one checked step in every frame (Stage 9)") {
	Player p;
	CHECK(p.ui->speed() == 1.0);
	p.ui->frame(0.5);
	CHECK(p.ui->lab().step() == 30);
	p.click("pause");
	CHECK(p.ui->paused());
	p.ui->frame(0.5);
	CHECK(p.ui->lab().step() == 30);
	p.click("speed:600"); // choosing a speed also resumes
	CHECK_FALSE(p.ui->paused());
	p.ui->frame(1.0 / 60.0);
	CHECK(p.ui->lab().step() == 630);
	p.ui->frame(10.0); // a long frame is capped at twenty simulated seconds
	CHECK(p.ui->lab().step() == 1830);
	p.click("check");
	p.ui->frame(1.0 / 60.0); // every step checked
	CHECK(p.ui->lab().step() == 2430);
	CHECK_FALSE(p.ui->lab().failed());
	p.click("reset");
	CHECK(p.ui->lab().step() == 0);
	CHECK(p.item("flask").place == "cabinet");
}

TEST_CASE("ui: the three stages of the demo, played with the pointer alone (Stage 9)") {
	Player p;
	// ---- stage 1: gas from malachite
	p.drag("item:flask", "square:11,12");
	p.drag("item:burner", "item:flask");
	CHECK(p.item("burner").place == "under");
	p.drag("item:jar", "square:4,13");
	p.drag("item:jar", "item:flask");
	p.click("panel:tip:malachite");
	CHECK(p.grams("flask", "malachite") == doctest::Approx(5.0).epsilon(1e-9));
	p.drag("item:balloon", "item:flask");
	p.click("valve:burner@1");
	CHECK(p.item("burner").valve == doctest::Approx(1.0).epsilon(0.02));
	p.click("speed:600");
	p.watch([&] { return p.grams("flask", "malachite") < 0.005; }, 1800);
	p.drag("item:balloon", "outbox"); // off the flask, sealed, and handed in
	CHECK(p.item("balloon").submitted);
	CHECK(p.item("flask").holds == -1);
	p.click("valve:burner@0");
	p.watch([&] { return p.item("flask").celsius < 30.0; }, 1800);

	// ---- stage 2: copper sulfate solution
	p.drag("item:acid", "square:7,12");
	p.drag("item:acid", "item:flask");
	p.click("panel:pour"); // the panel opens at 50 mL
	CHECK(p.item("flask").liquid_ml == doctest::Approx(50.0).epsilon(0.01));
	p.click("stir:flask");
	CHECK(p.item("flask").stirred);
	p.click("valve:burner@0.2");
	p.watch([&] { return p.grams("flask", "copper_oxide") < 0.001; }, 3600);
	p.click("stir:flask");
	CHECK_FALSE(p.item("flask").stirred);
	CHECK(p.grams("flask", "copper_sulfate_aq") > 7.0); // blue

	// ---- stage 3: water and crystals
	p.drag("item:condenser", "item:flask");
	p.drag("item:beaker", "square:19,12");
	p.drag("item:beaker", "item:condenser");
	CHECK(p.item("condenser").drains_to >= 0);
	p.click("valve:burner@1");
	p.watch([&] { return p.grams("flask", "water") < 10.0; }, 1800);
	p.click("valve:burner@0");
	p.watch([&] { return p.item("flask").celsius < 21.0; }, 3600);
	p.wait(900);
	p.drag("item:condenser", "square:23,3");
	p.drag("item:waste", "square:23,12");
	p.drag("item:flask", "item:waste");
	p.click("panel:slider@1");
	p.click("panel:pour");
	CHECK(p.item("flask").liquid_ml == 0.0);
	p.drag("item:beaker", "outbox");
	p.drag("item:flask", "outbox");
	CHECK(p.ui->message().empty());

	// ---- GAME_SPEC §17: done when
	const LabView view = p.ui->lab().view();
	CHECK(view.conserved);
	CHECK(view.events.empty());
	CHECK(p.item("balloon").submitted);
	CHECK(p.item("beaker").submitted);
	CHECK(p.item("flask").submitted);
	CHECK(p.grams("balloon", "carbon_dioxide") > 0.8);
	CHECK(p.grams("beaker", "water") > 35.0);
	for (const lab::SubstanceView &s : p.item("beaker").contents) {
		CHECK((s.species == "water" || s.species == "steam" || s.species == "nitrogen" || s.species == "oxygen" || s.species == "glass"));
	}
	const double crystals = p.grams("flask", "copper_sulfate_pentahydrate");
	CHECK(crystals > 8.0);
	CHECK(p.grams("flask", "copper_sulfate") == 0.0);
	CHECK(p.grams("flask", "sulfuric_acid_aq") == 0.0);
	CHECK(crystals / (crystals + p.grams("flask", "malachite") + p.grams("flask", "copper_oxide")) > 0.999);
	well_formed(p.ui->draw());
	MESSAGE("played with the pointer: " << p.grams("balloon", "carbon_dioxide") << " g of CO2, " << p.grams("beaker", "water") << " g of water, " << crystals
										<< " g of crystals, in " << view.step / 60 << " s");
}

TEST_CASE("ui: items are chosen by a click, a click with a key held, or a rectangle, and the chosen move together (Stage 13)") {
	Player p;
	p.click("pause");
	p.drag("item:flask", "square:11,12"); // [10, 8]
	p.drag("item:beaker", "square:19,12"); // [18, 8]
	p.drag("item:jar", "square:4,13"); // [3, 9]
	using V = std::vector<std::string>;
	p.click("item:flask");
	CHECK(p.ui->selection() == V{ "flask" });
	p.click_with("item:beaker", lab::Ui::ModShift);
	CHECK(p.ui->selection() == V{ "flask", "beaker" });
	CHECK(p.ui->selected() == "beaker");
	p.click_with("item:flask", lab::Ui::ModCtrl); // taken away again
	CHECK(p.ui->selection() == V{ "beaker" });
	p.click_with("item:jar", lab::Ui::ModMeta);
	CHECK(p.ui->selection() == V{ "beaker", "jar" });
	p.ui->key(lab::Ui::KeyEscape);
	CHECK(p.ui->selection().empty());
	// a rectangle from an empty square, across the flask's footprint and the beaker's, not the jar's
	float x0 = 0, y0 = 0, x1 = 0, y1 = 0;
	p.at("square:9,1", x0, y0);
	p.at("square:19,9", x1, y1);
	p.sweep(x0, y0, x1, y1);
	CHECK(p.ui->selection() == V{ "flask", "beaker" });
	p.click("square:1,1"); // a click on nothing lets go
	CHECK(p.ui->selection().empty());
	p.sweep(x0, y0, x1, y1);
	// carried together, a frame at a time: three squares up
	float bx = 0, by = 0;
	p.at("item:beaker", bx, by);
	p.sweep(bx, by, bx, by - 120);
	CHECK(p.item("beaker").col == 18);
	CHECK(p.item("beaker").row == 5);
	CHECK(p.item("flask").col == 10);
	CHECK(p.item("flask").row == 5);
	CHECK(p.ui->message().empty());
	// the flask would land on the jar: neither moves, and the reason is shown
	p.at("item:beaker", bx, by);
	p.sweep(bx, by, bx - 280, by + 120);
	CHECK(p.item("beaker").col == 18);
	CHECK(p.item("flask").col == 10);
	CHECK(p.ui->message().find("stands where \"flask\" would go") != std::string::npos);
	CHECK(p.ui->selection() == V{ "flask", "beaker" }); // still chosen
	// back to the cabinet together
	p.drag("item:flask", "cabinet");
	CHECK(p.item("flask").place == "cabinet");
	CHECK(p.item("beaker").place == "cabinet");
	CHECK(p.item("jar").place == "bench");
	well_formed(p.ui->draw());
}

TEST_CASE("ui: parts come out of the drawer, are set, wired and rigged with the pointer and the keys, and go back (Stage 13)") {
	Player p;
	p.click("pause");
	p.drag("item:flask", "square:11,12");
	p.drag("item:burner", "item:flask");
	p.drag("item:jar", "square:4,13");
	using V = std::vector<std::string>;
	const auto kinds = [&] {
		V out;
		for (const ItemView &v : p.ui->lab().view().items) {
			if (v.kind == "timer" || v.kind == "monitor" || v.kind == "alarm") {
				out.push_back(v.id);
			}
		}
		return out;
	};
	float x = 0, y = 0;
	// a monitor: its editor opens; the flask's temperature, between 30 and 40.5 C, typed and nudged
	p.drag("drawer:monitor", "square:1,1");
	CHECK(kinds() == V{ "monitor1" });
	CHECK(p.ui->selected() == "monitor1");
	p.click("edit:item:flask");
	p.click("edit:compare:between");
	p.click("edit:value:0");
	p.ui->key(lab::Ui::KeyText, '3');
	p.ui->key(lab::Ui::KeyText, '0');
	p.click("edit:value:1");
	for (const char c : std::string("40")) {
		p.ui->key(lab::Ui::KeyText, static_cast<uint32_t>(c));
	}
	p.click("edit:nudge:-1"); // the second value, focused: 40 + 0.1 * 5
	p.click("edit:nudge:1");
	p.click("edit:nudge:1");
	p.click("edit:nudge:1");
	p.click("edit:nudge:1");
	p.click("edit:nudge:1");
	p.click("edit:set");
	CHECK(p.ui->message().empty());
	ItemView m = p.item("monitor1");
	CHECK(m.part.watching);
	CHECK(m.part.quantity == lab::Quantity{ "flask", "celsius", "g" });
	CHECK(m.part.comparison.op == lab::Comparison::Between);
	CHECK(m.part.comparison.a == doctest::Approx(30.0));
	CHECK(m.part.comparison.b == doctest::Approx(40.4));
	CHECK_FALSE(p.ui->locate("edit:set", x, y)); // closed
	// edited again: a value that is not a number is refused, and the editor stays
	p.click("item:monitor1");
	p.click("part:edit");
	p.click("edit:value:0");
	p.ui->key(lab::Ui::KeyText, '-');
	p.ui->key(lab::Ui::KeyText, '.');
	p.ui->key(lab::Ui::KeyEnter);
	CHECK(p.ui->message().find("not a number") != std::string::npos);
	CHECK(p.ui->locate("edit:set", x, y));
	p.ui->key(lab::Ui::KeyEscape);
	CHECK_FALSE(p.ui->locate("edit:set", x, y));
	CHECK(p.item("monitor1").part.comparison.a == doctest::Approx(30.0)); // unchanged
	// a timer: typed, as hh:mm:ss.mmm or seconds
	p.drag("drawer:timer", "square:2,1");
	p.click("edit:value");
	for (const char c : std::string("0:01:30")) {
		p.ui->key(lab::Ui::KeyText, static_cast<uint32_t>(c));
	}
	p.click("edit:nudge:-1");
	p.click("edit:set");
	CHECK(p.item("timer1").part.preset == 89 * 60);
	// an alarm, and a wire into it; a wire onto a timer asks which of its inputs
	p.drag("drawer:alarm", "square:3,1");
	CHECK(kinds() == V{ "monitor1", "timer1", "alarm1" });
	CHECK_FALSE(p.ui->locate("edit:set", x, y)); // an alarm has nothing to set
	p.drag("port:monitor1", "item:alarm1");
	p.drag("port:monitor1", "item:timer1");
	p.click("choose:wire");
	CHECK(p.ui->lab().view().wires == std::vector<lab::Wire>{ { "monitor1", "alarm1" }, { "monitor1", "timer1" } });
	p.drag("port:timer1", "item:timer1"); // onto its own reset
	p.click("choose:reset");
	CHECK(p.ui->locate("edit:on:reset", x, y));
	p.click("edit:set");
	// a rig onto the burner's valve: on any change, 25 %
	p.drag("port:monitor1", "item:burner");
	p.click("edit:form:change");
	p.click("edit:change:value");
	p.ui->key(lab::Ui::KeyText, '2');
	p.ui->key(lab::Ui::KeyText, '0');
	p.click("edit:change:nudge:1");
	p.click("edit:change:nudge:1");
	p.click("edit:change:nudge:1");
	p.click("edit:change:nudge:1");
	p.click("edit:change:nudge:1");
	p.click("edit:set");
	// and onto the flask's stirring: stir when on, leave when off
	p.drag("port:monitor1", "item:flask");
	p.click("edit:off:leave");
	p.click("edit:set");
	const std::vector<lab::Rig> rigs = p.ui->lab().view().rigs;
	REQUIRE(rigs.size() == 3);
	CHECK(rigs[0].item == "timer1");
	CHECK(rigs[0].control == "reset");
	CHECK(rigs[1].change);
	CHECK(*rigs[1].any == doctest::Approx(0.25));
	CHECK(*rigs[2].on == 1.0);
	CHECK_FALSE(rigs[2].off.has_value());
	// a wire onto what has no control is refused with the reason
	p.drag("port:monitor1", "item:jar");
	CHECK(p.ui->locate("edit:set", x, y)); // a vessel: its stirring
	p.click("edit:cancel");
	// the readout takes wires and rigs off again
	p.click("item:monitor1");
	p.click("part:unrig:flask:stir");
	p.click("part:unwire:timer1");
	CHECK(p.ui->lab().view().rigs.size() == 2);
	CHECK(p.ui->lab().view().wires.size() == 1);
	// a part dropped on equipment is refused; dragged along the bench, it moves; to the drawer, it goes
	p.drag("item:alarm1", "item:jar");
	CHECK(p.ui->message().find("free square") != std::string::npos);
	p.drag("item:alarm1", "square:5,1");
	CHECK(p.item("alarm1").col == 5);
	p.drag("item:timer1", "cabinet");
	CHECK(kinds() == V{ "monitor1", "alarm1" });
	CHECK(p.ui->lab().view().rigs.size() == 1); // its reset rig went with it
	well_formed(p.ui->draw());
}

TEST_CASE("ui: a person boiling down at 600x: a monitor closes the valve at 10 g, and an alarm stops the clock and shows why (Stage 13)") {
	Player p;
	p.drag("item:flask", "square:11,12");
	p.drag("item:burner", "item:flask");
	p.drag("item:acid", "square:7,12");
	p.drag("item:acid", "item:flask");
	p.click("panel:pour");
	p.drag("drawer:monitor", "square:1,1");
	p.click("edit:item:flask");
	p.click("edit:quantity");
	p.click("edit:pick:water");
	p.click("edit:compare:below");
	p.click("edit:value:0");
	p.ui->key(lab::Ui::KeyText, '1');
	p.ui->key(lab::Ui::KeyText, '0');
	p.click("edit:set");
	p.drag("port:monitor1", "item:burner");
	p.click("edit:on:value");
	p.ui->key(lab::Ui::KeyText, '0');
	p.click("edit:set");
	p.drag("drawer:alarm", "square:3,1");
	p.drag("port:monitor1", "item:alarm1");
	CHECK(p.ui->message().empty());
	p.click("valve:burner@1");
	p.click("speed:600");
	// a person: watching the screen, doing nothing
	for (int frame = 0; !p.ui->paused(); ++frame) {
		REQUIRE_MESSAGE(frame < 60 * 60, "the alarm never rang");
		p.ui->frame(1.0 / 60.0);
	}
	CHECK(p.grams("flask", "water") < 10.0);
	CHECK(p.grams("flask", "water") > 9.9);
	CHECK(p.item("burner").valve == 0.0);
	CHECK(p.ui->message().find("alarm1 rang: monitor1, flask: H2O(l) (g) below 10.000 g") != std::string::npos);
	CHECK(p.ui->selected() == "flask"); // what the monitor watched is shown
	const std::string at_ring = p.ui->lab().digest();
	p.ui->frame(1.0); // paused: nothing moves
	CHECK(p.ui->lab().digest() == at_ring);
	p.click("speed:600"); // playing on answers it
	CHECK(p.ui->lab().view().ringing.empty());
	CHECK_FALSE(p.ui->paused());
	p.ui->frame(1.0);
	CHECK(p.item("burner").valve == 0.0); // and the valve stays as the rig left it
	MESSAGE("the alarm rang at " << p.grams("flask", "water") << " g of water");
}

TEST_CASE("ui: chosen parts become a circuit, opened to work inside, nested, and given back (Stage 14)") {
	Player p;
	p.click("pause");
	float x = 0, y = 0, x1 = 0, y1 = 0;
	const auto make = [&](const char *kind, const char *square) {
		p.drag(std::string("drawer:") + kind, square);
		if (p.ui->locate("edit:cancel", x, y)) {
			p.click("edit:cancel");
		}
	};
	make("timer", "square:2,1");
	make("alarm", "square:4,2");
	make("monitor", "square:9,1");
	p.drag("port:timer1", "item:alarm1");
	CHECK(p.ui->locate("circuit:create", x, y)); // the part just made is chosen
	p.ui->key(lab::Ui::KeyEscape);
	CHECK_FALSE(p.ui->locate("circuit:create", x, y)); // nothing chosen
	// a rectangle around the timer and the alarm
	p.at("square:1,0", x, y);
	p.at("square:5,3", x1, y1);
	p.sweep(x, y, x1, y1);
	CHECK(p.ui->selection() == std::vector<std::string>{ "timer1", "alarm1" });
	p.click("circuit:create");
	CHECK(p.ui->message().empty());
	CHECK(p.ui->selected() == "circuit1");
	CHECK(p.item("circuit1").col == 2);
	CHECK_FALSE(p.ui->locate("item:timer1", x, y)); // inside it, not on the bench
	CHECK(p.ui->locate("item:circuit1", x, y));
	// opened: what is in it is on the screen, and worked on there
	p.click("part:open");
	REQUIRE(p.ui->locate("item:timer1", x, y));
	p.drag("item:timer1", "inner:4,3");
	CHECK(p.item("timer1").col == 4);
	CHECK(p.item("timer1").row == 3);
	p.drag("drawer:monitor", "inner:0,3"); // out of the drawer, into it
	CHECK(p.item("monitor2").part.in == "circuit1");
	p.click("edit:cancel");
	p.drag("port:timer1", "item:monitor1"); // a wire out of it, to the bench
	CHECK(p.ui->lab().view().wires.size() == 2);
	p.drag("item:alarm1", "square:12,1"); // out of the panel: it stays in
	CHECK(p.ui->message().find("stays in it") != std::string::npos);
	CHECK(p.item("alarm1").part.in == "circuit1");
	p.click("circuit:close");
	CHECK_FALSE(p.ui->locate("item:alarm1", x, y));
	// the timer at zero rings the alarm in the circuit: the circuit is what is shown
	p.click("speed:60");
	for (int frame = 0; !p.ui->paused(); ++frame) {
		REQUIRE_MESSAGE(frame < 60 * 30, "the alarm in the circuit never rang");
		p.ui->frame(1.0 / 60.0);
	}
	CHECK(p.ui->message().find("alarm1 rang: timer1 at 00:00:00.000") != std::string::npos);
	p.click("pause"); // answered
	// nested: the circuit and the monitor into another
	p.click("item:circuit1");
	p.click_with("item:monitor1", lab::Ui::ModShift);
	p.click("circuit:create");
	CHECK(p.item("circuit1").part.in == "circuit2");
	p.click("part:open");
	p.click("item:circuit1"); // inside the outer one
	p.click("part:open");
	CHECK(p.ui->locate("item:timer1", x, y));
	CHECK(p.ui->locate("circuit:up", x, y));
	p.click("circuit:up");
	CHECK(p.ui->locate("item:circuit1", x, y));
	CHECK_FALSE(p.ui->locate("item:timer1", x, y));
	p.click("circuit:close");
	// given back, from the readout
	p.click("item:circuit2");
	p.click("part:give_back");
	CHECK(p.item("circuit1").place == "bench");
	CHECK(p.item("monitor1").place == "bench");
	p.click("item:circuit1");
	p.click("part:give_back");
	CHECK(p.item("timer1").place == "bench");
	CHECK(p.item("alarm1").place == "bench");
	CHECK(p.item("monitor2").place == "bench");
	well_formed(p.ui->draw());
}
