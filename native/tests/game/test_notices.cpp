// Notices (GAME_SPEC §13.4): what the lab says happened, at a level the player
// may silence; and the notice part, which hears them.
#include "lab/lab.hpp"
#include "lab/ui.hpp"

#include <doctest/doctest.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <fstream>
#include <sstream>
#include <string>

using lab::Notice;

namespace {

std::string read(const std::string &relative) {
	std::ifstream in(std::string(CHEM_REPO_ROOT) + "/" + relative);
	REQUIRE_MESSAGE(static_cast<bool>(in), relative);
	std::ostringstream out;
	out << in.rdbuf();
	return out.str();
}

std::unique_ptr<lab::Lab> open(const char *file = "data/benches/demo.json") {
	std::string why;
	auto made = lab::Lab::open(read(file), why);
	REQUIRE_MESSAGE(made != nullptr, why);
	return made;
}

void must(lab::Lab &l, const std::string &command) {
	std::string why;
	REQUIRE_MESSAGE(l.command(command, why), command << ": " << why);
}

int count(const lab::Lab &l, Notice::Level level, const std::string &part) {
	int n = 0;
	for (const Notice &x : l.view().notices) {
		n += x.level == level && x.says.find(part) != std::string::npos ? 1 : 0;
	}
	return n;
}

std::string all(const lab::Lab &l) {
	std::string out;
	for (const Notice &x : l.view().notices) {
		out += std::to_string(x.step) + " " + lab::level_name(x.level) + " " + x.says + "\n";
	}
	return out;
}

} // namespace

TEST_CASE("notices: the demo, as the lab tells it: each thing once, at its level, what a hand moved not news (Stage 16)") {
	auto l = open();
	l->set_checking(false);
	CHECK(l->view().notices.empty()); // what is there from the start is not news
	const nlohmann::json session = nlohmann::json::parse(read("data/sessions/demo.json"));
	for (nlohmann::json c : session.at("script")) {
		c.erase("note");
		must(*l, c.dump());
	}
	INFO(all(*l));
	CHECK(count(*l, Notice::Event, "the malachite is breaking down") == 1);
	CHECK(count(*l, Notice::Event, "CuO(s) appeared in flask") == 1);
	CHECK(count(*l, Notice::Detail, "flask: malachite decomposition stopped") == 1);
	CHECK(count(*l, Notice::Event, "the copper oxide dissolves") == 1);
	CHECK(count(*l, Notice::Event, "CuO(s) is used up in flask") == 1);
	CHECK(count(*l, Notice::Event, "flask boils") == 1);
	CHECK(count(*l, Notice::Event, "flask stopped boiling") == 1);
	CHECK(count(*l, Notice::Event, "blue copper sulfate crystals are growing") == 1);
	CHECK(count(*l, Notice::Event, "CuSO4.5H2O(s) appeared in flask") == 1);
	// poured and tipped by hand: no news
	CHECK(count(*l, Notice::Event, "Cu2CO3(OH)2(s) appeared") == 0);
	CHECK(count(*l, Notice::Event, "H2SO4(aq) appeared") == 0);
	// nothing went wrong
	CHECK(count(*l, Notice::Warning, "") == 0);
	CHECK(count(*l, Notice::Danger, "") == 0);
	// in order, and of the step they happened in
	const std::vector<Notice> n = l->view().notices;
	CHECK(std::is_sorted(n.begin(), n.end(), [](const Notice &a, const Notice &b) { return a.step < b.step; }));
	CHECK(nlohmann::json::parse(l->report()).at("notices").size() == n.size());
}

TEST_CASE("notices: warnings and dangers: a balloon nearly full, then bursting; a flask boiled dry; a spill into the tray (Stage 16)") {
	// the balloon: malachite heated hard under it, a small balloon
	std::string why;
	auto l = lab::Lab::open(R"({"format": "chem-bench/2", "room": {"celsius": 20}, "items": [
		{"id": "flask", "at": [0, 0], "kind": "vessel", "volume_ml": 250, "wall": "glass", "wall_g": 100, "loss": 0.9, "contents": {"malachite": {"g": 5, "particles": 2500000}}},
		{"id": "bag", "kind": "balloon", "wall": "latex", "wall_g": 2, "loss": 0.5, "capacity_ml": 450},
		{"id": "flame", "kind": "burner", "celsius": 1927, "conductance": 0.25}]})",
			why);
	REQUIRE_MESSAGE(l != nullptr, why);
	l->set_checking(false);
	for (const char *c : { R"({"do": "heat", "burner": "flame", "vessel": "flask"})", R"({"do": "attach", "item": "bag", "to": "flask"})",
				 R"({"do": "valve", "burner": "flame", "to": 1})" }) {
		must(*l, c);
	}
	for (int i = 0; i < 400 && count(*l, Notice::Danger, "burst") == 0; ++i) {
		REQUIRE(l->advance(60, why));
	}
	INFO(all(*l));
	CHECK(count(*l, Notice::Warning, "bag is 8") == 1); // 80% and more: once
	CHECK(count(*l, Notice::Danger, "bag burst") == 1);
	// a flask with a little water, boiled until it is dry
	auto d = open();
	d->set_checking(false);
	for (const char *c : { R"({"do": "pour", "from": "acid", "to": "flask", "ml": 5})", R"({"do": "heat", "burner": "burner", "vessel": "flask"})",
				 R"({"do": "valve", "burner": "burner", "to": 1})" }) {
		must(*d, c);
	}
	for (int i = 0; i < 600 && count(*d, Notice::Warning, "boiled dry") == 0; ++i) {
		REQUIRE(d->advance(60, why));
	}
	INFO(all(*d));
	CHECK(count(*d, Notice::Event, "flask boils") == 1);
	CHECK(count(*d, Notice::Warning, "flask boiled dry") == 1);
	// a cup nearly full under a condenser that holds water: what drips in spills over
	auto s = lab::Lab::open(R"({"format": "chem-bench/2", "room": {"celsius": 20}, "items": [
		{"id": "tube", "at": [0, 0], "kind": "vessel", "volume_ml": 50, "wall": "glass", "wall_g": 50, "loss": 0.1, "coolant": {"celsius": 15, "conductance": 10}, "contents": {"water": {"g": 20}}},
		{"id": "cup", "at": [3, 0], "kind": "vessel", "volume_ml": 50, "wall": "glass", "wall_g": 20, "loss": 0.3, "contents": {"water": {"g": 45}}}]})",
			why);
	REQUIRE_MESSAGE(s != nullptr, why);
	s->set_checking(false);
	must(*s, R"({"do": "under", "item": "cup", "of": "tube"})");
	REQUIRE(s->advance(600, why));
	INFO(all(*s));
	CHECK(count(*s, Notice::Warning, "spilled into the tray") >= 1);
}

TEST_CASE("notices: what the signals did, and a notice part that hears warnings rings an alarm (Stage 16)") {
	auto l = open();
	l->set_checking(false);
	for (const char *c : { R"({"do": "heat", "burner": "burner", "vessel": "flask"})", R"({"do": "part", "kind": "timer", "at": [0, 0]})",
				 R"({"do": "set", "part": "timer1", "seconds": 1})", R"({"do": "rig", "from": "timer1", "item": "burner", "control": "valve", "on": 0.5})",
				 R"({"do": "part", "kind": "notice", "at": [1, 0]})", R"({"do": "set", "part": "notice1", "level": "warning"})",
				 R"({"do": "part", "kind": "alarm", "at": [2, 0]})", R"({"do": "wire", "from": "notice1", "to": "alarm1"})" }) {
		must(*l, c);
	}
	std::string why;
	REQUIRE(l->advance(62, why));
	INFO(all(*l));
	CHECK(count(*l, Notice::Detail, "timer1 reached zero") == 1);
	CHECK(count(*l, Notice::Detail, "timer1 set the valve of burner to 50%") == 1);
	CHECK(l->view().ringing.empty()); // details are below a warning
	CHECK_FALSE(l->command(R"({"do": "set", "part": "notice1", "level": "loud"})", why));
	CHECK(why.find("detail, event, warning or danger") != std::string::npos);
	CHECK_FALSE(l->command(R"({"do": "set", "part": "notice1", "level": "event", "item": "kettle"})", why));
	// a notice part that hears events about the flask: the malachite, tipped in and heated, breaking down
	must(*l, R"({"do": "set", "part": "notice1", "level": "event", "item": "flask"})");
	must(*l, R"({"do": "tip", "from": "jar", "to": "flask", "species": "malachite", "all": true})");
	must(*l, R"({"do": "valve", "burner": "burner", "to": 1})");
	for (int i = 0; i < 300 && l->view().ringing.empty(); ++i) {
		REQUIRE(l->advance(60, why, true));
	}
	REQUIRE(l->view().ringing == std::vector<std::string>{ "alarm1" });
	CHECK(count(*l, Notice::Event, "alarm1 rang") == 1); // in the step it rang
	CHECK(count(*l, Notice::Event, "the malachite is breaking down") == 1);
}

TEST_CASE("notices: a lab saved and loaded writes the same notices as one that was not (Stage 16)") {
	auto l = open();
	l->set_checking(false);
	for (const char *c : { R"({"do": "heat", "burner": "burner", "vessel": "flask"})", R"({"do": "tip", "from": "jar", "to": "flask", "species": "malachite", "all": true})",
				 R"({"do": "valve", "burner": "burner", "to": 1})" }) {
		must(*l, c);
	}
	std::string why;
	REQUIRE(l->advance(60 * 50 + 7, why)); // mid-reading
	auto again = lab::Lab::load(l->save(), why);
	REQUIRE_MESSAGE(again != nullptr, why);
	again->set_checking(false);
	CHECK(again->view().notices == l->view().notices);
	for (int i = 0; i < 30; ++i) {
		REQUIRE(l->advance(60 * 10, why));
		REQUIRE(again->advance(60 * 10, why));
	}
	CHECK(again->view().notices == l->view().notices);
	CHECK(again->digest() == l->digest());
	CHECK(count(*l, Notice::Detail, "malachite decomposition stopped") == 1);
}

TEST_CASE("notices: on the screen: the newest heard by the clock, the panel with a switch for each level, a notice part set out of the drawer (Stage 16)") {
	std::string why;
	auto ui = lab::Ui::open(read("data/benches/demo.json"), why);
	REQUIRE(ui != nullptr);
	const auto at = [&](const std::string &what, float &x, float &y) { REQUIRE_MESSAGE(ui->locate(what, x, y), what); };
	const auto click = [&](const std::string &what) {
		float x = 0, y = 0;
		at(what, x, y);
		ui->pointer(x, y, true);
		ui->pointer(x, y, false);
	};
	const auto drag = [&](const std::string &a, const std::string &b) {
		float x0 = 0, y0 = 0, x1 = 0, y1 = 0;
		at(a, x0, y0);
		at(b, x1, y1);
		ui->pointer(x0, y0, true);
		ui->pointer(x1, y1, true);
		ui->pointer(x1, y1, false);
	};
	const auto drawn = [&](const std::string &text) { return ui->draw().text.find(text) != std::string::npos; };
	for (const char *c : { R"({"do": "heat", "burner": "burner", "vessel": "flask"})", R"({"do": "tip", "from": "jar", "to": "flask", "species": "malachite", "all": true})",
				 R"({"do": "valve", "burner": "burner", "to": 1})" }) {
		REQUIRE(ui->lab().command(c, why));
	}
	click("speed:600");
	for (int i = 0; i < 600 && count(ui->lab(), Notice::Event, "the malachite is breaking down") == 0; ++i) {
		ui->frame(1.0 / 60.0);
	}
	click("pause");
	const std::vector<Notice> heard = ui->lab().view().notices;
	REQUIRE_FALSE(heard.empty());
	std::string newest; // the newest at a level shown (details are not, at first)
	for (const Notice &n : heard) {
		newest = n.level != Notice::Detail ? n.says : newest;
	}
	CHECK(drawn(newest.substr(0, 40))); // by the clock
	float x = 0, y = 0;
	CHECK_FALSE(ui->locate("notices:event", x, y));
	click("notices");
	REQUIRE(ui->locate("notices:event", x, y));
	CHECK(drawn("event (off)") == false);
	CHECK(drawn("detail (off)")); // details start silenced
	click("notices:event"); // silenced: still kept
	CHECK(drawn("event (off)"));
	CHECK_FALSE(drawn("the malachite is breaking down"));
	CHECK(count(ui->lab(), Notice::Event, "the malachite is breaking down") == 1);
	click("notices:event");
	CHECK(drawn("the malachite is breaking down"));
	click("notices:close");
	CHECK_FALSE(ui->locate("notices:close", x, y));
	// a notice part out of the drawer: its level and its item
	drag("drawer:notice", "square:0,0");
	click("edit:level:danger");
	click("edit:item:flask");
	click("edit:set");
	const lab::LabView v = ui->lab().view();
	const auto part = std::find_if(v.items.begin(), v.items.end(), [](const lab::ItemView &i) { return i.id == "notice1"; });
	REQUIRE(part != v.items.end());
	CHECK(part->part.notice_level == 3);
	CHECK(part->part.quantity.item == "flask");
}
