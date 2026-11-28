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

/* The pointer, in screen pixels: `state` is whether its button is down
 * (CG_POINTER_DOWN, or 1), with the keys held with it (§13.1: a click with one
 * adds an item to the selection or takes it away). */
enum {
	CG_POINTER_DOWN = 1,
	CG_POINTER_SHIFT = 2,
	CG_POINTER_CTRL = 4,
	CG_POINTER_META = 8,
	CG_POINTER_RIGHT = 16 /* the right button: the full menu over a vessel (Stage 27) */
};
CG_API void cg_ui_pointer(cg_ui *ui, float x, float y, int32_t state);
/* A frame of real time, in seconds: the lab advances by it times the speed
 * chosen on the screen. */
CG_API void cg_ui_frame(cg_ui *ui, double seconds);
/* A frame's budget for the lab's steps, in milliseconds of the CPU (Stage 36; lab::Ui::set_budget): each
 * cg_ui_frame runs whole steps until it has spent it and carries the rest, so a frame costs the same at any
 * speed of the clock and a heavy moment runs the clock slower. 0 (as opened): every step asked is run. */
CG_API void cg_ui_set_budget(cg_ui *ui, double ms);
/* A play (Stage 33; lab/play.hpp, "chem-play/1"): what a hand does on the screen, as data. Once given, each
 * cg_ui_frame first gives the screen that frame's gestures. cg_ui_playing: the mark of the frame last given
 * ("" for none), or NULL when no play runs (none was given, or it is over). */
CG_API cg_status cg_ui_play(cg_ui *ui, const char *play_json);
CG_API const char *cg_ui_playing(cg_ui *ui);
/* (Stage 42) The picture the play asks for after the frame last given ("" for none, NULL when no play runs). */
CG_API const char *cg_ui_play_shot(cg_ui *ui);

enum {
	CG_DRAW_RECT = 0, /* x, y, w, h: filled */
	CG_DRAW_OUTLINE = 1, /* x, y, w, h; width */
	CG_DRAW_LINE = 2, /* from (x, y) to (w, h); width */
	CG_DRAW_POLYGON = 3, /* points[2 * first .. 2 * (first + count)): filled */
	CG_DRAW_CIRCLE = 4, /* centre x, y; radius w: filled */
	CG_DRAW_TEXT = 5, /* text + first, count bytes (also NUL-ended); baseline starts at x, y; size h */
	CG_DRAW_IMAGE = 6 /* (Stage 61) images[first] stretched over x, y, w, h: a picture the screen paints */
};

typedef struct cg_draw_command {
	int32_t type; /* CG_DRAW_* */
	uint32_t color; /* 0xRRGGBBAA */
	float width;
	int32_t first;
	int32_t count;
	float x, y, w, h;
} cg_draw_command;

/* (Stage 61) a picture: w by h pixels of 8-bit sRGB red, green and blue, row by row from the top; a hash that moves
 * only when a pixel does (keep its texture by it) */
typedef struct cg_draw_image {
	int32_t w, h;
	const uint8_t *rgb;
	uint64_t hash;
} cg_draw_image;

typedef struct cg_draw_list {
	uint32_t size; /* sizeof(cg_draw_list), set by the caller */
	int32_t command_count;
	const cg_draw_command *commands; /* in painting order */
	int32_t point_count; /* pairs */
	const float *points; /* x, y, x, y, ... */
	int32_t text_size;
	const char *text;
	int32_t above; /* the first command painted over the optical pass; those before it are under it */
	int32_t image_count; /* (Stage 61) */
	const cg_draw_image *images;
} cg_draw_list;

/* What to paint now. Borrowed until the next call on this ui. */
CG_API cg_status cg_ui_draw(cg_ui *ui, cg_draw_list *out);

/* The optical pass (GAME_SPEC §12, §12.2): what is in the vessels and the
 * balloons, painted between the two parts of the draw list. The host runs the
 * per-pixel source (native/game/shader/optics_pixel.glsl) over the whole screen
 * every frame, each pixel keeping its own history; these are its inputs, laid
 * out as lab/optics_scene.hpp says: the header and the bodies (vec_count
 * vec4s), each body's spectra at 1 nm (row_count floats), narrow lines
 * (line_count vec4s), the room light's energy table (light_count vec4s) and
 * the wall as a table (wall_count vec4s). */
typedef struct cg_optics {
	uint32_t size; /* sizeof(cg_optics), set by the caller */
	int32_t vec_count; /* the scene is 4 * vec_count floats */
	const float *table;
	uint64_t revision; /* moves when any of these four does: upload them then */
	int32_t row_count;
	const float *rows;
	int32_t line_count;
	const float *lines; /* 4 * line_count floats */
	int32_t light_count;
	const float *lights; /* 4 * light_count floats */
	/* The wall's table (GAME_SPEC §12.3): large and seldom changed, with a
	 * revision of its own. */
	int32_t wall_count;
	const float *wall; /* 4 * wall_count floats */
	uint64_t wall_revision;
	/* Where pixels are read by their own rays (GAME_SPEC §12.3, Stage 29: through
	 * anything that bends): rectangle_count rectangles of whole pixels (left, top,
	 * right, bottom). A host runs its passes over the corners and the pixels of
	 * these, after its pass over the whole screen. They change with `revision`. */
	int32_t rectangle_count;
	const float *rectangles; /* 4 * rectangle_count floats */
	/* The shapes (GAME_SPEC §12.3, Stage 29): each bending body's surfaces, for a
	 * host whose pass traces its rays live (shape_count vec4s), with a revision of
	 * their own. */
	int32_t shape_count;
	const float *shapes; /* 4 * shape_count floats */
	uint64_t shapes_revision;
	/* The images things emit by (GAME_SPEC §12.1, Stage 42: the spectral
	 * viewer's screen): image_count vec4s (none: 0 and null), with a revision of
	 * their own; the per-pixel source reads them as image_vec. */
	int32_t image_count;
	const float *images; /* 4 * image_count floats */
	uint64_t images_revision;
	/* The view of the room (GAME_SPEC §12.3, Stage 42), as the passes take one: (scale, x, y, 0), a point l of the
	 * layout at l * scale + (x, y) on the screen. The view the scene was made at; the view now; whether it is
	 * moving: while it moves the scene is not made again, and a host carries the pixels it last made from the one
	 * view to the other (the per-pixel source's anchor_state, compiled OPTICS_RESAMPLE_PASS), tracing nothing; at
	 * rest, the scene made at the view now, it carries each pixel's state once and goes on. */
	float view[4];
	float view_now[4];
	int32_t view_moving;
	float room[4]; /* the room's part of the screen, which the view is of: left, top, right, bottom */
	/* Stage 46: how many records the sensor displays' pixels have in a pool of their own (a host keeps
	 * CG_OPTICS_RELAY_VECS vec4s for each, zero at first; the per-pixel source lays them out) */
	int32_t relay_pool;
	/* Stage 90: the room's lights, a share of their full light: the display's exposure opens no wider than 8 times it */
	float room_level;
} cg_optics;

/* A host whose pass traces its rays live keeps each pixel's beams between frames
 * (GAME_SPEC §12.3, Stage 33): a record of this many vec4s for each pixel of a
 * rectangle of `rectangles`, written and read by the per-pixel source alone
 * (kept_vec, kept_put), zero at first. */
#define CG_OPTICS_KEPT_VECS 26
#define CG_OPTICS_RELAY_VECS 4
/* And it gives the pixels read by their own rays a budget of their own: this
 * many points a frame over the whole screen (a point of theirs is a ray, or a
 * quarter of one), their asks summed apart from the others'. */
#define CG_OPTICS_RAY_POINTS 300000

/* Borrowed until the next call on this ui. */
CG_API cg_status cg_ui_optics(cg_ui *ui, cg_optics *out);

/* A host's picture of the optical pass alone, the whole screen (width x height
 * pixels of 4 bytes, red first, rows from the top), settled, held to the
 * colour each pixel should show wherever a body is, on the eye metric (GAME_SPEC
 * §18: O4). */
typedef struct cg_optics_parity {
	uint32_t size; /* sizeof(cg_optics_parity), set by the caller */
	int32_t worst; /* the largest difference on the eye metric, rounded up */
	int64_t covered; /* pixels a body covers */
	int64_t over; /* of them, those visibly different (above 8 on the eye metric) */
	int32_t worst_x, worst_y; /* where the largest difference is */
	float exposure; /* in (Stage 90): the display's exposure the picture was shown at; 0 or less: 1 */
} cg_optics_parity;

CG_API cg_status cg_ui_optics_compare(cg_ui *ui, const uint8_t *rgba, int32_t width, int32_t height, cg_optics_parity *out);

/* A demo of the optical pass alone (GAME_SPEC §12.3), no chemistry: "prism", a
 * prism of flint glass in a dark room before a slit of white light. It is
 * driven like the screen: a pointer in, a draw list in two parts and the
 * optical pass's scene out. NULL and cg_last_error() for an unknown name. */
typedef struct cg_demo cg_demo;
CG_API cg_demo *cg_demo_open(const char *name);
CG_API void cg_demo_close(cg_demo *demo);
CG_API void cg_demo_pointer(cg_demo *demo, float x, float y, int32_t down);
/* Turn what the demo shows about its upright axis. */
CG_API void cg_demo_turn(cg_demo *demo, float radians);
/* Borrowed until the next call on this demo. */
CG_API cg_status cg_demo_draw(cg_demo *demo, cg_draw_list *out);
CG_API cg_status cg_demo_optics(cg_demo *demo, cg_optics *out);

/* A tutorial over the screen's lab (GAME_SPEC §13.2; the files of data/tutorials):
 * its card above the readout, and a pointer at what its step asks for. Reset
 * starts it again. CG_ERR_INVALID_ARGUMENT, with the reason in cg_last_error,
 * for one this bench cannot follow. */
CG_API cg_status cg_ui_tutor(cg_ui *ui, const char *tutorial_json);

enum {
	CG_TUTORIAL_NONE = -1,
	CG_TUTORIAL_RUNNING = 0,
	CG_TUTORIAL_COMPLETE = 1, /* every step passed, and its goal holds */
	CG_TUTORIAL_FAILED = 2 /* until a reset */
};

typedef struct cg_tutorial {
	uint32_t size; /* sizeof(cg_tutorial), set by the caller */
	int32_t state; /* CG_TUTORIAL_* */
	int32_t step; /* the current step, from 0; equal to steps once all are passed */
	int32_t steps;
	int32_t strict; /* Stage 41: 1 strict, 0 free (the screen's mode, with or without a tutorial); written when size covers it */
} cg_tutorial;

CG_API cg_status cg_ui_tutorial(cg_ui *ui, cg_tutorial *out);

/* Strict (1, as opened) or free (0) (GAME_SPEC §13.2, Stage 41), as the tutorial card's switch sets it:
 * strict, a running tutorial takes only its step's gestures and its clock waits for them; free, it only
 * points at them. The screen's own: it stays through a reset and another experiment. */
CG_API void cg_ui_set_strict(cg_ui *ui, int32_t strict);
/* The supplies' pictures (GAME_SPEC §13.1, Stage 61): a host gives back those it kept ("chem-pictures/1"); takes
 * every one the screen has (borrowed until the next call on this screen), and whether one was captured since it last
 * took them; chooses whether a supply with none has its own captured apart (off as opened); and (a build's tool)
 * has every one the bench lacks captured, waiting. (Stage 86) A file of another renderer's pictures is taken as none. */
CG_API cg_status cg_ui_add_pictures(cg_ui *ui, const char *pictures_json);
CG_API const char *cg_ui_pictures(cg_ui *ui);
CG_API int32_t cg_ui_pictures_changed(const cg_ui *ui);
CG_API void cg_ui_set_capture_ahead(cg_ui *ui, int32_t on);
CG_API void cg_ui_capture_pictures(cg_ui *ui);
/* (Stage 93) A build's tool shares pictures by key: every supply of the bench with its picture's key
 * ("chem-picture-keys/1": the renderer's key and each supply's id and key; borrowed until the next call on this
 * screen), and one supply's picture captured alone, waiting (CG_ERR_INVALID_ARGUMENT for no supply of that id). */
CG_API const char *cg_ui_picture_keys(cg_ui *ui);
CG_API cg_status cg_ui_capture_picture(cg_ui *ui, const char *supply);

/* The experiments (GAME_SPEC §13.7, Stage 41): a chem-experiments/1 catalogue (data/experiments.json), each
 * entry's bench and tutorial (null for none) inlined or a path, read by `read`: the file's text, borrowed
 * until the next call of it, or NULL for one it cannot read (`read` may be NULL when all are inlined). Each
 * is opened at once; CG_ERR_INVALID_ARGUMENT, with the reason in cg_last_error, for one that cannot be, and
 * nothing is taken. The top bar's "experiments" lists them. */
typedef const char *(*cg_read_file)(void *user, const char *path);
CG_API cg_status cg_ui_experiments(cg_ui *ui, const char *catalogue_json, cg_read_file read, void *user);
/* Opens the experiment of this id, as a click on it in the list does. */
CG_API cg_status cg_ui_choose(cg_ui *ui, const char *id);
/* The id of the experiment shown, "" for none of the catalogue's; borrowed until the next call on this ui. */
CG_API const char *cg_ui_experiment(cg_ui *ui);
/* Progress through the experiments (Stage 55): a chem-progress/1 document, {"format", "done": [ids]}, of those
 * completed; borrowed until the next call on this ui. The host keeps it between runs and gives it back. */
CG_API const char *cg_ui_progress(cg_ui *ui);
CG_API cg_status cg_ui_set_progress(cg_ui *ui, const char *progress_json);

/* A key for the screen's focused field (GAME_SPEC §13.3): CG_KEY_TEXT with
 * the character's code point, or backspace, enter, escape. */
/* CG_KEY_UNDO (Stage 34): the host's undo chord (Ctrl+Z, Cmd+Z); it brings back what was last put in the
 * trash. Backspace, with nothing being typed, puts what is chosen in the trash; a host sends Delete as it. */
/* CG_KEY_VIEW (Stage 42): the host's key for the whole room again, at 1x. */
enum { CG_KEY_TEXT = 0, CG_KEY_BACKSPACE = 1, CG_KEY_ENTER = 2, CG_KEY_ESCAPE = 3, CG_KEY_UNDO = 4, CG_KEY_VIEW = 5 };
CG_API void cg_ui_key(cg_ui *ui, int32_t key, uint32_t codepoint);

/* The view of the room (GAME_SPEC §13.1, Stage 42): zoomed by `factor` about a point of the screen (a wheel's
 * notch, a pinch; over a panel nothing), moved by a hand's drag of (dx, dy) screen pixels, and what it is: its
 * scale and the layout's point at the room's top-left corner (view[3]). */
CG_API void cg_ui_zoom(cg_ui *ui, float x, float y, float factor);
CG_API void cg_ui_pan(cg_ui *ui, float dx, float dy);
CG_API void cg_ui_view(cg_ui *ui, float view[3]);

/* The signals on the bench (GAME_SPEC §13.3): timers, monitors, alarms, notice parts, writers, circuits.
 * Borrowed until the next call on this ui. */
typedef struct cg_part {
	const char *id; /* "monitor1" */
	const char *kind; /* "timer", "monitor", "alarm", "notice", "writer", "circuit" */
	int32_t col, row; /* its square */
	int32_t out; /* it signals */
	int32_t ringing; /* an alarm, until answered */
	double left_s; /* a timer's time left */
} cg_part;

typedef struct cg_signals {
	uint32_t size; /* sizeof(cg_signals), set by the caller */
	int32_t count;
	const cg_part *parts;
	int32_t wires, rigs;
	int32_t ringing; /* the alarms ringing */
	int32_t paused; /* the screen's clock is paused */
} cg_signals;

CG_API cg_status cg_ui_signals(cg_ui *ui, cg_signals *out);

/* The last notice (a refusal's reason, a failed invariant), "" if none; the
 * lab's report (as cg_lab_report). Borrowed until the next call on this ui. */
CG_API const char *cg_ui_message(cg_ui *ui);
/* The lab's save (chem-save/1) once the debug panel's "export" was pressed
 * (GAME_SPEC §13.6), for the host to write; "" when it was not since the last
 * call. Borrowed until the next call on this ui. */
CG_API const char *cg_ui_take_export(cg_ui *ui);
CG_API const char *cg_ui_report(cg_ui *ui);

/* The centre of a named thing on the screen, for tests and scripted hosts:
 * "item:flask", "square:11,12", "cabinet", "outbox", "valve:burner@0.5",
 * "stir:flask", "panel:pour", "panel:slider@0.25", "panel:tip:malachite",
 * "panel:cancel", "pause", "speed:600", "check", "reset". 0 when it is not on
 * the screen now. */
CG_API int32_t cg_ui_locate(cg_ui *ui, const char *what, float *x, float *y);

#ifdef __cplusplus
}
#endif

#endif /* CHEM_API_H */
