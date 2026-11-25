// A spectrophotometer in the lab (GAME_SPEC §13, Stage 55): a cuvette in its well, a blank, a reading of the
// cuvette's liquid at the beam's height; tuned; saved, not digested.
#include "lab/lab.hpp"
#include "lab/spectro.hpp"
#include "lab/ui.hpp"

#include "chem/builtin_ids.hpp"

#include <doctest/doctest.h>

#include "pupil.hpp"

#include <cmath>
#include <string>

namespace {

std::string bench() {
	return R"({"format": "chem-bench/2", "grid": [28, 15], "room": {"celsius": 20, "air_mol": 1000}, "seed": 9,
		"items": [
			{"id": "spec", "kind": "spectrophotometer", "nm": 750, "at": [2, 10]},
			{"id": "water", "kind": "vessel", "glassware": "beaker", "volume_ml": 250, "wall": "glass", "wall_g": 80, "at": [10, 7],
				"contents": {"water": {"g": 100}}},
			{"id": "blue", "kind": "vessel", "glassware": "beaker", "volume_ml": 250, "wall": "glass", "wall_g": 80, "at": [14, 7],
				"contents": {"water": {"g": 100}, "copper_sulfate_aq": {"mol": 0.005}}},
			{"id": "c0", "kind": "vessel", "glassware": "cuvette", "volume_ml": 5, "wall": "glass", "wall_g": 4, "loss": 0.05, "at": [20, 8]},
			{"id": "c1", "kind": "vessel", "glassware": "cuvette", "volume_ml": 5, "wall": "glass", "wall_g": 4, "loss": 0.05, "at": [23, 8]}
		]})";
}

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

lab::ItemView item(lab::Lab &l, const std::string &id) {
	for (const lab::ItemView &v : l.view().items) {
		if (v.id == id) {
			return v;
		}
	}
	FAIL("no " << id);
	return {};
}

double molar(lab::Lab &l, const std::string &id, const std::string &species) {
	const lab::ItemView v = item(l, id);
	for (const lab::SubstanceView &s : v.contents) {
		if (s.species == species) {
			return s.mol / (v.liquid_ml / 1000.0);
		}
	}
	return 0.0;
}

} // namespace

TEST_CASE("spectro lab: a cuvette of the solvent blanks it, a solution reads epsilon c l, a cuvette alone fits its well (Stage 55)") {
	auto l = open(bench());
	std::string why;
	must(*l, R"({"do": "pour", "from": "water", "to": "c0", "ml": 3.5})");
	must(*l, R"({"do": "pour", "from": "blue", "to": "c1", "ml": 3.5})");
	CHECK_FALSE(l->command(R"({"do": "insert", "spectrophotometer": "spec", "cuvette": "water"})", why)); // a beaker does not fit
	CHECK(why.find("cuvette") != std::string::npos);
	CHECK_FALSE(l->command(R"({"do": "blank", "spectrophotometer": "spec"})", why)); // nothing in its well
	must(*l, R"({"do": "insert", "spectrophotometer": "spec", "cuvette": "c0", "onto": true})");
	CHECK_FALSE(item(*l, "spec").blanked);
	CHECK_FALSE(item(*l, "spec").absorbance);
	must(*l, R"({"do": "blank", "spectrophotometer": "spec"})");
	CHECK(*item(*l, "spec").absorbance == 0.0);
	must(*l, R"({"do": "remove", "spectrophotometer": "spec"})");
	must(*l, R"({"do": "insert", "spectrophotometer": "spec", "cuvette": "c1", "onto": true})");
	const double c = molar(*l, "c1", "copper_sulfate_aq");
	CHECK(c == doctest::Approx(0.05).epsilon(0.02));
	const lab::spectro::Sample s{ { { chem::builtin::copper_sulfate_aq, c } }, 1.0 };
	const double ideal = lab::spectro::absorbance_at(s, lab::spectro::absorbance(s), 750);
	const double a = *item(*l, "spec").absorbance;
	MESSAGE("c " << c << " mol/L, A " << a << " against epsilon c l " << ideal);
	CHECK(a == doctest::Approx(ideal).epsilon(0.04)); // within its wavelength's error on the band's slope
	// the instrument changes nothing in the world
	l->command(R"({"do": "remove", "spectrophotometer": "spec"})", why);
	l->command(R"({"do": "insert", "spectrophotometer": "spec", "cuvette": "c1", "onto": true})", why);
	must(*l, R"({"do": "tune", "spectrophotometer": "spec", "nm": 700})");
	const double away = *item(*l, "spec").absorbance; // the blank was at 750: the lamp's slope with it
	must(*l, R"({"do": "tune", "spectrophotometer": "spec", "nm": 750})");
	CHECK(*item(*l, "spec").absorbance == a);
	CHECK(std::fabs(away - a) > 0.05);
	CHECK_FALSE(l->command(R"({"do": "tune", "spectrophotometer": "spec", "nm": 900})", why));
	// saved and loaded: its wavelength, its blank, the same reading; the digest is the world's
	auto loaded = lab::Lab::load(l->save(), why);
	REQUIRE_MESSAGE(loaded != nullptr, why);
	CHECK(item(*loaded, "spec").absorbance == item(*l, "spec").absorbance);
	CHECK(item(*loaded, "spec").nm == 750.0);
	CHECK(loaded->digest() == l->digest());
}

TEST_CASE("spectro lab: a liquid under the beam reads the air (Stage 55)") {
	auto l = open(bench());
	must(*l, R"({"do": "pour", "from": "water", "to": "c0", "ml": 3.5})");
	must(*l, R"({"do": "pour", "from": "blue", "to": "c1", "ml": 0.8})"); // 0.8 mL stands 10 mm deep: under the beam
	must(*l, R"({"do": "insert", "spectrophotometer": "spec", "cuvette": "c0", "onto": true})");
	must(*l, R"({"do": "blank", "spectrophotometer": "spec"})");
	must(*l, R"({"do": "remove", "spectrophotometer": "spec"})");
	must(*l, R"({"do": "insert", "spectrophotometer": "spec", "cuvette": "c1", "onto": true})");
	const double air = *item(*l, "spec").absorbance;
	MESSAGE("the air against the water's blank: " << air);
	CHECK(air <= 0.0); // no water in the beam: a little more light than through the blank
	CHECK(air > -0.05);
}

TEST_CASE("spectro screen: its slider and a nanometre's buttons tune it; a blank with an empty well is refused, saying why (Stage 55)") {
	pupil::Pupil p("data/benches/beer.json", "data/tutorials/beer.json", false);
	p.drag("item:spec", "square:3,13");
	REQUIRE(p.item("spec").place == "bench");
	p.click("item:spec");
	p.click("nm:spec@700");
	CHECK(p.item("spec").nm == 700.0);
	p.click("nm:spec:+");
	CHECK(p.item("spec").nm == 701.0);
	p.click("nm:spec:-");
	p.click("nm:spec:-");
	CHECK(p.item("spec").nm == 699.0);
	p.click("blank:spec");
	CHECK_FALSE(p.item("spec").blanked);
	CHECK(p.ui->message().find("nothing in its well") != std::string::npos);
	CHECK(p.drawn("no blank yet"));
	// a cuvette carried onto it stands in its well; the instrument's two displays are bodies of the pass
	p.drag("item:c0", "item:spec");
	CHECK(p.item("spec").under >= 0);
	p.click("item:spec");
	p.click("blank:spec");
	CHECK(p.item("spec").blanked);
	CHECK(p.drawn("absorbance 0.000"));
}

TEST_CASE("spectro lab: its cell holder warms the cuvette in its well to a set temperature and holds it, every invariant kept (Stage 56)") {
	auto l = open(bench());
	l->set_checking(true);
	std::string why;
	must(*l, R"({"do": "pour", "from": "blue", "to": "c1", "ml": 3.5})");
	CHECK_FALSE(l->command(R"({"do": "holder", "spectrophotometer": "spec", "celsius": 95})", why));
	must(*l, R"({"do": "holder", "spectrophotometer": "spec", "celsius": 50})");
	CHECK(*item(*l, "spec").holder_c == 50.0);
	must(*l, R"({"do": "wait", "seconds": 30})"); // nothing in its well: nothing warms
	CHECK(item(*l, "c1").celsius == doctest::Approx(20.0).epsilon(0.01));
	must(*l, R"({"do": "insert", "spectrophotometer": "spec", "cuvette": "c1", "onto": true})");
	must(*l, R"({"do": "wait", "seconds": 60})");
	const double warming = item(*l, "c1").celsius;
	MESSAGE("the cuvette after a minute in the holder: " << warming << " C");
	CHECK(warming > 40.0);
	// saved and loaded while it warms: the same lab, step by step
	auto loaded = lab::Lab::load(l->save(), why);
	REQUIRE_MESSAGE(loaded != nullptr, why);
	CHECK(loaded->digest() == l->digest());
	must(*l, R"({"do": "wait", "seconds": 240})");
	must(*loaded, R"({"do": "wait", "seconds": 240})");
	CHECK(loaded->digest() == l->digest());
	const double held = item(*l, "c1").celsius;
	MESSAGE("held at " << held << " C");
	CHECK(held == doctest::Approx(50.0).epsilon(0.02)); // within a kelvin of it: its loss to the room against its gain
	CHECK(held < 50.0);
	// lifted out, it cools; off, the holder takes its edge away
	must(*l, R"({"do": "remove", "spectrophotometer": "spec"})");
	must(*l, R"({"do": "wait", "seconds": 120})");
	CHECK(item(*l, "c1").celsius < held - 5.0);
	must(*l, R"({"do": "holder", "spectrophotometer": "spec"})");
	CHECK_FALSE(item(*l, "spec").holder_c);
}

TEST_CASE("spectro screen: its cell holder's switch and 5 C steps (Stage 56)") {
	pupil::Pupil p("data/benches/beer.json", "data/tutorials/beer.json", false);
	p.drag("item:spec", "square:3,13");
	p.click("item:spec");
	CHECK(p.drawn("holder off"));
	p.click("cell:spec");
	CHECK(*p.item("spec").holder_c == 25.0);
	p.click("cell:spec:+");
	p.click("cell:spec:+");
	CHECK(*p.item("spec").holder_c == 35.0);
	CHECK(p.drawn("holder 35 C"));
	p.click("cell:spec:-");
	CHECK(*p.item("spec").holder_c == 30.0);
	p.click("cell:spec");
	CHECK_FALSE(p.item("spec").holder_c);
}
