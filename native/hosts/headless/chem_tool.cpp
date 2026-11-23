// chem_tool: the headless host -- the game without an engine, through the C API
// alone (GAME_SPEC §14). Stage 0: `version`. Stage 1: `tables`. Stage 2:
// `study` (study.cpp: a stage's physics alone, on the core directly). Stage 7:
// `run` and `bench` (run.cpp: a session played through the C API).
#include "chem_api.h"

#include <cstdio>
#include <cstring>
#include <sstream>
#include <fstream>
#include <filesystem>
#include <chrono>

int run_study(const char *name); // study.cpp
int run_session(int argc, char **argv, bool bench); // run.cpp

namespace {

int usage() {
	std::fprintf(stderr, "usage: chem_tool <command>\n"
						 "  version    the build's name and the C API version\n"
						 "  tables     the built-in species and reactions\n"
						 "  run SESSION [--record FILE | --check FILE] [--quiet]\n"
						 "             a session script played through the C API, every invariant checked\n"
						 "  bench SESSION   the same with the checks off, timed\n"
						 "  study cooling         a flask cooling in a room, against Newton's law\n"
						 "  study decomposition   malachite in a flask over a flame\n"
						 "  study balloon         the same flask with a balloon on its mouth\n"
						 "  study distillation    water boiled through a condenser into a beaker\n"
						 "  study dissolution     copper oxide under sulfuric acid: still, stirred, warmed\n"
						 "  study crystals        the solution boiled down and left to cool\n"
						 "  study spectrum        how many spectral bands the renderer needs\n"
						 "  study tilt            what each vessel holds below its lip as it is turned, and what runs over it\n"
						 "  study vortex          a turning liquid's vortex against its energy, E = (M g / A) V\n"
						 "  study frames          the renderer's false positives and negatives, with and without band cuts\n"
						 "  maps DIR BENCH...     every glassware vessel's refraction maps, made before the game runs, into DIR\n");
	return 2;
}

} // namespace

int main(int argc, char **argv) {
	if (argc < 2) {
		return usage();
	}
	if (std::strcmp(argv[1], "version") == 0) {
		std::printf("%s (C API %d)\n", cg_build_name(), cg_api_version());
		return 0;
	}
	if (std::strcmp(argv[1], "run") == 0 || std::strcmp(argv[1], "bench") == 0) {
		return run_session(argc - 2, argv + 2, std::strcmp(argv[1], "bench") == 0);
	}
	if (std::strcmp(argv[1], "study") == 0 && argc >= 3) {
		return run_study(argv[2]);
	}
	if (std::strcmp(argv[1], "maps") == 0 && argc >= 4) {
		// GAME_SPEC §12.3, Stage 24: the maps' store, filled ahead; a table already there is read, not made again
		std::error_code made;
		std::filesystem::create_directories(argv[2], made);
		cg_optics_store(argv[2]);
		for (int i = 3; i < argc; ++i) {
			std::ifstream in(argv[i]);
			std::stringstream text;
			text << in.rdbuf();
			cg_ui *ui = cg_ui_open(text.str().c_str());
			if (ui == nullptr) {
				std::fprintf(stderr, "chem_tool: %s: %s\n", argv[i], cg_last_error());
				return 1;
			}
			const auto t0 = std::chrono::steady_clock::now();
			const int32_t tables = cg_ui_prepare(ui);
			cg_ui_close(ui);
			if (tables < 0) {
				std::fprintf(stderr, "chem_tool: %s: %s\n", argv[i], cg_last_error());
				return 1;
			}
			std::printf("%s: %d tables of maps in %s (%.0f ms)\n", argv[i], tables, argv[2], std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count());
		}
		return 0;
	}
	if (std::strcmp(argv[1], "tables") == 0) {
		static const char *const PHASE[] = { "s", "l", "g", "aq" };
		std::printf("%-30s %-14s %-5s %6s %12s\n", "species", "formula", "phase", "charge", "g/mol");
		for (int32_t i = 0; i < cg_species_count(); ++i) {
			cg_species_info info;
			info.size = sizeof info;
			if (cg_species_get(i, &info) != CG_OK) {
				std::fprintf(stderr, "chem_tool: %s\n", cg_last_error());
				return 1;
			}
			std::printf("%-30s %-14s %-5s %6d %12.3f\n", info.id, info.formula, PHASE[info.phase], info.charge, info.molar_mass);
		}
		std::printf("\n%-30s %s\n", "reaction", "equation (balanced in every element and in charge)");
		for (int32_t i = 0; i < cg_reaction_count(); ++i) {
			std::printf("%-30s %s\n", cg_reaction_id(i), cg_reaction_equation(i));
		}
		return 0;
	}
	return usage();
}
