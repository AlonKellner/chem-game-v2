// chem_tool capture (Stage 61; GAME_SPEC §13.1): the supplies' pictures built ahead, before the game runs. Each bench
// given is opened on a screen, which captures every supply's picture it lacks (of it alone in its box, by the
// renderer's own source on the CPU); what the file held is kept, what is new added, and the file written again.
#include "chem_api.h"

#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>

int run_capture(int argc, char **argv) {
	if (argc < 2) {
		std::fprintf(stderr, "usage: chem_tool capture PICTURES BENCH...\n");
		return 2;
	}
	const std::string out = argv[0];
	std::string kept;
	if (std::ifstream in(out); in) {
		std::ostringstream text;
		text << in.rdbuf();
		kept = text.str();
	}
	for (int k = 1; k < argc; ++k) {
		std::ifstream in(argv[k]);
		if (!in) {
			std::fprintf(stderr, "chem_tool capture: cannot read %s\n", argv[k]);
			return 1;
		}
		std::ostringstream text;
		text << in.rdbuf();
		cg_ui *ui = cg_ui_open(text.str().c_str());
		if (ui == nullptr) {
			std::fprintf(stderr, "chem_tool capture: %s: %s\n", argv[k], cg_last_error());
			continue; // (a bench of the optics alone, or one this build cannot make: none of its own)
		}
		if (!kept.empty() && cg_ui_add_pictures(ui, kept.c_str()) != CG_OK) {
			std::fprintf(stderr, "chem_tool capture: %s: %s (starting again)\n", out.c_str(), cg_last_error());
			kept.clear();
		}
		cg_ui_frame(ui, 1.0 / 60.0);
		cg_ui_capture_pictures(ui);
		if (cg_ui_pictures_changed(ui)) {
			kept = cg_ui_pictures(ui);
			std::printf("captured %s\n", argv[k]);
		}
		cg_ui_close(ui);
	}
	if (kept.empty()) {
		kept = "{\"format\": \"chem-pictures/1\", \"pictures\": []}";
	}
	std::ofstream file(out);
	file << kept;
	return file ? 0 : 1;
}
