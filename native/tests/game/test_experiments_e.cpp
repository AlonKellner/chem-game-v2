// The curriculum's step E (GAME_SPEC §13.7, Stage 85; docs/CURRICULUM.md): salts, precipitates and complexes, each
// experiment played through the screen with its own gestures, strict and free, at once and at a person's pace, and its
// report checked: what a student works out from the readings, held to the truth.
#include "lab/notebook.hpp"
#include "lab/ui.hpp"

#include <doctest/doctest.h>

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

