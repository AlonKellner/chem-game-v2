// What a frame costs (Stage 33; GAME_SPEC §18: O13), counted where no GPU runs: the rays a pixel follows and
// what each tries, over the play a host times (data/plays/cost.json, make pass-cost).
#include "lab/optics_scene.hpp"
#include "lab/play.hpp"
#include "lab/prism_demo.hpp"
#include "lab/ui.hpp"

#include <doctest/doctest.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <vector>
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

} // namespace

TEST_CASE("cost: a ray's work over the play's bench, at each of its marks (counts, printed where LAB_RAY_STATS is set) (Stage 33)") {
	if (std::getenv("LAB_RAY_STATS") == nullptr) {
		return;
	}
	std::string why;
	std::unique_ptr<lab::Ui> ui = lab::Ui::open(read("data/benches/lab.json"), why);
	REQUIRE_MESSAGE(ui != nullptr, why);
	lab::Play play;
	REQUIRE_MESSAGE(lab::Play::read(read("data/plays/cost.json"), play, why), why);
	std::string mark;
	int in_mark = 0;
	for (int i = 0; i < 6000 && play.next(*ui); ++i) {
		ui->frame(1.0 / 120.0);
		in_mark = play.mark() == mark ? in_mark + 1 : 0;
		mark = play.mark();
		// one frame of each mark, well into it
		if (mark.empty() || in_mark != (mark == "pour" ? 330 : 50)) {
			continue;
		}
		const lab::OpticsScene &scene = ui->optics();
		int64_t work[9];
		lab::optics_work(work, true);
		int64_t pixels = 0;
		for (int y = 2; y < 1080; y += 4) {
			for (int x = 2; x < 1920; x += 4) {
				pixels += lab::optics_rays_for(scene, x, y) > 0 ? 1 : 0;
			}
		}
		lab::optics_work(work, true);
		const double rays = static_cast<double>(std::max<int64_t>(work[0], 1));
		std::printf("%-8s %lld pixels of 16 read by rays, %.1f rays each; a ray tries %.2f bodies, searches %.2f meetings, tries %.1f quadrics and %.1f tori, solves %.2f, makes %.2f roots exact; past eight meetings %.2f%%, to the last allowed %.2f%%\n",
				mark.c_str(), static_cast<long long>(pixels), rays / static_cast<double>(std::max<int64_t>(pixels, 1)), static_cast<double>(work[1]) / rays, static_cast<double>(work[2]) / rays, static_cast<double>(work[3]) / rays,
				static_cast<double>(work[4]) / rays, static_cast<double>(work[5]) / rays, static_cast<double>(work[6]) / rays, 100.0 * static_cast<double>(work[7]) / rays, 100.0 * static_cast<double>(work[8]) / rays);
	}
}

// The prism's pixels that stop before a ray has found the slit (TDD 35): where each of a pixel's rays goes, by
// wavelength (printed where LAB_RAY_STATS is prism).
TEST_CASE("cost: where a prism pixel's rays go by wavelength (printed where LAB_RAY_STATS is prism) (Stage 35)") {
	if (std::getenv("LAB_RAY_STATS") == nullptr || std::string(std::getenv("LAB_RAY_STATS")) != "prism") {
		return;
	}
	lab::PrismDemo demo;
	const lab::OpticsScene scene = demo.optics();
	for (const int x : { 946, 932, 960 }) {
		std::printf("pixel (%d, 540):\n", x);
		for (const double nm : { 380.0, 400.0, 420.0, 439.4, 460.0, 480.0, 500.0, 520.0, 541.2, 580.0, 620.0, 680.0, 780.0 }) {
			std::printf("  %5.1f nm:", nm);
			for (const double off : { 0.0, 0.5, 1.0 }) {
				const lab::OpticsRay r = lab::optics_ray(scene, x + off, 540.5, nm);
				std::printf("  at +%.1f lands %d (%.1f, %.1f) route %08x", off, r.lands ? 1 : 0, r.x, r.y, r.route[0]);
			}
			std::printf("\n");
		}
		double truth[3];
		lab::optics_area_truth(scene, x, 540, 16, truth);
		std::printf("  the truth of its area: %.4f %.4f %.4f\n", truth[0], truth[1], truth[2]);
	}
}

namespace {

// what the frames of one mark of the play cost: the pixels that read, and the rays they followed
struct Cost {
	int frames = 0;
	int64_t read = 0, rays = 0, most = 0;
};

// The play, its frames given to the renderer's own frames over a sample of the screen (every `step`-th pixel
// across and down, every `every`-th frame of the play): what each mark's frames cost.
std::vector<std::pair<std::string, Cost>> play_cost(int step, int every) {
	std::string why;
	std::unique_ptr<lab::Ui> ui = lab::Ui::open(read("data/benches/lab.json"), why);
	REQUIRE_MESSAGE(ui != nullptr, why);
	lab::Play play;
	REQUIRE_MESSAGE(lab::Play::read(read("data/plays/cost.json"), play, why), why);
	lab::OpticsFrames frames(step / 2, step / 2, 1920 / step, 1080 / step, 16.0, step);
	std::vector<std::pair<std::string, Cost>> out;
	for (int i = 0; i < 6000 && play.next(*ui); ++i) {
		ui->frame(1.0 / 120.0);
		if (i % every != 0) {
			continue;
		}
		frames.frame(ui->optics());
		const std::string mark = play.mark().empty() ? "(between)" : play.mark();
		if (out.empty() || out.back().first != mark) {
			out.emplace_back(mark, Cost{});
		}
		Cost &c = out.back().second;
		if (every == 1 && std::getenv("LAB_RAY_STATS") != nullptr && std::string(std::getenv("LAB_RAY_STATS")) == "series") {
			// every frame of the mark, in order: the pixels of the screen it reads, and the rays it follows (thousands)
			// (and, in thousands over the screen: the points asked for, the points spent, the beams traced)
			std::printf("%s%lld/%lld/%.0f/%.0f/%.0f", c.frames % 10 == 0 ? (c.frames == 0 ? (mark + ":\n  ").c_str() : "\n  ") : " ", static_cast<long long>(frames.read() * step * step / 1000), static_cast<long long>(frames.rays() * step * step / 1000),
					frames.asked() * step * step / 1000.0, frames.points() * step * step / 1000.0, frames.beams_traced() * step * step / 1000.0);
		}
		++c.frames;
		c.read += frames.read();
		c.rays += frames.rays();
		c.most = std::max(c.most, frames.most_rays());
	}
	return out;
}

} // namespace

TEST_CASE("cost: the rays a frame follows through the play, by its marks (printed where LAB_RAY_STATS is set) (Stage 33)") {
	if (std::getenv("LAB_RAY_STATS") == nullptr) {
		return;
	}
	const int step = 8;
	const bool series = std::string(std::getenv("LAB_RAY_STATS")) == "series";
	for (const auto &[mark, c] : play_cost(step, series ? 1 : 8)) {
		std::printf(series && c.frames > 0 ? "\n" : "");
		const double frames = static_cast<double>(std::max(c.frames, 1));
		std::printf("%-10s %3d frames: a frame reads %8.0f pixels of the screen and follows %10.0f rays (%.1f a pixel read; the most by one %lld)\n", mark.c_str(), c.frames,
				static_cast<double>(c.read) * step * step / frames, static_cast<double>(c.rays) * step * step / frames, static_cast<double>(c.rays) / static_cast<double>(std::max<int64_t>(c.read, 1)), static_cast<long long>(c.most));
	}
}

TEST_CASE("cost: through the play no pixel follows more rays in a frame than O13 allows, a frame's rays stay under what each mark's did, and a bench left alone comes to follow none (Stage 33)") {
	const int step = 8;
	std::string why;
	std::unique_ptr<lab::Ui> ui = lab::Ui::open(read("data/benches/lab.json"), why);
	REQUIRE_MESSAGE(ui != nullptr, why);
	lab::Play play;
	REQUIRE_MESSAGE(lab::Play::read(read("data/plays/cost.json"), play, why), why);
	lab::OpticsFrames frames(step / 2, step / 2, 1920 / step, 1080 / step, 16.0, step);
	std::map<std::string, Cost> marks;
	bool fired = false;
	lab::OpticsScene settled;
	for (int i = 0; i < 6000 && play.next(*ui); ++i) {
		ui->frame(1.0 / 120.0);
		if (i % 8 != 0) {
			continue;
		}
		frames.frame(ui->optics());
		CHECK_MESSAGE(lab::check_O13(frames) == "", "frame " << i << " (" << play.mark() << ")");
		if (!fired && frames.most_rays() > 4) {
			fired = lab::check_O13(frames, 4) != ""; // (it fires on a bound a beam alone passes)
		}
		if (play.mark() == "settled" && settled.table.empty()) {
			settled = ui->optics();
		}
		Cost &c = marks[play.mark()];
		++c.frames;
		c.read += frames.read();
		c.rays += frames.rays();
	}
	CHECK(fired);
	// a frame's rays, over the whole screen: what the marks' frames followed when this was written, and a third more
	// (354,000, 634,000 and 253,000; Stage 32's, before the pass was made fast: 1.2, 2.35 and 2.0 million)
	const std::map<std::string, double> most{ { "carry", 480000.0 }, { "pour", 850000.0 }, { "stir", 340000.0 } };
	for (const auto &[mark, limit] : most) {
		const Cost &c = marks[mark];
		REQUIRE(c.frames > 0);
		const double a_frame = static_cast<double>(c.rays) * step * step / c.frames;
		INFO(mark << ": " << a_frame << " rays a frame");
		CHECK(a_frame < limit);
	}
	// left alone, the bench settles: a frame then follows no ray at all
	REQUIRE(!settled.table.empty());
	lab::OpticsFrames still(step / 2, step / 2, 1920 / step, 1080 / step, 16.0, step);
	int until = -1;
	for (int f = 0; f < 600 && until < 0; ++f) {
		still.frame(settled);
		if (f > 0 && still.rays() == 0 && still.read() == 0) {
			until = f;
		}
	}
	INFO("frames until no pixel reads: " << until);
	CHECK(until > 0);
}
