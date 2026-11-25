// The vertical dimension in the lab (GAME_SPEC §6, §13, Stage 47): a vessel layered from its bench, by a gentle
// pour, mixed by a fast pour or a stirrer; a burner under its lowest layer; read as a whole and layer by layer; saved
// and loaded as it is; and on the screen, a band of each layer's own colour.
#include "lab/colour.hpp"
#include "lab/lab.hpp"
#include "lab/optics_scene.hpp"
#include "lab/ui.hpp"

#include <doctest/doctest.h>

#include <cmath>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>

namespace {

std::string read(const std::string &relative) {
	std::ifstream in(std::string(CHEM_REPO_ROOT) + "/" + relative);
	REQUIRE_MESSAGE(static_cast<bool>(in), relative);
	std::ostringstream out;
	out << in.rdbuf();
	return out.str();
}

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

double mol_in(const lab::LayerView &layer, const std::string &species) {
	for (const lab::SubstanceView &s : layer.contents) {
		if (s.species == species) {
			return s.mol;
		}
	}
	return 0.0;
}

double mol_in(const lab::ItemView &v, const std::string &species) {
	for (const lab::SubstanceView &s : v.contents) {
		if (s.species == species) {
			return s.mol;
		}
	}
	return 0.0;
}

// a beaker of 1.4 mol/kg copper sulfate, and a bottle of 0.8 mol/kg nickel sulfate
const char *const DENSE_AND_LIGHT = R"({"format": "chem-bench/2", "room": {"celsius": 20, "air_mol": 1000}, "items": [
	{"id": "beaker", "at": [12, 7], "kind": "vessel", "volume_ml": 250, "wall": "glass", "glassware": "beaker", "wall_g": 80, "loss": 0.5,
		"contents": {"water": {"g": 80}, "copper_sulfate_aq": {"mol": 0.112}}},
	{"id": "bottle", "at": [4, 7], "kind": "vessel", "volume_ml": 500, "wall": "glass", "glassware": "bottle", "wall_g": 250, "loss": 0.8,
		"contents": {"water": {"g": 200}, "nickel_sulfate_aq": {"mol": 0.16}}}]})";

} // namespace

TEST_CASE("column: the layered bench keeps green over blue, read as a whole and layer by layer; their boundary spreads (Stage 47)") {
	auto l = open(read("data/benches/layers.json"));
	l->set_checking(true);
	lab::ItemView v = item(*l, "beaker");
	REQUIRE(v.layers.size() == 2);
	CHECK(mol_in(v.layers[0], "nickel_sulfate_aq") > 0.06); // the top, green
	CHECK(mol_in(v.layers[0], "copper_sulfate_aq") == 0.0);
	CHECK(mol_in(v.layers[1], "copper_sulfate_aq") > 0.11); // under it, blue
	CHECK(mol_in(v, "copper_sulfate_aq") + 1e-9 >= 0.112 - 1e-6); // the whole, read as one
	CHECK(v.liquid_ml > 150.0);
	std::string why;
	REQUIRE_MESSAGE(l->advance(60 * 600, why), why); // ten minutes
	v = item(*l, "beaker");
	CHECK(v.layers.size() > 2); // the boundary resolved into thinner layers
	CHECK(mol_in(v.layers.front(), "copper_sulfate_aq") < 1e-4); // the top still green, the copper not near it
	CHECK(mol_in(v.layers.back(), "nickel_sulfate_aq") < 1e-4);
	MESSAGE("after ten minutes " << v.layers.size() << " layers");
}

TEST_CASE("column: a gentle pour lays a lighter liquid on a denser one; a fast one's stream mixes them as far as its energy pays (Stage 47, Stage 51)") {
	auto gentle = open(DENSE_AND_LIGHT);
	ok(*gentle, R"({"do": "pour", "from": "bottle", "to": "beaker", "ml": 40, "gentle": true})");
	lab::ItemView v = item(*gentle, "beaker");
	REQUIRE(v.layers.size() >= 2);
	CHECK(mol_in(v.layers.front(), "nickel_sulfate_aq") > 0.02); // on top, the green it poured
	CHECK(mol_in(v.layers.front(), "copper_sulfate_aq") < 1e-4);
	double under = 0.0; // the copper sulfate, in every layer under the one it poured (it splits as its boundary is resolved)
	for (size_t k = 1; k < v.layers.size(); ++k) {
		under += mol_in(v.layers[k], "copper_sulfate_aq");
	}
	CHECK(under > 0.11);
	// (Stage 51) poured at the bottle's full tilt from its lip over the beaker's brim, a stream falling some 7 cm:
	// 40 mL landing at 1.2 m/s carry about 30 mJ, a fifth of which is many times the 0.9 mJ that make the two one
	auto fast = open(DENSE_AND_LIGHT);
	ok(*fast, R"({"do": "pour", "from": "bottle", "to": "beaker", "ml": 40})");
	CHECK(item(*fast, "beaker").layers.empty());
}

TEST_CASE("column: a stirrer mixes a layered vessel into one as its turning builds; a short stir leaves it part mixed; a burner heats its lowest layer (Stage 47, Stage 49)") {
	// (Stage 49, the user's test: stirred, the layers merged at once) the turning builds over a second and a half,
	// and mixes the layers as far as what it dissipates pays
	auto stirred = open(read("data/benches/layers.json"));
	stirred->set_checking(true);
	ok(*stirred, R"({"do": "stir", "vessel": "beaker", "on": true})");
	ok(*stirred, R"({"do": "wait", "seconds": 0.25})");
	CHECK(item(*stirred, "beaker").layers.size() >= 2); // a quarter of a second: still layered
	ok(*stirred, R"({"do": "wait", "seconds": 5})");
	CHECK(item(*stirred, "beaker").layers.empty());
	auto brief = open(read("data/benches/layers.json"));
	ok(*brief, R"({"do": "stir", "vessel": "beaker", "on": true})");
	ok(*brief, R"({"do": "wait", "seconds": 0.3})");
	ok(*brief, R"({"do": "stir", "vessel": "beaker", "on": false})");
	ok(*brief, R"({"do": "wait", "seconds": 10})"); // its turning gone
	const lab::ItemView part = item(*brief, "beaker");
	REQUIRE(part.layers.size() >= 2);
	CHECK(mol_in(part.layers.back(), "nickel_sulfate_aq") > 1e-3); // the green stirred down into the blue
	// the top still the greener: less of the blue than an even mixture, more of the green than the bottom
	const lab::LayerView &top = part.layers.front(), &bottom = part.layers.back();
	CHECK(mol_in(top, "copper_sulfate_aq") / top.liquid_ml < 0.5 * 0.112 / part.liquid_ml);
	CHECK(mol_in(top, "nickel_sulfate_aq") / top.liquid_ml > mol_in(bottom, "nickel_sulfate_aq") / bottom.liquid_ml);
	MESSAGE("a short stir: " << part.layers.size() << " layers; the top " << top.liquid_ml << " mL, Cu " << mol_in(top, "copper_sulfate_aq") << " mol; the bottom "
							 << bottom.liquid_ml << " mL, Ni " << mol_in(bottom, "nickel_sulfate_aq") << " mol");
	auto heated = open(read("data/benches/layers.json"));
	ok(*heated, R"({"do": "heat", "burner": "burner", "vessel": "beaker"})");
	ok(*heated, R"({"do": "valve", "burner": "burner", "to": 0.2})");
	ok(*heated, R"({"do": "wait", "seconds": 60})");
	const lab::ItemView v = item(*heated, "beaker");
	REQUIRE(v.layers.size() >= 2);
	// the blue under it warms first; it is still the denser, so they stay
	CHECK(v.layers.back().celsius > v.layers.front().celsius + 5.0);
	MESSAGE("a minute on a low flame: the bottom " << v.layers.back().celsius << " C, the top " << v.layers.front().celsius << " C");
}

TEST_CASE("column: a layered lab saved and loaded goes on exactly as the one not saved (Stage 47)") {
	auto first = open(read("data/benches/layers.json"));
	std::string why;
	REQUIRE(first->advance(60 * 100, why));
	auto second = lab::Lab::load(first->save(), why);
	REQUIRE_MESSAGE(second != nullptr, why);
	CHECK(second->digest() == first->digest());
	REQUIRE(first->advance(60 * 100, why));
	REQUIRE(second->advance(60 * 100, why));
	CHECK(second->digest() == first->digest());
}

TEST_CASE("column: on the screen a layered vessel is a band of each layer's colour, the pass's own the truth's (Stage 47)") {
	std::string why;
	std::unique_ptr<lab::Ui> ui = lab::Ui::open(read("data/benches/layers.json"), why);
	REQUIRE_MESSAGE(ui != nullptr, why);
	ui->frame(0.0);
	const lab::OpticsScene &scene = ui->optics();
	REQUIRE(lab::check_O1(scene) == "");
	const int body = lab::optics_body(scene, "beaker");
	REQUIRE(body >= 0);
	namespace L = lab::optics_layout;
	const int set = static_cast<int>(scene.table[static_cast<size_t>(L::HEADER_VECS + body * L::BODY_VECS + L::SET) * 4]);
	CHECK(scene.rows[static_cast<size_t>((set * L::ROWS_PER_SET + L::BANDS_ROW) * L::ROW)] == 2.0f);
	// a pixel in each band: low, blue; high, green
	const float *rect = &scene.table[static_cast<size_t>(L::HEADER_VECS + body * L::BODY_VECS) * 4];
	const float *form = &scene.table[static_cast<size_t>(L::HEADER_VECS + body * L::BODY_VECS + L::FORM) * 4];
	const int x = static_cast<int>(rect[0] + rect[2] / 2.0f + rect[2] * 0.15f);
	const float floor_y = rect[1] + rect[3] - scene.table[static_cast<size_t>(L::HEADER_VECS + body * L::BODY_VECS + L::REFRACT) * 4 + 2] / form[1];
	const float surface = form[2];
	const int low = static_cast<int>(floor_y - 0.25f * (floor_y - surface)), high = static_cast<int>(floor_y - 0.8f * (floor_y - surface));
	const auto colour = [&](int y) {
		double rgb[3];
		lab::optics_truth(scene, x, y, rgb);
		return lab::to_lab(chem::optics::Xyz{ 0.4124564 * rgb[0] + 0.3575761 * rgb[1] + 0.1804375 * rgb[2], 0.2126729 * rgb[0] + 0.7151522 * rgb[1] + 0.0721750 * rgb[2],
				0.0193339 * rgb[0] + 0.1191920 * rgb[1] + 0.9503041 * rgb[2] });
	};
	const lab::CieLab blue = colour(low), green = colour(high);
	MESSAGE("low: L* " << blue.l << " a* " << blue.a << " b* " << blue.b << "; high: L* " << green.l << " a* " << green.a << " b* " << green.b);
	CHECK(blue.b < -10.0);
	CHECK(green.a < -3.0); // green
	CHECK(green.b > blue.b + 5.0);
	CHECK(lab::check_O2(scene, x, low) == "");
	CHECK(lab::check_O2(scene, x, high) == "");
}

TEST_CASE("column: the layered bench has vessels to layer by hand: the green poured gently onto the blue lies on it (Stage 47)") {
	// the user's test: the experiment had no second vessel to pour into
	auto l = open(read("data/benches/layers.json"));
	CHECK(item(*l, "blue").layers.empty());
	CHECK(mol_in(item(*l, "blue"), "copper_sulfate_aq") > 0.11);
	CHECK(mol_in(item(*l, "green"), "nickel_sulfate_aq") > 0.15);
	CHECK(item(*l, "empty").contents.size() > 0); // (its air)
	ok(*l, R"({"do": "pour", "from": "green", "to": "blue", "ml": 40, "gentle": true})");
	const lab::ItemView v = item(*l, "blue");
	REQUIRE(v.layers.size() >= 2);
	CHECK(mol_in(v.layers.front(), "nickel_sulfate_aq") > 0.02);
	CHECK(mol_in(v.layers.front(), "copper_sulfate_aq") < 1e-4);
	ok(*l, R"({"do": "pour", "from": "green", "to": "empty", "ml": 40})");
	CHECK(item(*l, "empty").liquid_ml > 30.0);
}

TEST_CASE("column: on the screen the pour menu's \"pour gently\" lays the green on the blue, its \"pour\" mixes them (Stage 47, Stage 51)") {
	for (const bool gently : { true, false }) {
		std::string why;
		std::unique_ptr<lab::Ui> ui = lab::Ui::open(read("data/benches/layers.json"), why);
		REQUIRE_MESSAGE(ui != nullptr, why);
		ui->frame(1.0 / 60);
		const auto press = [&](const std::string &what, int mods) {
			float x = 0, y = 0;
			REQUIRE_MESSAGE(ui->locate(what, x, y), "not on the screen: " << what);
			ui->pointer(x, y, false, mods);
			ui->pointer(x, y, true, mods);
			ui->pointer(x, y, false, mods);
			ui->frame(1.0 / 60);
		};
		press("item:green", 0); // picked up
		press("item:blue", lab::Ui::ModRight); // onto the blue: the pour menu
		press("panel:slider@0.2", 0);
		press(gently ? "panel:gentle" : "panel:pour", 0);
		const lab::ItemView v = item(ui->lab(), "blue");
		if (gently) {
			REQUIRE(v.layers.size() >= 2);
			CHECK(mol_in(v.layers.front(), "nickel_sulfate_aq") > 0.02);
			CHECK(mol_in(v.layers.front(), "copper_sulfate_aq") < 1e-4);
		} else {
			// (Stage 51) its stream's energy mixes them
			CHECK(v.layers.empty());
			CHECK(mol_in(v, "nickel_sulfate_aq") > 0.02);
		}
	}
}

TEST_CASE("column: a held pour onto a layered vessel at any speed, a frame's first step checked, keeps every invariant (Stage 47)") {
	// the user's test: poured by hand, the lab stopped on C5. A pour that lands as a layer moves the liquid there
	// before the step; after unchecked steps a checked one could not then start where the last one ended.
	for (const char *target : { "blue", "beaker" }) {
		for (const char *speed : { "speed:10", "speed:600" }) {
			std::string why;
			std::unique_ptr<lab::Ui> ui = lab::Ui::open(read("data/benches/layers.json"), why);
			REQUIRE(ui != nullptr);
			ui->frame(1.0 / 60);
			float x = 0, y = 0;
			REQUIRE(ui->locate(speed, x, y));
			ui->pointer(x, y, false);
			ui->pointer(x, y, true);
			ui->pointer(x, y, false);
			float ax = 0, ay = 0, fx = 0, fy = 0;
			REQUIRE(ui->locate("item:green", ax, ay));
			REQUIRE(ui->locate(std::string("item:") + target, fx, fy));
			ui->pointer(ax, ay, true);
			ui->pointer(ax, ay, false);
			for (int k = 1; k <= 20; ++k) {
				ui->pointer(ax + (fx - ax) * static_cast<float>(k) / 20.0f, ay + (fy - ay) * static_cast<float>(k) / 20.0f, false);
				ui->frame(1.0 / 60.0);
			}
			ui->pointer(fx, fy, true);
			for (int k = 0; k < 360; ++k) {
				if (k == 240) {
					ui->pointer(fx, fy, false);
				}
				ui->frame(1.0 / 60.0);
			}
			CHECK_MESSAGE(ui->message().empty(), target << " at " << speed << ": " << ui->message());
			CHECK_FALSE(ui->lab().failed());
		}
	}
}

TEST_CASE("column: a layered vessel pours its top layer first, the one under it as the level reaches it (Stage 47)") {
	// the user's test: the layers looked mixed in the vessel poured from
	auto l = open(read("data/benches/layers.json"));
	l->set_checking(true);
	ok(*l, R"({"do": "pour", "from": "beaker", "to": "empty", "ml": 60})");
	lab::ItemView got = item(*l, "empty");
	CHECK(mol_in(got, "nickel_sulfate_aq") > 0.04); // the green, the top
	CHECK(mol_in(got, "copper_sulfate_aq") < 0.002);
	const lab::ItemView left = item(*l, "beaker");
	CHECK(mol_in(left, "copper_sulfate_aq") > 0.11); // the blue still in it, all of it
	ok(*l, R"({"do": "pour", "from": "beaker", "to": "empty", "ml": 60})");
	got = item(*l, "empty");
	CHECK(mol_in(got, "nickel_sulfate_aq") > 0.06); // the rest of the green
	CHECK(mol_in(got, "copper_sulfate_aq") > 0.02); // and then the blue
}

TEST_CASE("column: a layered vessel tipped to pour shows its layers level in the room: the top one at the lip, the lower at its far bottom (Stage 47)") {
	std::string why;
	std::unique_ptr<lab::Ui> ui = lab::Ui::open(read("data/benches/layers.json"), why);
	REQUIRE_MESSAGE(ui != nullptr, why);
	ui->frame(1.0 / 60);
	float ax = 0, ay = 0, fx = 0, fy = 0;
	REQUIRE(ui->locate("item:beaker", ax, ay));
	REQUIRE(ui->locate("item:empty", fx, fy));
	ui->pointer(ax, ay, true);
	ui->pointer(ax, ay, false);
	for (int k = 1; k <= 20; ++k) {
		ui->pointer(ax + (fx - ax) * static_cast<float>(k) / 20.0f, ay + (fy - ay) * static_cast<float>(k) / 20.0f, false);
		ui->frame(1.0 / 60.0);
	}
	ui->pointer(fx, fy, true);
	for (int k = 0; k < 120; ++k) { // held: tipped, and pouring
		ui->frame(1.0 / 60.0);
	}
	const lab::ItemView v = item(ui->lab(), "beaker");
	REQUIRE(v.turn > 0.3);
	REQUIRE(v.layers.size() >= 2);
	const lab::OpticsScene &scene = ui->optics();
	REQUIRE(lab::check_O1(scene) == "");
	const int body = lab::optics_body(scene, "beaker");
	REQUIRE(body >= 0);
	namespace L = lab::optics_layout;
	const int set = static_cast<int>(scene.table[static_cast<size_t>(L::HEADER_VECS + body * L::BODY_VECS + L::SET) * 4]);
	CHECK(scene.rows[static_cast<size_t>((set * L::ROWS_PER_SET + L::BANDS_ROW) * L::ROW)] >= 2.0f); // its bands, tipped
	// the liquid's lowest and highest points on the screen, along the column through the rectangle's middle third
	const float *rect = &scene.table[static_cast<size_t>(L::HEADER_VECS + body * L::BODY_VECS) * 4];
	const auto colour = [&](int x, int y) {
		double rgb[3];
		lab::optics_truth(scene, x, y, rgb);
		return lab::to_lab(chem::optics::Xyz{ 0.4124564 * rgb[0] + 0.3575761 * rgb[1] + 0.1804375 * rgb[2], 0.2126729 * rgb[0] + 0.7151522 * rgb[1] + 0.0721750 * rgb[2],
				0.0193339 * rgb[0] + 0.1191920 * rgb[1] + 0.9503041 * rgb[2] });
	};
	int blues = 0, greens = 0;
	for (int x = static_cast<int>(rect[0]); x < static_cast<int>(rect[0] + rect[2]); x += 4) {
		for (int y = static_cast<int>(rect[1]); y < static_cast<int>(rect[1] + rect[3]); y += 4) {
			const lab::CieLab c = colour(x, y);
			if (c.b < -25.0 && c.a > -20.0) {
				++blues; // the copper's blue
			} else if (c.a < -15.0 && c.b > -15.0) {
				++greens; // the nickel's green
			}
		}
	}
	MESSAGE("tipped: " << blues << " blue samples, " << greens << " green");
	CHECK(blues > 20);
	CHECK(greens > 20);
}

namespace {

// a bottle of green nickel sulfate over blue copper sulfate, and an empty beaker to drain it into
const char *const LAYERED_BOTTLE = R"({"format": "chem-bench/2", "room": {"celsius": 20, "air_mol": 1000}, "items": [
	{"id": "bottle", "at": [4, 7], "kind": "vessel", "volume_ml": 500, "wall": "glass", "glassware": "bottle", "wall_g": 250, "loss": 0.8,
		"layers": [{"water": {"g": 80}, "nickel_sulfate_aq": {"mol": 0.064}}, {"water": {"g": 80}, "copper_sulfate_aq": {"mol": 0.112}}]},
	{"id": "cup", "at": [12, 7], "kind": "vessel", "volume_ml": 250, "wall": "glass", "glassware": "beaker", "wall_g": 80, "loss": 0.5}]})";

} // namespace

TEST_CASE("column: an outlet at a layered vessel's bottom draws its lowest layer first (Stage 49)") {
	// Stage 47's Open: the outlet drew from the top layer
	auto l = open(read("data/benches/layers.json"));
	l->set_checking(true);
	ok(*l, R"({"do": "under", "item": "empty", "of": "beaker"})");
	std::string why;
	REQUIRE_MESSAGE(l->advance(1, why), why); // (an outlet runs all of what is over it each step: the lowest layer, then the next)
	const lab::ItemView got = item(*l, "empty");
	CHECK(mol_in(got, "copper_sulfate_aq") > 0.11); // the blue, from the bottom
	CHECK(mol_in(got, "nickel_sulfate_aq") < 1e-6);
	CHECK(mol_in(item(*l, "beaker"), "nickel_sulfate_aq") > 0.0639); // the green all still in it
	ok(*l, R"({"do": "undrain", "of": "beaker"})"); // its line comes off, from whichever layer it left
	const double there = item(*l, "empty").liquid_ml;
	REQUIRE_MESSAGE(l->advance(60, why), why);
	CHECK(item(*l, "empty").liquid_ml == doctest::Approx(there).epsilon(1e-3));
}

TEST_CASE("column: a layered bottle upside down runs its lowest layer out through its cap first, the one over it after (Stage 49)") {
	auto l = open(LAYERED_BOTTLE);
	l->set_checking(true);
	ok(*l, R"({"do": "invert", "item": "bottle", "over": "cup"})");
	ok(*l, R"({"do": "cap", "item": "bottle", "to": 1})");
	std::string why;
	REQUIRE_MESSAGE(l->advance(60, why), why); // a second: some of the blue
	lab::ItemView got = item(*l, "cup");
	CHECK(mol_in(got, "copper_sulfate_aq") > 0.005);
	CHECK(mol_in(got, "nickel_sulfate_aq") < 1e-4);
	// saved and loaded while its cap's drain leaves from a layer, it goes on exactly
	auto copy = lab::Lab::load(l->save(), why);
	REQUIRE_MESSAGE(copy != nullptr, why);
	REQUIRE_MESSAGE(copy->advance(60 * 10, why), why);
	REQUIRE_MESSAGE(l->advance(60 * 10, why), why);
	CHECK(copy->digest() == l->digest());
	REQUIRE_MESSAGE(l->advance(60 * 110, why), why); // two minutes: all of it, the green last
	got = item(*l, "cup");
	CHECK(mol_in(got, "copper_sulfate_aq") > 0.11);
	CHECK(mol_in(got, "nickel_sulfate_aq") > 0.06);
	MESSAGE("through the cap: " << got.liquid_ml << " mL");
}
