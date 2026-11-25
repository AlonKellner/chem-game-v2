# TDD 60: The planner (Stage 60)

## The request

The curriculum's step C, its last part (`docs/CURRICULUM.md`: S3), after Stage 59's sign-off: the user's "Approved,
merge and proceed" (2026-11-25). The planner: a target quota, the route the student chooses, the amounts each step
needs, the target tracked as a goal. With it, experiment 8 (limiting reagent and percent yield).

## What the simulation showed first

- **Seeding** (curriculum 35): a solution 5 % over saturation stays clear for an hour, as it should; but a 10 mg seed
  grows only to 15 mg in the next hour. A real seed sets off secondary nucleation (fragments the stirrer chips off it
  grow as crystals of their own), and the excess comes out within minutes; the model nucleates only from the
  solution. Moved, with a chemistry step that adds secondary nucleation from measured data (C16).
- **The hydroxide boiled to the oxide**: in boiling water the model converts a fifth before the water is gone (half
  in 30 min, its data's), the rest dry among the salts at 400 C. Experiment 8 weighs the hydroxide, the curriculum's
  first yield.

## The design

- `lab/planner.hpp` (pure): a target (a species, its mass) and the route from it back, each step a reaction that makes
  what the step before needs and the yield expected of it; worked back, each step's reaction runs as far as its
  product is needed over its yield, every reactant it uses is needed, the one the next step makes carried, the others
  bought. A route that does not chain is refused, saying which step.
- The screen: the notebook's fourth tab, "planner": the target's formula, grams and purity; each step's reaction
  chosen with arrows among those that make what it is asked for (the target, or the first reactant of the step before
  that a reaction makes; changing a step drops the ones after it), its yield; "+ step", "- step"; what each step
  needs, carried or bought; "set it as a goal in the outbox": a player's goal on the first free slot, its mass and its
  purity. The panel's close and erase at its right (four tabs). Its check: `calculated` "plan".
- The experiment `yield` (curriculum 8).

## Phases

| Phase | Topic | Status |
|---|---|---|
| 60.1 | The explorations; `lab/planner.hpp` and its test | Done |
| 60.2 | The screen: the planner's tab, the goal | Done |
| 60.3 | The experiment: limiting reagent and percent yield | Done |
| 60.4 | This document, GAME_SPEC, CHANGELOG, the curriculum, `make gates`, the user's sign-off | |

## The goldens

None moved.

## Measured

- 5.00 g of the pentahydrate by the oxide: 1.593 g of the oxide carried, 2.214 g of malachite; expecting 60 %
  crystallizing, 3.69 g of malachite; directly from malachite in acid, half a mole of malachite a mole.
- `yield` at the bench's seed: 1.6232 g of the crystals, 0.6344 g of the hydroxide against a theoretical 0.6342: 100.0 %.

**Tests**:

| Test | What it checks |
|---|---|
| `test_planner.cpp` (2) | the routes by the oxide and direct, a yield's effect, refusals; on the screen a target typed, its route chosen, worked back, set as a goal |
| `test_experiments.cpp` (3) | `yield` strict (the yield from the notebook's two readings within 0.5 % of the truth, the report, the prediction), at a person's pace, free |

## Open

- **A yield of 100 %**: no powder clings to glass or paper, none passes the paper, a residue weighs dry at once; a
  real yield is lower (§20, Stage 59's).
- **Crystals to a quota** (curriculum 50) on the planner: its own stage (the malachite route, weighed and handed in).
- **Seeding** (35): with secondary nucleation.
