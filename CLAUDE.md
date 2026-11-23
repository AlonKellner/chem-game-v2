# Chem Game v2

A chemistry lab where simulated materials follow mechanical, physical and chemical laws, with
conservation laws enforced as invariants. Design: `GAME_SPEC.md`.

## Stack

- **Game**: engine-free native C++20 (`native/core`, `native/game`) behind a C API (`native/capi/include/chem_api.h`)
- **Engine host**: Godot 4.7 (`engines/godot`; the host is one GDExtension node, `native/hosts/godot`)
- **Testing**: doctest/ctest (`chem_tests`, `lab_tests`, a ctest entry a source file); GUT 9.6.0 for the Godot host only
- **IDE**: VSCode

## Architecture

Layers with a strict dependency direction: core -> lab -> C API -> hosts. Everything but `native/hosts/godot` and
`engines/godot` is engine-free (ctest `*_no_engine`); the Godot host includes only `chem_api.h` (ctest
`godot_adapter_includes`) and the Godot project holds at most 300 lines of GDScript outside tests (ctest `gdscript_budget`).

- `native/core/include/chem/` — The simulation kernel, namespace `chem` (GAME_SPEC §3–§12). `quantity.hpp`: integer amounts (1e-12 mol) and energy (1e-9 J). `species.hpp`: species tables and `Reaction`, which cannot be constructed unbalanced. `world.hpp`: cells with private state, edges, the two primitives `transfer` and `transform`, refusals, the ledger and `StepTrace`; a rigid cell with no gas path to an elastic one is sealed (`CellMeta::sealed`, kept by the world); a drain's rate, the share of the solids its liquid carries and the solids' own rate (`set_drain`); work along an edge (`set_power`: its source a reservoir so hot its energy carries no entropy). `thermo.hpp`: what state implies (heat capacity, temperature, entropy, volume, activity from ions with Pitzer's excess, molality, affinity; a sealed cell's pressure, its energy `U + P_atm V`). `math.hpp`: the core's own `exp` and `log`. `optics.hpp`: spectra at 1 nm (the CIE observer, D65, each species' medium, pure water as measured), narrow lines beside them, the reference colour, each light's energy table and the estimate by energy bands with moving points (GAME_SPEC §12.2); the first measurement's fixed bands (`game_bands()`, for `chem_tool study spectrum`). `sim.hpp`: `Simulation`, the fixed-step loop and its processes, applied in sequence (reactions with first-order and surface kinetics, crystal nucleation, heat conduction, work, liquid and solids down the drains, then settling: every cell to its fast equilibria such as boiling and condensation, every rigid cell exactly full); the last step's flows by process (`flows()`, GAME_SPEC §13.6). `invariants.hpp`: the invariant kernel (`check_C1`…`check_C8`, `check_T1`…`check_T3`, `check_S1`, `check_V1`…`check_V3`, `check_P1`, `check_X1`, `check_X2`, `check_all`, `Totals`). No engine, no C-library transcendentals (ctest `core_no_libm`).
- `native/datagen/` — `chem_datagen`: embeds `data/elements.json`, `species.json` (with each dissolved species' ions), `reactions.json`, `solutions.json` (Pitzer's parameters) and `data/optics/` in the core at build time, deriving atoms from formulas (`formula.hpp`) and refusing an unbalanced reaction (ctest `datagen_refuses_*`). Generated: `chem/builtin_ids.hpp` (species and reactions by name).
- `native/game/include/lab/` — The engine-free lab layer, namespace `lab` (GAME_SPEC §13). `lab.hpp`: `Lab`, a bench of equipment over one closed world, on a grid of squares (each item on a footprint), writing notices in levels of what happened (GAME_SPEC §13.4), with goals on the outbox's six slots (GAME_SPEC §13.5), and an item from the inside for the debug panel (`Lab::debug`: composition, flows, each reaction's rate, affinity and what limits it; GAME_SPEC §13.6), driven by JSON commands (`place`, `move`, `stow`, `trash`, `restore`, `heat`, `unheat`, `valve`, `stir`, `pour`, `tip`, `attach`, `detach`, `under`, `undrain`, `submit` (to a slot of the outbox), `goal`, `ungoal`, `wait`, `wait_until`; `hold`, `invert`, `unpour`, `cap` (pouring as a control: a held pour's `pour {from, rate}`, a placed pour's cap); signals: `part`, `scrap`, `sense`, `unsense`, `set`, `wire`, `unwire`, `rig`, `unrig`, `reset`, `ack`, `circuit`, `uncircuit`), with reports, events, a SHA-256 digest of the whole state (`sha256.hpp`), and save/load. `ui.hpp`: `Ui`, the screen (GAME_SPEC §13.1): a pointer in, lab commands, a draw list in two parts and the optical pass's scene out; every gesture is decided here, not in a host: the selection, the drawer of signal parts, wiring from ports, the part editor. `contents.hpp` (GAME_SPEC §11, Stages 30 to 32): what lies in a vessel at any turn, pure: the volume under a level, the liquid's surface, the sediment's flat section and pile, a vortex's cone and the energy it stands for (`swirl`), the lip and what runs out of a round mouth (`over_brim`); one model for the lab's pour by hand, its stirring and the picture; `check_H1`, `check_H2`. `tutorial.hpp`: `Tutorial` (GAME_SPEC §13.2): steps with checks read from the lab's view and the screen (the selection, the part editor), failures, a goal; drawn by the screen as a card and a pointer at the next gesture; while it runs the screen takes only its step's gestures and holds the clock for them. `conditions.hpp`: a quantity of an item measured from the lab's view (purity included), comparisons, the guided lists a sensing part's editor offers (kinds, materials, measures: what the item has now), and the conditions tutorials are written in (read from JSON by `src/conditions_json.cpp`). `signals.hpp` (GAME_SPEC §13.3): timers, monitors, controllers, alarms and notice parts, circuits that hold them, wires, rigs (a controller's follow its level, a valve to it, stirring by PWM) and what a monitor or a controller senses; `evaluate`, one step's signals, synchronous; `check_G1` to `check_G4`. `control.hpp`: a controller's tuning, pure: a step's response fitted (first order with a delay, or an integrator), SIMC's PI gains (Skogestad 2003) for a speed, the loop's margins; the lab tries what a controller controls on two copies of itself to get the response. `colour.hpp`: CIE L\*a\*b\*, CIEDE2000, the eye metric fitted to the user's marks (`eye_difference`, visible above 8), OKLCh gamut mapping, what the screen shows, sRGB, and the fixed bands' measured error. `optics_trace.hpp` (GAME_SPEC §12.3): the tracer (bodies as surface patches, 3D rays, float64, on the CPU, never in a pixel's loop) and a body's refraction map (`build_map`: samples across on the screen's quarter pixels, each pixel's level, cubic pieces in wavelength). `optics_scene.hpp`: the renderer's scene from live state (`optics_compose`: bodies, spectra at 1 nm, lines, the room light's energy table, each refracting body's map, the wall as a table of exact integrals; `OpticsBody::layer`: what is held stands a gap nearer the eye, in one world of the screen's pixels; the stacks: a held vessel's rays followed through the bodies behind it by their maps, on the CPU, into a table (`optics_stacks`); the rectangles where the pass's second pass runs), the reference renderer (the truth at a pixel, and `OpticsFrames`: the renderer's own frames, each pixel's history), `optics_compare` (O4), the truth of a pixel's area by its own tracer (`optics_area_truth`), `check_O1`, `check_O2`, `check_O7`, `check_O8`; and the reading that traces a pixel's own rays in the pass (Stage 29; `optics_reading(5)` in a test, `LAB_BEAM=5` in a host; the only one that draws a vessel part turned and what lies in it as shapes): `check_O9`, `check_O10`, `check_O11`; and (Stage 33, the pass made fast: `docs/TDD_33_FAST.md`) a pixel's beams kept while its shape key is the same and the corners of neighbouring pixels shared (`OpticsFrames::keep`, `share`), `check_O12`, `check_O13`, `check_O15` (`src/optics_single.cpp`: the tracer of a shape's surfaces compiled in single precision). `play.hpp`: a play, gestures as data a frame at a time (`data/plays/`), what `make pass-cost` times and the tests count. `native/game/shader/optics_pixel.glsl`: the per-pixel method (GAME_SPEC §12.2), one source compiled as the Godot host's compute shader and, in float64, as the reference (`src/optics_reference.cpp`); it keeps to what GLSL and C++ read alike. A pixel is the light of all its rays (§12.3): it reads maps and the wall's table, and traces nothing. On the GPU a band costs what it could do, done or not, so the hot loop holds only what most pixels need (`plain_band`), and the host compiles the source twice: without `OPTICS_CHAIN` for the pass over the whole screen, with it for a second pass over what is held or is a prism (`general_band`: one vessel, any map; `stack_band`: a stack's table). Following a ray through the layers (`chain_at`, `OPTICS_FOLLOW`) runs on the CPU alone: per-ray arrays are a hundred times slower on the GPU than their arithmetic.
- `native/capi/` — `chem_api.h`: the game behind a plain C ABI (size-prefixed structs, borrowed views, statuses).
- `native/hosts/headless/` — `chem_tool`: the game without an engine: `version`, `tables` (the built-in species and reactions), `run SESSION [--record|--check FILE]` (a session script through the C API, every invariant checked, the measured results and the lab's warnings printed), `bench SESSION` (the same unchecked, timed), `study <name>` (a stage's physics alone against its closed form, on the core directly: `cooling`, `decomposition`, `balloon`, `distillation`, `dissolution`, `crystals`; `spectrum`, the band-count measurement; `tilt` and `vortex`, what lies in a vessel against its closed forms).
- `native/hosts/godot/` — The Godot host: `ChemLab`, a `Node2D` that reads the bench, forwards the mouse, calls a frame and paints: the draw list's first part, the optical pass (a compute shader over the screen and a second over the rectangles `cg_optics` names, `optics_compute.cpp`: the per-pixel source with its state in four float images on the GPU, fed `cg_ui_optics`, shown by a child `TextureRect`), the list's second part (`cg_ui_*`). No game logic.
- `engines/godot/` — The Godot project. `make native` builds the extension into `engines/godot/bin` and copies `data/` into `engines/godot/data` and the pixel shader source into `engines/godot/shaders`.
- `data/` — Source-of-truth data: `elements.json`, `species.json`, `reactions.json` (embedded at build), `benches/` (equipment and what it holds), `sessions/` (scripts), `tutorials/` (the demo as steps with checks). `bench/sessions/` — the sessions' golden records (a digest after every command).
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
make screenshots                # pictures of the host through the demo -> build/screenshots (one with a vessel held over another), the GPU's settled optical pass against the truth on the eye metric (O4) and its cost (headless first, then a window for a few seconds, time-limited)
make pass-cost                  # the optical pass's cost on the GPU (Stage 33): a play (data/plays/cost.json: settled, a vessel carried, poured by hand, stirred) timed mark by mark, then every history starting again; headless first, then a window, time-limited
make demo                       # the 3-stage demo, headless: the commands and what was measured
make demo-bench                 # its step cost
make sessions-check             # the sessions against their golden records (make sessions re-records: a behaviour change)
```

- Native tests: doctest `TEST_CASE`s in `native/tests/*.cpp` (`chem_tests`) and `native/tests/game/*.cpp` (`lab_tests`); names end with their stage, `(Stage N)`. Each source file is a ctest entry, run side by side; a test that holds many pixels to the brute-force truth leans on the reference's own threads (`LAB_THREADS`) and, under the sanitizers, samples sparsely (`sparse()`, `tests/support/sparse.hpp`): `docs/WORKFLOW.md`.
- Godot host tests: `engines/godot/tests/test_*.gd`, extending `GutTest`. The screen itself is tested natively (`native/tests/game/test_ui.cpp`), through `Ui::locate`.
- **A script that runs on Godot's own thread never waits unbounded**: every loop in `engines/godot/tools/` has a limit, and a windowed Godot started from a shell is given a time limit (`perl -e 'alarm 90; exec @ARGV' godot …`). An unbounded wait froze the window once.
- **Invariants over examples**: a property goes in the catalogue (GAME_SPEC §18) as a named check with a self-test that proves it fires on a corrupted trace.
- **Fetched data is documented and held to its source**: any data fetched from outside and used to change or fix the code (a measured or fitted value, a constant, a calibration) is documented in full (`docs/DATA_SOURCES.md`: the source, what was read, the fit, the confidence) and has a gate test that runs the model at the source's own conditions and checks what the source measured (the measured kinetics: `native/tests/test_measured.cpp`), so a change to the data or to the code that reads it fails the gate. Every number in `data/` has an entry in `data/sources.json` (measured with its tests and doc rows, calibrated with why, or design); ctest `data_sources` fails on one without, so new data cannot enter undocumented or untested.
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
