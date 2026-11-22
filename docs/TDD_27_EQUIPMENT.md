# TDD 27: Equipment: pouring as a control, supports, adjacency (Stage 27)

## The request

Chosen by the user, 2026-11-22: equipment (pouring at a rate, supports that appear with the glassware,
adjacency). Refined by the user while it was built:
- pouring is a controllable action, like stirring: a held pour (the player presses, the vessel tilts)
  and a placed pour (upside down over the target, a cap that is an active element with a slider,
  supporting signals);
- a single click picks an item up and a single click puts it down (no holding); dragging is for other
  actions (wiring); over a vessel, the left click pours and the right click opens the full menu; a
  right click on an item (the burner) chooses it and shows its controls.

GAME_SPEC §6 (drains with a rate), §12.3 (*A vessel turned*), §13 (pouring, supports, adjacency),
§13.1 (gestures), docs/DATA_SOURCES.md (*Pouring: an orifice*).

| Phase | Topic | Status |
|---|---|---|
| 27.1 | Spec | Done |
| 27.2 | The core: a drain with a rate, carrying solids; the overflow of a solution | Done |
| 27.3 | The lab: `hold`, `invert`, `pour`, `cap`, `unpour`; the cap a control; adjacency by the mouth | Done; goldens re-recorded with approval |
| 27.4 | The screen: carrying by clicks, the held pour, the menu, the cap's slider; the tutorial | Done |
| 27.5 | Drawing: upside down (the truth's rays), the stream, the supports | Done; the tilt deferred (user, 2026-11-22) |
| 27.6 | The user's test and sign-off | Signed off (2026-11-22) |

## Measured

- A held pour at half tilt from the bottle: 15 mL in two seconds (its 15 mL/s by its tilt).
- Upside down, the cap half open under 9.6 cm of acid: 5.24 mL in a second, the orifice's law.
- The demo session: 251 steps longer (the pours take their time); the crystals 9.194995 g against
  9.194996, 99.994% pure, 1146 of 1885.5 um; every other result the same.
- A bottle upside down against the 3D truth: 170 pixels, worst 2.7 on the eye metric, mean 0.15.

## Found on the way

- **A solution's overflow** (core): the excess was taken as a share of the liquid species' volume,
  then that share of the whole solution ran out: more than the excess, and the vessel was left
  under-full (V1). A drain had only carried distilled water before. Now the share is of the
  solution's own volume.
- **The menu in a tutorial** (the user: "pour upside down did nothing but slow the frame rate"): a
  tutorial refused the button it showed; it is offered only where a step asks for it. Upside down
  was not yet drawn either, then. The frame rate while a vessel is held over another is the held
  pass's known cost (TDD 25, 26).

## Open

- **A held pour drawn tilted.** The renderer would read a part-turned body in its own frame and the
  wall along a slanting line; the liquid's level for each turn found ahead. Deferred by the user
  for the stronger model, with the other renderer items (TDD 25).
