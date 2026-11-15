# GAME_SPEC version history

## 0.6.0 (2026-11-16, Stage 5)

- §4.5: the step's order restated: rate reactions, heat, drains, then settling (fast equilibria
  and volumes together, last).
- §6: drains; settling takes a cell's equilibria again after it draws gas; a starved cell is now
  also one whose fast reaction outruns eight fillings a step; a cell is never left over-full.
- §8: the equilibrium law and its search; what follows from it.
- §18: P1 and V2 implemented. P1's tolerance restated: it was "a few quanta of the reaction",
  which does not bound the rounding of gas moved after the equilibrium is taken.
- §20: four simplifications declared (ideal steam: water boils at 100.44 °C; no nucleation
  barrier; instantaneous drains; no overflow yet).

## 0.5.0 (2026-11-15, Stage 4)

- §4.1: wall species; molar volumes derived from densities.
- §6 demo subset written out: elastic and rigid cells, the tree, expelling and drawing, the
  enthalpy carried, the sealing rule and its refusal, starved cells.
- §18: V1 and V3 implemented; T1 extended to gas; V2 moved to Stage 5 (it is about liquids).
- §20: four simplifications declared (no partial vacuum, no gas diffusion, air as N₂ and O₂,
  walls at their contents' temperature).
- Data: densities of water, CuO, malachite and CuSO₄ verified; latex added as the balloon's wall.

## 0.4.0 (2026-11-15, Stage 3)

- §4.5 rewritten: processes are applied in sequence, each from the state the last one left, each
  capped at its own equilibrium. The earlier design (every rate from one frozen state, competing
  processes scaled together) is dropped, with the reason. Heat conduction is no longer an
  exception; it is the rule.
- §4.6: activities defined (gases now, solutes in Stage 6); the affinity factor made symmetric
  and bounded, so a reverse rate cannot diverge.
- §7: the first-order law and its exact step; a reaction needs thermodynamic data for every
  species it names.
- §18: T1 extended to reactions; D3 at chemical equilibrium; M1, M2, M3 and the malachite K1
  oracle implemented.
- §20: three simplifications declared (calibrated kinetics, the reverse rate's form, sequential
  application).
- Data: malachite verified (Richardson and Brown 1974; Kiseleva et al. 1992).

## 0.3.0 (2026-11-15, Stage 2)

- §3: the core's own `exp` and `log` implemented and pinned; the per-step range limit stated.
- §4.2: reservoirs are energy accounts at a fixed temperature, holding no matter; a world has
  temperatures when built with one, and then every cell must have a temperature.
- §4.5: heat conduction is relaxed pairwise in edge order, the declared exception to
  frozen-state evaluation.
- §5: the exact two-body relaxation and its quantisation rule (no overshoot, static at rest).
- §18: T1, T2, T3, S1, D3 and the first K1 oracle implemented; T2's tolerance restated as the
  rounding bound of its own sum (was a guess); UX6 tested.
- Data: thermodynamic values for water, steam, CO₂, N₂, O₂, CuO, CuSO₄ and glass verified against
  the NIST Chemistry WebBook and ISO 3585; the other species have none yet and cannot be in a
  cell with a temperature.

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
