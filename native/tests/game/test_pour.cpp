// Pouring as a control (GAME_SPEC §13, Stage 27): a vessel in a pouring position over another pours down a
// drain with a rate: held, by its tilt; upside down, through its cap, an orifice under its head of liquid.
#include "chem/quantity.hpp"
#include "chem/sim.hpp"
#include "lab/contents.hpp"
#include "lab/glassware.hpp"
#include "lab/lab.hpp"

#include "sparse.hpp"

#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
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

// the tilt (a share of the full) at which a vessel's lip comes down to its liquid, by what it holds below its lip
double tilt_to_lip(const lab::glass::Vessel &g, double cm3) {
	const double full = lab::contents::full_tilt(g, 1);
	double lo = 0.0, hi = 1.0;
	for (int k = 0; k < 50; ++k) {
		const double mid = 0.5 * (lo + hi);
		(lab::contents::held_at(g, mid * full, 1) > cm3 ? lo : hi) = mid;
	}
	return hi;
}

std::string pour_at(const std::string &from, double tilt) {
	char text[160];
	std::snprintf(text, sizeof text, R"({"do": "pour", "from": "%s", "rate": %.17g})", from.c_str(), tilt);
	return text;
}

// a bench of one beaker of water with black copper oxide in it, over another
std::unique_ptr<lab::Lab> muddy() {
	std::string why;
	auto l = lab::Lab::open(R"({"format": "chem-bench/2", "room": {"celsius": 20}, "items": [
		{"id": "muddy", "at": [4, 8], "kind": "vessel", "volume_ml": 250, "wall": "glass", "glassware": "beaker", "wall_g": 80,
			"contents": {"water": {"g": 120}, "copper_oxide": {"g": 12, "particles": 1000000000}}},
		{"id": "beaker", "at": [10, 8], "kind": "vessel", "volume_ml": 250, "wall": "glass", "glassware": "beaker", "wall_g": 80}]})",
			why);
	REQUIRE_MESSAGE(l != nullptr, why);
	return l;
}

double grams(lab::Lab &l, const std::string &id, const std::string &species) {
	const lab::LabView v = l.view();
	for (const lab::SubstanceView &c : item(v, id).contents) {
		if (c.species == species) {
			return c.g;
		}
	}
	return 0.0;
}

} // namespace

TEST_CASE("pour: a round mouth is a weir under a small head and an orifice under a deep one: Torricelli's speed over the wet part of its opening (Stage 31)") {
	namespace C = lab::contents;
	// Kirchhoff's contraction of a jet from a slot; what weirs and orifices are measured to discharge (docs/DATA_SOURCES.md)
	CHECK(C::WEIR_CD == doctest::Approx(M_PI / (M_PI + 2.0)).epsilon(1e-12));
	CHECK(C::WEIR_CD > 0.60);
	CHECK(C::WEIR_CD < 0.62);
	const double speed = std::sqrt(2.0 * C::GRAVITY);
	for (const double brim : { 1.5, 3.3 }) {
		for (const double turn : { 0.3, 0.9, 1.5, 2.2, 2.9 }) {
			CAPTURE(brim);
			CAPTURE(turn);
			// a small head wets a sliver of the mouth at its lip, a parabola's segment: the closed forms. Short of
			// lying on its side, the sharp-crested weir's; past it, the same through the opening itself.
			for (const double head : { 0.01, 0.03 }) {
				const double weir = C::WEIR_CD * speed * (M_PI / 4.0) * std::sqrt(2.0 * brim / std::sin(turn)) * head * head;
				CHECK(C::over_brim(brim, turn, head) == doctest::Approx(turn < M_PI / 2.0 ? weir : weir / std::sin(turn)).epsilon(0.01));
			}
			// more head, more flow; none, none
			CHECK(C::over_brim(brim, turn, 0.0) == 0.0);
			CHECK(C::over_brim(brim, turn, -0.2) == 0.0);
			double last = 0.0;
			for (double head = 0.1; head < 3.0 * brim; head += 0.1) {
				const double q = C::over_brim(brim, turn, head);
				CHECK(q > last);
				last = q;
			}
			// far under the surface the whole mouth is an orifice under the head at its middle
			const double deep = 200.0 * brim;
			const double orifice = C::WEIR_CD * M_PI * brim * brim * (turn < M_PI / 2.0 ? std::sin(turn) : 1.0) * speed * std::sqrt(deep - brim * std::sin(turn));
			CHECK(C::over_brim(brim, turn, deep) == doctest::Approx(orifice).epsilon(1e-4));
		}
		// on its side: the vertical mouth, a weir and an orifice alike (the two readings meet there)
		CHECK(C::over_brim(brim, M_PI / 2.0 - 1e-6, 0.4) == doctest::Approx(C::over_brim(brim, M_PI / 2.0 + 1e-6, 0.4)).epsilon(1e-5));
		// upside down: the whole mouth under one head, Torricelli's orifice
		CHECK(C::over_brim(brim, M_PI, 0.5) == doctest::Approx(C::WEIR_CD * M_PI * brim * brim * speed * std::sqrt(0.5)).epsilon(1e-6));
	}
	// the vessels' own rates (the script's pour, data/glassware.json: 25, 12 and 15 mL/s) are what their brims
	// pass under a head of some millimetres, well tilted: the two laws are of one size
	for (const char *kind : { "beaker", "conical_flask", "bottle" }) {
		const lab::glass::Vessel *g = lab::glass::find(kind, kind == std::string("bottle") ? 500.0 : 250.0);
		REQUIRE(g != nullptr);
		const double brim = g->mouth(); // (its inside's radius at the mouth)
		double lo = 0.0, hi = 3.0;
		for (int k = 0; k < 50; ++k) {
			const double mid = 0.5 * (lo + hi);
			(C::over_brim(brim, 1.0, mid) < g->pour_ml_s ? lo : hi) = mid;
		}
		MESSAGE(std::string(kind) << ": its " << g->pour_ml_s << " mL/s runs over its brim (" << 2.0 * brim << " cm across) under a head of " << 10.0 * hi << " mm");
		CHECK(hi > 0.3);
		CHECK(hi < 0.9);
	}
}

TEST_CASE("pour: by hand, nothing runs until the vessel is tilted so far that its liquid stands over its lip; then it runs as over a weir, down to the lip and never under it, and stops when let go (Stage 31)") {
	auto l = demo();
	ok(*l, R"({"do": "hold", "item": "acid", "over": "flask"})");
	CHECK(item(l->view(), "acid").pours_into >= 0);
	std::string why;
	REQUIRE(l->advance(60, why));
	CHECK(liquid(*l, "flask") == 0.0); // held, not tilted: nothing runs
	const lab::glass::Vessel *bottle = lab::glass::find("bottle", 500.0);
	REQUIRE(bottle != nullptr);
	const double full = lab::contents::full_tilt(*bottle, 1);
	const double held = item(l->view(), "acid").condensed_ml, to_lip = tilt_to_lip(*bottle, held);
	MESSAGE("the bottle holds " << held << " mL: its lip comes down to the acid at " << to_lip * full * 180.0 / M_PI << " degrees of its " << full * 180.0 / M_PI);
	REQUIRE(to_lip > 0.2);
	REQUIRE(to_lip < 0.95);
	// short of it: tilted, and nothing runs
	ok(*l, pour_at("acid", to_lip - 0.03));
	REQUIRE(l->advance(120, why));
	CHECK(liquid(*l, "flask") == 0.0);
	CHECK(item(l->view(), "acid").head_cm <= 0.0);
	CHECK(item(l->view(), "acid").turn == doctest::Approx((to_lip - 0.03) * full).epsilon(1e-12));
	// past it: the first step runs what the weir passes under the head the acid has over the lip
	const double tilt = to_lip + 0.06;
	ok(*l, pour_at("acid", tilt));
	const lab::ItemView before = item(l->view(), "acid");
	REQUIRE(before.head_cm > 0.0);
	const double law = lab::contents::over_brim(bottle->mouth(), before.turn, before.head_cm);
	CHECK(before.pours_ml_s == doctest::Approx(law).epsilon(1e-9));
	REQUIRE(l->advance(1, why));
	CHECK(before.condensed_ml - item(l->view(), "acid").condensed_ml == doctest::Approx(law * chem::STEP_SECONDS).epsilon(0.01));
	// it runs down toward the lip, ever slower, and never under it: what it holds below its lip at this turn stays
	const double stays = lab::contents::held_at(*bottle, tilt * full, 1);
	double last = item(l->view(), "acid").condensed_ml;
	for (int second = 0; second < 30; ++second) {
		REQUIRE(l->advance(60, why));
		const lab::ItemView now = item(l->view(), "acid");
		CHECK(now.head_cm >= 0.0);
		CHECK(now.condensed_ml >= stays * (1.0 - 1e-6));
		CHECK(now.condensed_ml <= last);
		last = now.condensed_ml;
	}
	CHECK(last < stays + 0.05 * held); // (the last of it dribbles: over a lip the head falls as 1/t)
	CHECK(liquid(*l, "flask") > 0.8 * (held - last)); // into the flask (but for what is dissolved in it, and what evaporates)
	MESSAGE("tilted to " << tilt * full * 180.0 / M_PI << " degrees for half a minute: " << held - last << " mL ran, " << last - stays << " mL still over the lip");
	// upright again: it stops
	ok(*l, pour_at("acid", 0.0));
	const double then = liquid(*l, "acid");
	REQUIRE(l->advance(60, why));
	CHECK(liquid(*l, "acid") == doctest::Approx(then).epsilon(1e-3));
	// at its full tilt it is poured out
	ok(*l, pour_at("acid", 1.0));
	REQUIRE(l->advance(60 * 20, why));
	CHECK(item(l->view(), "acid").condensed_ml < 0.5);
	ok(*l, R"({"do": "unpour", "item": "acid"})");
	CHECK(item(l->view(), "acid").pours_into < 0);
	CHECK_FALSE(l->failed());
}

TEST_CASE("pour: a script's pour is as it was: exactly that much at the vessel's own rate, then upright again (Stage 27)") {
	auto l = demo();
	ok(*l, R"({"do": "pour", "from": "acid", "to": "beaker", "ml": 20})");
	CHECK(liquid(*l, "beaker") == doctest::Approx(20.0).epsilon(2e-3));
	CHECK(item(l->view(), "acid").pours_into < 0);
}

TEST_CASE("pour: by hand, a liquid is decanted off its sediment, which runs once its flat section stands over the lip; stirred up, the solids go with the liquid (Stage 31)") {
	const lab::glass::Vessel *beaker = lab::glass::find("beaker", 250.0);
	REQUIRE(beaker != nullptr);
	const double full = lab::contents::full_tilt(*beaker, 1);
	std::string why;
	SUBCASE("at rest: the water first, then the oxide") {
		auto l = muddy();
		ok(*l, R"({"do": "hold", "item": "muddy", "over": "beaker"})");
		const double oxide = grams(*l, "muddy", "copper_oxide");
		REQUIRE(oxide == doctest::Approx(12.0).epsilon(1e-6));
		// tilted well past where the water reaches the lip: the water runs, the oxide stays
		const double tilt = tilt_to_lip(*beaker, item(l->view(), "muddy").condensed_ml) + 0.12;
		REQUIRE(tilt < 0.9);
		ok(*l, pour_at("muddy", tilt));
		REQUIRE(l->advance(60 * 10, why));
		CHECK(liquid(*l, "beaker") > 40.0);
		CHECK(grams(*l, "beaker", "copper_oxide") == 0.0);
		// (Stage 63: to a part in ten million: pure water's own hydrogen ions take a few nanomoles of the oxide)
		CHECK(grams(*l, "muddy", "copper_oxide") == doctest::Approx(oxide).epsilon(1e-7));
		// to its full tilt: the oxide too, all of it
		ok(*l, pour_at("muddy", 1.0));
		REQUIRE(l->advance(60 * 30, why));
		CHECK(grams(*l, "beaker", "copper_oxide") == doctest::Approx(oxide).epsilon(1e-3));
		CHECK(item(l->view(), "muddy").condensed_ml < 0.5);
		CHECK_FALSE(l->failed());
	}
	SUBCASE("stirred up: its share with each drop") {
		auto l = muddy();
		ok(*l, R"({"do": "stir", "vessel": "muddy", "on": true})");
		REQUIRE(l->advance(60 * 15, why)); // a quarter of a minute: the liquid turns, and holds all the oxide up (Stage 32)
		CHECK(item(l->view(), "muddy").suspended == 1.0);
		ok(*l, R"({"do": "hold", "item": "muddy", "over": "beaker"})");
		const double oxide = grams(*l, "muddy", "copper_oxide"), water = liquid(*l, "muddy");
		ok(*l, pour_at("muddy", tilt_to_lip(*beaker, item(l->view(), "muddy").condensed_ml) + 0.2));
		REQUIRE(l->advance(60 * 5, why));
		const double ran = liquid(*l, "beaker") / water;
		CHECK(ran > 0.2);
		CHECK(grams(*l, "beaker", "copper_oxide") == doctest::Approx(ran * oxide).epsilon(0.01));
		CHECK_FALSE(l->failed());
	}
	(void)full;
}

TEST_CASE("pour: a dry powder is poured by hand: nothing until its flat section passes the lip, all of it at the full tilt (Stage 31)") {
	auto l = demo();
	std::string why;
	ok(*l, R"({"do": "hold", "item": "jar", "over": "beaker"})");
	const double powder = grams(*l, "jar", "malachite");
	REQUIRE(powder == doctest::Approx(5.0).epsilon(1e-6));
	ok(*l, pour_at("jar", 0.5));
	REQUIRE(l->advance(120, why));
	CHECK(grams(*l, "beaker", "malachite") == 0.0);
	ok(*l, pour_at("jar", 1.0));
	REQUIRE(l->advance(60 * 20, why));
	CHECK(grams(*l, "beaker", "malachite") == doctest::Approx(powder).epsilon(1e-3));
	CHECK_FALSE(l->failed());
}

TEST_CASE("pour: upside down, its cap is an orifice under the head of liquid over it: Q = open x 0.61 x a sqrt(2 g h) (Torricelli; a sharp-edged orifice's discharge coefficient) (Stage 27)") {
	auto l = demo();
	ok(*l, R"({"do": "invert", "item": "acid", "over": "flask"})");
	const lab::ItemView acid = item(l->view(), "acid"); // a copy: the view is a temporary
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
	const lab::ItemView jar = item(l->view(), "jar");
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

#include "chem/optics.hpp"
#include "lab/colour.hpp"
#include "lab/optics_scene.hpp"
#include "lab/ui.hpp"

TEST_CASE("pour: a vessel set upside down is drawn upside down over what it pours into: its glass flipped, its acid at its mouth, as the 3D truth's rays find it (Stage 27)") {
	std::string why;
	std::unique_ptr<lab::Ui> ui = lab::Ui::open(read("data/benches/lab.json"), why);
	REQUIRE_MESSAGE(ui != nullptr, why);
	for (const char *c : { R"({"do": "place", "item": "flask", "at": [10, 7]})", R"({"do": "place", "item": "acid", "at": [4, 7]})", R"({"do": "invert", "item": "acid", "over": "flask"})" }) {
		REQUIRE_MESSAGE(ui->lab().command(c, why), c << ": " << why);
	}
	ui->frame(0.0);
	float ax = 0, ay = 0, fx = 0, fy = 0;
	REQUIRE(ui->locate("item:acid", ax, ay));
	REQUIRE(ui->locate("item:flask", fx, fy));
	CHECK(ax == doctest::Approx(fx).epsilon(0.01)); // over it
	CHECK(ay < fy);
	const lab::OpticsScene &scene = ui->optics();
	namespace L = lab::optics_layout;
	int bottle = -1;
	for (int b = 0; b < static_cast<int>(scene.table[0]); ++b) {
		if (scene.table[static_cast<size_t>(L::HEADER_VECS + b * L::BODY_VECS + L::SET) * 4 + 3] > 0.5f) {
			bottle = b;
		}
	}
	REQUIRE(bottle >= 0); // flipped
	const float *rect = &scene.table[static_cast<size_t>(L::HEADER_VECS + bottle * L::BODY_VECS + L::RECT) * 4];
	const float surface = scene.table[static_cast<size_t>(L::HEADER_VECS + bottle * L::BODY_VECS + L::FORM) * 4 + 2];
	CHECK(surface > rect[1] + 0.3f * rect[3]); // its 241 mL lie toward its mouth, at the bottom
	const auto eye = [](const double a[3], const double b[3]) {
		const auto seen = [](const double c[3]) {
			return lab::shown(chem::optics::Xyz{ 0.4124564 * c[0] + 0.3575761 * c[1] + 0.1804375 * c[2], 0.2126729 * c[0] + 0.7151522 * c[1] + 0.0721750 * c[2],
					0.0193339 * c[0] + 0.1191920 * c[1] + 0.9503041 * c[2] });
		};
		return lab::eye_difference(seen(a), seen(b));
	};
	double worst = 0.0, sum = 0.0;
	int compared = 0, visible = 0;
	for (const float f : { 0.12f, 0.3f, 0.5f, 0.7f, 0.9f }) { // down the bottle: its base, now at the top; its shoulder; its acid; its neck
		const int y = static_cast<int>(rect[1] + f * rect[3]);
		for (int x = static_cast<int>(rect[0]) + 1; x < static_cast<int>(rect[0] + rect[2]); x += 3 * sparse()) { // (Stage 68: every sparse()-th)
			if (lab::optics_cover(scene, x, y) <= 0.0) {
				continue;
			}
			double shown[3], truth[3];
			lab::optics_truth(scene, x, y, shown);
			lab::optics_area_truth(scene, x, y, 6, truth);
			const double e = eye(shown, truth);
			worst = std::max(worst, e);
			sum += e;
			visible += e > lab::EYE_VISIBLE ? 1 : 0;
			++compared;
		}
	}
	MESSAGE(compared << " pixels of the bottle upside down: worst " << worst << " on the eye metric, mean " << sum / std::max(compared, 1) << ", " << visible << " visible");
	CHECK(compared > 80 / sparse()); // (Stage 68: as many as it samples)
	CHECK(sum / compared < 1.5);
	CHECK(visible <= compared / 100);
}
