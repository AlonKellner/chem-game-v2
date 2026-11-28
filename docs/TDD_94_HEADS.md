# TDD 94: Heights and heads (Stage 94)

## The request

The first stage of the plan the user approved on 2026-11-28 (tubing as a vessel, then physical measuring devices:
14 stages, 94 to 107 and 83). The user: "The weight of the tube contents and their orientation should affect the
pressure at the ends, so situations like siphoning will work as expected." Before a tube can weigh anything, the
bench needs heights and the closure (Stage 89, `native/core/src/closure.cpp`) a liquid's head. Stage 89 left it
open: "a liquid's head (`P + ρ g h` at a port: communicating vessels, a siphon, a manometer)".

Worked on branch `stage-94`, in the worktree directory still named `.claude/worktrees/stage-89` (this session
cannot make a new worktree; the branch was started there from `origin/main` at `46d9748`).

## What is there (read 2026-11-28)

- A conduit's end is "foot" or "top" (`Edge::foot`, two bits): which of liquid and gas stands there. No height.
- A vessel's cells have a shape by height (`CellMeta::column_shape`, `column_rise`, `column_area`;
  `strata.cpp`'s `height_of`, `area_at`, `levels_of`) but no height above anything: the lab's items stand on
  squares with no size in centimetres (40 px a square and 11.75 px/cm, in the screen's code only).
- The closure takes nodes from the highest pressure down, because what crosses runs down the pressure.

## The design (GAME_SPEC §6, Flow between vessels: heads)

### In the core

- **A cell's height** (`CellMeta::height`, m): its floor above the bench's datum (`World::set_height`).
- **A port's height** (`Edge::port_a`, `port_b`, m above its end's floor; `World::set_ports`): where the conduit
  meets each vessel. Infinite: at its top (what Stage 89 called "top"); 0: at its foot. `set_conduit`'s foot bits
  stay as those two.
- **What stands at a port** is what the column holds at that height: the layer that spans it, then the layers
  above it as they come down to it, then the gas once the surface is at the port. What lies under a port stays
  (a dip tube leaves a heel). A cell with no shape has no heights: a port at 0 gives its liquid, any other its gas,
  and it has no head.
- **A port's pressure** is its vessel's and the weight of the liquid over it, `P + g Σ ρ_k Δh_k` up the layers
  from the port to the surface (heights from the column's shape, as the round begins).
- **What drives a crossing**: `(P_g + head_g) − (P_t + head_t) − ρ g (Z_t − Z_g)`, the two ports' pressures less
  the weight of what crosses over the rise from the giver's port to the taker's (`Z`: the cell's height and its
  port's; the conduit holds what its giver gives, until tubes are vessels, Stage 95). A gas's weight is left out.
- **Stable at any step**: a crossing lowers its giver's surface and raises its taker's, by its mass over each
  one's area there. The law is met with that in it: the rate is the root of
  `G(m) + ρ g dt (1/A_g + 1/A_t) m = ρ ΔΦ`, so a crossing never passes the level it runs to (`conduit_rate`'s
  `give`). Each conduit takes its own effect on the levels; two on one vessel see each other's a step late.
- **The order**: with heads a vessel at the lower pressure may be the giver. The crossings' masses need the
  pressures and the heads alone and are found first; then the nodes are sized in the order of the crossings (a
  giver before its taker, by Kahn's order; in a ring, the rest as they stand, a giver not yet sized giving at the
  temperature it began the round at).
- **Gravity's work is in the ledger.** A liquid that falls from its giver's surface to its taker's gives up
  `m g (Z_s,g − Z_s,t)`; pushed up, it takes it. Without it a column filling a closed flask would squeeze the
  flask's gas with nothing to pay for it, and the entropy would fall by `v ΔP / T`. The energy comes from a
  reservoir of work named `gravity` (at `WORK_KELVIN`, as the lab's mains: what crosses to it is no heat, T1),
  made by the world when the first conduit is set (so a world with none is as it was, cell for cell), with an edge
  to each conduit's ends. The taker's top takes it.
- **Z1** (new check): the energy along a gravity edge in a step is the step's crossings' into that cell, each its
  mass times `g` times its fall (the fall in the crossing's row of the trace), to a quantum a crossing.

### In the lab

- **A square has a side** (`data/glassware.json`, `square_mm`: 34.04 mm, what the screen's 40 px a square at
  11.75 px a centimetre already made it; `glass::square_cm`).
- Every vessel's cell gets its height each step from where its item stands (`set_heights`): on the bench its
  rectangle's foot, in a vessel that vessel's floor, on a vessel's mouth that vessel's brim.
- **A syringe stood in a vessel** (`stand`, Stage 71) has its open nozzle in what the vessel holds, not in the
  room's air (`open_mouth`: a conduit of its nozzle, its foot on the other's floor): pulled, it draws the liquid
  up; pushed, it gives it back; lifted out, its nozzle is in the air again. No new gesture. (The plan had a
  `dip` command; standing it in the vessel is the same thing, with the displacement and the walls' contacts Stage 71
  already gives.)

## Phases

| Phase | Topic | Status |
|---|---|---|
| 94.1 | Spec (§6, §18), this document | Done |
| 94.2 | Ports and heights in the world; `conduit_rate`'s `give`; the column's heights shared with the closure | Done |
| 94.3 | Heads in the closure: what stands at a port, the drive, the order by crossings; vessels open to the room | Done |
| 94.4 | Gravity's reservoir and its work; Z1 and its self-test | Done |
| 94.5 | The lab: the square's side, heights, a syringe stood in a vessel; save, load, digest | Done |
| 94.6 | Docs; `make gates`; the user's sign-off | |

## Tests

- `native/tests/test_heads.cpp` (6): two open vessels joined at their feet, 18 cm apart, level to 0.4 um at
  16.40 cm (what they hold over both areas), never past level, at a step and at four steps' length; three in a row,
  one on a 6 cm block, at 15.333, 15.333 and 15.333 cm over the bench; a port 4 cm up, the other half a metre
  below: drained to 4.00 cm and no further; a 1 m open column into a closed flask: its air at 110.79 kPa,
  `P_atm + rho g dh` to 2e-4, gravity having given 0.056 J (its reservoir's account to the quantum), every invariant
  on each of 14 400 steps; a syringe 10 cm over a beaker, its nozzle a centimetre over the beaker's floor: 39.15 mL
  of a 40 mL pull, its air at 100.35 kPa, the room's less the 10 cm of water it holds up; Z1's self-test (five
  corruptions).
- `test_closure.cpp`: a conduit's ports, a cell's height, gravity's reservoir made once with an edge to each end,
  a column's heights by its shape. `test_conduit.cpp`: the law with a give.
- `native/tests/game/test_syringe_lab.cpp` (2): a syringe stood in a beaker of 150 mL draws 54.8 mL of a 55 mL
  pull (its air at 100.9 kPa), gives it back, goes on after a save and a load, and on the bench again is open to
  the room; two syringes of water on a tube stay as they are side by side, and nine squares apart the upper runs
  down into the lower.
- Stage 89's tests, the lab's syringe tests and the gas-syringe experiments unchanged.

## Found

- **F1 could not ask the law again from two pressures.** A liquid's drive is the difference of two port pressures
  a hundred kilopascals each and, near level, a ten-thousandth of a pascal apart: recomputed from the row's two
  numbers it was 1e-7 off, and the rate looked over its law. The row keeps the drive as it was reckoned.
- **A vessel not yet full at the room's pressure cannot be sealed by a conduit** (as by a stopper): a test's flask
  built a thousandth short of air was refused until a step had filled it.
- **Two vessels open to the room** joined by a conduit were skipped by Stage 89 (no pressure to solve): with heads
  they are the communicating vessels, and are run.

## Open

- **The heel's share is by height, not by volume**: in a vessel that narrows, what of the port's layer lies over the
  port is taken as its share of the layer's height.
- **Two conduits on one vessel** see each other's effect on its level a step late (each is met with its own).
- **A conduit holds what its giver gives**: its own contents, with their weight along its own path, are Stage 95.
- **Pours and drains** (a burette's tip, a held pour) stay the lab's own kinematic rules, with no work of gravity's.
- **Heights in the picture**: an item's height is its rectangle's foot; the glass's own floor is a wall's thickness
  higher.
