# TDD 74: A cost held by the work (Stage 74)

## The request

After Stage 73's sign-off, the user, 2026-11-27: "Approved, merge and proceed". The open item that cost every gate:
`lab_tests.signals.12` failed three of the last five gates, always at a machine load of 20 to 50, and passed alone.
No behaviour of the game changes; GAME_SPEC stays 0.73.0.

| Phase | Topic | Status |
|---|---|---|
| 74.1 | Why it failed: a core's speed in the thread's own time | Done |
| 74.2 | The cost as instructions retired, a share of the frame | Done |
| 74.3 | `make gates`; the user's sign-off | gates passed; sign-off open |

## 74.1 Why

"Three monitors cost little at 600x" (Stage 13) held the monitors' cost to 3 ms of a 600-step frame (about 33 ms)
by the thread's own CPU time, the best of five each way (Stage 68). The machine has 6 performance and 6 efficiency
cores. On a crowded machine the scheduler moves a thread to an efficiency core, where the same work takes two to
three times the CPU time, so the best of five can pair a fast run without the monitors with a slow one with them.
At a load of 35, twelve copies side by side read a difference from −2.7 to +5.5 ms; six would have failed.

## 74.2 The work

The test now compares the instructions the process retired (`proc_pid_rusage`, `RUSAGE_INFO_V4`'s `ri_instructions`;
each lab test case is its own ctest process, the lab on one thread) and holds the monitors' share of a frame under a
tenth. It reads 5.9 %: 391 million instructions a frame without them, 414 million with. The same twelve copies read
5.87 to 5.95 %. The thread's times are still printed. Where the counter is missing (not macOS), the thread's own time
is the measure. Under the sanitizers the share reads 9.6 % (their instrumentation weighs the monitors' code
otherwise), so there it is reported and not bounded, as the time was.

## 74.3 The gates

`make gates` at a load of 23 to 35: every gate passed in 385 s, signals.12 among them (the release build's; the
sanitizers' left it to the full gate, over its limit there, and passed it alone: 9.45 s).

WORKFLOW's "What keeps the gates quick" has the rule: a cost is held by the work, not the clock.
