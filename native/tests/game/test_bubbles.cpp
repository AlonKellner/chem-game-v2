// Boiling's bubbles (GAME_SPEC §11, Stage 45): the floor, the bubbles' rise and condensing, held to their
// sources at the sources' conditions (docs/DATA_SOURCES.md, "Boiling's bubbles (Stage 45)").
#include "lab/bubbles.hpp"
#include "lab/colour.hpp"
#include "lab/lab.hpp"
#include "lab/optics_scene.hpp"

#include <doctest/doctest.h>

#include <cmath>
#include <string>

using namespace lab::bubbles;

namespace {

bool near(double a, double b, double rel) {
	return b != 0.0 && std::abs(a / b - 1.0) < rel;
}

// a 250 mL beaker's floor (ISO 3819: 70 mm across outside, about 66 inside), with 150 mL of water over it
Floor beaker(double heat_w, double bulk_c, double depth_m = 0.044) {
	Floor f;
	const double r = 0.033;
	f.floor_m2 = 3.14159265358979 * r * r;
	f.perimeter_m = 2.0 * 3.14159265358979 * r;
	f.depth_m = depth_m;
	f.heat_w = heat_w;
	f.bulk_c = bulk_c;
	return f;
}

} // namespace

TEST_CASE("measured: water's properties are Lienhard's rows and its surface tension IAPWS's (Stage 45)") {
	// Lienhard, A Heat Transfer Textbook 6.00, Table A.3: 100 C, 958.3 kg/m3, 0.6791 W/m K, 2.940e-7 m2/s, Pr 1.75
	const Water w = water_at(100.0);
	CHECK(w.rho == doctest::Approx(958.3));
	CHECK(w.k == doctest::Approx(0.6791));
	CHECK(w.nu == doctest::Approx(2.940e-7));
	CHECK(w.pr == doctest::Approx(1.75));
	CHECK(water_at(57.0).beta == doctest::Approx(0.5 * (0.000436 + 0.000565))); // between 47 and 67 C
	// eq. 9.2a; Lienhard's Example 9.2 takes 0.0589 N/m at 100 C, and IAPWS gives 71.97 mN/m at 25 C
	CHECK(surface_tension(373.15) == doctest::Approx(0.0589).epsilon(0.002));
	CHECK(surface_tension(298.15) == doctest::Approx(0.07197).epsilon(0.002));
}

TEST_CASE("measured: Rohsenow's flux is Lienhard's Example 9.2: 800 kW/m2 at 22 K with C_sf 0.016 (Stage 45)") {
	// water at 100 C: cp 4.22 kJ/kg K, Pr 1.75, drho 958 kg/m3, sigma 0.0589, h_fg 2257 kJ/kg, mu 0.000282 kg/m s
	Water w;
	w.cp = 4220.0;
	w.pr = 1.75;
	w.rho = 958.6;
	w.nu = 0.000282 / w.rho;
	Surface s;
	s.csf = 0.016;
	s.s = 1.0;
	CHECK(near(rohsenow(22.0, w, 0.0589, 2257e3, 0.6, s), 800e3, 0.03));
	// and the glass's (data/boiling.json, calibrated: no glass value read): copper's and platinum's 0.013
	CHECK(surface("glass").csf == doctest::Approx(0.013));
}

TEST_CASE("measured: a glass floor's cavities hold a nucleus at Lienhard's Example 9.1's superheat, about 8 K (Stage 45)") {
	// Example 9.1: at 8 K of superheat a nucleus is held in a cavity of 0.0035 mm (2 sigma / (p_sat - p))
	const Bubbles b = boil(beaker(0.001, 20.0, 1e-6)); // (a milliwatt: convection carries it)
	CHECK(b.onset_c - b.saturation_floor_c == doctest::Approx(8.0).epsilon(0.1));
	MESSAGE("onset " << b.onset_c << " C over a boiling point of " << b.saturation_floor_c << " C at the floor");
}

TEST_CASE("measured: a bubble's rise peaks near 0.15 cm and dips near 0.65 cm across in pure water (Stage 45)") {
	// Baz-Rodriguez et al. 2012: in pure water the terminal velocity's extrema lie at about 0.15 and 0.65 cm
	// (Mendelson 1967, Loth 2010), the formulation's at 0.17 and 0.67 cm; distilled water at 20 to 28 C
	const Water w = water_at(22.0);
	const double sigma = surface_tension(295.15);
	double best = 0.0, best_d = 0.0, least = 1e9, least_d = 0.0;
	for (double d = 0.5e-3; d < 12e-3; d += 0.01e-3) {
		const double u = rise(d, w, sigma);
		if (d < 4e-3 && u > best) {
			best = u;
			best_d = d;
		}
		if (d > 3e-3 && u < least) {
			least = u;
			least_d = d;
		}
	}
	MESSAGE("peak " << best << " m/s at " << best_d * 100 << " cm; trough " << least << " m/s at " << least_d * 100 << " cm");
	CHECK(best_d * 100.0 == doctest::Approx(0.17).epsilon(0.15));
	CHECK(least_d * 100.0 == doctest::Approx(0.67).epsilon(0.15));
	CHECK(best > 0.3); // about 35 cm/s, the clean-water peak
	CHECK(best < 0.4);
}

TEST_CASE("measured: a heated floor's natural convection is Raithby and Hollands', near Lloyd and Moran's 0.54 Ra^1/4 in water (Stage 45)") {
	// Lienhard eq. 8.37a: Nu = 0.560 Ra^1/4 / [1 + (0.492 / Pr)^9/16]^4/9, L* = A / P; at Pr 5 (Fujii and Imura's
	// water) its constant is 0.504, against Lloyd and Moran's 0.54 from mass transfer at Sc 2200
	const double r = 0.033, area = 3.14159265358979 * r * r, edge = 2.0 * 3.14159265358979 * r, length = area / edge;
	const double floor_c = 64.0, bulk_c = 60.0; // Pr about 3
	const Water w = water_at(62.0);
	const double ra = 9.80665 * w.beta * 4.0 * length * length * length / (w.nu * w.alpha);
	REQUIRE(ra < 1e7);
	const double expected = 0.560 * std::pow(ra, 0.25) / std::pow(1.0 + std::pow(0.492 / w.pr, 9.0 / 16.0), 4.0 / 9.0) * w.k / length;
	CHECK(near(convection(area, edge, floor_c, bulk_c), expected, 1e-9));
	CHECK(near(expected, 0.54 * std::pow(ra, 0.25) * w.k / length, 0.1));
	MESSAGE("Ra " << ra << ", h " << expected << " W/m2 K");
	CHECK(convection(area, edge, 50.0, 60.0) == 0.0); // a floor cooler than the bulk takes nothing over it
}

TEST_CASE("bubbles: heated from below, they start at the floor and die near it in a cool bulk; near the boil they reach the surface (Stage 45)") {
	// a gas burner's few hundred watts into a beaker of water
	const Bubbles cool = boil(beaker(300.0, 60.0));
	CHECK(check_B1(beaker(300.0, 60.0), cool) == "");
	REQUIRE(cool.boiling);
	CHECK(cool.diameter_m > 1e-3);
	CHECK(cool.diameter_m < 4e-3);
	CHECK_FALSE(cool.surface);
	CHECK(cool.end_m < 0.003); // 40 K under: gone within a few millimetres
	const Bubbles warm = boil(beaker(300.0, 99.0));
	CHECK(warm.end_m > 5.0 * cool.end_m); // a kelvin under: they climb far higher
	const Bubbles boiling = boil(beaker(300.0, 100.436)); // at the bulk's boiling point (the table's)
	CHECK(boiling.surface);
	CHECK(boiling.end_m == doctest::Approx(0.044));
	// they still shrink a little: the floor's boiling point is the higher by the water over it (0.12 K under 4.4 cm),
	// so even a bulk at its surface's boiling point is that much under the floor's
	CHECK(boiling.exponent < 0.5);
	MESSAGE("at 60 C they end " << cool.end_m * 1000 << " mm up; at 99 C " << warm.end_m * 1000 << " mm; at the boil, the surface; the floor at "
									  << boiling.floor_c << " C, " << boiling.per_second << " bubbles of " << boiling.diameter_m * 1000 << " mm a second");
	// a little heat: convection carries it, nothing boils
	const Bubbles gentle = boil(beaker(5.0, 60.0));
	CHECK_FALSE(gentle.boiling);
	CHECK(gentle.extinction_floor == 0.0);
	CHECK(gentle.floor_c > 60.0);
	CHECK(gentle.floor_c < gentle.onset_c);
}

TEST_CASE("bubbles: a deeper liquid holds more bubbles at once, and its floor boils hotter (Stage 45)") {
	const Bubbles shallow = boil(beaker(300.0, 100.436, 0.02)), deep = boil(beaker(300.0, 100.436, 0.08));
	REQUIRE(shallow.surface);
	REQUIRE(deep.surface);
	// what is seen up the column: its integral, the same bubbles a longer way (four times as deep: two and a half
	// times the bubbles, for the deep floor's bubbles shrink in the 0.17 K the bulk is under their boiling point)
	const auto column = [](const Bubbles &b) { return b.extinction_floor * b.end_m / (b.exponent + 1.0); };
	CHECK(column(deep) > 2.0 * column(shallow));
	CHECK(deep.saturation_floor_c > shallow.saturation_floor_c + 0.1); // 6 cm of water: 0.6 kPa, about 0.17 K
	// at the boil the shallow floor's bubbles reach its surface; under it by half a kelvin, the deep column's die
	// on the way up
	const Bubbles under = boil(beaker(300.0, 99.9, 0.08));
	CHECK_FALSE(under.surface);
}

TEST_CASE("bubbles: a reaction's gas rises to the surface as bubbles of its own (Stage 45)") {
	Floor f = beaker(0.0, 20.0);
	f.gas_m3_s = 2e-6; // 2 mL a second
	const Bubbles b = boil(f);
	CHECK_FALSE(b.boiling);
	CHECK(b.gas_extinction > 0.0);
	CHECK(b.extinction_at(0.0) == doctest::Approx(b.extinction_at(0.04)));
	CHECK(check_B1(f, b) == "");
}

TEST_CASE("invariants: B1 fires on bubbles past the surface, a negative extinction, a holdup of one or more heat than the floor took (Stage 45)") {
	const Floor f = beaker(300.0, 99.0);
	const Bubbles good = boil(f);
	REQUIRE(check_B1(f, good) == "");
	Bubbles past = good;
	past.end_m = 1.5 * f.depth_m;
	CHECK(check_B1(f, past).rfind("B1", 0) == 0);
	Bubbles negative = good;
	negative.extinction_floor = -1.0;
	CHECK(check_B1(f, negative).rfind("B1", 0) == 0);
	Bubbles full = good;
	full.holdup_floor = 1.0;
	CHECK(check_B1(f, full).rfind("B1", 0) == 0);
	Bubbles more = good;
	more.boiling_w = 2.0 * f.heat_w;
	CHECK(check_B1(f, more).rfind("B1", 0) == 0);
	// over a sweep of heats, temperatures and depths it holds
	for (const double heat : { 0.0, 20.0, 150.0, 450.0, 2000.0 }) {
		for (const double bulk : { 20.0, 60.0, 90.0, 99.5, 100.436 }) {
			for (const double depth : { 0.005, 0.03, 0.1 }) {
				const Floor g = beaker(heat, bulk, depth);
				CHECK_MESSAGE(check_B1(g, boil(g)) == "", heat << " W, " << bulk << " C, " << depth << " m");
			}
		}
	}
}

TEST_CASE("bubbles: a beaker of water on a burner first makes bubbles that die near its floor, then boils with bubbles to the surface (Stage 45)") {
	std::string why;
	const char *bench = R"({"format": "chem-bench/2", "grid": [28, 15], "room": {"celsius": 20, "air_mol": 1000}, "items": [
		{"id": "beaker", "kind": "vessel", "volume_ml": 250, "wall": "glass", "glassware": "beaker", "wall_g": 80, "loss": 0.5, "at": [10, 7],
			"contents": {"water": {"g": 150}}},
		{"id": "burner", "kind": "burner", "celsius": 1927, "conductance": 0.25}]})";
	auto lab = lab::Lab::open(bench, why);
	REQUIRE_MESSAGE(lab != nullptr, why);
	REQUIRE_MESSAGE(lab->command(R"({"do": "heat", "burner": "burner", "vessel": "beaker"})", why), why);
	REQUIRE_MESSAGE(lab->command(R"({"do": "valve", "burner": "burner", "to": 1})", why), why);
	const auto beaker = [&] {
		for (const lab::ItemView &v : lab->view().items) {
			if (v.id == "beaker") {
				return v;
			}
		}
		FAIL("no beaker");
		return lab::ItemView{};
	};
	double first_c = 0.0, below_mm = 0.0;
	for (int s = 0; s < 1200 && !beaker().bubbles_surface; ++s) {
		REQUIRE_MESSAGE(lab->command(R"({"do": "wait", "seconds": 1})", why), why);
		const lab::ItemView v = beaker();
		if (v.boiling && first_c == 0.0) {
			first_c = v.celsius;
			below_mm = v.bubbles_end_cm * 10.0;
		}
	}
	const lab::ItemView boiling = beaker();
	MESSAGE("bubbles first at " << first_c << " C, gone " << below_mm << " mm up; at the surface at " << boiling.celsius << " C, the floor at " << boiling.floor_c
								<< " C, " << boiling.bubble_mm << " mm bubbles, " << boiling.depth_cm << " cm deep");
	CHECK(first_c > 20.0);
	CHECK(first_c < 95.0);
	CHECK(below_mm < 5.0);
	REQUIRE(boiling.bubbles_surface);
	CHECK(boiling.celsius > 99.5);
	CHECK(boiling.bubbles_cm == doctest::Approx(boiling.depth_cm));
	CHECK(boiling.bubble_extinction > 0.0);
	CHECK(lab->debug("beaker").facts.size() > 1);
}

namespace {

lab::CieLab colour_at(const lab::OpticsScene &scene, int x, int y) {
	double rgb[3];
	lab::optics_truth(scene, x, y, rgb);
	return lab::to_lab(chem::optics::Xyz{ 0.4124564 * rgb[0] + 0.3575761 * rgb[1] + 0.1804375 * rgb[2], 0.2126729 * rgb[0] + 0.7151522 * rgb[1] + 0.0721750 * rgb[2],
			0.0193339 * rgb[0] + 0.1191920 * rgb[1] + 0.9503041 * rgb[2] });
}

} // namespace

TEST_CASE("measured: bubbles in water scatter forward, g 0.85 (Flatau et al. 2000); their haze is what they turn away, 1 - g of their extinction (Stage 45)") {
	CHECK(asymmetry() == doctest::Approx(0.85));
}

TEST_CASE("bubbles: on the screen they are a white haze in the liquid, from the floor up to where they end; a reaction's gas all the way up (Stage 45)") {
	lab::OpticsRoom room;
	room.w = room.h = 1000;
	room.bench = 0.7305;
	room.elsewhere = 0.02;
	room.refraction = false;
	// 100 mL of water in a 250 mL vessel drawn 90 wide (6.34 cm: 0.0704 cm a pixel), its floor at y 212.5
	lab::ItemView v;
	v.id = "beaker";
	v.kind = "vessel";
	v.volume_ml = 250.0;
	v.celsius = 95.0;
	v.liquid_ml = v.condensed_ml = 100.0;
	lab::SubstanceView water;
	water.species = "water";
	water.phase = 'l';
	water.mol = 5.5;
	water.g = 100.0;
	v.contents = { water };
	const auto scene_of = [&](const lab::ItemView &item, lab::OpticsScene &scene) {
		lab::optics_compose(scene, room, { lab::OpticsBody{ &item, 100, 100, 90, 112.5f } });
		REQUIRE(lab::check_O1(scene) == "");
	};
	lab::OpticsScene plain, boiling, fizzing;
	scene_of(v, plain);
	lab::ItemView b = v;
	b.boiling = true;
	b.bubble_extinction = 2.0 / (1.0 - asymmetry()); // a centimetre, at the floor: 2 seen, after the forward share
	b.bubbles_end_cm = 1.0;
	b.bubble_exponent = 1.0;
	scene_of(b, boiling);
	lab::ItemView g = v;
	g.gas_extinction = 1.0 / (1.0 - asymmetry());
	scene_of(g, fizzing);
	const int x = 145, low = 209, high = 180; // 0.25 cm and 2.3 cm over the floor; the liquid is 3.2 cm deep
	const lab::CieLab plain_low = colour_at(plain, x, low), plain_high = colour_at(plain, x, high);
	const lab::CieLab boil_low = colour_at(boiling, x, low), boil_high = colour_at(boiling, x, high);
	MESSAGE("L* low " << plain_low.l << " -> " << boil_low.l << "; high " << plain_high.l << " -> " << boil_high.l);
	CHECK(boil_low.l > plain_low.l + 3.0); // the bubbles turn the room's light back
	CHECK(std::fabs(boil_high.l - plain_high.l) < 0.05); // above where they end, nothing
	CHECK(std::fabs(boil_low.a) < 1.5); // white, not tinted
	CHECK(std::fabs(boil_low.b) < 1.5);
	CHECK(colour_at(fizzing, x, high).l > plain_high.l + 3.0); // a reaction's gas, all the way up
	// the pass's own colour there is the truth's
	CHECK(lab::check_O2(boiling, x, low) == "");
	CHECK(lab::check_O2(boiling, x, high) == "");
	CHECK(lab::check_O2(fizzing, x, high) == "");
	// what the scene gives the pass: the extinction times 1 - g, quantised in steps of 3 %
	const float floor_haze = boiling.table[static_cast<size_t>(lab::optics_layout::HEADER_VECS + 6) * 4];
	CHECK(std::fabs(floor_haze / (b.bubble_extinction * (1.0 - asymmetry())) - 1.0) < 0.02);
	// a corrupted profile is caught
	lab::OpticsScene broken = boiling;
	broken.table[static_cast<size_t>(lab::optics_layout::HEADER_VECS + 6) * 4] = -1.0f;
	CHECK(lab::check_O1(broken).rfind("O1", 0) == 0);
}
