// Analysis (GAME_SPEC §13.2, Stage 85): an unknown drawn from a list by the bench's seed, and a report's answer that
// is no number, the choices that hold picked from a list.
#include "lab/lab.hpp"
#include "lab/notebook.hpp"
#include "lab/tutorial.hpp"
#include "lab/ui.hpp"

#include <doctest/doctest.h>

#include <map>
#include <memory>
#include <string>

namespace {


// a beaker of one of three salts, by the seed
std::string unknown_bench(int seed) {
	return R"({"format": "chem-bench/2", "room": {"celsius": 20, "air_mol": 1000}, "seed": )" + std::to_string(seed) + R"(, "items": [
		{"id": "unknown", "at": [6, 7], "kind": "vessel", "volume_ml": 50, "wall": "glass", "glassware": "beaker", "wall_g": 40, "label": "unknown A",
			"one_of": [{"water": {"g": 20}, "sodium_chloride_aq": {"mol": 0.002}},
				{"water": {"g": 20}, "sodium_sulfate_aq": {"mol": 0.002}},
				{"water": {"g": 20}, "barium_chloride_aq": {"mol": 0.002}}]}]})";
}

double of(lab::Lab &l, const std::string &expression) {
	double v = 0.0;
	std::string why;
	REQUIRE_MESSAGE(lab::evaluate(expression, l.view(), v, why), why);
	return v;
}

// which of the three the beaker holds: 0 sodium chloride, 1 sodium sulfate, 2 barium chloride
int drawn(lab::Lab &l) {
	const bool sulfate = of(l, "unknown.sulfate_ion.mol + unknown.hydrogen_sulfate_ion.mol") > 0.0, barium = of(l, "unknown.barium_ion.mol") > 0.0;
	return sulfate ? 1 : barium ? 2 : 0;
}

} // namespace

TEST_CASE("analysis: an unknown is one of a list, drawn once by the bench's seed; the same seed, the same one (Stage 85)") {
	std::map<int, int> counts;
	for (int seed = 1; seed <= 60; ++seed) {
		std::string why;
		auto l = lab::Lab::open(unknown_bench(seed), why);
		REQUIRE_MESSAGE(l != nullptr, why);
		const int k = drawn(*l);
		++counts[k];
		// the same seed again: the same salt; its amount as written
		auto again = lab::Lab::open(unknown_bench(seed), why);
		CHECK(drawn(*again) == k);
		CHECK(of(*l, "unknown.chloride_ion.mol + unknown.sulfate_ion.mol + unknown.hydrogen_sulfate_ion.mol") > 0.0019);
	}
	// each of the three drawn, about a third of the time
	CHECK(counts.size() == 3);
	for (const auto &[k, n] : counts) {
		CHECK(n >= 10);
	}
	// a list and contents beside it, or an empty list, are refused
	std::string why;
	CHECK(lab::Lab::open(R"({"format": "chem-bench/2", "items": [{"id": "u", "at": [6, 7], "kind": "vessel", "volume_ml": 50, "wall": "glass", "glassware": "beaker", "wall_g": 40,
		"contents": {"water": {"g": 20}}, "one_of": [{"water": {"g": 20}}]}]})",
			  why) == nullptr);
	CHECK(lab::Lab::open(R"({"format": "chem-bench/2", "items": [{"id": "u", "at": [6, 7], "kind": "vessel", "volume_ml": 50, "wall": "glass", "glassware": "beaker", "wall_g": 40,
		"one_of": []}]})",
			  why) == nullptr);
}

TEST_CASE("analysis: a report's answer may be the choices that hold, picked on the card; right only where the picks are those (Stage 85)") {
	const char *const TUTORIAL = R"({"format": "chem-tutorial/1",
		"steps": [{"title": "Look", "text": "Look at it.", "checks": [{"text": "on the bench", "when": {"item": "unknown", "place": "bench"}}]}],
		"report": [
			{"label": "its anion", "single": true, "choices": [
				{"label": "chloride", "truth": "unknown.chloride_ion.mol"},
				{"label": "sulfate", "truth": "unknown.sulfate_ion.mol + unknown.hydrogen_sulfate_ion.mol"}]},
			{"label": "the ions in it", "choices": [
				{"label": "Na+", "truth": "unknown.sodium_ion.mol"},
				{"label": "Ba2+", "truth": "unknown.barium_ion.mol"},
				{"label": "Cl-", "truth": "unknown.chloride_ion.mol"},
				{"label": "SO4 2-", "truth": "unknown.sulfate_ion.mol + unknown.hydrogen_sulfate_ion.mol"}]}]})";
	// seeds giving each salt
	std::map<int, int> seed_of;
	for (int seed = 1; seed <= 60 && seed_of.size() < 3; ++seed) {
		std::string why;
		auto l = lab::Lab::open(unknown_bench(seed), why);
		seed_of.emplace(drawn(*l), seed);
	}
	REQUIRE(seed_of.size() == 3);
	// the right picks for each: [anion], [ions]
	const std::map<int, std::pair<int, std::vector<int>>> right = { { 0, { 0, { 0, 2 } } }, { 1, { 1, { 0, 3 } } }, { 2, { 0, { 1, 2 } } } };
	for (const auto &[salt, seed] : seed_of) {
		std::string why;
		auto ui = lab::Ui::open(unknown_bench(seed), why);
		REQUIRE_MESSAGE(ui != nullptr, why);
		ui->set_strict(false);
		REQUIRE_MESSAGE(ui->tutor(TUTORIAL, why), why);
		for (int f = 0; f < 10 && ui->tutorial()->view().state != lab::TutorialView::Complete; ++f) {
			ui->frame(1.0 / 60.0);
		}
		REQUIRE(ui->tutorial()->view().state == lab::TutorialView::Complete);
		const auto click = [&](const std::string &what) {
			float x = 0, y = 0;
			REQUIRE_MESSAGE(ui->locate(what, x, y), what);
			ui->pointer(x, y, false);
			ui->pointer(x, y, true);
			ui->pointer(x, y, false);
		};
		const auto said = [&](const std::string &s) { return ui->draw().text.find(s) != std::string::npos; };
		// nothing chosen: nothing to check
		click("report:check");
		ui->frame(1.0 / 60.0);
		CHECK(said("nothing to check: choose"));
		// the wrong anion, one ion short: not quite, and the right ones shown
		const auto &[anion, ions] = right.at(salt);
		click("report:0:" + std::to_string(1 - anion));
		click("report:1:" + std::to_string(ions[0]));
		click("report:check");
		ui->frame(1.0 / 60.0);
		CHECK(said("not quite"));
		CHECK(!said("yours is right: "));
		// single: picking the other moves the pick; the second ion added: right, both
		click("report:0:" + std::to_string(anion));
		click("report:1:" + std::to_string(ions[1]));
		click("report:check");
		ui->frame(1.0 / 60.0);
		CHECK(!said("not quite"));
		CHECK(said("yours is right: "));
		// a pick taken back: wrong again
		click("report:1:" + std::to_string(ions[1]));
		click("report:check");
		ui->frame(1.0 / 60.0);
		CHECK(said("not quite"));
	}
	// a choice item of one choice, or one asked as a prediction, is refused as the tutorial opens
	std::string why;
	auto ui = lab::Ui::open(unknown_bench(1), why);
	CHECK(!ui->tutor(R"({"format": "chem-tutorial/1", "steps": [{"title": "t", "text": "t", "checks": [{"text": "c", "when": {"item": "unknown", "place": "bench"}}]}],
		"report": [{"label": "x", "choices": [{"label": "a", "truth": "1"}]}]})",
			why));
	CHECK(!ui->tutor(R"({"format": "chem-tutorial/1", "steps": [{"title": "t", "text": "t", "checks": [{"text": "c", "when": {"item": "unknown", "place": "bench"}}]}],
		"report": [{"label": "x", "predict": true, "choices": [{"label": "a", "truth": "1"}, {"label": "b", "truth": "0"}]}]})",
			why));
}


TEST_CASE("analysis: a filter funnel's first drops into an empty beaker keep every invariant, whatever the solution (Stage 85)") {
	// The funnel's first drip, a milligram, lands in the empty beaker's air and dries there: no fast equilibrium takes it
	// past the activity model's range (thermo.hpp, within_range), so the ions it leaves are a solution still, and the next
	// drops dilute it as any other (before: limewater's ions left with a quantum of water, ln gamma near -3.5e5, the
	// world's entropy fell as the next drop came, T2)
	const std::vector<std::string> contents = { R"("water": {"g": 250}, "calcium_hydroxide_aq": {"mol": 0.0005})", R"("water": {"g": 250}, "calcium_hydroxide_aq": {"mol": 0.002})",
		R"("water": {"g": 250}, "calcium_hydroxide": {"g": 2.0, "particles": 1000000})", R"("water": {"g": 250}, "barium_chloride_aq": {"mol": 0.005}, "sodium_hydroxide_aq": {"mol": 0.01})",
		R"("water": {"g": 250}, "copper_sulfate_aq": {"mol": 0.02})", R"("water": {"g": 250}, "silver_nitrate_aq": {"mol": 0.02})" };
	for (const char *ppm : { "0", "420" }) {
		for (const std::string &c : contents) {
			const std::string bench = R"({"format": "chem-bench/2", "room": {"celsius": 20, "air_mol": 1000, "co2_ppm": )" + std::string(ppm) + R"(}, "items": [
			{"id": "lime", "kind": "vessel", "glassware": "bottle", "volume_ml": 500, "wall": "glass", "wall_g": 300, "at": [0, 7], "contents": {)" + c + R"(}},
			{"id": "funnel", "kind": "vessel", "glassware": "filter_funnel", "volume_ml": 100, "wall": "glass", "wall_g": 40, "at": [8, 0]},
			{"id": "beaker", "kind": "vessel", "glassware": "beaker", "volume_ml": 250, "wall": "glass", "wall_g": 80, "at": [8, 7]}]})";
			std::string why;
			auto l = lab::Lab::open(bench, why);
			REQUIRE_MESSAGE(l != nullptr, why);
			REQUIRE_MESSAGE(l->command(R"({"do": "attach", "item": "funnel", "to": "beaker"})", why), why);
			bool ok = l->command(R"({"do": "pour", "from": "lime", "to": "funnel", "ml": 60})", why);
			for (int s = 0; s < 20 && ok; ++s) {
				ok = l->command(R"({"do": "wait", "seconds": 1})", why);
			}
			CHECK_MESSAGE(ok, ppm << " ppm, " << c << ": " << why);
		}
	}
}

TEST_CASE("analysis: very soluble salts boiled down stop at the activity model's range, every invariant held (Stage 85)") {
	// lithium chloride saturates past an ionic strength of 20 (about 20 mol/kg) and has no solid in the tables: boiled
	// down, its water stops leaving at the range's edge, a syrup; sodium chloride's crystals come out first, and it dries.
	// (Calcium chloride boiled down loses its temperature, T3, as it did before this stage: docs/TDD_85_ANALYSIS.md, Open)
	for (const char *salt : { "lithium_chloride_aq", "sodium_chloride_aq" }) {
		const std::string bench = R"({"format": "chem-bench/2", "room": {"celsius": 20, "air_mol": 1000}, "items": [
			{"id": "beaker", "kind": "vessel", "glassware": "beaker", "volume_ml": 50, "wall": "glass", "wall_g": 30, "at": [6, 7], "contents": {"water": {"g": 10}, ")" +
				std::string(salt) + R"(": {"mol": 0.01}}},
			{"id": "burner", "kind": "burner", "celsius": 1927, "conductance": 0.25}]})";
		std::string why;
		auto l = lab::Lab::open(bench, why);
		REQUIRE_MESSAGE(l != nullptr, why);
		REQUIRE_MESSAGE(l->command(R"({"do": "heat", "burner": "burner", "vessel": "beaker"})", why), why);
		REQUIRE_MESSAGE(l->command(R"({"do": "valve", "burner": "burner", "to": 1})", why), why);
		bool ok = true;
		for (int s = 0; s < 30 && ok; ++s) {
			ok = l->command(R"({"do": "wait", "seconds": 20})", why);
		}
		CHECK_MESSAGE(ok, salt << ": " << why);
		MESSAGE(salt << ": water left " << of(*l, "beaker.water") << " g, at " << of(*l, "beaker.celsius") << " C");
		if (std::string(salt) == "lithium_chloride_aq") {
			// 0.01 mol at an ionic strength of 20: 0.5 g of water held
			CHECK(of(*l, "beaker.water") == doctest::Approx(0.01 / 20.0 * 1000.0).epsilon(0.02));
		} else {
			CHECK(of(*l, "beaker.water") < 1e-6);
		}
	}
}
