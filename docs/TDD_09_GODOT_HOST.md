# TDD 09: The screen and the Godot host (Stage 9)

## The request

> … an optimized C++ native core, with Godot as a thin wrapper …

Stage 9 makes the demo playable. The screen is written once, engine-free; Godot forwards the mouse
and paints.

| Phase | Topic | Status |
|---|---|---|
| 9.1 | A plain-data view of the lab (`Lab::view`) | Done |
| 9.2 | The screen (`lab/ui.hpp`): layout, gestures, the pour panel, the clock, the readout, the draw list | Done |
| 9.3 | Checked and unchecked steps mixed: one checked step a frame | Done |
| 9.4 | The screen through the C API (`cg_ui_*`) | Done |
| 9.5 | The Godot host: `ChemLab` reads the bench, forwards the mouse, paints the list | Done |
| 9.6 | The interactive bench, `data/benches/lab.json` | Done |
| 9.7 | Tests: the demo played with the pointer alone; the host's suite | Done |
| 9.8 | `make screenshots` | Done |
| 9.9 | The user's interactive sign-off | Pending |

## Decisions

- **The screen is not Godot's.** Hit-testing, dragging, what a drop means, the pour panel and the
  clock are in the engine-free layer, behind the C API. The host is about 290 lines of C++ and
  cannot decide anything about the game; the only GDScript outside the tests is the screenshot
  tool. The consequence that matters: the demo is played end to end by a test with nothing but
  pointer positions, without an engine.
- **`Ui::locate` names things.** A test or a script asks where "item:flask" or "valve:burner@1"
  is and puts the pointer there. Tests do not hold pixel coordinates, so the layout can change.
- **A drop means one thing.** Burner on vessel: heat. Balloon or condenser on vessel: attach.
  Vessel on condenser: under its outlet. Vessel on vessel: ask (the pour panel). Outbox: hand in.
  A drop that cannot be done is refused with the lab's reason and nothing moves.
- **One checked step a frame.** Checking every step at 600× would cost 25 ms a frame. Instead the
  first step of every frame is checked against every invariant and the rest run untraced; the
  readout's first line shows the conservation laws at all times. To allow it, the lab learned to
  defer the check by one step when an unchecked step with a pour in progress precedes it.
- **A burner goes under a vessel straight from the cabinet**, as a balloon goes onto a mouth.
  Requiring it to be placed on the bench first made the first gesture of the demo a refusal.

## An incident

The first `make screenshots` froze its Godot window, and the user had to stop it. The script
clicked "pause" while the pour panel was open; a click outside the panel closes it; the next
click ("pour") therefore did nothing; and the script then waited, on Godot's own thread and with
no limit, for an oxide that was never going to dissolve.

What changed: every wait in the script is bounded and gives up with an error; nothing is clicked
while the panel is open; the script was run headless first to confirm it ends; and a windowed
Godot started from a shell now gets a time limit. The rule is in `CLAUDE.md`.

## Evidence

- `ui: the three stages of the demo, played with the pointer alone`: 0.896 g of CO₂ in the
  balloon, 41.27 g of water in the beaker, 9.08 g of crystals in the flask at over 99.9% of its
  solids; the totals unchanged; no refusal on the way. The numbers differ a little from the
  scripted session's because the player here looks once every ten simulated seconds.
- The host's suite (GUT, headless): 6 tests: the bench opens, dragging places and joins, a
  refusal's reason is shown, frames advance the clock and keep the totals, the engine's own mouse
  events reach the screen.
- `make screenshots`: five pictures of the host through the demo (`build/screenshots/`).

## What a player sees

Flat colours on a dark bench: glass as white lines; liquid as a level, bluer with dissolved
copper; malachite green, the oxide black, the crystals blue, each a mound; the balloon a red disc
that grows; a flame whose height and colour follow the valve; a line from the condenser's outlet
to the beaker under it. The readout names what the pointer is on, to the milligram.

## Known limits

- **Flat.** The colours are a table, not optics (Stage 10).
- **No debug panel of rates and heat flows**, no single-stepping, no export from the screen.
- **A vessel looks like a beaker** whatever it is; the condenser is a tube with an outlet.
- **Not signed off by eye.** The tests and five screenshots say it works; whether it is pleasant
  to play is the user's to say.

## Tests

- `ui: …` (4 cases): dragging from the cabinet, along the bench and back; a refusal shown and
  nothing changed; the clock; the demo with the pointer alone
- `engines/godot/tests/test_host_smoke.gd` (6 tests)
- `capi_c_smoke`, `lab: …`: unchanged and passing
