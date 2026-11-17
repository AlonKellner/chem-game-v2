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
			"{\"id\": \"cup\", \"at\": [1, 0], \"kind\": \"vessel\", \"volume_ml\": 100, \"wall\": \"glass\", \"wall_g\": 50, \"contents\": {\"water\": {\"g\": 20}}},"
			"{\"id\": \"flame\", \"at\": [2, 0], \"kind\": \"burner\", \"celsius\": 1900, \"conductance\": 0.2}]}");
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
	/* Stage 8: a saved lab, loaded, is the same lab */
	const char *digest = cg_lab_digest(lab);
	char before[65];
	strncpy(before, digest, sizeof before - 1);
	before[sizeof before - 1] = '\0';
	cg_lab *again = cg_lab_load(cg_lab_save(lab));
	if (again == NULL || strcmp(cg_lab_digest(again), before) != 0 || cg_lab_step(again) != cg_lab_step(lab)) {
		fprintf(stderr, "a saved lab did not load as itself: %s\n", cg_last_error());
		return 1;
	}
	if (cg_lab_load("{}") != NULL) {
		fprintf(stderr, "a save that is not one was not refused\n");
		return 1;
	}
	cg_lab_close(again);
	cg_lab_close(lab);
	/* Stages 9 and 10: the screen: a draw list in two parts, and the optical pass's table between them */
	cg_ui *ui = cg_ui_open("{\"format\": \"chem-bench/1\", \"room\": {\"celsius\": 20}, \"grid\": [4, 1], \"items\": ["
			"{\"id\": \"cup\", \"at\": [1, 0], \"kind\": \"vessel\", \"volume_ml\": 100, \"wall\": \"glass\", \"wall_g\": 50, \"contents\": {\"water\": {\"g\": 20}}}]}");
	if (ui == NULL) {
		fprintf(stderr, "the screen did not open: %s\n", cg_last_error());
		return 1;
	}
	cg_ui_frame(ui, 1.0 / 60.0);
	cg_draw_list list;
	list.size = sizeof list;
	cg_optics optics;
	optics.size = sizeof optics;
	if (cg_ui_draw(ui, &list) != CG_OK || list.command_count < 10 || list.above <= 0 || list.above >= list.command_count) {
		fprintf(stderr, "the draw list is not in two parts around the optical pass\n");
		return 1;
	}
	if (cg_ui_optics(ui, &optics) != CG_OK || optics.vec_count != 3 + 16 * 7 + 2 * 256 || optics.table[0] != 1.0f || optics.revision == 0 || optics.row_count != 16 * 5 * 401 ||
			optics.line_count != 256 || optics.light_count != 4097 || optics.rows == NULL || optics.lights == NULL || optics.lights[4 * 4096 + 2] < 0.99f) {
		fprintf(stderr, "the optical pass's scene is not one vessel in 627 vectors, with its spectra, its lines and the room light's table\n");
		return 1;
	}
	optics.size = 0;
	if (cg_ui_optics(ui, &optics) != CG_ERR_INVALID_ARGUMENT) {
		fprintf(stderr, "a table with no size was not refused\n");
		return 1;
	}
	/* a picture of nothing differs from the reference wherever the cup is */
	static uint8_t picture[64 * 36 * 4];
	cg_optics_parity parity;
	parity.size = sizeof parity;
	if (cg_ui_optics_compare(ui, picture, 64, 36, &parity) != CG_OK || parity.covered != 0 || cg_ui_optics_compare(ui, NULL, 64, 36, &parity) != CG_ERR_INVALID_ARGUMENT) {
		fprintf(stderr, "the comparison did not find the corner of the screen empty\n");
		return 1;
	}
	/* Stage 13: a timer written with the pointer and the keys: the cup's temperature, a time limit of 0:02 */
	float x = 0, y = 0;
	if (!cg_ui_locate(ui, "item:cup", &x, &y)) {
		fprintf(stderr, "the cup is not on the screen\n");
		return 1;
	}
	cg_ui_pointer(ui, x, y, 0);
	cg_ui_pointer(ui, x, y, 1);
	cg_ui_pointer(ui, x, y, 0);
	const char *gestures[] = { "timer:add", "timer:remove:0", "timer:limit:value" };
	for (int i = 0; i < 3; ++i) {
		if (!cg_ui_locate(ui, gestures[i], &x, &y)) {
			fprintf(stderr, "not on the screen: %s\n", gestures[i]);
			return 1;
		}
		cg_ui_pointer(ui, x, y, 1);
		cg_ui_pointer(ui, x, y, 0);
	}
	cg_ui_key(ui, CG_KEY_TEXT, '2');
	cg_ui_key(ui, CG_KEY_ENTER, 0);
	cg_ui_frame(ui, 2.5);
	cg_timers timers;
	timers.size = sizeof timers;
	if (cg_ui_timers(ui, &timers) != CG_OK || timers.count != 1 || timers.timers[0].fired != 1 || strcmp(timers.timers[0].fired_by, "0:02 passed") != 0 || timers.paused != 1) {
		fprintf(stderr, "the timer did not fire at its time limit and pause the clock\n");
		return 1;
	}
	/* Stage 12: a tutorial over the screen, passed by a drag */
	cg_tutorial tutorial;
	tutorial.size = sizeof tutorial;
	if (cg_ui_tutorial(ui, &tutorial) != CG_OK || tutorial.state != CG_TUTORIAL_NONE || cg_ui_tutor(ui, "{\"format\": \"chem-tutorial/1\", \"steps\": []}") != CG_ERR_INVALID_ARGUMENT) {
		fprintf(stderr, "a screen without a tutorial, or an empty tutorial, was not told apart\n");
		return 1;
	}
	if (cg_ui_tutor(ui, "{\"format\": \"chem-tutorial/1\", \"steps\": [{\"title\": \"move\", \"text\": \"the cup to the left\", "
					   "\"checks\": [{\"text\": \"in the first square\", \"when\": {\"item\": \"cup\", \"place\": \"bench\", \"not\": {\"item\": \"cup\", \"liquid_below_ml\": 1}}}]}]}") != CG_OK) {
		fprintf(stderr, "the tutorial was refused: %s\n", cg_last_error());
		return 1;
	}
	if (cg_ui_tutorial(ui, &tutorial) != CG_OK || tutorial.state != CG_TUTORIAL_COMPLETE || tutorial.step != 1 || tutorial.steps != 1) {
		fprintf(stderr, "the one-step tutorial, already done, is not complete\n");
		return 1;
	}
	cg_ui_close(ui);
	printf("capi_c_smoke: %s\n", cg_build_name());
	return 0;
}
