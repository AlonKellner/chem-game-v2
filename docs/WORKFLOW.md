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

## The conservation sweep

`sweeps: conservation holds on every step of every random world` (in `chem_tests`) builds random
species tables and worlds and drives them with random primitive calls, honest and dishonest.
`CHEM_SWEEP_WORLDS=n` sets the number of worlds (default 2000; the sanitizer build runs
`ASAN_SWEEP_WORLDS`, default 300). A failure names its seed.

`sweeps: heat flows downhill and entropy grows on every step of every random thermal world` does
the same for worlds with temperatures: random matter at 80–3000 K, reservoirs, conductances from
zero to 1e9 W/K, the topology changing between steps.

## ctest entries

| Entry | Checks |
|---|---|
| `chem_tests`, `lab_tests` | the core's and the lab layer's doctest cases |
| `capi_c_smoke` | a C11 host compiles against `chem_api.h` and links the shared library |
| `chem_tool_version`, `chem_tool_tables` | the headless host runs and prints the built-in tables |
| `chem_tool_study_cooling` | the cooling study runs with every invariant checked on every step and ends with the world's totals unchanged |
| `datagen_refuses_*` | `chem_datagen` refuses data that breaks a conservation law (an element or the charge out of balance, an unknown species, a malformed formula), so such data does not build |
| `*_no_engine` | core, lab, C API and headless host name no game engine |
| `core_no_libm` | the core calls no C-library transcendental function |
| `godot_adapter_includes` | the Godot adapter includes only `chem_api.h`, godot-cpp, the standard library, its own headers |
| `gdscript_budget` | at most 300 lines of GDScript outside tests and addons |
| `godot_api_matches_binary` | the installed Godot's API equals the file godot-cpp generates bindings from |

## Goldens

Files under `bench/` are committed evidence of behaviour (session records, reference values). The
`goldens` gate fails while any differ from the index. Re-recording one is a behaviour change: show
what changed and why, get the user's approval, then commit it.
