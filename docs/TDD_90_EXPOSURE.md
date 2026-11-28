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
| 90.4 | A bright flame fading shown black in places (the user's report; Stage 82's): a history's change measured at the exposure and by the light's amount beyond the display | done |
| 90.5 | The experiment `flames` (the user's request): its bench, tutorial and tests; a flame's load by element, the lights in a tutorial's checks, the probe in a flame | done |
| 90.6 | Documents, `make gates`, the user's sign-off | |

## Measured

- The flames' play (Stage 82): the exposure 0.78 with a fresh sodium drop's flame, 0.86 with potassium, 0.93 lithium;
  O4 0 pixels visibly off, the largest 8.
- The exposure play (`data/plays/exposure.json`: four lamps lit, the lights stepped down): 1.0, 0.93 at 30 %, 0.55 at
  10 %, 0.22 at 3 %; O4 0, 1, 0 and 114 pixels visibly off (the last in the neon tube: Open).

- Its cost on the GPU (`make pass-cost`, the cost play, the M3 Pro, against Stage 85's build back to back): settled 7.90
  ms a frame against 7.74, carried 7.53 against 7.07, poured 10.88 against 10.08, at rest 12.51 against 11.48, stirred
  15.95 against 14.56, fast 13.03 against 12.22: the meter and the step, and the frames that show every pixel again,
  0.2 to 1.4 ms a frame (2 to 9 %).

## A bright flame's black pixels (90.4)

The user saw black pixels in a very bright flame. They were Stage 82's, not the exposure's: the same play on Stage 85's
build showed them too. A pixel keeps what its history found (its corrections, over its fixed estimate) while its
estimate moves by less than `DRIFT_MOST` on the eye metric, summed. The metric read each colour through `gamut`, which
since Stage 82 shows a light brighter than the display at its brightest channel whatever its amount: a flame fading
from 30 to 20 times the room's white was no change at all, and its pixels kept the corrections found at 30 (a sodium
flame's are large: its lines are read by the band cuts) while their estimate fell, until the corrections outweighed it,
red below nought, shown black. Now two of a pixel's colours are compared as they are shown (`apart`, the per-pixel
source): at the display's exposure, and where either is brighter than the display both scaled by the brighter's
brightest channel, so that light beyond the display is told apart by its amount. In the display's range at exposure 1
nothing changes. The test (`test_flame.cpp`, Stage 90): the burner's flame after ten flame tests fading to a
thirtieth, four frames at each 5 % step, in double and in single precision; 179,307 dark pixel-frames with the old
measure, none with this.

## The experiment flames (90.5)

The user asked for an extensive flames experiment to follow. `data/benches/flames.json`, `data/tutorials/flames.json`
(32 steps in five parts): the burner shut down (soot's yellow) and wide open (its blue); the probe in the flame; sodium,
lithium and potassium (the lights at 10 % for it), each line written in the notebook; calcium, strontium, barium and
copper, each peak written; the flame let clear between tests (the clock runs only in those waits: a step that asks for
a gesture holds it, and the flame with it); a mixture of sodium and potassium chlorides; two unknowns drawn by the
seed; the report: three lines, a mole of sodium's photons (203.0 kJ), strontium against lithium, copper against barium,
what the mixture holds, the unknowns. What the viewer lists, played (`test_flames.cpp`): sodium 589; lithium 671;
potassium 766; calcium 423, 555, 625 (its peak), 645 and potassium's last trace at 766; strontium 461, 606, 649, 659,
671, 682 (its peak); barium 488, 502, 513, 524, 554 (its peak: the atom's line, sharper than the bands), 745; copper
505, 512, 524, 533, 546 (its peak), 605; the mixture 589 and 766.

On the GPU (a play zoomed 6x on the burner): the plain flame pale blue, sodium orange-yellow, lithium and strontium
crimson, calcium brick red, barium and copper two greens, the mixture sodium's. Potassium shows as the flame's own blue
even in the dimmed room: the model gives it only its lines (766 and 770 nm, where the eye is nearly blind, and a weak
violet pair at 404); a real flame's lilac is not reached by them alone here (a matter for the flame model: Open); the card says what is seen.

New in the lab layer and the screen: a burner's `flame_umol` with an element for its measure (that element's load), a
tutorial's `lights_at_most` and `lights_at_least` (the room's lights, read from the screen), the viewer's probe dropped
in a lit burner's flame resting on the burner (`Ui::flame_at`), and the screen's name `flame:<burner>`.

## Open

- **A neon tube at the dimmest lights**: its 158 lines in eleven stretches, each read at one point a frame; a stretch
  tens of nanometres wide is read with its whole colour's weight at that point, and where the eye's colour matching
  changes across it the reading leans (17 on the eye metric at 3 %, settled, not noise). More cuts a pixel than the
  pass's 24, or a stretch's colour weighted at its point, would close it.
- **A view moving** carries the picture as it was shown (at the exposure then): a change of exposure while it moves shows
  after it stops.
- **Potassium's lilac**: the flame model gives potassium its lines alone, and a potassium flame shows as the burner's
  blue, even with the room's lights at 10 %. What else a real one's lilac takes (its weak lines' share at the flame's
  temperature, any continuum) is not in data/flame.json; a measured potassium flame spectrum would settle it.
