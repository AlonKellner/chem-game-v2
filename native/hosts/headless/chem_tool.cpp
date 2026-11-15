// chem_tool: the headless host -- the game without an engine, through the C API
// alone (GAME_SPEC §14). Stage 0: `version`.
#include "chem_api.h"

#include <cstdio>
#include <cstring>

namespace {

int usage() {
	std::fprintf(stderr, "usage: chem_tool <command>\n"
						 "  version    the build's name and the C API version\n");
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
	return usage();
}
