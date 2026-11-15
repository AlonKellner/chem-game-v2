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

	/* Stage 1: the built-in tables */
	cg_species_info info;
	info.size = sizeof info;
	if (cg_species_count() < 1 || cg_species_get(0, &info) != CG_OK || strcmp(info.id, "malachite") != 0 ||
			strcmp(info.formula, "Cu2CO3(OH)2") != 0 || info.phase != CG_PHASE_SOLID || info.molar_mass < 221.113 || info.molar_mass > 221.115) {
		fprintf(stderr, "the first built-in species is not malachite at 221.114 g/mol\n");
		return 1;
	}
	if (cg_species_get(cg_species_count(), &info) != CG_ERR_INVALID_ARGUMENT) {
		fprintf(stderr, "a species index past the end was not refused\n");
		return 1;
	}
	info.size = 4;
	if (cg_species_get(0, &info) != CG_ERR_INVALID_ARGUMENT) {
		fprintf(stderr, "a struct without its size was not refused\n");
		return 1;
	}
	if (cg_reaction_count() < 1 || strcmp(cg_reaction_id(0), "malachite_decomposition") != 0 ||
			strcmp(cg_reaction_equation(0), "Cu2CO3(OH)2(s) -> 2 CuO(s) + CO2(g) + H2O(g)") != 0 || cg_reaction_equation(-1) != NULL) {
		fprintf(stderr, "the first built-in reaction is not the malachite decomposition\n");
		return 1;
	}
	/* Stage 7: a lab, a command, a report */
	if (cg_lab_open("not a bench") != NULL || cg_last_error()[0] == '\0') {
		fprintf(stderr, "a malformed bench was not refused\n");
		return 1;
	}
	cg_lab *lab = cg_lab_open("{\"format\": \"chem-bench/1\", \"room\": {\"celsius\": 20}, \"items\": ["
			"{\"id\": \"cup\", \"kind\": \"vessel\", \"volume_ml\": 100, \"wall\": \"glass\", \"wall_g\": 50, \"contents\": {\"water\": {\"g\": 20}}},"
			"{\"id\": \"flame\", \"kind\": \"burner\", \"celsius\": 1900, \"conductance\": 0.2}]}");
	if (lab == NULL) {
		fprintf(stderr, "the lab did not open: %s\n", cg_last_error());
		return 1;
	}
	if (cg_lab_command(lab, "{\"do\": \"heat\", \"burner\": \"flame\", \"vessel\": \"cup\"}") != CG_OK ||
			cg_lab_command(lab, "{\"do\": \"valve\", \"burner\": \"flame\", \"to\": 1}") != CG_OK || cg_lab_advance(lab, 600) != CG_OK || cg_lab_step(lab) != 600) {
		fprintf(stderr, "the lab did not run: %s\n", cg_last_error());
		return 1;
	}
	if (cg_lab_command(lab, "{\"do\": \"dance\"}") != CG_ERR_REFUSED || strstr(cg_last_error(), "no command") == NULL) {
		fprintf(stderr, "an unknown command was not refused\n");
		return 1;
	}
	if (strstr(cg_lab_report(lab), "\"unchanged\":true") == NULL || strlen(cg_lab_digest(lab)) != 64) {
		fprintf(stderr, "the report or the digest is not what a lab gives\n");
		return 1;
	}
	cg_lab_close(lab);
	printf("capi_c_smoke: %s\n", cg_build_name());
	return 0;
}
