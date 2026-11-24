# TDD 41: Strict and free tutorials, and the experiments (Stage 41)

## The request

The user, 2026-11-24: "I want the tutorial to have a "strict" mode which is enabled by default. When strict,
the tutorial does not allow any unexpected user action. When non strict, the tutorial just highlights things,
but does not stop the user from doing any unexpected action. Let's plan the addition of more experiments,
similar to the current tutorial in that they will have step by step instructions and have an optional non
strict mode. The experiments should explore more characteristics which are currently missing from our
simulation [...] This is not the step for actual levels [...] but it is supposed to explore all of the
features which would be required for step by step chemical principles lessons."

The plan the user chose, 2026-11-24:
- **Scope.** Stage 41 builds the modes, an experiment picker, and three experiments on what the simulation
  already does. A roadmap covers the rest, with no stage numbers.
- **Free mode.** It only guides. The clock is the player's, and failures still end the run.
- **The switch.** It sits on the card and is the player's at any time.
- **The roadmap's areas.** Acids, bases and pH; precipitation and gas; boiling, bubbling and agitation;
  freezing and melting; pressure and temperature; humidity and saturation.

The spec: GAME_SPEC §13.2 (strict or free; results of a quantity), §13.3 (a strict tutorial and the signals),
§13.7 (new: the experiments) and §14 (the C API). Version 0.41.0. The roadmap: `docs/EXPERIMENTS_ROADMAP.md`.

| Phase | Topic | Status |
|---|---|---|
| 41.1 | Measured: the three experiments' physics, headless, against closed forms | Done (below) |
| 41.2 | Strict and free: the screen's mode, the card's switch | Done |
| 41.3 | The experiment picker: the catalogue, the list on the screen, the C API, the Godot host | Done |
| 41.4 | Three experiments: benches and tutorials; results of a quantity | Done |
| 41.5 | The roadmap of experiments | Done (`docs/EXPERIMENTS_ROADMAP.md`) |
| 41.6 | GAME_SPEC 0.41.0; `make gates`; the user's sign-off | |

## 41.1 Measured

**Heat of solution** (`chem_tool study solution_heat`, a ctest entry). The setup:
- The same 0.0501 mol of copper sulfate is tipped into 50 g of water in an 80 g glass beaker at 20 °C,
  stirred (x3).
- The heats of solution come from the tables' ΔHf alone: −64.59 kJ/mol for the anhydrous salt, +14.56 for the
  pentahydrate (25 °C). At 20 °C they are −65.16 and +13.02, since the heat capacities are constant (§10) and
  the heat moves with their change.
- The closed form is the beaker's heat capacity taking that heat. The study fails when the adiabatic run ends
  more than 0.1 K from it, or with salt undissolved.

| Salt | Adiabatic | Closed form | Losing 0.5 W/K to the room | Half dissolved | 99 % dissolved |
|---|---|---|---|---|---|
| CuSO₄, 8.0 g | +11.440 K | +11.499 K (off by −0.059) | +10.606 K at most | 23 s | 61 s |
| CuSO₄·5H₂O, 12.5 g | −2.176 K | −2.151 K (off by −0.024) | −1.724 K at most | 29 s | 220 s |

The rest is the steam and the air over the water. Every total is unchanged, and every invariant is checked on
every step.

**Vacuum** (`data/benches/vacuum.json`, a scratch probe of lab commands):

| Time | What happens |
|---|---|
| 120 s | Full flame: the flask boils at 100.4 °C, and its nitrogen is gone (below 1 mg) |
| +60 s | Stoppered and off the heat: 64 kPa at 88 °C |
| +600 s | 6.3 kPa at 37 °C |
| +30 min | 2.4 kPa at 20.5 °C |
| After reheating | A gentle flame (20 %): 13 kPa at 51 °C, so it boils at 50 °C. The full flame pops the stopper at 138 kPa (103 °C) |

**Rate** (`data/benches/rate.json`): 1.0 g of CuO under 50 mL of 1 M acid in each beaker. One beaker is heated
at full flame to 60 °C and then left to cool. Each beaker's CuO after 10 minutes:

| Beaker | CuO after 10 minutes |
|---|---|
| Still | 0.964 g |
| Stirred | 0.965 g: the surface sets the pace, as the demo's step 13 says |
| Heated to 60 °C | 0.669 g, as it cooled to 34 °C |

The tutorial therefore holds the warm beaker near 60 °C with a thermostat the player makes: a monitor below
60 °C, rigged to open the burner's valve to 30 % and close it above. A thermostat is also the first use of a
rig in both directions.

**The experiments played through** (`native/tests/game/test_experiments.cpp`, strict):

| Experiment | Result |
|---|---|
| `vacuum` | Pressure: least 6.0 kPa, most 137.9 kPa. Temperature: least 20.0 °C, most 109.3 °C |
| `solution_heat` | Left beaker 20.0 °C → 30.6 °C at most; right 20.0 °C → 18.3 °C at least |
| `rate` | CuO left: still 0.912 g, stirred 0.912 g, warm 0.010 g; the warm beaker at most 60.0 °C |

## 41.2 Strict and free

**The state and the predicates**
- `Ui` holds the mode: strict as opened, kept through a reset and another experiment, and not in the lab's
  save or digest.
- `guided()` (every refusal: about 70 places in `ui.cpp`) became `running() && strict`.
- `held()` (the clock that waits for a gesture) became strict-only.
- The card and the outline read `running()`, so they show in both modes.

**What free mode keeps**
- The clock runs a second at a time with a `follow()` after each, so a step still passes in the second it
  holds.
- The hint a new part gives ("monitor1 senses nothing yet") is still cleared under a running tutorial, since
  the step says how.
- `debug:step` is taken.

**The switch**
- `tutorial:strict`, on the card's header. Pressing it while the part editor is open toggles the mode and
  leaves the editor open, like the clock.
- It is not taken through the notices or the debug panel, which cover the card.

**The interfaces**
- The C API: `cg_ui_set_strict`, and `cg_tutorial.strict`. A caller whose `size` ends at `steps` (built
  before Stage 41) is still answered, and nothing is written past its size.
- The Godot host: `set_strict`, and the 4th element of `tutorial()`.

## 41.3 The experiment picker

**The catalogue**
- `data/experiments.json` (`chem-experiments/1`) lists, for each experiment, an id, a title, a summary, and
  its bench and tutorial as paths from the catalogue's folder.
- `Ui::experiments(text, why, read)` reads the paths through the host's reader. In the C API this is
  `cg_ui_experiments(ui, json, cg_read_file, user)`.
- The lab layer does the JSON itself. Godot's parser would make the benches' integers (grains) floats.
- Every experiment is opened at load (its Lab, and its tutorial against that Lab's view), and the catalogue is
  refused whole for one that cannot be. Ids are unique.

**On the screen**
- "experiments" sits in the top bar over the cabinet (x 10 to 250, the bar's one free stretch). It opens a flat
  list over the bench.
- A row clicked opens that bench and tutorial in place of what is open, through the reset's path (`begin`).
  The hand, the pour panel and the drags are let go.
- A press beside the list closes it and does nothing else. With something in hand, a press is where the thing
  goes, not the list's.
- The one shown is the entry whose bench and tutorial are what is open (as parsed JSON). That is how the demo,
  opened by `bench_path` and `tutorial_path`, is marked as `demo`.

**The Godot host**
- `experiments_path` (by default `res://data/experiments.json`), read in `_ready` after the tutorial.
- `experiment()` and `choose(id)`.

## 41.4 The experiments

Each is played to its end through the screen with its own gestures, in three ways: strict, strict at a
person's pace, and free with its own gestures at once. That is 9 test cases, each a ctest entry.

The pointer's harness moved into `native/tests/support/pupil.hpp`, with `follow_through` and `play_as_person`.
The demo's own tests are unchanged.

**Results of a quantity.** Any quantity of §13.3 can be a result, shown as `start`, `least`, `most` or `now`.
It is read each time the tutorial follows the lab (at least once a lab second), and starts again with a reset.

The steps were written as the demo's are. They use parts the demo taught: monitors in the `vacuum` and
`solution_heat` experiments, and a monitor rigged to the burner in the `rate` experiment.

## Open

- **The room's air is dry.** A jar of the pentahydrate on the `solution_heat` bench raises, at 0.2 s, "the blue
  crystals are losing water and turning pale: they are too hot or too dry". The loss over the experiment is
  negligible: all 12.5 g still dissolves to 7.99 g of CuSO₄(aq). It is the roadmap's humidity area (6). Ways
  to fix it:
  - give the room water vapour (a design value of humidity);
  - or a capped jar.
- **The heats of solution are the fit's.** CuSO₄(aq)'s ΔHf (−835.99 kJ/mol) is fitted to its solubility
  (DATA_SOURCES, Stage 19), and there is no heat of dilution (§20).
  - The game gives −64.6 and +14.6 kJ/mol.
  - Atkins's ions (the doc's reference row, −844.50) would give −73.1 and +6.0 at infinite dilution.
  - The signs and the order are right. A heat of dilution (§10's excess enthalpy) would close the gap.
- **Strict again, mid-run.** A lab that went off the steps in free mode can leave a step its own gestures
  cannot pass (an item now where the step's drag cannot take it). Reset recovers, and the spec says so. A
  later stage could instead pass over a step whose checks already hold.
- **Free, at a person's pace,** the `vacuum` stopper must go in before the flask draws air back in. At 60x
  that is a few real seconds: the free run is the player's to lose, and the failure says why. Its test plays
  the gestures at once.
