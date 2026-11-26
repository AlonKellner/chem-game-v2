// Pressure in the lab (GAME_SPEC §13, Stage 28): a rubber stopper seals a neck and pops at its hold over the
// neck's bore; a bottle's screw cap seals it and holds until its glass bursts; a sealed vessel cooled boils
// below 100 C; nothing pours in or out of what is sealed.
#include "lab/lab.hpp"

#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <memory>
#include <string>

namespace {

// a flask of water, a bottle of water, a stopper and a burner, on a bench of the lab's
const char *const BENCH = R"({
	"format": "chem-bench/2",
	"grid": [28, 15],
	"room": {"celsius": 20, "air_mol": 1000},
	"items": [
		{"id": "flask", "kind": "vessel", "volume_ml": 250, "wall": "glass", "glassware": "conical_flask", "wall_g": 100, "at": [2, 6],
			"contents": {"water": {"g": 100}}},
		{"id": "bottle", "kind": "vessel", "volume_ml": 500, "wall": "glass", "glassware": "bottle", "wall_g": 250, "at": [10, 6],
			"contents": {"water": {"g": 200}}},
		{"id": "beaker", "kind": "vessel", "volume_ml": 250, "wall": "glass", "glassware": "beaker", "wall_g": 80, "at": [18, 6]},
		{"id": "stopper", "kind": "stopper", "wall": "latex", "wall_g": 8, "hold_n": 30, "loss": 0.1},
		{"id": "burner", "kind": "burner", "celsius": 1927, "conductance": 0.25}
	]
})";

std::unique_ptr<lab::Lab> bench() {
	std::string why;
	auto l = lab::Lab::open(BENCH, why);
	REQUIRE_MESSAGE(l != nullptr, why);
	return l;
}

void ok(lab::Lab &l, const std::string &c) {
	std::string why;
	REQUIRE_MESSAGE(l.command(c, why), c << ": " << why);
}

lab::ItemView item(const lab::Lab &l, const std::string &id) {
	const lab::LabView v = l.view();
	const auto at = std::find_if(v.items.begin(), v.items.end(), [&](const lab::ItemView &x) { return x.id == id; });
	REQUIRE(at != v.items.end());
	return *at;
}

bool said(const lab::Lab &l, const std::string &words) {
	for (const lab::Notice &n : l.view().notices) {
		if (n.says.find(words) != std::string::npos) {
			return true;
		}
	}
	return false;
}

// advances a second at a time until `until` holds, or `seconds` have gone
template <class F>
double run_until(lab::Lab &l, double seconds, F until) {
	std::string why;
	for (int s = 0; s < static_cast<int>(seconds); ++s) {
		REQUIRE_MESSAGE(l.advance(60, why), why);
		if (until()) {
			return s + 1.0;
		}
	}
	return -1.0;
}

} // namespace

TEST_CASE("pressure: a stopper seals a flask's neck; heated, its pressure climbs, a warning first, and the stopper pops out at its hold over the bore, about 138 kPa; the flask is open again (Stage 28)") {
	auto l = bench();
	ok(*l, R"({"do": "attach", "item": "stopper", "to": "flask"})");
	CHECK(item(*l, "flask").sealed);
	CHECK(item(*l, "stopper").place == "on");
	const double pops = item(*l, "flask").gives_way_kpa;
	CHECK(pops == doctest::Approx(101.325 + 30.0 / (M_PI * 0.0161 * 0.0161) / 1000.0).epsilon(0.02)); // 30 N over a 32 mm bore
	// nothing pours into what is sealed
	std::string why;
	CHECK_FALSE(l->command(R"({"do": "pour", "from": "bottle", "to": "flask", "ml": 10})", why));
	CHECK(why.find("sealed") != std::string::npos);
	ok(*l, R"({"do": "heat", "burner": "burner", "vessel": "flask"})");
	ok(*l, R"({"do": "valve", "burner": "burner", "to": 1})");
	double highest = 0.0;
	const double at = run_until(*l, 600, [&] {
		highest = std::max(highest, item(*l, "flask").pressure_kpa);
		return !item(*l, "flask").sealed;
	});
	REQUIRE(at > 0.0);
	MESSAGE("the stopper popped after " << at << " s, at " << highest << " kPa (" << item(*l, "flask").celsius << " C)");
	CHECK(highest == doctest::Approx(pops).epsilon(0.05));
	CHECK(said(*l, "its stopper pops past"));
	CHECK(said(*l, "stopper popped out of flask"));
	CHECK(item(*l, "stopper").on < 0);
	CHECK(item(*l, "flask").pressure_kpa == doctest::Approx(101.325));
	CHECK_FALSE(l->failed());
}

TEST_CASE("pressure: a bottle's screw cap seals it, a control like any other; heated with water, it holds until the glass bursts, its contents into the tray, the bottle broken (Stage 28)") {
	auto l = bench();
	std::string why;
	CHECK_FALSE(l->command(R"({"do": "cap", "item": "beaker", "to": 0})", why)); // no screw cap fits a beaker
	CHECK_FALSE(l->command(R"({"do": "attach", "item": "stopper", "to": "beaker"})", why)); // nor a stopper its mouth
	ok(*l, R"({"do": "cap", "item": "bottle", "to": 0})");
	CHECK(item(*l, "bottle").sealed);
	CHECK(item(*l, "bottle").screwed);
	CHECK(item(*l, "bottle").gives_way_kpa == doctest::Approx(401.325));
	ok(*l, R"({"do": "heat", "burner": "burner", "vessel": "bottle"})");
	ok(*l, R"({"do": "valve", "burner": "burner", "to": 1})");
	double highest = 0.0, hottest = 0.0;
	const double at = run_until(*l, 1800, [&] {
		const lab::ItemView b = item(*l, "bottle");
		highest = std::max(highest, b.pressure_kpa);
		hottest = std::max(hottest, b.celsius);
		return b.burst;
	});
	REQUIRE(at > 0.0);
	MESSAGE("the bottle burst after " << at << " s, at " << highest << " kPa and " << hottest << " C");
	CHECK(highest == doctest::Approx(401.325).epsilon(0.03));
	CHECK(hottest > 120.0); // its water far above 100 C, sealed
	CHECK(said(*l, "its glass bursts past"));
	CHECK(said(*l, "bottle burst at"));
	const lab::ItemView b = item(*l, "bottle");
	CHECK(b.place == "cabinet");
	CHECK(b.liquid_ml == 0.0);
	CHECK(item(*l, "tray").liquid_ml > 150.0);
	CHECK_FALSE(l->command(R"({"do": "place", "item": "bottle", "at": [10, 6]})", why));
	CHECK(why.find("broken") != std::string::npos);
	CHECK_FALSE(l->failed());
}

TEST_CASE("pressure: a flask stoppered while it boils, then cooled, falls below an atmosphere and boils on, sealed, below 100 C; the cap and the stopper go back out (Stage 28)") {
	auto l = bench();
	ok(*l, R"({"do": "heat", "burner": "burner", "vessel": "flask"})");
	ok(*l, R"({"do": "valve", "burner": "burner", "to": 1})");
	REQUIRE(run_until(*l, 900, [&] { return item(*l, "flask").celsius > 100.3; }) > 0.0);
	std::string why;
	REQUIRE(l->advance(60 * 30, why)); // boiled for half a minute: the air driven out by steam
	ok(*l, R"({"do": "unheat", "burner": "burner"})");
	ok(*l, R"({"do": "attach", "item": "stopper", "to": "flask"})");
	REQUIRE(run_until(*l, 3600, [&] { return item(*l, "flask").celsius < 80.0; }) > 0.0);
	const lab::ItemView f = item(*l, "flask");
	MESSAGE("stoppered boiling and cooled: " << f.celsius << " C at " << f.pressure_kpa << " kPa");
	CHECK(f.sealed);
	CHECK(f.pressure_kpa < 60.0); // its saturation at 80 C is 47 kPa, with what air was left
	// warmed a little, it boils below 100 C, sealed
	ok(*l, R"({"do": "heat", "burner": "burner", "vessel": "flask"})");
	ok(*l, R"({"do": "valve", "burner": "burner", "to": 0.2})");
	REQUIRE(run_until(*l, 60, [&] { return said(*l, "boils, sealed, at"); }) > 0.0);
	CHECK(item(*l, "flask").celsius < 95.0);
	ok(*l, R"({"do": "unheat", "burner": "burner"})");
	ok(*l, R"({"do": "detach", "item": "stopper"})"); // pulled out against the vacuum: the room's air rushes in
	CHECK_FALSE(item(*l, "flask").sealed);
	REQUIRE(l->advance(1, why));
	CHECK(item(*l, "flask").pressure_kpa == doctest::Approx(101.325));
	CHECK_FALSE(l->failed());
}

TEST_CASE("pressure: a timer rigs a bottle's cap on, and a save holds the stopper, the cap and the pressure, continuing digest for digest (Stage 28)") {
	auto l = bench();
	ok(*l, R"({"do": "part", "kind": "timer", "at": [0, 0]})");
	ok(*l, R"({"do": "set", "part": "timer1", "seconds": 1})");
	ok(*l, R"({"do": "rig", "from": "timer1", "item": "bottle", "control": "cap", "on": 0})");
	std::string why;
	REQUIRE(l->advance(90, why));
	CHECK(item(*l, "bottle").screwed);
	ok(*l, R"({"do": "attach", "item": "stopper", "to": "flask"})");
	ok(*l, R"({"do": "heat", "burner": "burner", "vessel": "flask"})");
	ok(*l, R"({"do": "valve", "burner": "burner", "to": 1})");
	REQUIRE(l->advance(60 * 5, why));
	const std::string saved = l->save();
	auto again = lab::Lab::load(saved, why);
	REQUIRE_MESSAGE(again != nullptr, why);
	CHECK(again->digest() == l->digest());
	REQUIRE(l->advance(60 * 20, why));
	REQUIRE(again->advance(60 * 20, why));
	CHECK(again->digest() == l->digest());
	CHECK(item(*again, "flask").pressure_kpa == doctest::Approx(item(*l, "flask").pressure_kpa));
}
