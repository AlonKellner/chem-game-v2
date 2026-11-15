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

#ifdef __cplusplus
}
#endif

#endif /* CHEM_API_H */
