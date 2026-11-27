# TDD 70: The gates under five minutes again (Stage 70)

## The request

The user, 2026-11-27, signing off Stage 69: "Approved, merge and proceed. If you can, proceed to optimize the gates".
Stages 63 (ions) and 67 (liquids that expand) made the physics heavier: `make gates` on main was 335 to 596 s (the
machine shared with other worktrees' gates), and a new worktree's first gate ran every sanitizer entry (916 s).
No behaviour of the game changes: every golden and digest as it was; GAME_SPEC stays 0.69.0.

| Phase | Topic | Status |
|---|---|---|
| 70.1 | The column's tests checked each step twice | Done |
| 70.2 | D2's windows as the demo's fixture lists them, a long wait in two halves | Done |
| 70.3 | ctest's measured costs shared by every worktree | Done |
| 70.4 | A probe's dead evaluation | Done |
| 70.5 | `make gates`; the user's sign-off | signed off by the user, 2026-11-27: "Approved, merge and proceed" |

## 70.1 Each step checked once

`test_column.cpp`'s bench turned the simulation's own checking on, and its `run()` held every step's trace to every
invariant again: each step checked twice (the simulation's check reports through the error sink; `run()`'s fails the
test, and stays). `chem_tests.column.2`, an hour of a layered column: 209 s to 47 s (at a load of 17 to 30).

## 70.2 D2's windows from the fixture

The demo record plays a `wait` or a `wait_until` of more than 100,000 steps in two halves: a wait of half its
steps (whole seconds), a save, then the rest (a wait, what is left of it; a wait_until, itself). The record's digest
after the command holds that the halves end where the whole did. It lists D2's windows (`windows.json`: from which
save, which commands, into which step and digest); D2 plays them, eight entries. The demo's cooling (command 19,
340,000 steps) and its two-hour wait (command 20, 432,000) are two windows each: the longest 54 s, from 125 to 190 s.

## 70.3 Measured costs for every worktree

`tools/gate_cache.py` folds a tree's measured costs (`Testing/Temporary/CTestCostData.txt`) into its kind's shared file
(`~/Library/Caches/chem-game-v2/costs/build-core.txt`, `build-asan.txt`) after each run, and gives a tree what the
others measured that it has not, before it runs: a new worktree's ctest starts the longest first, and the
sanitizers' limits (`--limit build-core:30 --limit build-asan:20`, by kind) leave out the same entries at its first
gate. Stage 69's new worktree ran 730 entries under the sanitizers at its first gate (916 s); this one, 639, in 84 s.

## 70.4 A probe's dead evaluation

`reach` (sim.cpp) asked the affinity at no extent, which its caller had just asked, and put the answer aside unread.
At a cell at its balance (the usual case) a probe's evaluations were three, now two: the demo's play 6.92e11 to
6.58e11 instructions. (The affinity's own cost is the activity model at each extent, which the next extent changes;
fewer evaluations would be another search, and other numbers: left.)

## 70.5 The gates

`GATE_CACHE=0 make gates`, another worktree's gate on the machine (a load of 8 to 25): **262 s**, every gate passed;
the release ctest 262 s (2,919 s summed), the sanitizers' 84 s (639 entries), 1,912 s of CPU. The release run is the
gate's length; its longest entries: `chem_tests.column.2` 125 s, the demo record 122 s, ui.9 106 s, controller.4
75 s, the tutorials 70 s.

Next, if it is needed: the demo's plays (the record, ui.9, the two tutorials: four plays of a four-hour session), the
settling of the fast equilibria (43 % of a step since Stage 63).
