// The lab layer (GAME_SPEC §13): a bench driven by commands.
#include "lab/lab.hpp"
#include "lab/sha256.hpp"

#include <doctest/doctest.h>
#include <nlohmann/json.hpp>

#include <fstream>
#include <sstream>
#include <string>

using json = nlohmann::json;

namespace {

std::string read(const std::string &relative) {
	std::ifstream in(std::string(CHEM_REPO_ROOT) + "/" + relative);
	REQUIRE_MESSAGE(static_cast<bool>(in), relative);
	std::ostringstream out;
	out << in.rdbuf();
	return out.str();
}

std::unique_ptr<lab::Lab> demo_bench() {
	std::string why;
	auto made = lab::Lab::open(read("data/benches/demo.json"), why);
	REQUIRE_MESSAGE(made != nullptr, why);
	return made;
}

bool run(lab::Lab &lab, const char *command, std::string *why = nullptr) {
	std::string reason;
	const bool ok = lab.command(command, reason);
	if (why != nullptr) {
		*why = reason;
	}
	return ok;
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

} // namespace

TEST_CASE("lab: the build's name carries GAME_SPEC's version (Stage 0)") {
	CHECK(lab::build_name().rfind("chem-game-v2 ", 0) == 0);
	CHECK(lab::build_name().size() > std::string("chem-game-v2 ").size());
}

TEST_CASE("lab: SHA-256 is the standard's (Stage 7)") {
	lab::Sha256 empty;
	CHECK(empty.hex() == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
	lab::Sha256 abc;
	abc.update("abc", 3);
	CHECK(abc.hex() == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
	lab::Sha256 long_one; // two blocks
	const std::string text = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
	long_one.update(text.data(), text.size());
	CHECK(long_one.hex() == "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
}

TEST_CASE("lab: the demo's bench opens with every vessel full of the room's air (Stage 7)") {
	auto lab = demo_bench();
	const json report = json::parse(lab->report());
	CHECK(report.at("step") == 0);
	const json flask = item(report, "flask");
	CHECK(flask.at("celsius").get<double>() == doctest::Approx(20.0).epsilon(1e-9));
	CHECK(flask.at("gas_ml").get<double>() == doctest::Approx(250.0).epsilon(1e-6));
	CHECK(grams(item(report, "jar"), "malachite") == doctest::Approx(5.0).epsilon(1e-9));
	CHECK(item(report, "balloon").at("gas_ml").get<double>() == 0.0);
	CHECK(grams(item(report, "acid"), "water") == doctest::Approx(240.5).epsilon(1e-9));
	CHECK(report.at("conserved").at("unchanged").get<bool>());
	// Left alone it is not quite at rest: the acid's water wets the air above
	// it and the condenser cools to its coolant. Nothing is lost doing so.
	std::string why;
	const std::string before = lab->digest();
	REQUIRE(lab->advance(600, why));
	CHECK(lab->step() == 600);
	const json later = json::parse(lab->report());
	CHECK(later.at("conserved") == report.at("conserved"));
	CHECK(grams(item(later, "acid"), "steam") > 0.0);
	CHECK(item(later, "condenser").at("celsius").get<double>() < 16.0);
	CHECK(grams(item(later, "jar"), "malachite") == doctest::Approx(5.0).epsilon(1e-9));
	CHECK(lab->digest() != before);
}

TEST_CASE("lab: a malformed bench is refused with a reason (Stage 7)") {
	std::string why;
	CHECK(lab::Lab::open("not json", why) == nullptr);
	CHECK(lab::Lab::open(R"({"format": "something-else"})", why) == nullptr);
	CHECK(lab::Lab::open(R"({"format": "chem-bench/1", "room": {"celsius": 20}, "items": [{"id": "a", "kind": "kettle"}]})", why) == nullptr);
	CHECK(why.find("kind") != std::string::npos);
	CHECK(lab::Lab::open(R"({"format": "chem-bench/1", "room": {"celsius": 20}, "items": [
		{"id": "a", "kind": "vessel", "volume_ml": 10, "wall": "glass", "wall_g": 5, "contents": {"water": {"g": 50}}}]})", why) == nullptr); // 50 mL in 10
	CHECK(why.find("fit") != std::string::npos);
	CHECK(lab::Lab::open(R"({"format": "chem-bench/1", "room": {"celsius": 20}, "items": [
		{"id": "a", "kind": "vessel", "volume_ml": 10, "wall": "water", "wall_g": 5}]})", why) == nullptr); // not a wall
	CHECK(lab::Lab::open(R"({"format": "chem-bench/1", "room": {"celsius": 20}, "items": [
		{"id": "a", "kind": "balloon", "wall": "latex", "wall_g": 2}, {"id": "a", "kind": "balloon", "wall": "latex", "wall_g": 2}]})", why) == nullptr);
	CHECK(lab::Lab::open(R"({"format": "chem-bench/1", "room": {"celsius": 20}, "items": []})", why) != nullptr); // an empty room is a bench
}

TEST_CASE("lab: a refused command says why and changes nothing (Stage 7)") {
	auto lab = demo_bench();
	const std::string before = lab->digest();
	std::string why;
	const struct {
		const char *command;
		const char *reason;
	} refused[] = {
		{ "nonsense", "not JSON" },
		{ R"({"go": "heat"})", "\"do\"" },
		{ R"({"do": "dance"})", "no command" },
		{ R"({"do": "heat", "burner": "flask", "vessel": "flask"})", "not a burner" },
		{ R"({"do": "heat", "burner": "burner", "vessel": "kettle"})", "no \"kettle\"" },
		{ R"({"do": "valve", "burner": "burner", "to": 2})", "between 0 and 1" },
		{ R"({"do": "pour", "from": "flask", "to": "beaker", "all": true})", "nothing of that" },
		{ R"({"do": "pour", "from": "acid", "to": "flask", "ml": 900})", "does not hold that much" },
		{ R"({"do": "pour", "from": "acid", "to": "jar", "ml": 200})", "does not fit" },
		{ R"({"do": "pour", "from": "acid", "to": "acid", "all": true})", "itself" },
		{ R"({"do": "pour", "from": "acid", "to": "flask"})", "\"ml\" or \"all\"" },
		{ R"({"do": "tip", "from": "jar", "to": "flask", "species": "water", "all": true})", "not a solid" },
		{ R"({"do": "tip", "from": "jar", "to": "flask", "species": "malachite", "g": 50})", "does not hold that much" },
		{ R"({"do": "tip", "from": "flask", "to": "jar", "species": "malachite", "all": true})", "nothing of that" },
		{ R"({"do": "attach", "item": "flask", "to": "balloon"})", "vessel's mouth" },
		{ R"({"do": "detach", "item": "balloon"})", "not on anything" },
		{ R"({"do": "unheat", "burner": "burner"})", "not under anything" },
		{ R"({"do": "wait_until", "item": "flask", "below_g": 1})", "needs a \"species\"" },
		{ R"({"do": "wait_until", "item": "flask", "species": "water", "above_g": 1, "timeout": 0.5})", "timeout" },
	};
	for (const auto &r : refused) {
		CAPTURE(r.command);
		CHECK_FALSE(run(*lab, r.command, &why));
		CHECK(why.find(r.reason) != std::string::npos);
		CHECK_FALSE(lab->failed());
	}
	// only the timed-out wait moved the clock; nothing else changed anything
	const json report = json::parse(lab->report());
	CHECK(report.at("step") == 30);
	CHECK(grams(item(report, "acid"), "water") + grams(item(report, "acid"), "steam") == doctest::Approx(240.5).epsilon(1e-6)); // none poured
	CHECK(grams(item(report, "flask"), "water") == 0.0);
	CHECK(grams(item(report, "jar"), "malachite") == doctest::Approx(5.0).epsilon(1e-9));
	CHECK(report.at("conserved").at("unchanged").get<bool>());
	(void)before;
}

TEST_CASE("lab: what would seal a vessel is refused; mouths and burners are single (Stage 7)") {
	auto lab = demo_bench();
	std::string why;
	REQUIRE(run(*lab, R"({"do": "attach", "item": "balloon", "to": "flask"})"));
	CHECK_FALSE(run(*lab, R"({"do": "attach", "item": "condenser", "to": "flask"})", &why)); // the mouth is taken
	CHECK(why.find("taken") != std::string::npos);
	CHECK_FALSE(run(*lab, R"({"do": "attach", "item": "balloon", "to": "beaker"})", &why)); // it is on the flask
	CHECK_FALSE(run(*lab, R"({"do": "submit", "item": "flask"})", &why)); // still joined
	// the beaker on the condenser, then the condenser on the beaker: the pair would have no way out
	REQUIRE(run(*lab, R"({"do": "attach", "item": "beaker", "to": "condenser"})"));
	CHECK_FALSE(run(*lab, R"({"do": "attach", "item": "condenser", "to": "beaker"})", &why));
	REQUIRE(run(*lab, R"({"do": "heat", "burner": "burner", "vessel": "flask"})"));
	CHECK_FALSE(run(*lab, R"({"do": "heat", "burner": "burner", "vessel": "beaker"})", &why));
	CHECK(why.find("already under") != std::string::npos);
	REQUIRE(run(*lab, R"({"do": "unheat", "burner": "burner"})"));
	CHECK(run(*lab, R"({"do": "heat", "burner": "burner", "vessel": "beaker"})"));
	REQUIRE(run(*lab, R"({"do": "detach", "item": "balloon"})"));
	CHECK(run(*lab, R"({"do": "submit", "item": "balloon"})"));
	CHECK(run(*lab, R"({"do": "wait", "seconds": 5})")); // every invariant, on every step of this tangle
	CHECK_FALSE(lab->failed());
}

TEST_CASE("lab: pouring and tipping move exactly what was asked, with its heat (Stage 7)") {
	auto lab = demo_bench();
	REQUIRE(run(*lab, R"({"do": "pour", "from": "acid", "to": "flask", "ml": 50})"));
	REQUIRE(run(*lab, R"({"do": "tip", "from": "jar", "to": "beaker", "species": "malachite", "g": 2})"));
	REQUIRE(run(*lab, R"({"do": "wait", "seconds": 1})"));
	const json report = json::parse(lab->report());
	// 50 mL of the 241.2 mL of water, and the same share of the acid dissolved in it
	const double share = 50.0 / (240.5 / 0.997048);
	// (a few milligrams of the water are vapour over each liquid by now)
	CHECK(grams(item(report, "flask"), "water") + grams(item(report, "flask"), "steam") == doctest::Approx(240.5 * share).epsilon(1e-4));
	CHECK(grams(item(report, "flask"), "sulfuric_acid_aq") / 98.072 == doctest::Approx(0.25 * share).epsilon(1e-6));
	CHECK(grams(item(report, "acid"), "water") + grams(item(report, "acid"), "steam") == doctest::Approx(240.5 * (1.0 - share)).epsilon(1e-4));
	CHECK(grams(item(report, "acid"), "sulfuric_acid_aq") / 98.072 == doctest::Approx(0.25 * (1.0 - share)).epsilon(1e-6));
	CHECK(grams(item(report, "beaker"), "malachite") == doctest::Approx(2.0).epsilon(1e-9));
	CHECK(grams(item(report, "jar"), "malachite") == doctest::Approx(3.0).epsilon(1e-9));
	// the powder's grains went with it, in proportion
	const json beaker = item(report, "beaker");
	for (const json &row : beaker.at("contents")) {
		if (row.at("species") == "malachite") {
			const int64_t moved = row.at("particles").get<int64_t>(); // two fifths of 2.5 million, rounded down
			CHECK(moved >= 999999);
			CHECK(moved <= 1000000);
		}
	}
	// the flask is still exactly full: the liquid pushed air out
	const json flask = item(report, "flask");
	CHECK(flask.at("gas_ml").get<double>() + flask.at("condensed_ml").get<double>() == doctest::Approx(250.0).epsilon(1e-6));
	CHECK(report.at("conserved").at("unchanged").get<bool>());
}

TEST_CASE("lab: the same commands give the same state, however the steps are grouped and whether or not they are checked (Stage 7)") {
	// D1 and D4 in the small: a short, busy session four ways
	const auto play = [](bool check, int group) {
		auto lab = demo_bench();
		lab->set_checking(check);
		std::string why;
		REQUIRE(run(*lab, R"({"do": "heat", "burner": "burner", "vessel": "flask"})"));
		REQUIRE(run(*lab, R"({"do": "tip", "from": "jar", "to": "flask", "species": "malachite", "all": true})"));
		REQUIRE(run(*lab, R"({"do": "attach", "item": "balloon", "to": "flask"})"));
		REQUIRE(run(*lab, R"({"do": "valve", "burner": "burner", "to": 1})"));
		for (int done = 0; done < 18000; done += group) { // five minutes: the malachite is decomposing
			REQUIRE(lab->advance(group, why));
		}
		return lab->digest();
	};
	const std::string reference = play(true, 18000);
	CHECK(play(true, 1) == reference); // a step at a time
	CHECK(play(true, 600) == reference); // ten seconds a frame
	CHECK(play(false, 8) == reference); // unchecked, untraced
	CHECK(play(false, 18000) == reference);
}

TEST_CASE("lab: the demo session reproduces its golden record, and its results are what was measured (Stage 7)") {
	// D4. bench/sessions/demo.jsonl: a digest of the whole state after every command
	const json session = json::parse(read("data/sessions/demo.json"), nullptr, true, true);
	std::istringstream golden(read("bench/sessions/demo.jsonl"));
	auto lab = demo_bench();
	int index = 0;
	for (const json &command : session.at("script")) {
		json plain = command;
		plain.erase("note");
		std::string why;
		REQUIRE_MESSAGE(lab->command(plain.dump(), why), "command " << index << ": " << why);
		std::string line;
		REQUIRE(std::getline(golden, line));
		const json recorded = json::parse(line);
		CAPTURE(index);
		CHECK(recorded.at("i") == index);
		CHECK(recorded.at("step") == lab->step());
		CHECK(recorded.at("digest") == lab->digest());
		++index;
	}
	std::string extra;
	CHECK_FALSE(std::getline(golden, extra));

	// GAME_SPEC §17: "done when"
	const json report = json::parse(lab->report());
	CHECK(report.at("conserved").at("unchanged").get<bool>());
	const json flask = item(report, "flask"), beaker = item(report, "beaker"), balloon = item(report, "balloon");
	CHECK(flask.at("submitted").get<bool>());
	CHECK(beaker.at("submitted").get<bool>());
	CHECK(balloon.at("submitted").get<bool>());
	// a sealed balloon of CO2
	CHECK(grams(balloon, "carbon_dioxide") > 0.8);
	CHECK(balloon.at("on").is_null());
	// a beaker of water and nothing else that is not air
	CHECK(grams(beaker, "water") > 35.0);
	for (const json &row : beaker.at("contents")) {
		const std::string s = row.at("species");
		CHECK((s == "water" || s == "steam" || s == "nitrogen" || s == "oxygen" || s == "glass"));
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
			CHECK(row.at("particles").get<int64_t>() > 1000);
			CHECK(row.at("size_um").get<double>() > 50.0);
		}
	}
}
