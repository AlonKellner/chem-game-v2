# Workflow

## The three build trees

| Tree | Built by | Contains | Used for |
|---|---|---|---|
| `native/build-core` | `make core` | the tests, `chem_tool`, the C API; no godot-cpp | the inner loop and every ctest gate |
| `native/build` | `make native` | the GDExtension Godot loads; no tests | running the game, the Godot suite |
| `native/build-asan` | `make native-asan` | the tests under ASan+UBSan, Debug at -O1; no godot-cpp | the merge gate |

The first `make native` compiles godot-cpp (about 1100 files) with 4 jobs; later builds use every core.

## The three gates

| Command | When | What |
|---|---|---|
| `make check` | while working | `make core`, then ctest (`AREA=core\|lab`, or `TC=<doctest filter>`) |
| `make gate` | before every commit | core, ctest, the extension, the Godot host's suite, goldens unchanged; through the result cache |
| `make gates` | before a merge or a stage close | the same uncached, then ctest under ASan+UBSan |

`SKIP_GODOT=1 make gate` runs every engine-free gate. Every gate runs even after a failure; the
summary table gives each gate's seconds and result, and the log directory.

## The result cache

`tools/gate_cache.py`: a ctest entry whose binary, arguments, environment (`CHEM_*`, `LAB_*`) and
repo data (`data/`, `bench/`) hash the same as on a run where it passed is not run again. Lints
always run. `make gates` never reads the cache. `make gate-clear-cache` empties it.

## What keeps the gates quick

- **A ctest entry for each test source file** (`chem_tests.<file>`, `lab_tests.<file>`, by doctest's
  source-file filter): ctest runs them side by side, and the result cache keeps each apart.
  `chem_tests.every_file_runs` and `lab_tests.every_file_runs` fail if a test case is in a file no
  entry runs.
- **The reference's heavy loops are dealt among threads** (`in_pieces`,
  `native/game/src/optics_reference.cpp`): the truth of a pixel's area (thousands of exact rays),
  the O8 check's scan, the renderer's frames. A piece is the same piece on every machine and
  results are joined in the pieces' order, so a result does not depend on the number of threads.
  `LAB_THREADS=n` sets it (default: half the machine's, six at most); the five heavy `lab_tests`
  entries run with four, and ctest counts each as four processors.
- **The sanitizer build samples the heaviest comparisons sparsely**: `LAB_SPARSE=n` takes every
  n-th sample across in the tests that hold every pixel to 64 traced rays
  (`native/tests/support/sparse.hpp`; `ASAN_SPARSE`, default 3), as it runs fewer sweep worlds.
  The release ctest runs every sample.
- **The sanitizer build is optimized (-O2)**, still a Debug build whose assertions abort: the
  sanitizers instrument the same code. Its tests ran 237 s at -O1 and 102 s at -O2 (their summed
  work 1,649 s and 470 s; the machine was busier for the first).
- Measured (Stage 22, a machine busy with other work): the lab tests in one process, 368 s; as
  entries side by side with threads, 112 s. The closing gate (`make gates`): 37 min at the end of
  Stage 21; 8 min with the entries and threads; then the sanitizer build at -O2.
- **The GPU's cost is compared back to back**: the same machine reads a restarting pass from 1.6 to
  2.3 ms as its state drifts over an hour. A change's cost is the difference between it and the
  code before it, measured one after the other, three times each (`make pass-cost`).
- **Next, if it is needed**: the sanitizer run's longest entry is `lab_tests.optics` (98 s), whose
  tests could be split into two files.

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
| `chem_tests.<file>`, `lab_tests.<file>` | the core's and the lab layer's doctest cases, an entry a source file; `.every_file_runs`: no test case is left out |
| `capi_c_smoke` | a C11 host compiles against `chem_api.h` and links the shared library |
| `chem_tool_version`, `chem_tool_tables` | the headless host runs and prints the built-in tables |
| `demo_session` | the 3-stage demo, played through the C API with every invariant on every step, reproduces its golden record digest for digest |
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
