// chem_tool study frames: the renderer's false positives and false negatives (GAME_SPEC §12.2), on its own
// frames (the reference, lab::OpticsFrames) against the truth, frame by frame, with the band cuts at the
// jumps of a pixel's light (§12.2) and without them, in three scenarios:
//   prism     the prism demo: opened, turned, moved, turned back
//   bench     a bench of glass vessels before the grid: one dragged, one jumping, one poured into, one
//             reacting slowly, one still
//   palettes  16 glasses of random hard spectra (the twelve families, narrow lines among them),
//             settled, then all changed at once
// A false positive is a point spent on a pixel whose colour did not change and was already right (the eye
// metric 4 or less); a false negative is a pixel shown visibly wrong (above 8), persistent if for 6 frames
// running.
#include "lab/colour.hpp"
#include "lab/optics_scene.hpp"
#include "lab/prism_demo.hpp"

#include "../../tests/game/random_spectra.hpp"

#include "chem/builtin_ids.hpp"
#include "chem/optics.hpp"

#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <string>
#include <vector>

namespace {

using lab::OpticsScene;

lab::CieLab shown_of(const double rgb[3]) {
	return lab::shown(chem::optics::Xyz{ 0.4124564 * rgb[0] + 0.3575761 * rgb[1] + 0.1804375 * rgb[2], 0.2126729 * rgb[0] + 0.7151522 * rgb[1] + 0.0721750 * rgb[2],
			0.0193339 * rgb[0] + 0.1191920 * rgb[1] + 0.9503041 * rgb[2] });
}

struct Tally {
	long pixel_frames = 0, visible = 0, persistent = 0, after_visible = 0, after_frames = 0;
	double points = 0.0, wasted = 0.0, error = 0.0;
	int longest = 0;
};

// One scenario's region, measured every `step` pixels: each frame `update(f, scene)` sets the scene.
Tally run(int x, int y, int w, int h, int step, int frames, bool cuts, const std::function<void(int, OpticsScene &)> &update, double budget = 16.0) {
	// Stage 29, spike A: a narrower region and fewer frames, for readings too slow on the CPU to run whole
	if (const char *narrow = std::getenv("CHEM_FRAMES_NARROW"); narrow != nullptr) {
		int nx = x, nw = w, nstep = step, nframes = frames;
		if (std::sscanf(narrow, "%d,%d,%d,%d", &nx, &nw, &nstep, &nframes) == 4) {
			x = nx;
			w = nw;
			step = nstep;
			frames = nframes;
		}
	}
	lab::OpticsFrames rendered(x, y, w, h, budget);
	OpticsScene scene;
	struct Px {
		int x, y;
		std::array<double, 5> key{ -1, -1, -1, -1, -1 };
		double truth[3] = {};
		lab::CieLab truth_lab{}, shown_before{};
		bool known = false, changed = false;
		int run = 0, since_change = 1000;
	};
	std::vector<Px> px;
	for (int yy = y; yy < y + h; yy += step) {
		for (int xx = x; xx < x + w; xx += step) {
			px.push_back(Px{ xx, yy });
		}
	}
	Tally t;
	for (int f = 0; f < frames; ++f) {
		update(f, scene);
		scene.table[2 * 4 + 3] = cuts ? 1.0f : 0.0f;
		rendered.frame(scene);
		for (Px &p : px) {
			if (lab::optics_cover(scene, p.x, p.y) <= 0.0) {
				p.known = false;
				continue;
			}
			// the truth, again only where what the pixel sees has changed
			const std::array<double, 5> key{ lab::optics_cover(scene, p.x, p.y), lab::optics_bend(scene, p.x, p.y, 550.0).x, lab::optics_through(scene, p.x, p.y, 450.0),
				lab::optics_through(scene, p.x, p.y, 550.0), lab::optics_through(scene, p.x, p.y, 650.0) };
			const bool fresh = key != p.key;
			lab::CieLab before = p.truth_lab;
			if (fresh) {
				p.key = key;
				lab::optics_truth(scene, p.x, p.y, p.truth);
				p.truth_lab = shown_of(p.truth);
			}
			const bool unchanged = p.known && lab::eye_difference(before, p.truth_lab) == 0.0;
			if (p.known && !unchanged && lab::eye_difference(before, p.truth_lab) > lab::EYE_VISIBLE) {
				p.since_change = 0;
			} else {
				++p.since_change;
			}
			double now[3];
			rendered.colour(scene, p.x, p.y, now);
			const lab::CieLab seen = shown_of(now);
			const double e = lab::eye_difference(p.truth_lab, seen), spent = rendered.spent(p.x, p.y);
			if (p.known && unchanged && lab::eye_difference(p.truth_lab, p.shown_before) <= 4.0) {
				t.wasted += spent;
			}
			p.shown_before = seen;
			p.known = true;
			++t.pixel_frames;
			t.points += spent;
			t.error += e;
			p.run = e > lab::EYE_VISIBLE ? p.run + 1 : 0;
			t.visible += e > lab::EYE_VISIBLE ? 1 : 0;
			t.persistent += p.run >= 6 ? 1 : 0;
			if (p.run > t.longest && std::getenv("CHEM_FRAMES_WHERE") != nullptr) {
				std::printf("    frame %d: (%d, %d) visibly wrong for %d frames: truth %.3f %.3f %.3f, shown %.3f %.3f %.3f, spent %g\n", f, p.x, p.y, p.run, p.truth[0], p.truth[1], p.truth[2], now[0], now[1], now[2], spent);
			}
			t.longest = std::max(t.longest, p.run);
			if (p.since_change <= 30) {
				++t.after_frames;
				t.after_visible += e > lab::EYE_VISIBLE ? 1 : 0;
			}
		}
	}
	return t;
}

void report(const char *scenario, const Tally &off, const Tally &on) {
	const auto line = [&](const char *mode, const Tally &t) {
		const double n = static_cast<double>(t.pixel_frames > 0 ? t.pixel_frames : 1);
		std::printf("  %-8s %-12s %9ld %8.2f %8.1f%% %9.2f%% %10.2f%% %8.2f%% %8d %8.2f\n", scenario, mode, t.pixel_frames, t.points / n, t.points > 0 ? 100.0 * t.wasted / t.points : 0.0,
				100.0 * static_cast<double>(t.visible) / n, 100.0 * static_cast<double>(t.persistent) / n,
				t.after_frames > 0 ? 100.0 * static_cast<double>(t.after_visible) / static_cast<double>(t.after_frames) : 0.0, t.longest, t.error / n);
	};
	line("no cuts", off);
	line("cuts", on);
	std::fflush(stdout);
}

// ---- the scenarios

lab::ItemView vessel(const std::string &id, double ml, double molar) {
	lab::ItemView v;
	v.id = id;
	v.kind = "vessel";
	v.volume_ml = 250.0;
	v.celsius = 20.0;
	v.liquid_ml = v.condensed_ml = ml;
	lab::SubstanceView water, salt, glass;
	water.species = "water";
	water.phase = 'l';
	water.mol = ml / 18.0;
	water.g = ml;
	salt.species = "copper_sulfate_aq";
	salt.phase = 'a';
	salt.mol = molar * ml / 1000.0;
	salt.g = salt.mol * 159.6;
	glass.species = "glass";
	glass.phase = 's';
	glass.mol = 0.83;
	glass.g = 50.0;
	glass.wall = true;
	v.contents = { water, salt, glass };
	return v;
}

lab::OpticsRoom bench_room() {
	lab::OpticsRoom r;
	r.x = 100;
	r.y = 100;
	r.w = 1500;
	r.h = 700;
	r.bench = 0.7305;
	r.elsewhere = 0.0232;
	for (int c = 0; c <= 10; ++c) { // the grid: what the game draws on its wall
		r.shapes.push_back(lab::OpticsShape{ 100.0f + 140.0f * c, 100.0f, 1.0f, 700.0f, 0.55 });
	}
	for (int c = 0; c <= 2; ++c) {
		r.shapes.push_back(lab::OpticsShape{ 100.0f, 100.0f + 300.0f * c, 1500.0f, 1.0f, 0.55 });
	}
	return r;
}

} // namespace

int frames_study() {
	std::printf("chem_tool study frames: false positives and negatives of the renderer's frames against the truth (GAME_SPEC §12.2)\n");
	std::printf("  %-8s %-12s %9s %8s %9s %10s %11s %9s %8s %8s\n", "scenario", "bands", "px-frames", "pts/px", "wasted", "visible", "persistent", "after", "longest", "mean");

	// the prism demo: opened, turned, moved, turned back
	const auto prism = [](bool cuts) {
		lab::PrismDemo demo;
		return run(890, 480, 300, 60, 2, 240, cuts, [&](int f, OpticsScene &scene) {
			if (f == 60) {
				demo.turn(0.06);
			}
			if (f == 120) {
				demo.pointer(demo.centre_x(), demo.centre_y(), true);
				demo.pointer(demo.centre_x() - 40.0f, demo.centre_y(), true);
				demo.pointer(demo.centre_x(), demo.centre_y(), false);
			}
			if (f == 180) {
				demo.turn(-0.03);
			}
			scene = demo.optics();
		});
	};
	const char *only = std::getenv("CHEM_FRAMES_ONLY");
	const auto want = [&](const char *name) { return only == nullptr || std::string(only) == name; };
	if (want("prism")) {
		report("prism", prism(false), prism(true));
	}

	// the bench
	const auto bench = [](bool cuts) {
		std::vector<lab::ItemView> items{ vessel("dragged", 120, 0.4), vessel("jumping", 150, 0.2), vessel("poured", 100, 0.05), vessel("reacting", 140, 0.05), vessel("still", 0.0, 0.0) };
		const lab::OpticsRoom room = bench_room();
		return run(240, 380, 1100, 160, 3, 300, cuts, [&](int f, OpticsScene &scene) {
			float drag_x = 260.0f + 1.5f * static_cast<float>(f < 90 ? f : 90) + (f > 150 ? 1.5f * static_cast<float>((f < 210 ? f : 210) - 150) : 0.0f);
			const float jump_x = (f / 90) % 2 == 0 ? 540.0f : 820.0f;
			items[2].contents[1].mol = ((f / 120) % 2 == 0 ? 0.05 : 1.0) * 0.1;
			items[3].contents[1].mol = 0.05 * 0.14 * std::pow(1.01, f % 150);
			lab::optics_compose(scene, room, { lab::OpticsBody{ &items[0], drag_x, 400.0f, 90.0f, 112.5f }, lab::OpticsBody{ &items[1], jump_x, 400.0f, 90.0f, 112.5f },
													lab::OpticsBody{ &items[2], 960.0f, 400.0f, 90.0f, 112.5f }, lab::OpticsBody{ &items[3], 1100.0f, 400.0f, 90.0f, 112.5f },
													lab::OpticsBody{ &items[4], 1240.0f, 400.0f, 90.0f, 112.5f } });
		});
	};
	if (want("bench")) {
		report("bench", bench(false), bench(true));
	}

	// the palettes: random hard spectra in small glasses
	const auto palettes = [](bool cuts) {
		const lab::OpticsRoom room = bench_room();
		std::vector<lab::ItemView> items;
		for (int i = 0; i < 16; ++i) {
			items.push_back(vessel("p" + std::to_string(i), 100, 0.0));
		}
		spectra::Random random(77);
		std::vector<spectra::Absorber> absorbers(16);
		std::vector<double> depth(16);
		const auto draw = [&] {
			for (int i = 0; i < 16; ++i) {
				absorbers[static_cast<size_t>(i)] = random.absorber();
				depth[static_cast<size_t>(i)] = random.log_uniform(0.01, 2.0); // optical depth per cm at the peak
			}
		};
		draw();
		return run(190, 140, 1100, 640, 3, 120, cuts, [&](int f, OpticsScene &scene) {
			if (f == 60) {
				draw();
			}
			std::vector<lab::OpticsBody> bodies;
			for (int i = 0; i < 16; ++i) {
				bodies.push_back(lab::OpticsBody{ &items[static_cast<size_t>(i)], 200.0f + 320.0f * static_cast<float>(i % 4), 150.0f + 160.0f * static_cast<float>(i / 4), 90.0f, 112.5f });
			}
			lab::optics_compose(scene, room, bodies);
			// each glass's liquid: its random spectrum at 1 nm and its narrow lines
			using namespace lab::optics_layout;
			int line = 0;
			for (int i = 0; i < 16 && i < static_cast<int>(scene.table[0]); ++i) {
				const spectra::Absorber &a = absorbers[static_cast<size_t>(i)];
				float *row = &scene.rows[static_cast<size_t>((i * ROWS_PER_SET + LIQUID_ROW) * ROW)];
				for (int k = 0; k < ROW; ++k) {
					row[k] = static_cast<float>(depth[static_cast<size_t>(i)] * a.continuum[static_cast<size_t>(k)]);
				}
				const int first = line;
				for (const chem::optics::Line &l : a.lines) {
					if (line < MAX_LINES) {
						scene.lines[static_cast<size_t>(line) * 4] = static_cast<float>(l.centre_nm);
						scene.lines[static_cast<size_t>(line) * 4 + 1] = static_cast<float>(l.fwhm_nm);
						scene.lines[static_cast<size_t>(line) * 4 + 2] = static_cast<float>(depth[static_cast<size_t>(i)] * l.peak);
						++line;
					}
				}
				const size_t ids = static_cast<size_t>(HEADER_VECS + i * BODY_VECS + IDS) * 4;
				scene.table[ids + 1] = static_cast<float>(1 + (f >= 60 ? 1000 : 0) + i); // what it holds
				scene.table[ids + 2] = static_cast<float>(first);
				scene.table[ids + 3] = static_cast<float>(line - first);
			}
			++scene.revision;
		});
	};
	if (want("palettes")) {
		report("palettes", palettes(false), palettes(true));
	}
	std::printf("  (pts/px: points a pixel a frame; wasted: of them, false positives; visible, persistent: false negatives, of pixel-frames;\n"
				"   after: visible within 30 frames of a visible change; longest: the longest visible run, frames; mean: the eye metric)\n");
	return 0;
}
