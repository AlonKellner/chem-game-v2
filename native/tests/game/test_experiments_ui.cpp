// The experiments' list (GAME_SPEC §13.7, Stage 78): one column of rows scrolled a whole row at a time, clipped to its
// panel, and a search field over it that filters the rows as a query is typed (lab/search.hpp's matching).
#include "lab/play.hpp"
#include "lab/search.hpp"
#include "lab/ui.hpp"

#include <doctest/doctest.h>
#include <nlohmann/json.hpp>

#include "pupil.hpp"

#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

using pupil::Pupil;
using pupil::read;

namespace {

// the catalogue's entries, as data/experiments.json lists them
struct Entry {
	std::string id, title, summary;
};
std::vector<Entry> entries() {
	std::vector<Entry> out;
	const nlohmann::json doc = nlohmann::json::parse(read("data/experiments.json"));
	for (const nlohmann::json &e : doc["experiments"]) {
		out.push_back(Entry{ e["id"].get<std::string>(), e["title"].get<std::string>(), e.value("summary", "") });
	}
	return out;
}

// the ids a query leaves, by search.hpp's matching on each one's id, title and summary
std::set<std::string> left_by(const std::string &query) {
	std::set<std::string> out;
	for (const Entry &e : entries()) {
		std::vector<std::string> words;
		for (const std::string &t : { e.id, e.title, e.summary }) {
			for (const std::string &w : lab::search::words(t)) {
				words.push_back(w);
			}
		}
		if (lab::search::matches(query, words)) {
			out.insert(e.id);
		}
	}
	return out;
}

// the demo with the catalogue, as a host opens it
void with_catalogue(Pupil &p) {
	std::string why;
	REQUIRE_MESSAGE(p.ui->experiments(read("data/experiments.json"), why, [](const std::string &path) -> std::optional<std::string> { return read("data/" + path); }), why);
}

// the ids whose rows are on the screen now
std::set<std::string> in_sight(Pupil &p) {
	std::set<std::string> out;
	float x = 0, y = 0;
	for (const Entry &e : entries()) {
		if (p.ui->locate("experiment:" + e.id, x, y)) {
			out.insert(e.id);
		}
	}
	return out;
}

// each text drawn: its string and its baseline's start
struct Drawn {
	std::string s;
	float x = 0, y = 0;
};
std::vector<Drawn> texts(Pupil &p) {
	const lab::DrawList &list = p.ui->draw();
	std::vector<Drawn> out;
	for (const lab::DrawCommand &c : list.commands) {
		if (c.type == lab::DrawCommand::Text) {
			out.push_back(Drawn{ std::string(list.text.c_str() + c.first), c.x, c.y });
		}
	}
	return out;
}

void wheel(Pupil &p, float factor, int notches = 1) {
	float x = 0, y = 0;
	REQUIRE(p.ui->locate("experiments:scroll", x, y));
	for (int i = 0; i < notches; ++i) {
		p.ui->zoom(x, y, factor);
	}
}

void key(Pupil &p, lab::Ui::Key k) {
	p.ui->key(k);
}

// the list's panel, from its top to its foot (what is drawn of it lies within)
constexpr float PANEL_TOP = 64, PANEL_FOOT = 64 + 120 + 84 * 10 + 8;

} // namespace

TEST_CASE("experiments' list: one column, ten rows in sight, the wheel and a trackpad scroll it a whole row at a time, its bar is dragged, its rows are clipped to it (Stage 78)") {
	Pupil p;
	with_catalogue(p);
	const std::vector<Entry> all = entries();
	REQUIRE(all.size() > 10);
	p.click("menu:experiments");
	// as it opens: the first ten in sight (the demo, shown, among them), the rest not, none drawn
	std::set<std::string> first;
	for (size_t k = 0; k < 10; ++k) {
		first.insert(all[k].id);
	}
	CHECK(in_sight(p) == first);
	const auto titles_drawn = [&]() {
		std::set<std::string> out;
		for (const Drawn &d : texts(p)) {
			for (const Entry &e : all) {
				if (d.s == e.title) {
					out.insert(e.id);
					CHECK(d.y > PANEL_TOP);
					CHECK(d.y < PANEL_FOOT);
				}
			}
		}
		return out;
	};
	CHECK(titles_drawn() == first);
	CHECK(p.drawn(std::to_string(all.size()) + " of " + std::to_string(all.size())));
	float x = 0, y = 0, bar_y = 0;
	REQUIRE(p.ui->locate("experiments:bar", x, bar_y));
	// the wheel: a notch down, a row down; the room's view stays as it was
	const lab::View before = p.ui->view();
	wheel(p, 0.8f);
	CHECK_FALSE(p.ui->locate("experiment:" + all[0].id, x, y));
	REQUIRE(p.ui->locate("experiment:" + all[10].id, x, y));
	CHECK(y < PANEL_FOOT);
	CHECK(p.ui->view() == before);
	REQUIRE(p.ui->locate("experiments:bar", x, y));
	CHECK(y > bar_y);
	// to its end and no further: the last in sight, at its foot within the panel
	wheel(p, 0.8f, 100);
	std::set<std::string> last;
	for (size_t k = all.size() - 10; k < all.size(); ++k) {
		last.insert(all[k].id);
	}
	CHECK(in_sight(p) == last);
	CHECK(titles_drawn() == last);
	// up a notch, a row up
	wheel(p, 1.25f);
	CHECK(p.ui->locate("experiment:" + all[all.size() - 11].id, x, y));
	CHECK_FALSE(p.ui->locate("experiment:" + all.back().id, x, y));
	// its bar dragged to its top: the first ten again
	float bx = 0, by = 0, tx = 0, ty = 0;
	REQUIRE(p.ui->locate("experiments:bar", bx, by));
	REQUIRE(p.ui->locate("experiments:bar@0", tx, ty));
	p.ui->pointer(bx, by, false);
	p.ui->pointer(bx, by, true);
	for (int i = 1; i <= 10; ++i) {
		p.ui->pointer(bx, by + (ty - by) * static_cast<float>(i) / 10.0f, true);
	}
	p.ui->pointer(tx, ty, false);
	CHECK(in_sight(p) == first);
	// a trackpad's two fingers: the rows follow them, a whole row for each row's height
	p.ui->pan(0, -84.0f * 3 - 20);
	CHECK(p.ui->locate("experiment:" + all[12].id, x, y));
	CHECK_FALSE(p.ui->locate("experiment:" + all[2].id, x, y));
	p.ui->pan(0, 84.0f * 3 + 20);
	CHECK(in_sight(p) == first);
	// the list still open after all of it, and a press beside it closes it
	CHECK(p.ui->locate("experiments:search", x, y));
	p.ui->pointer(30, 1000, false);
	p.ui->pointer(30, 1000, true);
	p.ui->pointer(30, 1000, false);
	CHECK(in_sight(p).empty());
	// opened again with another shown far down: that one in sight
	std::string why;
	REQUIRE_MESSAGE(p.ui->choose(all.back().id, why), why);
	p.click("menu:experiments");
	CHECK(in_sight(p).count(all.back().id) == 1);
	CHECK(in_sight(p).size() == 10);
}

TEST_CASE("experiments' list: typing filters its rows live, a backspace and Escape bring them back, and a row left is chosen (Stage 78)") {
	Pupil p;
	with_catalogue(p);
	const size_t n = entries().size();
	p.click("menu:experiments");
	float x = 0, y = 0;
	// the field takes the keys while the list is open
	p.type("liquid mercury"); // (Stage 85: "mercury" alone finds the discharge lamps' spectra too)
	CHECK(left_by("liquid mercury") == std::set<std::string>{ "mercury" });
	CHECK(in_sight(p) == left_by("liquid mercury"));
	CHECK(p.drawn("liquid mercury_"));
	CHECK(p.drawn("1 of " + std::to_string(n)));
	CHECK_FALSE(p.ui->locate("experiments:bar", x, y)); // (every row left in sight)
	// a backspace a character, Escape the whole query: every row again
	for (int i = 0; i < 14; ++i) {
		key(p, lab::Ui::KeyBackspace);
	}
	CHECK(p.drawn(std::to_string(n) + " of " + std::to_string(n)));
	p.type("titr");
	const std::set<std::string> titr = left_by("titr");
	REQUIRE(titr.size() > 3);
	CHECK(titr.count("titration") == 1);
	CHECK(titr.count("titration_curve") == 1);
	CHECK(titr.count("demo") == 0);
	CHECK(in_sight(p) == titr); // (fewer than ten: all of them, from the top)
	// a word more narrows it; any case, any order
	p.type(" CURVE");
	CHECK(in_sight(p) == left_by("titr curve"));
	CHECK(in_sight(p).count("titration_curve") == 1);
	CHECK(in_sight(p).size() < titr.size());
	key(p, lab::Ui::KeyEscape);
	CHECK(p.ui->locate("experiments:search", x, y)); // (cleared, still open)
	CHECK(in_sight(p).size() == 10);
	CHECK(p.drawn(std::to_string(n) + " of " + std::to_string(n)));
	// a row the query leaves is chosen with the pointer: it opens, the list closes and lets its query go
	p.type("seed");
	REQUIRE(in_sight(p).count("seed") == 1);
	p.click("experiment:seed");
	CHECK(p.ui->message().empty());
	CHECK(p.ui->experiment() == "seed");
	CHECK_FALSE(p.ui->locate("experiments:search", x, y));
	p.click("menu:experiments");
	CHECK_FALSE(p.drawn("seed_"));
	CHECK(p.drawn(std::to_string(n) + " of " + std::to_string(n)));
	// Enter opens the first row left; Escape with no query closes the list
	p.type("vinegar");
	key(p, lab::Ui::KeyEnter);
	CHECK(p.ui->experiment() == "vinegar");
	p.click("menu:experiments");
	key(p, lab::Ui::KeyEscape);
	CHECK_FALSE(p.ui->locate("experiments:search", x, y));
	// closed, the keys are not the list's: the demo's own fields take them as before
	CHECK(p.ui->experiment() == "vinegar");
}

TEST_CASE("experiments' list: a query nothing matches says so, and none of its rows is drawn or taken (Stage 78)") {
	Pupil p;
	with_catalogue(p);
	p.click("menu:experiments");
	p.type("zzqx");
	REQUIRE(left_by("zzqx").empty());
	CHECK(p.drawn("no experiments match \"zzqx\""));
	CHECK(in_sight(p).empty());
	float x = 0, y = 0;
	CHECK_FALSE(p.ui->locate("experiments:bar", x, y));
	CHECK(p.ui->locate("experiments:scroll", x, y));
	// a press where the rows were does nothing, and Enter opens none
	const std::string shown = p.ui->experiment();
	p.ui->pointer(x, y, false);
	p.ui->pointer(x, y, true);
	p.ui->pointer(x, y, false);
	key(p, lab::Ui::KeyEnter);
	CHECK(p.ui->experiment() == shown);
	CHECK(p.ui->locate("experiments:search", x, y));
	key(p, lab::Ui::KeyEscape);
	CHECK_FALSE(p.drawn("no experiments match"));
	CHECK(in_sight(p).size() == 10);
}

TEST_CASE("experiments' list: its names on the screen while it is open; a row out of sight is not located, and a play or a pupil scrolls to it (Stage 78)") {
	Pupil p;
	with_catalogue(p);
	float x = 0, y = 0;
	for (const char *name : { "experiments:search", "experiments:scroll", "experiments:bar", "experiments:bar@0.5", "experiment:demo" }) {
		CHECK_FALSE(p.ui->locate(name, x, y));
	}
	p.click("menu:experiments");
	for (const char *name : { "experiments:search", "experiments:scroll", "experiments:bar", "experiments:bar@0.5", "experiment:demo" }) {
		CHECK_MESSAGE(p.ui->locate(name, x, y), name);
	}
	CHECK_FALSE(p.ui->locate("experiments:bar@2", x, y));
	CHECK_FALSE(p.ui->locate("experiment:free_lab", x, y)); // (the last, out of sight)
	CHECK_FALSE(p.ui->locate("experiment:nothing", x, y));
	// scroll_to: the wheel turned over the list until it is in sight, down or up
	CHECK(lab::scroll_to(*p.ui, "experiment:free_lab"));
	CHECK(p.ui->locate("experiment:free_lab", x, y));
	CHECK_FALSE(p.ui->locate("experiment:demo", x, y));
	CHECK(lab::scroll_to(*p.ui, "experiment:demo"));
	CHECK(p.ui->locate("experiment:demo", x, y));
	CHECK_FALSE(lab::scroll_to(*p.ui, "experiment:nothing"));
	p.type("mercury");
	CHECK_FALSE(lab::scroll_to(*p.ui, "experiment:demo")); // (left by the search)
	key(p, lab::Ui::KeyEscape);
	key(p, lab::Ui::KeyEscape);
	CHECK_FALSE(lab::scroll_to(*p.ui, "experiment:free_lab")); // (the list closed)
	// a play names the last row as the ones before it named theirs, and reaches it
	lab::Play play;
	std::string why;
	REQUIRE_MESSAGE(lab::Play::read(R"({"format": "chem-play/1", "steps": [{"to": "menu:experiments", "button": "click"}, {"to": "experiment:free_lab", "button": "click"}]})", play, why), why);
	while (play.next(*p.ui)) {
		p.ui->frame(1.0 / 60.0);
	}
	CHECK_MESSAGE(play.missed().empty(), play.missed());
	CHECK(p.ui->experiment() == "free_lab");
}

TEST_CASE("experiments' list: played at a person's pace, opened, scrolled a notch at a time, its bar dragged, a query typed a character a frame or so, a row it leaves chosen (Stage 78)") {
	Pupil p;
	with_catalogue(p);
	lab::Play play;
	std::string why;
	REQUIRE_MESSAGE(lab::Play::read(read("data/plays/experiments.json"), play, why), why);
	std::map<std::string, int> frames;
	std::map<std::string, std::set<std::string>> seen; // what was in sight at each picture
	for (int i = 0; i < 2000 && play.next(*p.ui); ++i) {
		p.ui->frame(1.0 / 60.0);
		++frames[play.mark()];
		if (!play.shot().empty()) {
			seen[play.shot()] = in_sight(p);
		}
		if (play.mark() == "typing") {
			CHECK(in_sight(p).size() <= 10);
		}
	}
	CHECK(play.over());
	CHECK_MESSAGE(play.missed().empty(), "not on the screen when its step came: " << play.missed());
	CHECK_MESSAGE(play.refused().empty(), play.refused());
	CHECK(frames["wheel"] == 12);
	CHECK(frames["typing"] == 56);
	const std::vector<Entry> all = entries();
	CHECK(seen["experiments-0-open"].count("demo") == 1);
	std::set<std::string> twelfth; // twelve notches: twelve rows down
	for (size_t k = 12; k < 22; ++k) {
		twelfth.insert(all[k].id);
	}
	CHECK(seen["experiments-1-scrolled"] == twelfth);
	CHECK(seen["experiments-2-back"] == seen["experiments-0-open"]);
	CHECK(seen["experiments-3-titr"] == left_by("titr"));
	CHECK(seen["experiments-4-titr-cu"] == left_by("titr cu"));
	CHECK(p.ui->experiment() == "titration_curve");
	CHECK(p.ui->tutorial() != nullptr);
	CHECK(seen["experiments-5-chosen"].empty()); // (the list closed)
}
