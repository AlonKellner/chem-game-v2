# TDD 42: The spectral viewer, and a view of the room (Stage 42)

## The request

The user, 2026-11-24: "I want to add a high resolution spectral viewer instrument to the cabinet. The player
may place the instrument screen, then pick up the sensing probe, and wire it to an element on screen,
attaching it to that specific relative position on that element. It will then present the spectral
distribution on the panel, which itself is in game. In addition, I want to add a zoom in and out feature, to
allow the player to closely inspect small details, such as the instrument screen and the presented plot. The
instrument screen should be simulated as an RGB image, as if it's an actual LED or LCD screen. The image itself
should be a simple plot, with an X axis or wavelength and a rainbow like colors along it to signify which
wavelength belongs to which color on the visible spectrum."

The user's decisions:
- **The zoom** is a camera on the room only. The top bar, the readout and the panels stay as they are. It is
  a real re-layout of the scene, not a magnified picture.
- **The primaries** are smooth design Gaussians. There are no narrow emission lines yet.
- **The reading** is the light that reaches the eye from the probe's point, at 1 nm. The probe's own head
  and cable are not in it.
- **The probe's targets** are any item or part, the viewer's own screen (read a frame late), or the room.
- **"While carrying the probe the spectrum plot should update live to the current probe position, and placing
  it only fixes the position, the probe is always probing."**
- **On the plan:** "The probe is a small circle, the way it works is by uniformly sampling point to send rays
  through, and averaging over time. The probe should settle to the ground truth uniform integral spectrum of
  the circle."
- **On the plan:** "When zooming, I expect the pixels to resample the settled pixels, such that pixels in the
  new zoom have a good starting point [...] when the zoom changed by small amounts, like 10%, I expect the
  vast majority of pixels to settle very quickly. When zooming quickly, I expect the pixels to keep
  resampling and not ray tracing until the zoom movement stops [...] I prefer the pixels to be recalculated
  once after the zoom is completed."
- **"The rainbow colors should be calibrated to the wavelength if possible, so that when there's a spike in
  a wavelength - the color associated that wavelength on the plot will be visible under the probe, looking
  similar in appearance to the player."**

The spec, by section:
- §12: the spectrum probe, built.
- §12.1: an image's weights for an emitting texture.
- §12.3: a view of the room, and a view's change.
- §13: the `viewer` item and the `probe` command.
- §13.1: the probe's gestures and the view.
- §14: the C API.
- §16: the spectrometer.
- §18: O17 to O20.
- §19 and §20.

Version 0.42.0. Branched from Stage 41's tip (`f8ad333`), since 41 changes the same files and merges first.

| Phase | Topic | Status |
|---|---|---|
| 42.1 | The probe's reading: a disc, ray by ray, coarse first, settled to its integral (O19) | Done |
| 42.2 | The plot as an RGB8 picture; the rainbow as the game shows a spike of each wavelength | Done |
| 42.3 | The screen form: emission with image weights, three LED primaries, the panel's colour management (O17) | Done |
| 42.4 | The lab: the `viewer` kind (no matter), the `probe` command, saved and not digested | Done |
| 42.5 | The screen: the viewer's bodies, the probe's gesture, the live reading, the readout | Done |
| 42.6 | The view: 1x to 16x, the room laid out anew (O18); a view's change resampled (O20) | Done |
| 42.7 | The C API and the Godot host: the wheel, a pinch, two fingers, middle-drag, Home and Ctrl/Cmd+0 | Done |
| 42.8 | Costs and pictures on the GPU (`data/plays/viewer.json`) | Measured (below); Open items |
| 42.9 | GAME_SPEC 0.42.0; the documents; `make gates`; the user's sign-off | `make gates` passed (309 s: ASan+UBSan 309, ctest 253, Godot 12 of 12); awaiting sign-off |

## 42.1 The probe's reading

**Each sample.** A sample is one ray through one point of the disc at one nanometre (`optics_point`). Each
ray is the pass's own source:
- where nothing bends it, the bodies at the point and the wall there;
- otherwise the ray traced to where it lands, through a grating each of its ways by its share.

Its truth is the truth's own tracer at the point (`optics_point_truth`). Off the edges of what is drawn, the
two agree on all 3,975 rays tested. A point exactly on a drawn edge is on both sides of it: the pass reads a
thousandth of a pixel across, and the truth reads the point.

**The points.** They are uniform over the disc, by area (√ of the radius), from the plastic number's sequence
(R2). Each nanometre has its own offset, so each nanometre's points are uniform on their own.

**The sweeps.** A sweep reads one ray at each nanometre: every 16th first, then the 8ths between, and so on
down to each one. A reading cut short anywhere is still even over the spectrum. The curve draws straight
lines between the nanometres read.

**Settling and restarting.** The sweeps go on until every nanometre has 4,096 rays, and the reading is then
settled. It starts again:
- where the disc moves;
- or where what it looks through changes: the content key of its square at its middle and at four sides,
  as a pixel's history keys it.

A key that changed while the disc did not move keeps the last curve where nothing is read yet, so a
continually changing liquid shows its latest light, not an empty plot.

**The disc** is 4 px of the layout across its radius (design: a fibre's face, a tenth of a square).

**Measured, on the CPU, per ray:** 0.16 µs on a part's edge, 0.19 in a flame, 0.46 through water and 0.39 at
a jar's rim. A frame's 2 ms reads about ten sweeps, and a probe settles in about three seconds.

**O19.** The mean is within four of its standard errors of the disc's truth. The truth is the uniform
integral by the truth's tracer, on a polar grid of 24 rings of equal area with 144 points each. It was
tested across a part's edge, in a flame, through water and at a jar's rim. It fires on four wrong
samplers:
- the centre alone;
- the disc's square (told apart only settled, since the square holds a twentieth more of the part);
- points crowded to the middle (by the radius, not its square root);
- a disc moved without starting again.

## 42.2 The plot

**The LCD is 416 × 232 pixels:**
- a column a nanometre from 380 to 780 nm;
- the curve scaled to its peak, in white, with each column's colour at 35% under it;
- a grid every 50 nm;
- under the axis, an 8-pixel rainbow strip, ticks, and the labels 400 to 700 and "nm";
- over the peak, its wavelength, in a 3 × 5 font.

**The rainbow is each wavelength as the game shows a spike of it** (the user's word). Each column takes the
display's colour of that wavelength's light alone (`shown()`: its lightness and hue kept, its chroma given up
where the display has none). The light is taken at the brightness at which the display shows it most
saturated (a blue spike too bright is shown pale), dimmer toward the ends where the eye sees little. Two
earlier tries were wrong:
- one lightness for every wavelength washed the blues and the reds to white and pink;
- my own OKLab cusps were close, but were not the game's own path.

## 42.3 The screen form

**A screen is a slab whose glow is its image's.** Its set's glow row is −1 (O1 allows −1 only for an imaged
slab), so other pixels pay nothing to check for a screen.

**Its pixels.** Each pixel is three subpixels side by side, red, green and blue, each 0.8 of its third lit
across, with the pixel 0.9 lit down.

**The image block** (`OpticsScene::images`, its own buffer, binding 16) holds:
- the three primaries' light at 1 nm;
- each row's running sums of the pixels' weights.

The pass reads any sample exactly: the end pixels directly, the pixels between from the sums, and a loop
over the rows it spans. The sums are per row, so a float's sums stay within 416 and keep their precision.

**What a sample takes of each channel does not depend on the wavelength.** It is found once a sample and
read again for every band (a private variable on the GPU, thread-local in the reference and reset per
scene).

**The primaries** (`data/optics/media.json`: `led_red`, `led_green`, `led_blue`) are Gaussians of 30 nm at
620, 530 and 450 nm. Before the rainbow was calibrated, the study (`docs/DATA_SOURCES.md`) asked how close a
panel of smooth primaries shows each wavelength's spike colour, on the eye metric over 400 to 700 nm:

| Widths (1/e) | Worst | Mean |
|---|---|---|
| 20 nm or less | 0.0 | 0.0 |
| 25 nm | 2.0 | 0.46 |
| 30 nm at 615, 535 and 460 | 3.5 | 1.49 |
| 35 nm | 5.9 | 2.86 |
| 30 nm at 620, 530 and 450 (chosen) | 1.6 | 0.31 |

They are scaled so that full white over a pixel is the room's white. The panel drives its primaries to show
each colour; where they cannot reach it, it moves toward grey of the colour's luminance. Seen through the
pass, the screen's rainbow is within 3.0 of the game's spike colours (worst at 400 nm).

**The truth** reads a screen pixel by pixel and subpixel by subpixel from its weights, written apart from the
pass. **O17** holds the pass to it, to a float's precision, over samples from a ray's point to many pixels.
It fires on sums shifted a pixel, and on red and blue swapped.

**O12 and O16.** Held water stands in front of a screen whose plot changes:
- 1,584 of the water's pixels keep their beams through a new plot (the picture's hash is in the colour key);
- in single precision, 6 of 6,382 pixels are visibly apart, as O16 allows.

## 42.4 The lab

**`viewer`** is a kind with no cell and no edge. Each command that reads an item's cell is guarded
(`holds_matter`). Its footprint is 7 × 4. The world and its digest are the same with a viewer or without,
step for step, with a flask boiling, and through `probe` and `move` commands. Every command aimed at a
viewer, with it in every field, is refused or leaves the world as it was.

**`probe`** fixes the probe on an item or a part at (u, v) of it in its own frame, on the room, or back in
its holder. The link:
- is an id, since signal parts are not items;
- is saved (optional fields: a lab without a probe out saves as before), and not digested;
- goes back to the holder when what it is on leaves the lab, or the viewer leaves the bench.

`lab.json` holds a viewer in its cabinet; the shelves go from 91 to 82 px. Three tests that counted every
slab and bar of that bench now count the viewer's 3 slabs and 13 bars.

## 42.5 The screen

**The viewer's bodies** are all of the pass:
- a case, a screen and a holder;
- a ring of six bars about the probe's disc, a handle, and a cable of six sagging bars to the case.

**The disc's middle:**
- carried: the pointer;
- fixed: the target's upright rectangle at (u, v), upside down and turned with it;
- on the room: its point;
- otherwise: the holder.

**The reading scene.** The probes read a scene made without their own heads and cables, at 1x whatever the
view. It is made again whenever a probe moves, and at most every 100 ms while they rest. Each frame reads
one sweep of every nanometre first, then rays for 2 ms (at most 16,384). The picture is plotted at once and
shown the next frame, so on its own screen a probe reads the frame before.

**Off the bench the viewer is off:** its screen is dark and its probe reads nothing.

**The gesture:**
- a click takes the probe up, and the next press lets it go;
- pressed and moved, it goes where the button is let go;
- Escape puts it back.

Let go, it fixes the probe on whatever is under it: an item or a part, the viewer's own screen, the rest of
the viewer (home), or the room.

**The readout** says where the probe is, how much it has read and the peak (in mW/(sr m² nm) on the room's
white of 100 cd/m²), and has a "probe home" button.

**Tested at a person's pace:**
- a slow sweep from the holder over the acid, read live at every frame;
- let go on the acid, then the acid moved;
- dragged onto the bench, onto its own screen, and home;
- Escape;
- the readout's button.

Over a blue flame wide open, the probe's peak is at 431 nm, 1.7 times the wall's light.

## 42.6 The view

**The view** (`lab/view.hpp`) is a scale and the layout's point at the room's corner, clamped to the room.
In the `Ui`:
- the room's commands of the draw list are placed by it (its paint sized too);
- masks cover what the room spills past its part of the screen;
- the pointer's points over the room are the view's (not over the drawer, an open panel, the editor or the
  menu);
- `locate` and the tutorial's guide give the view's places.

**The scene at a view.** Bodies, ends, pivots, sizes (not a grating's pitch) and dashes are all scaled. So
are the wall's distance, a layer's gap and the room's cube. The cube's middle moved into the scene's header
(a fifth vec4), where 960, 540 was written in the shader. Two things had to change for 1x and a view to be
one geometry:
- **the wall's ruling** is drawn as the pixel strips 1x paints (a centred line a pixel wide was half a
  pixel off its layout at any view);
- **a glassware vessel's axis** is placed on a whole pixel of the layout (`OpticsRoom::pixel`), not of the
  screen.

**O18** holds a 4x view of the acid to its layout:
- 1 ray in 8,093 apart, landing 0.05 px from an edge;
- of the rays that land on the view's own screen (below: Open);
- at points 0.47 into the layout's pixels, since what is drawn is painted on whole pixels of each.

It fires on a view whose wall is not moved back with it.

**A view's change.** The view is moving while it changed within the last 120 ms (`VIEW_QUIET_MS`).

*While it moves:*
- the scene is not made again;
- the GPU keeps its pixels' state as the anchor (`OpticsCompute::anchor`);
- a fourth pass from the one source (`OPTICS_RESAMPLE_PASS`) shows the anchor carried to the view now:
  between pixels' middles closer, a 4 × 4 mean over a pixel's span further out;
- beyond what the anchor holds of the room, the room as last seen whole at 1x (kept every 30 frames there,
  and when a move starts from it).

*At rest:*
- the scene is made once;
- the pass writes each pixel's state carried from the anchor, its content key's shape parts none;
- the first frame reads every pixel's estimate, and the drift rule keeps or restarts it.

The prior's points are the fewest of the anchor's, unhalved. Halved, a settled pixel fell under 128 and
asked again: 23% of a fresh start's points became 49%. The reference does the same
(`OpticsFrames::resample`).

**O20, measured on the signal parts, the wire and the ruling, a tenth closer:**

| | Carried from the anchor | A fresh start |
|---|---|---|
| Pixels of bodies that kept their history | 10,190 of 10,612 (96%) | — |
| Points spent in three frames by the 11,416 pixels nothing bends | 276,612 | 1,187,260 |
| Pixels visibly off the settled picture | 0 | 0 |
| Settled, visibly apart from a fresh start | 0 | — |
| Given the scene's whole key (the self-test) | 751 visibly off | — |

What I learned measuring it:
- **The anchor in the test must be whole.** A sparse anchor answers for a pixel it does not hold with its
  nearest sample, up to 2 px away: 13% restarted.
- **A screen whose plot is changing restarts its pixels anyway,** which is a change of content.
- **Inside a vessel nothing is saved:** a pixel read by its own rays retraces its beam after any change of
  its geometry (54,000 rays carried or fresh).

## 42.7 The host

`ChemLab` takes:
- the wheel (a quarter a notch, about the pointer);
- `InputEventMagnifyGesture` and `InputEventPanGesture`;
- the middle button held;
- Home and Ctrl/Cmd+0.

The C API adds `cg_ui_zoom`, `cg_ui_pan`, `cg_ui_view`, `CG_KEY_VIEW`, and `cg_optics`' images, views and
room. A GUT case pushes a wheel, a pinch and Home. No GDScript was added (the budget is 300 of 300).

**Plays** (`lab/play.hpp`) gained:
- `do`: a lab command, to set a bench out;
- `zoom`: a wheel's notch a frame;
- `shot`: a picture the host saves into `LAB_SHOTS` once it is painted.

`LAB_PLAY_PRINT` prints a slow frame's parts.

## 42.8 Costs and pictures

`LAB_SHOTS=$PWD/build/screenshots make pass-cost PLAY=data/plays/viewer.json` (Apple M3 Pro, a machine
busy with another stage's builds; a frame's mean, then its worst):

| Mark | Frames | A frame | The worst |
|---|---|---|---|
| settled (the viewer, a flame, the acid) | 130 | 9.1 ms | 51 ms |
| carry (the probe over the acid to the flame) | 100 | 8.3 ms | 8.9 ms |
| reading | 190 | 8.3 ms | 52 ms |
| zoom (to 14x and back out) | 24 | 8.3 ms | 57 ms |
| rest (at 14x on the screen) | 70 | 8.3 ms | 250 ms |
| tenth (a tenth out, at 12x) | 41 | 8.4 ms | 247 ms |
| whole | 70 | 10.6 ms | 64 ms |
| (every history starting again, 1x) | 10 | 25 ms | |

Carrying the probe cost 26 ms a frame until the panel's drive was worked out once a colour: a plot has about
a thousand colours, and each of its 96,000 pixels ran the colour management twice a frame.

The pictures (`build/screenshots/viewer-*.png`):
- `viewer-flame`: the probe over the flame, the plot's peak at 431 nm.
- `viewer-screen-14x`: the screen's red, green and blue subpixels resolved, the strip and the 500 nm label.
- `viewer-moving-out`: at 2.2x on the way out, the room carried from the 1x anchor and the plot from the
  view's own.
- `viewer-whole`: back at 1x.

## Open

- **The first frame at rest where a screen fills the room's part takes 250 ms on the GPU** (the frames after
  it take 8 ms). The cause is understood:
  - at 12x to 14x a tenth's change moves the subpixels' stripes under every pixel, so every history rightly
    starts again;
  - a restarting pixel always reads its whole fixed estimate and a least batch; only batches are under the
    budget;
  - 1.4 million pixels of the screen take 250 ms, where a whole restart at 1x, a tenth of the room covered,
    takes 25.

  The host sets the first frame's asks as a whole restart's, so its batches keep to the budget. That leaves
  the estimates. The fix: let the pixels whose history starts again take their estimate in turns, tile by
  tile under the budget (as batches already do), showing their carried colour until their turn. That is a
  change to the pass's scheduling, for a stage of its own.
- **Past the view's own screen, the wall is the far wall's mean.** The wall's table is made in the screen's
  pixels for each view, and covers only the screen. A ray that a vessel bends past it (at a rim, zoomed in)
  reads about 1% other light than it would laid out (O18 counts only what lands on the screen).

  The fix: the wall's table in the layout's pixels at every view, with the view's map applied at every read
  (the wall's integrals, which callers take in the screen's lengths, scaled with it). This would also drop
  the wall's rebuild at each view.
- **O4 on the GPU** (`make screenshots`, the demo's pictures): as before the stage, a few pixels a picture are
  visibly off. Measured against Stage 41's tip built beside it:

  | | Visibly off a picture | The largest difference |
  |---|---|---|
  | Stage 41's tip | 0 to 9 | 14 |
  | Stage 42 | 0 to 12 | 14 |

  The pixels Stage 42 adds are at the cabinet (x about 125): the viewer is a tenth item there, so its glassware
  is drawn smaller (shelves of 82 px, not 91), and small glass is where the known O4 pixels lie
  (`docs/TDD_37_RIGHT.md`). Nothing of the pass's own changed there; open as O4 is.
- **The pool of kept beams** grows to its cap (384 MB) when a vessel fills a 16x view; past it, rectangles
  trace every frame. This was not reached in the play.
- **The probe reads at 1x.** Zoomed in, the disc is still 4 px of the layout. A probe whose disc shrinks
  with the view, for finer inspection, would be a setting.
