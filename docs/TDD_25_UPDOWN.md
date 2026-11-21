# TDD 25: Rays bending up and down; smooth rows (Stage 25)

## The request

Stage 24 split off the renderer's bending up and down (decided by the user, 2026-11-21). After its
sign-off the user still saw "some pixelation, difficult to explain": at six times the size, lines seen
through a flask's cone jog where one band of four rows meets the next, and level lines pass through
the cone unbent where the truth bends them two or three rows. Approved by the user, 2026-11-21:
- rows read between knots, blended in the rims' measure (no band edges);
- each ray's drop, and the footprint a patch of the wall's rows;
- knots placed where needed, made before the game runs, capped for the GPU's cache;
- stacks follow a ray's drop.

GAME_SPEC §12.3 (*A vessel's whole shape*), §18 (O8), §20.

| Phase | Topic | Status |
|---|---|---|
| 25.1 | Spec | Done |
| 25.2 | Maps hold drops; knots refined in the rims' measure, made ahead; their reading against the rays | Done |
| 25.3 | The pixel pass: two knots a row (the light through each mixed by the share); the footprint over the wall's rows | Rows between knots done; the footprint up and down deferred (below) |
| 25.4 | Stacks follow the drop | Deferred (below) |
| 25.5 | The GPU host; O4; the costs | Done (below) |
| 25.6 | Pictures; the user judges by eye and signs off | |

Decided by the user, 2026-11-21: document the specks below and move on; the renderer's remaining work
(the specks, bending up and down) waits for a later attempt with a more capable reasoning model.

What Stage 24 measured of knots refined to a fiftieth of a pixel (`docs/TDD_24_VESSELS.md`): 150 to
400 a vessel, 5 to 9 s to make; right to a few tenths of a pixel. Made ahead the time is the build's;
the count is the GPU's cache, which doubled the restarting pass at 63 maps a vessel.

## What was built

- **Rows between knots** (`build_knots`, `knots_read`, `knots_block`; the shader's `knot_for_row`,
  `knot_rims`, `knot_span`): a row reads the two knots around it, each along the row's own outline (the
  measure both rims keep), and its light is the light through each knot's map mixed by the share of
  the way. Lines seen through a cone curve smoothly; the bands of four rows that jogged are gone.
- **Knots** refined in that measure where the middle row (and, past a pixel, the quarters) is not the
  line between its neighbours, to a tenth of a pixel (`ROW_TOLERANCE`), a route that changes between
  them split for, rims apart, at most 96 (`ROW_KNOTS_MOST`), each cropped to its own outline, made ahead.
- **Maps hold each ray's drop** (how far below its row it lands): built and tested, not yet read by
  the renderer.
- Stacks cut every two pixels at most down a shaped vessel.
- The renderer against the 3D truth through a flask's curved rows, before a wall of upright lines:
  a mean of 0.47 on the eye metric, 2 of 2,063 pixels visible (a thin line's fringe, `test_truth3d`).

## Costs (`make screenshots`)

| | Stage 24 | Stage 25 |
|---|---|---|
| A pass where every history starts again | 2.2 ms | 5.5 ms |
| The same, a vessel held over another | 20 ms | 27 ms |
| A settled pass | 0 | 0 |

Measured on the way: the restarting pass follows the maps' size (2.8 ms at 64 knots a vessel, 5.5 at
96 or 128: the GPU's cache); cropping each knot to its outline saved little. A stack cut at every knot
cost 40 ms held; every two pixels, 27.

## Open, for a later attempt

**Coloured specks along a vessel's floor and corners** (the user's "pixelated in a way difficult to
explain"). Seen beside the truth at a 250 mL beaker's lower corner: the truth's corner is a smooth dark
band with the level grid line bent up into the floor; the renderer's has coloured specks along the
floor's rows and a stepped band.
- **Cause: wavelength, not place.** A shaped vessel's maps are of one piece in wavelength
  (`ROW_PIECES_MOST = 1`): four nodes across the spectrum and a cubic through them. Inside a thick floor
  or a tight corner a ray's route often changes part way through the spectrum; there the reading takes
  the nearest node's ray, so each band of wavelengths jumps to one route or another and the pixel is
  tinted by chance. The truth integrates every wavelength and is smooth.
- **Ruled out:** sampling sixteen times a pixel where routes change more than once across it (built,
  measured: no change, removed); mixing the two knots' light instead of picking one (kept: no change to
  the specks, but no flip between knots).
- **The fix, likely:** a shaped vessel's maps of several pieces where a route's boundary sweeps with the
  wavelength (as the prism's and the upright vessels' may be). The plain reading (its cells are indexed
  for one piece), the reading between knots (`cell_on`) and the stacks (which follow one piece's four
  nodes) all assume one piece today; each must take the piece of the wavelength it reads. Memory grows
  only in the rows that need it; the restarting pass follows memory, so it must be measured.

**Bending up and down in the renderer.** The maps hold each ray's drop; the renderer does not read it
yet: a level line seen through a cone is where it is on the wall, not two or three rows away as the
truth has it. The design (GAME_SPEC §12.3): the footprint a patch of the wall's rows, each read by its
share; O8 held for a level line; stacks follow the drop.

**O4** flags one or two pixels a picture where the GPU and the reference fall on opposite sides of a
route's change between two knots (float against double).
