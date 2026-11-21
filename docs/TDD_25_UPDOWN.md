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
| 25.2 | Maps hold drops; knots refined in the rims' measure, made ahead; their reading against the rays | |
| 25.3 | The pixel pass: two knots a row; the footprint over the wall's rows; O8 level | |
| 25.4 | Stacks follow the drop | |
| 25.5 | The GPU host; O4; the costs against Stage 24's (2.2 ms restarting, 20 ms held over another) | |
| 25.6 | Pictures; the user judges by eye and signs off | |

What Stage 24 measured of knots refined to a fiftieth of a pixel (`docs/TDD_24_VESSELS.md`): 150 to
400 a vessel, 5 to 9 s to make; right to a few tenths of a pixel. Made ahead the time is the build's;
the count is the GPU's cache, which doubled the restarting pass at 63 maps a vessel.
