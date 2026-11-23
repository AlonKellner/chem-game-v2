# TDD 29: The renderer traces (Stage 29)

## The request

Asked by the user, 2026-11-22, of a stronger model: solve what was left "for a later attempt"
(TDD 25, TDD 27, TDD 21 *Open*) together with three features that need the same thing, a vessel's
contents as shapes in the room, drawn right and fast at any angle:
- a held pour that tilts about its lip, its liquid level in the room, pouring when it reaches the lip;
  sediment that piles and pours; the stream;
- the renderer: "very inefficient at times", vessels that "morph and look incorrect after certain
  movements", and the level layering that tilting makes wrong;
- a stirred liquid's vortex.

Decided with the user (2026-11-22): the pile and the vortex are both cones, each centred on its own
level surface at any angle; the renderer traces live and drops maps, knots, stacks and the store,
**gated by two spikes**; stirring's work enters the ledger (Stage 32); the scripted
`pour {from, to, ml | all}` is unchanged. The stages: 29 the renderer, 30 contents with shape and
bodies turned, 31 the hand pour, 32 the vortex.

GAME_SPEC §12.2, §12.3, §18 (O4, O7, O8), §19, §20.

| Phase | Topic | Status |
|---|---|---|
| 29.1 | Spike A, CPU: a band read as a beam of exact rays, against the tests that guard noise and thin lines | Done: beams hold, points do not (below) |
| 29.2 | Spike B, GPU: the tracer in the pass, its cost | Done: a ray for every band fails by a hundred times (below) |
| 29.3 | The user's decision on what the spikes found | Decided (2026-11-22): rays for a pixel, four corners, the invariants watched and added to |
| 29.4 | The reading in the reference: a pixel's rays at four nodes, beams, cuts; the new invariants O9, O10, O11; O7 over bands | Done: every check holds, the three pixel-against-3D-truth tests among them (below) |
| 29.5 | The same in the Godot host: a pass of its own | Built; it runs, a hundred times too slow where pixels change (below) |
| 29.6 | The user's decision on how the pass is made fast | Decided (2026-11-22): keep engineering it; then, asked by the user for a way that provably converges and is efficient: a pixel as an average over time (below) |
| 29.7 | A pixel as its beam's exact reading and the average of its rays' corrections; no body a special case (a prism's spectrum read in stretches) | Built in the reference and in the Godot host; one test still fails (the prism settling in 40 frames) |
| 29.8 | A diffraction grating in the prism demo, asked for by the user as a test of the reading | Done: five tests, and a picture from the GPU |
| 29.9 | The pass made fast: culling, a way drawn by its share, ray differentials, a ray for several bands, beams kept and moved with a body | |
| 29.10 | Spec (§12.2, §12.3, §19, §20); the cases that read the maps ported; the default switched | |
| 29.11 | Maps, knots, stacks, the store and `chem_tool maps` deleted | |
| 29.12 | Pictures; the user's test and sign-off | |

## What is in the tree

Off unless `LAB_BEAM` is set (`make run` sets it to 5 since Stage 30); the tests' reading is the maps' unless a
test asks (`optics_reading(5)`).
- **`LAB_BEAM=5`: a pixel as an average over time** (`native/game/shader/optics_pixel.glsl`, `OPTICS_TRACE`,
  `OPTICS_NODES`): `trace_shape`, `trace_body`, `ray_met` (the tracer, the truth's method step for step);
  `beam_trace`, `beam_fan`, `beam_band`, `beam_read` (the beam and its exact reading); `ray_shade`, `beam_at`,
  `beam_correction`, `beam_means` (the corrections and their mean). In the reference in float64 (a test may
  ask for it: `optics_reading(5)`); in the Godot host as a third pass (`OPTICS_BEAM_PASS`) over every bending
  body's rectangle and every grating's.
- **The exact reading** (`beam_cut`, `beam_sum`; `LAB_BEAM=4` reads everything by it): a part followed to
  where its rays part. It is what the average comes to, and what the reference reads at one wavelength
  (`optics_through`: O5 to O8, O11).
- **The grating**: `OpticsBody::Grating`, `ray_way`, `way_order`, `way_share`, `GRATING_PASS`; the truth's own
  in `exact_ray`; in the prism demo (`PrismDemo::place_grating`); `native/tests/game/test_grating.cpp`.
- The shapes: each bending body's surfaces as a buffer (`optics_shapes`, `OpticsScene::shapes`, and
  `shapes_exact` in double for the reference; `cg_optics::shapes`; the host's binding 13).
- The invariants: `check_O9`, `check_O10`, `check_O11`, `check_O7` over bands; `optics_ray`,
  `optics_exact_ray`, `optics_made`, `optics_rays_for` (`native/tests/game/test_rays.cpp`); GAME_SPEC §18.
- Spike A's readings from the truth's rays (`LAB_BEAM=1 | 2 | 3`: `ray_band`, `oracle_*`), to be removed.
- For measuring: `LAB_BEAM_DEEPEST`, `LAB_BEAM_CUT_AT`, `LAB_BEAM_STRAIGHT`, `LAB_BEAM_MOST`,
  `LAB_RAY_STATS` (the reference); `LAB_BEAM_TIMES`, `LAB_BEAM_DRY`, `LAB_BEAM_STUB` (the host);
  `CHEM_FRAMES_NARROW` (`chem_tool study frames`).

## Measured

### Spike A: is a beam of live rays as good a reading as the maps'?

In the reference, the rays the exact tracer's (`exact_ray`), so only the reading is measured.

| Check | The maps | A point | Two corners | Four corners |
|---|---|---|---|---|
| O7 (a uniform wall stays as it is) | holds | | holds | holds |
| O8, one vessel (a thin line's light to 1%) | holds | | holds | holds |
| O8, two layers | holds | | holds | holds |
| The prism's spectrum settles (pixels visibly wrong after 40 frames; none allowed) | 0 | 25 to 39 | 0 | 0 |
| Frames settle on a bent, dispersed grid; a moving body keeps its history | hold | | hold | hold |
| Bench frames, a vessel dragged (visible pixel-frames; within 30 frames of a change; the eye metric's mean) | 0.00%; 0.00%; 0.20 | 0.02%; 0.12%; 0.32 | 0.00%; 0.01%; 0.26 | not finished |
| Rays a band, in the settle tests; in the bench frames | | 1 | 14.7; 19.6 | 31.9 |

What a beam needed to hold them, each found by a check failing:
- **A part is halved where its middle ray does not land half way between its ends'** (0.005 of a pixel).
  Taken as straight across a whole pixel, O8 through two layers was 3 to 10% out.
- **A part whose rays go different ways is cut where they part**, found by halving along it, each side
  its own footprint to the last ray that goes its way. Read ray by ray instead, O8 at the rims was 10% out.
- **A band is swept in straight steps of wavelength** (0.02 of a pixel, as the maps sweep). As one
  step, the prism's spectrum kept 6 to 8 pixels wrong.

The bench frames are the study's bench narrowed to the dragged vessel (`CHEM_FRAMES_NARROW=240,280,4,200`):
with rays from the CPU's exact tracer the whole study would take hours. Two corners across take their
height by sampling, a place for each band: a level edge in a pixel is then averaged over bands, not cut
exactly, which is the 0.26 against the maps' 0.20. Four corners cut it exactly, at twice the rays; its
run of the bench frames had not finished when this was written.

The shared source's own tracer, compiled in float64 (`LAB_BEAM=12`), holds O7, both O8s, the lens, the
moving body and the dispersed grid; the prism's spectrum keeps 1 pixel wrong (its steps are equal,
not halved where the way changes).

### Spike B: what does tracing cost in the pass?

M3 Pro, 1920 x 1080, the demo's bench (`tools/screenshot.gd cost`), the CPU busy with spike A's
studies meanwhile.

| | Settled pass | Every history starting again | The same, a vessel held over another |
|---|---|---|---|
| Today (maps, stacks) | 0.17 ms | 7.3 ms | 32.5 ms |
| A ray for every band, the reading as first written | the shader did not finish building in 75 s | | |
| The same, by loops (the tracer called from 4 places, not 132) | built in 4.9 s (0.16 s today) | over 1 s a pass | |
| Benchmark: 2 rays a pixel that starts again | 0.42 ms | 5.5 ms | 6.9 ms |
| Benchmark: 18 rays | 0.32 ms | 35.5 ms | 47.1 ms |

So **one ray for every pixel of every body on the screen costs 1.9 ms (2.5 ms with a vessel held)**,
with the tracer as it is: every body of a layer looked at, every surface of a shape tried at every
meeting. A band read as a beam is about 12 rays, and a pixel starting again reads 72 bands: about
900 rays a pixel, two seconds a pass.

## The reading that was built (29.4)

A pixel's rays are traced at four wavelengths (the maps' nodes: even in 1 / wavelength squared), once;
every band reads the cubic through them. The rays are a part's four corners and its middle: a beam.
- **The footprint goes through the middle ray**: four triangles from the part's edges to where its
  middle ray lands. Read from the corners alone, a part had to be quartered wherever the glass magnifies
  unevenly (61% of pixels); through the middle, 20% are.
- **A part whose rays part is cut where they do**, into two sides, each a beam of its own with four
  corners and a middle. The cut is found by halving, at each node (where its rays part moves with the
  wavelength; an outline's does not, and is found twice, not four times). A side that is bent, or not
  all one way, sends the part to its quarters instead, twice at most; then a side is read ray by ray.
- **Where the middle ray's way changes along the spectrum, the spectrum is cut there**, and each side
  read with nodes of its own: the specks' cause, met where it arises.
- A prism keeps its map: four nodes cannot hold its spectrum (O10 fires on it).

What the invariants found while it was built, each a fault the older checks did not see:
- O7 (at once): the two sides of a cut left the sliver between them unread (0.59985 of 0.6).
- O7 over bands (new): the two sides of a cut read their shares at different wavelengths (1.7e-5); then,
  where one side was read ray by ray, at different places of the band (2.3e-4).
- O9: every ray that goes the truth's way lands with it: of 58,776, all within a thousandth of a pixel,
  18 outside a millionth; 138 graze a surface and go another way.
- O10: over 3,906 pixels, 2 rays of 11,718 outside the sweep's tolerance, none by twenty times.
- The pixel-against-3D-truth tests: a side read along its edges alone was up to 19 out (8 is visible),
  on the row of a held vessel's liquid surface; as an area, and bent sides sent to quarters, 5.7.

| The reading | Rays a pixel: mean, median, most | O7, O8, O11 | Pixels against the 3D truth |
|---|---|---|---|
| Corners alone, cut as sixteenths | 354, 100, 5,900 | hold | hold |
| Through the middle ray, cut as sixteenths | 195, 20, 5,700 | hold | hold |
| A parted pixel cut at once, its sides read along their edges | 51, 20, 420 | hold | up to 19 out |
| A parted pixel cut at once into areas; bent sides to quarters | 185, 20, 5,900 | hold | hold (worst 5.7) |
| The same, at most 400 rays a pixel, the rest by its middle ray | 92, 20, 770 | hold | up to 28 out |

So the reading that is right costs 20 rays for four pixels in five, and hundreds to thousands for a
pixel on a rim, a floor's corner or two vessels' crossing outlines: thin bands of rays that go different
ways, each followed to its edges.

## The pass on the GPU (29.5)

M3 Pro, 1920 x 1080, the demo's bench.

| | The maps | A pixel's own rays |
|---|---|---|
| The second pass's shader built, cold (then cached by the system) | 5.7 s | 7.1 s |
| The whole screen settling at the start | no frame over 35 ms | five frames of 0.6 to 1.0 s |
| A frame in which every history starts again | 7 ms | 1.3 s (1.1 s of it following rays) |
| The beaker carried toward the flask | 8.3 ms a frame (the display's rate) | over 1 s a frame |
| Settled | the display's rate | the display's rate |

- **Apple's compiler** (the last stage of a shader's build) did not finish with the reading written as
  a loop with jumps back (`continue`, a loop inside for the carry): over 45 s with all of its rays
  stubbed out. Written straight down, 0.4 s stubbed and 7 s whole. `LAB_BEAM_TIMES` prints each stage.
- **The cost is the tail, not the mean.** The pass runs pixels in groups of 64 that wait for their
  slowest, and a vessel's surface row, floor row and rims put a pixel of thousands of rays in most of
  its groups.

## A pixel as an average over time (29.7)

Asked by the user (2026-11-22): a way that provably comes to the true answer and is efficient,
"relying on the over time averages". The exact reading above is right and its cost is set by the
hardest pixel, which a GPU's groups wait for. The answer:

A pixel's light is one integral, over its area and the spectrum, of what a ray's way lets through
times the wall where it lands. So
**the pixel = its beam's exact reading + the average of (a traced ray's light - the beam's own light at that ray's place)**.
- The mean is the integral **whatever the beam is like** (a control variate): the error after N rays is
  at most the largest difference over the square root of N, and the difference is bounded (O2).
- Where the glass is smooth the difference is nothing: four pixels in five are right at once, thin
  lines on the wall included (the beam's reading integrates the wall exactly).
- Every pixel costs the same: a beam when what it looks through changes, a few rays a band a frame. No
  cuts, no quarters, nothing for a group to wait on.
- It is the pass's own shape: what a pixel shows was already a fixed estimate plus a running mean of
  (a frame's estimate less it). The fixed estimate is now the beam's reading; the mean, the corrections'.
- **No body is a special case.** A prism's spectrum is read in stretches: where the ray traced between a
  stretch's nodes is not where their cubic puts it, the stretch is halved (O10 is restated over the
  stretches a pixel is read in; its own test is a reading that never halves, on the prism).
- A correction is read **along the line between its band's two ends**, as the beam's is: a ray that just
  misses a bright line where the beam's just hits it is a small difference, not the line's whole light.
- What it gives up: a pixel on an edge is not right at once. Its grain fades over frames.

In the reference (the same tests as the table above):

| | The exact reading | The average over time |
|---|---|---|
| O7 (single wavelengths and bands), O8 (one vessel, two layers), O9, O10, O11 | hold | hold (O7 over bands exactly: in an even room every correction is nothing) |
| Held over standing, against the 3D truth (8 is visible) | worst 5.7 | worst 5.6, mean 1.36 |
| A flask's curved rows; a wall of any drawing; a beaker's upright band | hold | hold |
| Frames settle on a dispersed grid; a moving body keeps its history | hold | hold |
| The prism's spectrum settles in 40 frames (none wrong allowed) | holds (by its map) | **8 or 9 pixels still wrong** |
| O5 (straight rays at a rim, 0.002) | 0.013 | not run |
| The reference's time for the settle and truth tests | minutes | a minute |

On the GPU (M3 Pro, 1920 x 1080, the demo's bench):

| | The maps | The exact reading | The average over time |
|---|---|---|---|
| The beaker carried toward the flask, a frame | 8.3 ms | over 1 s | 42 ms (29 with one ray a correction, not two) |
| A frame in which every history starts again | 7 ms | 1.3 s | 97 ms (70) |
| The pass's shader built, cold | 5.7 s | 7.1 s | 7.5 s |

Found on the way:
- Halving the spectrum for a pixel whose rays part cost a rim's pixel hundreds of rays for nothing (the
  beam there is its middle ray alone): 239 ms a frame carried, against 44 without.
- A pixel whose content has just changed traced its beam twice, for the fixed estimate and for the
  frame's batch: both from one tracing, 44 to 29 ms.
- The reading at one wavelength counted a ray four times where its four nodes are one (found by O7).

## The grating (29.8)

Asked by the user as a test of the reading: nothing in it is refraction. A thin plate ruled with lines
as wide as the gaps between them. A ray goes on from it in several ways at once, its orders: order m
turned across the ruling so that its sine changes by m times the wavelength over the pitch; a quarter
of the light in the order 0, `1 / (pi m)^2` in each odd order, none in the even ones (the ruling's
Fourier series; derived, not fetched), half on the lines themselves, which show the room in front. A
ray is followed in one way at a time, and a pixel is the sum of its ways by their shares; the truth's
tracer follows every way of every ray. 600 lines a millimetre (a design value, in the demo).

`native/tests/game/test_grating.cpp`, all passing:
- each order lands where the grating equation sends it; an order past a right angle does not leave;
- each order of the slit carries its share of the slit's light to 1%, at 450 and 600 nm;
- held before the slit: the slit white, a spectrum to each side, violet nearest and red furthest, the
  same on both sides; against the truth's rays, worst 0.5 on the eye metric;
- O7 holds through it;
- frame by frame the picture comes to the truth: none visibly wrong (worst 7.8, mean 0.9).

On the GPU: `build/screenshots/prism_4_grating.png` (`make prism-shots` with `LAB_BEAM=5`).

## What the spikes decide

1. **Beams, not points.** Decided by spike A; the plan already said so.
2. **Not a ray for every band.** It fails on the GPU by a hundred times, and it cannot be bought back:
   the cost is the count.
3. **Rays for a pixel, read by all its bands.** What a band needs from a ray (where it lands, what
   passes, its chords) is smooth in wavelength along one route: the maps hold it at four nodes and a
   cubic through them. So a pixel traces its corner rays at four wavelengths once a step (12 to 20
   rays), and every band of the step reads those. Where a corner's nodes go different ways, the
   spectrum is cut where they part (halving in wavelength, as a part is cut across): the specks'
   cause, met where it arises.
4. **The shader's size is a cost of its own.** The tracer is called from as few places as can be.

The estimate made then (20 rays a pixel; a vessel held or tilting 1 to 5 ms a frame) was wrong by the
pixels whose rays part: built and measured (above), the reading that is right follows 185 rays a pixel
on average and thousands at a rim, and the pass waits for those.

## Open

- **The prism settling** under the average: 8 or 9 pixels still visibly wrong after 40 frames (the maps: none).
  A slit twelve times white, three pixels wide, spread over hundreds: the corrections there are few and large.
- **The pass's cost**: 42 ms a frame carried and 97 ms for a whole screen, against a budget of 8. In order:
  bodies found by the screen's squares and surfaces by height; a grating's way drawn by its share (one
  correction, not five); rays that have lost their light ended by chance, the rest weighed up; a ray's
  derivatives carried in place of the beam's twenty rays; a traced ray serving several bands (the two ends of
  neighbouring bands are one ray); beams kept in images and moved with a body that is carried.
  Decided by the user (2026-11-22), having seen it in the game: "visually it's much better than the existing
  implementation ... keep it as non efficient as it is, and we'll plan a more extensive optimization plan
  later". Stages 30 to 32 are built on it; a bottle tilting and pouring by hand costs 69 ms a frame, a stirred
  one's vortex 37 (TDD 31, TDD 32).
- **O5** in the exact reading: a liquid's depth is read at a part's middle ray alone (0.013 at a rim; 0.002 asked).
- The band estimator's first and last bands are tens of nanometres wide: a spectrum's black ends may be
  another dark colour (seen at 380 nm in a grating's first order before corrections were read along lines).
- The exact reading with the spectrum halved (`beam_sum` over bands) loses parts of chaotic pixels; it is
  used at single wavelengths, where nothing is read between nodes. To be removed or mended.
- Spike A's readings (`LAB_BEAM=1 | 2 | 3`) and the measuring switches, to be removed.
- The cold build of a shader (5.7 s for today's second pass, 7.5 s for the beams') is paid once per
  change of the source: the system keeps what it built.
