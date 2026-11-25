# TDD 56: Rates by absorbance: the order in the acid and the activation energy (Stage 56)

## The request

The curriculum's step B, its last part (`docs/CURRICULUM.md`), after Stage 55's sign-off: the user's "Approved, merge
and proceed" (2026-11-25). Stage 55 gave the student a spectrophotometer and a plot; this stage times a reaction with
them: curriculum 36, what sets a rate, the order in the acid and the activation energy.

## What the simulation showed first

Each candidate experiment of the step was run before it was written (an exploratory test, not kept):

- **Copper oxide in acid, in a stirred cuvette** (curriculum 36): the blue of the copper it releases climbs at 750 nm
  steadily: with 5 mg of the oxide in 3.5 mL of 1 M acid, 0.0025 in 4 min at 20 C, 0.033 at 50 C (13 times: Ea 69
  kJ/mol); half the acid, 1.39 times slower (the surface law's order 0.5: 1.41). A lesson's experiment.
- **A first-order half-life** (curriculum 37): the simulation's first-order reactions (malachite's and the
  hydroxide's decompositions) change only solids and gases, which no instrument here reads while they run (a gas
  syringe, or a balance under a burner, is not built). Moved to step F, with crystal violet by absorbance (C4).
- **A solubility curve** (curriculum 4, 45): copper sulfate's solubility at 20 C is right at equilibrium (19.7 g per
  100 g of water against the measured 20.7), but its crystals dissolve as slowly as they grow: stirred, 5 g of the
  pentahydrate in 10 g of water takes an hour to saturate, and slow heating dissolves the last crystal 50 K above the
  saturation temperature. A real crystal dissolves in minutes, its rate set by diffusion through the liquid's film
  (much faster than its growth, which its surface limits). By the curriculum's first principle (a model's artefact a
  lesson would expose is fixed before that lesson), the solubility curve waits for dissolution by mass transfer: its
  own stage (a chemistry change: the goldens move). Cooled, its solution nucleates only near room temperature, deeply
  supersaturated, as copper sulfate readily does.

## What it builds

- **A heated cell holder** on the spectrophotometer: a set temperature (20 to 80 C), a heater from the mains into the
  cuvette in its well, in proportion to how far it is under (5 W/K, up to 10 W; design), into its lowest layer.
- **A suspension in the beam**: the grains a stirred cuvette holds up dim it (seen in this stage's first picture: the
  stirred cuvette grey with its oxide, its reading clean; the reading had counted only a sediment at the beam).
- **The experiment** `rates`: copper oxide's dissolution timed by absorbance at three temperatures and two strengths
  of acid; blanked against the stirred suspension, the time for A to reach 0.050, rate ∝ 1/t; ln t against 1/T, its
  slope Ea/R; the order from two strengths.

## Phases

| Phase | Topic | Status |
|---|---|---|
| 56.1 | The explorations; GAME_SPEC; the holder's data | Done |
| 56.2 | The lab: the holder, saved and digested | Done |
| 56.3 | The screen: the holder's controls | Done |
| 56.4 | The experiment: the order and the activation energy | Done |
| 56.5 | This document, CHANGELOG, the curriculum's moves, `make gates`, the user's sign-off | `make gates` passed (872 s: ASan+UBSan 872, ctest 664, Godot 13 of 13, goldens unchanged); awaiting the user's sign-off |

## The goldens

None moved: a lab that never sets a holder makes no edge and digests as before.

## What was built

**The lab** (`lab.cpp`). `holder {spectrophotometer, celsius?}`; `set_holders` each step before the processes: while
set with a cuvette in its well, an edge from the mains to the cuvette's lowest layer (made, moved to another layer or
cuvette, taken off as needed), its power `min(10 W, 5 W/K × (set − T))`, never below 0. Saved (`holder_c`), digested
while set; the view's `holder_c`; the quantity `holder`. The model's data (`data/instruments.json`,
`spectrophotometer.holder`, design).

**A suspension in the beam** (`spectro.hpp` `turbidity`, `lab.cpp` `spectro_sample`): each solid's grains in the
cell read, its particles times the cuvette's `suspended` share over the liquid's volume, each `2 π d² / 4` (d from
`particle_diameter`), decadic, the same at every wavelength; only the settled share stacks at the floor.

**Conditions**: `noted_count`; the vessel's quantity `suspended`.

**The screen** (`ui.cpp`). The holder's switch and 5 C steps beside blank (`cell:<id>`, `cell:<id>:-`, `:+`); the
cuvette's warmth in the spectrophotometer's readout; a notebook field clicked into is replaced by what is typed.

**The experiment** `rates` (29 steps): a prediction of the order; four runs of seven steps (into the holder, warming,
the oxide in and stirred, stirred up, blank and the start, until 0.050, its end and temperature and time).

## Measured

- The holder brings a cuvette from 20 to 49.4 C in a minute and holds it at 49.7 C (its loss to the room, 0.05 W/K,
  against 5 W/K); saved and loaded while it warms, the same lab step by step.
- 50 mg of copper oxide (38 µm grains) held up in 3.5 mL of water: A 0.39; stopped, under 0.01 within a minute.
- `rates` at the bench's seed, blanked against the suspension: 208 s at 24.9 C, 85 s at 35.0 C, 37 s at 44.7 C;
  ln t against 1/T, 68.8 kJ/mol (R² 0.99987) against the data's 69; half the acid at 34.8 C, 120 s: the order 0.501.
  (Blanked before the oxide went in, the grains alone read past 0.050 within a second.)

**Tests**:

| Test | What it checks |
|---|---|
| `test_spectro_lab.cpp` (3 more) | a suspension's turbidity, stirred and settled; the holder: refused past its range, nothing warmed with an empty well, warmed and held, saved and loaded mid-warming, the cuvette cooling once out; on the screen its switch and steps |
| `test_experiments.cpp` (3) | `rates` strict (Ea and the order from the notebook, the report and the prediction beside it), at a person's pace, free |

## Open

- **Crystals dissolve as slowly as they grow** (C15): the solubility curve (4, 45) waits on dissolution by mass
  transfer, a chemistry stage of its own (the goldens will move): `docs/CURRICULUM.md`, step B2.
- **A first-order half-life** (37): with crystal violet by absorbance, step F.
- **A Peltier holder** that cools as well as heats, under a PID controller: this one only heats, in proportion.
- **The time is read off the notebook's table** by subtraction: the calculator (step C) is where a student sets
  such sums up.
