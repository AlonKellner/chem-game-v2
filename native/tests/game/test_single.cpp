// The pass's rays in single precision (Stage 33; GAME_SPEC §18: O15): the tracer of a shape's surfaces, the
// same text the GPU runs, compiled with a float's arithmetic and the GPU's tolerances, held ray by ray to
// itself in double, over the vessels of the play a host times: standing, turned and pouring, stirred.
#include "lab/optics_scene.hpp"
#include "lab/play.hpp"
#include "lab/ui.hpp"

#include <doctest/doctest.h>

#include <fstream>
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

// a tracer whose glass is another glass: every ray a little off (what O15 rules out)
lab::OpticsPassage another_glass(const lab::OpticsScene &scene, int body, const double from[3], const double heading[3], double n_glass, double n_core) {
	return lab::optics_passage(scene, body, from, heading, n_glass + 0.01, n_core, true);
}

} // namespace

TEST_CASE("single: the pass's rays in single precision are its rays in double, over the play's vessels; O15 fires on another glass (Stage 33)") {
	std::string why;
	std::unique_ptr<lab::Ui> ui = lab::Ui::open(read("data/benches/lab.json"), why);
	REQUIRE_MESSAGE(ui != nullptr, why);
	lab::Play play;
	REQUIRE_MESSAGE(lab::Play::read(read("data/plays/cost.json"), play, why), why);
	std::string mark;
	int in_mark = 0, held = 0;
	for (int i = 0; i < 6000 && play.next(*ui); ++i) {
		ui->frame(1.0 / 120.0);
		in_mark = play.mark() == mark ? in_mark + 1 : 0;
		mark = play.mark();
		// a frame of the bench settled, one of the bottle turned and pouring, one of the flask stirred
		if (in_mark != (mark == "pour" ? 330 : 50) || (mark != "settled" && mark != "pour" && mark != "stir")) {
			continue;
		}
		INFO(mark);
		CHECK(lab::check_O15(ui->optics()) == "");
		if (mark == "pour") {
			CHECK(lab::check_O15(ui->optics(), another_glass) != "");
		}
		++held;
	}
	CHECK(held == 3);
}
