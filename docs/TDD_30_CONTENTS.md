# TDD 30: What lies in a vessel, and bodies turned (Stage 30)

## The request

The second stage of the plan of TDD 29 (the user, 2026-11-22): a vessel's contents as shapes in the
room, the same for the lab's pour and for the picture, at any angle. Decided with the user: a pile
and a vortex are both **cones, each centred on its own level surface**; and, after the renderer of
Stage 29 was seen (2026-11-22): "visually it's much better than the existing implementation ... keep
it as non efficient as it is ... proceed to the rest of the plan".

GAME_SPEC §11 (what lies in a vessel), §12.3 (a vessel turned, what lies in it, beyond the screen),
§18 (H1, H2).

| Phase | Topic | Status |
|---|---|---|
| 30.1 | `lab/contents.hpp`: the volume under a level at any turn, a level's cut, cones, the pile's closure, the liquid's level, the lip, the full tilt; H1, H2 | Done |
| 30.2 | The truth's tracer: a core whose medium is by place (liquid, air, sediment), the surfaces between them, a body turned about a pivot | Done |
| 30.3 | The pass's tracer, the same step for step; the scene carries how each body lies | Done: O9 holds turned, with liquid, sediment and a vortex |
| 30.4 | A ray that lands beyond the screen | Done: found by the turned vessels' pictures (below) |
| 30.5 | The screen draws a pour by hand turned about its lip; `make run` plays with the reading that traces | Done |
| 30.6 | The user's test and sign-off | |

## What was built

- **`lab::contents`** (pure, engine-free; cm, cm³, radians). A turn is clockwise as seen; the room's
  up in the vessel's frame is `(−sin θ, cos θ)`, and a level is a height along it.
  - `below(vessel, turn, level)`: across each height the inside is a disc, so its part under the level
    is a circular segment in closed form; along the axis, Gauss-Legendre (4 points in 8 stretches a
    piece of the profile, split at its kinks). `level_of`: by halving. `cut`: the level's line in the
    section (its ends, middle, half length) and the surface's area.
  - A `Cone` on a cut: base an ellipse centred on the cut's middle, as wide as the cut, as deep as
    gives it the cut's area; **its volume is the cut's area times its height over three**, whatever
    the vessel and the turn.
  - `lying(vessel, turn, liquid, solid, vortex, side)`: the sediment's flat section and pile (as
    peaked as repose allows, `tan 35°`; its peak where the two hold the bulk, the powder over its
    packing 0.5: one unknown, by halving), the liquid's level over them (less the vortex), the
    vortex's cone, the lip and how far the liquid and the sediment stand over it.
  - `full_tilt`, `held_at`, `lip_of`.
- **H1, H2** (§18): what lies in a vessel adds up; a pile within repose, a vortex within the liquid.
  Each fires on a corrupted `Lying` (`native/tests/game/test_contents.cpp`).
- **The tracers.** `trace::Inside` and `trace::trace` (the truth), `inside_medium`, `inside_crossing`,
  `inside_normal`, `trace_shape`, `trace_body` (the pass, `native/game/shader/optics_pixel.glsl`): in
  the core a ray's medium is by where it is; the liquid's level and the two cones are met only there;
  the sediment ends the ray. A body is traced where it stands upright, the ray turned back about the
  pivot and its leaving turned forward. The shapes buffer has a head of 8 vec4s a body
  (`optics_layout::SHAPE_BLOCK`): its upright rectangle, turn, pivot, the room's up, and what lies in
  it. The truth traces what lies in a vessel only under the reading that traces (the maps' reading
  and its tests are as they were).
- **Beyond the screen.** A turned vessel throws rays far: one landed 108 pixels over the screen's
  top, where the pass read the wall's first row and the truth "elsewhere" (two pixels 24 to 26 on
  the eye metric at a tilted liquid's surface). The wall's table now has a row over the screen's
  rows and one under (`wall_profile`); after it the worst of 145 such pixels is 4.2.
- **A pixel's history follows how its body lies** (its turn, levels and cones are in the body's
  signature), so a level that moves at a fixed tilt starts no stale average.
- **Under the reading that traces, no map is made and no stack is followed** (`optics_traced()`):
  a turning vessel's map was traced again every frame (0.3 s a frame on the CPU). The reference
  reads nothing past the maps' data, as the GPU's cleared buffer gives.
- **The screen** (`ui.cpp`): `lip_in`, `side_of`, `turned`: a vessel pouring by hand is a body turned
  about its tip by the lab's `turn`. **`make run` sets `LAB_BEAM=5`** (the reading that draws a turn);
  `make run LAB_BEAM=0` plays with the maps.

## Measured

- `chem_tool study tilt` (a ctest entry): each vessel half full, turned to its full tilt (the
  beaker's 87.9°, the flask's 101.2°, the bottle's 109.2°): what it holds below its lip, the head,
  what runs.
- `test_contents`: 8 cases: closed forms upright, upside down and a tilted cylinder; the level
  monotone in volume and continuous in turn; cones' volumes exact; H1 and H2 over every glassware,
  six turns, three fills, with and without powder and vortex.
- `test_lying` (the reading that traces): the surface level in the room at five turns; **O9 turned**
  (five turns, liquid, a muddy beaker, vortices of 0.7 and 1.0); O7 across a tilted surface and a
  vortex; a ray into the pile ends there and one over it goes on; 145 pixels of turned, stirred and
  muddy vessels against the truth's own rays: worst 4.2 on the eye metric (visible above 8), mean 1.0.
- Pictures by the truth's rays (`LAB_PICTURE=file lab_tests -tc="*a picture of a pour by hand*"`) and
  from the GPU: the bottle turned about its lip over the flask, its acid level in the room.

## Open

- The maps' reading draws no part turn: a pour by hand is right only under `LAB_BEAM=5`. The default
  of the tests is still the maps (TDD 29, 29.10).
- `Vessel::head_inverted` and the maps' `flipped` are still what an upside-down vessel is drawn and
  poured by; `contents::lying` at half a turn gives the same head (tested, within 0.2%) and could
  replace both.
- **`make screenshots` fails O4 at the last commit too** (`29ef135`, built apart and run:
  2 pixels at (401, 684), the acid bottle's lower corner, 16 on the eye metric, in every picture of
  the demo's bench): the maps' corner specks of TDD 25, not this stage's.
