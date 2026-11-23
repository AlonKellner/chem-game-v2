// What a frame costs (Stage 33; GAME_SPEC §18: O13), counted where no GPU runs: the rays a pixel follows and
// what each tries, over the play a host times (data/plays/cost.json, make pass-cost).
#include "lab/colour.hpp"
#include "lab/optics_scene.hpp"
#include "lab/optics_trace.hpp"
#include "lab/play.hpp"
#include "lab/ui.hpp"

#include <doctest/doctest.h>

#include <array>
#include <chrono>
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

struct Traced {
	Traced() { lab::optics_reading(5); }
	~Traced() { lab::optics_reading(0); }
};

} // namespace

TEST_CASE("cost: a ray's work over the play's bench, at each of its marks (counts, printed where LAB_RAY_STATS is set) (Stage 33)") {
	if (std::getenv("LAB_RAY_STATS") == nullptr) {
		return;
	}
	const Traced traced;
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

// How many meetings a ray is allowed in a shape (TRACE_MOST), measured: how the passages end, by their meetings,
// and what the truth's picture becomes with fewer allowed, on the eye metric.
TEST_CASE("cost: how passages through a shape end, and the truth with fewer meetings allowed (printed where LAB_RAY_STATS is most) (Stage 33)") {
	if (std::getenv("LAB_RAY_STATS") == nullptr || std::string(std::getenv("LAB_RAY_STATS")) != "most") {
		return;
	}
	const Traced traced;
	std::string why;
	std::unique_ptr<lab::Ui> ui = lab::Ui::open(read("data/benches/lab.json"), why);
	REQUIRE_MESSAGE(ui != nullptr, why);
	lab::Play play;
	REQUIRE_MESSAGE(lab::Play::read(read("data/plays/cost.json"), play, why), why);
	const auto eye = [](const double a[3], const double b[3]) {
		const auto seen = [](const double c[3]) {
			return lab::shown(chem::optics::Xyz{ 0.4124564 * c[0] + 0.3575761 * c[1] + 0.1804375 * c[2], 0.2126729 * c[0] + 0.7151522 * c[1] + 0.0721750 * c[2],
					0.0193339 * c[0] + 0.1191920 * c[1] + 0.9503041 * c[2] });
		};
		return lab::eye_difference(seen(a), seen(b));
	};
	std::string mark;
	int in_mark = 0;
	for (int i = 0; i < 6000 && play.next(*ui); ++i) {
		ui->frame(1.0 / 120.0);
		in_mark = play.mark() == mark ? in_mark + 1 : 0;
		mark = play.mark();
		if (mark.empty() || mark == "settled" || in_mark != (mark == "pour" ? 330 : (mark == "stir" ? 300 : 50))) {
			continue;
		}
		const lab::OpticsScene &scene = ui->optics();
		int64_t count[65], inside[65];
		double light[65];
		lab::optics_ended(count, inside, light, true);
		std::vector<std::pair<int, int>> read;
		for (int y = 1; y < 1080; y += 4) {
			for (int x = 1; x < 1920; x += 4) {
				if (lab::optics_rays_for(scene, x, y) > 0) {
					read.emplace_back(x, y);
				}
			}
		}
		lab::optics_ended(count, inside, light, true);
		int64_t all = 0;
		double all_light = 0.0;
		for (int m = 0; m < 65; ++m) {
			all += count[m];
			all_light += light[m];
		}
		std::printf("%s: %lld passages through a shape; by meetings: share of passages, of them still inside, share of the light carried out\n", mark.c_str(), static_cast<long long>(all));
		for (int m = 0; m < 65; ++m) {
			if (count[m] > 0) {
				std::printf("  %2d: %6.2f%% %6.2f%% %6.2f%%\n", m, 100.0 * count[m] / all, 100.0 * inside[m] / count[m], 100.0 * light[m] / all_light);
			}
		}
		std::vector<std::array<double, 3>> full(read.size());
		lab::optics_meetings_most(128, 128);
		for (size_t k = 0; k < read.size(); ++k) {
			lab::optics_area_truth(scene, read[k].first, read[k].second, 2, full[k].data());
		}
		// (meetings allowed, times turned back allowed)
		const std::pair<int, int> allowed[] = { { 32, 8 }, { 16, 8 }, { 16, 4 }, { 16, 2 }, { 12, 4 }, { 8, 4 } };
		for (const auto &[most, turns] : allowed) {
			lab::optics_meetings_most(most, turns);
			int visible = 0;
			double worst = 0.0, sum = 0.0;
			for (size_t k = 0; k < read.size(); ++k) {
				double rgb[3];
				lab::optics_area_truth(scene, read[k].first, read[k].second, 2, rgb);
				const double e = eye(full[k].data(), rgb);
				visible += e > lab::EYE_VISIBLE ? 1 : 0;
				worst = std::max(worst, e);
				sum += e;
			}
			std::printf("  %3d meetings and %d turns allowed: of %zu pixels read by rays, %d differ visibly from the truth of 128 of each (%.2f%%); worst %.1f, mean %.3f\n", most, turns, read.size(), visible, 100.0 * visible / std::max<size_t>(read.size(), 1), worst, sum / std::max<size_t>(read.size(), 1));
		}
		lab::optics_meetings_most(lab::trace::MEETINGS_MOST, lab::trace::TURNS_MOST);
	}
}

// Where a carried vessel comes over a standing one: how many rays a pixel follows to be read once, every pixel
// of the screen (printed where LAB_RAY_STATS is over).
TEST_CASE("cost: the rays of each pixel where a carried vessel comes over a standing one (printed where LAB_RAY_STATS is over) (Stage 33)") {
	if (std::getenv("LAB_RAY_STATS") == nullptr || std::string(std::getenv("LAB_RAY_STATS")) != "over") {
		return;
	}
	const Traced traced;
	std::string why;
	std::unique_ptr<lab::Ui> ui = lab::Ui::open(read("data/benches/lab.json"), why);
	REQUIRE_MESSAGE(ui != nullptr, why);
	lab::Play play;
	REQUIRE_MESSAGE(lab::Play::read(read("data/plays/cost.json"), play, why), why);
	int in_mark = 0;
	std::string mark;
	for (int i = 0; i < 6000 && play.next(*ui); ++i) {
		ui->frame(1.0 / 120.0);
		in_mark = play.mark() == mark ? in_mark + 1 : 0;
		mark = play.mark();
		if (mark != "carry" || (in_mark != 30 && in_mark != 80)) {
			continue;
		}
		const lab::OpticsScene &scene = ui->optics();
		int64_t pixels = 0, rays = 0, over[4] = {};
		for (int y = 400; y < 760; ++y) {
			for (int x = 550; x < 800; ++x) {
				const int64_t r = lab::optics_rays_for(scene, x, y);
				pixels += r > 0 ? 1 : 0;
				rays += r;
				over[0] += r > 30 ? 1 : 0;
				over[1] += r > 100 ? 1 : 0;
				over[2] += r > 300 ? 1 : 0;
				over[3] += r > 1000 ? 1 : 0;
			}
		}
		// how far a pixel's view sweeps along the wall from one end of the spectrum to the other (pixels)
		int64_t sweeps[6] = {};
		for (int y = 400; y < 760; y += 2) {
			for (int x = 550; x < 800; x += 2) {
				if (lab::optics_rays_for(scene, x, y) > 0) {
					const double sweep = std::fabs(lab::optics_bend(scene, x, y, 780.0).x - lab::optics_bend(scene, x, y, 380.0).x);
					++sweeps[sweep < 0.02 ? 0 : (sweep < 0.05 ? 1 : (sweep < 0.2 ? 2 : (sweep < 1.0 ? 3 : (sweep < 5.0 ? 4 : 5))))];
				}
			}
		}
		int64_t even[3];
		lab::optics_even(even, true);
		for (int y = 400; y < 760; y += 2) {
			for (int x = 550; x < 800; x += 2) {
				lab::optics_rays_for(scene, x, y);
			}
		}
		lab::optics_even(even, true);
		std::printf("  beams read over a wall that is one under them %lld; not: %lld whose rays part, %lld others\n", static_cast<long long>(even[0]), static_cast<long long>(even[1]), static_cast<long long>(even[2]));
		std::printf("  sweeps across the spectrum: under 0.02 px %lld, to 0.05 %lld, to 0.2 %lld, to 1 %lld, to 5 %lld, more %lld\n", static_cast<long long>(sweeps[0]), static_cast<long long>(sweeps[1]),
				static_cast<long long>(sweeps[2]), static_cast<long long>(sweeps[3]), static_cast<long long>(sweeps[4]), static_cast<long long>(sweeps[5]));
		std::printf("carry frame %d: %lld pixels read by rays, %lld rays; pixels over 30 rays %lld, over 100 %lld, over 300 %lld, over 1000 %lld\n", in_mark, static_cast<long long>(pixels), static_cast<long long>(rays),
				static_cast<long long>(over[0]), static_cast<long long>(over[1]), static_cast<long long>(over[2]), static_cast<long long>(over[3]));
	}
}

// The two pixels the GPU's settled picture has visibly off the truth (O4, TDD 33): what the reference, in double,
// settles to there, against the truth (printed where LAB_RAY_STATS is heel).
TEST_CASE("cost: the reference's settled pixels at the demo flask's heel against the truth (printed where LAB_RAY_STATS is heel) (Stage 33)") {
	if (std::getenv("LAB_RAY_STATS") == nullptr || std::string(std::getenv("LAB_RAY_STATS")) != "heel") {
		return;
	}
	const Traced traced;
	std::string why;
	std::unique_ptr<lab::Ui> ui = lab::Ui::open(read("data/benches/demo.json"), why);
	REQUIRE_MESSAGE(ui != nullptr, why);
	ui->frame(1.0 / 60.0);
	const lab::OpticsScene scene = ui->optics();
	lab::OpticsFrames frames(397, 680, 9, 8);
	for (int f = 0; f < 400; ++f) {
		frames.frame(scene);
	}
	for (int y = 680; y < 688; ++y) {
		for (int x = 397; x < 406; ++x) {
			double shown[3], truth[3];
			frames.colour(scene, x, y, shown);
			lab::optics_area_truth(scene, x, y, 16, truth);
			const auto seen = [](const double c[3]) {
				return lab::shown(chem::optics::Xyz{ 0.4124564 * c[0] + 0.3575761 * c[1] + 0.1804375 * c[2], 0.2126729 * c[0] + 0.7151522 * c[1] + 0.0721750 * c[2], 0.0193339 * c[0] + 0.1191920 * c[1] + 0.9503041 * c[2] });
			};
			double once[3];
			lab::optics_truth(scene, x, y, once);
			std::printf(" %5.1f/%-5.1f", lab::eye_difference(seen(shown), seen(truth)), lab::eye_difference(seen(once), seen(truth)));
		}
		std::printf("   (row %d, x 397 to 405: the reference settled against the truth / its one reading against the truth, on the eye metric)\n", y);
	}
}

// How long a vessel held over another takes to settle once the hand is still: the frames until no pixel of the
// sample is visibly off what it comes to, and until none reads anything (printed where LAB_RAY_STATS is held).
TEST_CASE("cost: the frames a held vessel takes to settle once it is still (printed where LAB_RAY_STATS is held) (Stage 33)") {
	if (std::getenv("LAB_RAY_STATS") == nullptr || std::string(std::getenv("LAB_RAY_STATS")) != "held") {
		return;
	}
	const Traced traced;
	std::string why;
	std::unique_ptr<lab::Ui> ui = lab::Ui::open(read("data/benches/lab.json"), why);
	REQUIRE_MESSAGE(ui != nullptr, why);
	lab::Play play;
	REQUIRE_MESSAGE(lab::Play::read(read("data/plays/cost.json"), play, why), why);
	const int step = 4;
	lab::OpticsFrames moving(step / 2, step / 2, 1920 / step, 1080 / step, 16.0, step), last(step / 2, step / 2, 1920 / step, 1080 / step, 16.0, step);
	bool carried = false;
	for (int i = 0; i < 6000 && play.next(*ui); ++i) {
		ui->frame(1.0 / 120.0);
		if (play.mark() == "carry") {
			carried = true;
			moving.frame(ui->optics()); // (as the pass has it: every frame of the carry)
			last.frame(ui->optics());
		} else if (carried) {
			break; // the bottle is held over the flask
		}
	}
	REQUIRE(carried);
	const lab::OpticsScene scene = ui->optics();
	const auto seen = [](const double c[3]) {
		return lab::shown(chem::optics::Xyz{ 0.4124564 * c[0] + 0.3575761 * c[1] + 0.1804375 * c[2], 0.2126729 * c[0] + 0.7151522 * c[1] + 0.0721750 * c[2], 0.0193339 * c[0] + 0.1191920 * c[1] + 0.9503041 * c[2] });
	};
	// what it comes to
	for (int f = 0; f < 1500; ++f) {
		last.frame(scene);
	}
	int visible_until = 0, reads_until = -1;
	for (int f = 0; f < 1500; ++f) {
		moving.frame(scene);
		int off = 0;
		for (int y = step / 2; y < 1080; y += step) {
			for (int x = step / 2; x < 1920; x += step) {
				double a[3], b[3];
				moving.colour(scene, x, y, a);
				last.colour(scene, x, y, b);
				off += lab::eye_difference(seen(a), seen(b)) > lab::EYE_VISIBLE ? 1 : 0;
			}
		}
		if (f == 0 || f == 5 || f == 15 || f == 30 || f == 60 || f == 120 || f == 240) {
			std::printf("frame %d: %d pixels of the sample visibly off what they come to, %lld reading\n", f, off, static_cast<long long>(moving.read()));
		}
		if (off > 0) {
			visible_until = f + 1;
		}
		if (moving.read() == 0 && reads_until < 0) {
			reads_until = f;
		}
	}
	std::printf("visibly settled after %d frames; nothing read after %d\n", visible_until, reads_until);
}

namespace {
double poured(lab::Ui &ui) {
	const lab::LabView view = ui.lab().view();
	for (const lab::ItemView &i : view.items) {
		if (i.id == "acid") {
			return i.pours_ml_s;
		}
	}
	return 0.0;
}
double turned(lab::Ui &ui) {
	const lab::LabView view = ui.lab().view();
	for (const lab::ItemView &i : view.items) {
		if (i.id == "acid") {
			return i.turn;
		}
	}
	return 0.0;
}
} // namespace

// What a frame of the screen costs the CPU while a bottle pours by hand, at the clock's own speed and at 600
// times it (printed where LAB_RAY_STATS is speed).
TEST_CASE("cost: the CPU's part of a frame while pouring by hand, at 1x and at 600x (printed where LAB_RAY_STATS is speed) (Stage 33)") {
	if (std::getenv("LAB_RAY_STATS") == nullptr || std::string(std::getenv("LAB_RAY_STATS")) != "speed") {
		return;
	}
	const Traced traced;
	for (const bool fast : { false, true }) {
		std::string why;
		std::unique_ptr<lab::Ui> ui = lab::Ui::open(read("data/benches/lab.json"), why);
		REQUIRE_MESSAGE(ui != nullptr, why);
		lab::Play play;
		REQUIRE_MESSAGE(lab::Play::read(read("data/plays/cost.json"), play, why), why);
		std::string mark;
		int in_mark = 0;
		double frame_ms = 0.0, scene_ms = 0.0;
		int timed = 0;
		for (int i = 0; i < 6000 && play.next(*ui); ++i) {
			in_mark = play.mark() == mark ? in_mark + 1 : 0;
			mark = play.mark();
			if (fast && mark == "settled" && in_mark == 5) {
				ui->pointer(670.0f, 32.0f, true); // the 600x button (the play's own pointer is back at the next frame)
				ui->pointer(670.0f, 32.0f, false);
				play.next(*ui);
			}
			const auto t0 = std::chrono::steady_clock::now();
			ui->frame(1.0 / 120.0);
			const auto t1 = std::chrono::steady_clock::now();
			ui->optics();
			const auto t2 = std::chrono::steady_clock::now();
			if ((mark == "pour" && in_mark % 60 == 0) || ((mark == "settled" || mark == "carry" || mark == "rest" || mark == "stir") && in_mark == 40)) {
				std::printf("  (%s)", mark.c_str());
				std::printf("  pour frame %d: the game's frame %.2f ms, the scene %.2f ms; the bottle pours %.2f mL/s, turned %.2f\n", in_mark, std::chrono::duration<double, std::milli>(t1 - t0).count(), std::chrono::duration<double, std::milli>(t2 - t1).count(), poured(*ui), turned(*ui));
			}
			if (mark == "pour") {
				frame_ms += std::chrono::duration<double, std::milli>(t1 - t0).count();
				scene_ms += std::chrono::duration<double, std::milli>(t2 - t1).count();
				++timed;
			}
		}
		std::printf("%s: over %d frames of the pour, the game's frame %.2f ms, the scene composed %.2f ms\n", fast ? "600x" : "1x", timed, frame_ms / std::max(timed, 1), scene_ms / std::max(timed, 1));
	}
}

// The pixels of the demo flask's heel the GPU has visibly off the truth (O4, TDD 33): the pass's tracer there in
// single precision against itself in double, ray by ray across the pixels (printed where LAB_RAY_STATS is heel32).
TEST_CASE("cost: the tracer in single precision against double at the demo flask's heel (printed where LAB_RAY_STATS is heel32) (Stage 33)") {
	if (std::getenv("LAB_RAY_STATS") == nullptr || std::string(std::getenv("LAB_RAY_STATS")) != "heel32") {
		return;
	}
	const Traced traced;
	std::string why;
	std::unique_ptr<lab::Ui> ui = lab::Ui::open(read("data/benches/demo.json"), why);
	REQUIRE_MESSAGE(ui != nullptr, why);
	ui->frame(1.0 / 60.0);
	const lab::OpticsScene scene = ui->optics();
	namespace L = lab::optics_layout;
	// the body whose rectangle holds (401, 684) and that bends
	int body = -1;
	for (int b = 0; b < static_cast<int>(scene.table[0]); ++b) {
		const float *rect = &scene.table[static_cast<size_t>(L::HEADER_VECS + b * L::BODY_VECS) * 4];
		if (scene.shapes[static_cast<size_t>(b) * L::SHAPE_BLOCK * 4] >= 0.0f && 401.0f >= rect[0] && 401.0f < rect[0] + rect[2] && 684.0f >= rect[1] && 684.0f < rect[1] + rect[3]) {
			body = b;
		}
	}
	REQUIRE(body >= 0);
	const float *rect = &scene.shapes[static_cast<size_t>(body) * L::SHAPE_BLOCK * 4 + 8];
	const double cm = scene.table[static_cast<size_t>(L::HEADER_VECS + body * L::BODY_VECS + L::FORM) * 4 + 1], axis = rect[0] + 0.5 * rect[2], base = rect[1] + rect[3];
	std::printf("body %d: its rectangle upright %.1f, %.1f, %.1f by %.1f; %.5f cm a pixel\n", body, rect[0], rect[1], rect[2], rect[3], cm);
	const double u[3] = { 0.0, 0.0, 1.0 };
	for (int y = 682; y <= 686; ++y) {
		for (int x = 399; x <= 403; ++x) {
			int rays = 0, astray = 0;
			double worst_at = 0.0, worst_toward = 0.0;
			for (int j = 0; j < 8; ++j) {
				for (int i = 0; i < 8; ++i) {
					const double from[3] = { (x + (i + 0.5) / 8.0 - axis) * cm, (base - (y + (j + 0.5) / 8.0)) * cm, -40.0 };
					const lab::OpticsPassage d = lab::optics_passage(scene, body, from, u, 1.47, 1.333, false), s = lab::optics_passage(scene, body, from, u, 1.47, 1.333, true);
					++rays;
					if (d.route != s.route || d.left != s.left) {
						++astray;
					} else {
						worst_at = std::max(worst_at, std::hypot(d.at[0] - s.at[0], d.at[1] - s.at[1], d.at[2] - s.at[2]));
						worst_toward = std::max(worst_toward, std::hypot(d.toward[0] - s.toward[0], d.toward[1] - s.toward[1], d.toward[2] - s.toward[2]));
					}
				}
			}
			std::printf("  (%d, %d): of %d rays %d go another way in single precision; of the others, apart by %.2g cm and %.2g in heading at most\n", x, y, rays, astray, worst_at, worst_toward);
		}
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
	const Traced traced;
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
	const Traced traced;
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
