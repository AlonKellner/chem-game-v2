// Goals (GAME_SPEC §13.5): the outbox's six slots, and what should be handed in
// to them; met live, noticed, read by monitors and conditions, saved, not digested.
#include "lab/conditions.hpp"
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

// two cups of water and a burner; with `goals`, a game goal of warm water in slot 1
std::string warm_bench(bool goals) {
	nlohmann::json b = nlohmann::json::parse(R"({"format": "chem-bench/2", "room": {"celsius": 20}, "items": [
		{"id": "cup", "at": [0, 0], "kind": "vessel", "volume_ml": 50, "wall": "glass", "wall_g": 20, "loss": 0.3, "contents": {"water": {"g": 20}}},
		{"id": "cold", "at": [3, 0], "kind": "vessel", "volume_ml": 50, "wall": "glass", "wall_g": 20, "loss": 0.3, "contents": {"water": {"g": 20}}},
		{"id": "burner", "at": [6, 0], "kind": "burner", "celsius": 1927, "conductance": 0.25}]})");
	if (goals) {
		b["goals"] = nlohmann::json::parse(R"([{"id": "warm", "name": "warm water", "slot": 1, "terms": [
			{"quantity": {"what": "water", "measure": "g"}, "compare": "at_least", "value": 10},
			{"quantity": {"what": "celsius"}, "compare": "above", "value": 40}]}])");
	}
	return b.dump();
}

std::unique_ptr<lab::Lab> open_text(const std::string &text) {
	std::string why;
	auto made = lab::Lab::open(text, why);
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
	CHECK_FALSE_MESSAGE(l.command(command, why), command);
	CHECK(l.digest() == before);
	return why;
}

int count(const lab::Lab &l, Notice::Level level, const std::string &says) {
	int n = 0;
	for (const Notice &x : l.view().notices) {
		n += x.level == level && x.says == says ? 1 : 0;
	}
	return n;
}

const lab::ItemView &item(const lab::LabView &v, const std::string &id) {
	const auto i = std::find_if(v.items.begin(), v.items.end(), [&](const lab::ItemView &x) { return x.id == id; });
	REQUIRE(i != v.items.end());
	return *i;
}

bool holding(const lab::Cond &c, const lab::LabView &v) {
	return lab::holds(c, lab::CondContext{ v, 1.0, false, "", nullptr });
}

const lab::Goal *goal(const lab::LabView &v, int slot) {
	for (const lab::Goal &g : v.goals) {
		if (g.slot == slot) {
			return &g;
		}
	}
	return nullptr;
}

} // namespace

TEST_CASE("goals: the bench's goal is met live while what is handed in meets every term; becoming met and no longer met are notices (Stage 17)") {
	auto l = open_text(warm_bench(true));
	REQUIRE(l->view().goals.size() == 1);
	CHECK(l->view().goals[0].game);
	CHECK_FALSE(l->view().goals[0].met);
	must(*l, R"({"do": "heat", "burner": "burner", "vessel": "cup"})");
	must(*l, R"({"do": "valve", "burner": "burner", "to": 1})");
	must(*l, R"({"do": "wait_until", "item": "cup", "above_c": 60, "timeout": 600})");
	CHECK_FALSE(l->view().goals[0].met); // nothing handed in
	// with no slot, the first free one without a goal
	must(*l, R"({"do": "submit", "item": "cold"})");
	CHECK(item(l->view(), "cold").slot == 2);
	CHECK_FALSE(l->view().goals[0].met);
	must(*l, R"({"do": "submit", "item": "cup", "slot": 1})");
	CHECK(item(l->view(), "cup").slot == 1);
	CHECK(l->view().goals[0].met);
	CHECK(count(*l, Notice::Event, "goal met: warm water") == 1); // at once
	CHECK(l->view().notices.back().item == "warm");
	lab::Cond c;
	c.goal = "warm";
	CHECK(holding(c, l->view()));
	lab::Cond all;
	all.goals_met = true;
	CHECK(holding(all, l->view()));
	// what is handed in still changes: it cools below the term
	std::string why;
	REQUIRE(l->advance(60 * 600, why));
	CHECK(item(l->view(), "cup").celsius < 40.0);
	CHECK_FALSE(l->view().goals[0].met);
	CHECK(count(*l, Notice::Warning, "goal no longer met: warm water") == 1);
	CHECK_FALSE(holding(c, l->view()));
	CHECK_FALSE(holding(all, l->view()));
}

TEST_CASE("goals: the player's goals are made, changed and taken off; the game's are locked; refusals with the reason (Stage 17)") {
	auto l = open_text(read("data/benches/lab.json"));
	const lab::LabView v = l->view();
	REQUIRE(v.goals.size() == 3);
	CHECK(goal(v, 1)->id == "co2");
	CHECK(goal(v, 2)->id == "water");
	CHECK(goal(v, 3)->id == "crystals");
	const std::string term = R"([{"quantity": {"what": "water", "measure": "g"}, "compare": "at_least", "value": 1}])";
	CHECK(refused(*l, R"({"do": "goal", "slot": 1, "name": "mine", "terms": )" + term + "}").find("game's goal") != std::string::npos);
	CHECK(refused(*l, R"({"do": "ungoal", "slot": 2})").find("cannot be changed") != std::string::npos);
	must(*l, R"({"do": "goal", "slot": 4, "name": "some water", "terms": )" + term + "}");
	REQUIRE(goal(l->view(), 4) != nullptr);
	CHECK(goal(l->view(), 4)->id == "goal1");
	CHECK_FALSE(goal(l->view(), 4)->game);
	must(*l, R"({"do": "goal", "slot": 4, "name": "more water", "terms": )" + term + "}");
	CHECK(goal(l->view(), 4)->id == "goal1"); // changed, the same goal
	CHECK(goal(l->view(), 4)->name == "more water");
	must(*l, R"({"do": "ungoal", "slot": 4})");
	CHECK(goal(l->view(), 4) == nullptr);
	must(*l, R"({"do": "goal", "slot": 5, "name": "again", "terms": )" + term + "}");
	CHECK(goal(l->view(), 5)->id == "goal2"); // never used again
	CHECK(refused(*l, R"({"do": "ungoal", "slot": 6})").find("no goal") != std::string::npos);
	CHECK(refused(*l, R"({"do": "goal", "slot": 7, "name": "x", "terms": )" + term + "}").find("1 to 6") != std::string::npos);
	CHECK(refused(*l, R"({"do": "goal", "slot": 6, "terms": )" + term + "}").find("\"name\"") != std::string::npos);
	CHECK(refused(*l, R"({"do": "goal", "slot": 6, "name": "x", "terms": []})").find("\"terms\"") != std::string::npos);
	CHECK_FALSE(refused(*l, R"({"do": "goal", "slot": 6, "name": "x", "terms": [{"quantity": {"what": "water"}, "compare": "reaches", "value": 1}]})").empty());
	CHECK(refused(*l, R"({"do": "goal", "slot": 6, "name": "x", "terms": [{"quantity": {"what": "kettle"}, "compare": "above", "value": 1}]})").find("kettle") != std::string::npos);
	CHECK_FALSE(refused(*l, R"({"do": "goal", "slot": 6, "name": "x", "terms": [{"quantity": {"what": "met"}, "compare": "above", "value": 0}]})").empty());
	CHECK_FALSE(refused(*l, R"({"do": "goal", "slot": 6, "name": "x", "terms": [{"quantity": {"what": "water", "measure": "furlongs"}, "compare": "above", "value": 0}]})").empty());
	CHECK(refused(*l, R"({"do": "goal", "slot": 6, "name": "x", "terms": [{"quantity": {"what": "water"}, "compare": "between", "value": 1}]})").find("\"values\"") != std::string::npos);
	// a bench whose goal is not one
	std::string why;
	nlohmann::json bad = nlohmann::json::parse(warm_bench(true));
	bad["goals"][0]["slot"] = 0;
	CHECK(lab::Lab::open(bad.dump(), why) == nullptr);
	CHECK(why.find("warm") != std::string::npos);
}

TEST_CASE("goals: handing in to a slot: one item to a slot; with no slot, the free ones without a goal first; a full outbox refuses (Stage 17)") {
	nlohmann::json b = nlohmann::json::parse(R"({"format": "chem-bench/2", "room": {"celsius": 20}, "items": []})");
	for (int k = 0; k < 7; ++k) {
		b["items"].push_back({ { "id", "cup" + std::to_string(k) }, { "at", { 2 * k, 0 } }, { "kind", "vessel" }, { "volume_ml", 50 }, { "wall", "glass" }, { "wall_g", 20 }, { "loss", 0.3 } });
	}
	b["goals"] = nlohmann::json::parse(R"([{"id": "a", "name": "a", "slot": 2, "terms": [{"quantity": {"what": "water"}, "compare": "above", "value": 1}]},
		{"id": "b", "name": "b", "slot": 5, "terms": [{"quantity": {"what": "water"}, "compare": "above", "value": 1}]}])");
	auto l = open_text(b.dump());
	must(*l, R"({"do": "submit", "item": "cup0", "slot": 5})");
	CHECK(refused(*l, R"({"do": "submit", "item": "cup1", "slot": 5})").find("slot 5 holds \"cup0\"") != std::string::npos);
	for (const int expected : { 1, 3, 4, 6, 2 }) { // without a goal first, then the rest
		const std::string cup = "cup" + std::to_string(expected == 1 ? 1 : expected == 3 ? 2 : expected == 4 ? 3 : expected == 6 ? 4 : 5);
		must(*l, R"({"do": "submit", "item": ")" + cup + "\"}");
		CHECK(item(l->view(), cup).slot == expected);
	}
	CHECK(refused(*l, R"({"do": "submit", "item": "cup6"})").find("outbox is full") != std::string::npos);
	CHECK(refused(*l, R"({"do": "submit", "item": "cup6", "slot": 0})").find("1 to 6") != std::string::npos);
}

TEST_CASE("goals: a monitor reads a goal's met, a notice part hears one goal; saved and loaded the same, and not in the digest (Stage 17)") {
	auto l = open_text(warm_bench(true));
	for (const char *c : { R"({"do": "part", "kind": "monitor", "at": [10, 0]})", R"({"do": "set", "part": "monitor1", "item": "warm", "what": "met", "compare": "at_least", "value": 1})",
				 R"({"do": "part", "kind": "alarm", "at": [11, 0]})", R"({"do": "wire", "from": "monitor1", "to": "alarm1"})", R"({"do": "part", "kind": "notice", "at": [12, 0]})",
				 R"({"do": "set", "part": "notice1", "level": "event", "item": "warm"})", R"({"do": "part", "kind": "alarm", "at": [13, 0]})",
				 R"({"do": "wire", "from": "notice1", "to": "alarm2"})", R"({"do": "heat", "burner": "burner", "vessel": "cup"})", R"({"do": "valve", "burner": "burner", "to": 1})",
				 R"({"do": "wait_until", "item": "cup", "above_c": 60, "timeout": 600})" }) {
		must(*l, c);
	}
	CHECK(refused(*l, R"({"do": "set", "part": "monitor1", "item": "nothing", "what": "met", "compare": "at_least", "value": 1})").find("no goal") != std::string::npos);
	CHECK(l->view().ringing.empty());
	must(*l, R"({"do": "submit", "item": "cup", "slot": 1})");
	std::string why;
	REQUIRE(l->advance(10, why));
	std::vector<std::string> ringing = l->view().ringing;
	std::sort(ringing.begin(), ringing.end());
	CHECK(ringing == std::vector<std::string>{ "alarm1", "alarm2" });
	// saved and loaded: the same goals, slots and notices, and digests on
	auto again = lab::Lab::load(l->save(), why);
	REQUIRE_MESSAGE(again != nullptr, why);
	CHECK(again->view().goals == l->view().goals);
	CHECK(item(again->view(), "cup").slot == 1);
	REQUIRE(l->advance(60 * 400, why));
	REQUIRE(again->advance(60 * 400, why));
	CHECK(again->digest() == l->digest());
	CHECK(again->view().notices == l->view().notices);
	CHECK(again->view().goals == l->view().goals);
	// goals are not state: the same play on the same bench without them digests the same
	auto with = open_text(warm_bench(true)), without = open_text(warm_bench(false));
	for (const char *c : { R"({"do": "heat", "burner": "burner", "vessel": "cup"})", R"({"do": "valve", "burner": "burner", "to": 1})", R"({"do": "wait", "seconds": 30})",
				 R"({"do": "submit", "item": "cup", "slot": 1})", R"({"do": "submit", "item": "cold"})", R"({"do": "wait", "seconds": 30})" }) {
		must(*with, c);
		must(*without, c);
		CHECK(with->digest() == without->digest());
	}
}

TEST_CASE("goals: on the screen: a slot for each, a drop onto a slot, the player's goal made, changed and taken off in the editor (Stage 17)") {
	std::string why;
	auto ui = lab::Ui::open(read("data/benches/lab.json"), why);
	REQUIRE_MESSAGE(ui != nullptr, why);
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
	const auto type = [&](const std::u32string &text) {
		for (const char32_t ch : text) {
			ui->key(lab::Ui::KeyText, static_cast<uint32_t>(ch));
		}
	};
	const auto drawn = [&](const std::string &text) { return ui->draw().text.find(text) != std::string::npos; };
	CHECK(drawn("carbon dioxide")); // the game goal's name, in its slot
	float x = 0, y = 0;
	CHECK_FALSE(ui->locate("goal:1", x, y)); // the game's: locked
	// a goal of the player's, in slot 4: two terms
	click("goal:4");
	click("edit:name");
	type(U"salty");
	click("edit:term:0:what");
	click("edit:pick:copper_sulfate_aq");
	click("edit:term:0:measure"); // g, then purity
	click("edit:term:0:compare"); // at least, then at most
	click("edit:term:0:value");
	type(U"50");
	click("edit:term:add");
	click("edit:term:1:value");
	type(U"3");
	click("edit:set");
	const lab::LabView made = ui->lab().view();
	const lab::Goal *g = goal(made, 4);
	REQUIRE(g != nullptr);
	CHECK(g->name == "salty");
	REQUIRE(g->terms.size() == 2);
	CHECK(g->terms[0].quantity == lab::Quantity{ "", "copper_sulfate_aq", "purity" });
	CHECK(g->terms[0].comparison == lab::Comparison{ lab::Comparison::AtMost, 50.0, 0.0 });
	CHECK(g->terms[1].quantity == lab::Quantity{ "", "water", "g" });
	CHECK(g->terms[1].comparison.a == 3.0);
	CHECK(drawn("salty"));
	// changed: a term taken off
	click("goal:4");
	click("edit:term:1:remove");
	click("edit:set");
	CHECK(goal(ui->lab().view(), 4)->terms.size() == 1);
	// taken off
	click("goal:4");
	click("edit:remove");
	CHECK(goal(ui->lab().view(), 4) == nullptr);
	// a drop onto a slot hands in there; onto the outbox, the first free slot without a goal
	REQUIRE(ui->lab().command(R"({"do": "place", "item": "beaker", "at": [2, 2]})", why));
	REQUIRE(ui->lab().command(R"({"do": "place", "item": "waste", "at": [8, 2]})", why));
	drag("item:beaker", "slot:5");
	CHECK(item(ui->lab().view(), "beaker").slot == 5);
	drag("item:waste", "outbox");
	CHECK(item(ui->lab().view(), "waste").slot == 4);
	// a notice part hears one goal, by its id
	drag("drawer:notice", "square:0,0");
	click("edit:level:event");
	click("edit:item:crystals");
	click("edit:set");
	CHECK(item(ui->lab().view(), "notice1").part.quantity.item == "crystals");
}
