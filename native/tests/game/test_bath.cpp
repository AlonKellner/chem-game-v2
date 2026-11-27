// A vessel in a vessel (GAME_SPEC §6, §13, Stage 71): a test tube stood in a beaker. What it displaces raises the
// beaker's layers and its level, a brim-full beaker spilling it; each of its rings passes heat to each of the beaker's
// cells it overlaps, by what of the ring lies there, never vessel to vessel; the beaker holds where it stands while the
// tube is in it; taken out, the beaker is its own shape again; saved and loaded, it goes on as it was.
#include "lab/glassware.hpp"
#include "lab/lab.hpp"
#include "lab/ui.hpp"

#include <doctest/doctest.h>

#include <nlohmann/json.hpp>

#include <array>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <memory>
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

lab::ItemView item(lab::Lab &l, const std::string &id) {
	for (const lab::ItemView &v : l.view().items) {
		if (v.id == id) {
			return v;
		}
	}
	FAIL("no " << id);
	return lab::ItemView{};
}

double grams(const lab::ItemView &v, const std::string &species) {
	double g = 0.0;
	for (const lab::SubstanceView &s : v.contents) {
		g += s.species == species ? s.g : 0.0;
	}
	return g;
}

// a 250 mL beaker of `water_g` of water at `beaker_c`, and a test tube of 10 g of water at `tube_c`
std::string bench(double water_g, double beaker_c = 20.0, double tube_c = 20.0) {
	return R"({"format": "chem-bench/2", "room": {"celsius": 20, "air_mol": 1000}, "items": [
		{"id": "beaker", "at": [12, 7], "kind": "vessel", "volume_ml": 250, "wall": "glass", "glassware": "beaker", "wall_g": 80, "celsius": )" +
		   std::to_string(beaker_c) + R"(, "contents": {"water": {"g": )" + std::to_string(water_g) + R"(}}},
		{"id": "tube", "at": [4, 7], "kind": "vessel", "volume_ml": 18, "wall": "glass", "glassware": "test_tube", "wall_g": 25, "celsius": )" +
		   std::to_string(tube_c) + R"(, "contents": {"water": {"g": 10}}}]})";
}

} // namespace

TEST_CASE("glassware: a test tube is ISO 4142's, its round floor its own radius (Stage 71)") {
	const lab::glass::Vessel *t = lab::glass::find("test_tube", 18.0);
	REQUIRE(t != nullptr);
	CHECK(t->diameter == doctest::Approx(1.6));
	CHECK(t->height == doctest::Approx(15.0));
	MESSAGE("capacity " << t->capacity() << " mL");
	CHECK(t->capacity() == doctest::Approx(18.0).epsilon(0.1)); // PYREX 1624/12M: 18 mL
	// its floor round: narrower near its foot than up its side, its outside a sphere's there
	CHECK(t->radius_outside(0.2) < 0.7 * t->radius_outside(2.0));
	CHECK(t->radius_outside(2.0) == doctest::Approx(0.8).epsilon(1e-3));
	// what its outside encloses: a cylinder of its diameter over its height, less what its round floor leaves of the
	// cylinder's foot (pi r^3 / 3)
	const double cylinder = 3.14159265358979 * 0.8 * 0.8 * 15.0, corners = 3.14159265358979 * 0.8 * 0.8 * 0.8 / 3.0;
	CHECK(t->outside_below(15.0) == doctest::Approx(cylinder - corners).epsilon(2e-3));
	CHECK(t->outside_below(7.5) == doctest::Approx(0.5 * t->outside_below(15.0) - 0.5 * 3.14159265358979 * 0.8 * 0.8 * 0.8 / 3.0).epsilon(0.01));
}

TEST_CASE("bath: a tube stood in a beaker raises its level by what it displaces; refused where it does not fit (Stage 71)") {
	auto l = open(bench(150.0));
	const lab::ItemView before = item(*l, "beaker");
	const lab::glass::Vessel *b = lab::glass::find("beaker", 250.0), *t = lab::glass::find("test_tube", 18.0);
	REQUIRE(b != nullptr);
	ok(*l, R"({"do": "stand", "item": "tube", "in": "beaker"})");
	const lab::ItemView after = item(*l, "beaker"), tube = item(*l, "tube");
	CHECK(tube.place == "in");
	CHECK(tube.stands_in == "beaker");
	// its level: where the beaker alone would hold its water and what the tube displaces under that level
	const double level = b->level(after.condensed_ml + after.displaced_ml);
	MESSAGE("displaced " << after.displaced_ml << " mL; the level from " << b->level(before.condensed_ml) << " to " << level << " cm");
	CHECK(after.displaced_ml == doctest::Approx(t->outside_below(level - b->floor)).epsilon(1e-6));
	CHECK(after.displaced_ml > 7.0); // 1.6 cm across, about 4.5 cm deep: 8.6 mL
	CHECK(level > b->level(before.condensed_ml) + 0.2);
	CHECK(after.condensed_ml == doctest::Approx(before.condensed_ml).epsilon(1e-9)); // the same water, higher
	CHECK(after.volume_ml < before.volume_ml - 10.0); // its room less what the tube takes
	std::string why;
	// the beaker cannot move while the tube stands in it; a beaker does not stand in a tube
	CHECK_FALSE(l->command(R"({"do": "place", "item": "beaker", "at": [20, 7]})", why));
	CHECK_FALSE(l->command(R"({"do": "stand", "item": "beaker", "in": "tube"})", why));
	// taken out: its own shape again
	ok(*l, R"({"do": "place", "item": "tube", "at": [4, 7]})");
	const lab::ItemView out = item(*l, "beaker");
	CHECK(item(*l, "tube").place == "bench");
	CHECK(out.displaced_ml == 0.0);
	CHECK(out.volume_ml == doctest::Approx(before.volume_ml).epsilon(1e-9));
}

TEST_CASE("bath: a brim-full beaker spills what a tube stood in it pushes over the brim (Stage 71)") {
	const lab::glass::Vessel *b = lab::glass::find("beaker", 250.0);
	REQUIRE(b != nullptr);
	auto l = open(bench(b->capacity() * 0.997 - 1.0)); // its brim: water at 20 C is 0.9982 g/mL
	const double before = grams(item(*l, "beaker"), "water");
	ok(*l, R"({"do": "stand", "item": "tube", "in": "beaker"})");
	ok(*l, R"({"do": "wait", "seconds": 2})");
	const lab::ItemView after = item(*l, "beaker");
	const double spilled = before - grams(after, "water");
	MESSAGE("spilled " << spilled << " g; the tube displaced " << after.displaced_ml << " mL");
	CHECK(spilled > 0.8 * after.displaced_ml);
	CHECK(spilled < 1.1 * after.displaced_ml);
}

TEST_CASE("bath: heat crosses ring by layer: a warm tube in cold water cools, its rings under the bath's surface faster than those over it (Stage 71)") {
	auto l = open(bench(150.0, 2.0, 40.0));
	ok(*l, R"({"do": "stand", "item": "tube", "in": "beaker"})");
	ok(*l, R"({"do": "wait", "seconds": 30})");
	const lab::ItemView tube = item(*l, "tube"), beaker = item(*l, "beaker");
	REQUIRE(tube.wall_c.size() > 4);
	MESSAGE("after 30 s: the tube " << tube.celsius << " C, the beaker " << beaker.celsius << " C; the tube's floor " << tube.wall_c.front() << " C, its top ring "
									<< tube.wall_c.back() << " C");
	CHECK(tube.celsius < 35.0); // it cools into the bath
	CHECK(beaker.celsius > 2.0); // which warms
	CHECK(tube.wall_c.front() < tube.wall_c.back()); // its floor in the cold water, its top in the air over it
	// the same tube on the bench, beside it: in the air only, it cools far less
	auto alone = open(bench(150.0, 2.0, 40.0));
	ok(*alone, R"({"do": "wait", "seconds": 30})");
	CHECK(item(*alone, "tube").celsius > tube.celsius + 3.0);
}

TEST_CASE("bath: saved and loaded with a tube standing in it, the lab goes on exactly as it was (Stage 71)") {
	auto l = open(bench(150.0, 5.0, 30.0));
	ok(*l, R"({"do": "stand", "item": "tube", "in": "beaker"})");
	ok(*l, R"({"do": "wait", "seconds": 5})");
	std::string why;
	auto copy = lab::Lab::load(l->save(), why);
	REQUIRE_MESSAGE(copy != nullptr, why);
	CHECK(copy->digest() == l->digest());
	ok(*l, R"({"do": "wait", "seconds": 10})");
	ok(*copy, R"({"do": "wait", "seconds": 10})");
	CHECK(copy->digest() == l->digest());
	CHECK(item(*copy, "tube").stands_in == "beaker");
}

TEST_CASE("measured: ice's long-wave emissivity is Warren and Brandt's n + ik's over 8 to 14 um (Stage 71)") {
	// data/optics/ice_lwir.csv, Warren and Brandt (2008)'s rows: the normal emissivity 1 - R averaged over the band
	std::ifstream in(std::string(CHEM_REPO_ROOT) + "/data/optics/ice_lwir.csv");
	REQUIRE(static_cast<bool>(in));
	std::vector<std::array<double, 3>> rows;
	for (std::string line; std::getline(in, line);) {
		std::array<double, 3> r{};
		if (!line.empty() && line[0] != '#' && std::sscanf(line.c_str(), "%lf,%lf,%lf", &r[0], &r[1], &r[2]) == 3) {
			rows.push_back(r);
		}
	}
	double sum = 0.0, span = 0.0;
	const auto e = [](double n, double k) { return 1.0 - ((n - 1.0) * (n - 1.0) + k * k) / ((n + 1.0) * (n + 1.0) + k * k); };
	for (size_t i = 0; i + 1 < rows.size(); ++i) {
		if (rows[i][0] >= 8000.0 && rows[i + 1][0] <= 14000.0) {
			const double dw = rows[i + 1][0] - rows[i][0];
			sum += 0.5 * (e(rows[i][1], rows[i][2]) + e(rows[i + 1][1], rows[i + 1][2])) * dw;
			span += dw;
		}
	}
	REQUIRE(span > 5000.0);
	const nlohmann::json media = nlohmann::json::parse(std::ifstream(std::string(CHEM_REPO_ROOT) + "/data/optics/media.json"));
	MESSAGE("the band's emissivity " << sum / span);
	CHECK(media.at("lwir").at("emissivity").at("ice").get<double>() == doctest::Approx(sum / span).epsilon(0.005));
}

TEST_CASE("bath: on the screen, a tube carried over a beaker stands in it by the menu's \"stand it in\", drawn on its floor (Stage 71)") {
	std::string why;
	auto ui = lab::Ui::open(bench(150.0), why);
	REQUIRE_MESSAGE(ui != nullptr, why);
	float tx = 0, ty = 0, bx = 0, by = 0;
	REQUIRE(ui->locate("item:tube", tx, ty));
	REQUIRE(ui->locate("item:beaker", bx, by));
	ui->pointer(tx, ty, true); // picked up
	ui->pointer(tx, ty, false);
	for (int k = 1; k <= 20; ++k) {
		ui->pointer(tx + (bx - tx) * static_cast<float>(k) / 20.0f, ty + (by - ty) * static_cast<float>(k) / 20.0f, false);
		ui->frame(1.0 / 60.0);
	}
	ui->pointer(bx, by, true, lab::Ui::ModRight); // the menu
	ui->pointer(bx, by, false, lab::Ui::ModRight);
	float sx = 0, sy = 0;
	REQUIRE(ui->locate("panel:stand", sx, sy));
	ui->pointer(sx, sy, true);
	ui->pointer(sx, sy, false);
	ui->frame(1.0 / 60.0);
	lab::ItemView tube{};
	for (const lab::ItemView &v : ui->lab().view().items) {
		tube = v.id == "tube" ? v : tube;
	}
	CHECK(tube.place == "in");
	CHECK(tube.stands_in == "beaker");
	// drawn within the beaker's width, its foot near the beaker's
	float nx = 0, ny = 0;
	REQUIRE(ui->locate("item:tube", nx, ny));
	CHECK(std::fabs(nx - bx) < 5.0f);
}
