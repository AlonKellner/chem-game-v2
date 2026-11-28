# Workflow

## The three build trees

| Tree | Built by | Contains | Used for |
|---|---|---|---|
| `native/build-core` | `make core` | the tests, `chem_tool`, the C API; no godot-cpp | the inner loop and every ctest gate |
| `native/build` | `make native` | the GDExtension Godot loads; no tests | running the game, the Godot suite |
| `native/build-asan` | `make native-asan` | the tests under ASan+UBSan, Debug at -O2; no godot-cpp | the merge gate |

The first `make native` compiles godot-cpp (about 1100 files) with 4 jobs; later builds use every core.

## The three gates

| Command | When | What |
|---|---|---|
| `make check` | while working | `make core`, then ctest (`AREA=core\|lab`, or `TC=<doctest filter>`) |
| `make gate` | before every commit | core, ctest, the extension, the Godot host's suite, goldens unchanged; through the result cache |
| `make gates` | before a merge or a stage close | the same, its cache taking only what passed at a merge gate, and ctest under ASan+UBSan side by side with it from the start: of the sanitizers', the entries of 30 s or less in the release tree and 20 s or less under them; the truth's heaviest comparisons every third sample (`LAB_SPARSE=3`) |
| `make gates-full` | before a release, and when a change touches what only the long plays reach (Stage 68) | the merge gate with every sample (`LAB_SPARSE=1`) and every entry under the sanitizers |

`SKIP_GODOT=1 make gate` runs every engine-free gate. Every gate runs even after a failure; the
summary table gives each gate's seconds and result, the log directory, and each ctest run's summed
seconds and its slowest entries (Stage 68: a slow new test is seen at its first gate).

## One gate at a time

A gate fills every core, and several sessions work on this machine, each in a worktree of its own. Three gates
side by side took three times as long each (2026-11-28: a load of 60 to 77 on 12 cores; a commit gate of 9 minutes
took 29), and tests that hold a time budget failed for the load alone. So a heavy run waits its turn
(`tools/gate_queue.sh`): `gates.sh` at every level, `make quick` and `make check`'s ctest take a ticket in a queue
every worktree shares (`~/Library/Caches/chem-game-v2/gate_queue`) and run in the order they asked, one at a time.
A waiting run names what it waits behind and since when.

- A ticket is a file named by the second it was taken and its process. A ticket whose process is gone is swept by
  whoever looks next: a run that was killed holds nobody.
- The wait is bounded: after an hour (`GATE_QUEUE_WAIT_S`) a run goes on beside what is there, saying so.
- `GATE_QUEUE=0`: no queue, as before. A run started by a queued run (a gate's own ctest) asks no more
  (`GATE_QUEUE_HELD`).
- Not queued: builds (`make core`, `make native`; the build cache is shared), one doctest filter
  (`make check TC=`), a single binary run by hand, the game.
- It helps only once every worktree's branch has it: a branch without it runs beside the queue.
- ctest `gate_queue` (`tools/test_gate_queue.sh`) holds it to this: the order, no overlap for runs asked at once,
  a dead ticket, the command's status, the bound.

## The result cache

`tools/gate_cache.py`: a ctest entry whose binary, arguments, environment (`CHEM_*`, `LAB_*`) and
repo data (`data/`, `bench/`) hash the same as on a run where it passed is not run again. Lints
always run. `make gate-clear-cache` empties it.

- **The data's key is its content's** (2026-11-28): each file's blob id as the working tree has it, the same
  edited, staged and committed (`fingerprint`; ctest `gate_cache_fingerprint`, `tools/test_gate_cache.py`). Before,
  the index's ids and the edited files' own hashes were two spellings of one tree: the gate before a commit that
  touched `data/` and the gate after it shared nothing, and the second ran all 1065 entries again.

- **Two levels** (Stage 68, `GATE_LEVEL`): a pass at a merge gate (`make gates`) is kept as one, and as
  a commit pass; `make gates` takes only a merge-level pass, `make gate` either. A failure forgets the
  entry's key at both. So `make gates` run again after one entry failed runs that entry and what
  changed, not the hour. `GATE_CACHE=0` runs everything (and records what passed).
- **Measured costs for every worktree** (Stage 70): each tree's measured costs (`CTestCostData.txt`) are folded into
  its kind's shared file (`~/Library/Caches/chem-game-v2/costs/`) after each run, and a tree is given what others
  measured that it has not before it runs: a new worktree's ctest starts the longest first, and the sanitizers'
  limits leave out the same entries at its first gate.
- **One cache for every worktree** (Stage 68): `~/Library/Caches/chem-game-v2/gate_cache.json`
  (`GATE_CACHE_FILE`), read and written under a lock. An entry is known by its tree's kind
  (`build-core`, `build-asan`) and name; the ASan tree's entries are cached the same way (`make asan-gate`).
- **The same binaries in every worktree, as the cache reads them** (Stage 68): the sources' paths are
  relative (ccache's `base_dir`), the debug info's directory is `.` (`-fdebug-compilation-dir`), and the
  repository's root is found at run time from where the binary is (`tests/support/repo_root.hpp`;
  `CHEM_REPO_ROOT` in the environment overrides it). The debug map still names each object by its whole
  path (relative ones, `-oso_prefix`, left a sanitizer's report and `atos` without a line), and the cache
  hashes a binary by its sections' names and contents (its code and data; not its header nor its link-edit
  data, where those paths, its UUID and its signature are). So two worktrees on one commit share their results,
  and main after a merge whose tree is the branch's runs only what the branch did not.

## The shared build cache

Every tree compiles through the project's own ccache, `~/Library/Caches/chem-game-v2/ccache`
(`CHEM_CCACHE_DIR`, 40 GB, `CHEM_CCACHE_SIZE`; the user's own ccache is untouched; `-DCHEM_CCACHE_SHARED=OFF`
for the plain one): its paths relative to the worktree's root and the working directory left out of
its key, so a new worktree compiles from what another already compiled, godot-cpp's 1,100 files among
them. The compiler's version is in the key (`/usr/bin/c++` is a shim).

## What keeps the gates quick

- **A ctest entry for each test case of the lab's tests** (`lab_tests.<file>.<n>`, the n-th case of the
  file, by doctest's `--source-file` with `--first` and `--last`; Stage 40), and for each source file of
  the core's (`chem_tests.<file>`): ctest runs them side by side, the longest first once it has their
  times (`Testing/Temporary/CTestCostData.txt`), and the result cache keeps each apart. The cases are
  counted from the source's lines that start with `TEST_CASE(`; `lab_tests.<file>.count` fails if
  doctest counts another number (configure again: an edit to a test file does it), each entry fails if
  it ran no case, and `*.every_file_runs` fails if a test case is in a file no entry runs.
- **Marks before a test case** (`native/CMakeLists.txt`, `chem_add_cases`): `// ctest: parts N` makes it N
  entries (`lab_tests.<file>.<n>.<i>`), each run with `LAB_PART=i/N`, and the case does the i-th share of
  its heaviest loop (`native/tests/support/part.hpp`: `part().mine(k)`, by a hash of k, so the dear items
  that lie together are shared out). Only a loop whose every item is checked alone is parted: a statistic
  over the loop (a mean, a count of visible pixels) is not the same over a part. `// ctest: threads N` runs
  it on N threads (`LAB_THREADS`), and ctest counts it as N processors. Every other case keeps to one
  thread: side by side the cases fill the machine, and a thread more a case mostly waits.
- **The reference's heavy loops are dealt among threads** (`in_pieces`,
  `native/game/src/optics_reference.cpp`): the truth of a pixel's area (thousands of exact rays),
  the O8 check's scan, the renderer's frames. A piece is the same piece on every machine and
  results are joined in the pieces' order, so a result does not depend on the number of threads.
  `LAB_THREADS=n` sets it (default: half the machine's, six at most); ctest gives each case one, or
  what its mark says.
- **A load threshold, when asked** (Stage 68, `CTEST_LOAD=n`, `GATE_TEST_LOAD=n`: ctest's `--test-load`): no entry
  starts while the machine's load is over n. Four ctest runs of twelve, from three worktrees, were seen at a load
  of 106 on twelve cores. It is off by default: ctest starts none at all while the load stays up, its first among
  them, and a gate waited out another worktree's whole run (one without the threshold) doing nothing.
- **What a case needs that the build can make is made at build time** (Stage 68): the glassware's shapes
  (`chem_glassgen`, `data/glassware.json`: `glass::find` takes them; a test holds each to `glass::make` to the last
  bit). Making them was more than half of a short case's time, in every process.
- **The gates sample, a full gate takes all** (Stage 68): `make gates` runs the truth's heaviest comparisons at
  every third sample (`GATE_SPARSE`, `LAB_SPARSE=3`, as the sanitizer build always has; a bound on how many were
  compared scales with `sparse()`) and, under the sanitizers, the entries of 30 s or less in the release tree and
  20 s or less under the sanitizers (`ASAN_MOST_S`, `ASAN_OWN_MOST_S`, by ctest's own measured costs,
  `CTestCostData.txt`, `gate_cache.py --limit`; an entry not yet measured runs; one that needs a fixture whose
  setup is left out is left out with it), the rest named in the summary as left to `make gates-full`. A long play
  reaches the code's paths its short cases reach, and plays them longer. As the game grows the gates' sanitizer
  run grows with its short cases only; `make gates-full` runs everything.
- **A play waits a second at a time** (Stage 68): a tutorial followed, strictly or free, or played as a person,
  waits on the lab a second's frame at a time, the screen's view taken once a frame, not once a step (a wait ends
  at most a second later than in frames of 1/60 s; every experiment's and tutorial's results held so). The
  person's gestures keep their frames.
- **The budget** (Stage 68): the summary names every release entry over `GATE_BUDGET_S` (60 s). A new long one
  takes one of the ways here: a fixture, parts, `sparse()`, coarse frames where only its end is held.
- **The step's cost at every merge gate** (Stage 75): `make gates` plays the demo once unchecked beside ctest
  (`chem_tool bench`, about a minute of one core) and prints its instructions a step, against main's where the branch
  left it: `step cost: N instructions a step (demo), +X % against main's at <sha>`, and a `!!` line past
  `GATE_STEPCOST_WARN` percent (10). It never fails the gate. The counts are kept in
  `~/Library/Caches/chem-game-v2/step_costs.json` by the binary's content and the data, with the source tree when
  it was clean, so an unchanged binary is not played again and a branch's last merge gate gives main its count.
  `make gate` takes it with `GATE_STEPCOST=1`.
- **The Godot gate builds and runs only what needs the engine** (Stage 84): the extension (`make native PICTURES=0`),
  Godot's import and the GUT suite. The supplies' pictures are the native layer's (`chem_tool capture`, held by the
  native tests); the suite reads none. Capturing them was about 180 s of a new worktree's gate, now none.
  `make native` still captures them for the game. The rule for what goes where is the repo's: native first, a thin
  Godot host.
- **The supplies' pictures are shared by every worktree** (Stage 93): kept by their keys (each holds the renderer's) in
  `~/Library/Caches/chem-game-v2/pictures/RENDERER/KEY.json` (`PICTURES_STORE` over it), so a tree captures only what
  no tree has: a branch that changed the renderer leaves its pictures there for main after the merge, and one taken
  back finds the old ones. `make pictures` builds only `chem_tool`, lists every bench's supplies and their keys
  (`chem_tool capture --keys`), captures each missing picture alone, side by side (`--only`), and assembles the tree's
  file from the store (`tools/pictures.py`); a renderer unused for 30 days is removed. Cold: 68 s for 50 pictures at a
  load of about 30; warm: under a second (ctest `pictures_store`).
- **A cost is held by the work, not the clock** (Stage 74): a test that bounds what something costs compares the
  instructions the process retired (`proc_pid_rusage`, `ri_instructions`; `test_signals.cpp`'s `work_done`), as a
  share of the run it adds to. On a gate's crowded machine a thread moves between the performance cores and the
  efficiency cores, two to three times slower, so even its own CPU time swings by more than a small bound (signals.12
  failed three gates in five at a load of 20 to 50); the instructions moved by 0.1 % under the same load.
- **A check once a step**: a test that holds each step's trace to every invariant itself leaves the simulation's own
  checking off (`test_column.cpp`, Stage 70: each step was checked twice).
- **A simulation several cases start from is played once** (Stage 68): a ctest fixture. `// ctest: fixture setup
  NAME` before the case that plays it, `// ctest: fixture NAME` before each that reads it (`chem_add_cases`;
  FIXTURES_SETUP and FIXTURES_REQUIRED): ctest runs the setup first, and runs it whenever a case that needs it
  runs (the result cache's subsets too). Its files are in the tree's `fixtures/NAME` (`tests/support/fixtures.hpp`).
  The demo's (`test_demo_record.cpp`): the session played once, unchecked, its golden digests, results and notices
  held; after every command the lab's save and report, and the windows D2 plays from them (`windows.json`: a wait of
  more than 100,000 steps in two halves, a save between them; Stage 70), checked, together the gates' one checked
  play of the session, eight entries side by side; after every command the screen's scene (`tests/support/scene_file.hpp`, read back and held to what was
  written). The O2 scan's twelve parts read the scenes and play nothing (each part played the whole session before:
  207 to 395 s a part, 33 to 69 s now). A fixture is worth it where what it saves is most of the play: D2 read its
  saves once with each cut playing to the end, nine tenths of the steps after them, and waited a whole play for
  nothing; in windows from the saves, each cut to the next, it reads them again.
- **The sanitizer build's labs check no invariant** (Stage 68, `LAB_CHECK=0`, `ASAN_LAB_CHECK`): the release build
  checks every invariant at every step of the same trajectories (a lab's state is the same checked or not, D1),
  and `chem_tests` keeps every check under the sanitizers, its self-tests on corrupted traces and the four sweeps
  among them, so `check_all` itself still runs instrumented. The sanitizers want the code's paths; checking was
  60 % of their time.
- **The sanitizer build samples the heaviest comparisons sparsely**: `LAB_SPARSE=n` takes every
  n-th sample across in the tests that hold every pixel to 64 traced rays
  (`native/tests/support/sparse.hpp`; `ASAN_SPARSE`, default 3), as it runs fewer sweep worlds; and
  of a parted case only its first part, on four threads. The release ctest runs every sample.
- **The sanitizer build is optimized (-O2)**, still a Debug build whose assertions abort: the
  sanitizers instrument the same code. Its tests ran 237 s at -O1 and 102 s at -O2 (their summed
  work 1,649 s and 470 s; the machine was busier for the first).
- Measured (Stage 22, a machine busy with other work): the lab tests in one process, 368 s; as
  entries side by side with threads, 112 s. The closing gate (`make gates`): 37 min at the end of
  Stage 21; 8 min with the entries and threads; then the sanitizer build at -O2.
- **The GPU's cost is compared back to back**: the same machine reads a restarting pass from 1.6 to
  2.3 ms as its state drifts over an hour. A change's cost is the difference between it and the
  code before it, measured one after the other, three times each (`make pass-cost`).
- **The replays of the demo that hold digests run unchecked**: the lab's state is the same checked or not
  (D1), and `lab_tests.demo_record` checks every invariant at every step of the same session. So the cases that
  hold the record's digests (`lab: the demo session reproduces…`, `lab: a saved lab, loaded, continues…`)
  and the screen's O2 scan through the demo do not check each step again.
- Measured (Stage 40, M3 Pro, `docs/TDD_40_GATES.md`): `make gates` 614 s before (the release ctest 165 s,
  its last 100 s the one entry `lab_tests.optics` alone; then the sanitizers' 431 s, its last minutes the
  same entry); 320 s after, the release ctest and the sanitizers' together keeping the machine full; 284 s
  with O2 read at each nanometre (it read each half); 276 s with `kept` and the prism's slit sparse under
  the sanitizers.
- **Next, if it is needed**: the two runs end together (224 s and 274 s); what is left is their work. The
  sanitizers' longest entries are the prism's bands (140 to 180 s: a reference of 4,000 wavelengths a band).

## The conservation sweep

`sweeps: conservation holds on every step of every random world` (in `chem_tests`) builds random
species tables and worlds and drives them with random primitive calls, honest and dishonest.
`CHEM_SWEEP_WORLDS=n` sets the number of worlds (default 2000; the sanitizer build runs
`ASAN_SWEEP_WORLDS`, default 300). A failure names its seed.

`sweeps: heat flows downhill and entropy grows on every step of every random thermal world` does
the same for worlds with temperatures: random matter at 80–3000 K, reservoirs, conductances from
zero to 1e9 W/K, the topology changing between steps.

`sweeps: reactions run down their affinity and entropy grows on every step of every random
reacting world` adds reactions with random rates; random formation enthalpies and entropies make
them spontaneous in either direction.

`sweeps: rigid cells stay exactly full on every step of every random world with volumes` makes
about half the cells rigid, lets the edges pass gas, and gives every vessel a wall.

In the reacting sweeps one world in three has a reaction of the equilibrium law, and P1 is checked.
`CHEM_SWEEP_DUMP=1` prints the failing step's ledger, cells and edges.

## ctest entries

| Entry | Checks |
|---|---|
| `chem_tests.<file>`, `lab_tests.<file>.<n>` | the core's doctest cases, an entry a source file; the lab layer's, an entry a test case (`.<n>.<i>`: a part of one); `lab_tests.<file>.count`: the file's cases are those counted; `.every_file_runs`: no test case is left out |
| `capi_c_smoke` | a C11 host compiles against `chem_api.h` and links the shared library |
| `chem_tool_version`, `chem_tool_tables` | the headless host runs and prints the built-in tables |
| `lab_tests.demo_record.1` | the 3-stage demo reproduces its golden record digest for digest, its results and notices as measured, and keeps the screen's scenes for the O2 scan and the saves D2's windows start from (Stage 68; `demo_session` before) |
| `lab_tests.lab.13.<i>` | D2: the demo loaded at a cut plays on to the next, every invariant on every step, into the record's digests (together, every step of the session checked) |
| `pressure_session` | the pressure session through the C API (`chem_tool run`), every invariant on every step, against its own record (Stage 68) |
| `chem_tool_study_cooling`, `_decomposition`, `_balloon`, `_distillation`, `_dissolution`, `_crystals` | the studies run with every invariant checked on every step and end with the world's totals unchanged |
| `chem_tool_study_spectrum` | the band-count measurement runs, and the game's bands are within the colour threshold (GAME_SPEC §12.2) |
| `datagen_refuses_*` | `chem_datagen` refuses data that breaks a conservation law (an element or the charge out of balance, an unknown species, a malformed formula), so such data does not build |
| `*_no_engine` | core, lab, C API and headless host name no game engine |
| `core_no_libm` | the core calls no C-library transcendental function |
| `godot_adapter_includes` | the Godot adapter includes only `chem_api.h`, godot-cpp, the standard library, its own headers |
| `gdscript_budget` | at most 300 lines of GDScript outside tests and addons |
| `godot_api_matches_binary` | the installed Godot's API equals the file godot-cpp generates bindings from |

## Goldens

Files under `bench/` are committed evidence of behaviour. `bench/sessions/<name>.jsonl` is a
session's record: one line per command with the step it ended on and the SHA-256 of the whole
state. `make sessions-check` replays every session against its record; the `goldens` gate fails
while a file under `bench/` differs from the index.

Any change to the physics, the data or a session's script changes its record. Re-recording is a
behaviour change: show what changed in the measured results (`make demo`) and why, get the user's
approval, then `make sessions APPROVED=1` and commit the record with the change.
