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
| 22.1 | The spec; the calibrated rows | Done |
| 22.2 | The scene: 256 bodies, spectra named by set, a grid of squares for a pixel to find its bodies | Done: see below |
| 22.3 | Three shapes: the slab, the bar, the cone; opaque and glowing | Done: see below |
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

## Three shapes (22.3)

- **The slab, the bar and the cone** (`thing_at`, `native/game/shader/optics_pixel.glsl`): a body
  with no item, a shape made of a surface (a reflectance) and an emitter (a light), from the
  optical data (`surfaces`, `emitters`), embedded at build. Things of one make share a set of
  spectra: fifty part faces are one.
- A slab and a bar are opaque, lit by the room's light as a heap of solid is; a lamp is a slab that
  also emits; a flame is a cone of gas that emits along the path through it and absorbs nothing
  (a layer's unused `wet` carries that path: no field more on a pixel's layers).
- **Their outline and path are read at 4 x 4 places in a sample.** Read at its centre alone, a
  3 px bar's edge was 7.8 off on the eye metric and a flame's 8.5, against a plain wall.
- **At an opaque thing's edge the pixel reads the wall where it is uncovered** (four parts across,
  each by its uncovered share), once, in the pixel's path, for edge pixels only. Mixed with the
  wall over the whole pixel, a bar over a wall of thin lines was 9 off.
- **The truth reads the drawn wall exactly over each of its samples** (`drawn_over`): it had
  counted a line thinner than its samples by how many of them caught it (6 of 8 for a 0.7 px line).
  Behind what bends nothing, both the truth and the renderer now read the wall itself, not the flat
  backdrop: a balloon and a flame are seen through, and the bench's drawing with them.
- **O2** counts what bodies emit (a lamp, a glowing solid, a flame along its longest path).
- Measured: against 64 rays a pixel, a slab worst 1.4 on the eye metric, a bar 1.4, a flame 0.7,
  a part and a flame through a held vessel of water 1.5. O4, none visible on 14 pictures. The cost,
  back to back: before 1.87 and 1.88 ms, after 1.89 and 1.97.

