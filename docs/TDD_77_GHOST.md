# TDD 77: The ghost (Stage 77)

## The request

From Stage 71's sign-off (2026-11-27), by the user's choice, two renderer items; once the first was fixed, the user
split them (2026-11-27): this stage the ghost alone, merged before the other session's Stage 78 (reflection traced,
each material's complex index), and the nested rays a stage of their own after it, built on its traced reflection:

1. **The ghost** (the user, during Stage 71): stirring the ice bath after pouring salt into it by hand, "the rendering
   leaves a strange ghost image of the pile, not settling to the expected background"; "a sort of triangular pattern
   which changes from brighter to transparent and back to bright, repeating with lowering heights"; "the initial
   zoom scale dictates whether the ghost will appear or not ... more pronounced when the zoom scale is the most zoomed
   out ... It might have to do with the speed of the pile movement"; "it feels like the pixels settle on brighter
   values because something does not recalculate them properly, some of their bias is left behind".
2. **Nested rays**: "when a vessel is inside a vessel - rays should be rendered as entering the outer vessel, and
   then proceeding to hit any surface along the way and reacting accordingly, including the inner vessel. For
   example, a ray may enter the outer vessel, then enter the inner vessel, then exit the inner vessel, and then exit
   the outer vessel, and only then reach a wall. It should be correctly simulated."

## 1. The ghost

### Found

- The lab and the scene are right: the user's exports (loaded on the CPU by `Ui::open`, which opens a save from
  Stage 71) render cleanly by the truth's own tracer and by the renderer's frames.
- `data/plays/ghost.json` reproduces it on the GPU (the freezing point chosen, its tutorial free, the jar carried over
  the bath and poured by hand, set down, the bath stirred, paused, 240 frames settled, exported; `LAB_SHOT_PARITY=1
  LAB_SHOTS=... make pass-cost PLAY=data/plays/ghost.json`: a play's picture held to the truth by the host,
  `chem_lab.cpp`, as the screenshots' are). The settled GPU picture against the CPU's over the bath: a chevron
  between the pile's old cone and its new one, brighter by up to 25 of 255.
- The same play on the CPU (`lab::Play` through the renderer's own frames, as the host plays it at 120 Hz)
  reproduces it too, and alike with beams kept or not, corners shared or not: the pixels' history. The salt tipped
  in by a command (as every earlier CPU check did) does not.
- **The cause** (`step_pixel`, the drift rule, Stage 33): a pixel whose content changes keeps its history while its
  fixed estimate has moved, summed, by less than `DRIFT_MOST`: meant for a colour that changes under rays that go
  as they went (a liquid darkening). As a pile's surface sweeps slowly across a pixel (a fraction of a pixel a frame
  at the room's whole view: the zoom sets its speed on the screen), its beam is traced again each frame (what lies in
  the vessel changed on rows its rays pass), its estimate moves a little each time, and the rule kept the
  corrections its old rays had found, over the new estimate; with its points kept it counted as settled and asked
  for nothing (`done`), so they stood: the old shape left behind, until the drift's sum passed `DRIFT_MOST` and it
  started again (the user's "from brighter to transparent and back to bright").

### The fix

- A pixel read by its own rays whose beam was traced again (its rays moved) starts its history again; the drift rule
  keeps only a history whose rays go as they went (`rays_moved = both && !beam_held`).
- Held: `native/tests/game/test_ghost.cpp`, the play on the CPU, every pixel the bath covers within the eye metric's
  8 of the truth of its area once paused and settled (0 of 9,072; without the fix 41, the worst 10.9). On the GPU the
  same play's settled picture: no chevron, O4's visible pixels 70 → 23 (none in the bath).

## 2. Nested rays

Moved to a stage of its own after Stage 78 (the user's choice): the request above stands as its brief.

## Phases

| Phase | Topic | Status |
|---|---|---|
| 77.1 | The ghost: reproduced (the play, on the GPU and the CPU), its cause, the fix, its test | Done |
| 77.2 | GAME_SPEC, CHANGELOG, `make gates`, the user's sign-off | |

## Open

- The CPU's frames keep a wall pixel's state where nothing covers it (`optics_pixel_host.inc`: "the pixel keeps what
  it had"), where the GPU draws the wall from its table: a renderer's frames read there show what was last over it
  (a jar carried across: stripes). Nothing on the screen shows it; a test of the frames reads only covered pixels.
