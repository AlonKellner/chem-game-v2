# TDD 20: Pixel footprints (Stage 20)

## The request

> The background sometimes may have thin lines or detail … the refraction of a pixel might land on
> one side of a background line, and an adjacent pixel might land on the other side, never
> actually landing on the line itself, so the color and light of it are completely gone. … We
> might have more complicated refraction in the future, like spherical containers and cones.

Agreed in conversation (2026-11-19):
- **A pixel is the average of all its rays**, traced through every body by one general tracer.
- **Bending neither makes nor loses brightness.** The radiance theorem: a uniform wall stays
  uniform. What changes is how much fine detail a pixel's rays gather.
- **Caustics come later.** Light focused onto a surface is a stage of its own.

GAME_SPEC §12.3, §18 (O7, O8), §20.

| Phase | Topic | Status |
|---|---|---|
| 20.1 | The spec: a pixel the light of all its rays; the tracer; the footprint; O7, O8; the truth independent of the renderer | Done |
| 20.2 | The truth: a brute-force average of sub-pixel rays, traced by its own exact tracer, in float64 | Done |
| 20.3 | The tracer: one general tracer (a medium on each side of each boundary, vector Snell, turned back exactly) for the cylinder and the prism; the closed form and the truth's tracer agree with it to 1e-9 | Done |
| 20.4 | Footprints: the edges' and middle's rays, the wall's exact share where smooth (two halves), eighths where not, one ray at a moving point's place for the rest; the band cuts at the footprint's ends | Done (correct; too costly on the GPU, see Open) |
| 20.5 | O7 and O8, with self-tests (a renderer that brightens where rays crowd; centre sampling) | Done |
| 20.6 | Cost and pictures: `make screenshots`, O4 against the new truth, before and after | Pending |
| 20.7 | The user judges the pictures by eye and signs off | Pending |

## The redesign: refraction maps (2026-11-20)

The user asked for the design to be challenged, with deeper reasoning, and approved the result
(the plan: a general tracer on the CPU, a refraction map per body, the wall as a table with exact
integrals, a pixel pass of lookups).

| Phase | Topic | Status |
|---|---|---|
| 20.6 | The spike: `bend_at` a map lookup, everything else as built | Done: see below |
| 20.7 | The spec: §12.2, §12.3, §20 | Pending |
| 20.8 | The tracer on the CPU: surface patches, 3D rays | Pending |
| 20.9 | Maps: knots across a body, rows in wavelength | Pending |
| 20.10 | The wall table and its exact integrals | Pending |
| 20.11 | The pixel pass, in the reference: lookups and the wall over a band's sweep | Pending |
| 20.12 | The GPU host: the maps' and the wall's buffers; the cost | Pending |
| 20.13 | Pictures before and after; the user judges by eye and signs off | Pending |

### The spike, and the measurement corrected

- **The measurement was flawed.** A restarting pass's cost was read as the slope of the frame time
  between 2 and 6 extra passes. Frames are capped near 8.3 ms, so a pass cheaper than about 1.2 ms
  read as free, and the "1.78 ms" of before the stage was an underestimate.
- **Corrected:** it is now read between 8 and 24 extra passes (`engines/godot/tools/screenshot.gd`).
  The pass budget makes the cost per pass depend on the count, so numbers compare only within one
  protocol.

| Build | A restarting pass | `screenshot.gd`, windowed |
|---|---|---|
| `main` before the stage (95ba865) | 1.07 ms | 69 s |
| The general tracer on the GPU, no footprints (19a3bfd) | 1.50 ms | 105 s |
| Footprints with the GPU tracer (88461a8) | 8.94 ms | 210 s |
| The spike: footprints, `bend_at` a map lookup | 4.59 ms | 62 s |
| … the wall read as a constant | 2.63 ms | |
| … and no search for cuts | 1.85 ms | |
| One lookup a point, wall constant, no cuts | 0.55 ms | |
| No lookup (the floor) | 0.53 ms | |

- **A map lookup costs 0.02 ms over the whole screen.** The tracer need not be in the pixel loop.
- **What remains above the floor** is the wall's shape loop (2.0 ms), the cut search (0.8 ms) and
  the footprint's subdivision (1.3 ms): what the wall table and the exact sweep replace.
- **The gate for the GPU step:** a restarting pass at or under `main`'s 1.07 ms.
