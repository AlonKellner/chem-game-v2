// A vessel's wall as rings (GAME_SPEC §6, Stage 53): its floor and rings up to its brim, each a cell of its glass;
// conduction along the glass (SCHOTT DURAN's 1.2 W/m K), natural convection into the liquid and the gas each touches
// (Churchill and Chu, as Lienhard gives them; air from Lienhard's Table A.6), the floor into the liquid by Stage
// 45's heated floor, the room by the vessel's calibrated loss shared by outside area; a burner heats the floor.
#include "chem/builtin_ids.hpp"
#include "chem/species.hpp"
#include "lab/glassware.hpp"
#include "lab/lab.hpp"
#include "lab/walls.hpp"

#include <doctest/doctest.h>

#include <chrono>
#include <cmath>
#include <memory>
#include <string>

namespace {

std::unique_ptr<lab::Lab> open(const std::string &bench) {
	std::string why;
	auto l = lab::Lab::open(bench, why);
	REQUIRE_MESSAGE(l != nullptr, why);
	return l;
}

void ok(lab::Lab &l, const std::string &c) {
	std::string why;
	REQUIRE_MESSAGE(l.command(c, why), c << ": " << why);
}

lab::ItemView item(lab::Lab &l, const std::string &id) {
	for (const lab::ItemView &v : l.view().items) {
		if (v.id == id) {
			return v;
		}
	}
	FAIL("no " << id);
	return lab::ItemView{};
}

// a 250 mL beaker of 150 g of water on the bench, a burner beside it
const char *const BEAKER = R"({"format": "chem-bench/2", "room": {"celsius": 20, "air_mol": 1000}, "items": [
	{"id": "beaker", "at": [12, 7], "kind": "vessel", "volume_ml": 250, "wall": "glass", "glassware": "beaker", "wall_g": 80, "loss": 0.5,
		"contents": {"water": {"g": 150}}},
	{"id": "burner", "kind": "burner", "celsius": 1927, "conductance": 0.25}]})";

} // namespace

TEST_CASE("measured: a vertical wall's natural convection is Churchill and Chu's (Lienhard eqs. 8.13a-b) and air is Lienhard's Table A.6 (Stage 53)") {
	// Lienhard and Lienhard, A Heat Transfer Textbook, 6th ed. (v6.00, 2024), §8.3: Nu = 0.68 + 0.670 Ra^(1/4)
	// [1 + (0.492/Pr)^(9/16)]^(-4/9) for Ra up to 1e9 (8.13a); {0.825 + 0.387 Ra^(1/6) / [1 + (0.492/Pr)^(9/16)]^(8/27)}^2
	// above (8.13b). Table A.6, air at 1 atm, p. 755.
	for (const double pr : { 0.71, 7.0 }) {
		const double shape = 1.0 + std::pow(0.492 / pr, 9.0 / 16.0);
		for (const double ra : { 1e3, 1e6, 1e8 }) {
			CHECK(lab::walls::nusselt(ra, pr) / (0.68 + 0.670 * std::pow(ra, 0.25) * std::pow(shape, -4.0 / 9.0)) == doctest::Approx(1.0).epsilon(1e-12));
		}
		const double root = 0.825 + 0.387 * std::pow(1e11, 1.0 / 6.0) / std::pow(shape, 8.0 / 27.0);
		CHECK(lab::walls::nusselt(1e11, pr) / (root * root) == doctest::Approx(1.0).epsilon(1e-12));
	}
	const lab::walls::Air at300 = lab::walls::air_at(300.0), at350 = lab::walls::air_at(350.0), between = lab::walls::air_at(325.0);
	CHECK(at300.k == 0.0264);
	CHECK(at300.nu == 1.575e-5);
	CHECK(at300.pr == 0.707);
	CHECK(at350.k == 0.0300);
	CHECK(between.k / (0.5 * (0.0264 + 0.0300)) == doctest::Approx(1.0).epsilon(1e-12));
}

TEST_CASE("measured: a vessel's glass conducts at SCHOTT DURAN's 1.2 W/m K (Stage 53)") {
	// SCHOTT, DURAN borosilicate glass 3.3, technical data: thermal conductivity at 90 C, 1.2 W/(m K)
	const chem::SpeciesTable &table = chem::builtin_species();
	CHECK(table.species(chem::builtin::glass).conductivity == 1.2);
}

TEST_CASE("walls: a beaker's wall is its floor and rings of about 5 mm to its brim, their glass all of its glass (Stage 53)") {
	std::string why;
	const auto beaker = lab::glass::make("beaker", 250.0, why);
	REQUIRE_MESSAGE(beaker.has_value(), why);
	const std::vector<lab::walls::Ring> rings = lab::walls::rings_of(*beaker);
	REQUIRE(rings.size() >= 5);
	CHECK(rings.front().floor);
	CHECK(rings.front().to == doctest::Approx(beaker->floor));
	CHECK(rings.back().to == doctest::Approx(beaker->height));
	double glass = 0.0;
	for (size_t k = 1; k < rings.size(); ++k) {
		CHECK(rings[k].from == doctest::Approx(rings[k - 1].to));
		CHECK(rings[k].to - rings[k].from < 0.6);
		CHECK(rings[k].to - rings[k].from > 0.4);
		CHECK(rings[k].inside_cm2 > 0.0);
		glass += rings[k].glass_cm3;
	}
	glass += rings.front().glass_cm3;
	// the glass of the whole vessel, integrated finely
	double whole = 0.0;
	const int n = 20000;
	for (int k = 0; k < n; ++k) {
		const double y = (k + 0.5) * beaker->height / n, ro = beaker->radius_outside(y), ri = beaker->radius_inside(y);
		whole += 3.14159265358979323846 * std::max(ro * ro - ri * ri, 0.0) * beaker->height / n;
	}
	CHECK(glass / whole == doctest::Approx(1.0).epsilon(1e-3));
	MESSAGE((rings.size() - 1) << " rings and a floor; " << glass << " cm3 of glass");
}

TEST_CASE("walls: a beaker of water on a flame: its floor heats first and hottest, its wet wall with the water, its dry wall cooler; let alone, all of it back to the room (Stage 53)") {
	auto l = open(BEAKER);
	l->set_checking(true);
	lab::ItemView v = item(*l, "beaker");
	REQUIRE(v.wall_c.size() >= 5);
	for (const double c : v.wall_c) {
		CHECK(c == doctest::Approx(20.0).epsilon(1e-6));
	}
	ok(*l, R"({"do": "heat", "burner": "burner", "vessel": "beaker"})");
	ok(*l, R"({"do": "valve", "burner": "burner", "to": 1})");
	std::string why;
	REQUIRE_MESSAGE(l->advance(60 * 120, why), why); // two minutes
	v = item(*l, "beaker");
	const double floor = v.wall_c.front(), top = v.wall_c.back();
	MESSAGE("two minutes on a full flame: the floor " << floor << " C, the water " << v.celsius << " C, the brim " << top << " C");
	CHECK(floor > v.celsius + 2.0); // the floor between the flame and the water
	CHECK(v.celsius > 30.0);
	CHECK(top < v.celsius); // the dry wall at the brim, over the water
	// a wet ring near the water's middle is near the water's temperature
	const double wet = v.wall_c[3];
	CHECK(std::fabs(wet - v.celsius) < std::fabs(top - v.celsius));
	ok(*l, R"({"do": "unheat", "burner": "burner"})");
	REQUIRE_MESSAGE(l->advance(60 * 3600 * 3, why), why); // three hours (a time constant of some twenty minutes)
	v = item(*l, "beaker");
	for (const double c : v.wall_c) {
		CHECK(std::fabs(c - 20.0) < 1.0);
	}
	CHECK(std::fabs(v.celsius - 20.0) < 1.0);
}

TEST_CASE("walls: a lab with rings saved and loaded goes on exactly as the one not saved (Stage 53)") {
	auto first = open(BEAKER);
	ok(*first, R"({"do": "heat", "burner": "burner", "vessel": "beaker"})");
	ok(*first, R"({"do": "valve", "burner": "burner", "to": 0.5})");
	std::string why;
	REQUIRE(first->advance(60 * 30, why));
	auto second = lab::Lab::load(first->save(), why);
	REQUIRE_MESSAGE(second != nullptr, why);
	CHECK(second->digest() == first->digest());
	REQUIRE(first->advance(60 * 30, why));
	REQUIRE(second->advance(60 * 30, why));
	CHECK(second->digest() == first->digest());
	CHECK(item(*second, "beaker").wall_c == item(*first, "beaker").wall_c);
}


TEST_CASE("measured: a powder bed conducts as Zehner and Schlünder's stagnant bed (Stage 53)") {
	// Kandula (NASA, "On the effective thermal conductivity of porous packed beds with uniform spherical particles"),
	// eqs. 9a-c and 10: k_e / k_f = 1 - sqrt(1 - eps) + 2 sqrt(1 - eps) / (1 - lambda B) [(1 - lambda) B / (1 - lambda B)^2
	// ln(1 / (lambda B)) - (B + 1) / 2 - (B - 1) / (1 - lambda B)], lambda = k_f / k_s, B = C ((1 - eps) / eps)^(10/9),
	// C = 1.25 spheres, 1.40 broken grains. At its own form for a few beds; a grain no better than its fluid, the fluid.
	for (const double eps : { 0.4, 0.5 }) {
		for (const double ratio : { 10.0, 100.0, 1000.0 }) {
			for (const double c : { 1.25, 1.40 }) {
				const double kf = 0.026, ks = kf * ratio, lambda = kf / ks, b = c * std::pow((1.0 - eps) / eps, 10.0 / 9.0), n = 1.0 - lambda * b, root = std::sqrt(1.0 - eps);
				const double expected = kf * (1.0 - root + 2.0 * root / n * ((1.0 - lambda) * b / (n * n) * std::log(1.0 / (lambda * b)) - 0.5 * (b + 1.0) - (b - 1.0) / n));
				CHECK(lab::walls::bed_conductivity(kf, ks, eps, c) / expected == doctest::Approx(1.0).epsilon(1e-12));
			}
		}
	}
	CHECK(lab::walls::bed_conductivity(0.026, 0.02, 0.5) == 0.026);
	// a bed of malachite in air is several times air's, under its grains' own
	const double bed = lab::walls::bed_conductivity(0.026, 3.59, 0.5);
	MESSAGE("a malachite bed in air: " << bed << " W/m K");
	CHECK(bed > 0.1);
	CHECK(bed < 0.5);
}

TEST_CASE("walls: a powder on a flask's floor over a flame takes its heat through its bed: malachite decomposes in a minute or two (Stage 53)") {
	// a dry flask of 2 g of malachite on a full flame: the floor's glass heats, and the bed under it
	const char *const FLASK = R"({"format": "chem-bench/2", "room": {"celsius": 20, "air_mol": 1000}, "items": [
		{"id": "flask", "at": [12, 7], "kind": "vessel", "volume_ml": 250, "wall": "glass", "glassware": "conical_flask", "wall_g": 120, "loss": 0.5,
			"contents": {"malachite": {"g": 2, "particles": 2000000}}},
		{"id": "burner", "kind": "burner", "celsius": 1927, "conductance": 0.25}]})";
	auto l = open(FLASK);
	ok(*l, R"({"do": "heat", "burner": "burner", "vessel": "flask"})");
	ok(*l, R"({"do": "valve", "burner": "burner", "to": 1})");
	ok(*l, R"({"do": "wait_until", "item": "flask", "species": "malachite", "below_g": 0.01, "timeout": 600})");
	const lab::ItemView v = item(*l, "flask");
	MESSAGE("decomposed; the floor " << v.wall_c.front() << " C, the powder " << v.celsius << " C, the brim " << v.wall_c.back() << " C");
	CHECK(v.wall_c.front() > v.wall_c.back());
}
