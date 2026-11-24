# TDD 37: The renderer, right and faster (Stage 37)

## The request

The user, 2026-11-23, after Stage 35: "Let's tackle the rest of the remaining work"; chosen from the open
items: the renderer's correctness, its speed ideas (and, in Stage 38, the trash's leftovers and the
cleanup). The open items: `docs/TDD_35_ONE.md` (Open), `docs/TDD_33_FAST.md` (Open).

| Phase | Topic | Status |
|---|---|---|
| 37.1 | The pixels that stop off the truth (O4 on the GPU) | Measured; the screenshot tool's wait lengthened; open |
| 37.2 | The prism's slit, where a node's ray is turned back | Tried: right, and too dear as it stands; open |
| 37.3 | An upside-down vessel by the general model | Looked at; left as it is (below) |
| 37.4 | Speed: the ideas of TDD 33, each measured | No target left to measure them on (below) |
| 37.5 | `make gates`; the user's sign-off | `make gates` passed; awaiting the user |

## 37.1 The pixels that stop off the truth

TDD 35 found, with the whole source in single precision on the CPU (O16), that about one pixel in ten
thousand stops visibly off the truth in double as on the GPU, and named two causes: the stopping rule's
tail, and a noise estimate that forgets.

- **A noise that does not forget** (the mean over a pixel's last 256 batches, not 16): no change. The four
  pixels of the settled bench stop at the same frame, with the same colours: their batches all agree,
  because their rays never meet the narrow light they miss. It is not forgetting.
- **More points before a pixel whose rays part may stop** (`PARTED_POINTS`: a pixel whose kept beam is not
  one way, a rim, a surface, a floor's face, takes 1,024 points, not 128, before it may stop). On the CPU,
  over every pixel of the play's bench (`LAB_SINGLE_STEP=1 ./lab_tests -sf="*test_single.cpp" -tc="O16*"`):
  visibly off the one reading 4 → 1 settled, 6 to 8 → 0 with the bottle held, settling in 206 frames for
  199. With 4,096: 0 and 0 to 1, in 255 to 271 frames.
- **On the GPU it does not help** (`make screenshots`, every picture let settle 240 frames):

  | Picture | 128 points | 1,024 for a parted pixel |
  |---|---|---|
  | 2_decomposing | 4 | 3 |
  | 3_pour_panel | 5 | 10 |
  | 6_monitor_editor | 2 | 0 |
  | 4_boiling | 7 | 7 |
  | 5_crystals (10_notices, 13_debug) | 8 | 14 |
  | 14_held | 8 | 10 |
  | the others | 0 | 0 |

  So it was not taken. The GPU's few pixels are not the CPU's: another cause, not found.
- **The screenshot tool's wait** was 40 frames; with 240 the pictures have far fewer pixels off with the
  same renderer (14_held 21 → 8, 3_pour_panel 9 → 5, 6_monitor_editor 4 → 2): the 40 frames counted
  pixels not yet settled. The wait is 240 frames now (`engines/godot/tools/screenshot.gd`), each picture
  2 s.

Open: 0 to 8 pixels a picture of 30,000 to 69,000 visibly off on the GPU after settling (O4 fails;
`make screenshots` exits with an error). Not single precision (O16), not settling time, not the stopping
rule's tail as the CPU has it. To try: the GPU's tiles of batches (`BATCH_TILE_*`: a pixel's batches come
with its tile's, and a tile that rarely has one under the rays' budget), and `LAB_O4_PRINT=1 make
screenshots` at the new wait.

## 37.2 The prism's slit

The remedy recorded in Stage 35: cut the spectrum where the middle ray's route changes. In
`beam_means` a stretch of the spectrum was halved only for a beam whose rays all go one way; tried, a stretch
is halved too where the middle ray's four nodes have not one route (`b.c0` to `b.c3`).

| Halved for a route change down to | The slit (`test_refraction.cpp`, "a bent view sweeping…") | Bands 3% off of 704 (`test_footprint.cpp`) | O13 through the play (96 allowed) | O10 on the prism |
|---|---|---|---|---|
| (not: as it is) | 3 pixels wrong at rest, 5 after the turn | 16 | holds | holds |
| a 64th of the spectrum | none wrong | 9 | 386 to 488 rays a pixel in a frame | 550 of 1,270 stretches as short as they are made |
| an eighth | none wrong | 9 | 194 to 214 | 110 of 840 |

So it is the remedy for the slit, and a pixel at a vessel's rim, where a ray's way changes with the
wavelength too (dispersion moves where light is turned back), is read in several stretches. With a record
of one beam (`KEPT_MOST 1`) a pixel of several stretches does not fit it and is traced at every read, and a
frame's rays at a rim run to twice O13's bound. Not taken. What it needs: records of several beams
(`KEPT_MOST`, `CG_OPTICS_KEPT_VECS`, the pool's size), so a pixel's stretches are traced when it changes and
not at every read; then O13 looked at again (the frame a thing moves still traces them all: a bound by
stretch, or the extra stretches put off to the next frames); then the GPU's cost measured (`make pass-cost`,
the prism's own `make prism-shots`); and O10's own case restated for stretches cut at a route's change.
The two prism cases stay marked `may_fail`.

## 37.3 An upside-down vessel

A vessel made as glassware and set upside down is already drawn by the general model: the scene gives it
half a turn about its middle (`optics_compose`'s lie), and what lies in it is `contents::lying` at that turn,
traced as any turned vessel. What is still its own: `Vessel::head_inverted` (the head over the cap of a
placed pour, `lab.cpp`'s `set_pours` and the item's view), the level in the body's form for a vessel drawn as
a plain cylinder (no glassware: tests' benches), and the shader's `flipped` for that cylinder. The head could
be `lying(vessel, π, …)`'s (the same depth to a part in a billion, by a search where it is one bisection now);
the plain cylinder has no shape to lie in. Left as it is: nothing seen changes, and the special case that is
left is the plain cylinder's, which has no general model to go to.

## 37.4 Speed

Every mark of the cost play runs at the display's rate (8.3 ms a frame), 600× included (Stage 36). What is
over it: the frames of the lab's steps at 1× while a vessel is stirred or poured into (18 to 23 ms, the frames
between 4 to 6), and a frame in which every history starts again (23 ms). Stage 36 showed the step frames'
cost is not the pixels' work (putting all of it off a frame changed nothing). None of TDD 33's ideas (the
wall through the hardware's filter, a sparse lattice of rays, a ray's derivatives, an outline pixel in closed
form, a carried vessel's beams moved with it, `path_of` once a pixel) is aimed at that, and without a GPU
time a pass (the device's timestamps are empty on this backend) a gain could not be told from the frames'
noise. Not tried; the step frames are the first thing to find (TDD 36, Open).
