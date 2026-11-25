# TDD 59: Filtering (Stage 59)

## The request

The curriculum's step C, its second half (`docs/CURRICULUM.md`: T6, S3), after Stage 58's sign-off: the user's
"Approved, merge and proceed" (2026-11-25). This stage takes filtering and the separation it makes possible
(experiment 3); the planner (S3) and experiments 8, 35 and 50 are the next stage's.

## The design

- **A filter funnel** (`data/glassware.json`, `filter_funnel`): a 60° cone 75 mm across on a stem 75 mm long (ISO
  4798's form, design), with a Whatman grade 1 paper folded in quarters. An outlet vessel, as a pipette is, with no
  stopcock: `attach`ed on a vessel's mouth it runs into it as it stands.
- **Its paper drains by Darcy's law**: each patch of the wetted cone passes water in proportion to the head over it,
  the stem's column hanging under it: `Q = k 2π tan 30° / cos 30° (h³/6 + L h²/2)`, h the liquid's depth in the cone
  (no higher than the paper), L the stem; `k` from the paper's Herzberg time (Whatman: 150 s for 100 mL of water at
  20 °C through 10 cm² under a constant 10 cm head: 6.7e-3 cm/s per cm), as water's viscosity goes. Once its cone is
  drained (under 3 mm), its stem runs out as an orifice.
- **It keeps every grain**: an outlet's drain carries liquid only (Stage 52's), so what is poured in leaves its solids
  on the paper.
- **A residue is scraped out**: a drained funnel holding only solids, carried onto a vessel, opens the pour menu
  ("tip in the solid"), where any other outlet vessel stands on it.
- **Its inside is its cone's**, from the apex up: the paper is its floor, and what it keeps rests on it; its stem a
  fine bore under it (as a measuring cylinder's foot is solid glass). Seen in the stage's first picture: the residue
  sat at the stem's foot, the inside's floor then.
- **A drip line** is drawn from a condenser's outlet alone: a burette, a pipette or a funnel delivers from its tip, in
  the mouth under it (the picture showed a line from the funnel's stem off to its side, Stage 52's burette's too).
- **The experiment** `separate`: copper oxide separated from copper sulfate by water and a filter, weighed.

## Phases

| Phase | Topic | Status |
|---|---|---|
| 59.1 | The paper's source and its gate; the funnel's profile | Done |
| 59.2 | The lab: its drain through the paper | Done |
| 59.3 | The screen: a residue scraped out | Done |
| 59.4 | The experiment: separating a mixture | Done |
| 59.5 | This document, GAME_SPEC, CHANGELOG, `make gates`, the user's sign-off | |

## The goldens

None moved: no session holds a funnel.

## Measured

- 28 mL of water through the funnel's paper in 35 s, a gram of copper oxide kept on it, none through.
- `separate` at the bench's seed: 0.9515 g weighed against the mixture's 0.95148 g of oxide; the filtrate holds all
  1.278 g of the sulfate.

**Tests**:

| Test | What it checks |
|---|---|
| `test_filter.cpp` (2) | the funnel's geometry and paper; a minute's drain, every grain kept; the paper's permeance at the Herzberg test's own conditions (the gate) |
| `test_experiments.cpp` (3) | `separate` strict (the weighed oxide within 0.5 %, nothing through the paper, the report and the prediction), at a person's pace, free |

## Open

- **A paper keeps every grain**: Whatman grade 1 retains 11 µm at 98 %, and the model's precipitates are 0.3 µm grains
  that would run through; a real fresh precipitate clumps into flocs a paper keeps, which the model does not make.
- **A residue is dry as soon as it drains**: a real paper and its powder hold water until an oven drives it off (a
  drying oven, T6, is to come), and a wash carries away the solution they hold.
- **No cake**: a growing layer of solids on the paper adds no resistance (Ruth's), so a fine precipitate filters as
  fast as water.
