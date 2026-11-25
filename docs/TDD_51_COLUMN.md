# TDD 51: More of the vertical dimension (Stage 51)

## The request

**After Stage 49** the user chose the next stage (2026-11-25): "Column leftovers". The option read: "a mixing
efficiency that falls for strongly layered liquids, a pour's stream mixing where it lands (jet laws), liquid volumes
that grow with temperature, a conical flask's narrowing in its layers." Stage 50 (the curriculum's measured lab) is
another agent's.

**During the stage** the user asked for the glass to be in layers too, as a future stage: "decide on the approach
which you think is best, document it and let's implement it as a future stage". The decision is in
`docs/EXPERIMENTS_ROADMAP.md`, area 8b: a fixed grid of glass rings coupled to the liquid's layers by overlap.

GAME_SPEC 0.51.0 covers:
- §6: a pour's stream;
- §20: its simplifications.

## Phases

| Phase | Topic | Status |
|---|---|---|
| 51.1 | A pour's stream mixes where it lands: its kinetic energy as work (Linden's 0.2), its entrainment (Ricou and Spalding) as far as it plunges (Turner) | Done; the demo's golden re-recorded with the user's approval (its measured output unchanged) |
| 51.2 | The mixing efficiency falling with the Richardson number | Not done: no sourced coefficient (below) |
| 51.3 | A conical flask's narrowing in its layers | |
| 51.4 | Liquid volumes that grow with temperature | |
| 51.5 | The glass in layers: the design (roadmap area 8b) | Documented; a stage of its own |
| 51.6 | GAME_SPEC 0.51.0, `make gates`, the user's sign-off | |

## 51.1 A pour's stream

Stage 47's rule: a pour no faster than 2 mL/s laid its liquid as a layer, and a faster one stirred it in at once. It
is replaced by the stream's own physics.

**In the lab:**
- every pour lands as a layer of its own on the liquid there (the surface pushed down a place at its first step);
- the stream lands at the speed of its fall: `sqrt(2 g h)`, h from the lip (or outlet, or cap) 1 cm over the mouth
  to the liquid's surface (`set_landings`);
- a gentle pour (`"gentle": true`, the menu's "pour gently") runs down the wall and lands at no speed.

**In the core** (`strata.cpp`): what drained into the column's top this step (`landed_m3_`, `landed_kg_`, measured
by the drain process) landed at `CellMeta::landing`.
- **Its kinetic energy**, ½ m u², is work done on the column. Linden's 0.2 of it raises the column's potential
  energy, as a stirrer's turning does (Stage 49).
- **Its entrainment comes first.** A jet of flux q and momentum flux M = q u entrains 0.32 sqrt(π/4) sqrt(M) a metre
  of its path (Ricou and Spalding: 0.32 Q0 x / d0). It passes the top, where it lands, then each layer under it: one
  no denser than its mixture it passes whole; into a denser one it plunges no further than a forced fountain rises
  (Turner: 2.46 M^(3/4) B^(-1/2), B = g' q against the first denser layer).
- **What it entrains it lifts into the top**, edge by edge with its enthalpy, paying for each cubic metre lifted from
  layer k g [Σ between (ρ_k − ρ_j) h_j + (ρ_k − ρ_top) h_top / 2] from the work. The layers between sink as layer k
  thins: an exact first-order energy, not the distance between middles. The first version charged that distance,
  and the column gained only 71% of the work.
- **What is left of the work** mixes the layers from the top down (the Stage 49 mixer, now one routine for both:
  `mix(budget, from_top)`).

**What it does:**
- Two liquids of nearly one density mix fully as they are poured: the experiment `neutralisation`'s base into acid,
  about 2% apart. Entrainment alone left 0.27 g of the acid unmixed and failed that experiment, which is what led to
  the energy.
- A fast pour of 40 mL of the green from 7 cm onto 80 mL of the blue carries about 30 mJ. A fifth of it is many
  times the 0.9 mJ that make them one, so they are one.
- A pour by hand from the lip mixes them over its pour too: a stream from a few centimetres stirs a layered drink,
  which is why a bartender pours over a spoon.
- The menu's "pour gently" lays them clean.

**Tests:**
- `measured: a round jet entrains at Ricou and Spalding's 0.32 Q0 x / d0; one driven into a denser liquid plunges
  to Turner's 2.46 M^(3/4) B^(-1/2)`: the two laws as pure functions, `jet_entrains` and `fountain_reach`, against
  their sources' forms;
- `measured: a stream landing on a layered column raises its potential energy by Linden's 0.2 of its kinetic
  energy`: 47.85 µJ gained of 47.86 µJ paid in a step, against a still copy;
- `column: a stream landing slowly, or on a liquid much denser, plunges less far`: a trickle at 0.3 m/s lifts less
  than its whole path would;
- in the lab: a gentle pour lays the green on the blue, a fast one makes them one; the screen's "pour gently" and
  "pour".

**The goldens:** the demo's digests change from its first pour onto liquid (command 9), and its measured output is
identical line for line. The pressure session is unchanged. Re-recorded with the user's approval (2026-11-25).

**Tests' tolerances.** doctest's `Approx(v).epsilon(e)` allows `e × (1 + max(|a|, |v|))`, an absolute tolerance below
1. Stage 49's energy test (micro-joules) therefore passed on more than it claimed. Its checks, and the Fick, jet and
oxide checks of `test_column.cpp`, now compare ratios to 1. Elsewhere `test_measured.cpp`'s Planck check
(9.924e-3 at epsilon 1e-3, really ± 10%) is as weak; it is another stage's and is noted in Open.

## 51.2 The mixing efficiency and the Richardson number

Linden (1979) shows the mixing efficiency rising from zero with the Richardson number to its maximum, 0.2, and then
falling. Experiments fit the entrainment across an interface stirred by a grid as E = K Ri^(−n) (Turner 1968: n = 1
for heat, 3/2 for salt). The exponent is disputed (Fernando 1991's review: n > 1; some argue n = 3/2 comes from a
misreading), and no coefficient K was found to source (searched 2026-11-25). By the project's rule (a value is used
only once its row is verified), the efficiency stays Linden's 0.2. Open: the full texts of Turner 1968, Thompson and
Turner 1975, and E and Hopfinger 1986, for K.

## Open

- The efficiency's fall at a high Richardson number (51.2).
- `test_measured.cpp`'s Planck check at 10 µm is ± 10% under doctest's absolute tolerance (51.1, the tests'
  tolerances).
- A pour of a denser liquid onto a lighter one overturns whole (Stage 47's simplification); a sinking plume would
  entrain on its way down.
