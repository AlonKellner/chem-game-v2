// chem_tool capture (Stage 61; GAME_SPEC §13.1): the supplies' pictures built ahead, before the game runs. Each bench
// given is opened on a screen, which captures every supply's picture it lacks (of it alone in its box, by the
// renderer's own source on the CPU); what the file held is kept, what is new added, and the file written again (Stage 86:
// as the screen gives them, this renderer's alone: a file of another renderer's is captured again whole).
// (Stage 93) What tools/pictures.py shares the pictures by, through a store any worktree reads: `--keys BENCH...`, each
// bench's supplies with their pictures' keys, a line a bench ("BENCH\t" and its chem-picture-keys/1); `--only SUPPLY OUT
// BENCH`, that supply's picture alone, captured into OUT.
#include "chem_api.h"

#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>

namespace {

bool read_text(const char *path, std::string &text) {
	std::ifstream in(path);
	if (!in) {
		return false;
	}
	std::ostringstream all;
	all << in.rdbuf();
	text = all.str();
	return true;
}

// each bench's supplies and their pictures' keys, a line a bench
int capture_keys(int argc, char **argv) {
	int failed = 0;
	for (int k = 0; k < argc; ++k) {
		std::string text;
		if (!read_text(argv[k], text)) {
			std::fprintf(stderr, "chem_tool capture: cannot read %s\n", argv[k]);
			return 1;
		}
		cg_ui *ui = cg_ui_open(text.c_str());
		if (ui == nullptr) {
			continue; // (a bench of the optics alone, or one this build cannot make: none of its own)
		}
		cg_ui_frame(ui, 1.0 / 60.0);
		const char *keys = cg_ui_picture_keys(ui);
		if (keys == nullptr) {
			std::fprintf(stderr, "chem_tool capture: %s: %s\n", argv[k], cg_last_error());
			failed = 1;
		} else {
			std::printf("%s\t%s\n", argv[k], keys);
		}
		cg_ui_close(ui);
	}
	return failed;
}

// one supply's picture, alone, into OUT
int capture_only(const char *supply, const char *out, const char *bench) {
	std::string text;
	if (!read_text(bench, text)) {
		std::fprintf(stderr, "chem_tool capture: cannot read %s\n", bench);
		return 1;
	}
	cg_ui *ui = cg_ui_open(text.c_str());
	if (ui == nullptr) {
		std::fprintf(stderr, "chem_tool capture: %s: %s\n", bench, cg_last_error());
		return 1;
	}
	cg_ui_frame(ui, 1.0 / 60.0);
	if (cg_ui_capture_picture(ui, supply) != CG_OK) {
		std::fprintf(stderr, "chem_tool capture: %s: %s\n", bench, cg_last_error());
		cg_ui_close(ui);
		return 1;
	}
	const std::string pictures = cg_ui_pictures(ui);
	cg_ui_close(ui);
	std::ofstream file(out);
	file << pictures;
	return file ? 0 : 1;
}

} // namespace

int run_capture(int argc, char **argv) {
	if (argc >= 2 && std::string(argv[0]) == "--keys") {
		return capture_keys(argc - 1, argv + 1);
	}
	if (argc == 4 && std::string(argv[0]) == "--only") {
		return capture_only(argv[1], argv[2], argv[3]);
	}
	if (argc < 2) {
		std::fprintf(stderr, "usage: chem_tool capture PICTURES BENCH... | --keys BENCH... | --only SUPPLY OUT BENCH\n");
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
			std::printf("captured %s\n", argv[k]);
		}
		kept = cg_ui_pictures(ui);
		cg_ui_close(ui);
	}
	if (kept.empty()) {
		kept = "{\"format\": \"chem-pictures/1\", \"pictures\": []}";
	}
	std::ofstream file(out);
	file << kept;
	return file ? 0 : 1;
}
