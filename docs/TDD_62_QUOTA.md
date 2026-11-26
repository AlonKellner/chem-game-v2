# TDD 62: Crystals to a quota (Stage 62)

## The request

The curriculum's step C, its last experiment (`docs/CURRICULUM.md`: 50), after Stage 60's sign-off: the user's
"Approved, merge and proceed" (2026-11-26). Its number is 62: another agent's Stage 61 (supplies, storage, trash) holds
61.

## What the simulation showed first

Malachite (1.90 g) in 20 mL of 1 M sulfuric acid, stirred: all of it dissolved in 20 min, the acid a little in excess.
Boiled down to about 7 g of water, it stays clear while hot; cooled, it nucleates below about 28 C and its crop grows
over two hours to 2.905 g, 68 % of the 4.29 g its copper could make: the rest stays in the saturated mother liquor
(with the acid in it, the common ion lowering its solubility a little).

## The design

No new mechanism: the planner (Stage 60), the balance (50), the filter (59), goals (17).

- The experiment `quota`: the planner works back from 3.00 g of the pentahydrate at 99 %, crystallizing expected at
  70 %, by malachite in the acid (1.90 g of malachite, 17.2 mL of the acid), and sets the goal on slot 1; the
  malachite weighed by a hand, dissolved in 20 mL of the acid, boiled down, cooled until the crop has grown past 2.5 g,
  the liquor poured through the filter, the crystals scraped after it, drained, into a dish, handed in on slot 1.
  Its report: the crystals' mass (predicted first) and the share of the copper that crystallized.

## Phases

| Phase | Topic | Status |
|---|---|---|
| 62.1 | The exploration | Done |
| 62.2 | The experiment: crystals to a quota | Done |
| 62.3 | This document, GAME_SPEC, CHANGELOG, `make gates`, the user's sign-off | `make gates` passed (2579 s: ASan+UBSan 2579, ctest 1692, Godot 13 of 13, goldens unchanged); awaiting the user's sign-off |

## The goldens

None moved.

## Measured

- `quota` at the bench's seed: 1.86 g of malachite weighed (the plan's 1.90, a hand's), the crop poured off at
  2.50 g, 59.5 % of the copper: short of the quota. The plan's 70 % was a guess; the crop harvested sooner is smaller,
  and the liquor keeps the rest.

**Tests**:

| Test | What it checks |
|---|---|
| `test_experiments.cpp` (3) | `quota` strict (the plan's goal on slot 1, the malachite within a hand's 10 % of the plan, the crop, none through the paper, the report and the prediction), at a person's pace, free |

## Open

- **The crystals' nucleation** needs a deep supersaturation (Sheng's rate, a lower bound): a cooled liquor stays clear
  to below 30 C, where a real one would crystallize a few kelvin under saturation (Lyall: a metastable zone of 3–4
  K); seeding and secondary nucleation (C16) are the fix.
- **A crop's purity is 100 %**: a drained residue holds no liquor (§20, Stage 59's).
