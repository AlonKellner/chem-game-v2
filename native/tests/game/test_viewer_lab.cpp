// The spectral viewer in the lab (GAME_SPEC §13, Stage 42): equipment of the cabinet that holds no matter, so it
// changes nothing in the world; where its probe is fixed (an item or a part, at a place of it in its own frame;
// the room; its holder) is the lab's, saved, and not the world's.
#include "lab/lab.hpp"

#include <doctest/doctest.h>

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
	CHECK_MESSAGE(!l.command(command, why), command);
	CHECK(l.digest() == before);
	return why;
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

// the interactive bench, its viewer taken out
std::string without_viewer() {
	std::string text = read("data/benches/lab.json");
	const std::string entry = ",\n\t\t{\n\t\t\t\"id\": \"viewer\",\n\t\t\t\"kind\": \"viewer\"\n\t\t}";
	const size_t at = text.find(entry);
	REQUIRE(at != std::string::npos);
	return text.erase(at, entry.size());
}

} // namespace

TEST_CASE("lab: a viewer is equipment of the cabinet that changes nothing in the world (Stage 42)") {
	auto with = open_text(read("data/benches/lab.json"));
	auto without = open_text(without_viewer());
	const lab::ItemView &v = item(with->view(), "viewer");
	CHECK(v.kind == "viewer");
	CHECK(v.place == "cabinet");
	CHECK(v.w == 7);
	CHECK(v.h == 4);
	CHECK(v.contents.empty());
	CHECK(v.probe_on.empty());
	CHECK(!v.probe_room);
	CHECK(with->digest() == without->digest());
	// placed and probing, through steps with a flame under a flask: the same world, step for step
	for (lab::Lab *l : { with.get(), without.get() }) {
		must(*l, R"({"do":"place","item":"flask","at":[8,4]})");
		must(*l, R"({"do":"place","item":"burner","at":[8,11]})");
		must(*l, R"({"do":"heat","burner":"burner","vessel":"flask"})");
		must(*l, R"({"do":"valve","burner":"burner","to":0.8})");
	}
	must(*with, R"({"do":"place","item":"viewer","at":[16,2]})");
	must(*with, R"({"do":"probe","viewer":"viewer","on":"flask","at":[0.5,0.7]})");
	CHECK(with->digest() == without->digest());
	std::string why;
	REQUIRE(with->advance(300, why));
	REQUIRE(without->advance(300, why));
	CHECK(with->digest() == without->digest());
	must(*with, R"({"do":"probe","viewer":"viewer","at":[3.5,9.25]})");
	must(*with, R"({"do":"move","items":["viewer"],"by":[2,6]})");
	REQUIRE(with->advance(120, why));
	REQUIRE(without->advance(120, why));
	CHECK(with->digest() == without->digest());
	const std::string report = with->report();
	CHECK(report.find("\"viewer\"") != std::string::npos);
}

TEST_CASE("lab: the probe is fixed on an item, a part or the room, or back in its holder (Stage 42)") {
	auto l = open_text(read("data/benches/lab.json"));
	// in the cabinet it is not used
	CHECK(refused(*l, R"({"do":"probe","viewer":"viewer","on":"flask"})").find("cabinet") != std::string::npos);
	must(*l, R"({"do":"place","item":"viewer","at":[16,2]})");
	must(*l, R"({"do":"place","item":"flask","at":[8,4]})");
	must(*l, R"({"do":"probe","viewer":"viewer","on":"flask","at":[0.25,0.8]})");
	{
		const lab::ItemView &v = item(l->view(), "viewer");
		CHECK(v.probe_on == "flask");
		CHECK(!v.probe_room);
		CHECK(v.probe_u == 0.25);
		CHECK(v.probe_v == 0.8);
	}
	// on what is in the cabinet: it is drawn there, so it can be read there
	must(*l, R"({"do":"probe","viewer":"viewer","on":"beaker"})");
	CHECK(item(l->view(), "viewer").probe_u == 0.5);
	// on itself: its own screen
	must(*l, R"({"do":"probe","viewer":"viewer","on":"viewer","at":[0.4,0.4]})");
	CHECK(item(l->view(), "viewer").probe_on == "viewer");
	// on the room
	must(*l, R"({"do":"probe","viewer":"viewer","at":[2.5,13.75]})");
	{
		const lab::ItemView &v = item(l->view(), "viewer");
		CHECK(v.probe_on.empty());
		CHECK(v.probe_room);
		CHECK(v.probe_v == 13.75);
	}
	// back in its holder
	must(*l, R"({"do":"probe","viewer":"viewer"})");
	CHECK(!item(l->view(), "viewer").probe_room);
	// refused: nothing of that name, a viewer that is not one, "at" that is not two numbers
	refused(*l, R"({"do":"probe","viewer":"viewer","on":"nothing"})");
	refused(*l, R"({"do":"probe","viewer":"flask","on":"beaker"})");
	refused(*l, R"({"do":"probe","viewer":"viewer","at":[1]})");
	refused(*l, R"({"do":"probe","viewer":"viewer","at":["a","b"]})");
}

TEST_CASE("lab: a probe on what leaves the lab, or of a viewer put away, goes back to its holder (Stage 42)") {
	auto l = open_text(read("data/benches/lab.json"));
	must(*l, R"({"do":"place","item":"viewer","at":[16,2]})");
	must(*l, R"({"do":"part","kind":"timer","id":"clock","at":[4,12]})");
	must(*l, R"({"do":"probe","viewer":"viewer","on":"clock","at":[0.5,0.5]})");
	CHECK(item(l->view(), "viewer").probe_on == "clock");
	// in the trash it keeps it (a part restored comes back with it); emptied, it is gone, and the probe comes home
	must(*l, R"({"do":"trash","item":"clock"})");
	CHECK(item(l->view(), "viewer").probe_on == "clock");
	refused(*l, R"({"do":"probe","viewer":"viewer","on":"clock"})");
	must(*l, R"({"do":"empty_trash"})");
	CHECK(item(l->view(), "viewer").probe_on.empty());
	must(*l, R"({"do":"part","kind":"timer","id":"clock","at":[4,12]})");
	must(*l, R"({"do":"probe","viewer":"viewer","on":"clock"})");
	must(*l, R"({"do":"scrap","part":"clock"})");
	CHECK(item(l->view(), "viewer").probe_on.empty());
	// stowed, its probe goes home with it
	must(*l, R"({"do":"probe","viewer":"viewer","at":[1.0,1.0]})");
	must(*l, R"({"do":"stow","item":"viewer"})");
	CHECK(!item(l->view(), "viewer").probe_room);
}

TEST_CASE("lab: the probe is saved and loaded; a lab without a probe out saves as before (Stage 42)") {
	auto l = open_text(read("data/benches/lab.json"));
	const std::string plain = l->save();
	CHECK(plain.find("\"probe\"") == std::string::npos);
	must(*l, R"({"do":"place","item":"viewer","at":[16,2]})");
	must(*l, R"({"do":"place","item":"flask","at":[8,4]})");
	must(*l, R"({"do":"probe","viewer":"viewer","on":"flask","at":[0.1234567890123,0.75]})");
	std::string why;
	auto again = lab::Lab::load(l->save(), why);
	REQUIRE_MESSAGE(again != nullptr, why);
	const lab::ItemView &v = item(again->view(), "viewer");
	CHECK(v.kind == "viewer");
	CHECK(v.probe_on == "flask");
	CHECK(v.probe_u == 0.1234567890123);
	CHECK(v.probe_v == 0.75);
	CHECK(again->digest() == l->digest());
	CHECK(again->save() == l->save());
}

TEST_CASE("lab: every command aimed at a viewer is refused or leaves the world as it was (Stage 42)") {
	auto l = open_text(read("data/benches/lab.json"));
	must(*l, R"({"do":"place","item":"viewer","at":[16,2]})");
	must(*l, R"({"do":"place","item":"flask","at":[8,4]})");
	must(*l, R"({"do":"place","item":"burner","at":[2,4]})");
	const char *const commands[] = { "place", "move", "heat", "unheat", "valve", "stir", "pour", "tip", "attach", "detach", "under", "undrain", "submit", "hold", "invert", "unpour", "cap", "stow", "trash", "restore", "sense", "unsense", "rig", "unrig" };
	const char *const keys[] = { "item", "vessel", "burner", "to", "of", "from", "into", "species" };
	auto clean = open_text(read("data/benches/lab.json"));
	for (const char *command : commands) {
		for (const char *key : keys) {
			std::string why;
			const std::string text = std::string(R"({"do":")") + command + R"(",")" + key + R"(":"viewer","item":"viewer","to":"flask","at":[16,2],"rate":0.5,"slot":1,"on":true,"control":"stir","part":"none"})";
			const std::string before_world = l->digest();
			const bool ran = l->command(text, why);
			CAPTURE(text);
			CAPTURE(why);
			CHECK(!l->failed());
			if (ran) {
				// put back where it was, for the next (out of the trash, or the cabinet)
				l->command(R"({"do":"restore"})", why);
				l->command(R"({"do":"place","item":"viewer","at":[16,2]})", why);
			}
			// whatever it did, the world is as it was: a viewer holds no matter and joins nothing
			CHECK(l->digest() == before_world);
		}
	}
	CHECK(item(l->view(), "viewer").kind == "viewer");
	CHECK(item(l->view(), "viewer").contents.empty());
}
