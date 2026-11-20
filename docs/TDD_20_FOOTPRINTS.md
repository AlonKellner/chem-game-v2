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
| 20.7 | The spec: §12.2, §12.3, §20 | Done |
| 20.8 | The tracer on the CPU: surface patches, 3D rays (`lab/optics_trace.hpp`) | Done |
| 20.9 | Maps: samples across a body, pieces in wavelength | Done |
| 20.10 | The wall table and its exact integrals | Done |
| 20.11 | The pixel pass, in the reference: lookups and the wall over a band's sweep | Done |
| 20.12 | The GPU host: the maps' and the wall's buffers; the cost | Done. The cost gate (1.07 ms) was not met: 1.77 ms, which the user accepted on 2026-11-20 ("Approved, proceed") |
| 20.13 | Real glass: each vessel's wall thickness as sourced data | Done |
| 20.14 | Pictures before and after; the user judges by eye and signs off | Pictures taken; the sign-off pending |

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

## What was built (2026-11-20)

- **The tracer** (`native/game/src/optics_trace.cpp`): float64, on the CPU. A body is a list of
  surface patches (a quadric or a plane, its bounds, the medium on each side); rays are 3D;
  vector Snell, Fresnel, total internal reflection. Held to the cylinder's closed form to 1e-10,
  the prism's least deviation, and a ball lens's focus (the path that bends up and down).
- **A map per body**, traced once:
  - four samples a pixel across, on the screen's own quarter pixels;
  - each whole pixel's level: 1, 2 or 4 equal parts;
  - pieces in wavelength, even in 1/λ², a ray the cubic through each piece's four nodes. A
    vessel is one piece (about 3,000 rays, 0.6 ms); the prism 16 to 32.
- **The wall as a table**: breakpoints across, a profile for each row of pixels, the running
  integral in two floats. No limit on shapes; overlaps exact.
- **The pixel pass**: a pixel is its parts; the wall is integrated exactly over each part's
  footprint and over the band's sweep; liquids and surfaces are read once a step.
- **The truth** stays independent: its own tracer, the wall read from the shapes themselves.

## The cost on the GPU, and what it taught

Protocol: a restarting pass, read between 8 and 24 extra passes, at 1920 x 1080, on an M3 Pro.

| Build | A restarting pass |
|---|---|
| `main` before the stage | 1.07 ms |
| Footprints with the tracer on the GPU (88461a8) | 8.94 ms |
| Maps and the wall table, first working (the pixel pass as lookups) | 9.0 ms |
| The spectra read once a step, the parts sharing their boundary rays (6b39e95) | 3.9 ms |
| A reading for one-piece maps with nothing it never needs | 2.4 ms |
| Four samples a pixel (a pixel in four parts at most) | 1.72 ms |
| Work groups 2 x 32 pixels, not 8 x 8 | 1.56 ms |
| A rim's two sides carried on from their neighbours (O5 at the rim) (a2f36fc) | 1.77 ms |
| The same with one part a pixel everywhere (a bound, not a build) | 1.04 ms |
| No bodies near (the floor) | 0.53 ms |

`make screenshots`: 20 s (69 s on `main`); O4 on all 13 pictures, the largest difference 2 on the
eye metric.

**Findings:**
- **The GPU pays for what a band could do, done or not.** Removing a branch that no pixel ever
  took (a ray read between two samples) saved 0.9 ms; a rare one (a part split at a route's
  change) 0.5 ms; the wall's slow paths 0.75 ms. Marking branches (`[[dont_flatten]]`) changed
  nothing. So the hot loop must hold only what most pixels need, and the rest sits in a separate
  reading chosen once a pixel.
- **Reads are not the cost.** Halving the buffer reads of a band changed nothing; laying a
  sample's rows side by side in memory, 3%.
- **State carried in local arrays is slow.** One sweep through a batch's bands, each boundary
  ray and its place on the wall carried from band to band in arrays, read almost nothing and
  cost 6.7 ms against 3.9.
- **Pixels run together wait for the slowest.** The cost follows the most parts in a group more
  than the mean: tall, narrow work groups (a vessel's column has one level) took 9% off.
- **The first measurement of a lookup (0.02 ms a screen) was of a trivial one**, and misled the
  plan: a real lookup's cost is its branches.

**Decisions:**
- `MAP_SLOPE_TOLERANCE` 0.02 to 0.08: a thin line's light within 4%, half of what the eye sees of
  the brightest line. O8 (1% of a line's light, summed over the pixels) still holds.
- `MAP_LANDING_TOLERANCE` 0.01 to 0.02 of a pixel of the screen. The brute-force comparison is
  unchanged: worst 3.3 on the eye metric (visible above 8), mean 0.52.
- Four samples a pixel, not sixteen. With at most 8 or 4 parts a pixel every invariant holds;
  with 2, O5 fails.
- At a route's change each side's wall is read where its own ray lands: giving it its footprint
  costs 0.3 ms, for a quarter of a pixel of rays that graze a surface.

## Real glass (2026-11-20)

The user asked whether thin glass should bend as much as the vessels did, and to fix it if not.
It should not: a vessel's glass was its heat-holding mass spread over its side and floor (2.4 mm
for the 250 mL flask, 2.8 mm for the 50 mL jar, a fifth of its radius at most).

- **A thickness of its own.** Each vessel's `wall_mm` in the bench's data, apart from `wall_g`:
  ISO 3819's least wall for a beaker of its capacity (0.8, 1.1, 1.3 mm at 50, 250, 500 mL;
  `docs/DATA_SOURCES.md`). It is in the item, the view and the save; not in the digest, which is
  the world's (the session records are unchanged).
- **General at its ends.** A wall of no thickness is no glass (a bench that gives none; a save
  from before); one as thick as the vessel is wide is glass all through. No cap.
- **Measured.** Through an empty 250 mL vessel the wall behind is seen at 1.146 times its size
  (the paraxial 1 + 2 D (n − 1) t / (n R r): 1.146); at the flask's old 2.4 mm the same formula gives 1.33. The wall's mass changes
  nothing (`refraction: glass bends by how thick the vessel is made`).
- **On the GPU.** O4 on all 13 pictures (largest difference 2, none visible); a restarting pass
  1.73 ms; `make screenshots` 34 s.
- **The wall's distance** (20 cm, calibrated) is unchanged: for the user's eye in the pictures.

## After this stage

| Stage | Topic |
|---|---|
| 21 | **Layers of refraction** (the user, 2026-11-20): what the player holds is shown in front of what stands on the bench, with refraction, the spectral effects and everything; layers are general (a vessel in a bath), at least two, up to six. GAME_SPEC §12.3 |
| 22 | Measured data (spectra, rates): goldens re-recorded with approval, colours judged by eye |
| later | Spheres and cones (new patch lists, a map with the screen's rows too); caustics; controllers |

## Open

- **The cost** is 1.77 ms a restarting pass against `main`'s 1.07 ms, accepted by the user. The
  bound with one part a pixel is 1.04 ms: what is over it is the rims (six pixels each side of a
  vessel in four parts) and the pixels beside them in two. A way back to `main`'s cost for a
  restart from a colour's change: keep each pixel's wall term (new GPU state; not planned).
- **A prism being turned** traces its map again each frame (3 to 6 ms on the CPU, 16 to 32
  pieces): not measured in the window.
- **The general reading** (a map of several pieces) is as slow as before for the pixels that
  take it: the prism demo only, today.
