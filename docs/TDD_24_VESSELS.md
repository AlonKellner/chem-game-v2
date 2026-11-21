# TDD 24: Vessels in their whole shape (Stage 24)

## The request

The roadmap's vessel shapes: every vessel a piece of glassware made to its standard, seen through
the optical pass in its whole shape. Decided by the user on 2026-11-21:
- **The flask is a conical flask** (ISO 1773); the beakers, the waste, the jar and the condenser
  are low-form beakers (ISO 3819); the acid's bottle is a screw-neck bottle (ISO 4796-1).
- **A cell holds the shape's real capacity**, to the brim or the neck's top, not the nominal
  capacity (a behaviour change: the goldens are re-recorded with the user's approval, with
  evidence).
- What is not rotationally symmetric (a beaker's spout, a bottle's thread and cap) is a later
  stage.

GAME_SPEC §12.3 (*A vessel's whole shape*), §13, §20; `docs/DATA_SOURCES.md` (Glassware).

| Phase | Topic | Status |
|---|---|---|
| 24.1 | Spec and sources: the three standards read from their own pages; what they leave open calibrated | Done |
| 24.2 | The shape model: glassware data, profiles of lines and arcs, capacity, the liquid's level, the silhouette; the benches' vessels made as glassware | Done; goldens re-recorded with the user's approval (below) |
| 24.3 | The tracer: arcs as tori, traced numerically; profiles as patches | Done |
| 24.4 | The truth in three dimensions, through the general tracer | Done |
| 24.5 | Maps for a shaped vessel's rows: upright bands one map, curved bands one every 4 px, each of one piece; made before the game runs (`chem_tool maps`, `make native`) | Done (the strict maps with rows measured and set aside: below) |
| 24.6 | The pixel pass: a row's band found in one read; near the rims a row read along its own outline (smooth outlines); stacks cut at the bands, found by halving | Done |
| 24.7 | The GPU host: the maps' store named; O4 on every picture; the pass's cost | Done (costs below) |
| 24.8 | Pictures; the user judges by eye and signs off | Awaiting the user |

Decided by the user while building (2026-11-21):
- **Split the renderer** (bending up and down): Stage 24 closes with the shapes, the capacities, the 3D
  truth and a renderer that reads each row of a curved band level; Stage 25 builds the full renderer.
- **Smooth outlines now**, in Stage 24 (the first cut stepped every 4 px).
- **Whatever can be made before the game runs is**: the maps are made by `chem_tool maps` into a
  store, keyed by their inputs.

## What the standards say

Read from the standards' preview pages (the scans rendered and read by eye). The figures and the
rules are in `docs/DATA_SOURCES.md`, Glassware. Two corrections to the spec's earlier plan:
- ISO 3819 §8.3 puts a beaker's base radius at 15 to 20 % of its diameter from 250 mL (the spec
  had said 10 to 20 %).
- ISO 3819 asks for no bead on a beaker: its edge is "finished in a plane parallel with that of
  the base". The bead (a "suitably reinforced" top, a "strengthening lip") is the flask's and the
  bottle's.

Checked on the way: ISO 3819:2015's table gives 1.1 mm for a 250 mL low-form beaker and 1.2 mm
for its thick-walled type; the bench's 1.1 mm is right.

## The condenser

The condenser stands for a Liebig condenser, which is glassware of another standard; it is drawn
tall and narrow (44 × 120 px). Made a beaker, its 42 × 60 mm profile could not be that shape, so it
keeps the bench's own wall and floor and stays a cylinder of its volume until a condenser is made
to its standard (decided while building, reported to the user with the goldens' evidence).

## The goldens' evidence (approved by the user, 2026-11-21)

Each vessel's cell holds its shape's capacity to the brim (or the neck's top): the flask 351 mL (was
250), a 250 mL beaker 330 mL, a 50 mL beaker 75 mL, the bottle 663 mL. From `make demo`:

| | Before | After |
|---|---|---|
| Simulated time | 9,167 s | 9,176 s (boiling down ends 9.5 s later) |
| The flask's air | 246 mL | 347 mL |
| The crystals | 9.207 g in 1,155 of 1.88 mm | 9.195 g in 1,146 of 1.89 mm |
| The balloon | 751 mL, 65 % CO2 | 831 mL, 56 % CO2 (more of the flask's air pushed in) |
| The flask's wall, the bottle's | 1.1 mm, 1.3 mm | 0.9 mm (ISO 1773), 1.5 mm (ISO 4796-1) |

Every conservation total is exact, as before.

## The maps with rows, measured (for Stage 25)

The first design (GAME_SPEC §12.3 as specified in 24.1) refined knots down a vessel's height by
halving until each middle row was the straight line between its neighbours, to a fiftieth of a pixel,
landing and drop (how far a ray lands below its row) alike. Measured on the three kinds:
- **At a fixed place across, rows are not alike near a rim**: the rim moves across as the radius
  changes, and everything near it jumps. Read at the same share of the radius they are; better still in
  a measure that keeps both rims (the inner rim at 1, the outer at 2), since the wall is as thick in
  every row and the radius is not.
- **Within a pixel of a rim rays graze and turn chaotically with the row**: no knot spacing makes them
  straight. Read by the nearer knot.
- **Route boundaries that sweep with the wavelength** where rays graze a vessel's base and corners asked
  for up to 64 pieces (0.8 s a row); capped.
- **A middle alike may hide a curve**: the quarters must be held too; and a route that changes between
  two knots must be split for, as across. Held so, the maps were right (the worst landing a few tenths of
  a pixel as a shift) but 150 to 400 knots and 5 to 9 s a vessel.

Stage 25's design starts from these: knots refined locally (by region, not by whole rows), read in the
rims' measure, made before the game runs.

## What was built

- **Glassware** (`data/glassware.json`, `lab/glassware.hpp`): profiles of lines and arcs, the inside
  moved in by each segment's thickness; capacity, the liquid's level, the radius at a height. Each kind
  held to its standard's rows and capacity rule (`test_glassware.cpp`).
- **The tracer** (`lab/optics_trace.hpp`): flat rings bounded about the axis; arcs as tori, found by
  stepping and halving on the quartic's coefficients within their own arc; arcs on the axis as spheres
  (`test_revolved.cpp`).
- **The truth in three dimensions** (`optics_reference.cpp`): every body through the general tracer
  (`test_truth3d.cpp`).
- **The renderer**: a table of each shaped vessel's bands of rows (`build_rows`, `rows_block`); each
  row's band from an index of its whole rows; near the rims each row read along its own outline;
  the store of maps made ahead (`optics_store`, `Ui::prepare_optics`, `cg_ui_prepare`,
  `chem_tool maps`). `test_rows.cpp`, `test_truth3d.cpp` (the renderer within the eye's threshold of
  the truth through an upright band; O7 through a flask).

## Costs (`make screenshots`, the 8/24 protocol)

| | Stage 23 | Stage 24 |
|---|---|---|
| A settled pass | 0.00 ms | 0.00 ms |
| A pass where every history starts again | 1.59 ms | 2.12 ms |
| The same, a vessel held over another | 9.5 ms | 19.5 ms |
| The maps, made before the game runs (both benches) | | 0.4 s |
| O4 on every picture | 0 visible | 0 visible |

Along the way: bands every 2 px doubled the restarting pass (the maps of one vessel grew from one to
63, and the GPU's cache missed); every 4 px brought it back. Reading the curved rows by the general
reading in the second pass cost 12 ms (a branch costs what it could do on the GPU): the plain reading
reads them instead. A held vessel over another costs twice what it did; Stage 25 measures it again.
