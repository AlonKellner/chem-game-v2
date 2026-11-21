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
| 24.2 | The shape model: glassware data, profiles of lines and arcs, capacity, the liquid's level, the silhouette | |
| 24.3 | The tracer: arcs as tori, traced numerically; profiles as patches | |
| 24.4 | The truth in three dimensions, through the general tracer | |
| 24.5 | Maps with rows: straight bands shared, curved bands their own; the cost of a build measured (gate: within a frame's budget) | |
| 24.6 | The pixel pass: curved bands in the second pass, the footprint as a patch of the wall; the stacks traced in three dimensions | |
| 24.7 | The GPU host; O4 on every picture; the pass's cost at or under `main`'s | |
| 24.8 | The goldens' evidence; pictures; the user judges by eye and signs off | |

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
