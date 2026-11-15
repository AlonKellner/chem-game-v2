# GAME_SPEC version history

## 0.2.0 (2026-11-15, Stage 1)

- §4.1–§4.3 implemented: species with atoms and molar mass derived from the formula, cells that
  store integer amounts and energy, edges with phase filters, the two primitives, refusals, the
  ledger.
- §4.2: matter and energy enter a world only as initial conditions; totals that do not fit the
  integer range are refused at build; the topology changes at a step boundary only.
- §4.3: a `Reaction` cannot be constructed unbalanced; unbalanced data fails the build.
- §18: C1–C7 implemented; C8 (a transform's change is balanced) and L0 (a refused call changes
  nothing) added; S1 moved to Stage 2 (Stage 1 has no floating-point state).

## 0.1.0 (2026-11-15, Stage 0)

- First specification: principles, numeric conventions, the conservation kernel, every system in
  full with its demo subset, the 3-stage demo, the invariant catalogue, declared simplifications.
- Implemented: the build identity and the error and log plumbing (§14).
