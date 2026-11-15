# TDD 05: Boiling and condensation (Stage 5)

## The request

> … and finally recover recover pure water and grow Copper Sulfate crystals …

Stage 5 is phase change (GAME_SPEC §8): water's liquid and vapour, in every cell, each step. With
it the demo's third stage runs without its player or its salt: water boils in the flask, condenses
in the condenser and runs into the beaker.

| Phase | Topic | Status |
|---|---|---|
| 5.1 | Oracle data: water's saturation properties from NIST | Done |
| 5.2 | The equilibrium law as data (`"law": "equilibrium"`) | Done |
| 5.3 | `AffinityProbe`: a reaction's affinity along its own extent at fixed enthalpy (`chem/thermo.hpp`) | Done |
| 5.4 | `reach()`: the last quantum before the affinity changes sign; rate reactions use it too | Done |
| 5.5 | Settling: equilibria and volumes together, deepest cell first, last in the step (`chem/sim.hpp`) | Done |
| 5.6 | Drains: liquid down a directed edge | Done |
| 5.7 | Invariants P1 and V2 with self-tests | Done |
| 5.8 | Fast reactions in the random sweeps | Done |
| 5.9 | `chem_tool study distillation`; the balloon study with condensation | Done |
| 5.10 | The user's sign-off | Pending |

## Decisions

- **Phase change is a reaction like any other.** `H2O(l) -> H2O(g)` is a row of the reaction
  table; its heat is the difference of two formation enthalpies; its equilibrium is where its
  affinity is zero. There is no saturation curve in the code and no latent-heat constant: the
  saturation pressure is what the species table implies, and NIST's curve is a test.
- **Fast means solved, not stepped.** Boiling against a 365 W flame is stiff as a rate. The
  equilibrium law takes the reaction to the last quantum before its affinity would change sign, at
  fixed enthalpy: boiling cools the cell to the boiling point, so the heat that arrived decides
  how much boils.
- **Equilibria and volumes settle together, last.** Taking an equilibrium changes the gas a cell
  holds; drawing gas changes its equilibrium. Each cell, deepest in the gas tree first, takes its
  equilibria, then its volume; if it drew, both again. The step ends with every cell at
  equilibrium and full, which is what P1 and V1 check.
- **One probe for the step and the checks.** `AffinityProbe` answers "what would the affinity be
  after this much of the reaction" in closed form, with no copy of the cell. The step searches
  with it and T1 and P1 ask the same object, so they agree to the bit. It replaced the trial-state
  copies of Stage 3.
- **P1's tolerance is the rounding it has to forgive.** The first version allowed "a few quanta
  of the reaction". It fired during evaporation: after a flask's equilibrium is taken it expels
  gas rounded to whole quanta per species, which shifts the vapour fraction by about one part in
  10⁹, and a few quanta of the reaction do not undo that. The tolerance is now that shift:
  `2 N R T / (the cell's gas in quanta)`.
- **A cell is never left over-full; it may be left under-full, flagged.** The sweep found a rigid
  cell with a fast gas-consuming reaction that emptied itself as fast as it was refilled. It
  draws its fill at most eight times a step, then expels any overfill and is marked starved: the
  step's limit on how fast a cell can pull gas in.
- **Drains are kinematic.** All the liquid, each step, as much as fits below. No rate, no
  gravitational energy.

## Evidence

`chem_tool study distillation`: 100 g of water, a 2200 K flame through 0.25 W/K, a condenser
cooled at 15 °C through 10 W/K, a beaker.

- The flask reaches the boil at about two minutes and sits at 100.44 °C, holding steam alone.
- It boils off 10.16 g a minute until it is dry at just under 12 minutes.
- The condenser settles at 56.6 °C with 16.7% vapour: the temperature at which its coolant takes
  what the steam brings. Air is drawn in to fill what condensing leaves.
- 99.98% of the water that left the flask is liquid in the beaker; 0.008 g of vapour reached the
  air. The beaker holds water, its own vapour and air: no other species.
- The coolant took 245 kJ of the 353 kJ the flame gave.
- Every element, the mass and the energy are the same integers after 46,800 steps.

`chem_tool study balloon`, now with condensation: the hot balloon holds 0.753 L at 64.7% CO₂,
2.3% water vapour and 33.0% air, with 0.353 g of liquid water inside. Planning estimated 0.77 L
and 65%. After the flask cools and draws gas back: 0.562 L.

Tests:

- The table's saturation pressure is within 2% of NIST from 25 to 100 °C, its heat of
  vaporisation within 0.6%, its boiling point 0.46 K high.
- Boil-off rate against power over latent heat: within 1e-5 over a minute of steady boiling,
  with the flask within 1e-6 K of the boiling point throughout.
- The sweeps, with a fast reaction in one world in three: every invariant holds on every step.

## Known limits

- **Water boils at 100.44 °C.** Steam is an ideal gas with a constant heat capacity. A player
  with a thermometer can see it.
- **Step cost is 2.5 µs** for the distillation's four cells, half the budget of 5 µs for the
  demo's world. The cost is in building the trace and in settling; Stage 7's benchmark is where
  it is addressed.
- **The beaker cannot overflow.** A rigid cell that receives more liquid than it holds violates
  V2 until Stage 8 adds the spill tray.
- **Boiling dry is fast and hot.** The flask goes from 100 °C to 350 °C in two minutes once the
  water is gone. That is honest, and it is how the crystals will burn in Stage 6.

## Tests

- `phase: …` (7 cases): the data oracle, evaporation under dry air and rest, condensation,
  boiling dry, the boil-off rate, distillation, a drain into a full vessel
- `invariants: …` (2 new cases): away from phase equilibrium three ways (P1); more liquid than
  the vessel holds (V2)
- `gas: malachite over a flame fills a balloon …`: now counts the water that condensed
- ctest `chem_tool_study_distillation`
