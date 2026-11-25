// Volumetric glassware in the lab (GAME_SPEC §13, Stage 52): a burette or a pipette stands on a vessel's mouth and
// drains into it through its tip, as far open as its stopcock, at the standard's delivery time; a pipette draws and a
// volumetric flask is made up to its mark, its true volume within its class's tolerance; each reads by instruments
// within its error (I1).
#include "lab/glassware.hpp"
#include "lab/instruments.hpp"
#include "lab/lab.hpp"

#include <doctest/doctest.h>

#include <cmath>
#include <string>

namespace {

std::string bench(const std::string &extra = "") {
	return R"({"format": "chem-bench/2", "grid": [28, 15], "room": {"celsius": 20, "air_mol": 1000},)" + extra + R"(
		"items": [
			{"id": "burette", "kind": "vessel", "glassware": "burette", "volume_ml": 50, "wall": "glass", "wall_g": 60, "at": [1, 0]},
			{"id": "pipette", "kind": "vessel", "glassware": "pipette", "volume_ml": 25, "wall": "glass", "wall_g": 20, "at": [4, 0]},
			{"id": "flask", "kind": "vessel", "glassware": "conical_flask", "volume_ml": 250, "wall": "glass", "wall_g": 100, "at": [8, 8]},
			{"id": "volumetric", "kind": "vessel", "glassware": "volumetric_flask", "volume_ml": 100, "wall": "glass", "wall_g": 60, "at": [12, 8]},
			{"id": "cylinder", "kind": "vessel", "glassware": "measuring_cylinder", "volume_ml": 100, "wall": "glass", "wall_g": 120, "at": [16, 8]},
			{"id": "bottle", "kind": "vessel", "glassware": "bottle", "volume_ml": 500, "wall": "glass", "wall_g": 300, "at": [20, 8],
				"contents": {"water": {"g": 400}, "sodium_hydroxide_aq": {"mol": 0.04}}}
		]})";
}

std::unique_ptr<lab::Lab> open(const std::string &text) {
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

lab::ItemView item(const lab::LabView &v, const std::string &id) {
	for (const lab::ItemView &i : v.items) {
		if (i.id == id) {
			return i;
		}
	}
	FAIL("no item " << id);
	return lab::ItemView{};
}

} // namespace

TEST_CASE("lab: a burette on a flask delivers its scale through its stopcock in its standard's time (Stage 52)") {
	auto l = open(bench());
	const lab::glass::Vessel &g = *lab::glass::find("burette", 50);
	const double to_zero = g.volume_below(g.zero);
	must(*l, R"({"do": "pour", "from": "bottle", "to": "burette", "ml": )" + std::to_string(to_zero) + "}");
	CHECK(std::fabs(*item(l->view(), "burette").scale_ml) < 0.01); // at its zero line
	must(*l, R"({"do": "attach", "item": "burette", "to": "flask"})");
	CHECK(item(l->view(), "burette").place == "on");
	CHECK(item(l->view(), "flask").holds >= 0);
	// its stopcock shut: nothing runs
	must(*l, R"({"do": "wait", "seconds": 10})");
	CHECK(item(l->view(), "flask").liquid_ml == 0.0);
	// open: the 50 mL of its scale in the standard's 80 s (ISO 385/2: 60 to 100 s)
	must(*l, R"({"do": "cap", "item": "burette", "to": 1})");
	double seconds = 0.0;
	while (*item(l->view(), "burette").scale_ml < 50.0 && seconds < 200.0) {
		must(*l, R"({"do": "wait", "seconds": 0.5})");
		seconds += 0.5;
	}
	MESSAGE("the burette's scale delivered in " << seconds << " s");
	CHECK(seconds == doctest::Approx(80.0).epsilon(0.04));
	CHECK(item(l->view(), "flask").liquid_ml > 49.0);
	// half open, half as fast
	must(*l, R"({"do": "cap", "item": "burette", "to": 0})");
	const double at = *item(l->view(), "burette").scale_ml;
	must(*l, R"({"do": "cap", "item": "burette", "to": 0.5})");
	must(*l, R"({"do": "wait", "seconds": 2})");
	const double half = *item(l->view(), "burette").scale_ml - at;
	must(*l, R"({"do": "cap", "item": "burette", "to": 1})");
	must(*l, R"({"do": "wait", "seconds": 2})");
	const double full = *item(l->view(), "burette").scale_ml - at - half;
	CHECK(half / full == doctest::Approx(0.5).epsilon(0.03));
	// lifted off, it holds what is left
	must(*l, R"({"do": "cap", "item": "burette", "to": 0})");
	const double left = item(l->view(), "burette").liquid_ml;
	must(*l, R"({"do": "detach", "item": "burette"})");
	must(*l, R"({"do": "wait", "seconds": 5})");
	CHECK(item(l->view(), "burette").liquid_ml == doctest::Approx(left).epsilon(1e-6));
	CHECK(item(l->view(), "flask").holds == -1);
	// saved mid-delivery, it goes on exactly
	must(*l, R"({"do": "attach", "item": "burette", "to": "flask"})");
	must(*l, R"({"do": "cap", "item": "burette", "to": 0.7})");
	must(*l, R"({"do": "wait", "seconds": 1})");
	std::string why;
	auto copy = lab::Lab::load(l->save(), why);
	REQUIRE_MESSAGE(copy != nullptr, why);
	must(*l, R"({"do": "wait", "seconds": 3})");
	must(*copy, R"({"do": "wait", "seconds": 3})");
	CHECK(copy->digest() == l->digest());
}

TEST_CASE("lab: a pipette draws to its mark, its true volume within its class, and delivers it all (Stage 52)") {
	auto l = open(bench(R"( "seed": 9,)"));
	const lab::ItemView empty = item(l->view(), "pipette");
	REQUIRE(empty.mark_ml);
	CHECK(std::fabs(*empty.mark_ml - 25.0) <= 0.03); // class A: +-0.03 mL
	CHECK(*empty.mark_ml != 25.0); // a piece of its own
	must(*l, R"({"do": "fill", "item": "pipette", "from": "bottle"})");
	const lab::ItemView full = item(l->view(), "pipette");
	CHECK(full.condensed_ml == doctest::Approx(*full.mark_ml).epsilon(1e-6));
	CHECK(refused(*l, R"({"do": "fill", "item": "pipette", "from": "bottle"})").find("mark already") != std::string::npos);
	// standing on the flask, it delivers what it held, within its delivery time (ISO 648: 25 to 50 s; its own 35)
	must(*l, R"({"do": "attach", "item": "pipette", "to": "flask"})");
	double seconds = 0.0;
	while (item(l->view(), "pipette").condensed_ml > 1e-6 && seconds < 120.0) {
		must(*l, R"({"do": "wait", "seconds": 0.5})");
		seconds += 0.5;
	}
	MESSAGE("the pipette delivered in " << seconds << " s");
	CHECK(seconds == doctest::Approx(35.0).epsilon(0.08));
	// all its water is the flask's now, liquid or vapour (the flask's dry air takes up some 4 mg as steam), but for the
	// trace both mouths gave the room
	const auto water = [&](const std::string &id) {
		double g = 0.0;
		for (const lab::SubstanceView &x : item(l->view(), id).contents) {
			g += x.species == "water" || x.species == "steam" ? x.g : 0.0;
		}
		return g;
	};
	double held = 0.0;
	for (const lab::SubstanceView &x : full.contents) {
		held += x.species == "water" ? x.g : 0.0;
	}
	// (the emptied pipette's air holds its own steam; the liquid running in pushes 25 mL of the flask's saturated air out
	// of its mouth, about 0.4 mg of steam, and its mouth gives the room as much again by diffusion in 35 s)
	CHECK(water("flask") + water("pipette") > held - 1.5e-3);
	CHECK(water("flask") + water("pipette") < held + 1e-9);
	CHECK(item(l->view(), "flask").condensed_ml == doctest::Approx(*full.mark_ml).epsilon(5e-4));
	// refused: a beaker has no mark; a pipette standing on something cannot draw
	CHECK(refused(*l, R"({"do": "fill", "item": "flask", "from": "bottle"})").find("no mark") != std::string::npos);
	CHECK(refused(*l, R"({"do": "fill", "item": "pipette", "from": "bottle"})").find("stands on") != std::string::npos);
}

TEST_CASE("lab: a volumetric flask made up to its mark holds its own true volume; a cylinder reads within its class (Stage 52)") {
	auto l = open(bench(R"( "readings": "instruments", "seed": 4,)"));
	// a solution made up: some base poured in, then water to the mark from the bottle
	must(*l, R"({"do": "fill", "item": "volumetric", "from": "bottle"})");
	const lab::ItemView v = item(l->view(), "volumetric");
	REQUIRE(v.mark_ml);
	CHECK(std::fabs(*v.mark_ml - 100.0) <= 0.10);
	CHECK(v.condensed_ml == doctest::Approx(*v.mark_ml).epsilon(1e-7));
	// a cylinder: what it reads within its tolerance and a tenth of its division
	must(*l, R"({"do": "pour", "from": "bottle", "to": "cylinder", "ml": 63.7})");
	const lab::ItemView c = item(l->view(), "cylinder");
	REQUIRE(c.read_ml);
	CHECK(lab::instruments::check_I1({ lab::instruments::Read{ "cylinder", c.condensed_ml, *c.read_ml, c.tolerance_ml, c.graduation_ml / 10.0 } }) == "");
	CHECK(c.tolerance_ml == 0.5);
	// a burette's reading by instruments: within 0.05 mL and its 0.01 mL
	must(*l, R"({"do": "pour", "from": "bottle", "to": "burette", "ml": 40})");
	const lab::ItemView b = item(l->view(), "burette");
	REQUIRE(b.scale_ml);
	REQUIRE(b.read_ml);
	CHECK(lab::instruments::check_I1({ lab::instruments::Read{ "burette", *b.scale_ml, *b.read_ml, b.tolerance_ml, 0.01 } }) == "");
}

TEST_CASE("lab: by instruments a vessel's thermometer lags it by its time constant, and is saved (Stage 52)") {
	auto l = open(R"({"format": "chem-bench/2", "grid": [28, 15], "room": {"celsius": 20, "air_mol": 1000}, "readings": "instruments",
		"items": [{"id": "beaker", "kind": "vessel", "glassware": "beaker", "volume_ml": 250, "wall": "glass", "wall_g": 80, "at": [8, 7], "contents": {"water": {"g": 100}}},
			{"id": "flame", "kind": "burner", "celsius": 1927, "conductance": 0.25}]})");
	must(*l, R"({"do": "heat", "burner": "flame", "vessel": "beaker"})");
	must(*l, R"({"do": "valve", "burner": "flame", "to": 1})");
	must(*l, R"({"do": "wait", "seconds": 60})");
	// heating at a steady rate r, a first-order thermometer reads r tau behind: the truth tau seconds ago
	const double t0 = item(l->view(), "beaker").celsius;
	must(*l, R"({"do": "wait", "seconds": 4.3})");
	const lab::ItemView now = item(l->view(), "beaker");
	const double rate = (now.celsius - t0) / 4.3; // K/s
	CHECK(rate > 0.05);
	// (its offset within its 0.2 C, its resolution 0.1 C)
	CHECK(std::fabs(*now.read_c - (now.celsius - rate * lab::instruments::thermometer().time_constant_s)) <= 0.2 + 0.05 + 0.02);
	CHECK(*now.read_c < now.celsius - 0.5 * rate * 4.3);
	// saved and loaded, it reads on as it was
	std::string why;
	auto copy = lab::Lab::load(l->save(), why);
	REQUIRE_MESSAGE(copy != nullptr, why);
	CHECK(*item(copy->view(), "beaker").read_c == *now.read_c);
	CHECK(copy->digest() == l->digest());
}
