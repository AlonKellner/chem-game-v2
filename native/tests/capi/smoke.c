/* A C11 host compiles against chem_api.h and links the shared library (Stage 0). */
#include "chem_api.h"

#include <stdio.h>
#include <string.h>

static int logged = 0;

static void on_log(void *user, const char *message) {
	(void)user;
	if (strcmp(message, "smoke") == 0) {
		logged++;
	}
}

int main(void) {
	if (cg_api_version() != CG_API_VERSION) {
		fprintf(stderr, "header and library disagree on the C API version\n");
		return 1;
	}
	if (strncmp(cg_build_name(), "chem-game-v2 ", 13) != 0) {
		fprintf(stderr, "unexpected build name: %s\n", cg_build_name());
		return 1;
	}
	if (cg_last_error()[0] != '\0') {
		fprintf(stderr, "an error before any call failed\n");
		return 1;
	}
	if (cg_debug_report(NULL) != CG_ERR_INVALID_ARGUMENT || strstr(cg_last_error(), "null") == NULL) {
		fprintf(stderr, "a null argument was not refused\n");
		return 1;
	}
	cg_set_log(on_log, NULL);
	if (cg_debug_report("smoke") != CG_ERR_INTERNAL || logged != 1 || strcmp(cg_last_error(), "smoke") != 0) {
		fprintf(stderr, "the log sink or the last error is not wired\n");
		return 1;
	}
	cg_set_log(NULL, NULL);
	printf("capi_c_smoke: %s\n", cg_build_name());
	return 0;
}
