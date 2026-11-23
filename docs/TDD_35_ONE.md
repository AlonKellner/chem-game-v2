# TDD 35: One renderer (Stage 35)

## The request

Decided by the user when the pass was made fast (2026-11-22, TDD 33): end with the renderer that traces
a pixel's own rays as the only one; the maps, the stacks, the store and their tests removed. Asked for
again, 2026-11-23: "complete stage 35, and then work on the single precision CPU harness".

GAME_SPEC §12.2, §12.3, §18, §19, §20.

| Phase | Topic | Status |
|---|---|---|
| 35.1 | The traced reading is the default (the reference, the host); the tests hold under it; the tests of the maps' own machinery are retired | Done |
| 35.2 | The maps, knots, stacks and the store deleted from the lab layer, the C API, the hosts and the tools | Done |
| 35.3 | The old readings deleted from the per-pixel source; the pixel's path pruned of map data | Done |
| 35.4 | The measuring switches and hooks that no gate needs removed; GAME_SPEC and CLAUDE.md rewritten for one renderer | Done (GAME_SPEC 0.36.0) |
| 35.5 | The whole per-pixel source in single precision on the CPU, beside the reference (the GPU's pixels off the truth, TDD 33 *Open*) | Done: O16; the pixels are not single precision's (below) |
| 35.6 | `make gates`; the user's sign-off | `make gates` passed (uncached, ASan+UBSan) on the stage's last code; awaiting the user |

## What the inventory found (2026-11-23, before 35.1)

Four things break if the old code is simply deleted:

- **`beam_sum` stays.** The traced reading uses it for every read at one wavelength (`optics_through`, so
  O2, O7, O8 and most tests) and for `optics_made` (O11). Only its tuning switches go.
- **The reference's traced branches are guarded by `OPTICS_FOLLOW`** (`band_light`, `batch`, `truth`,
  `step_pixel`: `#if defined(OPTICS_NODES) && defined(OPTICS_FOLLOW)`). Dropping the macro with the
  stacks' follower silently turns the traced code off in the reference: the guards are changed first.
- **`shader::ready()` requires the stacks' directory** (`s.stacks.size() >= MAX_BODIES * 4`). Without
  `optics_stacks` no scene is ever ready: the condition goes with it.
- **The traced passes use `chain` as their list of rectangles** (the kept beams' pool is laid out by it).
  It stays, under a name of its own.

### The per-pixel source (`native/game/shader/optics_pixel.glsl`)

Old only, to delete: the maps (`knot_for_row`, `knot_map`, `knot_rims`, `knot_span`, `map_for_row`,
`rim_measure`, `rim_place`, `MAP_FINE`, `Bend`, `MapRef`, `to_knot`, `to_band`, `from_band`, `ref_of`,
`Edge`, `edge_one`, `edge_load`, `thirds`, `nearest_weights`, `nearest`, `edge_at`, `map_at`, `map_level`,
`map_bend`, `core_share`, `map_change`); the general reading under `OPTICS_CHAIN` (`map_on`, `edge_on`,
`toward`, `end_wall`, `split_part`, `edge_in`, `core_light`, `general_band`); the stacks (`whole_of`,
`land_bend`, `stack_value`, `stack_light`, `ends_wall`, `stack_rows`, `stacked`, `stack_band`,
`STACK_SLOTS`); under `OPTICS_FOLLOW` the follower (`MAX_VISITS`, `MAX_STRIPS`, `Stack`, `ref_at`,
`stack_of`, `core_of`, `Ray`, `chain_at`); the plain reading (`Plain`, `Cell`, `cell_on`, `plain_of`,
`wall_along`, `side_light`, `plain_band`); both `other_band`s (what survives of them: `flat_band`);
`chained` (under the traced reading it is `beamed`: the host's wrapper asks it); the `Path` fields
`rlayer`, `rmap`, `rcoarse`, `rsamples`, `rpieces`, `rmedia`, `rleft`, `rrims`, `rdown`, `rmap2`,
`rcoarse2`, `rsamples2`, `rleft2`, `rf`, `rrims2`, `rwet`, `rin`, `rfloor`, `chain`.

To prune, not delete: `path_of` (the refractor's block reads the MAP slot, `map_for_row`, `map_vec`, the
knots, and `rwet`, `rin`, `rfloor`, `raxis`, `rdown` and the signature of what stands behind feed the
content key: with them gone a traced pixel's key is its shape and content tags and what its layers
say); `band_light`, `batch`, `truth` (their paths for a pixel no vessel bends become `flat_band`);
`step_pixel` (`dear = q.chain`).

Kept: the layout and constants, `path_of`'s layers (`layer_at`, `thing_at`, `beyond_outline`), the
spectra, the wall's table, `map_place`, `thirds_weights`, `map_steps`, the shading (`along`, `shade_*`,
`flat_band`, `swept_wall`, `body_does`), the whole traced section, `beamed`, the bands, the colour,
`step_pixel`, `shown`.

### The lab layer

- `optics_reference.cpp`: `map_vec`, `stack_vec`; spike A (`LAB_BEAM=1 | 2 | 3`: `ray_band`,
  `OracleRay`, `BeamKey`, `beam_rays`, `oracle_*`, `optics_beam_counts`); `beam_setting`,
  `beam_straight_setting`; `optics_through_general`; the old branch of `optics_bend`; `fnv_of`,
  `optics_stacks`; `optics_reading`, `optics_traced` (the reading is one: `rays_on`, `rays_nodes`,
  `rays_mean` are true).
- `optics_scene.cpp`: the store (`MAP_STORE_VERSION`, `store_*`, `optics_store*`), `shaped_block`,
  `place_maps`, `optics_maps`, the map-piece logic of the chain's rectangles (kept: a rectangle for
  every body that bends), the signature of what stands behind (only `path_of` read it).
- `optics_scene.hpp`: the `MAP` slot, `maps`, `maps_revision`, `maps_changed_from`, `map_keys`,
  `MapRegion`, `map_regions`, `stacks`, `stacks_revision`, `stack_keys`, `stack_blocks`, `stack_rays`,
  the store's functions, `optics_maps`, `optics_stacks`, `optics_through_general`, `optics_reading`,
  `optics_traced`; `chain`, `chain_bodies` renamed.
- `optics_trace.hpp`, `.cpp`: everything of maps and knots (`MapSpec`, `Map`, `build_map`, `map_ray`,
  `map_read`, `rows_ray`, `KnotMap`, `build_knots`, `knots_read`, `knots_block`, `map_block`, their
  constants). Kept: `Vec3`, `Patch`, `Shape`, `Inside`, `Landing`, `trace`, `MEETINGS_MOST`,
  `TURNS_MOST`, `cylinder`, `prism`, `ball`, `revolved`, `revolved_of`.
- `ui.cpp`, `ui.hpp`: `Ui::prepare_optics`.

### The C API, the hosts, the tools

- `chem_api.h`, `.cpp`: `cg_optics_store`, `cg_ui_prepare`; of `cg_optics`: `map_count`, `maps`,
  `maps_revision`, `maps_changed_from`, `stack_count`, `stacks`, `stacks_revision`; `chain`,
  `chain_count` renamed (the traced passes' rectangles).
- `chem_lab.cpp`: `cg_optics_store("res://data/maps")`.
- `optics_compute.cpp`, `.hpp`: bindings 10 (`MapBuffer`) and 12 (`StackBuffer`), `maps_`, `stacks_`
  and their uploads; the chain's pass (pass 1) and its shader; `passes_` (the traced passes are the
  passes); `chain_` renamed.
- `chem_tool maps` (`chem_tool.cpp`), the Makefile's step in `godot-data` that runs it,
  `engines/godot/data/maps/`; `LAB_BEAM` in the Makefile (`run`, `pass-cost`) and in CLAUDE.md.

### Tests

- Retired with the machinery (35.1): `test_maps_store.cpp`, `test_rows.cpp`; of `test_trace.cpp` the
  two cases named "map: …"; of `test_footprint.cpp` "a pixel through a map of one piece is read by a
  reading of its own…"; the assertions on `scene.maps`, `stacks`, `chain_bodies` and the MAP slot
  inside other tests; of `tests/capi/smoke.c` those on the maps' and stacks' counts.
- To hold under the traced reading (35.1): the cases of `test_refraction.cpp`, `test_footprint.cpp`,
  `test_layers.cpp`, `test_truth3d.cpp`, `test_things.cpp`, `test_wall_pixels.cpp`, `test_optics.cpp`
  (the demo's bench), `test_pour.cpp`, `test_ui.cpp` and `test_rays.cpp` (O7) that render through the
  default reading. When every test ran with `LAB_BEAM=5`: 7 of 84 entries failed, about eleven cases
  beside the two of the store.
- The wrappers that choose the reading (`struct Traced`, `Reading`, in `test_cost`, `test_kept`,
  `test_single`, `test_lying`, `test_grating`, `test_ui`, `test_vortex`) go in 35.3 with
  `optics_reading`.

### Switches and hooks

- Needed by gates, kept: `LAB_THREADS`, `LAB_SPARSE`; `OPTICS_RAY_HOOK` (O13, `OpticsFrames::rays`),
  `OPTICS_STRETCH_HOOK` (O10).
- Measurement only, to go (35.4): `LAB_BEAM` and its modes, `LAB_BEAM_COUNT`, `_DEEPEST`, `_CUT_AT`,
  `_MOST`, `_STRAIGHT`, `_STRIDE`, `_AT_ONCE`, `_GROUP`, `_DRY`, `_STUB`, `_DEFINE`, `_TIMES`,
  `LAB_RAY_POINTS`, `LAB_UPLOADS`, `LAB_O9_COUNTS`, `LAB_EVEN_PRINT`, `LAB_PLAY_SERIES`; the hooks
  `OPTICS_WORK_HOOK`, `_ENDED_HOOK`, `_EVEN_HOOK`, `_STEPS_HOOK`, `_SWEPT_HOOK`, `_STAT_HOOK`,
  `OPTICS_STUB_*`, `optics_meetings_most`; and the cases of `test_cost.cpp` and `test_rays.cpp` that
  only print where `LAB_RAY_STATS` is set. (The user decides which measurements are worth keeping as
  tools: `LAB_O4_PRINT`, `LAB_PLAY_SERIES` and `LAB_RAY_STATS=series` found most of what TDD 33
  records.)

### GAME_SPEC

To rewrite for one renderer: §12.3 "The scene" (maps, stacks, the second pass's rectangles), "A pixel
is the light of all its rays" (the tracer "never in a pixel's loop"; "A refraction map for each body";
"The footprint", "One light, two readings"), "Layers of refraction" (a round body's map, the chain,
stacks, "Three readings … two passes"), "A vessel turned", "A map for each band of its rows" to
"Layers" (knots; "Made before the game runs", the store, `chem_tool maps`), the Status paragraphs of
Stages 20, 21 and 24; §11's "a map with rows"; §18's O8 row ("a stack's rims"); §19's timings of the
maps and stacks; §20's rows on knots, stacks, map samples and the stack's reading. The traced reading
itself is described today only in `docs/TDD_29_TRACER.md` and `docs/TDD_33_FAST.md`: its design moves
into §12.3.

## What 35.1 found, and what was mended

With a pixel's own rays as the default reading, three tests failed for faults of the reading itself (not of
the tests). Mended in the per-pixel source:

- **O8 through two layers** (three thin lines 1.3% to 6.4% of their light out). A correction took the beam
  to say one thing of a place (`beam_at`: each triangle of the fan evenly by area) and the beam's own
  reading weighed another (`beam_wall`: each row of a triangle alike), so their difference stayed in the
  mean; and a beam landing straight was read as its corners' quad. Now `beam_wall` weighs a triangle's
  rows by their width (`taper`) and the quad is the fixed estimate's alone (`fan_quad`). The case passes.
- **A band's rays along a line.** A correction's place in the pixel came from the same point as its
  wavelength. Now the place is its own (the multipliers 807 and 941 in `beam_read`). Bands more than 3%
  off where a prism's rays part: 29 of 704, then 16.
- **A history kept through a turn: tried, and taken out.** A pixel whose fixed estimate is the same
  before and after its rays move keeps what its batches found. Giving that history the weight of one
  point where the beam was traced again (`step_pixel`) was tried twice. For every such pixel: the
  prism's row had 0 to 3 wrong at rest and 2 to 5 after a turn, and a stir cost 2.4 ms a frame more
  (every pixel of the liquid asked again at every frame: 10.7 ms for 8.4). Only where the history is
  more than a hundredth of what is shown: the prism's counts were what they are without it (3 at rest,
  5 after the turn) and the stir still cost 1.5 ms. Without it the counts are 3 and 5: it was the two
  mends above and the one below that brought the turn's 37 to 5, so it is not in the source.
- A beam's nodes that go different ways: `beam_weights` reads between two neighbouring nodes that go one
  way, and the nearest node where they part (`beam_piece_one_way`), in place of a cubic through nodes of
  other routes.

## What 35.2 to 35.4 did (2026-11-23)

In the inventory's order: the guards first (the reference reads a pixel by its own rays whatever `LAB_BEAM`
says; a scene is ready without a stacks' directory; the traced branches are left out by a macro of the host's
pass over the screen, `OPTICS_SCREEN_PASS`, where they were let in by the follower's); then the per-pixel
source; the macros every compilation defines folded away (`OPTICS_TRACE`, `OPTICS_RAYS`, `OPTICS_NODES`,
`OPTICS_KEPT`, `OPTICS_LATTICE`: the reference's binary came out the same, byte for byte); the lab layer, the C
API, the hosts and the tools; the switches and hooks. The per-pixel source: 5,609 lines, then 3,930.

The picture did not change. Held before and after each step: every assertion's values of twenty of the lab's
test files (`lab_tests -s`), the play's series frame by frame (`LAB_RAY_STATS=series`), the prism's probe, all the
same to the last digit; `chem_tool study frames` the same; the GPU's O4 counts the same (0, 5, 9 and 4 pixels
visibly off the truth in the four pictures); the pass's cost, back to back three times each: 8.31 to 8.37 ms
before and 8.32 to 8.40 after for the five marks (the display's rate), a restarting frame 17.8 to 18.1 ms before
and 17.7 to 17.8 after.

Where it departs from the inventory:

- **The signature of what stands behind a vessel is kept** (the scene's `SET` z, and in a pixel's key). The
  inventory had it go with the stacks, as read by `path_of` alone; but it is the one thing in a pixel's key
  that says a body its bent rays reach, and which does not stand behind the pixel itself, has changed colour
  (the shape and content tags say where such a body stands and what lies in it, not what it is made of).
  Without it such a pixel of a held vessel would have nothing to do while the flask behind it reacts.
  What went from the key: the pixel's place across its vessel, the vessel's axis, and how much of the pixel is
  under the liquid and within the vessel (`raxis`, `rwet`, `rin`; `rfloor` and `rdown` were nothing once no map
  was made). The play's series did not move by a pixel.
- **A body's vec4 6 (the map's) is unused, not gone**: the bodies are laid out as they were, and every reader
  (the source, the reference, the tests) names the others by number.
- **`OPTICS_WORK_HOOK` and `optics_work` are kept**: the first case of `test_cost.cpp` prints a ray's work at
  each mark wherever `LAB_RAY_STATS` is set, `series` among them.
- `body_down` went with the rows it served; `OpticsPassage::most` with the counts that read it.
- `engines/godot/data/maps/` is left where it is (generated, not tracked); nothing makes or reads it, and
  `godot-data` still keeps it out of what it deletes (`--exclude maps/`), until it is removed by hand.
- `chem_tool study frames` keeps its own switches (`CHEM_FRAMES_NARROW`, `_ONLY`, `_WHERE`).

Found on the way, and left as it is:

- `step_pixel`: "the history weighs one point" (above) is written `points = min(points, 1.0e9)`, which does
  nothing (a history holds 4,096 points at most). The picture is as the gates and the play have it; whether
  the line was meant as `1.0` is to be settled with the prism's slit.
- `through_layer` (the per-pixel source) is read by nothing, and was not before.
- `make screenshots`: its play gives up "waiting for the solution to boil down", headless and in a window,
  before and after this work; the four pictures before that are taken and compared.
- Comments and names of tests still speak of maps and the chain where they say what a test was written for
  (`test_footprint`, `test_layers`, `test_truth3d`, `test_refraction`).

## The whole source in single precision (35.5)

**What was built.**
- `native/game/src/optics_pixel_host.inc`: what the reference wrapped the per-pixel source in (the shading
  language's vectors, the scene's buffers, the hooks, the source, and the frames of a sample of pixels),
  written on a number type `real` and included by each arithmetic in a namespace of its own:
  `optics_reference.cpp` with `double` (`lab::shader`, as before: every test's numbers are what they
  were), `optics_single.cpp` with `f32` (`lab::shader32`).
- `f32`: a float that stays one. A literal is rounded to it before anything is done with it and nothing
  is done in double on the way (the build has no fast math and joins no product to a sum). With it the
  GPU's tolerances (`TRACE_NEAR` and the rest of the host's `TRACE_HEAD`, `BEAM_BETWEEN 0.5`), the shapes
  as the floats the GPU is sent (`OpticsScene::shapes`, not `shapes_exact`), a pixel's state and its kept
  beams as floats.
- `OpticsFrames(x, y, w, h, budget, step, single)`: the same class in either arithmetic
  (`src/optics_frames.hpp`: `OpticsFramesCore`, one made by each file). `OPTICS_SHAPES_ALONE` is gone
  from the source: O15's tracer of one shape is the whole compile's.
- `check_O16` (GAME_SPEC §18) and its test (`test_single.cpp`): over a sample of the play's bench,
  settled and with the bottle held turned over the flask, both arithmetics stepped until neither asks
  for more; 5 s.
- `LAB_O4_PRINT=1 make screenshots` prints, at each pixel where the GPU's picture is visibly off, what
  the pixel's own frames come to on the CPU in double and in single precision, beside the picture, the
  one reading and the truth of the pixel's area.
- It is not the GPU bit for bit: a GPU's compiler may join a product and a sum into one rounding, and
  has its own sine and logarithm.

**What it found: the GPU's pixels off the truth are not single precision's.**

| The play's bench, every pixel (`LAB_SINGLE_STEP=1`), until no pixel asks | Pixels of bodies | Visibly apart, single against double | Visibly off the one reading: in double | in single precision |
|---|---|---|---|---|
| Settled | 44,207 | 6 (the worst 10.5; the mean 0.13) | 4 | 4 |
| The bottle held, turned, over the flask | 45,024 | 6 (the worst 19; the mean 0.18) | 6 | 8 |
| Settled, the stopping target halved (`TARGET` 1) | 44,207 | 6 | 4 | 3 |
| The bottle held, the target halved (600 frames, the test's limit) | 45,024 | 1 | 0 | 1 |

- **The source's own frames stop with about one pixel in ten thousand visibly off the truth, in double as
  in single precision.** The GPU's settled pictures show 0, 5, 9 and 4 of 30,000 to 64,000
  (`make screenshots`): the same handful. At the GPU's own off pixels the CPU's frames are off too, each
  arithmetic its own way: at (669, 617) of the pour's picture the one reading is 0.342 0.341 0.337, the
  GPU shows 0.262 0.371 0.287, the frames in single precision 0.293 0.365 0.284, in double 0.374 0.308
  0.350.
- Single precision and double part visibly at 6 pixels in 45,000 because those pixels' rays are chaotic
  (a vessel's floor near its axis, a held vessel's rim over another's): each arithmetic is another draw
  of the same mean. Everywhere else they are the same picture (the mean 0.13 to 0.18 apart, where 8 is
  visible).
- **Two causes, both in how a pixel decides it has enough** (`step_pixel`):
  1. *The stopping rule's tail.* A pixel stops when its own estimate of its error is `TARGET` (2 on the
     eye metric, where 8 is visible): four of its standard errors off is one draw in sixteen thousand,
     a few pixels of a screen. Halving the target brought the held bottle's 6 to 0 (and 8 to 1): those
     wanted only more points.
  2. *A noise estimate that forgets.* On the settled bench the same 4 pixels stay off whatever the
     target, after the same 199 frames: they ask for nothing more. They lie in one row of the flask
     ((553, 532) to (584, 533), its floor's upper face). A pixel's noise is a running mean over its
     last 16 batches (`noise + (trial - noise) / min(frames + 1, 16)`): where a correction is rarely
     large (one ray in many finds another way through the floor), sixteen quiet batches leave no trace
     of it, the pixel takes its least points (`SETTLED_POINTS`, 128) and stops on whatever the rare
     rays it happened to draw made of its mean.
- Earlier notes are answered by this: "not grain: the same after every history starts again"
  (TDD 33) held because a pixel's draws are the same draws every time (its place and the frame's
  number seed them), not because its mean had come to the truth.

## At the stage's end (2026-11-23, M3 Pro, 1920 x 1080)

- `make pass-cost`, a frame's median: settled 8.34 ms, carry 8.37, pour 8.37, rest 8.33, stir 8.39 (the
  display's rate); every history starting again 19.2.
- `make screenshots`, all fourteen pictures (the tool had stopped at the boiling since Stage 34: its
  script still led the condenser's outlet by pressing the beaker onto the condenser; it drags the
  outlet's line now). Pixels visibly off the truth of those in bodies:

  | Picture | Pixels in bodies | Visibly off | The largest |
  |---|---|---|---|
  | 1_set_up | 30,027 | 0 | 7 |
  | 2_decomposing | 46,123 | 5 | 10 |
  | 3_pour_panel | 40,561 | 9 | 11 |
  | 6_monitor_editor | 65,882 | 4 | 9 |
  | 4_boiling | 69,120 | 10 | 10 |
  | 5_crystals, 10_notices, 13_debug | 68,642 | 11 | 15 |
  | 14_held (a vessel held over another) | 63,634 | 20 | 13 |
  | 7_chosen, 8_circuit, 9_circuit_open, 11_writer, 12_goal_editor | 26,739 to 29,951 | 0 | 7 to 8 |

  One to three pixels in ten thousand where vessels hold liquid: the open item below, in double as on
  the GPU.

## Open

- **A pixel in ten thousand stops visibly off the truth** (O4 on the GPU: 0 to 9 pixels a picture; the
  table above). Not a fault of precision or of the GPU. What would mend it, none of it tried:
  - the noise as the mean over all of a pixel's batches (or the largest of the last few), so a rare large
    correction is not forgotten: cause 2;
  - a second, slower stage: a pixel settled to `TARGET` goes on asking for a fraction of a batch until its
    error is a half or a quarter of it, under the same budget (4 or 16 times the points, at pixels whose
    noise is not nothing: one in five). It must still come to ask for nothing (`done`), or a settled
    pass is no longer free: cause 1;
  - not `TARGET` alone: every pixel on a rim would take four times as long to stop, in competition with
    pixels that have just moved.
  To measure: `LAB_SINGLE_STEP=1 LAB_SINGLE_PRINT=1 ./lab_tests -sf="*test_single.cpp" -tc="O16*"`
  (70 s; the last column is what must come to nothing), then `make screenshots` and `make pass-cost`.
  The screenshot tool waits a fixed number of frames for a picture to settle: a slower second stage
  needs that wait lengthened, or the tool will count pixels that are only not finished (seen with
  `TARGET` 1: 19 in one picture).
- **The prism's slit, where a node's ray is turned back** (`test_refraction.cpp` "a bent view sweeping…",
  3 pixels of the row wrong at rest and 5 after a turn; `test_footprint.cpp` "where a prism's rays part…",
  16 of 704 bands; both marked `may_fail`, not to be loosened). At such a pixel the ray at 380 to 400 nm is
  totally reflected inside the prism and does not land; from about 410 nm it lands, racing in from far
  along the wall (at (946, 540): x 1493 at 420 nm, 1161 at 440). The slit's image lies in the piece of
  the spectrum between the node that is gone and the first that lands, where the beam says nothing and
  no ray of a correction happens on the narrow window. The probe: `LAB_RAY_STATS=prism ./lab_tests
  -tc="*prism*"` (`test_cost.cpp`). The remedy, not built: cut the spectrum where the middle ray's route
  changes (halving between the two nodes) and give each side a beam of its own, so the landing's sweep on
  the lit side is integrated to its end. It needs more than one beam in a pixel's record
  (`KEPT_MOST > 1`, `CG_OPTICS_KEPT_VECS`) and O13's bound of 96 rays a pixel a frame looked at again.
