// The demo session played once for all that holds it (GAME_SPEC §17, §18 D4; Stage 68): its golden record digest for
// digest, its results, what the lab told of it, and the screen's scene after every command, the ctest fixture `demo`:
// the screen's scene through the demo holds O2 (test_optics.cpp), which reads each scene as this play made it.
#include "lab/lab.hpp"
#include "lab/optics_scene.hpp"
#include "lab/ui.hpp"

#include <doctest/doctest.h>
#include <nlohmann/json.hpp>

#include "fixtures.hpp"
#include "scene_file.hpp"

#include <algorithm>
#include <array>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

using json = nlohmann::json;
using lab::Notice;

namespace {

std::string read(const std::string &relative) {
	std::ifstream in(std::string(CHEM_REPO_ROOT) + "/" + relative);
	REQUIRE_MESSAGE(static_cast<bool>(in), relative);
	std::ostringstream out;
	out << in.rdbuf();
	return out.str();
}

const json item(const json &report, const char *id) {
	for (const json &i : report.at("items")) {
		if (i.at("id") == id) {
			return i;
		}
	}
	FAIL("no item " << id);
	return json();
}

double grams(const json &item, const char *species) {
	for (const json &row : item.at("contents")) {
		if (row.at("species") == species) {
			return row.at("g").get<double>();
		}
	}
	return 0.0;
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

// what of a scene a pixel's light is made from: two scenes alike in all of it show every pixel alike
} // namespace

// ctest: fixture setup demo
TEST_CASE("demo: the session reproduces its golden record, its results are what was measured, and the lab tells it; its scenes kept (Stage 7, Stage 16, Stage 68)") {
	// D4. bench/sessions/demo.jsonl: a digest of the whole state after every command
	const json session = json::parse(read("data/sessions/demo.json"), nullptr, true, true);
	std::istringstream golden(read("bench/sessions/demo.jsonl"));
	std::string why;
	const std::unique_ptr<lab::Ui> ui = lab::Ui::open(read("data/benches/demo.json"), why);
	REQUIRE_MESSAGE(ui != nullptr, why);
	lab::Lab &l = ui->lab();
	// unchecked (Stage 40): its state is the same (D1); every step of the session is checked by D2's windows
	// (test_lab.cpp, each a loaded lab played on checked to the next cut), side by side, not before the scenes here
	// (Stage 68)
	l.set_checking(false);
	CHECK(l.view().notices.empty()); // what is there from the start is not news (Stage 16)
	fixtures::fresh("demo");
	json scenes = json::array();
	std::array<uint64_t, 3> last{};
	int index = 0;
	for (const json &command : session.at("script")) {
		json plain = command;
		plain.erase("note");
		REQUIRE_MESSAGE(l.command(plain.dump(), why), "command " << index << ": " << why);
		std::string line;
		REQUIRE(std::getline(golden, line));
		const json recorded = json::parse(line);
		CAPTURE(index);
		CHECK(recorded.at("i") == index);
		CHECK(recorded.at("step") == l.step());
		CHECK(recorded.at("digest") == l.digest());
		// the screen's scene (for test_optics.cpp's O2): whether it is another than the last command's, by its
		// revisions as the screen keeps them, the scene itself, and where the flask is on the screen
		ui->frame(0.0);
		const lab::OpticsScene &scene = ui->optics();
		const std::array<uint64_t, 3> now{ scene.revision, scene.wall_revision, scene.shapes_revision };
		float fx = -1, fy = -1;
		REQUIRE(ui->locate("item:flask", fx, fy));
		scenes.push_back(json{ { "step", l.step() }, { "changed", index == 0 || now != last }, { "flask", { fx, fy } } });
		last = now;
		// and at D2's cuts (test_lab.cpp) the lab saved and its report, where its windows start
		for (const int cut : { 0, 4, 9, 12, 17, 18, 19, 20 }) {
			if (index == cut) {
				fixtures::write("demo", std::to_string(index) + ".json", l.save());
				fixtures::write("demo", std::to_string(index) + ".report.json", l.report());
			}
		}
		const std::string written = scene_file::write(scene);
		fixtures::write("demo", std::to_string(index) + ".scene", written);
		lab::OpticsScene back;
		REQUIRE(scene_file::read(fixtures::read("demo", std::to_string(index) + ".scene"), back));
		CHECK_MESSAGE(scene_file::write(back) == written, "command " << index << "'s scene read back is another");
		++index;
	}
	std::string extra;
	CHECK_FALSE(std::getline(golden, extra));
	fixtures::write("demo", "scenes.json", scenes.dump());

	// GAME_SPEC §17: "done when"
	const json report = json::parse(l.report());
	CHECK(report.at("conserved").at("unchanged").get<bool>());
	const json flask = item(report, "flask"), beaker = item(report, "beaker"), balloon = item(report, "balloon");
	CHECK(flask.at("submitted").get<bool>());
	CHECK(beaker.at("submitted").get<bool>());
	CHECK(balloon.at("submitted").get<bool>());
	// a sealed balloon of CO2
	CHECK(grams(balloon, "carbon_dioxide") > 0.8);
	CHECK(balloon.at("on").is_null());
	// a beaker of water and nothing else that is not air
	// (Stage 53: 33.6 g; its hot distillate cools slower through its glass and evaporates more; Stage 63: 26.6 g, the
	// oxide's dissolving slower at its end, the sulfate made holding the last of the acid as bisulfate)
	CHECK(grams(beaker, "water") > 25.0);
	for (const json &row : beaker.at("contents")) {
		const std::string s = row.at("species");
		// (Stage 50: and a trace of the carbon dioxide the room's air took up, as the open beaker's gas exchanges with it;
		// Stage 63: and water's own ions; Stage 64: the carbon dioxide the water takes up by Henry's law, and its bicarbonate)
		const double g = row.at("g").get<double>();
		INFO(s << ": " << g << " g");
		const bool carbon = s == "carbon_dioxide" || s == "carbon_dioxide_aq" || s == "bicarbonate_ion" || s == "carbonate_ion";
		CHECK((s == "water" || s == "steam" || s == "nitrogen" || s == "oxygen" || s == "glass" || s == "hydrogen_ion" || s == "hydroxide_ion" ||
				(carbon && g < 1e-3)));
		if (carbon) {
			MESSAGE("the beaker's distillate: " << s << " " << g << " g");
		}
	}
	// a flask whose only solid or liquid, beside a trace of undecomposed malachite, is the pentahydrate
	const double crystals = grams(flask, "copper_sulfate_pentahydrate");
	CHECK(crystals > 8.0);
	CHECK(grams(flask, "water") == 0.0);
	CHECK(grams(flask, "copper_sulfate_aq") == 0.0);
	CHECK(grams(flask, "sulfuric_acid_aq") == 0.0);
	CHECK(grams(flask, "copper_sulfate") == 0.0); // not overheated
	CHECK(crystals / (crystals + grams(flask, "malachite") + grams(flask, "copper_oxide")) > 0.999);
	for (const json &row : flask.at("contents")) {
		if (row.at("species") == "copper_sulfate_pentahydrate") {
			CHECK(row.at("particles").get<int64_t>() > 500); // (Stage 53: 971, a little fewer and larger as the flask cools slower)
			CHECK(row.at("size_um").get<double>() > 50.0);
		}
	}

	// GAME_SPEC §13.4 (Stage 16): the demo, as the lab tells it: each thing once, at its level, what a hand moved not news
	INFO(all(l));
	CHECK(count(l, Notice::Event, "the malachite is breaking down") == 1);
	CHECK(count(l, Notice::Event, "CuO(s) appeared in flask") == 1);
	CHECK(count(l, Notice::Detail, "flask: malachite decomposition stopped") == 1);
	CHECK(count(l, Notice::Event, "the copper oxide dissolves") == 1);
	CHECK(count(l, Notice::Event, "CuO(s) is used up in flask") == 1);
	CHECK(count(l, Notice::Event, "flask boils") == 1);
	CHECK(count(l, Notice::Event, "flask stopped boiling") == 1);
	CHECK(count(l, Notice::Event, "blue copper sulfate crystals are growing") == 1);
	CHECK(count(l, Notice::Event, "CuSO4.5H2O(s) appeared in flask") == 1);
	// poured and tipped by hand: no news
	CHECK(count(l, Notice::Event, "Cu2CO3(OH)2(s) appeared") == 0);
	CHECK(count(l, Notice::Event, "H2SO4(aq) appeared") == 0);
	// nothing went wrong
	CHECK(count(l, Notice::Warning, "") == 0);
	CHECK(count(l, Notice::Danger, "") == 0);
	// in order, and of the step they happened in
	const std::vector<Notice> n = l.view().notices;
	CHECK(std::is_sorted(n.begin(), n.end(), [](const Notice &a, const Notice &b) { return a.step < b.step; }));
	CHECK(json::parse(l.report()).at("notices").size() == n.size());
}
