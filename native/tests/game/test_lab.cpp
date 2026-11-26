// The lab layer (GAME_SPEC §13): a bench driven by commands.
#include "lab/lab.hpp"
#include "lab/glassware.hpp"
#include "lab/sha256.hpp"
#include "chem/species.hpp"
#include "chem/thermo.hpp"

#include <doctest/doctest.h>
#include <nlohmann/json.hpp>

#include "fixtures.hpp"
#include "part.hpp"

#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

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

double mol(const json &item, const char *species) {
	for (const json &row : item.at("contents")) {
		if (row.at("species") == species) {
			return row.at("mol").get<double>();
		}
	}
	return 0.0;
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
	// a conical flask of 250 mL holds what its shape holds to its neck's top (GAME_SPEC §12.3, Stage 24)
	CHECK(flask.at("gas_ml").get<double>() == doctest::Approx(lab::glass::find("conical_flask", 250)->capacity()).epsilon(1e-6));
	CHECK(flask.at("gas_ml").get<double>() > 330.0);
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
	CHECK(lab::Lab::open(R"({"format": "chem-bench/1", "room": {"celsius": 20}, "grid": [2, 1], "items": [
		{"id": "a", "at": [0, 0], "kind": "balloon", "wall": "latex", "wall_g": 2}, {"id": "b", "at": [0, 0], "kind": "balloon", "wall": "latex", "wall_g": 2}]})", why) == nullptr); // one cell, two items
	CHECK(lab::Lab::open(R"({"format": "chem-bench/1", "room": {"celsius": 20}, "grid": [2, 1], "items": [
		{"id": "a", "at": [5, 0], "kind": "balloon", "wall": "latex", "wall_g": 2}]})", why) == nullptr); // off the grid
	CHECK(lab::Lab::open(R"({"format": "chem-bench/1", "room": {"celsius": 20}, "items": [
		{"id": "tray", "kind": "balloon", "wall": "latex", "wall_g": 2}]})", why) == nullptr); // the bench's own name
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
		{ R"({"do": "pour", "from": "flask", "to": "beaker", "all": true})", "holds no liquid" },
		{ R"({"do": "pour", "from": "acid", "to": "flask", "ml": 900})", "does not hold that much" },
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
		{ R"({"do": "place", "item": "flask", "at": [3, 5]})", "stands there" }, // over the acid's footprint
		{ R"({"do": "place", "item": "flask", "at": [40, 1]})", "off the bench" },
		{ R"({"do": "place", "item": "flask"})", "[column, row]" },
		{ R"({"do": "place", "item": "tray", "at": [0, 0]})", "cannot be moved" },
		{ R"({"do": "pour", "from": "acid", "to": "tray", "ml": 5})", "not a vessel" },
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
	// none poured: what is missing is the steam the bottle's settling gas took out (its solution takes room: Stage 19)
	CHECK(grams(item(report, "acid"), "water") + grams(item(report, "acid"), "steam") == doctest::Approx(240.5).epsilon(1e-5));
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
	// 50 mL of the 241.2 mL of water (at 25 C; Stage 67: at the acid's own temperature as it is poured, 17.4 C once its
	// bisulfate has settled, Stage 63), and the same share of the acid dissolved in it
	const double poured_at = item(report, "acid").at("celsius").get<double>() + 273.15;
	const double share = 50.0 / (240.5 / 0.997048 * chem::expansion(chem::builtin_species(), poured_at));
	// (a few milligrams of the water are vapour over each liquid by now)
	CHECK(grams(item(report, "flask"), "water") + grams(item(report, "flask"), "steam") == doctest::Approx(240.5 * share).epsilon(1e-4));
	// (the pour takes seconds, Stage 27: a little water evaporates from what is left meanwhile, and the last drops
	// are that much stronger)
	// (the acid is its ions, Stage 63: its sulfate, free and as bisulfate)
	const auto sulfate = [&](const char *id) { return mol(item(report, id), "sulfate_ion") + mol(item(report, id), "hydrogen_sulfate_ion"); };
	CHECK(sulfate("flask") == doctest::Approx(0.25 * share).epsilon(1e-4));
	CHECK(grams(item(report, "acid"), "water") + grams(item(report, "acid"), "steam") == doctest::Approx(240.5 * (1.0 - share)).epsilon(1e-4));
	CHECK(sulfate("acid") == doctest::Approx(0.25 * (1.0 - share)).epsilon(1e-4));
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
	CHECK(flask.at("gas_ml").get<double>() + flask.at("condensed_ml").get<double>() == doctest::Approx(lab::glass::find("conical_flask", 250)->capacity()).epsilon(1e-6));
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

// (the demo session's golden record and its results, Stage 7: test_demo_record.cpp, which plays it once for all that
// holds it; Stage 68)

// ---------------------------------------------------------------- Stage 8

namespace {

// a small bench for the equipment's rules: everything in the cabinet but the flask
const char *const SMALL = R"({"format": "chem-bench/1", "grid": [4, 2], "room": {"celsius": 20}, "items": [
	{"id": "flask", "at": [1, 0], "kind": "vessel", "volume_ml": 250, "wall": "glass", "wall_g": 100, "loss": 0.9, "contents": {"water": {"g": 60}}},
	{"id": "cup", "kind": "vessel", "volume_ml": 20, "wall": "glass", "wall_g": 15, "loss": 0.3},
	{"id": "tube", "kind": "vessel", "volume_ml": 50, "wall": "glass", "wall_g": 50, "loss": 0.1, "coolant": {"celsius": 15, "conductance": 10}},
	{"id": "bag", "kind": "balloon", "wall": "latex", "wall_g": 2, "loss": 0.5, "capacity_ml": 300},
	{"id": "flame", "kind": "burner", "celsius": 1927, "conductance": 0.25}]})";

std::unique_ptr<lab::Lab> small_bench() {
	std::string why;
	auto made = lab::Lab::open(SMALL, why);
	REQUIRE_MESSAGE(made != nullptr, why);
	return made;
}

} // namespace

TEST_CASE("lab: equipment is used on the bench, and placed there from Supplies (Stage 8; Stage 61)") {
	auto lab = small_bench();
	std::string why;
	// (Stage 61) what the bench gives without a square is a supply: no item of it until one is taken out
	const json start = json::parse(lab->report());
	CHECK(std::any_of(start.at("supplies").begin(), start.at("supplies").end(), [](const json &s) { return s.at("id") == "cup"; }));
	const std::string digest = lab->digest();
	CHECK_FALSE(run(*lab, R"({"do": "heat", "burner": "flame", "vessel": "cup"})", &why)); // a copy taken out is in storage, not on the bench
	CHECK(why.find("in storage") != std::string::npos);
	CHECK(lab->digest() == digest); // refused: nothing was taken out
	CHECK_FALSE(run(*lab, R"({"do": "attach", "item": "bag", "to": "cup"})", &why));
	CHECK_FALSE(run(*lab, R"({"do": "pour", "from": "flask", "to": "cup", "ml": 5})", &why));
	CHECK(lab->digest() == digest);
	REQUIRE(run(*lab, R"({"do": "place", "item": "flame", "at": [0, 0]})"));
	REQUIRE(run(*lab, R"({"do": "place", "item": "cup", "at": [2, 0]})"));
	CHECK_FALSE(run(*lab, R"({"do": "place", "item": "bag", "at": [2, 0]})", &why)); // the cup stands there
	REQUIRE(run(*lab, R"({"do": "place", "item": "cup", "at": [3, 1]})")); // along the bench
	REQUIRE(run(*lab, R"({"do": "heat", "burner": "flame", "vessel": "flask"})"));
	CHECK_FALSE(run(*lab, R"({"do": "place", "item": "flask", "at": [0, 1]})", &why)); // the burner depends on where it stands
	CHECK_FALSE(run(*lab, R"({"do": "stow", "item": "flask"})", &why));
	REQUIRE(run(*lab, R"({"do": "attach", "item": "bag", "to": "flask"})")); // from Supplies straight onto the mouth
	json report = json::parse(lab->report());
	CHECK(item(report, "bag").at("place") == "on");
	CHECK(item(report, "flame").at("place") == "under");
	CHECK(item(report, "cup").at("at") == json::array({ 3, 1 }));
	CHECK(report.at("grid") == json::array({ 4, 2 }));
	REQUIRE(run(*lab, R"({"do": "detach", "item": "bag"})"));
	CHECK(item(json::parse(lab->report()), "bag").at("place") == "bench"); // set down beside the flask
	REQUIRE(run(*lab, R"({"do": "stow", "item": "bag"})"));
	REQUIRE(run(*lab, R"({"do": "submit", "item": "flask"})")); // lifted off its burner
	report = json::parse(lab->report());
	CHECK(item(report, "flask").at("place") == "outbox");
	CHECK(item(report, "flame").at("place") == "bench");
	CHECK(item(report, "flame").at("under").is_null());
	CHECK_FALSE(run(*lab, R"({"do": "pour", "from": "flask", "to": "cup", "ml": 5})", &why)); // (no glassware: it does not pour)
	CHECK(run(*lab, R"({"do": "wait", "seconds": 2})"));
}

TEST_CASE("lab: on a bench of squares, an item stands on a footprint of them, and nothing overlaps (Stage 13)") {
	// chem-bench/2: the grid in squares; a footprint by kind and volume, or the bench's own "size"
	const char *const SQUARES = R"({"format": "chem-bench/2", "grid": [12, 8], "room": {"celsius": 20}, "items": [
		{"id": "flask", "at": [4, 1], "kind": "vessel", "volume_ml": 250, "wall": "glass", "wall_g": 100, "loss": 0.9, "contents": {"water": {"g": 60}}},
		{"id": "cup", "kind": "vessel", "volume_ml": 20, "wall": "glass", "wall_g": 15, "loss": 0.3},
		{"id": "tube", "kind": "vessel", "volume_ml": 50, "wall": "glass", "wall_g": 50, "loss": 0.1, "coolant": {"celsius": 15, "conductance": 10}},
		{"id": "bag", "kind": "balloon", "wall": "latex", "wall_g": 2, "loss": 0.5, "capacity_ml": 300},
		{"id": "slab", "kind": "vessel", "volume_ml": 20, "size": [5, 1], "wall": "glass", "wall_g": 15, "loss": 0.3},
		{"id": "flame", "kind": "burner", "celsius": 1927, "conductance": 0.25}]})";
	std::string why;
	auto lab = lab::Lab::open(SQUARES, why);
	REQUIRE_MESSAGE(lab != nullptr, why);
	const auto size = [&](const char *id) {
		for (const lab::ItemView &v : lab->view().items) {
			if (v.id == id) {
				return std::make_pair(v.w, v.h);
			}
		}
		for (const lab::ItemView &v : lab->view().supplies) { // (Stage 61: a supply's, as a copy of it is)
			if (v.id == id) {
				return std::make_pair(v.w, v.h);
			}
		}
		FAIL("no item " << id);
		return std::make_pair(0, 0);
	};
	CHECK(size("flask") == std::make_pair(3, 7));
	CHECK(size("cup") == std::make_pair(2, 6));
	CHECK(size("tube") == std::make_pair(2, 5));
	CHECK(size("bag") == std::make_pair(4, 5));
	CHECK(size("flame") == std::make_pair(3, 3));
	CHECK(size("slab") == std::make_pair(5, 1));
	// the flask covers columns 4-6 and rows 1-7: any footprint over one of its squares is refused
	CHECK_FALSE(run(*lab, R"({"do": "place", "item": "cup", "at": [6, 2]})", &why));
	CHECK(why.find("\"flask\" stands there") != std::string::npos);
	CHECK_FALSE(run(*lab, R"({"do": "place", "item": "slab", "at": [0, 7]})", &why)); // its last square is the flask's
	CHECK_FALSE(run(*lab, R"({"do": "place", "item": "cup", "at": [11, 2]})", &why)); // half off the grid
	CHECK(why.find("off the bench") != std::string::npos);
	REQUIRE(run(*lab, R"({"do": "place", "item": "cup", "at": [7, 2]})")); // beside it, touching
	REQUIRE(run(*lab, R"({"do": "place", "item": "slab", "at": [0, 0]})"));
	REQUIRE(run(*lab, R"({"do": "place", "item": "cup", "at": [7, 1]})")); // along the bench, over its own old squares
	// set down beside: the first free footprint whose foot is level with the flask's, the nearest column first
	REQUIRE(run(*lab, R"({"do": "heat", "burner": "flame", "vessel": "flask"})"));
	REQUIRE(run(*lab, R"({"do": "unheat", "burner": "flame"})"));
	const auto flame = [&]() {
		for (const lab::ItemView &v : lab->view().items) {
			if (v.id == "flame") {
				return v;
			}
		}
		FAIL("no flame");
		return lab::ItemView{};
	};
	CHECK(flame().place == "bench");
	CHECK(flame().col == 1);
	CHECK(flame().row == 5); // its foot on row 7, the flask's
	REQUIRE(run(*lab, R"({"do": "stow", "item": "flame"})"));
	REQUIRE(run(*lab, R"({"do": "heat", "burner": "flame", "vessel": "flask"})"));
	REQUIRE(run(*lab, R"({"do": "submit", "item": "flask"})")); // the burner stays where the flask stood
	CHECK(flame().place == "bench");
	CHECK(flame().col == 4);
	CHECK(flame().row == 5);
	// a bench of one item per cell (chem-bench/1) is read as before: every footprint one square
	auto cells = small_bench();
	for (const lab::ItemView &v : cells->view().items) {
		CHECK(v.w == 1);
		CHECK(v.h == 1);
	}
	// a footprint is saved, and loaded
	auto again = lab::Lab::load(lab->save(), why);
	REQUIRE_MESSAGE(again != nullptr, why);
	bool slab = false;
	for (const lab::ItemView &v : again->view().items) {
		if (v.id == "slab") {
			slab = true;
			CHECK(v.w == 5);
			CHECK(v.col == 0);
		}
	}
	CHECK(slab);
}

TEST_CASE("lab: a balloon filled past its capacity bursts, and its gas is the room's (Stage 8)") {
	auto lab = small_bench();
	std::string why;
	REQUIRE(run(*lab, R"({"do": "place", "item": "flame", "at": [0, 0]})"));
	REQUIRE(run(*lab, R"({"do": "heat", "burner": "flame", "vessel": "flask"})"));
	REQUIRE(run(*lab, R"({"do": "attach", "item": "bag", "to": "flask"})"));
	REQUIRE(run(*lab, R"({"do": "valve", "burner": "flame", "to": 1})"));
	// 60 g of water boiling into a 300 mL balloon in a 20 C room: steam condenses in
	// it, but the flask's air and the steam on its way fill it
	REQUIRE(run(*lab, R"({"do": "wait", "seconds": 400})", &why));
	const json report = json::parse(lab->report());
	REQUIRE(report.at("events").size() == 1);
	CHECK(report.at("events")[0].at("what") == "burst");
	CHECK(report.at("events")[0].at("item") == "bag");
	const json bag = item(report, "bag");
	CHECK(bag.at("burst").get<bool>());
	CHECK(bag.at("on").is_null());
	CHECK(bag.at("gas_ml").get<double>() == 0.0); // its gas went to the room
	CHECK(bag.at("gas_ml").get<double>() + bag.at("condensed_ml").get<double>() <= 300.0);
	CHECK(report.at("conserved").at("unchanged").get<bool>());
	CHECK_FALSE(lab->failed()); // the flask was never sealed: its mouth opened as the balloon came off
	CHECK_FALSE(run(*lab, R"({"do": "attach", "item": "bag", "to": "flask"})", &why));
	CHECK(why.find("burst") != std::string::npos);
}

TEST_CASE("lab: a vessel filled past its brim overflows into the tray (Stage 8)") {
	// 60 g of water distilled into a 20 mL cup
	auto lab = small_bench();
	std::string why;
	REQUIRE(run(*lab, R"({"do": "place", "item": "flame", "at": [0, 0]})"));
	REQUIRE(run(*lab, R"({"do": "place", "item": "cup", "at": [3, 0]})"));
	REQUIRE(run(*lab, R"({"do": "heat", "burner": "flame", "vessel": "flask"})"));
	REQUIRE(run(*lab, R"({"do": "attach", "item": "tube", "to": "flask"})"));
	REQUIRE(run(*lab, R"({"do": "under", "item": "cup", "of": "tube"})"));
	REQUIRE(run(*lab, R"({"do": "valve", "burner": "flame", "to": 1})"));
	REQUIRE_MESSAGE(run(*lab, R"({"do": "wait_until", "item": "flask", "species": "water", "below_g": 5, "timeout": 1200})", &why), why); // V2 on every step
	const json report = json::parse(lab->report());
	const json cup = item(report, "cup"), tray = item(report, "tray");
	CHECK(cup.at("condensed_ml").get<double>() == doctest::Approx(20.0).epsilon(1e-6)); // full to the brim
	CHECK(cup.at("gas_ml").get<double>() < 1e-6);
	CHECK(grams(tray, "water") + grams(tray, "steam") > 30.0); // the rest ran over
	CHECK(grams(cup, "water") + grams(tray, "water") + grams(tray, "steam") + grams(item(report, "flask"), "water") > 59.0); // nearly all accounted for as liquid
	CHECK(tray.at("kind") == "tray");
	CHECK(report.at("conserved").at("unchanged").get<bool>());
}

// ctest: parts 8
// ctest: fixture demo
TEST_CASE("lab: a saved lab, loaded, continues exactly as the one that was not saved (Stage 8)") {
	// D2. The demo, saved after each of several commands, in the middle of each stage: the loaded lab is the saved
	// one, saved again it is the same text, and it reproduces the golden record to the next cut (the last to its
	// end), every invariant held at every step. (Stage 68: the saves from the fixture `demo`, test_demo_record.cpp;
	// the windows together check every step of the session after its first command, the gates' one checked play of
	// it, a cut an entry, the long cooling and its wait (commands 19 and 20) in windows of their own. A save after
	// command 1, the malachite just tipped in, does not continue so: the open issue in docs/TDD_68_GATES_FAST.md.)
	const json session = json::parse(read("data/sessions/demo.json"), nullptr, true, true);
	std::vector<json> golden;
	{
		std::istringstream lines(read("bench/sessions/demo.jsonl"));
		std::string line;
		while (std::getline(lines, line)) {
			golden.push_back(json::parse(line));
		}
	}
	const auto command = [&](size_t i) {
		json plain = session.at("script")[i];
		plain.erase("note");
		return plain.dump();
	};
	const std::vector<size_t> cuts{ 0, 4, 9, 12, 17, 18, 19, 20 };
	for (size_t k = 0; k < cuts.size(); ++k) {
		if (static_cast<int>(k % static_cast<size_t>(part().count)) != part().index) {
			continue; // (as ctest runs it, a cut an entry; under the sanitizers the first alone)
		}
		const size_t cut = cuts[k];
		CAPTURE(cut);
		const std::string saved = fixtures::read("demo", std::to_string(cut) + ".json");
		std::string why;
		auto loaded = lab::Lab::load(saved, why);
		REQUIRE_MESSAGE(loaded != nullptr, why);
		loaded->set_checking(true);
		CHECK(loaded->digest() == golden[cut].at("digest").get<std::string>());
		CHECK(loaded->step() == golden[cut].at("step").get<int64_t>());
		CHECK(json::parse(loaded->report()) == json::parse(fixtures::read("demo", std::to_string(cut) + ".report.json")));
		CHECK(loaded->save() == saved); // saving what was loaded gives the same text
		const size_t until = k + 1 < cuts.size() ? cuts[k + 1] + 1 : golden.size();
		for (size_t i = cut + 1; i < until; ++i) {
			REQUIRE_MESSAGE(loaded->command(command(i), why), "command " << i << ": " << why);
			REQUIRE_MESSAGE(loaded->digest() == golden[i].at("digest").get<std::string>(), "command " << i << " departs from the record");
		}
	}
}

TEST_CASE("lab: a vessel's wall is as thick as its bench says, apart from its mass, and a save keeps it (Stage 20)") {
	// what is seen through a vessel bends by its wall's thickness (GAME_SPEC §12.3): data of its own; from
	// Stage 24 its glassware's standard's least wall (ISO 3819 for a beaker, ISO 1773 for a flask, ISO 4796-1
	// for a bottle), and a floor 2.36 walls thick (calibrated)
	auto lab = demo_bench();
	const auto wall_mm = [](const lab::Lab &l, const char *id) {
		for (const lab::ItemView &v : l.view().items) {
			if (v.id == id) {
				return v.wall_mm;
			}
		}
		for (const lab::ItemView &v : l.view().supplies) { // (Stage 61: a bench item without a square is a supply)
			if (v.id == id) {
				return v.wall_mm;
			}
		}
		FAIL("no item " << id);
		return -1.0;
	};
	CHECK(wall_mm(*lab, "jar") == doctest::Approx(0.8).epsilon(1e-12)); // a 50 mL beaker
	CHECK(wall_mm(*lab, "flask") == doctest::Approx(0.9).epsilon(1e-12)); // a 250 mL conical flask
	CHECK(wall_mm(*lab, "acid") == doctest::Approx(1.5).epsilon(1e-12)); // a 500 mL bottle
	CHECK(wall_mm(*lab, "condenser") == 0.8); // made to no standard: its bench's own
	CHECK(wall_mm(*lab, "balloon") == 0.0); // rubber: none given
	// and its floor, glass all through (Stage 21)
	const auto floor_mm = [](const lab::Lab &l, const char *id) {
		for (const lab::ItemView &v : l.view().items) {
			if (v.id == id) {
				return v.floor_mm;
			}
		}
		return -1.0;
	};
	CHECK(floor_mm(*lab, "jar") == doctest::Approx(0.8 * 2.36).epsilon(1e-12));
	CHECK(floor_mm(*lab, "flask") == doctest::Approx(0.9 * 2.36).epsilon(1e-12));
	CHECK(floor_mm(*lab, "acid") == doctest::Approx(1.5 * 2.36).epsilon(1e-12));
	CHECK(floor_mm(*lab, "condenser") == 1.9);
	CHECK(floor_mm(*lab, "balloon") == 0.0);
	std::string why;
	auto again = lab::Lab::load(lab->save(), why);
	REQUIRE_MESSAGE(again != nullptr, why);
	CHECK(wall_mm(*again, "acid") == wall_mm(*lab, "acid"));
	CHECK(floor_mm(*again, "acid") == floor_mm(*lab, "acid"));
	CHECK(again->digest() == lab->digest());
	// a save from before walls had a thickness loads, its walls of none
	json old = json::parse(lab->save());
	for (json &item : old.at("bench").at("items")) {
		item.erase("wall_mm");
		item.erase("floor_mm");
	}
	auto older = lab::Lab::load(old.dump(), why);
	REQUIRE_MESSAGE(older != nullptr, why);
	CHECK(wall_mm(*older, "acid") == 0.0);
	CHECK(floor_mm(*older, "acid") == 0.0);
	CHECK(lab::Lab::open(R"({"format": "chem-bench/1", "room": {"celsius": 20}, "items": [
		{"id": "a", "kind": "vessel", "volume_ml": 50, "wall": "glass", "wall_g": 20, "floor_mm": -1}]})", why) == nullptr);
	// a bench that gives none has a wall of none; less than none is refused
	auto plain = lab::Lab::open(R"({"format": "chem-bench/1", "room": {"celsius": 20}, "items": [
		{"id": "a", "kind": "vessel", "volume_ml": 50, "wall": "glass", "wall_g": 20}]})", why);
	REQUIRE_MESSAGE(plain != nullptr, why);
	CHECK(wall_mm(*plain, "a") == 0.0);
	CHECK(lab::Lab::open(R"({"format": "chem-bench/1", "room": {"celsius": 20}, "items": [
		{"id": "a", "kind": "vessel", "volume_ml": 50, "wall": "glass", "wall_g": 20, "wall_mm": -1}]})", why) == nullptr);
	CHECK(why.find("wall") != std::string::npos);
}

TEST_CASE("lab: a save that is not one is refused (Stage 8)") {
	std::string why;
	CHECK(lab::Lab::load("not json", why) == nullptr);
	CHECK(lab::Lab::load(R"({"format": "chem-bench/1"})", why) == nullptr);
	auto lab = small_bench();
	json saved = json::parse(lab->save());
	{
		json other = saved;
		other["species"] = saved["species"].get<int>() + 1; // made with other data: more species than the tables have
		CHECK(lab::Lab::load(other.dump(), why) == nullptr);
		CHECK(why.find("other data") != std::string::npos);
		other["species"] = 3; // fewer, but its cells hold more than three (a save from before the tables grew holds as many as it says: Stage 43)
		CHECK(lab::Lab::load(other.dump(), why) == nullptr);
		CHECK(why.find("do not match") != std::string::npos);
	}
	{
		json other = saved;
		other["cells"][0]["mixing"] = "not bits";
		CHECK(lab::Lab::load(other.dump(), why) == nullptr);
	}
	{
		json other = saved;
		other["bench"]["items"][0]["cell"] = 99;
		CHECK(lab::Lab::load(other.dump(), why) == nullptr);
		CHECK(why.find("outside") != std::string::npos);
	}
	{
		json other = saved;
		other.erase("edges");
		CHECK(lab::Lab::load(other.dump(), why) == nullptr);
	}
	CHECK(lab::Lab::load(saved.dump(), why) != nullptr);
}

// Found by the tutorial played as a person (Stage 12): an idle vessel whose gas
// filled it to the last bit. The settling expelled a quantum of each gas because
// its own arithmetic found it a hair over-full, while T1, adding the same
// volumes in another order, found it exactly full. The state is kept as it was
// saved, half a lab-minute before the step that failed.
TEST_CASE("lab: a vessel full to the last bit expels nothing T1 would not, by the same arithmetic (Stage 12)") {
	std::string why;
	std::unique_ptr<lab::Lab> lab = lab::Lab::load(read("bench/states/t1_expulsion_rounding.json"), why);
	REQUIRE_MESSAGE(lab != nullptr, why);
	lab->set_checking(false);
	REQUIRE(lab->advance(1111000 - lab->step(), why));
	lab->set_checking(true); // every invariant on every step past the one that failed (1,111,034)
	CHECK_MESSAGE(lab->advance(100, why), why);
	CHECK_FALSE(lab->failed());
}

TEST_CASE("lab: several items move along the bench together, all of them or none (Stage 13)") {
	auto lab = demo_bench(); // jar [0, 9], acid [2, 8], balloon [5, 10], flask [9, 8], condenser [12, 10], beaker [14, 8], waste [17, 8], burner [20, 12]
	const std::string before = lab->digest();
	std::string why;
	const auto at = [&](const char *id) {
		for (const lab::ItemView &v : lab->view().items) {
			if (v.id == id) {
				return std::make_pair(v.col, v.row);
			}
		}
		FAIL("no item " << id);
		return std::make_pair(0, 0);
	};
	// into one another's old squares is fine: they move as one
	REQUIRE_MESSAGE(run(*lab, R"({"do": "move", "items": ["beaker", "waste"], "by": [2, -3]})", &why), why); // the beaker onto the waste's old squares
	CHECK(at("beaker") == std::make_pair(16, 5));
	CHECK(at("waste") == std::make_pair(19, 5));
	CHECK(run(*lab, R"({"do": "move", "items": ["beaker", "waste"], "by": [0, 0]})", &why)); // standing still is a move
	// one of them would land on something left behind: neither moves
	CHECK_FALSE(run(*lab, R"({"do": "move", "items": ["jar", "acid"], "by": [1, 0]})", &why));
	CHECK(why.find("\"balloon\" stands where \"acid\" would go") != std::string::npos);
	CHECK(at("jar") == std::make_pair(0, 9));
	CHECK_FALSE(run(*lab, R"({"do": "move", "items": ["jar", "acid"], "by": [-1, 0]})", &why)); // the jar off the grid
	CHECK(why.find("off the bench") != std::string::npos);
	CHECK(at("acid") == std::make_pair(2, 8));
	CHECK_FALSE(run(*lab, R"({"do": "move", "items": ["jar", "jar"], "by": [0, -1]})", &why));
	CHECK(why.find("twice") != std::string::npos);
	CHECK_FALSE(run(*lab, R"({"do": "move", "items": ["jar", "kettle"], "by": [0, -1]})", &why));
	CHECK_FALSE(run(*lab, R"({"do": "move", "items": ["jar"]})", &why));
	REQUIRE(run(*lab, R"({"do": "move", "items": ["jar", "acid", "balloon"], "by": [0, -1]})", &why));
	CHECK(at("jar") == std::make_pair(0, 8));
	CHECK(at("balloon") == std::make_pair(5, 9));
	// where things stand is not the world: moving them changed nothing physical
	CHECK(lab->digest() == before);
	REQUIRE(run(*lab, R"({"do": "heat", "burner": "burner", "vessel": "flask"})"));
	CHECK_FALSE(run(*lab, R"({"do": "move", "items": ["flask", "jar"], "by": [0, -1]})", &why)); // the burner under it depends on where it stands
	CHECK(why.find("depends") != std::string::npos);
}

TEST_CASE("lab: a drain comes off when either of its vessels is moved away, stowed or handed in (Stage 15)") {
	// demo bench: condenser [12, 10] (2 x 5), beaker [14, 8], waste [17, 8]
	std::string why;
	const auto drains = [](lab::Lab &l) {
		int open = 0;
		const json report = json::parse(l.report());
		for (const lab::ItemView &v : l.view().items) {
			open += v.drains_to >= 0 ? 1 : 0;
		}
		(void)report;
		return open;
	};
	const auto joined = [&]() {
		auto l = demo_bench();
		REQUIRE(run(*l, R"({"do": "under", "item": "beaker", "of": "condenser"})", &why));
		REQUIRE(drains(*l) == 1);
		return l;
	};
	{ // the vessel under it moved away: its outlet drips into nothing, and the condenser moves freely
		auto l = joined();
		REQUIRE_MESSAGE(run(*l, R"({"do": "place", "item": "beaker", "at": [20, 0]})", &why), why);
		CHECK(drains(*l) == 0);
		REQUIRE_MESSAGE(run(*l, R"({"do": "place", "item": "condenser", "at": [0, 0]})", &why), why);
	}
	{ // the condenser moved away from what stands under it
		auto l = joined();
		REQUIRE_MESSAGE(run(*l, R"({"do": "place", "item": "condenser", "at": [0, 0]})", &why), why);
		CHECK(drains(*l) == 0);
	}
	{ // stowed, either of them
		auto l = joined();
		REQUIRE_MESSAGE(run(*l, R"({"do": "stow", "item": "beaker"})", &why), why);
		CHECK(drains(*l) == 0);
		auto m = joined();
		REQUIRE_MESSAGE(run(*m, R"({"do": "stow", "item": "condenser"})", &why), why);
		CHECK(drains(*m) == 0);
	}
	{ // the beaker handed in: no longer fed by the outlet, liquid in the condenser stays in it
		auto l = joined();
		REQUIRE_MESSAGE(run(*l, R"({"do": "pour", "from": "acid", "to": "condenser", "ml": 20})", &why), why);
		REQUIRE_MESSAGE(run(*l, R"({"do": "submit", "item": "beaker"})", &why), why);
		CHECK(drains(*l) == 0);
		const double held = grams(item(json::parse(l->report()), "condenser"), "water");
		REQUIRE(run(*l, R"({"do": "wait", "seconds": 30})", &why));
		CHECK(grams(item(json::parse(l->report()), "condenser"), "water") == doctest::Approx(held).epsilon(1e-3)); // nothing runs down any more
		CHECK(json::parse(l->report()).at("conserved").at("unchanged").get<bool>());
	}
	{ // moved together, they stay joined; one of them alone, they part
		auto l = joined();
		REQUIRE_MESSAGE(run(*l, R"({"do": "move", "items": ["condenser", "beaker"], "by": [0, -1]})", &why), why);
		CHECK(drains(*l) == 1);
		REQUIRE_MESSAGE(run(*l, R"({"do": "move", "items": ["beaker"], "by": [6, -4]})", &why), why);
		CHECK(drains(*l) == 0);
	}
}

TEST_CASE("lab: every vessel of every bench is its glassware: the cell holds its capacity, its wall and floor are its standard's (Stage 24)") {
	// GAME_SPEC §12.3: the flask a conical flask (ISO 1773), the beakers and the jar low-form beakers (ISO 3819),
	// the acid's a screw-neck bottle (ISO 4796-1); the condenser is made to no standard yet
	const std::map<std::string, std::string> made_as = { { "flask", "conical_flask" }, { "beaker", "beaker" }, { "waste", "beaker" }, { "jar", "beaker" }, { "acid", "bottle" } };
	int vessels = 0;
	for (const char *bench : { "data/benches/demo.json", "data/benches/lab.json" }) {
		std::string why;
		auto l = lab::Lab::open(read(bench), why);
		REQUIRE_MESSAGE(l != nullptr, why);
		const lab::LabView seen = l->view();
		std::vector<lab::ItemView> all = seen.items; // (Stage 61: and the supplies, the free lab's all of them)
		all.insert(all.end(), seen.supplies.begin(), seen.supplies.end());
		for (const lab::ItemView &v : all) {
			if (v.kind != "vessel" || v.condenser) {
				continue;
			}
			CAPTURE(bench);
			CAPTURE(v.id);
			REQUIRE(made_as.count(v.id) == 1);
			CHECK(v.glassware == made_as.at(v.id));
			const lab::glass::Vessel *g = lab::glass::find(v.glassware, v.nominal_ml);
			REQUIRE(g != nullptr);
			CHECK(v.volume_ml == doctest::Approx(g->capacity()).epsilon(1e-9));
			CHECK(v.volume_ml > 1.10 * v.nominal_ml);
			CHECK(v.wall_mm == doctest::Approx(10.0 * g->wall).epsilon(1e-12));
			CHECK(v.floor_mm == doctest::Approx(10.0 * g->floor).epsilon(1e-12));
			++vessels;
		}
	}
	CHECK(vessels == 10);
	// glassware no standard makes, or a wall given beside it, is refused with the reason
	std::string why;
	CHECK(lab::Lab::open(R"({"format": "chem-bench/1", "room": {"celsius": 20}, "items": [
		{"id": "a", "kind": "vessel", "volume_ml": 75, "wall": "glass", "wall_g": 20, "glassware": "beaker"}]})", why) == nullptr);
	CHECK(why.find("75") != std::string::npos);
	CHECK(lab::Lab::open(R"({"format": "chem-bench/1", "room": {"celsius": 20}, "items": [
		{"id": "a", "kind": "vessel", "volume_ml": 50, "wall": "glass", "wall_g": 20, "glassware": "beaker", "wall_mm": 1.0}]})", why) == nullptr);
	CHECK(why.find("glassware gives its wall") != std::string::npos);
}
