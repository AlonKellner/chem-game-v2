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
- **The experiment** `rates`: copper oxide's dissolution timed by absorbance at three temperatures and two strengths
  of acid; the time for A to reach 0.050, rate ∝ 1/t; ln t against 1/T, its slope Ea/R; the order from two strengths.

## Phases

| Phase | Topic | Status |
|---|---|---|
| 56.1 | The explorations; GAME_SPEC; the holder's data | |
| 56.2 | The lab: the holder, saved and digested | |
| 56.3 | The screen: the holder's controls | |
| 56.4 | The experiment: the order and the activation energy | |
| 56.5 | This document, CHANGELOG, the curriculum's moves, `make gates`, the user's sign-off | |
