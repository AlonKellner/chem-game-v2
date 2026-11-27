# TDD 66: Seeds (Stage 66)

## The request

The curriculum's step C2 (`docs/CURRICULUM.md`: C16, experiment 35), chosen by the user after Stage 67's sign-off
(2026-11-26: "Approved, merge and proceed"; then "C2: Seeds"). Stage 60 found it: a solution 5 % over saturation stays
clear for an hour, as it should, but a 10 mg seed dropped in grows only to 15 mg in the next hour, where a real one,
stirred, sets the excess out in minutes. The model nucleated only from the solution.

## What the simulation showed first

`chem_tool study seed` (new): 100 g of water held at 25 °C, copper sulfate at 1.05 times the model's solubility
(1.3574 mol/kg, S = 1.0533), a 10 mg crystal of the pentahydrate (one crystal of 2.0 mm):
- still: 10.0 → 11.3 mg in two hours, one crystal;
- stirred by the lab's stirrer (0.01 W, 0.08 W/kg): 10.0 → 35.3 mg in two hours, one crystal, S 1.0526.

## The design

- **Secondary nucleation** (GAME_SPEC §9): where a surface law grows its solid and the cell is stirred, the crystals
  breed: `secondary · ε · M · (S − 1)^secondary_order` new particles a second, ε the stirring's dissipation per kg of
  the liquid (`dissipation_per_kg`, Stage 57's), M the kg of the crystals there, S = e^(A/RT). Each fragment is one
  quantum of the solid, as a primary nucleus is; both share the step's carried remainder.
- **Data**: `secondary` and `secondary_order` on a surface law's kinetics (`data/reactions.json`), both or neither
  (the generator refuses one alone: `datagen_refuses_secondary_alone`); the four forms of the salt 2e10 and 1.7.
  Calibrated (`docs/DATA_SOURCES.md`, Seeds): no measured rate for the pentahydrate is open (Ishii and Fujita 1981,
  Garside and Davey 1980, Lyall et al. 2004 paywalled; Tanimoto 1962 a scan). The form is the textbook one, linear in
  the dissipation and in the crystals' mass; the order is Lyall's measured nucleation order; the rate is set so the
  stirred seed sets out half the excess in about a quarter of an hour.
- **Unstirred, none**: a seed in a still solution grows alone.
- **The debug panel** says what the stirring breeds: "growing on N crystals, the stirring breeding X more a second".

## Phases

| Phase | Topic | Status |
|---|---|---|
| 66.1 | Measured first: `chem_tool study seed`, still and stirred | Done |
| 66.2 | The core: the kinetics' fields, the generator, the step's breeding; the debug panel | Done |
| 66.3 | Data: the calibration, `data/sources.json`, `docs/DATA_SOURCES.md` | Done |
| 66.4 | Tests: `test_seeds.cpp`, the generator's refusal | Done: 1.57 g of 1.94 out at 20 min, 2.2 million crystals at 40, S 0.99996; 1,134 bred in the first 10 s; `make check` 594 of 594 before them, the goldens unchanged (no session stirs a growing crop) |
| 66.5 | The experiment `seed` (curriculum 35): a bench and its tutorial | Done: strict, at a person's pace and free; the stirred beaker 1.03 g in 1.24 million crystals, the still seed 10.3 mg, one crystal |
| 66.6 | Sessions, plays and the goldens (with the user's approval) | Done: none moved (no session stirs a growing crop) |
| 66.7 | GAME_SPEC, CHANGELOG 0.66.0, `make gates`, the user's sign-off | `make gates` passed (51 min, merge level): core, ctest, ASan+UBSan, the Godot suite (13 of 13), the goldens; again after Stage 68 was merged in. Signed off by the user |

## Tests

`native/tests/test_seeds.cpp` (Stage 66):
- a still solution's seed only grows, one crystal;
- stirred, a seed breeds and sets out the solution's excess: over half of it by 20 min, S within 0.1 % of 1 by 40
  min, crystals of 30–100 µm, every invariant held;
- the crystals bred go with the stirring, the crystals' mass and (S − 1)^1.7 (each doubled, twice as many in 10 s);
- none are bred unstirred, without crystals, or where they dissolve.

`native/tests/game/test_experiments.cpp`: the experiment `seed` followed with its own gestures, strict (the stirred
beaker's crop 1.05 g within 3 % in over 100,000 crystals, the still seed one crystal under 20 mg; the report checked
beside the prediction), played at a person's pace, and free.

`datagen_refuses_secondary_alone` (ctest): a surface law with a breeding rate and no order.

## Open

- **Equal spheres** (§20): a seed's fragments share its mass at once, so a 2 mm seed and its first thousand fragments
  are a thousand crystals of 0.2 mm; the crop's area is overstated at first and the seed is not seen apart. A size
  distribution by moments (§9's full design) would keep them apart.
- **A measured rate**: Ishii and Fujita (1981) give the pentahydrate's primary and secondary rate constants against
  the impeller's Reynolds number; with it, the calibration becomes a fit with a gate test at their conditions.
- **The metastable zone**: a cooled, stirred liquor still waits for its first crystal on Sheng's primary rate (a
  lower bound), below the 3–4 K Lyall measured; once it has one, the stirring breeds the rest.
