# TDD 90: The display's exposure and the room's lights (Stage 90)

## The request

The user, 2026-11-28, after Stage 82's flames could not be yellow over the light bench: "normalize the brightness of
the screen when there are peaks ... just like our pupils do when they react to changes in brightness, a slight delay and
a smooth response ... it would also allow to dim the room lights and see dimmer light effects like fluorescence". Their
choices: the meter by the **brightest block** (16 x 9), an exposure that **darkens and brightens** (exactly 1 in the
normal room, up to 8 with the room's lights dimmed), **pupil-like** (closing fast, opening slowly).

## The design

- **The exposure** (GAME_SPEC §12.2) is a display transform: the screen multiplies the pass's linear light by it before
  the gamut. The per-pixel source holds its math (`exposure_target`, `exposure_step`, `meter_luminance`), so the CPU's
  tests run the GPU's own (`optics_exposure_target`, `optics_exposure_step`, `optics_meter`); `display_exposure()` is the
  host's (the GPU's buffer; the CPU's `OpticsScene::exposure`).
- **On the GPU** (`native/hosts/godot/optics_compute.cpp`): two passes after the others each frame. The meter (8 x 8
  groups, shared memory, one atomic add a group into its block's sum, a 256th of the room's white a unit) and the step
  (one thread: the brightest block, the target, the step, the sums cleared). A settled pixel was never written again, so
  the step raises a reshow flag where the exposure has moved by more than half a per cent: the next frame every pixel is
  shown again at it, a settled one and a bare wall's too. The frame's seconds by the host's steady clock.
- **The room's lights**: `OpticsRoom::level`. The pass's light stays in shares of the room's white at its level; every
  emitter is divided by it (a thing's glow row, a flame's and a lamp's lines, a screen's primaries; a hot thing's glow
  through the room light's table, its white at the level). The target min(1 / B, 1) then shows the room as it was at
  any level the eye can open to (8 times), and an emitter the brighter. The level keys every pixel's history (the wall's
  key) and the screen's images. The top bar's "lights" steps it; the C API passes it to the host (`cg_optics::room_level`).
- **O4**: the host reads the exposure back (`OpticsCompute::exposure`) into `cg_optics_parity::exposure`, and the truth
  is shown at it. The play's O4 line prints it.
- **A lamp's lines** (Stage 85's `lamps::spikes`): every line in one of eleven stretches, the nearest joined first (a
  neon tube's faint lines, dropped, were left to the moving points; at a dimmed room's brighter tube they showed).

## Phases

| Phase | Topic | Status |
|---|---|---|
| 90.1 | The exposure in the per-pixel source, the CPU's picture and truth at it, its tests | done |
| 90.2 | The meter and the step on the GPU, the reshow, O4 at the exposure read back | done |
| 90.3 | The room's lights: emitters against them, the top bar's control, the level to the host; tests | done |
| 90.4 | Documents, `make gates`, the user's sign-off | |

## Measured

- The flames' play (Stage 82): the exposure 0.78 with a fresh sodium drop's flame, 0.86 with potassium, 0.93 lithium;
  O4 0 pixels visibly off, the largest 8.
- The exposure play (`data/plays/exposure.json`: four lamps lit, the lights stepped down): 1.0, 0.93 at 30 %, 0.55 at
  10 %, 0.22 at 3 %; O4 0, 1, 0 and 114 pixels visibly off (the last in the neon tube: Open).

## Open

- **A neon tube at the dimmest lights**: its 158 lines in eleven stretches, each read at one point a frame; a stretch
  tens of nanometres wide is read with its whole colour's weight at that point, and where the eye's colour matching
  changes across it the reading leans (17 on the eye metric at 3 %, settled, not noise). More cuts a pixel than the
  pass's 24, or a stretch's colour weighted at its point, would close it.
- **A view moving** carries the picture as it was shown (at the exposure then): a change of exposure while it moves shows
  after it stops.
