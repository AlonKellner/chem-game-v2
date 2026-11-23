// A stirred liquid's turning (GAME_SPEC §11, Stage 32): its kinetic energy and the vortex it makes, the stirrer's
// work in the ledger, what the turning holds up of the liquid's solids, and how they settle.
#include "chem/sim.hpp"
#include "lab/contents.hpp"
#include "lab/glassware.hpp"
#include "lab/lab.hpp"
#include "lab/optics_scene.hpp"
#include "lab/ui.hpp"

#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <memory>
#include <string>

namespace {

// the game's stirrer (native/game/src/lab.cpp): its work a second, and how long a liquid keeps turning
constexpr double STIR_WATTS = 0.01, SWIRL_SECONDS = 1.5;
constexpr double WATER_VISCOSITY = 1.0016e-3; // Pa s at 20 C (docs/DATA_SOURCES.md)
constexpr double G = 9.80665;

std::unique_ptr<lab::Lab> bench(double water_g, double oxide_g, double particles = 1000000) {
	char text[900];
	std::snprintf(text, sizeof text, R"({"format": "chem-bench/2", "room": {"celsius": 20}, "items": [
		{"id": "beaker", "at": [4, 8], "kind": "vessel", "volume_ml": 250, "wall": "glass", "glassware": "beaker", "wall_g": 80, "loss": 0.5,
			"contents": {"water": {"g": %.6f}%s}},
		{"id": "other", "at": [10, 8], "kind": "vessel", "volume_ml": 250, "wall": "glass", "glassware": "beaker", "wall_g": 80, "loss": 0.5,
			"contents": {"water": {"g": %.6f}}}]})",
			water_g, oxide_g > 0.0 ? (std::string(R"(, "copper_oxide": {"g": )") + std::to_string(oxide_g) + R"(, "particles": )" + std::to_string(static_cast<long long>(particles)) + "}").c_str() : "", water_g);
	std::string why;
	auto l = lab::Lab::open(text, why);
	REQUIRE_MESSAGE(l != nullptr, why);
	return l;
}

void ok(lab::Lab &l, const std::string &c) {
	std::string why;
	REQUIRE_MESSAGE(l.command(c, why), c << ": " << why);
}

lab::ItemView item(lab::Lab &l, const std::string &id) {
	const lab::LabView v = l.view();
	const auto at = std::find_if(v.items.begin(), v.items.end(), [&](const lab::ItemView &x) { return x.id == id; });
	REQUIRE(at != v.items.end());
	return *at;
}

} // namespace

TEST_CASE("vortex: a turning liquid's energy is its weight on a unit of surface times the volume its vortex displaces; the vortex is a cone in the proportions of the space it has (Stage 32)") {
	namespace C = lab::contents;
	for (const char *kind : { "beaker", "conical_flask", "bottle" }) {
		const lab::glass::Vessel *g = lab::glass::find(kind, kind == std::string("bottle") ? 500.0 : 250.0);
		REQUIRE(g != nullptr);
		for (const double liquid : { 60.0, 150.0 }) {
			const double mass = liquid * 1.0; // water
			const C::Swirl most = C::swirl(*g, 0.0, liquid, 0.0, mass, 0.0);
			CAPTURE(kind);
			CAPTURE(liquid);
			REQUIRE(most.most > 0.0);
			CHECK(most.share == 0.0);
			// the most it holds: its vortex down to the glass under the surface's middle, M g D / 3
			CHECK(most.most == doctest::Approx(mass * 1e-3 * G * most.depth * 1e-2 / 3.0).epsilon(1e-9));
			const C::Lying full = C::lying(*g, 0.0, liquid, 0.0, 1.0, 1);
			CHECK(full.vortex.height == doctest::Approx(full.depth).epsilon(1e-6));
			CHECK(C::check_H1(*g, full, liquid, 0.0) == "");
			CHECK(C::check_H2(full) == "");
			for (const double part : { 0.05, 0.3, 0.8 }) {
				const double energy = part * most.most;
				const C::Swirl s = C::swirl(*g, 0.0, liquid, 0.0, mass, energy);
				const C::Lying l = C::lying(*g, 0.0, liquid, 0.0, s.share, 1);
				CAPTURE(part);
				// the law: E = (M g / A) V, in SI
				const double displaced = l.vortex.volume() * 1e-6, surface = l.surface_area * 1e-4;
				CHECK(mass * 1e-3 * G / surface * displaced == doctest::Approx(energy).epsilon(1e-4));
				// its proportions are the space's: as deep in the liquid's depth as it is wide in the surface
				CHECK(l.vortex.height / l.depth == doctest::Approx(s.share).epsilon(1e-6));
				CHECK(s.share == doctest::Approx(std::cbrt(part)).epsilon(0.05));
				CHECK(C::check_H1(*g, l, liquid, 0.0) == "");
				CHECK(C::check_H2(l) == "");
			}
			// more than it can hold makes no more vortex
			CHECK(C::swirl(*g, 0.0, liquid, 0.0, mass, 5.0 * most.most).share == 1.0);
			// a heavier liquid of the same volume holds more energy, and the same energy makes a smaller vortex in it
			const C::Swirl heavy = C::swirl(*g, 0.0, liquid, 0.0, 1.8 * mass, 0.3 * most.most);
			CHECK(heavy.most == doctest::Approx(1.8 * most.most).epsilon(1e-9));
			CHECK(heavy.share < C::swirl(*g, 0.0, liquid, 0.0, mass, 0.3 * most.most).share);
		}
		// a shallow liquid cannot make a deep vortex: less liquid, less depth, less energy at the most
		CHECK(C::swirl(*g, 0.0, 30.0, 0.0, 30.0, 0.0).most < C::swirl(*g, 0.0, 150.0, 0.0, 150.0, 0.0).most);
	}
}

TEST_CASE("vortex: stirred, a liquid's turning grows as dE/dt = P - E / tau to what its stirrer keeps up, never more than it can hold, and dies down when the stirrer stops; its vortex follows (Stage 32)") {
	auto l = bench(200.0, 0.0);
	l->set_checking(true);
	std::string why;
	CHECK(item(*l, "beaker").swirl_j == 0.0);
	CHECK(item(*l, "beaker").vortex == 0.0);
	ok(*l, R"({"do": "stir", "vessel": "beaker", "on": true})");
	// after one time constant: 1 - 1/e of what the stirrer keeps up
	REQUIRE(l->advance(static_cast<int64_t>(SWIRL_SECONDS * 60.0), why));
	const double kept_up = STIR_WATTS * SWIRL_SECONDS;
	const lab::ItemView early = item(*l, "beaker");
	REQUIRE(early.swirl_most_j > kept_up); // (200 g of water holds more than this stirrer gives)
	CHECK(early.swirl_j == doctest::Approx(kept_up * (1.0 - std::exp(-1.0))).epsilon(1e-6));
	REQUIRE(l->advance(60 * 20, why));
	const lab::ItemView steady = item(*l, "beaker");
	CHECK(steady.swirl_j == doctest::Approx(kept_up).epsilon(1e-6));
	CHECK(steady.vortex == doctest::Approx(std::cbrt(kept_up / steady.swirl_most_j)).epsilon(0.05));
	CHECK(steady.vortex > 0.3);
	CHECK(steady.vortex < 1.0);
	MESSAGE("200 mL of water stirred: " << steady.swirl_j * 1000.0 << " mJ of " << steady.swirl_most_j * 1000.0 << " at most, a vortex " << steady.vortex * 100.0 << "% of the largest that fits");
	// stopped: it dies down by e in each time constant, and is gone
	ok(*l, R"({"do": "stir", "vessel": "beaker", "on": false})");
	REQUIRE(l->advance(static_cast<int64_t>(SWIRL_SECONDS * 60.0), why));
	CHECK(item(*l, "beaker").swirl_j == doctest::Approx(kept_up * std::exp(-1.0)).epsilon(1e-6));
	REQUIRE(l->advance(60 * 40, why));
	CHECK(item(*l, "beaker").swirl_j == 0.0);
	CHECK(item(*l, "beaker").vortex == 0.0);
	// a little liquid holds less than the stirrer gives: its vortex reaches the glass, and no further
	auto shallow = bench(40.0, 0.0);
	ok(*shallow, R"({"do": "stir", "vessel": "beaker", "on": true})");
	REQUIRE(shallow->advance(60 * 20, why));
	const lab::ItemView low = item(*shallow, "beaker");
	REQUIRE(low.swirl_most_j < kept_up);
	CHECK(low.swirl_j == doctest::Approx(low.swirl_most_j).epsilon(1e-9));
	CHECK(low.vortex == doctest::Approx(1.0).epsilon(1e-6));
	CHECK_FALSE(l->failed());
	CHECK_FALSE(shallow->failed());
}

TEST_CASE("vortex: the stirrer's work is in the ledger: the mains give it, and it is heat in the liquid, as much as a second of stirring is (Stage 32)") {
	// two beakers alike, one stirred: after ten minutes the stirred one is warmer by the work done on it over its
	// heat capacity (the two lose heat to the room alike, but for that difference)
	auto l = bench(200.0, 0.0);
	l->set_checking(true);
	std::string why;
	REQUIRE(l->advance(60 * 30, why)); // (the bench opens a little out of equilibrium with the room's air)
	ok(*l, R"({"do": "stir", "vessel": "beaker", "on": true})");
	const double before = item(*l, "beaker").celsius - item(*l, "other").celsius;
	const int seconds = 600;
	REQUIRE(l->advance(60 * seconds, why));
	const lab::ItemDebug d = l->debug("beaker");
	REQUIRE(d.found);
	const double warmer = item(*l, "beaker").celsius - item(*l, "other").celsius - before;
	// (its loss to the room, 0.5 W/K, takes back a part of it: over ten minutes of a time constant of half an hour)
	const double tau = d.heat_capacity / 0.5, expected = STIR_WATTS / 0.5 * (1.0 - std::exp(-seconds / tau));
	MESSAGE("ten minutes of stirring: " << warmer * 1000.0 << " mK warmer than the beaker beside it (" << STIR_WATTS * seconds << " J into " << d.heat_capacity << " J/K: " << expected * 1000.0 << " mK with what it loses to the room)");
	CHECK(warmer == doctest::Approx(expected).epsilon(0.05));
	// the debug panel shows the flow: work, from the mains
	bool shown = false;
	for (const lab::FlowDebug &f : d.flows) {
		if (f.process == "work") {
			shown = true;
			CHECK(f.with == "mains");
			CHECK(f.watts == doctest::Approx(STIR_WATTS).epsilon(1e-3));
		}
	}
	CHECK(shown);
	// an empty vessel's stirrer does no work
	auto empty = bench(200.0, 0.0);
	ok(*empty, R"({"do": "pour", "from": "beaker", "to": "other", "all": true})");
	ok(*empty, R"({"do": "stir", "vessel": "beaker", "on": true})");
	REQUIRE(empty->advance(120, why));
	for (const lab::FlowDebug &f : empty->debug("beaker").flows) {
		CHECK(f.process != "work");
	}
	CHECK_FALSE(l->failed()); // every invariant, each step: the second law among them (T1, T2)
}

TEST_CASE("vortex: its turning holds a liquid's solids up while its dissipation covers what they would give up falling (their weight in the liquid times Stokes' speed); left alone they settle at that speed, and lie as sediment again (Stage 32)") {
	// a fine powder: 12 g of copper oxide in a thousand million grains
	auto l = bench(120.0, 12.0, 1.0e9);
	l->set_checking(true);
	std::string why;
	CHECK(item(*l, "beaker").suspended == 0.0);
	ok(*l, R"({"do": "stir", "vessel": "beaker", "on": true})");
	REQUIRE(l->advance(60 * 15, why));
	const lab::ItemView up = item(*l, "beaker");
	CHECK(up.suspended == 1.0); // the turning holds it all up
	double grain_um = 0.0, oxide_ml = 0.0, oxide_g = 0.0;
	for (const lab::SubstanceView &c : up.contents) {
		if (c.species == "copper_oxide") {
			grain_um = c.size_um;
			oxide_ml = c.ml;
			oxide_g = c.g;
		}
	}
	REQUIRE(grain_um > 0.0);
	const lab::glass::Vessel *g = lab::glass::find("beaker", 250.0);
	REQUIRE(g != nullptr);
	// Stokes: v = (rho_s - rho) g d^2 / (18 mu), with water's viscosity at 20 C (docs/DATA_SOURCES.md)
	const double liquid_ml = up.condensed_ml - oxide_ml, depth_m = lab::contents::swirl(*g, 0.0, liquid_ml, 0.0, 120.0, 0.0).depth * 1e-2;
	const double heavier = (oxide_g / oxide_ml - 120.0 / liquid_ml) * 1000.0; // kg/m^3 over the water's
	const double speed = heavier * G * std::pow(grain_um * 1e-6, 2.0) / (18.0 * WATER_VISCOSITY);
	const double holding = heavier * G * oxide_ml * 1e-6 * speed; // W: what holds it all up
	REQUIRE(holding < STIR_WATTS);
	// let go: the turning dies down; once its dissipation is under what holds the powder up (E / tau = P e^(-t / tau)),
	// the powder falls at its speed through the liquid's depth
	ok(*l, R"({"do": "stir", "vessel": "beaker", "on": false})");
	const double held_for = SWIRL_SECONDS * std::log(STIR_WATTS / holding), settles_in = depth_m / speed;
	MESSAGE("oxide grains " << grain_um << " um across fall " << speed * 1000.0 << " mm/s through water: held " << held_for << " s by the dying turning, then down through " << depth_m * 100.0 << " cm in "
							<< settles_in << " s");
	REQUIRE(settles_in > 30.0);
	REQUIRE(l->advance(static_cast<int64_t>(60.0 * (held_for - 0.5)), why));
	CHECK(item(*l, "beaker").suspended == 1.0);
	int seconds = 0;
	double last = 1.0;
	while (seconds < 3600 && item(*l, "beaker").suspended > 0.0) {
		REQUIRE(l->advance(60, why));
		++seconds;
		const double now = item(*l, "beaker").suspended;
		CHECK(now <= last);
		last = now;
	}
	CHECK(seconds == doctest::Approx(settles_in).epsilon(0.05));
	// the same mass in coarse grains asks for more power than the stirrer gives: a share of it stays down
	auto coarse = bench(120.0, 12.0, 1.0e5);
	ok(*coarse, R"({"do": "stir", "vessel": "beaker", "on": true})");
	REQUIRE(coarse->advance(60 * 30, why));
	const lab::ItemView loaded = item(*coarse, "beaker");
	double coarse_um = 0.0;
	for (const lab::SubstanceView &c : loaded.contents) {
		coarse_um = c.species == "copper_oxide" ? c.size_um : coarse_um;
	}
	const double coarse_speed = heavier * G * std::pow(coarse_um * 1e-6, 2.0) / (18.0 * WATER_VISCOSITY);
	CHECK(loaded.suspended == doctest::Approx(STIR_WATTS / (heavier * G * oxide_ml * 1e-6 * coarse_speed)).epsilon(0.02));
	CHECK(loaded.suspended < 0.5);
	MESSAGE("in grains " << coarse_um << " um across the turning holds " << loaded.suspended * 100.0 << "% of it up");
	CHECK_FALSE(l->failed());
	CHECK_FALSE(coarse->failed());
}

TEST_CASE("vortex: saved in the middle of its turning and loaded, a lab goes on exactly; one that never stirred digests as one made before stirring was work (Stage 32)") {
	// coarse grains, of which the turning holds up a share; and a little liquid, whose turning is at its most:
	// what a loaded lab works out must be what the lab it was saved from had worked out
	for (const double water : { 150.0, 30.0 }) {
		auto l = bench(water, 6.0, 2.0e5);
		std::string why;
		ok(*l, R"({"do": "stir", "vessel": "beaker", "on": true})");
		REQUIRE(l->advance(77, why));
		std::unique_ptr<lab::Lab> again = lab::Lab::load(l->save(), why);
		REQUIRE_MESSAGE(again != nullptr, why);
		CHECK(again->digest() == l->digest());
		REQUIRE(l->advance(900, why));
		REQUIRE(again->advance(900, why));
		CHECK(again->digest() == l->digest());
		CHECK(item(*again, "beaker").swirl_j == item(*l, "beaker").swirl_j);
		CHECK(item(*again, "beaker").suspended == item(*l, "beaker").suspended);
		CHECK(item(*l, "beaker").suspended > 0.0);
		// let go and saved again while it settles
		ok(*l, R"({"do": "stir", "vessel": "beaker", "on": false})");
		REQUIRE(l->advance(45, why));
		again = lab::Lab::load(l->save(), why);
		REQUIRE_MESSAGE(again != nullptr, why);
		REQUIRE(l->advance(600, why));
		REQUIRE(again->advance(600, why));
		CHECK(again->digest() == l->digest());
	}
}

TEST_CASE("vortex: on the screen a stirred vessel's liquid has its vortex, a cone the pass traces, and its powder is a haze in the liquid while it turns (Stage 32)") {
	const int before = lab::optics_reading(5);
	std::string why;
	std::unique_ptr<lab::Ui> ui = lab::Ui::open(R"({"format": "chem-bench/2", "room": {"celsius": 20}, "items": [
		{"id": "beaker", "kind": "vessel", "volume_ml": 250, "wall": "glass", "glassware": "beaker", "wall_g": 80, "loss": 0.5,
			"contents": {"water": {"g": 150}, "copper_oxide": {"g": 3, "particles": 1000000000}}}]})",
			why);
	REQUIRE_MESSAGE(ui != nullptr, why);
	REQUIRE_MESSAGE(ui->lab().command(R"({"do": "place", "item": "beaker", "at": [10, 7]})", why), why);
	ui->frame(0.0);
	namespace L = lab::optics_layout;
	{
		const lab::OpticsScene &scene = ui->optics();
		const int body = lab::optics_body(scene, "beaker");
		REQUIRE(body >= 0);
		const double *lie = &scene.lies[static_cast<size_t>(body) * L::SHAPE_BLOCK * 4];
		CHECK(lie[16] == 0.0); // no vortex
		CHECK(lie[19] == 1.0); // its powder a sediment
		CHECK(scene.table[static_cast<size_t>(L::HEADER_VECS + body * L::BODY_VECS + L::MORE) * 4] == 0.0f); // no haze
	}
	REQUIRE(ui->lab().command(R"({"do": "stir", "vessel": "beaker", "on": true})", why));
	REQUIRE(ui->lab().advance(60 * 15, why));
	ui->frame(0.0);
	{
		const lab::OpticsScene &scene = ui->optics();
		const int body = lab::optics_body(scene, "beaker");
		REQUIRE(body >= 0);
		const double *lie = &scene.lies[static_cast<size_t>(body) * L::SHAPE_BLOCK * 4];
		CHECK(lie[16] > 0.5); // a vortex, more than 5 mm deep
		CHECK(lie[19] == 0.0); // nothing lies at rest
		CHECK(scene.table[static_cast<size_t>(L::HEADER_VECS + body * L::BODY_VECS + L::MORE) * 4] > 0.0f); // a haze
		CHECK(lab::check_O1(scene) == "");
		// the pass's rays through the vortex are the truth's
		float x = 0, y = 0;
		REQUIRE(ui->locate("item:beaker", x, y));
		CHECK(lab::check_O9(scene, static_cast<int>(x) - 50, static_cast<int>(y) - 60, static_cast<int>(x) + 50, static_cast<int>(y) + 60, 5) == "");
	}
	lab::optics_reading(before);
}
