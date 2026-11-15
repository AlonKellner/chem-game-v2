# TDD 03: Reaction kinetics (Stage 3)

## The request

> … simulating their connections using the physical, thermal, mechanical, and chemical laws
> required to realistically simulate the progression and changing of material concentrations …

Stage 3 is the chemical law alone (GAME_SPEC §7): a reaction runs inside a cell at a rate, and the
malachite decomposition of the demo's first stage happens for the first time.

| Phase | Topic | Status |
|---|---|---|
| 3.1 | Data: malachite's ΔHf°, S°, Cp verified; its kinetics sourced, the prefactor labelled calibrated | Done |
| 3.2 | Gas activities, the entropy's mixing terms, affinity (`chem/thermo.hpp`) | Done |
| 3.3 | Rate laws as data: `first_order` in `data/reactions.json`, validated by the build | Done |
| 3.4 | Reactions in the step: exact first-order extent, the affinity factor, the equilibrium cap (`chem/sim.hpp`) | Done |
| 3.5 | T1 extended to reactions; D3 at chemical equilibrium | Done |
| 3.6 | The metamorphic invariants M1 (reference shift), M2 (scaling), M3 (convergence) | Done |
| 3.7 | The reacting-world sweep | Done |
| 3.8 | `chem_tool study decomposition`; step cost | Done |
| 3.9 | The user's sign-off | Done (2026-11-15: "Very good, approved, proceed") |

## Decisions

- **Reaction heat is never written anywhere.** `transform` leaves the cell's enthalpy unchanged;
  the products have a different formation enthalpy, so the derived temperature moves. M1 tests
  exactly this: shifting every formation enthalpy by a constant per atom leaves every trajectory
  the same, which fails if a reaction's heat is counted twice or not at all.
- **Every reaction is reversible in form.** The rate carries an affinity factor that vanishes at
  equilibrium and changes sign past it. The planning draft's `1 − e^(−A/RT)` diverges for a
  strongly unfavourable reaction; the factor is now symmetric and bounded in [−1, 1].
- **A reaction never passes its own equilibrium.** If the step's extent would flip the sign of the
  affinity, the last quantum that keeps it is found by bisection on trial states. Entropy
  therefore rises in every application, and at equilibrium the cell is static.
- **Sequential application replaces the frozen-state limiter** (GAME_SPEC §4.5, rewritten). The
  plan had every rate evaluated from one start-of-step state and competing processes scaled
  together. A process applied second would then run on a driving force the first had already
  changed, and T1 could not be exact. Applied in sequence, each process is exact-conserving and
  second-law-safe on its own. The cost is first-order accuracy in the step, measured below.
- **A reaction without verified data does not run.** Affinity is undefined if any species it
  names lacks thermodynamic data, so the six reactions still waiting for Stage 5 and 6 data are
  inert.
- **The ledger records a transform's extent**, so T1 recovers the reaction's numbers from its
  change and evaluates the affinity with the same arithmetic the step used.
- **Calibrated values are labelled.** Malachite's activation energy is inside the measured range;
  its prefactor is chosen for play and says so in `docs/DATA_SOURCES.md` and GAME_SPEC §20.

## Evidence

`chem_tool study decomposition`: 5 g of malachite in a 100 g glass flask of air, a 2200 K flame
through 0.2 W/K, a 20 °C room through 0.9 W/K.

- Nothing visible below 200 °C; the reaction takes off near 300 °C and is 99% done at 8 minutes,
  with the flask at 363 °C.
- Products: 3.597 g CuO, 0.995 g CO₂, 0.407 g H₂O from 5.000 g.
- Every element, the mass and the energy are the same integers after 72,000 steps.
- Step cost: 0.43 µs (one cell, two reservoirs, the reaction table; trace built, checks off).

Tests:

- First-order decay at a held 600 K against `n0·e^(−kt)`: worst error 6e-7 of the initial amount
  over 60,000 steps.
- A sealed cell: stops with 0.033 of 0.1 mol left at 320 K, affinity 5e-7 J/mol, then
  bit-identical for 50,000 steps.
- Convergence: errors at steps of 4, 2 and 1 s are in ratio 1.9 : 1.9. At the game's step the
  error is 0.2% of the amount at the steepest moment, a time shift of about one step.
- The reacting sweep: 2000 random worlds, 120,000 steps, 110,000 reaction steps, 521,000 heat
  transfers; every invariant holds.

## Known limits

- **First order in the step.** A second-order splitting (half a heat step, the reactions, half a
  heat step) would cut the error further; it is not needed at 1/60 s.
- **The reverse rate's form is arbitrary.** A reverse reaction runs at a rate proportional to the
  forward law's reactant. That is thermodynamically sound and kinetically a placeholder: fully
  decomposed malachite never reforms.
- **One temperature per cell.** In the study the whole flask, glass included, reaches 367 °C.

## Tests

- `kinetics: …` (11 cases): the data oracle, affinity's sign, refused kinetics, exponential decay,
  a rate far too fast for the step, equilibrium and rest, the reverse direction, the heated flask,
  M1, M2, M3
- `sweeps: reactions run down their affinity and entropy grows on every step of every random
  reacting world`
- ctest `chem_tool_study_decomposition`
