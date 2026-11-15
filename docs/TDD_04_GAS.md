# TDD 04: Gas volumes and flow (Stage 4)

## The request

> For example, instead of multi layered containers, start with homogonous containers, and
> simulating their connections using the physical, thermal, mechanical, and chemical laws …

Stage 4 is the mechanical law in its simplest honest form (GAME_SPEC §6, the demo's subset): every
cell is at one atmosphere, a flask has a fixed volume, and gas moves between connected cells so
that the flask is exactly full. The demo's first stage now runs end to end without its player: the
balloon inflates.

| Phase | Topic | Status |
|---|---|---|
| 4.1 | Data: densities verified; molar volumes derived by the build; wall species (glass, latex) | Done |
| 4.2 | Volumes derived from state: ideal gas at 1 atm plus solids and liquids (`chem/thermo.hpp`) | Done |
| 4.3 | Rigid and elastic cells; the sealing rule at build and at disconnection (`chem/world.hpp`) | Done |
| 4.4 | Gas equalisation in the step: the tree, expelling, drawing by a closed-form root (`chem/sim.hpp`) | Done |
| 4.5 | Invariants V1 and V3 with self-tests; T1 extended to gas | Done |
| 4.6 | The sweep with volumes | Done |
| 4.7 | `chem_tool study balloon`; step cost | Done |
| 4.8 | The user's sign-off | Pending |

## Decisions

- **Gas flow is a constraint, not a rate.** With every cell at 1 atm there is no pressure
  difference to drive a rate; pressure-driven flow would also be the stiffest process in the game.
  The condition "this flask is exactly full" is solved directly. GAME_SPEC §6 keeps the full
  pressure model as the design; this is its limit of infinite conductance.
- **Expelling needs no solve; drawing has a closed form.** Gas leaving a cell does not change the
  cell's temperature, so expelling the excess fraction fills it exactly. Gas entering mixes with
  the cell and its walls, and the amount that fills it is the positive root of a quadratic. A first
  version bisected; the closed form cut the step cost from 1.5 µs to 0.67 µs.
- **Expulsions round up.** A cell ends a fraction of a quantum under-full, which no draw can fill,
  so it settles in one step and a world at rest is static. Rounding down left one-quantum flows for
  a further step.
- **A rigid cell cannot be sealed.** The constant-pressure enthalpy bookkeeping is exact only
  while every cell is at 1 atm, so a stoppered flask is outside the demo's subset. The builder and
  `disconnect` refuse it; V3 witnesses it.
- **Starved is flagged, not hidden.** A flask closed by an empty balloon and then cooled wants gas
  that does not exist. The cell is left under-full and the trace says so; V1 accepts only flagged
  cells. Hiding it would make V1 meaningless; inventing gas would break C1.
- **Walls.** An empty balloon has no gas and would have no heat capacity, hence no temperature. A
  vessel's own material is a species that holds heat and does nothing else: it takes no room, does
  not move, does not react.
- **V2 moved to Stage 5.** "No vessel holds more solids and liquids than its volume" is about
  liquids and the spill tray.
- **A balloon has no capacity yet.** Bursting belongs to the equipment (Stage 8).

## Evidence

`chem_tool study balloon`: 5 g of malachite in a 250 mL flask of air, a 2 g balloon on its mouth,
twenty minutes over the flame, then twenty with the burner off.

- The first gas into the balloon is the flask's own air (99% air at 2 minutes).
- Hot, the balloon holds 1.22 L at 20 °C: 39.8% CO₂, 39.8% H₂O, 20.3% air.
- As the flask cools it draws 0.0056 mol of that mixture back; the balloon ends at 1.09 L.
- CO₂ made: 0.995 g. In the balloon: 0.792 g. Left in the flask: 0.203 g.
- Every element, the mass and the energy are the same integers after 144,000 steps.
- Step cost: 0.67 µs (two cells, two reservoirs, the reaction table; trace built, checks off).

The water is still vapour at 20 °C in this study: it condenses in Stage 5, which is expected to shrink the
balloon to about 0.77 L and raise its CO₂ fraction to about 65% (planning estimates).

Tests:

- An open flask heated to 400, 600 and 900 K keeps T₀/T of its air, to 1e-7.
- Cooled again, it draws all of it back, to 1e-7, and is then static for 20,000 steps.
- A chain of two rigid cells settles in one step, both full to 1e-9.
- The sweep: 2000 random worlds, 120,000 steps, 69,000 gas flows, 130,000 reaction steps, 638,000
  heat transfers; every invariant holds.

## Known limits

- **Only 80% of the CO₂ reaches the balloon.** The flask is well mixed, so the last gas made
  stays in it. A player who wants more can purge or cool and reheat; that is honest chemistry.
- **No diffusion between cells.** Flask and balloon compositions differ until gas flows.
- **The balloon's gas is at room temperature within a minute:** one cell, 0.5 W/K to the room.

## Tests

- `gas: …` (7 cases): the ideal gas volume, expulsion at three temperatures, drawing back and
  rest, a chain, the balloon over the flame, a starved flask, sealing and walls
- `invariants: …` (4 new cases): the honest gas step; not exactly full (V1); sealed (V3); gas
  flowing into a full flask (T1)
- `sweeps: rigid cells stay exactly full on every step of every random world with volumes`
- ctest `chem_tool_study_balloon`
