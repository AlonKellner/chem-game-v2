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
	CG_ERR_INTERNAL = 2
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

#ifdef __cplusplus
}
#endif

#endif /* CHEM_API_H */
