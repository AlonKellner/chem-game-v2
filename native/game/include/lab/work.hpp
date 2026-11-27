// The work a process does (Stage 74, Stage 75): the instructions it retired, the same on a fast core and a slow one
// and under any load, where the clock and even a thread's own CPU time are a core's speed too (docs/WORKFLOW.md, "A
// cost is held by the work, not the clock"). Off macOS, the thread's own CPU time in nanoseconds stands for it.
#pragma once

#include <ctime>

#ifdef __APPLE__
#include <libproc.h>
#include <sys/resource.h>
#include <unistd.h>
#endif

namespace lab {

// true when work_done() counts instructions (macOS), false when it reads the thread's CPU time
inline bool work_is_instructions() {
#ifdef __APPLE__
	rusage_info_v4 r{};
	return proc_pid_rusage(getpid(), RUSAGE_INFO_V4, reinterpret_cast<rusage_info_t *>(&r)) == 0 && r.ri_instructions > 0;
#else
	return false;
#endif
}

inline double work_done() {
#ifdef __APPLE__
	rusage_info_v4 r{};
	if (proc_pid_rusage(getpid(), RUSAGE_INFO_V4, reinterpret_cast<rusage_info_t *>(&r)) == 0 && r.ri_instructions > 0) {
		return static_cast<double>(r.ri_instructions);
	}
#endif
	timespec t{};
	clock_gettime(CLOCK_THREAD_CPUTIME_ID, &t);
	return static_cast<double>(t.tv_sec) * 1e9 + static_cast<double>(t.tv_nsec);
}

} // namespace lab
