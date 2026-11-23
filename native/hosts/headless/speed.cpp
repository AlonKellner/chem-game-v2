// chem_tool study speed: what a frame of the screen costs the CPU at each speed of the clock (GAME_SPEC §13.1, §19;
// Stage 36), through scenes of the demo's bench and of a vessel with sediment: the screen's frame (its steps of
// the lab, its view) and the optical scene composed. Each scene is set up by the lab's own commands, then played
// a frame of a 120 Hz display at a time.
#include "lab/lab.hpp"
#include "lab/optics_scene.hpp"
#include "lab/ui.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <functional>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#ifndef CHEM_REPO_ROOT
#define CHEM_REPO_ROOT "."
#endif

namespace {

std::string read_file(const std::string &relative) {
	std::ifstream in(std::string(CHEM_REPO_ROOT) + "/" + relative);
	std::ostringstream out;
	out << in.rdbuf();
	return out.str();
}

// a beaker of water with black copper oxide in it, over another (as native/tests/game/test_pour.cpp's)
const char *const MUDDY = R"({"format": "chem-bench/2", "room": {"celsius": 20}, "items": [
	{"id": "muddy", "at": [4, 8], "kind": "vessel", "volume_ml": 250, "wall": "glass", "glassware": "beaker", "wall_g": 80, "loss": 0.5,
		"contents": {"water": {"g": 120}, "copper_oxide": {"g": 12, "particles": 1000000000}}},
	{"id": "beaker", "at": [10, 8], "kind": "vessel", "volume_ml": 250, "wall": "glass", "glassware": "beaker", "wall_g": 80, "loss": 0.5}]})";

struct Scene {
	const char *name;
	const char *bench; // nullptr: the demo's
	std::vector<std::string> setup;
	bool tutorial = false;
};

bool command(lab::Ui &ui, const std::string &c) {
	std::string why;
	if (!ui.lab().command(c, why)) {
		std::fprintf(stderr, "speed: %s refused: %s\n", c.c_str(), why.c_str());
		return false;
	}
	return true;
}

void press(lab::Ui &ui, const std::string &what) {
	float x = 0.0f, y = 0.0f;
	if (!ui.locate(what, x, y)) {
		std::fprintf(stderr, "speed: %s is not on the screen\n", what.c_str());
		return;
	}
	ui.pointer(x, y, true);
	ui.pointer(x, y, false);
}

double ms_since(std::chrono::steady_clock::time_point t) {
	return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t).count();
}

} // namespace

int speed_study() {
	const std::string demo = read_file("data/benches/demo.json");
	const std::string monitors = R"({"do": "part", "kind": "monitor", "at": [0, 0]})";
	const std::vector<Scene> scenes = {
		{ "still", nullptr, {} },
		{ "decomposing", nullptr,
				{ R"({"do": "heat", "burner": "burner", "vessel": "flask"})", R"({"do": "tip", "species": "malachite", "from": "jar", "to": "flask", "all": true})",
						R"({"do": "attach", "item": "balloon", "to": "flask"})", R"({"do": "valve", "burner": "burner", "to": 1})" } },
		{ "dissolving, stirred", nullptr,
				{ R"({"do": "heat", "burner": "burner", "vessel": "flask"})", R"({"do": "tip", "species": "malachite", "from": "jar", "to": "flask", "all": true})",
						R"({"do": "valve", "burner": "burner", "to": 1})", R"({"do": "wait_until", "item": "flask", "species": "malachite", "below_g": 0.005, "timeout": 1800})",
						R"({"do": "valve", "burner": "burner", "to": 0})", R"({"do": "wait_until", "item": "flask", "below_c": 30, "timeout": 1800})",
						R"({"do": "pour", "from": "acid", "to": "flask", "ml": 50})", R"({"do": "stir", "vessel": "flask", "on": true})", R"({"do": "valve", "burner": "burner", "to": 0.2})" } },
		{ "dissolving, monitored", nullptr,
				{ R"({"do": "heat", "burner": "burner", "vessel": "flask"})", R"({"do": "tip", "species": "malachite", "from": "jar", "to": "flask", "all": true})",
						R"({"do": "valve", "burner": "burner", "to": 1})", R"({"do": "wait_until", "item": "flask", "species": "malachite", "below_g": 0.005, "timeout": 1800})",
						R"({"do": "valve", "burner": "burner", "to": 0})", R"({"do": "wait_until", "item": "flask", "below_c": 30, "timeout": 1800})",
						R"({"do": "pour", "from": "acid", "to": "flask", "ml": 50})", R"({"do": "stir", "vessel": "flask", "on": true})",
						R"({"do": "part", "kind": "monitor", "at": [0, 0]})", R"({"do": "part", "kind": "monitor", "at": [1, 0]})", R"({"do": "part", "kind": "monitor", "at": [2, 0]})",
						R"({"do": "set", "part": "monitor1", "item": "flask", "what": "water", "compare": "below", "value": 1})",
						R"({"do": "set", "part": "monitor2", "item": "flask", "what": "celsius", "compare": "above", "value": 500})",
						R"({"do": "set", "part": "monitor3", "item": "flask", "what": "copper_sulfate_aq", "measure": "purity", "compare": "above", "value": 99})",
						R"({"do": "part", "kind": "controller", "at": [3, 0]})", R"({"do": "set", "part": "controller1", "item": "flask", "what": "celsius", "target": 60})",
						R"({"do": "rig", "from": "controller1", "item": "burner", "control": "valve"})" } },
		{ "boiling into the condenser", nullptr,
				{ R"({"do": "pour", "from": "acid", "to": "flask", "ml": 100})", R"({"do": "heat", "burner": "burner", "vessel": "flask"})",
						R"({"do": "attach", "item": "condenser", "to": "flask"})", R"({"do": "under", "item": "beaker", "of": "condenser"})", R"({"do": "valve", "burner": "burner", "to": 1})" } },
		{ "poured by hand", nullptr, { R"({"do": "hold", "item": "acid", "over": "flask"})", R"({"do": "pour", "from": "acid", "rate": 0.55})" } },
		{ "poured by hand, sediment", MUDDY, { R"({"do": "hold", "item": "muddy", "over": "beaker"})", R"({"do": "pour", "from": "muddy", "rate": 0.62})" } },
		{ "poured by hand, stirred", MUDDY, { R"({"do": "stir", "vessel": "muddy", "on": true})", R"({"do": "hold", "item": "muddy", "over": "beaker"})", R"({"do": "pour", "from": "muddy", "rate": 0.62})" } },
		{ "under the tutorial", "lab", {}, true },
	};
	// (the last: 600x under the host's budget, lab::Ui::set_budget)
	const int speeds[] = { 1, 60, 600, -600 };
	const char *only = std::getenv("CHEM_SPEED_ONLY");
	const double budget = 3.0;
	std::printf("%-28s %8s %8s %8s %8s %9s %8s %8s\n", "scene", "speed", "steps", "frame ms", "worst", "us a step", "scene ms", "achieved");
	for (const Scene &s : scenes) {
		if (only != nullptr && std::string(s.name).find(only) == std::string::npos) {
			continue;
		}
		for (const int asked : speeds) {
			const int speed = asked < 0 ? -asked : asked;
			std::string why;
			const std::string bench = s.bench == nullptr ? demo : std::string(s.bench) == "lab" ? read_file("data/benches/lab.json") : std::string(s.bench);
			std::unique_ptr<lab::Ui> ui = lab::Ui::open(bench, why);
			if (!ui) {
				std::fprintf(stderr, "speed: %s\n", why.c_str());
				return 1;
			}
			if (s.tutorial && !ui->tutor(read_file("data/tutorials/demo.json"), why)) {
				std::fprintf(stderr, "speed: the tutorial: %s\n", why.c_str());
				return 1;
			}
			bool ok = true;
			for (const std::string &c : s.setup) {
				ok = ok && command(*ui, c);
			}
			if (!ok) {
				return 1;
			}
			ui->frame(1.0 / 120.0);
			press(*ui, "speed:" + std::to_string(speed));
			if (asked < 0) {
				ui->set_budget(budget);
			}
			const int frames = 240;
			double total = 0.0, worst = 0.0, scene = 0.0;
			int worst_at = 0;
			const int64_t first = ui->lab().step();
			for (int f = 0; f < frames; ++f) {
				const auto t0 = std::chrono::steady_clock::now();
				ui->frame(1.0 / 120.0);
				const double ms = ms_since(t0);
				const auto t1 = std::chrono::steady_clock::now();
				ui->optics();
				scene += ms_since(t1);
				total += ms;
				if (ms > worst) {
					worst = ms;
					worst_at = f;
				}
			}
			const int64_t steps = ui->lab().step() - first;
			const std::string label = std::to_string(speed) + (asked < 0 ? " (3ms)" : "");
			std::printf("%-28s %8s %8.1f %8.2f %8.2f %9.2f %8.2f %8.0f  (the worst at frame %d)\n", s.name, label.c_str(), static_cast<double>(steps) / frames, total / frames, worst, steps > 0 ? 1000.0 * total / static_cast<double>(steps) : 0.0,
					scene / frames, ui->achieved(), worst_at);
			std::fflush(stdout);
		}
	}
	return 0;
}
