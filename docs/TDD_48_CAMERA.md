# TDD 48: The thermal camera (Stage 48)

## The request

The user's thermal camera (2026-11-24), the second of the two stages its design became. Stage 46 gave the
renderer the long-wave range and the sensor display; this stage is the camera itself.

What the user asked for:

- **The frame.** The sensor frame is a full rectangle, the same size as the screen. By default it overlaps
  the screen, so moving the screen is a see-through view; moved, the screen shows the infrared of the
  positions inside the frame.
- **The colours and the marks.** The colour map matches temperature measuring devices, the hottest and
  coldest spots optionally marked, their estimated temperatures in the instrument's details.
- **"Make the UX realistic."**
- **The cabinet**: "allow the player to scroll the cabinet, and make cabinet elements all shrink to fit the
  same square box size. The sensors should be deactivated when in the cabinet".

Its number is 48: another agent's Stage 47 (layered liquids) was in progress on main's next number.

GAME_SPEC 0.48.0 covers it in §13 (the `thermal` kind, the `frame` and `thermal` commands), §13.1 (the
cabinet, the frame's gestures, the readout), §18 (O26) and §20.

| Phase | Topic | Status |
|---|---|---|
| 48.1 | GAME_SPEC; `data/thermal.json` (palettes, gains: design) | Done |
| 48.2 | The cabinet scrolls; every item in a box of one size | Done |
| 48.3 | The lab: `Kind::Thermal`, `frame` and `thermal`, saved and not digested | Done |
| 48.4 | `lab/thermal.hpp`: palettes, the span and gain, the search (O26) | Done |
| 48.5 | The overlay: what the screen shows over its picture, through the renderer | Done |
| 48.6 | The `Ui`: the camera's bodies, its relay, its frame's gestures, its search a step a frame | Done |
| 48.7 | The readout's controls and readings | Done |
| 48.8 | A play with pictures, the host's case | Done |
| 48.9 | This document, CHANGELOG, `make gates`, the user's sign-off | `make gates` passed (359 s: ASan+UBSan 359, ctest 292, Godot 13 of 13, goldens unchanged); the sign-off awaited |

## What was built

**The lab** (`lab.cpp`, `lab.hpp`).

- `Kind::Thermal` ("thermal", 5 × 4 squares): no cell, no edge, like the viewer.
- Its settings (`ThermalSettings`, in `ItemView::thermal`) are set by `frame` (`room` [x, y] in squares, or
  `home`) and `thermal` (palette, emissivity, reflected_c, markers, range, level_c, span_c, gain), each
  checked first. A refused command changes none of them.
- They are saved (as bits) and in the report; not digested. Through steps with a flame under a flask, the
  digest with a camera placed, framed and set is the digest without one.
- The frame goes home when the camera leaves the bench.
- `data/benches/lab.json` has a camera in its cabinet.

**`lab/thermal.hpp`.**

- The palettes from `data/thermal.json`, sRGB control points, at 256 linear entries.
- `thermal_span`: auto (hottest to coldest, at least 5 K, moved only past half a kelvin or 3 %), or locked;
  auto gain with hysteresis (low above 140 °C, high below 120 °C), ">150"/">650".
- **The search** (`ThermalSearch`), a step at a time under a budget of reads:
  - every second sensor pixel, each a 2 × 2 square, at 8 bands;
  - the 3 × 3 squares about the four hottest and four coldest at 16 bands;
  - the centre's 3 × 3 spot.
- Its reads are `optics_lwir_signal`: the pass's own source, flat over a square, or one ray where something
  bends, the camera's bodies hidden.
- **O26**: its hottest and coldest within 0.05 K of brute force over every sensor pixel. A one-pixel stream at
  420 K on an odd column is found (147 °C at column 27). The first pass alone misses it, and fires.

**The screen** (`ui.cpp`).

- **The camera's bodies.**
  - Its case and its screen, both hidden from its own rays by a bit a camera.
  - On the bench, the screen is a sensor display (Stage 46's relay) of its frame. Its relay is given its
    frame, emissivity, reflected room, span, palette and overlay every frame, and carried to the view.
  - Its frame, away from its screen, is four thin bars; its handle is a tab over the frame's top-right corner.
- **The overlay** (`thermal_overlay`): what the screen shows over its picture, an LCD pixel a sensor pixel,
  in a 3 × 5 font with a shadow:
  - the palette's scale with the span's ends;
  - the centre's cross and spot reading;
  - the emissivity and AUTO or LOCK;
  - ">150" or ">650";
  - the hot mark (red, ▲) and the cold one (blue, ▼) with their temperatures.

  It is the screen's own light: each LCD pixel's drive and how much of it the overlay is, in the display's
  block, mixed into the palette's drive in the shader (`relay_drive`). So it is seen through what is held
  before the camera as its picture is.
- **Two tags.** What the sensor reads (what lies under its frame, where it is) is apart from how its screen
  shows it (settings, span, overlay). A palette's or a span's change redraws the screen and reads nothing
  again.
- **The search** runs over the reading scene (the room laid out, made at most every 100 ms, as the probes
  read), 600 reads a frame a camera.
- **The frame's gestures**, as the probe's: by its handle, dragged or clicked; let go over its own screen
  (more than half), home; else on the room; Escape. `locate` knows `frame:<camera>` (its handle),
  `lens:<camera>` (its middle), `screen:<camera>@u,v`, `readout:camera:<control>`.
- **The readout**:
  - the controls: palette chips; marks; gain; frame home; emissivity −/+; reflected −/+; AUTO/LOCK, locking
    at the span shown; level and span −/+;
  - the readings: where the frame is, the hottest and coldest and where, the centre, the span and gain.
  - `Ui::thermal` gives what a camera last read.
- **The cabinet.**
  - Two columns of 100 px boxes, each item as large as fits, its name under it.
  - Six rows over the trash; a row a wheel notch over the cabinet, or its bar dragged.
  - What is scrolled out is hidden (not drawn, not located).
  - The camera, like the viewer, is off in it.

## Measured

**The play** (`data/plays/thermal.json`, `LAB_SHOTS=… make pass-cost PLAY=data/plays/thermal.json`):

- the camera see-through over the room (a uniform 20 °C, the span its 5 K at least);
- the frame carried over the burner: the flame at 119 °C under the hot mark, the burner's body at the room's
  temperature invisible against the room;
- zoomed 14.6× onto the screen: the LCD's stripes, the flame drawn through them, the cross.

Frames are 8.3 ms. The worst at rest is about 50 ms, the reading scene made every 100 ms: Stage 42 measured the
same with the viewer (51 ms). The machine was under other agents' builds.

**`make screenshots`**:

- the cabinet's items are larger in their boxes (52,758 pixels of bodies at the set-up, 32,330 before);
- 1 to 10 pixels a picture visibly off;
- the largest 13 to 17, at x = 26: the cabinet's small glass, where the known O4 pixels are
  (`docs/TDD_37_RIGHT.md`). Before, 0 to 12 and 14.

**Tests**:

| Test | What it checks |
|---|---|
| `test_thermal_lab.cpp` (6) | the world unchanged; the frame's places; every setting refused with its reason; saved and loaded; every other command harmless; the palettes, the gains, the span's hysteresis |
| `test_thermal.cpp` | O26 |
| `test_camera_ui.cpp` (4, at a person's pace) | see-through, then the frame carried slowly over a flame (it reads 120 °C), the hot mark moving 30 sensor pixels as the frame moves 30 px, let go and the camera moved; home over its screen, Escape; every control; the cabinet scrolled by the wheel and by its bar, an item taken from below the fold |
| The Godot host's case | the camera and its frame by the engine's pointer; the wheel over the cabinet does not zoom the room |

## Open

- **The reading scene's spikes**: about 50 ms at rest every 100 ms with a camera or a viewer out (Stage 42's).
- **O4 does not hold a camera's screen** to its truth in `make screenshots` (its truth integrates each sensor
  pixel: too slow for every pixel of a picture); O25 holds it in the tests.
- **The search reads at 1×**, as the probes do: zoomed in, the markers are of the layout's pixels.
- **Several cameras**: each has its own bit (24 at most) and its own search; they are not measured together.
