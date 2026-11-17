// The player's timers (GAME_SPEC §13.3): a time limit and value conditions, read
// after every quarter of a lab-second; the first to hold pauses the clock and
// shows the item. Written and armed here with the pointer and the keys alone.
#include "lab/timers.hpp"
#include "lab/ui.hpp"

#include <doctest/doctest.h>

#include <fstream>
#include <sstream>
#include <string>

using lab::Comparison;
using lab::ItemView;
using lab::LabView;
using lab::Quantity;

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
	bool person = false; // a second and a half before each gesture, the clock running

	explicit Player(const char *bench = "data/benches/demo.json") {
		std::string why;
		ui = lab::Ui::open(read(bench), why);
		REQUIRE_MESSAGE(ui != nullptr, why);
	}
	void frames(int n) {
		for (int i = 0; i < n; ++i) {
			ui->frame(1.0 / 60.0);
		}
	}
	void at(const std::string &what, float &x, float &y) { REQUIRE_MESSAGE(ui->locate(what, x, y), "not on the screen: " << what); }
	void click(const std::string &what) {
		float x = 0, y = 0;
		at(what, x, y);
		if (person) {
			frames(90);
		}
		ui->pointer(x, y, false);
		ui->pointer(x, y, true);
		ui->pointer(x, y, false);
	}
	void drag(const std::string &what, const std::string &to) {
		float x0 = 0, y0 = 0, x1 = 0, y1 = 0;
		at(what, x0, y0);
		at(to, x1, y1);
		if (person) {
			frames(90);
		}
		ui->pointer(x0, y0, false);
		ui->pointer(x0, y0, true);
		ui->pointer((x0 + x1) / 2, (y0 + y1) / 2, true);
		ui->pointer(x1, y1, true);
		ui->pointer(x1, y1, false);
	}
	void type(const std::string &text) {
		for (const char c : text) {
			ui->key(lab::Ui::KeyText, static_cast<uint32_t>(c));
			if (person) {
				frames(12);
			}
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
		double g = 0.0;
		for (const lab::SubstanceView &s : item(id).contents) {
			g += s.species == species ? s.g : 0.0;
		}
		return g;
	}
	template <typename Done>
	void watch(Done done, double seconds) {
		const int64_t until = ui->lab().step() + static_cast<int64_t>(seconds * 60.0);
		while (!done()) {
			REQUIRE_MESSAGE(ui->lab().step() < until, "waited too long");
			ui->frame(1.0 / 60.0);
			REQUIRE_MESSAGE(!ui->lab().failed(), ui->message());
		}
	}
	// a timer on the selected item's first condition (its temperature, at first)
	void timer_on(const std::string &item, const char *compare, const std::string &value, bool limited) {
		click("item:" + item);
		click("timer:add");
		click(std::string("timer:compare:0:") + compare);
		click("timer:value:0:0");
		type(value);
		if (!limited) {
			click("timer:limit:toggle");
		}
		click("timer:arm");
		REQUIRE_MESSAGE(ui->message().empty(), ui->message());
	}
};

ItemView flask(double celsius, double liquid_ml, double water_g) {
	ItemView v;
	v.id = "flask";
	v.kind = "vessel";
	v.place = "bench";
	v.celsius = celsius;
	v.liquid_ml = liquid_ml;
	v.contents = { { "water", "H2O", 'l', false, water_g / 18.015, water_g, 0, 0.0 }, { "copper_sulfate_pentahydrate", "CuSO4.5H2O", 's', false, 0.01, 2.5, 2000, 120.0 } };
	return v;
}

LabView view_at(int64_t step, std::initializer_list<ItemView> items) {
	LabView v;
	v.step = step;
	v.items = items;
	return v;
}

lab::TimerDraft draft_of(std::initializer_list<std::pair<lab::TimerCondition, std::array<std::string, 2>>> conditions) {
	lab::TimerDraft d;
	d.item = "flask";
	for (const auto &[c, text] : conditions) {
		d.conditions.push_back(c);
		d.text.push_back(text);
	}
	return d;
}

lab::TimerCondition on(const char *what, Comparison::Op op, const char *measure = "g") {
	return lab::TimerCondition{ Quantity{ "flask", what, measure }, Comparison{ op, 0.0, 0.0 } };
}

} // namespace

TEST_CASE("timers: quantities are measured from the lab's view, and compared (Stage 13)") {
	const LabView v = view_at(0, { flask(80.0, 12.0, 11.5) });
	CHECK(*lab::measure(v, Quantity{ "flask", "celsius", "g" }) == 80.0);
	CHECK(*lab::measure(v, Quantity{ "flask", "liquid_ml", "g" }) == 12.0);
	CHECK(*lab::measure(v, Quantity{ "flask", "water", "g" }) == 11.5);
	CHECK(*lab::measure(v, Quantity{ "flask", "copper_sulfate_pentahydrate", "count" }) == 2000.0);
	CHECK(*lab::measure(v, Quantity{ "flask", "copper_sulfate_pentahydrate", "size" }) == 120.0);
	CHECK(*lab::measure(v, Quantity{ "flask", "malachite", "g" }) == 0.0); // not held: none
	CHECK_FALSE(lab::measure(v, Quantity{ "beaker", "celsius", "g" })); // no such item
	CHECK_FALSE(lab::measure(v, Quantity{ "flask", "valve", "g" })); // a vessel has no valve
	CHECK(lab::quantities_of(v.items[0]).size() == 4 + 2);
	CHECK(lab::label(v, Quantity{ "flask", "water", "g" }) == "flask: H2O(l) (g)");
	CHECK(lab::shown(Quantity{ "flask", "copper_sulfate_pentahydrate", "count" }, 447472.0) == "447,472");

	using C = Comparison;
	CHECK(lab::meets(C{ C::Above, 10, 0 }, 10.5, std::nullopt));
	CHECK_FALSE(lab::meets(C{ C::Above, 10, 0 }, 10.0, std::nullopt));
	CHECK(lab::meets(C{ C::AtLeast, 10, 0 }, 10.0, std::nullopt));
	CHECK(lab::meets(C{ C::Below, 10, 0 }, 9.99, std::nullopt));
	CHECK(lab::meets(C{ C::AtMost, 10, 0 }, 10.0, std::nullopt));
	CHECK(lab::meets(C{ C::Between, 5, 10 }, 5.0, std::nullopt));
	CHECK_FALSE(lab::meets(C{ C::Between, 5, 10 }, 10.01, std::nullopt));
	CHECK(lab::meets(C{ C::Outside, 5, 10 }, 4.99, std::nullopt));
	CHECK_FALSE(lab::meets(C{ C::Outside, 5, 10 }, 7.0, std::nullopt));
	// reaches: crossed between two readings, from either side, or touched
	CHECK(lab::meets(C{ C::Reaches, 50, 0 }, 51.0, 49.0));
	CHECK(lab::meets(C{ C::Reaches, 50, 0 }, 49.0, 51.0));
	CHECK(lab::meets(C{ C::Reaches, 50, 0 }, 50.0, 40.0));
	CHECK_FALSE(lab::meets(C{ C::Reaches, 50, 0 }, 49.0, 40.0));
	CHECK_FALSE(lab::meets(C{ C::Reaches, 50, 0 }, 49.0, std::nullopt));
}

TEST_CASE("timers: any or all, the time limit, one-shot, re-armed and removed (Stage 13)") {
	using C = Comparison;
	std::string why;
	lab::Timers timers;
	// hot and still full: "any" fires on the heat, "all" waits for the water too
	REQUIRE(timers.arm(draft_of({ { on("celsius", C::Above), { "50", "" } }, { on("water", C::Below), { "10", "" } } }), view_at(0, { flask(20, 30, 30) }), why));
	lab::TimerDraft all = draft_of({ { on("celsius", C::Above), { "50", "" } }, { on("water", C::Below), { "10", "" } } });
	all.all = true;
	REQUIRE(timers.arm(all, view_at(0, { flask(20, 30, 30) }), why));
	CHECK(timers.poll(view_at(15, { flask(40, 30, 30) })) == -1);
	CHECK(timers.poll(view_at(30, { flask(60, 30, 30) })) == 0);
	CHECK(timers.list()[0].fired);
	CHECK(timers.list()[0].fired_by.find("above 50.0 C") != std::string::npos);
	CHECK(timers.list()[0].item_fired == "flask");
	CHECK(timers.poll(view_at(45, { flask(60, 30, 30) })) == -1); // one-shot: it stays fired
	CHECK(timers.poll(view_at(60, { flask(100, 9, 9) })) == 1);
	CHECK(timers.list()[1].fired_by.find("all its conditions") == 0);
	// re-armed: its time limit starts again from now
	timers.rearm(0, view_at(60, { flask(20, 30, 30) }));
	CHECK_FALSE(timers.list()[0].fired);
	CHECK(timers.list()[0].armed_at_s == 1.0);
	timers.remove(1);
	CHECK(timers.list().size() == 1);
	CHECK(timers.armed());

	// the time limit alone, from when it was armed
	lab::Timers clock;
	lab::TimerDraft limit;
	limit.limit_text = "0:30";
	REQUIRE(clock.arm(limit, view_at(600, {}), why));
	CHECK(clock.poll(view_at(600 + 29 * 60 + 45, {})) == -1);
	CHECK(clock.poll(view_at(600 + 30 * 60, {})) == 0);
	CHECK(clock.list()[0].fired_by == "0:30 passed");
	CHECK_FALSE(clock.armed());
	// and whichever comes first, when it has conditions too
	lab::TimerDraft both = draft_of({ { on("water", C::Below), { "10", "" } } });
	both.limit_text = "1:00";
	REQUIRE(clock.arm(both, view_at(0, { flask(100, 30, 30) }), why));
	CHECK(clock.poll(view_at(3600, { flask(100, 25, 25) })) == 1);
	CHECK(clock.list()[1].fired_by == "1:00 passed");

	// what cannot be armed
	lab::TimerDraft bare;
	bare.limited = false;
	CHECK_FALSE(clock.arm(bare, view_at(0, {}), why));
	CHECK(why.find("needs its time limit") != std::string::npos);
	CHECK_FALSE(clock.arm(draft_of({ { on("water", C::Below), { "ten", "" } } }), view_at(0, {}), why));
	CHECK(why.find("not a number") != std::string::npos);
	CHECK_FALSE(clock.arm(draft_of({ { on("water", C::Between), { "10", "5" } } }), view_at(0, {}), why));
	CHECK(why.find("low end is above the high end") != std::string::npos);
	lab::TimerDraft bad_limit;
	bad_limit.limit_text = "1:75";
	CHECK_FALSE(clock.arm(bad_limit, view_at(0, {}), why));
	for (int i = static_cast<int>(clock.list().size()); i < lab::Timers::MAX_TIMERS; ++i) {
		REQUIRE(clock.arm(limit, view_at(0, {}), why));
	}
	CHECK_FALSE(clock.arm(limit, view_at(0, {}), why));
	CHECK(why.find("five timers") != std::string::npos);
	double s = 0;
	CHECK(lab::Timers::seconds_of("1:30.5", s));
	CHECK(s == 90.5);
	CHECK(lab::Timers::seconds_of("90", s));
	CHECK(lab::Timers::clock_of(300) == "5:00");
}

TEST_CASE("timers: written with the pointer and the keys, a timer stops the clock within a quarter lab-second, the lab as it would be (Stage 13)") {
	Player p;
	p.drag("item:burner", "item:flask");
	p.click("valve:burner@1");
	p.click("item:flask");
	CHECK(p.ui->selected() == "flask");
	// the editor: the flask's temperature, at first, prefilled with its value now; the clock waits
	CHECK_FALSE(p.ui->paused());
	p.click("timer:add");
	CHECK(p.ui->paused());
	float x = 0, y = 0;
	CHECK(p.ui->locate("timer:value:0:0", x, y));
	CHECK_FALSE(p.ui->locate("timer:value:0:1", x, y)); // one value for "below"
	p.click("timer:compare:0:between");
	CHECK(p.ui->locate("timer:value:0:1", x, y));
	p.click("timer:value:0:0");
	p.type("80");
	p.click("timer:value:0:1");
	p.type("70");
	p.click("timer:arm");
	CHECK(p.ui->message().find("low end is above the high end") != std::string::npos);
	p.click("timer:compare:0:above");
	p.click("timer:value:0:0");
	p.type("5x0"); // only what a number is made of is taken
	p.ui->key(lab::Ui::KeyBackspace);
	p.type("0");
	p.click("timer:nudge:0:2"); // +1 C
	p.click("timer:nudge:0:-1"); // -0.1 C
	p.click("timer:limit:toggle");
	p.ui->key(lab::Ui::KeyEnter); // arms, and the clock plays on as it was
	REQUIRE(p.ui->message().empty());
	CHECK_FALSE(p.ui->paused());
	REQUIRE(p.ui->timers().list().size() == 1);
	const lab::Timer &t = p.ui->timers().list()[0];
	CHECK(t.conditions[0].comparison.op == Comparison::Above);
	CHECK(t.conditions[0].comparison.a == doctest::Approx(50.9));
	CHECK_FALSE(t.limited);
	CHECK(p.ui->lab().step() == 0);

	p.click("speed:600");
	p.watch([&] { return p.ui->paused(); }, 1800);
	const int64_t stopped = p.ui->lab().step();
	CHECK(p.item("flask").celsius > 50.9);
	CHECK(p.ui->timers().list()[0].fired);
	CHECK(p.ui->message().find("timer 1 fired") == 0);
	CHECK(p.ui->selected() == "flask"); // shown in the readout, outlined
	p.frames(120); // a person looking: the clock stands
	CHECK(p.ui->lab().step() == stopped);
	CHECK(stopped % lab::Timers::READ_STEPS == 0);

	// the same lab without a timer: the quantity crossed within the last quarter, and the
	// state is the same, digest for digest
	std::string why;
	std::unique_ptr<lab::Lab> plain = lab::Lab::open(read("data/benches/demo.json"), why);
	REQUIRE(plain->command(R"({"do": "heat", "burner": "burner", "vessel": "flask"})", why));
	REQUIRE(plain->command(R"({"do": "valve", "burner": "burner", "to": 1})", why));
	plain->set_checking(false);
	REQUIRE(plain->advance(stopped - lab::Timers::READ_STEPS, why));
	for (const ItemView &v : plain->view().items) {
		if (v.id == "flask") {
			CHECK(v.celsius <= 50.9);
		}
	}
	REQUIRE(plain->advance(lab::Timers::READ_STEPS, why));
	CHECK(plain->digest() == p.ui->lab().digest());

	// played on, re-armed, removed
	p.click("pause");
	CHECK_FALSE(p.ui->paused());
	p.click("timer:row:0:rearm");
	CHECK_FALSE(p.ui->timers().list()[0].fired);
	p.ui->frame(1.0 / 60.0); // already above: it fires at its first reading
	CHECK(p.ui->paused());
	CHECK(p.ui->lab().step() == stopped + lab::Timers::READ_STEPS);
	p.click("timer:row:0:remove");
	CHECK(p.ui->timers().list().empty());
	// a time limit alone: no value condition
	p.click("item:flask");
	p.click("timer:add");
	p.click("timer:remove:0");
	p.click("timer:limit:toggle");
	p.click("timer:arm");
	CHECK(p.ui->message().find("needs its time limit") != std::string::npos);
	p.click("timer:limit:toggle");
	p.click("timer:limit:value");
	p.type("0:20");
	p.click("timer:arm");
	REQUIRE(p.ui->message().empty());
	const int64_t armed = p.ui->lab().step();
	p.click("pause");
	p.watch([&] { return p.ui->paused(); }, 60);
	CHECK(p.ui->lab().step() == armed + 20 * 60);
	CHECK(p.ui->timers().list()[0].fired_by == "0:20 passed");
	// escape leaves the editor without a timer
	p.click("item:flask");
	p.click("timer:add");
	p.ui->key(lab::Ui::KeyEscape);
	CHECK_FALSE(p.ui->locate("timer:arm", x, y));
	CHECK(p.ui->timers().list().size() == 1);
}

TEST_CASE("timers: a person boiling down at 600x stops at 10 g with a timer, and boils past it without one (Stage 13)") {
	for (const bool with_timer : { true, false }) {
		CAPTURE(with_timer);
		Player p("data/benches/lab.json");
		// stages 1 and 2, quickly
		p.drag("item:flask", "square:11,12");
		p.drag("item:burner", "item:flask");
		p.drag("item:jar", "square:4,13");
		p.drag("item:jar", "item:flask");
		p.click("panel:tip:malachite");
		p.drag("item:balloon", "item:flask");
		p.click("valve:burner@1");
		p.click("speed:600");
		p.watch([&] { return p.grams("flask", "malachite") < 0.005; }, 1800);
		p.drag("item:balloon", "outbox");
		p.click("valve:burner@0");
		p.watch([&] { return p.item("flask").celsius < 30.0; }, 1800);
		p.drag("item:acid", "square:7,12");
		p.drag("item:acid", "item:flask");
		p.click("panel:pour");
		p.click("stir:flask");
		p.click("valve:burner@0.2");
		p.watch([&] { return p.grams("flask", "copper_oxide") < 0.001; }, 3600);
		p.click("stir:flask");
		p.drag("item:condenser", "item:flask");
		p.drag("item:beaker", "square:19,12");
		p.drag("item:beaker", "item:condenser");
		// stage 3, as a person: paused while setting up, then the flame full and the clock at 600x
		p.click("pause");
		p.person = true;
		if (with_timer) {
			p.click("item:flask");
			p.click("timer:add");
			p.click("timer:quantity:0");
			p.click("timer:pick:water");
			p.click("timer:compare:0:below");
			p.click("timer:value:0:0");
			p.type("10");
			p.click("timer:limit:toggle");
			p.click("timer:arm");
			REQUIRE(p.ui->message().empty());
			REQUIRE(p.ui->timers().list().size() == 1);
			const lab::Timer &t = p.ui->timers().list()[0];
			REQUIRE(t.conditions.size() == 1);
			CHECK(t.conditions[0].quantity.what == "water");
			CHECK(t.conditions[0].comparison.op == Comparison::Below);
			CHECK(t.conditions[0].comparison.a == 10.0);
			CHECK_FALSE(t.limited);
			CHECK_FALSE(t.fired);
		}
		p.click("valve:burner@1");
		p.click("speed:600");
		// the person watches the readout every half second
		p.watch([&] {
			p.frames(30);
			return p.ui->paused() || p.grams("flask", "water") < 10.0;
		}, 1800);
		p.click("valve:burner@0"); // a second and a half later
		const double left = p.grams("flask", "water");
		MESSAGE(std::string(with_timer ? "with a timer: " : "without one: ") << left << " g of water left when the flame went out; " << (p.ui->timers().list().empty() ? std::string("no timer") : p.ui->timers().list()[0].fired_by));
		if (with_timer) {
			CHECK(left < 10.0);
			CHECK(left > 9.9); // a quarter of a lab-second at full flame
		} else {
			CHECK(left < 5.0); // boiled far past it
		}
	}
}
