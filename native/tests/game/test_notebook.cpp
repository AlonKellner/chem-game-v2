// The notebook (GAME_SPEC §13, Stage 54): what is read of a quantity (by instruments an instrument's reading, or
// nothing), a fitted line, an axis's transform, a report's expression; recorded and logged by the lab, saved and not
// digested; an unknown drawn from its range.
#include "lab/notebook.hpp"
#include "lab/lab.hpp"
#include "lab/tutorial.hpp"

#include <doctest/doctest.h>

#include <cmath>
#include <string>

namespace {

std::string bench(const std::string &extra = "", const std::string &acid = "0.025") {
	return R"({"format": "chem-bench/2", "grid": [28, 15], "room": {"celsius": 20, "air_mol": 1000},)" + extra + R"(
		"items": [
			{"id": "beaker", "kind": "vessel", "glassware": "beaker", "volume_ml": 250, "wall": "glass", "wall_g": 80, "at": [8, 7],
				"contents": {"water": {"g": 100}, "sulfuric_acid_aq": {"mol": )" + acid + R"(}}},
			{"id": "flame", "kind": "burner", "celsius": 1927, "conductance": 0.25}
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

} // namespace

TEST_CASE("notebook: a fitted line, an axis's transform and a report's expression (Stage 54)") {
	using namespace lab;
	const LineFit exact = fit_line({ 1, 2, 3, 4 }, { 3, 5, 7, 9 });
	REQUIRE(exact.ok);
	CHECK(exact.slope == doctest::Approx(2.0));
	CHECK(exact.intercept == doctest::Approx(1.0));
	CHECK(exact.r2 == doctest::Approx(1.0));
	const LineFit rough = fit_line({ 1, 2, 3, 4 }, { 3.1, 4.9, 7.2, 8.8 });
	CHECK(rough.r2 < 1.0);
	CHECK(rough.r2 > 0.99);
	CHECK_FALSE(fit_line({ 1 }, { 2 }).ok);
	CHECK_FALSE(fit_line({ 2, 2 }, { 1, 3 }).ok);
	CHECK(*transformed(std::exp(2.0), AxisTransform::Log, false) == doctest::Approx(2.0));
	CHECK_FALSE(transformed(-1.0, AxisTransform::Log, false));
	CHECK(*transformed(26.85, AxisTransform::Inverse, true) == doctest::Approx(1.0 / 300.0)); // a temperature in 1/K
	CHECK(*transformed(4.0, AxisTransform::Inverse, false) == doctest::Approx(0.25));
	// the expression: precedence, parentheses, unary minus, quantities
	auto l = open(bench());
	const LabView v = l->view();
	double x = 0.0;
	std::string why;
	REQUIRE(evaluate("1 + 2 * 3", v, x, why));
	CHECK(x == 7.0);
	REQUIRE(evaluate("(1 + 2) * -3 / 2", v, x, why));
	CHECK(x == -4.5);
	REQUIRE_MESSAGE(evaluate("beaker.sulfuric_acid_aq.mol / (beaker.water / 1000)", v, x, why), why);
	CHECK(x == doctest::Approx(0.25).epsilon(1e-6)); // mol per kg of its water
	REQUIRE(evaluate("beaker.celsius + 273.15", v, x, why));
	CHECK(x == doctest::Approx(293.15).epsilon(1e-6));
	CHECK_FALSE(evaluate("kettle.celsius", v, x, why));
	CHECK(why.find("kettle") != std::string::npos);
	CHECK_FALSE(evaluate("1 / 0", v, x, why));
	CHECK_FALSE(evaluate("(1 + 2", v, x, why));
	CHECK_FALSE(evaluate("2 3", v, x, why));
	// a tutorial's report: an expression naming what the bench has not is refused as it opens; a zero divided by now
	// is the end's
	const auto tutorial = [&](const std::string &truth) {
		return Tutorial::open(R"({"format": "chem-tutorial/1", "steps": [{"title": "t", "text": "t", "checks": [{"text": "c", "when": {"item": "beaker", "place": "bench"}}]}],
			"report": [{"label": "r", "truth": ")" + truth + R"(", "tolerance": 1}]})",
			v, why);
	};
	REQUIRE_MESSAGE(tutorial("beaker.sulfuric_acid_aq.mol * 2") != nullptr, why);
	CHECK(tutorial("beaker.water / (beaker.sulfuric_acid_aq.mol - 0.025)") != nullptr);
	CHECK(tutorial("kettle.celsius") == nullptr);
	CHECK(why.find("kettle") != std::string::npos);
}

TEST_CASE("notebook: the lab records and logs readings, by instruments what an instrument reads, saved and not digested (Stage 54)") {
	auto truth = open(bench());
	must(*truth, R"({"do": "record", "item": "beaker", "what": "celsius", "label": "start"})");
	must(*truth, R"({"do": "record", "item": "beaker", "what": "sulfuric_acid_aq", "measure": "mol"})");
	lab::LabView v = truth->view();
	REQUIRE(v.notebook.size() == 2);
	CHECK(v.notebook[0].label == "start");
	CHECK(v.notebook[0].value == doctest::Approx(20.0).epsilon(1e-6));
	CHECK_FALSE(v.notebook[0].instrument);
	CHECK(v.notebook[1].value == doctest::Approx(0.025).epsilon(1e-9));
	// by instruments: the thermometer's reading, a species' amount refused
	auto measured = open(bench(R"( "readings": "instruments", "seed": 5,)"));
	const std::string before = measured->digest();
	must(*measured, R"({"do": "record", "item": "beaker", "what": "celsius"})");
	std::string why;
	CHECK_FALSE(measured->command(R"({"do": "record", "item": "beaker", "what": "sulfuric_acid_aq"})", why));
	CHECK(why.find("no instrument") != std::string::npos);
	v = measured->view();
	REQUIRE(v.notebook.size() == 1);
	CHECK(v.notebook[0].instrument);
	for (const lab::ItemView &i : v.items) {
		if (i.id == "beaker") {
			CHECK(v.notebook[0].value == *i.read_c);
		}
	}
	CHECK(measured->digest() == before); // the notebook is no part of the world
	// a logger: every 2 s, heating
	must(*measured, R"({"do": "heat", "burner": "flame", "vessel": "beaker"})");
	must(*measured, R"({"do": "valve", "burner": "flame", "to": 1})");
	must(*measured, R"({"do": "log", "item": "beaker", "what": "celsius", "every": 2, "label": "T"})");
	must(*measured, R"({"do": "wait", "seconds": 10.01})");
	v = measured->view();
	int logged = 0;
	double last = -1e9;
	for (const lab::NoteEntry &e : v.notebook) {
		if (e.label == "T") {
			++logged;
			CHECK(e.value >= last); // warming
			last = e.value;
		}
	}
	CHECK(logged == 6); // at 0, 2, 4, 6, 8 and 10 s
	CHECK_FALSE(measured->command(R"({"do": "log", "item": "beaker", "what": "celsius", "every": 0.5})", why));
	// saved and loaded: the same pages, the same logger, the same digest
	auto loaded = lab::Lab::load(measured->save(), why);
	REQUIRE_MESSAGE(loaded != nullptr, why);
	CHECK(loaded->view().notebook == v.notebook);
	CHECK(loaded->view().loggers == v.loggers);
	CHECK(loaded->digest() == measured->digest());
	must(*measured, R"({"do": "unlog", "item": "beaker", "what": "celsius"})");
	CHECK(measured->view().loggers.empty());
	must(*measured, R"({"do": "erase", "entry": 0})");
	CHECK(measured->view().notebook.size() == v.notebook.size() - 1);
	must(*measured, R"({"do": "erase"})");
	CHECK(measured->view().notebook.empty());
}

TEST_CASE("notebook: an unknown amount is drawn once from its range by the seed (Stage 54)") {
	const auto acid = [](uint64_t seed) {
		auto l = open(bench(R"( "seed": )" + std::to_string(seed) + ",", "[0.02, 0.03]"));
		for (const lab::ItemView &i : l->view().items) {
			for (const lab::SubstanceView &s : i.contents) {
				if (s.species == "sulfuric_acid_aq") {
					return s.mol;
				}
			}
		}
		return 0.0;
	};
	const double a = acid(1), b = acid(2);
	CHECK(a >= 0.02 - 1e-12);
	CHECK(a <= 0.03 + 1e-12);
	CHECK(a != b);
	CHECK(acid(1) == a); // the same seed, the same unknown
}
