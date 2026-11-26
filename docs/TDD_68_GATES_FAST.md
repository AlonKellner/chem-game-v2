# TDD 68: The gates fast again (Stage 68)

## The request

The user, 2026-11-26: "`make gates` takes around a full hour. That's way too much. Let's plan how we can speed it up
significantly": optimize, cache, share resources across worktrees, replace tests with faster ones of the same
coverage, merge tests that can share a simulation, drop overlapping coverage. Decided with the user: the sanitizer
build's labs run unchecked; `make gates` reuses earlier merge-level passes; overlap is proven, then run once (no
play deleted); shared state in `~/Library/Caches/chem-game-v2/`. No behaviour of the game changes: every golden and
digest is as it was (the proof), GAME_SPEC stays at 0.62.0.

| Phase | Topic | Status |
|---|---|---|
| 68.0 | Measured: where the time goes; the gates' summary names each run's slowest entries | Done |
| 68.1 | The hot code, bit for bit: the invariants' shared work, no allocation a step, a vessel's shape once, its level | Done |
| 68.2 | The demo played once for all that holds it (a ctest fixture: its scenes) | Done |
| 68.3 | Checks where another run checks the same states; independent runs side by side | Done |
| 68.4 | The sanitizer build's labs unchecked (`LAB_CHECK=0`) | Done |
| 68.5 | The Godot suite opens no catalogue it does not read | Done |
| 68.6 | The result cache: merge-level passes, one cache for every worktree | Done |
| 68.7 | One ccache for every worktree; no worktree's path in a binary | Done |
| 68.8 | `make gates` (1,307 s uncached; 20 s again with nothing changed); the user's sign-off | gates passed; sign-off open |

## 68.0 Measured (M3 Pro: 6 fast cores, 6 slow; the machine shared with other worktrees' gates)

`make gates` grew from 359 s (Stage 48) to 2,579 s (Stage 62). At Stage 62's close nothing was compiled: the
release ctest ran 1,389 s (16,067 s of its entries' time summed), the sanitizers' 2,122 s (22,908 s), the Godot
suite 1,396 s, side by side on twelve cores at a load near 40. The gate was bound by its work (its sum over
twelve is its length), its last entry the sanitizers' `demo_session` (2,122 s).

Profiles (`sample`, on the running gate):

- **Checked runs** (`demo_session`, trash.2, walls.4): `check_all` 58 to 61 % of the time, the simulation 23 to 28 %.
  Of it, the world's totals 21 to 23 % (C1 to C4 each made them twice a step, every species of every cell counted,
  the absent ones too), copies of the state about 16 % (a step's trace copied the world twice, C5 and T1 once each;
  under the sanitizers mostly their allocator).
- **The experiments' plays at a person's pace** (experiments.60): the view of every frame 31 to 37 % in a vessel's
  level, through its boiling's bubbles.
- **The Godot suite**: all of it in `glass::Vessel::make_paths`, each ChemLab opening every experiment's bench (26), a
  vessel's shape made again for every item: about 25 s of CPU a ChemLab, 16 ChemLabs.
- **The demo session played about 19 times**: lab.9, notices.1, the six cuts of D2 (lab.14), each of the O2 scan's
  twelve parts, and `demo_session`.
- **Worktrees shared nothing**: ccache's own cache full (5 GB, 13 % hits), its keys the worktree's absolute paths;
  each worktree compiled godot-cpp (1,089 files) again; the result cache was a file a worktree.

`gates.sh` now ends its summary with each ctest run's summed time and its slowest entries, so a slow new test is
seen at its first gate.

## 68.1 The hot code, bit for bit

- `check_all`: the totals once a step for C1 to C4; a species a cell does not hold adds nothing to them (exact: 0
  added is the sum); the ledger's replays (C5, T1) on one buffer a thread; P1 probes no equilibrium a cell can go
  neither way by (what it would conclude whatever the affinity). A test holds `check_all` to the checks one by one, in
  their order, on the sweeps' honest traces and on traces corrupted six ways (`sweeps: check_all reports exactly
  what the checks report one by one`).
- A step allocates nothing for its copies of the state: `World::close_step` and `Simulation::step` into a trace kept
  from step to step, its buffers swapped and copy-assigned.
- `sealed_cells` floods along each cell's own gas edges (it scanned every edge at every cell it reached).
- `Lab::open` takes each vessel's shape from `glass::find`, made once a process.
- `Vessel::level`: the volume under a height from a running sum of the pieces wholly below (the same additions in
  the same order), the search stopping where its midpoint is an end (every later step would choose the same), and
  the cut piece's own search reading again the points of its last search where it turns the same way.
- A screen takes the lab's view once a change (`Lab::stamp`, another after every command or advance and no two
  labs' alike): a tutorial's frame took it twice a second of the clock, every vessel's bubbles and level worked out
  again. (A level remembered for the shapes `glass::find` keeps was tried, and bought nothing: an open vessel's water
  changes a little every step, so its level is asked at a new volume each time.)

Measured, instructions retired (`time -l`), back to back with main: the checked demo session 2.03e12 to 1.35e12 (its
CPU 212 to 120 s) before P1 and `sealed_cells`; trash.2 4.43e11 to 2.76e11; experiments.58 2.05e11 to 1.33e11. The
release ctest whole: 833 s with the machine shared, `demo_session` 128 s (941 s at Stage 62's gate). With the view
taken once a change and the cut piece's points read again: experiments.58 2.05e11 to 8.07e10, experiments.60 1.02e12
to 4.94e11 (its CPU 200 to 76 s); after them the plays' view is still two thirds of their time (the level's search
and the bubbles' physics, worked out for every vessel at every step).

## 68.2 The demo played once

`test_demo_record.cpp` plays the session once, unchecked (D1; `demo_session` checks every step of it), and holds what
lab.9 and notices.1 held: the golden record digest for digest, the results, the notices. After every command it
keeps, as the ctest fixture `demo`, the screen's scene (`tests/support/scene_file.hpp`, read back and held to what was
written; a member added to `OpticsScene` fails the build until the file holds it) and where the flask is.

- The O2 scan (optics.11, twelve parts) reads each command's scene and plays nothing. 207 to 395 s a part to 33 to
  69 s.
- D2 (lab.13) was made to load the fixture's saves too, and was put back: nine tenths of the record's steps lie after
  every cut but the last (commands 17 to 20 hold 770,000 of its 866,000 steps), so a save read there saved a tenth
  of a play and cost the wait for a whole one (under the sanitizers, the gate's last 1,100 s were the fixture's play
  and then D2's). Each cut plays its own way to its command again.
- A save is not the screen's scene: a loaded lab has no last step's flows, so its first view shows no boiling's
  bubbles (open, below). The scenes are kept as the session's screen made them, not made again from a save.

## 68.3 Checks where another run checks the same states

- trash.2: the stowed lab unchecked; the test holds the two labs one physical state minute by minute, and the trashed
  one is checked at every step.
- walls.4: the first hour of the cool-down checked (where the floor, the rings and the water trade their heat fastest),
  its last five hours of slow decay not.
- controller.1: its three speeds side by side on three threads (`// ctest: threads 3`), each its own lab.
- The experiments' plays (strict, at a person's pace, free): after 68.1 `check_all` is 5 to 7 % of them, their view
  two thirds, so checking them once less would buy little. They keep their checks.
- signals.12 (three monitors' cost at 600x, under 3 ms a frame by the thread's own time): the best of five rounds,
  not three. Of three, main's own build read 3.5 ms in one run of four under a gate's load.

## 68.4 The sanitizer build's labs unchecked

`LAB_CHECK=0` (`ASAN_LAB_CHECK`, `make asan-gate`): no lab of the process checks an invariant, whatever it is asked.
The release build checks every invariant on the same trajectories; `chem_tests` keeps every check under the
sanitizers, so `check_all` still runs instrumented. A deliberate read past a vector's end put in `Lab::view` was
caught by the sanitizers in a lab case under `LAB_CHECK=0`. `demo_session` does not run under the sanitizers:
unchecked there, it was the demo record's play again (567 s and 592 s of the sanitizers' run); the C API's path
under them is `pressure_session`'s, a new entry in both trees (until then only `make sessions-check` played it).

## 68.5 The Godot suite

Each test's ChemLab opens no catalogue (`experiments_path` and `progress_path` empty): only the test that lists the
experiments makes one that does. With 68.1, the shapes are made once a process. 1,396 s to 19 s.

## 68.6 The result cache

`tools/gate_cache.py`: a merge gate takes only a pass recorded at a merge gate (`GATE_LEVEL`), so `make gates` run
again after a failure runs what failed and what changed; one cache file for every worktree, under a lock; the ASan
tree's entries cached the same way. A load threshold when asked (`CTEST_LOAD`, `GATE_TEST_LOAD`: ctest's
`--test-load`): four ctest runs of twelve, from three worktrees, were seen at a load of 106 on twelve cores. Off by
default: ctest then starts no entry at all while another's run keeps the load up, and a gate waited out another
worktree's whole run doing nothing (at twelve, and still at 24).

## 68.7 One build cache for every worktree

The project's own ccache (`~/Library/Caches/chem-game-v2/ccache`, 40 GB), its paths relative to the worktree's root
(`base_dir`) and the working directory out of its keys; the compiler's version in them. No worktree's path in an
object: `-fdebug-compilation-dir=.`, the repository's root found at run time (`tests/support/repo_root.hpp`),
`demo_session`'s paths from its tree. (`-oso_prefix` made the binaries themselves alike too, but a sanitizer's report
and `atos` lost their lines with the debug map's paths relative. The result cache hashes a binary by its sections'
names and contents instead, its code and data, not its header nor its link-edit data, where the debug map's paths,
its UUID and its signature are.) godot-cpp's 1,089 files are compiled once for
every worktree. Held: a new worktree on the branch's head built its core tree with 99 of 134 compiles from the
cache (the others' sources not compiled anywhere yet); after the last change to the flags, its `chem_tests`,
`lab_tests` and `chem_tool` read the same to the result cache as this worktree's (main's `chem_tests` does not).

## 68.8 The gates

`GATE_CACHE=0 make gates`, the machine shared with another worktree's gate at its start (a load of 16 to 35): 1,307 s,
every gate passed. The release ctest 614 s (6,683 s summed; Stage 62: 1,389 s, 16,067 s), the sanitizers' 1,306 s
(12,148 s; 2,122 s, 22,908 s), the Godot suite 11 s (1,396 s). The sanitizers' run is now the gate's length, bound by
its work, its longest entries the demo's record (592 s) and the plays through the screen (ui.9, the experiments,
the tutorials: 350 to 530 s each, their time the view of every frame). `make gates` again with nothing changed but
a new entry: 20 s (573 and 555 entries passed at the merge level, 11 run).

Not done, and where the time is next: the experiments' and tutorials' plays spend two thirds of their time in the
screen's view (each vessel's level and bubbles worked out at every step); the sanitizers' run could run the three
variants of each experiment once, or the plays more sparsely, if the user wants less of them there.

## Open

- **D2 does not hold for a save just after the demo's command 1** (the malachite tipped into the flask, no step run
  since the heat was set): loaded, command 2's step gives another digest; the flask's rings' conductances to the room
  differ in their last digits after it. Every other command's save continues as the record does (held at every
  command while 68.2 was made; the D2 test keeps the six cuts it always had). Something a tip and the next step's
  walls share is not in the save. Found by this stage, not made by it (main's lab does the same).
- **A loaded lab's first view has no boiling's bubbles**: they are made from the last step's flows, which a save does
  not hold; the next step makes them again.
