// The curriculum's step E (GAME_SPEC §13.7, Stage 85; docs/CURRICULUM.md): salts, precipitates and complexes, each
// experiment played through the screen with its own gestures, strict and free, at once and at a person's pace, and its
// report checked: what a student works out from the readings, held to the truth.
#include "lab/notebook.hpp"
#include "lab/ui.hpp"

#include <doctest/doctest.h>
#include <nlohmann/json.hpp>

#include "pupil.hpp"

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

using lab::TutorialView;
using pupil::Pupil;

namespace {

void completed(Pupil &p) {
	REQUIRE(p.tutorial().state == TutorialView::Complete);
	CHECK(p.ui->lab().view().conserved);
	CHECK(p.drawn("Tutorial complete"));
	for (const std::string &r : p.tutorial().results) {
		MESSAGE(r);
	}
}

// the report's numbers typed (a choice item skipped), and checked on the card
void report(Pupil &p, const std::vector<double> &typed) {
	for (size_t k = 0; k < typed.size(); ++k) {
		if (!p.ui->tutorial()->report()[k].choices.empty()) {
			continue;
		}
		p.click("report:" + std::to_string(k));
		for (int b = 0; b < 20; ++b) {
			p.ui->key(lab::Ui::KeyBackspace);
		}
		char number[32];
		std::snprintf(number, sizeof number, "%.6g", typed[k]);
		p.type(number);
	}
	p.click("report:check");
	p.ui->frame(1.0 / 60.0);
}

double truth_of(Pupil &p, const std::string &expression) {
	double v = 0.0;
	std::string why;
	REQUIRE_MESSAGE(lab::evaluate(expression, p.ui->lab().view(), v, why), why);
	return v;
}

// the burette's readings, in the order recorded
std::vector<double> burette_readings(Pupil &p) {
	std::vector<double> out;
	for (const lab::NoteEntry &e : p.ui->lab().view().notebook) {
		if (e.quantity.what == "scale_ml") {
			out.push_back(e.value);
		}
	}
	return out;
}

} // namespace

TEST_CASE("experiment ksp: followed with its own gestures, strict, each filtrate's titration gives its hydroxide, the solubility product and the common ion's effect (Stage 85)") {
	Pupil p("data/benches/ksp.json", "data/tutorials/ksp.json");
	CHECK(pupil::follow_through(p, 3600) > 20);
	completed(p);
	// what a student works out: [OH-] = 0.0500 M x V / 25.00 mL, Ksp = [OH-]^3 / 2
	const std::vector<double> r = burette_readings(p);
	REQUIRE(r.size() == 4);
	const double oh_a = 0.0500 * (r[1] - r[0]) / 25.00, oh_b = 0.0500 * (r[3] - r[2]) / 25.00, ksp = oh_a * oh_a * oh_a / 2.0;
	const double truth_a = truth_of(p, p.ui->tutorial()->report()[0].truth), truth_k = truth_of(p, p.ui->tutorial()->report()[1].truth),
				 truth_b = truth_of(p, p.ui->tutorial()->report()[2].truth);
	MESSAGE((r[1] - r[0]) << " mL and " << (r[3] - r[2]) << " mL: [OH-] " << oh_a << " and " << oh_b << " against " << truth_a << " and " << truth_b << "; Ksp " << ksp << " against " << truth_k);
	CHECK(std::abs(oh_a / truth_a - 1.0) < 0.02);
	CHECK(std::abs(oh_b / truth_b - 1.0) < 0.02);
	CHECK(oh_b < 0.9 * oh_a); // the common ion holds it back
	report(p, { oh_a, ksp, oh_b });
	p.click("report:3:0");
	p.click("report:check");
	p.ui->frame(1.0 / 60.0);
	CHECK(p.drawn("yours is right: less dissolves"));
	CHECK(!p.drawn("yours is off by"));
}

TEST_CASE("experiment ksp: played as a person plays it, strict, it completes (Stage 85)") {
	Pupil p("data/benches/ksp.json", "data/tutorials/ksp.json");
	pupil::play_as_person(p, 3600);
	completed(p);
}

TEST_CASE("experiment ksp: followed with its own gestures, free, it completes (Stage 85)") {
	Pupil p("data/benches/ksp.json", "data/tutorials/ksp.json", false);
	pupil::follow_through(p, 3600);
	completed(p);
}

TEST_CASE("experiment hardness: followed with its own gestures, strict, the EDTA run in to the dye's blue gives the water's hardness (Stage 85)") {
	Pupil p("data/benches/hardness.json", "data/tutorials/hardness.json");
	CHECK(pupil::follow_through(p, 3600) > 8);
	completed(p);
	// what a student works out: mg/L = 0.0100 M x V x 100.09 g/mol / 25.00 mL
	const std::vector<double> r = burette_readings(p);
	REQUIRE(r.size() == 2);
	const double found = 0.0100 * (r[1] - r[0]) * 100.09 * 1000.0 / 25.00, truth = truth_of(p, p.ui->tutorial()->report()[0].truth);
	MESSAGE((r[1] - r[0]) << " mL of EDTA: " << found << " mg/L against " << truth);
	CHECK(std::abs(found / truth - 1.0) < 0.03);
	report(p, { found });
	CHECK(p.drawn("yours is within it"));
	report(p, { truth * 1.1 });
	CHECK(p.drawn("yours is off by"));
}

TEST_CASE("experiment hardness: played as a person plays it, strict, it completes (Stage 85)") {
	Pupil p("data/benches/hardness.json", "data/tutorials/hardness.json");
	pupil::play_as_person(p, 3600);
	completed(p);
}

TEST_CASE("experiment hardness: followed with its own gestures, free, it completes (Stage 85)") {
	Pupil p("data/benches/hardness.json", "data/tutorials/hardness.json", false);
	pupil::follow_through(p, 3600);
	completed(p);
}

TEST_CASE("experiment lechatelier: followed with its own gestures, strict, each tube against its control shows which way its equilibrium went, and the report's choices are right (Stage 85)") {
	Pupil p("data/benches/lechatelier.json", "data/tutorials/lechatelier.json");
	CHECK(pupil::follow_through(p, 1800) > 8);
	completed(p);
	// the truth the tubes show: each toward the choice Le Chatelier says
	for (size_t k = 0; k < 5; ++k) {
		const double v = truth_of(p, p.ui->tutorial()->report()[k].choices[0].truth);
		MESSAGE(p.ui->tutorial()->report()[k].label << ": " << v);
		CHECK(v > 0.0);
	}
	// enough to see: the warm tube's 0.25 mmol of cobalt, 3 % or more of it gone blue
	CHECK(truth_of(p, p.ui->tutorial()->report()[3].choices[0].truth) > 0.03 * 2.5e-4);
	for (int k = 0; k < 5; ++k) {
		p.click("report:" + std::to_string(k) + ":0");
	}
	p.click("report:check");
	p.ui->frame(1.0 / 60.0);
	CHECK(!p.drawn("not quite"));
	CHECK(p.drawn("yours is right: toward the blue CoCl4 2-"));
	p.click("report:3:1");
	p.click("report:check");
	p.ui->frame(1.0 / 60.0);
	CHECK(p.drawn("not quite: toward the blue CoCl4 2-"));
}

TEST_CASE("experiment lechatelier: played as a person plays it, strict, it completes (Stage 85)") {
	Pupil p("data/benches/lechatelier.json", "data/tutorials/lechatelier.json");
	pupil::play_as_person(p, 1800);
	completed(p);
}

TEST_CASE("experiment lechatelier: followed with its own gestures, free, it completes (Stage 85)") {
	Pupil p("data/benches/lechatelier.json", "data/tutorials/lechatelier.json", false);
	pupil::follow_through(p, 1800);
	completed(p);
}


TEST_CASE("experiment qualitative: followed with its own gestures, strict, the tests and the flame tell the unknown's ions, and the right choices are right (Stage 85)") {
	Pupil p("data/benches/qualitative.json", "data/tutorials/qualitative.json");
	CHECK(pupil::follow_through(p, 600) > 6);
	completed(p);
	// the choices that hold, picked: right; another: not
	for (size_t k = 0; k < 2; ++k) {
		const auto &choices = p.ui->tutorial()->report()[k].choices;
		int holds = 0;
		for (size_t i = 0; i < choices.size(); ++i) {
			if (truth_of(p, choices[i].truth) > 0.0) {
				p.click("report:" + std::to_string(k) + ":" + std::to_string(i));
				++holds;
				MESSAGE(p.ui->tutorial()->report()[k].label << ": " << choices[i].label);
			}
		}
		CHECK(holds == 1);
	}
	p.click("report:check");
	p.ui->frame(1.0 / 60.0);
	CHECK(!p.drawn("not quite"));
}

TEST_CASE("experiment qualitative: played as a person plays it, strict, it completes (Stage 85)") {
	Pupil p("data/benches/qualitative.json", "data/tutorials/qualitative.json");
	pupil::play_as_person(p, 600);
	completed(p);
}

TEST_CASE("experiment qualitative: followed with its own gestures, free, it completes (Stage 85)") {
	Pupil p("data/benches/qualitative.json", "data/tutorials/qualitative.json", false);
	pupil::follow_through(p, 600);
	completed(p);
}

TEST_CASE("experiment qualitative: each of the twelve salts answers its own tests and none of the others' (Stage 85)") {
	const nlohmann::json bench = nlohmann::json::parse(pupil::read("data/benches/qualitative.json"));
	nlohmann::json unknown;
	for (const nlohmann::json &i : bench.at("items")) {
		if (i.at("id") == "unknown") {
			unknown = i;
		}
	}
	for (const nlohmann::json &salt : unknown.at("one_of")) {
		std::string name;
		for (const auto &[k, v] : salt.items()) {
			name = k == "water" ? name : k;
		}
		CAPTURE(name);
		nlohmann::json b = bench;
		for (nlohmann::json &i : b.at("items")) {
			if (i.at("id") == "unknown") {
				i.erase("one_of");
				i["contents"] = salt;
			}
		}
		std::string why;
		auto l = lab::Lab::open(b.dump(), why);
		REQUIRE_MESSAGE(l != nullptr, why);
		const auto ok = [&](const std::string &c) { REQUIRE_MESSAGE(l->command(c, why), c << ": " << why); };
		for (const auto &[tube, reagent] : { std::pair{ "tube_1", "silver" }, std::pair{ "tube_2", "barium" }, std::pair{ "tube_3", "acid" }, std::pair{ "tube_4", "base" } }) {
			ok(std::string(R"({"do": "pour", "from": "unknown", "to": ")") + tube + R"(", "ml": 3})");
			ok(std::string(R"({"do": "pour", "from": ")") + reagent + R"(", "to": ")" + tube + R"(", "ml": 1})");
		}
		ok(R"({"do": "valve", "burner": "burner", "to": 1})");
		ok(R"({"do": "flame_test", "burner": "burner", "from": "unknown"})");
		const auto g = [&](const char *tube, const char *species) {
			double v = 0.0;
			std::string w;
			lab::evaluate(std::string(tube) + "." + species, l->view(), v, w);
			return v;
		};
		const lab::ItemView burner = [&] {
			for (const lab::ItemView &v : l->view().items) {
				if (v.id == "burner") {
					return v;
				}
			}
			return lab::ItemView{};
		}();
		std::string flame;
		for (const auto &[element, mol] : burner.flame_load) {
			flame += element;
		}
		ok(R"({"do": "wait", "seconds": 5})");
		const bool chloride = name.find("chloride") != std::string::npos, sulfate = name.find("sulfate") != std::string::npos, carbonate = name.find("carbonate") != std::string::npos;
		CHECK((g("tube_1", "silver_chloride") > 0.001) == chloride); // 0.3 mmol of AgCl is 43 mg
		CHECK((g("tube_2", "barium_sulfate") > 0.001) == sulfate);
		CHECK((g("tube_3", "carbon_dioxide") + g("tube_3", "carbon_dioxide_aq") > 0.002) == carbonate); // (0.3 mmol of carbonate: 13 mg of CO2; the room's in a tube's air, micrograms)
		CHECK((g("tube_4", "copper_hydroxide") > 0.001) == (name.find("copper") != std::string::npos));
		CHECK((g("tube_4", "iron_iii_hydroxide") > 0.001) == (name.find("iron") != std::string::npos));
		// the flame: the metal's atoms in it (sodium's from the salt, not from a reagent)
		const std::string metal = name.substr(0, name.find('_'));
		const std::string symbol = metal == "sodium" ? "Na" : metal == "lithium" ? "Li" : metal == "calcium" ? "Ca" : metal == "barium" ? "Ba" : metal == "copper" ? "Cu" : "Fe"; // (iron's held, not coloured)
		CHECK(flame == symbol);
	}
}


TEST_CASE("experiment kc: followed with its own gestures, strict, Benesi and Hildebrand's line through 1/A against 1/[Fe] gives FeSCN2+'s constant (Stage 85)") {
	Pupil p("data/benches/kc.json", "data/tutorials/kc.json");
	CHECK(pupil::follow_through(p, 600) > 6);
	completed(p);
	// what a student works out: each mixture's iron written, its absorbance recorded; the least-squares line through
	// (1/[Fe], 1/A), K = intercept / slope
	std::vector<double> fe, a;
	for (const lab::NoteEntry &e : p.ui->lab().view().notebook) {
		if (e.label == "Fe") {
			fe.push_back(e.value);
		} else if (e.quantity.what == "absorbance") {
			a.push_back(e.value);
		}
	}
	REQUIRE(fe.size() == 4);
	REQUIRE(a.size() == 4);
	double sx = 0.0, sy = 0.0, sxx = 0.0, sxy = 0.0;
	for (size_t k = 0; k < 4; ++k) {
		const double x = 1.0 / fe[k], y = 1.0 / a[k];
		sx += x;
		sy += y;
		sxx += x * x;
		sxy += x * y;
		MESSAGE("[Fe] " << fe[k] << " M: A " << a[k]);
	}
	const double slope = (4.0 * sxy - sx * sy) / (4.0 * sxx - sx * sx), intercept = (sy - slope * sx) / 4.0, k = intercept / slope;
	const double truth = truth_of(p, p.ui->tutorial()->report()[0].truth);
	MESSAGE("K " << k << " L/mol against " << truth);
	for (const char *m : { "m1", "m2", "m3", "m4" }) {
		const std::string c(m);
		MESSAGE(c << ": formal quotient " << truth_of(p, c + ".iron_thiocyanate.mol * " + c + ".condensed_ml / 1000 / ((" + c + ".iron_iii_ion.mol + " + c + ".iron_hydroxo_ion.mol) * " + c + ".thiocyanate_ion.mol)") << ", Fe(SCN)2+ " << truth_of(p, c + ".iron_dithiocyanate.mol") << ", FeSCN2+ " << truth_of(p, c + ".iron_thiocyanate.mol"));
	}
	CHECK(std::abs(k / truth - 1.0) < 0.12);
	report(p, { k });
	CHECK(p.drawn("yours is within it"));
}

TEST_CASE("experiment kc: played as a person plays it, strict, it completes (Stage 85)") {
	Pupil p("data/benches/kc.json", "data/tutorials/kc.json");
	pupil::play_as_person(p, 600);
	completed(p);
}

TEST_CASE("experiment kc: followed with its own gestures, free, it completes (Stage 85)") {
	Pupil p("data/benches/kc.json", "data/tutorials/kc.json", false);
	pupil::follow_through(p, 600);
	completed(p);
}
