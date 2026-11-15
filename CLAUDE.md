# Chem Game v2

A chemistry lab where simulated materials follow mechanical, physical and chemical laws, with
conservation laws enforced as invariants. Design: `GAME_SPEC.md`.

## Stack

- **Game**: engine-free native C++20 (`native/core`, `native/game`) behind a C API (`native/capi/include/chem_api.h`)
- **Engine host**: Godot 4.7 (`engines/godot`; the host is one GDExtension node, `native/hosts/godot`)
- **Testing**: doctest/ctest (`chem_tests`, `lab_tests`); GUT 9.6.0 for the Godot host only
- **IDE**: VSCode

## Architecture

Layers with a strict dependency direction: core -> lab -> C API -> hosts. Everything but `native/hosts/godot` and
`engines/godot` is engine-free (ctest `*_no_engine`); the Godot host includes only `chem_api.h` (ctest
`godot_adapter_includes`) and the Godot project holds at most 300 lines of GDScript outside tests (ctest `gdscript_budget`).

- `native/core/include/chem/` — The simulation kernel, namespace `chem` (GAME_SPEC §3–§12). `quantity.hpp`: integer amounts (1e-12 mol) and energy (1e-9 J). `species.hpp`: species tables and `Reaction`, which cannot be constructed unbalanced. `world.hpp`: cells with private state, edges, the two primitives `transfer` and `transform`, refusals, the ledger and `StepTrace`. `thermo.hpp`: what state implies (heat capacity, temperature, entropy, volume, activity, affinity). `math.hpp`: the core's own `exp` and `log`. `sim.hpp`: `Simulation`, the fixed-step loop and its processes, applied in sequence (reactions with first-order and surface kinetics, crystal nucleation, heat conduction, liquid down the drains, then settling: every cell to its fast equilibria such as boiling and condensation, every rigid cell exactly full). `invariants.hpp`: the invariant kernel (`check_C1`…`check_C8`, `check_T1`…`check_T3`, `check_S1`, `check_V1`…`check_V3`, `check_P1`, `check_X1`, `check_X2`, `check_all`, `Totals`). No engine, no C-library transcendentals (ctest `core_no_libm`).
- `native/datagen/` — `chem_datagen`: embeds `data/elements.json`, `species.json`, `reactions.json` in the core at build time, deriving atoms from formulas (`formula.hpp`) and refusing an unbalanced reaction (ctest `datagen_refuses_*`). Generated: `chem/builtin_ids.hpp` (species and reactions by name).
- `native/game/include/lab/` — The engine-free lab layer, namespace `lab` (GAME_SPEC §13). `lab.hpp`: `Lab`, a bench of equipment over one closed world, driven by JSON commands (`heat`, `valve`, `stir`, `pour`, `tip`, `attach`, `detach`, `under`, `submit`, `wait`, `wait_until`), with reports and a SHA-256 digest of the whole state (`sha256.hpp`).
- `native/capi/` — `chem_api.h`: the game behind a plain C ABI (size-prefixed structs, borrowed views, statuses).
- `native/hosts/headless/` — `chem_tool`: the game without an engine: `version`, `tables` (the built-in species and reactions), `run SESSION [--record|--check FILE]` (a session script through the C API, every invariant checked, the measured results printed), `bench SESSION` (the same unchecked, timed), `study <name>` (a stage's physics alone against its closed form, on the core directly: `cooling`, `decomposition`, `balloon`, `distillation`, `dissolution`, `crystals`).
- `native/hosts/godot/` — The Godot host: `ChemLab`.
- `engines/godot/` — The Godot project. `make native` builds the extension into `engines/godot/bin` and copies `data/` into `engines/godot/data`.
- `data/` — Source-of-truth data: `elements.json`, `species.json`, `reactions.json` (embedded at build), `benches/` (equipment and what it holds), `sessions/` (scripts). `bench/sessions/` — the sessions' golden records (a digest after every command).
- `docs/DATA_SOURCES.md` — every physical value with its source and confidence. A value is used only once its row is verified.

## Testing

The protocol: `docs/WORKFLOW.md`.

```bash
make check                      # the engine-free build, then every ctest entry
make check AREA=core            # core | lab | all
make check TC="sweeps*"         # one doctest filter in chem_tests (CHEM_SWEEP_WORLDS=n scales the conservation sweep)
make quick                      # between commits: only what a change can affect (result cache)
make gate                       # before every commit: core, ctest, the extension, the Godot suite, goldens unchanged
SKIP_GODOT=1 make gate          # every engine-free gate
make gates                      # before a merge or a stage close: uncached, plus ASan+UBSan
make native                     # the Godot extension, data copy, import
./run_tests_parallel.sh         # the Godot host's GUT suite
make run                        # the game (the Godot host)
make demo                       # the 3-stage demo, headless: the commands and what was measured
make demo-bench                 # its step cost
make sessions-check             # the sessions against their golden records (make sessions re-records: a behaviour change)
```

- Native tests: doctest `TEST_CASE`s in `native/tests/*.cpp` (`chem_tests`) and `native/tests/game/*.cpp` (`lab_tests`); names end with their stage, `(Stage N)`.
- Godot host tests: `engines/godot/tests/test_*.gd`, extending `GutTest`.
- **Invariants over examples**: a property goes in the catalogue (GAME_SPEC §18) as a named check with a self-test that proves it fires on a corrupted trace.
- **Regression policy**: after Stage N, all tests from Stages 0 through N pass before proceeding.

## Stage Workflow

1. Spec first: the stage's section of `GAME_SPEC.md`, its invariants, its rows of `docs/DATA_SOURCES.md` verified
2. Tests, then the kernel green under `make check`; `make gate` before each commit (small green commits on a branch)
3. The lab layer, the C API, the hosts, as the stage needs
4. Goldens: a changed golden or session record is a behaviour change; bring the evidence and get the user's approval before re-recording
5. Docs: the stage's TDD doc (`docs/TDD_NN_<TOPIC>.md`, phase table), CHANGELOG, GAME_SPEC's version; `make gates` closes the stage
6. The user tests interactively (or reviews the headless report) and gives pass/fail feedback
7. Failing interactive tests become automated tests; fix and re-run until all pass

**No stage is complete without the user's sign-off.**

Commits: `Stage N: <behaviour sentence>` on a branch; merged to `main` as `Merge Stage N: <summary> (GAME_SPEC x.y.z)`.

## Code Style

- C++20, tabs, short comments naming the GAME_SPEC section
- Conserved quantities are integers (amounts in 1e-12 mol, energy in 1e-9 J); derived quantities are float64 in SI units
- Cell state changes only through `transfer` and `transform` (GAME_SPEC §4.3)
- Error handling: debug builds assert and abort on an invariant violation; release builds report and continue (`chem/error.hpp`; the C API returns statuses)

## Key References

- `GAME_SPEC.md` — the design: every system in full, each with the demo's subset; the invariant catalogue (§18)
- `docs/WORKFLOW.md` — gates and build trees
- `docs/DATA_SOURCES.md` — physical data and its sources
- `docs/TDD_00_BOOTSTRAP.md` onward — one document per stage
- Reference for conventions: the sibling repository `ricochet-game-v2`
