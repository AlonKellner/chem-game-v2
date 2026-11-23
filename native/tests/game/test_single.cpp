// The pass's rays in single precision (Stage 33; GAME_SPEC §18: O15): the tracer of a shape's surfaces, the
// same text the GPU runs, compiled with a float's arithmetic and the GPU's tolerances, held ray by ray to
// itself in double, over the vessels of the play a host times: standing, turned and pouring, stirred.
#include "lab/colour.hpp"
#include "lab/optics_scene.hpp"
#include "lab/play.hpp"
#include "lab/ui.hpp"

#include <doctest/doctest.h>

#include "../support/sparse.hpp"

#include <algorithm>
#include <cstdlib>
#include <memory>
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

// how far apart two colours (linear sRGB) are to the eye (8 is visible)
double eye(const double a[3], const double b[3]) {
	const auto seen = [](const double c[3]) {
		return lab::shown(chem::optics::Xyz{ 0.4124564 * c[0] + 0.3575761 * c[1] + 0.1804375 * c[2], 0.2126729 * c[0] + 0.7151522 * c[1] + 0.0721750 * c[2],
				0.0193339 * c[0] + 0.1191920 * c[1] + 0.9503041 * c[2] });
	};
	return lab::eye_difference(seen(a), seen(b));
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

// Stage 35 (GAME_SPEC §18: O16): the whole per-pixel source in single precision (OpticsFrames with `single`: every
// sum a float's, the GPU's tolerances, the scene's numbers as the GPU is sent them), frame by frame beside itself
// in double, over a sample of the play's bench, until no pixel of either asks for more. What a float's
// arithmetic alone does to a pixel's settled colour, where no GPU runs.
// As a measurement (LAB_SINGLE_STEP=1: every pixel; LAB_SINGLE_PRINT=1: each pixel that parts) it also counts the
// pixels of each that are visibly off the one reading that reads every band (optics_truth): what the GPU's
// settled picture shows a handful of (O4, `make screenshots`) is there in double as in single precision
// (docs/TDD_35_ONE.md).
TEST_CASE("O16: the whole per-pixel source in single precision settles on what it settles on in double, over the play's bench; it fires on another picture (Stage 35)") {
	std::string why;
	std::unique_ptr<lab::Ui> ui = lab::Ui::open(read("data/benches/lab.json"), why);
	REQUIRE_MESSAGE(ui != nullptr, why);
	lab::Play play;
	REQUIRE_MESSAGE(lab::Play::read(read("data/plays/cost.json"), play, why), why);
	// (LAB_SINGLE_STEP: every pixel, or every other, for a measurement: a pixel off the truth is one in ten thousand)
	const char *asked = std::getenv("LAB_SINGLE_STEP");
	const bool measuring = asked != nullptr;
	const int step = measuring ? std::max(std::atoi(asked), 1) : 6 * sparse(), w = 1920 / step, h = 1080 / step;
	std::string mark;
	int in_mark = 0, held = 0;
	// the settled bench's frames in double, kept: the pouring bench's in single precision are not these (the self-test)
	std::unique_ptr<lab::OpticsFrames> settled;
	for (int i = 0; i < 6000 && play.next(*ui); ++i) {
		ui->frame(1.0 / 120.0);
		in_mark = play.mark() == mark ? in_mark + 1 : 0;
		mark = play.mark();
		// the bench settled, and the bottle held turned over the flask (two layers, where single precision spreads most)
		if (in_mark != (mark == "pour" ? 330 : 50) || (mark != "settled" && mark != "pour")) {
			continue;
		}
		INFO(mark);
		const lab::OpticsScene &scene = ui->optics();
		auto exact = std::make_unique<lab::OpticsFrames>(0, 0, w, h, 16.0, step);
		lab::OpticsFrames single(0, 0, w, h, 16.0, step, true);
		// until no pixel asks for more, in either
		int frames = 0;
		for (; frames < 600 && (frames < 8 || exact->asked() > 0.0 || single.asked() > 0.0); ++frames) {
			exact->frame(scene);
			single.frame(scene);
		}
		double stats[4] = {};
		CHECK(lab::check_O16(*exact, single, scene, 0, 0, w * step, h * step, step, stats) == "");
		CHECK(stats[0] > (sparse() > 1 ? 50.0 : 500.0));
		int exact_off = 0, single_off = 0;
		if (measuring) {
			for (int row = 0; row < h; ++row) {
				for (int col = 0; col < w; ++col) {
					const int x = col * step, y = row * step;
					if (lab::optics_cover(scene, x, y) < 0.02) {
						continue;
					}
					double a[3], b[3], truth[3];
					exact->colour(scene, x, y, a);
					single.colour(scene, x, y, b);
					// each against the one reading that reads every band and follows every ray it needs
					lab::optics_truth(scene, x, y, truth);
					exact_off += eye(a, truth) > 8.0 ? 1 : 0;
					single_off += eye(b, truth) > 8.0 ? 1 : 0;
					if (eye(a, b) > 8.0 && std::getenv("LAB_SINGLE_PRINT") != nullptr) {
						MESSAGE("(" << x << ", " << y << "): " << eye(a, b) << " apart; in double " << a[0] << " " << a[1] << " " << a[2] << ", in single " << b[0] << " " << b[1] << " " << b[2] << ", the one reading " << truth[0] << " " << truth[1] << " "
									<< truth[2]);
					}
				}
			}
			MESSAGE(mark << ": visibly off the one reading: " << exact_off << " in double, " << single_off << " in single precision");
		}
		MESSAGE(mark << ": " << stats[0] << " pixels of bodies, " << stats[1] << " visibly apart in single precision after " << frames << " frames (none asking), the worst " << stats[2] << ", the mean " << stats[3]);
		if (mark == "settled") {
			settled = std::move(exact);
		} else {
			// it fires: the bench as it was before the bottle was lifted is not the bench with the bottle held over the flask
			REQUIRE(settled != nullptr);
			CHECK(lab::check_O16(*settled, single, scene, 0, 0, w * step, h * step, step) != "");
		}
		++held;
	}
	CHECK(held == 2);
}
