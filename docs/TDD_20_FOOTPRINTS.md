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
| 20.2 | The truth: a brute-force average of sub-pixel rays, traced by its own exact tracer, in float64 | Pending |
| 20.3 | The tracer, a refactor: every body a ray meets, in 3D; today's pictures unchanged (proved pixel by pixel) | Pending |
| 20.4 | Footprints: corners and centre, exact shares where smooth, quarters where not, jitter over frames for the rest | Pending |
| 20.5 | O7 and O8, with self-tests that fire on centre sampling | Pending |
| 20.6 | Cost and pictures: `make screenshots`, O4 against the new truth, before and after | Pending |
| 20.7 | The user judges the pictures by eye and signs off | Pending |
