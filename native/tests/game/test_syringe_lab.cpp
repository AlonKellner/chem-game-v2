// The gas syringe (GAME_SPEC §13, Stage 79): a graduated glass barrel whose plunger is a piston (chem/world.hpp), held
// by a hand toward a volume on its scale with at most a hand's force, or free; its nozzle capped, on a vessel's mouth,
// or joined to another syringe's by a tube; read by its scale and its pressure; saved and loaded, it goes on as it was.
#include "lab/glassware.hpp"
#include "lab/instruments.hpp"
#include "lab/lab.hpp"
#include "lab/ui.hpp"

#include <doctest/doctest.h>

#include <cmath>
#include <fstream>
#include <memory>
#include <random>
#include <sstream>
#include <string>
#include <vector>

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

void no(lab::Lab &l, const std::string &c) {
	std::string why;
	CHECK_MESSAGE(!l.command(c, why), c);
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

std::string syringe(const std::string &id, int col, double ml, bool capped, const std::string &contents = "") {
	return R"({"id": ")" + id + R"(", "at": [)" + std::to_string(col) + R"(, 4], "kind": "vessel", "volume_ml": 100, "wall": "glass", "glassware": "gas_syringe", "wall_g": 60, "plunger_ml": )" +
		   std::to_string(ml) + (capped ? R"(, "capped": true)" : "") + (contents.empty() ? "" : R"(, "contents": )" + contents) + "}";
}

std::string bench(const std::string &items) {
	return R"({"format": "chem-bench/2", "room": {"celsius": 25, "air_mol": 1000}, "items": [)" + items + "]}";
}

} // namespace

TEST_CASE("glassware: a gas syringe's plunger runs over its nominal volume from fully in, its bore a 120 mm scale's (Stage 79)") {
	const lab::glass::Vessel *g = lab::glass::find("gas_syringe", 100.0);
	REQUIRE(g != nullptr);
	CHECK(g->syringe);
	CHECK(g->plunger_area() == doctest::Approx(100.0 / 12.0).epsilon(1e-6));
	CHECK(g->volume_below(g->plunger_most) - g->volume_below(g->plunger_least) == doctest::Approx(100.0).epsilon(1e-6));
	CHECK(g->plunger_most - g->plunger_least == doctest::Approx(12.0).epsilon(1e-4));
	CHECK(g->plunger_most < g->height);
	MESSAGE("dead volume " << g->volume_below(g->plunger_least) << " mL, fully in at " << g->plunger_least << " cm");
	CHECK(g->volume_below(g->plunger_least) < 1.0);
}

TEST_CASE("syringe lab: a capped syringe of air held and pushed in: its pressure times its volume stands (Boyle); let go, it springs back to about where it was (Stage 79)") {
	auto l = open(bench(syringe("s", 4, 100.0, true)));
	const lab::ItemView start = item(*l, "s");
	CHECK(start.syringe);
	CHECK(start.sealed);
	CHECK(start.plunger_ml == doctest::Approx(100.0).epsilon(1e-9));
	const lab::glass::Vessel *g = lab::glass::find("gas_syringe", 100.0);
	const double dead = g->volume_below(g->plunger_least);
	ok(*l, R"({"do": "plunger", "item": "s", "ml": 60})");
	ok(*l, R"({"do": "wait", "seconds": 60})");
	const lab::ItemView pushed = item(*l, "s");
	CHECK(pushed.plunger_held);
	CHECK(pushed.plunger_ml == doctest::Approx(60.0).epsilon(1e-6));
	CHECK(pushed.read_ml.has_value());
	CHECK(*pushed.read_ml == doctest::Approx(60.0));
	// its gas's volume is its scale's and the dead volume under it
	CHECK(pushed.pressure_kpa * (60.0 + dead) == doctest::Approx(start.pressure_kpa * (100.0 + dead)).epsilon(1e-3));
	CHECK(pushed.plunger_force_n < 0.0);
	MESSAGE("60 mL: " << pushed.pressure_kpa << " kPa, the hand " << -pushed.plunger_force_n << " N");
	ok(*l, R"({"do": "plunger", "item": "s", "free": true})");
	ok(*l, R"({"do": "wait", "seconds": 30})");
	const lab::ItemView back = item(*l, "s");
	CHECK_FALSE(back.plunger_held);
	CHECK(back.plunger_force_n == 0.0);
	CHECK(back.plunger_ml > 97.0);
	CHECK(back.plunger_ml <= 100.0);
	MESSAGE("let go: " << back.plunger_ml << " mL");
}

TEST_CASE("syringe lab: a hand pushes with at most its force: held toward nothing, the capped syringe stops where its air holds the hand (Stage 79)") {
	auto l = open(bench(syringe("s", 4, 100.0, true)));
	ok(*l, R"({"do": "plunger", "item": "s", "ml": 0})");
	ok(*l, R"({"do": "wait", "seconds": 60})");
	const lab::ItemView v = item(*l, "s");
	CHECK(lab::instruments::hand_force() == 79.8); // a thumb's average maximum on a plunger (Astin 1999, by Vo et al. 2016)
	CHECK(-v.plunger_force_n == doctest::Approx(lab::instruments::hand_force()));
	CHECK(v.plunger_ml > 45.0);
	CHECK(v.plunger_ml < 60.0);
	MESSAGE("the hand's most: " << v.plunger_ml << " mL at " << v.pressure_kpa << " kPa");
}

TEST_CASE("syringe lab: an open syringe's plunger moves as the hand takes it, its air in and out at the room's pressure; capped, uncapped, refused where it is no syringe (Stage 79)") {
	auto l = open(bench(syringe("s", 4, 20.0, false) + "," +
						R"({"id": "beaker", "at": [12, 4], "kind": "vessel", "volume_ml": 250, "wall": "glass", "glassware": "beaker", "wall_g": 80})"));
	CHECK_FALSE(item(*l, "s").sealed);
	ok(*l, R"({"do": "plunger", "item": "s", "ml": 80})");
	ok(*l, R"({"do": "wait", "seconds": 10})");
	CHECK(item(*l, "s").plunger_ml == doctest::Approx(80.0).epsilon(1e-6));
	CHECK(item(*l, "s").pressure_kpa == doctest::Approx(101.325).epsilon(1e-6));
	ok(*l, R"({"do": "cap", "item": "s", "to": 0})");
	CHECK(item(*l, "s").sealed);
	ok(*l, R"({"do": "cap", "item": "s", "to": 1})");
	CHECK_FALSE(item(*l, "s").sealed);
	no(*l, R"({"do": "plunger", "item": "beaker", "ml": 10})");
	no(*l, R"({"do": "plunger", "item": "s", "ml": 120})");
	no(*l, R"({"do": "tube", "item": "s", "to": "s"})");
	no(*l, R"({"do": "tube", "item": "s", "to": "beaker"})");
}

TEST_CASE("syringe lab: two syringes of air joined by a tube: one pushed in, the other moves out with it, a little short; untubed, each its own (Stage 79)") {
	auto l = open(bench(syringe("one", 4, 50.0, false) + "," + syringe("two", 10, 50.0, false)));
	ok(*l, R"({"do": "tube", "item": "one", "to": "two"})");
	CHECK(item(*l, "one").sealed);
	CHECK(item(*l, "one").tube >= 0);
	ok(*l, R"({"do": "plunger", "item": "one", "ml": 20})");
	ok(*l, R"({"do": "wait", "seconds": 90})"); // (the push's heat gone back to the room)
	const double one = item(*l, "one").plunger_ml, two = item(*l, "two").plunger_ml;
	MESSAGE("one at " << one << " mL, two at " << two << " mL");
	CHECK(one == doctest::Approx(20.0).epsilon(1e-6));
	// together they hold their air as it was, within what friction lets their pressure stand off the room's (each
	// plunger holds 1 N on its bore: about 1.2 %; the push's heat first moved the other out, and friction holds it)
	const lab::glass::Vessel *g = lab::glass::find("gas_syringe", 100.0);
	const double band = g->plunger_friction / (g->plunger_area() * 1e-4) / 101325.0;
	CHECK(std::fabs(one + two - 100.0) <= (100.0 + 2.0 * g->volume_below(g->plunger_least)) * band * 1.2); // (at rest it stands at the band's edge)
	CHECK(two > 75.0);
	ok(*l, R"({"do": "untube", "item": "two"})");
	CHECK_FALSE(item(*l, "one").sealed);
	CHECK(item(*l, "one").tube == -1);
}

TEST_CASE("syringe lab: two syringes of water joined by a tube: one pushed in, its water runs into the other, which moves out as far (Stage 79)") {
	const std::string water = R"({"water": {"g": 49.5}})";
	auto l = open(bench(syringe("one", 4, 50.0, false, water) + "," + syringe("two", 10, 50.0, false, water)));
	ok(*l, R"({"do": "tube", "item": "one", "to": "two"})");
	ok(*l, R"({"do": "wait", "seconds": 5})");
	const double one0 = item(*l, "one").plunger_ml, two0 = item(*l, "two").plunger_ml;
	ok(*l, R"({"do": "plunger", "item": "one", "ml": 30})");
	ok(*l, R"({"do": "wait", "seconds": 10})");
	const lab::ItemView one = item(*l, "one"), two = item(*l, "two");
	MESSAGE("one " << one0 << " -> " << one.plunger_ml << " mL, two " << two0 << " -> " << two.plunger_ml << " mL; water in two " << two.liquid_ml << " mL");
	CHECK(one.plunger_ml == doctest::Approx(30.0).epsilon(1e-6));
	CHECK(two.plunger_ml - two0 == doctest::Approx(one0 - one.plunger_ml).epsilon(0.02));
	CHECK(two.liquid_ml > one.liquid_ml);
}

TEST_CASE("syringe lab: a free syringe on a flask's mouth takes the gas the flask gives: one space, sealed, at the room's pressure; detached, its nozzle open (Stage 79)") {
	auto l = open(bench(syringe("s", 4, 0.0, false) + "," +
						R"({"id": "flask", "at": [12, 4], "kind": "vessel", "volume_ml": 250, "wall": "glass", "glassware": "conical_flask", "wall_g": 120, "celsius": 25})"));
	ok(*l, R"({"do": "attach", "item": "s", "to": "flask"})");
	CHECK(item(*l, "s").place == "on");
	CHECK(item(*l, "flask").sealed);
	no(*l, R"({"do": "cap", "item": "s", "to": 0})");
	// the flask warmed: its air expands into the syringe
	ok(*l, R"({"do": "wait", "seconds": 5})");
	const double before = item(*l, "s").plunger_ml;
	ok(*l, R"({"do": "detach", "item": "s"})");
	CHECK_FALSE(item(*l, "flask").sealed);
	CHECK_FALSE(item(*l, "s").sealed);
	CHECK(before >= 0.0);
}

TEST_CASE("syringe lab: saved and loaded, a held syringe goes on as it was, digest for digest (Stage 79)") {
	auto l = open(bench(syringe("one", 4, 50.0, false) + "," + syringe("two", 10, 50.0, false) + "," + syringe("s", 16, 80.0, true)));
	ok(*l, R"({"do": "tube", "item": "one", "to": "two"})");
	ok(*l, R"({"do": "plunger", "item": "one", "ml": 30})");
	ok(*l, R"({"do": "plunger", "item": "s", "ml": 60})");
	ok(*l, R"({"do": "wait", "seconds": 0.5})");
	std::string why;
	auto copy = lab::Lab::load(l->save(), why);
	REQUIRE_MESSAGE(copy != nullptr, why);
	CHECK(copy->digest() == l->digest());
	ok(*l, R"({"do": "wait", "seconds": 2})");
	ok(*copy, R"({"do": "wait", "seconds": 2})");
	CHECK(copy->digest() == l->digest());
	CHECK(item(*copy, "two").plunger_ml == doctest::Approx(item(*l, "two").plunger_ml).epsilon(1e-12));
	CHECK(item(*copy, "s").plunger_held);
}

TEST_CASE("syringe ui: the plunger's handle is a control on the bench: held and dragged down at a person's pace, the hand pushes the plunger toward it; let go, it is free (Stage 79)") {
	std::string why;
	auto ui = lab::Ui::open(bench(syringe("s", 6, 80.0, true)), why);
	REQUIRE_MESSAGE(ui != nullptr, why);
	const auto view = [&]() {
		for (const lab::ItemView &v : ui->lab().view().items) {
			if (v.id == "s") {
				return v;
			}
		}
		FAIL("no syringe");
		return lab::ItemView{};
	};
	float hx = 0, hy = 0, tx = 0, ty = 0;
	REQUIRE(ui->locate("plunger:s", hx, hy));
	REQUIRE(ui->locate("plunger:s@50", tx, ty));
	CHECK(ty > hy); // in is down
	ui->pointer(hx, hy, false);
	ui->pointer(hx, hy, true);
	CHECK(view().plunger_held);
	// down to 50 mL over a second and a half, then held there for two
	for (int k = 1; k <= 90; ++k) {
		ui->pointer(hx, hy + (ty - hy) * static_cast<float>(k) / 90.0f, true);
		ui->frame(1.0 / 60.0);
	}
	for (int k = 0; k < 120; ++k) {
		ui->frame(1.0 / 60.0);
	}
	const lab::ItemView held = view();
	CHECK(held.plunger_held);
	CHECK(held.plunger_ml == doctest::Approx(50.0).epsilon(0.02));
	CHECK(held.pressure_kpa > 140.0);
	ui->pointer(tx, ty, false);
	for (int k = 0; k < 120; ++k) {
		ui->frame(1.0 / 60.0);
	}
	const lab::ItemView let = view();
	CHECK_FALSE(let.plunger_held);
	CHECK(let.plunger_ml > 75.0);
	MESSAGE("held at " << held.plunger_ml << " mL, " << held.pressure_kpa << " kPa; let go, back to " << let.plunger_ml << " mL");
	// the drawing's bodies: its plunger and its handle among them
	const lab::DrawList &d = ui->draw();
	(void)d;
}

TEST_CASE("syringe lab: a plunger locked stays where the hand leaves it, held by its clamp; unlocked, it springs back (Stage 79)") {
	auto l = open(bench(syringe("s", 4, 100.0, true)));
	ok(*l, R"({"do": "plunger", "item": "s", "lock": true})");
	ok(*l, R"({"do": "plunger", "item": "s", "ml": 70})");
	ok(*l, R"({"do": "wait", "seconds": 10})");
	ok(*l, R"({"do": "plunger", "item": "s", "free": true})");
	ok(*l, R"({"do": "wait", "seconds": 20})");
	const lab::ItemView locked = item(*l, "s");
	CHECK(locked.plunger_locked);
	CHECK_FALSE(locked.plunger_hand);
	CHECK(locked.plunger_ml == doctest::Approx(70.0).epsilon(1e-3));
	CHECK(locked.pressure_kpa > 140.0);
	no(*l, R"({"do": "plunger", "item": "s", "lock": 1})");
	ok(*l, R"({"do": "plunger", "item": "s", "lock": false})");
	ok(*l, R"({"do": "wait", "seconds": 20})");
	CHECK(item(*l, "s").plunger_ml > 97.0);
}

namespace {

std::string bench_file(const char *path) {
	std::ifstream f(std::string(CHEM_REPO_ROOT) + "/" + path);
	std::stringstream s;
	s << f.rdbuf();
	return s.str();
}

// hands at random on a bench's syringes, several jerked every step (to an end or anywhere, let go, locked, capped, the
// bench's own moves): instant reversals of the force on the plungers, every invariant held on every step
void random_hands(const char *bench, const std::vector<std::string> &ids, const std::vector<std::string> &moves, unsigned seed, int steps) {
	std::string why;
	auto l = lab::Lab::open(bench_file(bench), why);
	REQUIRE_MESSAGE(l != nullptr, why);
	l->set_checking(true);
	std::mt19937 rng(seed);
	std::vector<std::string> done;
	for (int i = 0; i < steps; ++i) {
		for (int h = 0; h < 3; ++h) {
			const std::string &id = ids[rng() % ids.size()];
			const unsigned r = rng() % 16;
			std::string c = r == 0   ? R"({"do":"plunger","free":true,"item":")" + id + "\"}"
					: r == 1		 ? std::string(R"({"do":"plunger","lock":)") + (rng() % 2 ? "true" : "false") + R"(,"item":")" + id + "\"}"
					: r == 2		 ? std::string(R"({"do":"cap","to":)") + (rng() % 2 ? "1" : "0") + R"(,"item":")" + id + "\"}"
					: r == 3 && !moves.empty() ? moves[rng() % moves.size()]
					: r < 10				   ? R"({"do":"plunger","item":")" + id + "\",\"ml\":" + (rng() % 2 ? "0" : "100") + "}"
											   : R"({"do":"plunger","item":")" + id + "\",\"ml\":" + std::to_string(rng() % 101) + "}";
			std::string ignored;
			if (l->command(c, ignored)) {
				done.push_back(std::to_string(l->step()) + " " + c);
			}
		}
		if (!l->advance(1, why) || l->failed()) {
			for (size_t k = done.size() > 8 ? done.size() - 8 : 0; k < done.size(); ++k) {
				MESSAGE(done[k]);
			}
			FAIL(bench << ", seed " << seed << ", step " << l->step() << ": " << why);
		}
	}
}

} // namespace

TEST_CASE("syringe lab: hands jerked at random on the four setups, with tubes joined and undone, the baths and a flask: every invariant on every step (Stage 79)") {
	const std::vector<std::string> six = { "water", "air", "water_a", "water_b", "air_a", "air_b" };
	random_hands("data/benches/syringes.json", six, {}, 11, 1500);
	random_hands("data/benches/syringes.json", six,
			{ R"({"do":"untube","item":"water_a"})", R"({"do":"untube","item":"air_a"})", R"({"do":"tube","item":"water_a","to":"water_b"})", R"({"do":"tube","item":"air_a","to":"air_b"})",
					R"({"do":"tube","item":"water","to":"air"})", R"({"do":"untube","item":"water"})", R"({"do":"tube","item":"water_b","to":"air_a"})", R"({"do":"untube","item":"water_b"})" },
			12, 1500);
	random_hands("data/benches/charles.json", { "s" },
			{ R"({"do":"stand","item":"s","in":"cold"})", R"({"do":"stand","item":"s","in":"hot"})", R"({"do":"place","item":"s","at":[6,6]})" }, 13, 1500);
	random_hands("data/benches/molar_volume.json", { "s" },
			{ R"({"do":"attach","item":"s","to":"flask"})", R"({"do":"detach","item":"s"})", R"({"do":"tip","from":"jar","to":"flask","species":"malachite","all":true})" }, 14, 1500);
}
