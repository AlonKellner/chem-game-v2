# TDD 37: The renderer, right and faster (Stage 37)

## The request

The user, 2026-11-23, after Stage 35: "Let's tackle the rest of the remaining work"; chosen from the open
items: the renderer's correctness, its speed ideas (and, in Stage 38, the trash's leftovers and the
cleanup). The open items: `docs/TDD_35_ONE.md` (Open), `docs/TDD_33_FAST.md` (Open).

| Phase | Topic | Status |
|---|---|---|
| 37.1 | The pixels that stop off the truth (O4 on the GPU) | Measured; the screenshot tool's wait lengthened; open |
| 37.2 | The prism's slit, where a node's ray is turned back | |
| 37.3 | An upside-down vessel by the general model | |
| 37.4 | Speed: the ideas of TDD 33, each measured | |
| 37.5 | `make gates`; the user's sign-off | |

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
