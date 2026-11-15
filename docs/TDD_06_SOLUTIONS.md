# TDD 06: Solutions and crystals (Stage 6)

## The request

> … then create a Copper Sulfate solution, and finally recover recover pure water and grow Copper
> Sulfate crystals (final container has an amount of pure solid Copper Sulfate, if the state of
> crystal can be tracked and simulated, that's a bonus).

Stage 6 is solution chemistry (GAME_SPEC §7, §9). With it every process of the demo exists: the
oxide dissolves in the acid, the solution boils down, and crystals nucleate and grow as it cools.
The crystals' state is tracked: how many, and how large.

| Phase | Topic | Status |
|---|---|---|
| 6.1 | Data: the pentahydrate and the sulfate ion verified; CuSO₄(aq) fitted to the solubility curve | Done |
| 6.2 | Activities of solvent and dissolved species, in the entropy, the affinity and the probe | Done |
| 6.3 | The surface law: area from particles, a driver, stirring, an exact step | Done |
| 6.4 | Particles: counts per solid, inheritance, nucleation, derived size | Done |
| 6.5 | Reactions: the oxide's dissolution, crystallisation, one-step dehydration | Done |
| 6.6 | Invariants X1 and X2 with self-tests; the solubility oracle | Done |
| 6.7 | `chem_tool study dissolution` and `study crystals`; calibration | Done |
| 6.8 | The user's sign-off | Pending |

## Decisions

- **The dissolved salt is fitted, and says so.** The game's solution is ideal-dilute with neutral
  solutes. Fed the tabulated ionic values, that model puts copper sulfate's saturation near
  0.002 mol/kg; it is 1.3. Two numbers, the dissolved salt's enthalpy and entropy, are therefore
  fitted to the handbook's solubility at five temperatures. The fit's heat of solution is
  11.2 kJ/mol. The handbook curve is then both the calibration and the test, which is weaker than
  an independent oracle: the test shows the simulation reproduces its own fit to 1.5%, and that
  two parameters suffice for five points.
- **The solvent's activity had to change.** The plan had `a_w = n_w / (n_w + n_dissolved)`. With
  solutes at their molality that pair violates the Gibbs-Duhem relation, so the entropy was not a
  function of state, and the reacting sweeps found steps where it fell. The consistent form is
  `ln a = −n_dissolved / n_liquid`. A second inconsistency went the same way: a liquid absent from
  a liquid that is present has activity 0 (liquids mix), not 1.
- **A solute needs a solvent.** The sweeps also built cells with dissolved species and no liquid,
  where the first drop of liquid made the entropy jump. That is not a state a solution can be in:
  it is refused when a world is built and checked as X2. The dynamics cannot reach it, because the
  last of the solvent has activity zero.
- **Particles are state, outside the two primitives.** A count is not conserved, so it cannot go
  through `transfer` or `transform`. The primitives keep it honest (a solid that appears is one
  lump, one that is gone has none); the step sets how many (grains inherited, nuclei born).
- **One law for dissolving and growing.** The surface law's area comes from the amount and the
  particle count, and its direction from the affinity. With that area the cube root of the amount
  moves linearly, so the step is exact for any rate.
- **Few large or many small is not a rule.** Nucleation and growth draw on the same
  supersaturation. Cooled slowly, the first crystals keep it low and few more form.
- **One-step dehydration.** No source was found for the trihydrate's and monohydrate's data, so
  they stay inert, and the pentahydrate goes to the anhydrous salt in one reaction between two
  verified species. Blue goes to white without the pale stages.
- **Calibrated constants are a table.** Four rate constants are chosen for play. They are listed
  with what they give in `docs/DATA_SOURCES.md`.

## Evidence

`chem_tool study dissolution`: 3.597 g of CuO in 2.5 million grains (76 µm) under 100 mL of 1 M
acid.

| | half gone | 99% gone |
|---|---|---|
| 20 °C, still | 446 s | 2208 s |
| 20 °C, stirred (×3) | 147 s | 673 s |
| gentle flame, stirred | 84 s | 192 s |

`chem_tool study crystals`: that flask boiled down through the condenser, the burner off at 14 g
of water, then left to cool.

- The solution boils at 101 to 103 °C, hotter as it concentrates.
- 83.25 g of water reaches the beaker, and nothing else.
- Crystals appear as the flask passes about 31 °C and the crop is complete ten minutes later:
  5.032 g of CuSO₄·5H₂O in 140,946 crystals of 0.31 mm, 44.6% of the copper.
- The mother liquor keeps 2.10 mol/kg of CuSO₄ beside 4.59 mol/kg of acid in 11.9 g of water.
- No anhydrous salt forms: the flask never overheats.
- Every element, the mass and the energy are the same integers after 216,000 steps.

Tests:

- Solubility from an excess of crystals at 0, 10, 20, 60 and 100 °C: within 1.5% of the
  handbook, and the same from a supersaturated start.
- Heat of solution: an adiabatic beaker cools by the fitted 11.17 kJ/mol to 1%.
- Boiling-point elevation of a one-molal solution: 0.51 K, water's ebullioscopic constant to 3%.
- Stirring ×3 makes dissolution 3.0 times faster; 80 °C against 20 °C follows Arrhenius to 5%.
- Slow cooling: 108,000 crystals of 0.42 mm. Fast: 1.1 million of 0.19 mm. The same crop.
- Crystals at 200 °C in an open flask are 99% anhydrous in twenty minutes, in the same grains; in
  room air at half saturation they keep their water for a day.
- All sweeps pass with the solution model in the random chemistries.

## Known limits

- **Left-over acid acts with the wrong sign.** It lowers water's activity, which in this model
  raises the salt's solubility (2.1 mol/kg in the liquor above, against 1.3 in water). In reality
  the common sulfate ion lowers it. The crop is therefore smaller than a real one would be. Fixing
  it needs ions and activity coefficients (GAME_SPEC §10).
- **The model fails above about 11 mol/kg.** Past that its solubility has a second, unphysical
  branch. A solution boiled almost dry can reach it.
- **The demo's yield is 45%, and the crystals sit in acid.** Getting them out is decanting,
  Stage 8.
- **Step cost was not re-measured.** Two more rate laws and the solution's activities were added.

## Tests

- `solution: …` (10 cases): the solubility oracle, saturation from both sides, the heat of
  solution, boiling-point elevation, the oxide's dissolution, the limiting reagent, cooling slow
  and fast, overheated crystals, crystals in room air, refusals
- `invariants: …` (2 new cases): X1, X2
- ctest `chem_tool_study_dissolution`, `chem_tool_study_crystals`
