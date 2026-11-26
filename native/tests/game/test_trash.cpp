// The trash (GAME_SPEC §13, Stage 34): what is put away to be restored. An item
// in it stays in the world, as one in the cabinet does; a signal part in it
// keeps its settings and signals nothing.
#include "lab/lab.hpp"

#include <doctest/doctest.h>

#include <algorithm>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace {

std::string read(const std::string &relative) {
	std::ifstream in(std::string(CHEM_REPO_ROOT) + "/" + relative);
	REQUIRE_MESSAGE(static_cast<bool>(in), relative);
	std::ostringstream out;
	out << in.rdbuf();
	return out.str();
}

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

// refused, with the state as it was
std::string refused(lab::Lab &l, const std::string &command) {
	std::string why;
	const std::string before = l.digest();
	CHECK_MESSAGE(!l.command(command, why), command);
	CHECK(l.digest() == before);
	return why;
}

bool says(const std::string &text, const char *part) {
	return text.find(part) != std::string::npos;
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

bool there(const lab::LabView &v, const std::string &id) {
	return std::any_of(v.items.begin(), v.items.end(), [&](const lab::ItemView &i) { return i.id == id; });
}

// the trash's elements by name, oldest first
std::vector<std::string> trashed(const lab::LabView &v) {
	std::vector<std::string> out;
	for (const int index : v.trash) {
		REQUIRE(index >= 0);
		REQUIRE(index < static_cast<int>(v.items.size()));
		CHECK(v.items[static_cast<size_t>(index)].place == "trash");
		out.push_back(v.items[static_cast<size_t>(index)].id);
	}
	return out;
}

double grams(const lab::LabView &v, const std::string &id, const std::string &species) {
	double g = 0.0;
	for (const lab::SubstanceView &s : item(v, id).contents) {
		g += s.species == species ? s.g : 0.0;
	}
	return g;
}

bool noticed(const lab::LabView &v, const char *text) {
	return std::any_of(v.notices.begin(), v.notices.end(), [&](const lab::Notice &n) { return says(n.says, text); });
}

using Names = std::vector<std::string>;

} // namespace

TEST_CASE("trash: a vessel with liquid goes in and comes back to its square, the world conserved and running as it would have (Stage 34)") {
	auto l = bench(), twin = bench(); // every step checked against every invariant
	const lab::LabView start = l->view();
	const double water = grams(start, "acid", "water");
	REQUIRE(water > 100.0);
	CHECK(start.trash.empty());

	must(*l, R"({"do": "trash", "item": "acid"})");
	lab::LabView v = l->view();
	CHECK(item(v, "acid").place == "trash");
	CHECK(item(v, "acid").col == 2); // the square it goes back to
	CHECK(item(v, "acid").row == 8);
	CHECK(trashed(v) == Names{ "acid" });
	CHECK(v.conserved);
	CHECK(v.mass_g == start.mass_g);
	CHECK(v.energy_j == start.energy_j);
	CHECK(grams(v, "acid", "water") == water); // what it holds is in the world
	CHECK(v.items.size() == start.items.size());

	// in the trash it takes no command but those that take it out
	CHECK(says(refused(*l, R"({"do": "trash", "item": "acid"})"), "already in the trash"));
	for (const char *c : { R"({"do": "pour", "from": "acid", "to": "flask", "ml": 10})", R"({"do": "pour", "from": "jar", "to": "acid", "all": true})",
				 R"({"do": "move", "items": ["flask", "acid"], "by": [0, -1]})", R"({"do": "submit", "item": "acid"})", R"({"do": "stir", "vessel": "acid"})",
				 R"({"do": "heat", "burner": "burner", "vessel": "acid"})", R"({"do": "hold", "item": "acid", "over": "flask"})", R"({"do": "attach", "item": "balloon", "to": "acid"})",
				 R"({"do": "under", "item": "acid", "of": "condenser"})", R"({"do": "cap", "item": "acid", "to": 0})" }) {
		CHECK_MESSAGE(says(refused(*l, c), "\"acid\" is in the trash: restore it first"), c);
	}
	// its square is free for another
	must(*l, R"({"do": "place", "item": "jar", "at": [2, 8]})");
	must(*l, R"({"do": "place", "item": "jar", "at": [0, 9]})");

	must(*l, R"({"do": "wait", "seconds": 2})");
	must(*twin, R"({"do": "wait", "seconds": 2})");
	CHECK(l->view().conserved);
	CHECK_FALSE(l->failed());

	must(*l, R"({"do": "restore"})");
	v = l->view();
	CHECK(item(v, "acid").place == "bench");
	CHECK(item(v, "acid").col == 2);
	CHECK(item(v, "acid").row == 8);
	CHECK(v.trash.empty());
	CHECK(v.conserved);
	CHECK(l->digest() == twin->digest()); // the world went on as if it had never left the bench
	must(*l, R"({"do": "pour", "from": "acid", "to": "flask", "ml": 10})"); // and it is used again
	CHECK(l->view().conserved);

	CHECK(says(refused(*l, R"({"do": "restore"})"), "the trash is empty"));
	CHECK(says(refused(*l, R"({"do": "restore", "item": "acid"})"), "\"acid\" is not in the trash"));
	CHECK(says(refused(*l, R"({"do": "restore", "item": "kettle"})"), "\"kettle\" is not in the trash"));
	CHECK(says(refused(*l, R"({"do": "trash", "item": "kettle"})"), "no such item to trash"));
	CHECK(says(refused(*l, R"({"do": "trash"})"), "needs \"item\""));
}

TEST_CASE("trash: a hot vessel cools in the trash exactly as it would in the cabinet (Stage 34)") {
	const auto hot = [] {
		auto l = bench();
		for (const char *c : { R"({"do": "pour", "from": "acid", "to": "flask", "ml": 60})", R"({"do": "heat", "burner": "burner", "vessel": "flask"})",
					 R"({"do": "valve", "burner": "burner", "to": 1})", R"({"do": "wait", "seconds": 20})", R"({"do": "unheat", "burner": "burner"})" }) {
			must(*l, c);
		}
		return l;
	};
	auto binned = hot(), stowed = hot();
	const double before = item(binned->view(), "flask").celsius;
	REQUIRE(before > 30.0);
	must(*binned, R"({"do": "trash", "item": "flask"})");
	must(*stowed, R"({"do": "stow", "item": "flask"})");
	// (Stage 53: half an hour, not thirty seconds: its floor's glass, hotter than what it holds, gives up its heat to it
	// first, and the flask warms before it cools, by its own outside, slowly)
	for (int k = 0; k < 30; ++k) {
		must(*binned, R"({"do": "wait", "seconds": 60})");
		must(*stowed, R"({"do": "wait", "seconds": 60})");
		const lab::LabView a = binned->view(), b = stowed->view();
		CHECK(a.conserved);
		CHECK(item(a, "flask").celsius == item(b, "flask").celsius);
		CHECK(grams(a, "flask", "water") == grams(b, "flask", "water"));
		CHECK(a.energy_j == b.energy_j);
	}
	const double after = item(binned->view(), "flask").celsius;
	CHECK(after < before - 1.0);
	CHECK(item(binned->view(), "flask").place == "trash");
	MESSAGE("in the trash the flask cooled from " << before << " C to " << after << " C in half an hour");
	// out of the trash, the two labs are one state
	must(*binned, R"({"do": "stow", "item": "flask"})");
	CHECK(binned->view().trash.empty());
	CHECK(item(binned->view(), "flask").place == "cabinet");
	CHECK(binned->digest() == stowed->digest());
}

TEST_CASE("trash: what is joined to something is refused as stowing it is, with the reason (Stage 34)") {
	auto l = bench();
	CHECK(says(refused(*l, R"({"do": "trash", "item": "tray"})"), "the tray is the bench's own"));
	must(*l, R"({"do": "heat", "burner": "burner", "vessel": "flask"})");
	CHECK(says(refused(*l, R"({"do": "trash", "item": "flask"})"), "\"flask\" stands over \"burner\": take the burner away first"));
	CHECK(says(refused(*l, R"({"do": "trash", "item": "burner"})"), "\"burner\" is under \"flask\": take it away first"));
	must(*l, R"({"do": "unheat", "burner": "burner"})");
	must(*l, R"({"do": "attach", "item": "balloon", "to": "flask"})");
	CHECK(says(refused(*l, R"({"do": "trash", "item": "flask"})"), "\"flask\" has \"balloon\" on it: take it off first"));
	CHECK(says(refused(*l, R"({"do": "trash", "item": "balloon"})"), "\"balloon\" is on \"flask\": take it away first"));
	must(*l, R"({"do": "detach", "item": "balloon"})");
	must(*l, R"({"do": "submit", "item": "jar"})");
	CHECK(says(refused(*l, R"({"do": "trash", "item": "jar"})"), "\"jar\" has been handed in"));
	// a drain comes off, as when it is stowed; a burner and a balloon go in as vessels do
	must(*l, R"({"do": "under", "item": "beaker", "of": "condenser"})");
	CHECK(item(l->view(), "condenser").drains_to >= 0);
	must(*l, R"({"do": "part", "kind": "monitor", "at": [0, 0]})");
	must(*l, R"({"do": "set", "part": "monitor1", "item": "flask", "what": "celsius", "compare": "above", "value": 10})");
	must(*l, R"({"do": "rig", "from": "monitor1", "item": "burner", "control": "valve", "on": 0})");
	const lab::LabView before = l->view();
	must(*l, R"({"do": "trash", "item": "beaker"})");
	CHECK(item(l->view(), "condenser").drains_to == -1);
	must(*l, R"({"do": "trash", "item": "burner"})");
	must(*l, R"({"do": "trash", "item": "balloon"})");
	must(*l, R"({"do": "trash", "item": "flask"})");
	CHECK(trashed(l->view()) == Names{ "beaker", "burner", "balloon", "flask" });
	CHECK(says(refused(*l, R"({"do": "heat", "burner": "burner", "vessel": "waste"})"), "\"burner\" is in the trash"));
	CHECK(says(refused(*l, R"({"do": "valve", "burner": "burner", "to": 1})"), "\"burner\" is in the trash"));
	CHECK(says(refused(*l, R"({"do": "attach", "item": "balloon", "to": "waste"})"), "\"balloon\" is in the trash"));
	// as in the cabinet, a part goes on sensing it and its rig to it stays; no new one is made, and one is taken off
	CHECK(l->view().rigs.size() == 1);
	must(*l, R"({"do": "wait", "seconds": 1})");
	CHECK(item(l->view(), "monitor1").part.out); // it reads the flask in the trash
	CHECK(says(refused(*l, R"({"do": "rig", "from": "monitor1", "item": "flask", "control": "stir", "on": true})"), "\"flask\" is in the trash"));
	CHECK(says(refused(*l, R"({"do": "sense", "part": "monitor1", "item": "balloon"})"), "\"balloon\" is in the trash"));
	must(*l, R"({"do": "unrig", "from": "monitor1", "item": "burner", "control": "valve"})");
	CHECK(l->view().rigs.empty());
	CHECK(l->view().conserved);
	// each back to its square, the newest first
	for (const char *id : { "flask", "balloon", "burner", "beaker" }) {
		const lab::ItemView &was = item(before, id);
		REQUIRE(was.place == "bench");
		must(*l, R"({"do": "restore"})");
		CHECK(item(l->view(), id).place == "bench");
		CHECK(item(l->view(), id).col == was.col);
		CHECK(item(l->view(), id).row == was.row);
	}
	CHECK(l->view().trash.empty());
}

TEST_CASE("trash: restored to a square that is taken, an item goes to the cabinet; one from the cabinet goes back there; any element is restored by name (Stage 34)") {
	auto l = bench();
	must(*l, R"({"do": "trash", "item": "beaker"})"); // from [14, 8]
	must(*l, R"({"do": "stow", "item": "jar"})");
	must(*l, R"({"do": "trash", "item": "jar"})"); // from the cabinet
	must(*l, R"({"do": "trash", "item": "condenser"})");
	lab::LabView v = l->view();
	CHECK(trashed(v) == Names{ "beaker", "jar", "condenser" });
	CHECK(item(v, "jar").col == -1); // no square: the cabinet
	must(*l, R"({"do": "place", "item": "waste", "at": [15, 8]})"); // over the beaker's footprint
	must(*l, R"({"do": "restore", "item": "beaker"})"); // not the newest
	v = l->view();
	CHECK(item(v, "beaker").place == "cabinet");
	CHECK(item(v, "beaker").col == -1);
	CHECK(trashed(v) == Names{ "jar", "condenser" });
	CHECK(noticed(v, "beaker went to the cabinet: \"waste\" stands where it stood"));
	must(*l, R"({"do": "restore", "item": "jar"})");
	CHECK(item(l->view(), "jar").place == "cabinet");
	must(*l, R"({"do": "restore"})");
	CHECK(item(l->view(), "condenser").place == "bench");
	CHECK(item(l->view(), "condenser").col == 12);
	CHECK(l->view().trash.empty());
	CHECK(l->view().conserved);
	must(*l, R"({"do": "place", "item": "beaker", "at": [22, 0]})"); // from the cabinet, as ever
}

TEST_CASE("trash: an item is carried out of the trash onto a free square, or stowed from it (Stage 34)") {
	auto l = bench();
	must(*l, R"({"do": "trash", "item": "beaker"})");
	must(*l, R"({"do": "trash", "item": "waste"})");
	CHECK(says(refused(*l, R"({"do": "place", "item": "beaker", "at": [9, 8]})"), "\"flask\" stands there"));
	CHECK(says(refused(*l, R"({"do": "place", "item": "beaker", "at": [27, 0]})"), "off the bench"));
	CHECK(trashed(l->view()) == Names{ "beaker", "waste" });
	must(*l, R"({"do": "place", "item": "beaker", "at": [22, 0]})");
	lab::LabView v = l->view();
	CHECK(item(v, "beaker").place == "bench");
	CHECK(item(v, "beaker").col == 22);
	CHECK(item(v, "beaker").row == 0);
	CHECK(trashed(v) == Names{ "waste" });
	must(*l, R"({"do": "stow", "item": "waste"})");
	v = l->view();
	CHECK(item(v, "waste").place == "cabinet");
	CHECK(v.trash.empty());
	CHECK(says(refused(*l, R"({"do": "restore"})"), "the trash is empty"));
	CHECK(v.conserved);
	CHECK(l->digest() == bench()->digest()); // where things stand is not the world
}

TEST_CASE("trash: a part keeps its settings, signals nothing while it is there, and comes back with its wires and rigs (Stage 34, 38)") {
	const auto made = [] {
		auto l = bench();
		for (const char *c : { R"({"do": "part", "kind": "timer", "at": [0, 0]})", R"({"do": "set", "part": "timer1", "seconds": 2})", R"({"do": "part", "kind": "alarm", "at": [1, 0]})",
					 R"({"do": "wire", "from": "timer1", "to": "alarm1"})", R"({"do": "part", "kind": "monitor", "at": [2, 0]})",
					 R"({"do": "set", "part": "monitor1", "item": "flask", "what": "celsius", "compare": "above", "value": 10})",
					 R"({"do": "rig", "from": "monitor1", "item": "flask", "control": "stir", "on": true})", R"({"do": "rig", "from": "monitor1", "item": "timer1", "control": "reset", "off": true})",
					 R"({"do": "part", "kind": "writer", "at": [3, 0]})", R"({"do": "set", "part": "writer1", "level": "warning", "says": "the flask is warm", "writes": "change"})",
					 R"({"do": "wire", "from": "monitor1", "to": "writer1"})", R"({"do": "part", "kind": "controller", "at": [4, 0]})",
					 R"({"do": "set", "part": "controller1", "item": "flask", "what": "celsius", "target": 50, "speed": "fast"})", R"({"do": "wait", "seconds": 1})" }) {
			must(*l, c);
		}
		return l;
	};
	auto l = made(), twin = made();
	lab::LabView v = l->view();
	CHECK(item(v, "timer1").part.left == 60); // a second of its two gone
	CHECK(item(v, "monitor1").part.out);
	CHECK(item(v, "flask").stirred); // by the monitor's rig
	REQUIRE(v.wires.size() == 2);
	REQUIRE(v.rigs.size() == 2);

	for (const char *id : { "timer1", "monitor1", "writer1", "controller1" }) {
		must(*l, std::string(R"({"do": "trash", "item": ")") + id + "\"}");
	}
	v = l->view();
	CHECK(trashed(v) == Names{ "timer1", "monitor1", "writer1", "controller1" });
	CHECK(v.wires.empty()); // to the alarm and the writer, outside them: off the lab, kept in the trash with them
	CHECK(v.rigs.empty());
	CHECK(item(v, "timer1").place == "trash");
	CHECK(item(v, "timer1").col == 0); // its square
	CHECK(item(v, "timer1").part.preset == 120);
	CHECK(item(v, "timer1").part.left == 60);
	CHECK(item(v, "monitor1").part.watching);
	CHECK(item(v, "monitor1").part.quantity.item == "flask");
	CHECK(item(v, "monitor1").part.quantity.what == "celsius");
	CHECK(item(v, "monitor1").part.comparison.a == 10.0);
	CHECK_FALSE(item(v, "monitor1").part.out); // its condition holds, and it signals nothing
	CHECK(item(v, "writer1").part.says == "the flask is warm");
	CHECK(item(v, "writer1").part.writes == 2);
	CHECK(item(v, "writer1").part.notice_level == 2);
	CHECK(item(v, "controller1").part.target == 50.0);
	CHECK(item(v, "controller1").part.speed == 2);
	CHECK(item(v, "flask").stirred); // a control stays as it was last set
	CHECK(l->debug("monitor1").found);
	CHECK(says(l->debug("monitor1").facts.front(), "in the trash"));

	// nothing evaluates them: the timer does not count, the alarm it was wired to never rings
	must(*l, R"({"do": "wait", "seconds": 3})");
	must(*twin, R"({"do": "wait", "seconds": 3})");
	v = l->view();
	CHECK(twin->view().ringing == Names{ "alarm1" });
	CHECK(v.ringing.empty());
	CHECK(item(v, "timer1").part.left == 60);
	CHECK_FALSE(item(v, "timer1").part.out);
	CHECK_FALSE(item(v, "monitor1").part.out);
	CHECK_FALSE(l->failed()); // G1 to G4 hold of what is left
	CHECK(v.conserved);

	// they take no command there, and their names stay theirs
	for (const char *c : { R"({"do": "set", "part": "timer1", "seconds": 9})", R"({"do": "wire", "from": "timer1", "to": "alarm1"})", R"({"do": "wire", "from": "controller1", "to": "timer1"})",
				 R"({"do": "rig", "from": "monitor1", "item": "flask", "control": "stir", "on": false})", R"({"do": "reset", "timer": "timer1"})", R"({"do": "scrap", "part": "timer1"})",
				 R"({"do": "move", "items": ["alarm1", "timer1"], "by": [0, 1]})", R"({"do": "circuit", "parts": ["alarm1", "timer1"]})", R"({"do": "stow", "item": "timer1"})",
				 R"({"do": "sense", "part": "monitor1", "item": "beaker"})" }) {
		CHECK_MESSAGE(says(refused(*l, c), "is in the trash: restore it first"), c);
	}
	CHECK(says(refused(*l, R"({"do": "part", "kind": "timer", "at": [9, 0], "id": "timer1"})"), "\"timer1\" is taken"));
	CHECK(says(refused(*l, R"({"do": "trash", "item": "timer1"})"), "already in the trash"));

	// back on their squares, set as they were, joined as they were (Stage 38: until then, to nothing)
	for (int k = 0; k < 4; ++k) {
		must(*l, R"({"do": "restore"})");
	}
	v = l->view();
	CHECK(v.trash.empty());
	const auto joined = [](const std::vector<lab::Wire> &ws) {
		std::vector<std::string> out;
		for (const lab::Wire &w : ws) {
			out.push_back(w.from + " -> " + w.to);
		}
		std::sort(out.begin(), out.end());
		return out;
	};
	CHECK(joined(v.wires) == joined(twin->view().wires)); // (in the order they came back)
	CHECK(v.rigs.size() == 2);
	CHECK(item(v, "timer1").place == "bench");
	CHECK(item(v, "timer1").col == 0);
	CHECK(item(v, "timer1").row == 0);
	CHECK(item(v, "timer1").part.preset == 120);
	CHECK(item(v, "timer1").part.left == 60);
	CHECK(item(v, "monitor1").col == 2);
	CHECK(item(v, "monitor1").part.watching);
	CHECK(item(v, "monitor1").part.comparison.a == 10.0);
	CHECK(item(v, "writer1").part.says == "the flask is warm");
	CHECK(item(v, "controller1").part.target == 50.0);
	CHECK(item(v, "controller1").part.watching);
	CHECK_FALSE(item(v, "controller1").part.tuned); // rigged to nothing
	// and they work again: the timer goes on from where it was, and wired as it was, the alarm rings
	CHECK(says(refused(*l, R"({"do": "wire", "from": "timer1", "to": "alarm1"})"), "already"));
	must(*l, R"({"do": "wait", "seconds": 0.5})");
	CHECK(item(l->view(), "timer1").part.left == 30);
	CHECK(item(l->view(), "monitor1").part.out);
	CHECK(l->view().ringing.empty());
	must(*l, R"({"do": "wait", "seconds": 0.6})");
	CHECK(l->view().ringing == Names{ "alarm1" });
	CHECK_FALSE(l->failed());
}

TEST_CASE("trash: a part whose square is taken stays in the trash, with the reason, and is carried out by hand (Stage 34)") {
	auto l = bench();
	must(*l, R"({"do": "part", "kind": "timer", "at": [0, 0]})");
	must(*l, R"({"do": "set", "part": "timer1", "seconds": 7})");
	must(*l, R"({"do": "trash", "item": "timer1"})");
	must(*l, R"({"do": "part", "kind": "alarm", "at": [0, 0]})");
	const std::string why = refused(*l, R"({"do": "restore"})");
	CHECK(says(why, "\"timer1\" cannot go back where it was: \"alarm1\" stands there"));
	CHECK(says(why, "It stays in the trash: place it on a free square"));
	CHECK(trashed(l->view()) == Names{ "timer1" });
	CHECK(says(refused(*l, R"({"do": "place", "item": "timer1", "at": [0, 0]})"), "\"alarm1\" stands there"));
	CHECK(says(refused(*l, R"({"do": "place", "item": "timer1", "at": [9, 9]})"), "\"flask\" stands there"));
	must(*l, R"({"do": "place", "item": "timer1", "at": [6, 0]})");
	const lab::LabView v = l->view();
	CHECK(v.trash.empty());
	CHECK(item(v, "timer1").place == "bench");
	CHECK(item(v, "timer1").col == 6);
	CHECK(item(v, "timer1").part.preset == 7 * 60);
	must(*l, R"({"do": "wire", "from": "timer1", "to": "alarm1"})");
	must(*l, R"({"do": "wait", "seconds": 0.1})");
	CHECK_FALSE(l->failed());
}

TEST_CASE("trash: a circuit goes in and comes back with the parts in it, the wires among them and those to what was outside (Stage 34, 38)") {
	auto l = bench();
	for (const char *c : { R"({"do": "part", "kind": "timer", "at": [3, 1]})", R"({"do": "set", "part": "timer1", "seconds": 2})", R"({"do": "part", "kind": "monitor", "at": [5, 2]})",
				 R"({"do": "set", "part": "monitor1", "item": "flask", "what": "celsius", "compare": "above", "value": 30})", R"({"do": "part", "kind": "alarm", "at": [6, 3]})",
				 R"({"do": "wire", "from": "timer1", "to": "monitor1"})", R"({"do": "wire", "from": "monitor1", "to": "alarm1"})",
				 R"({"do": "rig", "from": "monitor1", "item": "burner", "control": "valve", "on": 0.1})", R"({"do": "rig", "from": "monitor1", "item": "timer1", "control": "reset", "on": true})",
				 R"({"do": "circuit", "parts": ["timer1", "monitor1"]})", R"({"do": "wait", "seconds": 0.5})" }) {
		must(*l, c);
	}
	const lab::ItemView circuit = item(l->view(), "circuit1");
	must(*l, R"({"do": "trash", "item": "circuit1"})");
	lab::LabView v = l->view();
	CHECK(trashed(v) == Names{ "circuit1" }); // one element, with what it holds
	CHECK(item(v, "timer1").place == "trash");
	CHECK(item(v, "timer1").part.in == "circuit1");
	CHECK(item(v, "monitor1").place == "trash");
	CHECK(item(v, "alarm1").place == "bench");
	CHECK(v.wires.empty()); // the one inside and the one to the alarm: off the lab, kept with it
	CHECK(v.rigs.empty());
	CHECK(says(refused(*l, R"({"do": "restore", "item": "timer1"})"), "\"timer1\" is in \"circuit1\", in the trash: restore that"));
	CHECK(says(refused(*l, R"({"do": "place", "item": "monitor1", "at": [0, 0]})"), "\"monitor1\" is in \"circuit1\", in the trash: restore that"));
	CHECK(says(refused(*l, R"({"do": "uncircuit", "circuit": "circuit1"})"), "\"circuit1\" is in the trash"));
	CHECK(says(refused(*l, R"({"do": "part", "kind": "timer", "at": [0, 0], "in": "circuit1"})"), "\"circuit1\" is in the trash"));
	must(*l, R"({"do": "wait", "seconds": 3})");
	CHECK(l->view().ringing.empty());
	CHECK(item(l->view(), "timer1").part.left == 90);

	must(*l, R"({"do": "restore"})");
	v = l->view();
	CHECK(v.trash.empty());
	CHECK(item(v, "circuit1").place == "bench");
	CHECK(item(v, "circuit1").col == circuit.col);
	CHECK(item(v, "circuit1").row == circuit.row);
	CHECK(item(v, "circuit1").part.inner_w == circuit.part.inner_w);
	CHECK(item(v, "timer1").place == "circuit");
	CHECK(item(v, "timer1").part.in == "circuit1");
	CHECK(item(v, "timer1").part.preset == 120);
	CHECK(item(v, "monitor1").part.in == "circuit1");
	CHECK(item(v, "monitor1").col == 2); // as they lay in it
	CHECK(item(v, "monitor1").row == 1);
	CHECK(item(v, "monitor1").part.comparison.a == 30.0);
	CHECK(v.wires == std::vector<lab::Wire>{ lab::Wire{ "timer1", "monitor1" }, lab::Wire{ "monitor1", "alarm1" } });
	REQUIRE(v.rigs.size() == 2); // the reset inside it, and the one to the burner's valve
	CHECK(v.rigs[0].item == "burner");
	CHECK(v.rigs[1].item == "timer1");
	CHECK(v.rigs[1].control == "reset");
	must(*l, R"({"do": "wait", "seconds": 2})");
	CHECK_FALSE(l->failed());
	CHECK(item(l->view(), "timer1").part.left == 0);
	must(*l, R"({"do": "uncircuit", "circuit": "circuit1"})");
	CHECK(item(l->view(), "timer1").place == "bench");

	// a part trashed from inside a circuit goes back into it; with the circuit gone, onto the bench by hand
	must(*l, R"({"do": "circuit", "parts": ["timer1", "monitor1"]})");
	must(*l, R"({"do": "trash", "item": "monitor1"})");
	CHECK(item(l->view(), "monitor1").part.in == "circuit2");
	must(*l, R"({"do": "restore"})");
	CHECK(item(l->view(), "monitor1").place == "circuit");
	must(*l, R"({"do": "trash", "item": "monitor1"})");
	must(*l, R"({"do": "scrap", "part": "circuit2"})");
	CHECK(says(refused(*l, R"({"do": "restore"})"), "\"monitor1\" cannot go back where it was: there is no circuit \"circuit2\""));
	must(*l, R"({"do": "place", "item": "monitor1", "at": [0, 0]})");
	CHECK(item(l->view(), "monitor1").place == "bench");
	CHECK(item(l->view(), "monitor1").part.in.empty());
	CHECK(l->view().trash.empty());
}

TEST_CASE("trash: it holds 64 elements; one more and the oldest leaves for good, an item to the cabinet, a part scrapped (Stage 34)") {
	REQUIRE(lab::TRASH_MOST == 64);
	auto l = bench();
	const lab::LabView start = l->view();
	must(*l, R"({"do": "trash", "item": "jar"})"); // the oldest
	const auto another = [&](int n) {
		must(*l, R"({"do": "part", "kind": "timer", "at": [0, 0]})");
		must(*l, R"({"do": "set", "part": "timer)" + std::to_string(n) + R"(", "seconds": )" + std::to_string(n) + "}");
		must(*l, R"({"do": "trash", "item": "timer)" + std::to_string(n) + "\"}");
	};
	for (int n = 1; n <= 63; ++n) {
		another(n);
	}
	lab::LabView v = l->view();
	CHECK(v.trash.size() == 64);
	CHECK(trashed(v).front() == "jar");
	CHECK(item(v, "jar").place == "trash");

	another(64); // the 65th: the jar leaves, and cannot be destroyed
	v = l->view();
	CHECK(v.trash.size() == 64);
	CHECK(item(v, "jar").place == "cabinet");
	CHECK(grams(v, "jar", "malachite") == grams(start, "jar", "malachite"));
	CHECK(trashed(v).front() == "timer1");
	CHECK(trashed(v).back() == "timer64");
	CHECK(noticed(v, "the trash is full: jar went to the cabinet"));
	CHECK(v.conserved);
	CHECK(v.mass_g == start.mass_g);

	another(65); // and now a part: scrapped
	v = l->view();
	CHECK(v.trash.size() == 64);
	CHECK_FALSE(there(v, "timer1"));
	CHECK(trashed(v).front() == "timer2");
	CHECK(noticed(v, "the trash is full: timer1 left it for good"));
	CHECK(says(refused(*l, R"({"do": "restore", "item": "timer1"})"), "\"timer1\" is not in the trash"));
	CHECK(v.items.size() == start.items.size() + 64);
	must(*l, R"({"do": "restore", "item": "timer2"})");
	CHECK(item(l->view(), "timer2").part.preset == 2 * 60);
	must(*l, R"({"do": "wait", "seconds": 0.5})");
	CHECK(l->view().conserved);
	CHECK_FALSE(l->failed());
}

TEST_CASE("trash: emptied, everything in it leaves for good: an item to the cabinet with all it holds, a part scrapped with what joined it (Stage 38)") {
	auto l = bench();
	CHECK(says(refused(*l, R"({"do": "empty_trash"})"), "the trash is empty"));
	for (const char *c : { R"({"do": "part", "kind": "timer", "at": [0, 0]})", R"({"do": "part", "kind": "alarm", "at": [1, 0]})", R"({"do": "wire", "from": "timer1", "to": "alarm1"})",
				 R"({"do": "trash", "item": "jar"})", R"({"do": "trash", "item": "timer1"})" }) {
		must(*l, c);
	}
	const double malachite = grams(l->view(), "jar", "malachite");
	REQUIRE(malachite > 0.0);
	must(*l, R"({"do": "empty_trash"})");
	const lab::LabView v = l->view();
	CHECK(v.trash.empty());
	CHECK(item(v, "jar").place == "cabinet");
	CHECK(grams(v, "jar", "malachite") == malachite);
	CHECK_FALSE(there(v, "timer1")); // gone: its name free again, its wire to the alarm with it
	CHECK(v.wires.empty());
	CHECK(noticed(v, "the trash was emptied: 2 things")); // (the count once read 0)
	must(*l, R"({"do": "part", "kind": "timer", "at": [0, 0], "id": "timer1"})");
	CHECK(there(l->view(), "timer1"));
	CHECK(v.conserved);
	CHECK_FALSE(l->failed());
}

TEST_CASE("trash: a lab with things in the trash saves and loads, and goes on digest for digest (Stage 34)") {
	auto l = bench();
	for (const char *c : { R"({"do": "pour", "from": "acid", "to": "flask", "ml": 60})", R"({"do": "heat", "burner": "burner", "vessel": "flask"})", R"({"do": "valve", "burner": "burner", "to": 1})",
				 R"({"do": "wait", "seconds": 5})", R"({"do": "unheat", "burner": "burner"})", R"({"do": "part", "kind": "timer", "at": [3, 1]})", R"({"do": "set", "part": "timer1", "seconds": 4})",
				 R"({"do": "part", "kind": "monitor", "at": [5, 2]})", R"({"do": "set", "part": "monitor1", "item": "flask", "what": "celsius", "compare": "above", "value": 21})",
				 R"({"do": "part", "kind": "alarm", "at": [6, 3]})", R"({"do": "part", "kind": "writer", "at": [7, 3]})", R"({"do": "set", "part": "writer1", "level": "event", "says": "warm"})",
				 R"({"do": "wire", "from": "timer1", "to": "monitor1"})", R"({"do": "wire", "from": "monitor1", "to": "alarm1"})",
				 R"({"do": "rig", "from": "monitor1", "item": "timer1", "control": "reset", "change": true})", R"({"do": "circuit", "parts": ["timer1", "monitor1"]})",
				 R"({"do": "wait", "seconds": 1})", R"({"do": "trash", "item": "flask"})", R"({"do": "trash", "item": "circuit1"})", R"({"do": "stow", "item": "jar"})",
				 R"({"do": "trash", "item": "jar"})", R"({"do": "trash", "item": "writer1"})" }) {
		must(*l, c);
	}
	const std::string saved = l->save();
	CHECK(says(saved, "\"trash\""));
	std::string why;
	auto again = lab::Lab::load(saved, why);
	REQUIRE_MESSAGE(again != nullptr, why);
	CHECK(again->digest() == l->digest());
	CHECK(again->save() == saved);
	const lab::LabView a = again->view(), b = l->view();
	CHECK(trashed(a) == Names{ "flask", "circuit1", "jar", "writer1" });
	CHECK(a.trash == b.trash);
	REQUIRE(a.items.size() == b.items.size());
	for (size_t i = 0; i < a.items.size(); ++i) {
		CHECK(a.items[i].id == b.items[i].id);
		CHECK(a.items[i].place == b.items[i].place);
		CHECK(a.items[i].col == b.items[i].col);
		CHECK(a.items[i].row == b.items[i].row);
		CHECK(a.items[i].part == b.items[i].part);
	}
	for (int k = 0; k < 20; ++k) {
		REQUIRE(l->advance(30, why));
		REQUIRE(again->advance(30, why));
		REQUIRE(again->digest() == l->digest());
	}
	// the order and each remembered place are state: another order, or another square, is another digest
	auto other = lab::Lab::load(saved, why);
	REQUIRE_MESSAGE(other != nullptr, why);
	const std::string before = other->digest();
	must(*other, R"({"do": "restore", "item": "jar"})");
	must(*other, R"({"do": "trash", "item": "jar"})"); // the newest now
	CHECK(other->digest() != before);
	// restored the same way, the two go on as one
	for (lab::Lab *each : { l.get(), again.get() }) {
		must(*each, R"({"do": "restore"})"); // the writer
		must(*each, R"({"do": "restore", "item": "circuit1"})");
		must(*each, R"({"do": "restore", "item": "flask"})");
	}
	CHECK(again->digest() == l->digest());
	CHECK(item(again->view(), "flask").place == "bench");
	CHECK(item(again->view(), "flask").col == 9);
	CHECK(item(again->view(), "timer1").part.in == "circuit1");
	CHECK(again->view().wires == l->view().wires);
	CHECK(again->view().rigs == l->view().rigs);
	for (int k = 0; k < 10; ++k) {
		REQUIRE(l->advance(30, why));
		REQUIRE(again->advance(30, why));
		REQUIRE(again->digest() == l->digest());
	}
	CHECK(again->view().conserved);
}

TEST_CASE("trash: a lab whose trash is empty digests and saves as one that never had a trash (Stage 34)") {
	// (the sessions' golden records, digest for digest, are the evidence that this is the digest of before: ctest demo_session)
	auto l = bench(), twin = bench();
	const std::string fresh = l->save();
	CHECK_FALSE(says(fresh, "\"trash\""));
	must(*l, R"({"do": "part", "kind": "timer", "at": [0, 0]})");
	must(*twin, R"({"do": "part", "kind": "timer", "at": [0, 0]})");
	must(*l, R"({"do": "trash", "item": "acid"})");
	CHECK(l->digest() != twin->digest()); // the trash is state while it holds something
	must(*l, R"({"do": "restore"})");
	CHECK(l->digest() == twin->digest());
	CHECK(l->save() == twin->save());
	must(*l, R"({"do": "trash", "item": "timer1"})");
	must(*l, R"({"do": "restore"})");
	CHECK(l->digest() == twin->digest());
	CHECK_FALSE(says(l->save(), "\"trash\""));
	must(*l, R"({"do": "scrap", "part": "timer1"})");
	CHECK(l->digest() == bench()->digest());
}
