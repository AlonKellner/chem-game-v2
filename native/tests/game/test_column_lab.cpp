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

TEST_CASE("column: a gentle pour lays a lighter liquid on a denser one; a fast pour stirs it in (Stage 47)") {
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
	auto fast = open(DENSE_AND_LIGHT);
	ok(*fast, R"({"do": "pour", "from": "bottle", "to": "beaker", "ml": 40})");
	CHECK(item(*fast, "beaker").layers.empty()); // one liquid
}

TEST_CASE("column: a stirrer mixes a layered vessel into one; a burner heats its lowest layer (Stage 47)") {
	auto stirred = open(read("data/benches/layers.json"));
	ok(*stirred, R"({"do": "stir", "vessel": "beaker", "on": true})");
	ok(*stirred, R"({"do": "wait", "seconds": 1})");
	CHECK(item(*stirred, "beaker").layers.empty());
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
