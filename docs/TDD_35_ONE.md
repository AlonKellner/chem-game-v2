# TDD 35: One renderer (Stage 35)

## The request

Decided by the user when the pass was made fast (2026-11-22, TDD 33): end with the renderer that traces
a pixel's own rays as the only one; the maps, the stacks, the store and their tests removed. Asked for
again, 2026-11-23: "complete stage 35, and then work on the single precision CPU harness".

GAME_SPEC §12.2, §12.3, §18, §19, §20.

| Phase | Topic | Status |
|---|---|---|
| 35.1 | The traced reading is the default (the reference, the host); the tests hold under it; the tests of the maps' own machinery are retired | Done |
| 35.2 | The maps, knots, stacks and the store deleted from the lab layer, the C API, the hosts and the tools | |
| 35.3 | The old readings deleted from the per-pixel source; the pixel's path pruned of map data | |
| 35.4 | The measuring switches and hooks that no gate needs removed; GAME_SPEC and CLAUDE.md rewritten for one renderer | |
| 35.5 | The whole per-pixel source in single precision on the CPU, beside the reference (the GPU's pixels off the truth, TDD 33 *Open*) | |
| 35.6 | `make gates`; the user's sign-off | |

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
- **A history kept through a turn.** A pixel whose fixed estimate was the same before and after its rays
  moved kept what its batches had found. Now, where its beam was traced again and its history holds light
  of its own, the history weighs one point (`step_pixel`). Wrong pixels 40 frames after the prism's turn:
  37, then 5. (Dropped for every such pixel it cost a stir 2.4 ms a frame: every pixel of the liquid asked
  again at every frame. It is dropped only where the history is more than a hundredth of what is shown.)
- A beam's nodes that go different ways: `beam_weights` reads between two neighbouring nodes that go one
  way, and the nearest node where they part (`beam_piece_one_way`), in place of a cubic through nodes of
  other routes.

## Open

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
