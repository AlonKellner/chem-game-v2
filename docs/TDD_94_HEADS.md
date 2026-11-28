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

- **A square has a side** (cm): one number in `data/`, read by the lab and by the screen (which had it as two).
- Every vessel's cell gets its height from where its item stands; a syringe's joint and tube give their ports'
  heights (its nozzle at its foot; a flask's neck at its top).
- **`dip {item, in}`** / `undip`: a syringe held with its nozzle under an open vessel's liquid: a conduit from the
  syringe's foot to the vessel at a depth, the vessel left open. Pulling draws the liquid, pushing gives it back.
  (An end at the room's pressure, with its head.)

## Phases

| Phase | Topic | Status |
|---|---|---|
| 94.1 | Spec (§6, §18), this document | |
| 94.2 | Ports and heights in the world; `conduit_rate`'s `give`; the column's heights shared with the closure | |
| 94.3 | Heads in the closure: what stands at a port, the drive, the order by crossings | |
| 94.4 | Gravity's reservoir and its work; Z1 and its self-test | |
| 94.5 | The lab: the square's side, heights, ports, `dip`; save, load, digest | |
| 94.6 | Tests under hands at random; docs; `make gates`; the user's sign-off | |

## Tests (planned)

- Two open vessels joined at their feet level: by `ρ g Δh` through the conduit's law, never past level, at a step
  and at four steps' length; three in a row.
- The hydrostatic paradox: a wide vessel and a narrow one, the same depth, the same port pressure.
- A port above the heel: a vessel drains to its port's height and then gives its gas.
- A tall open column into a closed flask: the flask's gas comes to `P_atm + ρ g Δh`, and the world's entropy does
  not fall on any step (it does, by `v ΔP / T`, with gravity's work left out: the self-test of why it is there).
- A syringe draws water up from an open beaker: the hand's force includes the lift; let go, it runs back.
- Z1 fires on a gravity entry with no crossing, and on one of the wrong size.
- Stage 89's tests unchanged where nothing has a height (every port at a foot or a top, every cell at height 0).

## Open

(as found)
