# TDD 31: The pour by hand (Stage 31)

## The request

The third stage of the plan of TDD 29 (the user, 2026-11-22): a vessel tilts about its pouring tip
when the player presses over a target; its liquid pours when it reaches the lip; solids pour too
(what is suspended with the liquid, the sediment when its flat section reaches the lip); the stream.
Decided with the user: the scripted `pour {from, to, ml | all}` stays as it is.

GAME_SPEC §6 (the drain), §12.3 (a stream), §13 (pouring), §13.1 (the gesture), §20.

| Phase | Topic | Status |
|---|---|---|
| 31.1 | The kernel: a drain carries a share of the solids with its liquid, and solids run by themselves at a rate of their own | Done |
| 31.2 | What runs out of a round mouth: one law, a weir to an orifice | Done |
| 31.3 | The lab: a pour by hand from what lies in the vessel at its tilt; decanting; a dry powder | Done |
| 31.4 | The screen: to the pouring place, the tilt, back to the hand; the stream | Done |
| 31.5 | A person-paced playthrough test; pictures from the truth and from the GPU | Done |
| 31.6 | The user's test and sign-off | |

## What was built

- **The drain** (`chem/world.hpp`, `sim.cpp`): `set_drain(edge, rate, carried, solids_rate)`. Of the
  upper cell's solids the liquid takes its own share of `carried` (what is suspended); `solids_rate`
  of them (their own volume a second) runs by itself, liquid or none. `carried` 1 and no rate of
  their own is exactly the Stage 27 drain that passes solids (the same bits: no record changed).
- **What runs out of a round mouth** (`contents::over_brim`): Torricelli's speed over the wet part
  of the opening, `Q = Cd ∫ sqrt(2 g · depth) dA`. The mouth is a disc turned with the vessel, each
  place of it `s sin θ` over the lip and `2 sqrt(s (2R − s))` wide. Short of lying on its side the
  liquid leaves level, through the opening as seen from the side (a factor `sin θ`): a sharp-crested
  weir, for a small head `Cd sqrt(2g) (π/4) sqrt(2R / sin θ) h²`. Turned further it leaves along the
  axis, through the opening itself: upside down, `Cd π R² sqrt(2 g h)`. Continuous through a
  quarter turn. `Cd = π / (π + 2)`, Kirchhoff's contraction of a jet from a slot (0.611; weirs and
  orifices are measured at 0.60 to 0.62: `docs/DATA_SOURCES.md`).
  - The first form, a weir along the brim's arc, counted the brim's perimeter once the whole mouth
    was under liquid (190 mL/s through a 33 mm neck under 1.5 cm); the opening's area is what
    passes it. Found by the first picture.
- **The lab** (`by_hand`): each step, `contents::lying` at the tilt's turn; the head over the lip
  gives the rate; never more in a step than stands over the lip's level. With the liquid goes the
  share of the solids it holds up; the sediment by the same law from its flat section's head. The
  view gives the turn, the head and what runs.
- **The screen**: pressed over a vessel it pours into, what is in hand goes in 0.25 s to where its
  tip is over the middle of the other's mouth, 10 pixels above it, and tilts (2.5 s to the full
  tilt); let go, upright again (0.6 s at most), then back to the hand, where `unpour` ends it;
  pressed again on the way, it tilts again. It pours from the lip nearer the other's axis.
- **The stream**: lengths of a column, each as wide as `2 sqrt(Q / (π sqrt(v0² + 2 g y)))` there;
  its colour its liquid's at its concentration (the Stage 27 column read the whole vessel's solutes
  as in one millilitre).

## Measured

- The vessels' own rates are what their mouths pass under a small head, well tilted: the beaker's
  25 mL/s under 6.4 mm, the flask's 12 under 5.5, the bottle's 15 under 6.1.
- The bottle, 250 mL in 500: its lip comes down to the acid at 84.9° of its 109.2°. Tilted to 91.4°
  for half a minute: 114.5 mL ran and 6.7 mL still stood over the lip (the head falls as 1/t).
- A beaker of water over 12 g of copper oxide: decanted with no oxide carried; at the full tilt all
  the oxide follows. Stirred up, each drop carries its share (within 1%). The jar's 5 g of dry
  malachite: nothing at half tilt, all of it at the full.
- A person's pour (`ui: a person pours by hand at their own pace`): 2.2 s held before the acid
  runs, the bottle at 85°; the stream from the lip over the flask's mouth, thinning; let go and
  pressed again on the way back, it pours on.
- The GPU (M3 Pro, 1920 × 1080, `LAB_BEAM=5`): 69 ms a frame while the bottle tilts and pours
  (the renderer's cost is left as it is: TDD 29, 29.9).

## Open

- **No air comes in against the liquid**: a mouth under liquid all over does not glug. A bottle
  turned right over empties in about 2 s; a real one takes 5 to 10 (§20).
- The last of a pour dribbles (the weir's 1/t); a real lip holds a drop by surface tension.
- Sediment runs by the liquid's law (§20).
- A vessel in hand turned over a vessel stands in front of it until it has tilted clear.
