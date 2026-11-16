/* chem_api.h -- the game behind a plain C interface (GAME_SPEC §14).
 *
 * Rules of the boundary:
 *   - every struct starts with its `size` (the caller's sizeof), so a field can
 *     be added without breaking a host built against an older header;
 *   - enums are int32 and never renumbered;
 *   - the library owns all memory: a returned pointer is a borrowed view, valid
 *     until the next non-const call on the same object (or, for cg_last_error
 *     and cg_build_name, the next call on the same thread);
 *   - no exception crosses the boundary: a failure is a status, its message in
 *     cg_last_error().
 *
 * Stage 0: the build's identity and the error and log plumbing.
 * Stage 1: the built-in species and reactions.
 * Stage 7: the lab, driven by commands.
 * Stage 9: the screen: a pointer in, a draw list out.
 */
#ifndef CHEM_API_H
#define CHEM_API_H

#include <stdint.h>

#if defined(_WIN32)
#define CG_API __declspec(dllexport)
#else
#define CG_API __attribute__((visibility("default")))
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef int32_t cg_status;
enum {
	CG_OK = 0,
	CG_ERR_INVALID_ARGUMENT = 1,
	CG_ERR_INTERNAL = 2,
	CG_ERR_REFUSED = 3, /* the command was refused; the lab is as it was (cg_last_error says why) */
	CG_ERR_INVARIANT = 4 /* an invariant failed; the lab takes no more commands */
};

/* The C API's own version: bumped when a host must change to keep working. */
#define CG_API_VERSION 1
CG_API int32_t cg_api_version(void);

/* "chem-game-v2 <GAME_SPEC version>" */
CG_API const char *cg_build_name(void);

/* The message of the calling thread's last failed call ("" if none). */
CG_API const char *cg_last_error(void);

/* Where the library's reports go (an invariant violation in a release build,
 * GAME_SPEC §4.4). A null sink restores the default, stderr. */
typedef void (*cg_log_fn)(void *user, const char *message);
CG_API void cg_set_log(cg_log_fn fn, void *user);

/* Stage 0 self-check, for a host's smoke test: reports `message` through the
 * log sink and fails with it. Returns CG_ERR_INVALID_ARGUMENT on null. */
CG_API cg_status cg_debug_report(const char *message);

/* ---- the built-in tables (GAME_SPEC §4.1): the species the game ships and its
 * reactions, each balanced in every element and in charge. */

enum {
	CG_PHASE_SOLID = 0,
	CG_PHASE_LIQUID = 1,
	CG_PHASE_GAS = 2,
	CG_PHASE_AQUEOUS = 3
};

typedef struct cg_species_info {
	uint32_t size; /* sizeof(cg_species_info), set by the caller */
	int32_t phase; /* CG_PHASE_* */
	int32_t charge;
	const char *id; /* "water" */
	const char *formula; /* "H2O" */
	double molar_mass; /* g/mol, derived from the formula */
} cg_species_info;

CG_API int32_t cg_species_count(void);
/* The strings are valid for the life of the library. */
CG_API cg_status cg_species_get(int32_t index, cg_species_info *out);

CG_API int32_t cg_reaction_count(void);
/* "malachite_decomposition"; NULL (and cg_last_error) for a bad index. */
CG_API const char *cg_reaction_id(int32_t index);
/* "Cu2CO3(OH)2(s) -> 2 CuO(s) + CO2(g) + H2O(g)"; NULL for a bad index. */
CG_API const char *cg_reaction_equation(int32_t index);

/* ---- the lab (GAME_SPEC §13): a bench of equipment over one closed world,
 * driven by commands. The library reads no files: the host passes the bench
 * (a file of data/benches) as text. */

typedef struct cg_lab cg_lab;

/* NULL, with cg_last_error, when the text is not a bench that can be built. */
CG_API cg_lab *cg_lab_open(const char *bench_json);
CG_API void cg_lab_close(cg_lab *lab);

/* One command: a JSON object with a "do" (place, stow, heat, unheat, valve,
 * stir, pour, tip, attach, detach, under, submit, wait, wait_until). CG_ERR_REFUSED: nothing
 * changed, except that a command which changes connections first takes the lab
 * to a step boundary (at most one step). */
CG_API cg_status cg_lab_command(cg_lab *lab, const char *command_json);

/* Advances whole steps of 1/60 s of simulated time. */
CG_API cg_status cg_lab_advance(cg_lab *lab, int64_t steps);

/* Every invariant on every step: on (nonzero) by default. Off runs the same
 * states, bit for bit, several times faster. Set it before the first step. */
CG_API void cg_lab_set_checking(cg_lab *lab, int32_t on);

CG_API int64_t cg_lab_step(const cg_lab *lab);

/* What is where, measured: JSON. Borrowed until the next call on this lab. */
CG_API const char *cg_lab_report(cg_lab *lab);

/* SHA-256 of the whole state, 64 hex digits: equal digests are equal states.
 * Borrowed until the next call on this lab. */
CG_API const char *cg_lab_digest(cg_lab *lab);

/* The whole lab as text. Borrowed until the next call on this lab. A lab
 * loaded from it continues exactly as this one would have. */
CG_API const char *cg_lab_save(cg_lab *lab);
/* NULL, with cg_last_error, when the text is not a save of this build's data. */
CG_API cg_lab *cg_lab_load(const char *save_json);

/* ---- the screen (GAME_SPEC §13): a lab on a 1920 x 1080 screen. A host
 * forwards its pointer, calls cg_ui_frame once a frame, and paints the draw
 * list: everything a player does on the screen is decided here, the same on
 * every host. */

typedef struct cg_ui cg_ui;

CG_API cg_ui *cg_ui_open(const char *bench_json); /* NULL, with cg_last_error, for a bench that cannot be built */
CG_API void cg_ui_close(cg_ui *ui);

/* The pointer, in screen pixels, and whether its button is down (nonzero). */
CG_API void cg_ui_pointer(cg_ui *ui, float x, float y, int32_t down);
/* A frame of real time, in seconds: the lab advances by it times the speed
 * chosen on the screen. */
CG_API void cg_ui_frame(cg_ui *ui, double seconds);

enum {
	CG_DRAW_RECT = 0, /* x, y, w, h: filled */
	CG_DRAW_OUTLINE = 1, /* x, y, w, h; width */
	CG_DRAW_LINE = 2, /* from (x, y) to (w, h); width */
	CG_DRAW_POLYGON = 3, /* points[2 * first .. 2 * (first + count)): filled */
	CG_DRAW_CIRCLE = 4, /* centre x, y; radius w: filled */
	CG_DRAW_TEXT = 5 /* text + first, count bytes (also NUL-ended); baseline starts at x, y; size h */
};

typedef struct cg_draw_command {
	int32_t type; /* CG_DRAW_* */
	uint32_t color; /* 0xRRGGBBAA */
	float width;
	int32_t first;
	int32_t count;
	float x, y, w, h;
} cg_draw_command;

typedef struct cg_draw_list {
	uint32_t size; /* sizeof(cg_draw_list), set by the caller */
	int32_t command_count;
	const cg_draw_command *commands; /* in painting order */
	int32_t point_count; /* pairs */
	const float *points; /* x, y, x, y, ... */
	int32_t text_size;
	const char *text;
	int32_t above; /* the first command painted over the optical pass; those before it are under it */
} cg_draw_list;

/* What to paint now. Borrowed until the next call on this ui. */
CG_API cg_status cg_ui_draw(cg_ui *ui, cg_draw_list *out);

/* The optical pass (GAME_SPEC §12): what is in the vessels and the balloons,
 * painted between the two parts of the draw list by one shader over the whole
 * screen (engines/godot/shaders/optics.gdshader). Its one input is this table,
 * the shader's `uniform vec4 scene[vec_count]`. */
typedef struct cg_optics {
	uint32_t size; /* sizeof(cg_optics), set by the caller */
	int32_t vec_count; /* the table is 4 * vec_count floats */
	const float *table;
	uint64_t revision; /* moves when the table does: upload it then */
} cg_optics;

/* Borrowed until the next call on this ui. */
CG_API cg_status cg_ui_optics(cg_ui *ui, cg_optics *out);

/* A host's picture of the optical pass alone, the whole screen (width x height
 * pixels of 4 bytes, red first, rows from the top), held to the reference
 * renderer wherever a body is (GAME_SPEC §18: O4). */
typedef struct cg_optics_parity {
	uint32_t size; /* sizeof(cg_optics_parity), set by the caller */
	int32_t worst; /* the largest difference of any channel, in 8-bit steps */
	int64_t covered; /* pixels a body covers */
	int64_t over; /* of them, those that differ by more than 2 steps */
	int32_t worst_x, worst_y; /* where the largest difference is */
} cg_optics_parity;

CG_API cg_status cg_ui_optics_compare(cg_ui *ui, const uint8_t *rgba, int32_t width, int32_t height, cg_optics_parity *out);

/* The last notice (a refusal's reason, a failed invariant), "" if none; the
 * lab's report (as cg_lab_report). Borrowed until the next call on this ui. */
CG_API const char *cg_ui_message(cg_ui *ui);
CG_API const char *cg_ui_report(cg_ui *ui);

/* The centre of a named thing on the screen, for tests and scripted hosts:
 * "item:flask", "cell:3,1", "cabinet", "outbox", "valve:burner@0.5",
 * "stir:flask", "panel:pour", "panel:slider@0.25", "panel:tip:malachite",
 * "panel:cancel", "pause", "speed:600", "check", "reset". 0 when it is not on
 * the screen now. */
CG_API int32_t cg_ui_locate(cg_ui *ui, const char *what, float *x, float *y);

#ifdef __cplusplus
}
#endif

#endif /* CHEM_API_H */
