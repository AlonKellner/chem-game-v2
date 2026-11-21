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
| A settled pass | 0 | 0 (0.23 ms since the pass shows the room: each bare pixel's state is read) |

Measured on the way: the restarting pass follows the maps' size (2.8 ms at 64 knots a vessel, 5.5 at
96 or 128: the GPU's cache); cropping each knot to its outline saved little. A stack cut at every knot
cost 40 ms held; every two pixels, 27.

## Frame drops on placing a vessel, and after its contents change

Found while the user tested (2026-11-21): the frame rate dropped when a vessel was first put on the
bench, or after what it held changed.
- **Placing: maps made in play.** The tables made ahead (`chem_tool maps`) are keyed by their inputs,
  the span from the vessel's axis among them. On the bench the axis, a whole pixel, came back as
  `rect.x + w / 2` in float, a few millionths off, so the key missed and the tables were made again
  while the game ran: 742 ms for a 250 mL beaker. Vessels on the cabinet's shelf and in an outbox slot
  are drawn smaller, and no table was made ahead at those sizes. Now a shaped vessel's span is from its
  width alone and its axis a whole pixel (the shader rounds it too); `prepare_optics` makes every size
  the screen draws (bench, shelf, slot), on both layers. Compose on placing: 742 ms → 5.7 ms.
- **Placing and lifting: every map sent again.** Any new map laid out all the maps again (19 MB,
  copied and sent to the GPU), and lifting a vessel (the nearer layer's tables) and setting it down did
  so each time. Now maps lie where they were first put, new ones after them, and the scene tells from
  where they changed (`maps_changed_from`, `cg_optics`); the host sends only that. Maps no body looks
  through are kept until they outweigh those in use and 16 MB, then laid out again. A vessel lifted a
  second time sends nothing. When the buffer must grow, the GPU copies what it has into the new one
  and clears the room beyond (it was all sent again from the CPU, the empty room too).
- **What is left on placing: the vessel's first estimate.** Every pixel of a vessel newly placed starts
  its history in the same frame (64 points each, through two knots' maps): about 9 ms more than a
  frame for a 250 mL flask, measured by placing it a second time, its maps already on the GPU.
- **After contents change: the GPU.** The pixels' histories follow what a vessel holds: a pour costs
  its pixels one frame like a vessel newly placed, then nothing; a reaction under way (malachite
  heated, oxide dissolving at 600x) re-estimates its pixels every frame. Measured in the host (a
  144 Hz display): no frame over the display's 6.9 ms while reacting.

| In the host (an M3 Pro at 120 Hz: 8.3 ms a frame) | Worst frame |
|---|---|
| A 250 mL beaker placed, before | 742 ms in compose alone |
| The flask placed, after: its maps new, or already on the GPU | 23, 17 ms |
| The acid's bottle placed, after | 19 ms |
| A pour, after | 13 ms |

Tests: `test_maps_store.cpp` (a vessel finds the tables made ahead wherever it stands, is held, is in
the cabinet or the outbox: nothing is made in play; a vessel lifted and set down again changes no map,
and what changes is only what was added).

## A box around every vessel

Seen by the user (2026-11-21), in two parts.
- **The window's scale.** The project stretches to its window (`canvas_items`): the room (the draw
  list's first part) was drawn at the window's own pixels, sharp, while the optical pass, a texture of
  the game's 1920 x 1080, was stretched and smoothed. Within each vessel's rectangle, where the pass
  paints the wall around the glass, the grid lines stood elsewhere and softer, and the rectangle's edge
  showed. The screenshots render at exactly 1920 x 1080, where it does not show; a real window at
  1500 x 844 (a Retina screen, scale 2) showed it plainly. Now the pass shows the wall wherever no body
  is (`wall_shown`: the wall's mean over the pixel, read from the same table), written once for each
  wall (a bare pixel's state holds the wall table's tag), so the room and the bodies are one picture,
  scaled as one; the overlays stay sharp over it.
- **Half pixels.** The grid lines are one pixel wide on whole coordinates: the host paints the pixels
  whose centres a shape covers (one row), the wall's table took the shape exactly (half of two rows);
  and the host centres an outline's stroke on its edges, which the wall had inside them. Now each
  shape enters the wall on the pixels the host paints.

Tests (`test_wall_pixels.cpp`): every pixel of the wall is one colour drawn there, never a share of
two (it fails on the old wall at every grid line); the renderer's picture is the whole room, and
around a vessel's glass, within its rectangle, each pixel is what it is with no vessel there.

## Open, for a later attempt

**A held vessel over a shaped one: steps in what is behind.** Seen by the user (2026-11-21): carrying
the beaker slowly over the flask, the flask's outline seen through it changes shape, as if lines
became rectangles. The stacks (the rays of what is held, followed through what stands behind it, on
the CPU) are bands of rows, each read at its middle; down a shaped vessel behind, they are cut at its
knots no closer than two pixels, and in a cone's middle its knots are five to eight pixels apart. So
the flask behind is read at one height for five to eight rows: its slanted outline is a staircase
(the 3D truth, `optics_area_truth`, moves it a fraction of a pixel each row), whose steps move as the
held vessel does. The renderer's own dense truth (`optics_truth`) has the same steps: it reads the
same stacks.
- **Ruled out: a band every row** where a shaped vessel's rows change (built, measured): right, but
  compose while carrying went from 6 to 31 ms and the stacks from 7 to 40 MB, sent every frame.
- **The fix, likely:** bands at the knots, as now, and a row between two bands read from both, each in
  the measure of the outline of what is behind as seen through what is held (what the knots' reading
  does with a vessel's own rims), mixed by the share of the way. The stacks would record, per band,
  where the outlines behind are seen.


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

**O4** flags one to three pixels a picture where the GPU and the reference fall on opposite sides of a
route's change between two knots (float against double). Where, in `make screenshots` (2026-11-21):
(130, 475) and (130, 711), vessels drawn smaller on the cabinet's shelf, up to 18 on the eye metric;
(401, 684), the floor of a beaker on the bench, 16. Since the grid lines are one row each (a sharp line
seen through a rim), the count rose by one in some pictures; the largest difference did not.

**A drag across a scaled window.** Pointer positions in the game's pixels are fractional when the
window is not 1920 x 1080: a held vessel's top is then a fraction of a pixel, which the knots, the
stacks and each pixel's history follow exactly (the reference renderer, one frame a move of 0.3 px, is
within 0.02 of its truth). What shows while dragging is the stacks' steps (above) and the restarting
histories' noise, not the fraction itself.
