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

## Open: the GPU's cost (2026-11-20)

- **The cost.** A pass where every pixel's history starts again costs about 18–20 ms on the GPU,
  against 2.4 ms with the general tracer alone and 1.8 ms before Stage 20. Settled frames cost
  nothing. The windowed `make screenshots` overruns its 90 s limit, and once the driver timed out
  waiting for the GPU.
- **Measured:**
  - Without the band cuts at the footprint's ends: 18.5 ms.
  - With an exact edge-cell average of the wall, in place of the one-pass average: 20.5 → 28 ms.
  - With a second level of splitting: 140 ms.
- **The rest is per point.** Each wavelength point traces four or more rays through the general
  tracer's loop, where there was one closed form.
- **Next:**
  - The cylinder's closed form behind the shape interface, an exact one for rays turned back
    inside its glass ring included.
  - The footprint's edge landings once a frame at a few wavelengths, interpolated, not traced at
    every point.
