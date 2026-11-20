# TDD 22: Everything that takes space is physical (Stage 22)

## The request

> I actually want all elements including things like signal parts and wires and burner - all
> should be physical. If it's an actual space taking part - it should have a spectral visual. The
> only non spectral colors and elements should be overlays like highlights and interaction
> elements. Interactive elements like the "stir" button and the burner slider should not be usually
> visible, but only when the element is selected, maybe even in an actions panel to the side and
> not in the lab grid, because it can obstruct the spectral rendering.

The plan was approved on 2026-11-20, with: colours calibrated, not measured; a part's state as a
lamp; the drip from a condenser's outlet a flat highlight for now. GAME_SPEC §12.3, §13.1, §20.

| Phase | Topic | Status |
|---|---|---|
| 22.0 | Quicker gates (asked for by the user): a ctest entry a test file, the reference's heavy loops on threads, sparse samples under the sanitizers. The sanitizer gate 35 min -> 5 min; then the sanitizer build at -O2: its tests 237 s -> 102 s | Done |
| 22.1 | The spec; the calibrated rows | Spec done; rows pending |
| 22.2 | The scene: 256 bodies, spectra named by set, a grid of squares for a pixel to find its bodies | Done: see below |
| 22.3 | Three shapes: the slab, the bar, the cone; opaque and glowing | Pending |
| 22.4 | The screen: parts, lamps, wires, the burner and its flame, the outlet as bodies; nothing of the lab drawn over the pass, and a test that holds it | Pending |
| 22.5 | The controls in the readout's column; the tutorial, the tests and the tools | Pending |
| 22.6 | The GPU host; O4 on every picture, one of a vessel held over a part and a burner; the cost | Pending |
| 22.7 | Pictures; the user judges by eye and signs off | Pending |

## The scene's capacity (22.2)

- 256 bodies (16 before), each naming its set of spectra (`SET`); a vessel's set is its own. The
  rows are as many sets as there are (`ROWS_PER_SET`), not a fixed block.
- The screen in squares of 40 px (48 x 27): each lists up to seven bodies that reach into it, the
  nearest the eye kept. A pixel looks at its square's alone. What stands behind a vessel on a
  layer further from the eye is found when the scene is composed, and signed for the pixel's history.
- A layer's set rides in its body number (body + 256 times its set): a field more on each of a
  pixel's four layers measured 0.1 ms in one comparison.
- O1 refuses a body made of a set that is not there. 200 bodies on the screen: each pixel is what it
  is with its own body alone (to 1e-9).
- **The cost**, back to back, three runs each: before 2.17, 2.17, 2.28 ms a restarting pass; after
  2.28, 2.20, 2.27. The same within the noise. (The machine was slower than when Stage 21 measured
  1.64: measured apart, an hour between, the two read 2.10 and 2.35, which looked like a regression
  and was not. Costs are compared back to back from here on.)

