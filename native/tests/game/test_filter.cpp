// Filtering (GAME_SPEC §13, Stage 59): a filter funnel stands on a flask's mouth; what is poured in drains through its
// paper at Darcy's rate from the paper's Herzberg time, its solids kept on the paper.
#include "lab/glassware.hpp"
#include "lab/lab.hpp"

#include <doctest/doctest.h>

#include <cmath>
#include <string>

namespace {

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

double grams(lab::Lab &l, const std::string &id, const std::string &species) {
	for (const lab::ItemView &v : l.view().items) {
		if (v.id == id) {
			for (const lab::SubstanceView &s : v.contents) {
				if (s.species == species) {
					return s.g;
				}
			}
		}
	}
	return 0.0;
}

std::string bench() {
	return R"({"format": "chem-bench/2", "grid": [28, 15], "room": {"celsius": 20, "air_mol": 1000}, "items": [
		{"id": "funnel", "kind": "vessel", "glassware": "filter_funnel", "volume_ml": 100, "wall": "glass", "wall_g": 40, "at": [3, 2]},
		{"id": "flask", "kind": "vessel", "glassware": "conical_flask", "volume_ml": 250, "wall": "glass", "wall_g": 100, "at": [10, 7]},
		{"id": "mix", "kind": "vessel", "glassware": "beaker", "volume_ml": 250, "wall": "glass", "wall_g": 80, "at": [16, 7],
			"contents": {"water": {"g": 30}, "copper_oxide": {"g": 1.0, "particles": 700000}}}]})";
}

} // namespace

TEST_CASE("filter: a funnel's paper drains as Darcy's law from its Herzberg time and keeps the solids (Stage 59)") {
	const lab::glass::Vessel *g = lab::glass::find("filter_funnel", 100);
	REQUIRE(g != nullptr);
	CHECK(g->outlet);
	CHECK(g->paper_herzberg_s == 150.0);
	CHECK(g->cone_half_angle == doctest::Approx(3.14159265 / 6.0));
	auto l = open(bench());
	must(*l, R"({"do": "attach", "item": "funnel", "to": "flask"})");
	must(*l, R"({"do": "stir", "vessel": "mix", "on": true})");
	must(*l, R"({"do": "wait", "seconds": 5})");
	must(*l, R"({"do": "pour", "from": "mix", "to": "funnel", "all": true})"); // a script's pour decants
	must(*l, R"({"do": "tip", "from": "mix", "to": "funnel", "species": "copper_oxide", "all": true})"); // the solid washed across
	const double oxide = grams(*l, "funnel", "copper_oxide") + grams(*l, "mix", "copper_oxide");
	MESSAGE("poured: " << grams(*l, "funnel", "water") << " g of water and " << grams(*l, "funnel", "copper_oxide") << " g of the oxide in the funnel, "
					 << grams(*l, "mix", "copper_oxide") << " g of it left in the beaker");
	CHECK(oxide == doctest::Approx(1.0).epsilon(1e-6));
	double t = 0.0;
	while (grams(*l, "funnel", "water") > 0.05 && t < 1800.0) {
		must(*l, R"({"do": "wait", "seconds": 5})");
		t += 5.0;
	}
	MESSAGE("drained to " << grams(*l, "funnel", "water") << " g in " << t << " s; " << grams(*l, "flask", "water") << " g of filtrate");
	CHECK(t > 20.0); // through paper: not a pour's
	CHECK(t < 180.0); // a minute or two through a medium paper
	CHECK(grams(*l, "flask", "copper_oxide") == 0.0); // the paper keeps every grain
	CHECK(grams(*l, "funnel", "copper_oxide") > 0.9);
}

TEST_CASE("measured: a filter paper passes water at its Herzberg time, at the test's own conditions (Stage 59)") {
	// Whatman grade 1 (Cytiva; Sigma-Aldrich, Avantor and Fisher's listings): 150 s for 100 mL of water at 20 C through
	// 10 cm2 of the paper under a constant 10 cm head (the Herzberg test, Cytiva's guide to laboratory filtration)
	const lab::glass::Vessel *g = lab::glass::find("filter_funnel", 100);
	REQUIRE(g != nullptr);
	const double k = 100.0 / (g->paper_herzberg_s * 10.0 * 10.0); // cm/s per cm of head: the model's permeance
	CHECK(k * 10.0 * 10.0 * 150.0 == doctest::Approx(100.0)); // 100 mL in 150 s through 10 cm2 at 10 cm
	CHECK(g->paper_height == doctest::Approx(5.5 * std::cos(3.14159265358979 / 6.0)).epsilon(1e-9)); // a 110 mm circle folded in quarters
}
