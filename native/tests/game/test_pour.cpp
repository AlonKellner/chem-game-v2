// Pouring as a control (GAME_SPEC §13, Stage 27): a vessel in a pouring position over another pours down a
// drain with a rate: held, by its tilt; upside down, through its cap, an orifice under its head of liquid.
#include "lab/glassware.hpp"
#include "lab/lab.hpp"

#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <sstream>
#include <string>

namespace {

std::string read(const std::string &relative) {
	std::ifstream in(std::string(CHEM_REPO_ROOT) + "/" + relative);
	std::ostringstream out;
	out << in.rdbuf();
	return out.str();
}

std::unique_ptr<lab::Lab> demo() {
	std::string why;
	auto l = lab::Lab::open(read("data/benches/demo.json"), why);
	REQUIRE_MESSAGE(l != nullptr, why);
	return l;
}

void ok(lab::Lab &l, const std::string &c) {
	std::string why;
	REQUIRE_MESSAGE(l.command(c, why), c << ": " << why);
}

const lab::ItemView &item(const lab::LabView &v, const std::string &id) {
	const auto at = std::find_if(v.items.begin(), v.items.end(), [&](const lab::ItemView &x) { return x.id == id; });
	REQUIRE(at != v.items.end());
	return *at;
}

double liquid(lab::Lab &l, const std::string &id) {
	return item(l.view(), id).liquid_ml;
}

} // namespace

TEST_CASE("pour: a held pour runs at its tilt's share of the vessel's rate, its solutes with it, and stops when let go (Stage 27)") {
	auto l = demo();
	ok(*l, R"({"do": "hold", "item": "acid", "over": "flask"})");
	CHECK(item(l->view(), "acid").pours_into >= 0);
	std::string why;
	REQUIRE(l->advance(60, why));
	CHECK(liquid(*l, "flask") == 0.0); // held, not tilted: nothing runs
	ok(*l, R"({"do": "pour", "from": "acid", "rate": 0.5})");
	const double before = liquid(*l, "acid");
	REQUIRE(l->advance(120, why)); // two seconds at half the bottle's 15 mL/s
	const double ran = before - liquid(*l, "acid");
	CHECK(ran == doctest::Approx(0.5 * 15.0 * 2.0).epsilon(0.02));
	CHECK(liquid(*l, "flask") > 14.0);
	ok(*l, R"({"do": "pour", "from": "acid", "rate": 0})");
	const double held = liquid(*l, "acid");
	REQUIRE(l->advance(60, why));
	CHECK(liquid(*l, "acid") == doctest::Approx(held).epsilon(1e-3)); // let go: it stops (but for what evaporates)
	ok(*l, R"({"do": "unpour", "item": "acid"})");
	CHECK(item(l->view(), "acid").pours_into < 0);
	// a script's pour: exactly that much, then upright again
	ok(*l, R"({"do": "pour", "from": "acid", "to": "beaker", "ml": 20})");
	CHECK(liquid(*l, "beaker") == doctest::Approx(20.0).epsilon(2e-3));
	CHECK(item(l->view(), "acid").pours_into < 0);
}

TEST_CASE("pour: upside down, its cap is an orifice under the head of liquid over it: Q = open x 0.61 x a sqrt(2 g h) (Torricelli; a sharp-edged orifice's discharge coefficient) (Stage 27)") {
	auto l = demo();
	ok(*l, R"({"do": "invert", "item": "acid", "over": "flask"})");
	const lab::ItemView &acid = item(l->view(), "acid");
	CHECK(acid.inverted);
	CHECK(acid.cap == 0.0);
	std::string why;
	REQUIRE(l->advance(60, why));
	CHECK(liquid(*l, "flask") == 0.0); // shut
	ok(*l, R"({"do": "cap", "item": "acid", "to": 0.5})");
	// what one second should run, by the orifice's law at the head the bottle's liquid stands over its mouth
	const lab::glass::Vessel *bottle = lab::glass::find("bottle", 500.0);
	REQUIRE(bottle != nullptr);
	const double head_m = bottle->head_inverted(item(l->view(), "acid").condensed_ml) / 100.0, bore_m = bottle->cap_bore / 100.0;
	const double expected_ml = 0.5 * 0.61 * (M_PI * bore_m * bore_m / 4.0) * std::sqrt(2.0 * 9.80665 * head_m) * 1e6;
	const double before = liquid(*l, "acid");
	REQUIRE(l->advance(60, why));
	CHECK(before - liquid(*l, "acid") == doctest::Approx(expected_ml).epsilon(0.02)); // the head falls a little in the second
	MESSAGE("upside down, the cap half open under " << head_m * 100.0 << " cm of acid: " << before - liquid(*l, "acid") << " mL in a second");
	// the cap is a control: rigged from a timer, it opens when the timer runs out; a controller follows it
	ok(*l, R"({"do": "cap", "item": "acid", "to": 0})");
	ok(*l, R"({"do": "part", "kind": "timer", "at": [0, 0]})");
	ok(*l, R"({"do": "set", "part": "timer1", "seconds": 1})");
	ok(*l, R"({"do": "rig", "from": "timer1", "item": "acid", "control": "cap", "on": 1})");
	REQUIRE(l->advance(90, why));
	CHECK(item(l->view(), "acid").cap == 1.0);
	// a vessel with no cap has none to set
	CHECK_FALSE(l->command(R"({"do": "cap", "item": "beaker", "to": 1})", why));
	CHECK(why.find("no cap") != std::string::npos);
}

TEST_CASE("pour: poured past its brim, a solution spills into the tray and leaves its vessel full to the brim (Stage 27)") {
	auto l = demo();
	ok(*l, R"({"do": "pour", "from": "acid", "to": "jar", "ml": 200})"); // the 50 mL jar
	const lab::ItemView &jar = item(l->view(), "jar");
	CHECK(jar.condensed_ml == doctest::Approx(jar.volume_ml).epsilon(1e-6));
	CHECK(item(l->view(), "tray").liquid_ml > 100.0);
	CHECK_FALSE(l->failed()); // V1: full, not under-full
}

TEST_CASE("adjacency: a balloon or a condenser seals a neck, not a beaker's wide mouth, and the refusal names the mouth (Stage 27)") {
	auto l = demo();
	std::string why;
	CHECK_FALSE(l->command(R"({"do": "attach", "item": "balloon", "to": "beaker"})", why));
	CHECK(why.find("mm mouth") != std::string::npos);
	CHECK_FALSE(l->command(R"({"do": "attach", "item": "condenser", "to": "jar"})", why));
	ok(*l, R"({"do": "attach", "item": "balloon", "to": "flask"})"); // a flask's neck
	ok(*l, R"({"do": "detach", "item": "balloon"})");
	ok(*l, R"({"do": "attach", "item": "balloon", "to": "acid"})"); // a bottle's neck
}
