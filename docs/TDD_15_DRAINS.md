# TDD 15: Removable drains (Stage 15)

## The request

From the open issues the user asked to have planned and tackled:

> Drains are never removed: a condenser can't move once a beaker stands under it, and a beaker
> handed in stays joined to the outlet.

GAME_SPEC §6, §13.

| Phase | Topic | Status |
|---|---|---|
| 15.1 | A drain comes off when either vessel is placed elsewhere, moved apart, stowed or handed in; moved together they stay joined | Done |
| 15.2 | The screen's softened refusal gone; the tutorial's condenser step reworded | Done |
| 15.3 | The demo's record re-recorded, with the user's approval ("Approved, proceed", 2026-11-17) | Done |
| 15.4 | The user plays it (`make run`) and signs off | Pending |

## Decisions

- **Either end.** A drain is between two vessels. Moving either away from the other ends it, not
  only the one under the outlet: a condenser with a beaker under it is no longer stuck.
- **A drain is taken off at a step boundary.** The lab finds it among the world's open drain edges
  by its two cells. Edges are closed, never reused, so nothing new is saved.
- **Moved together** (one `move` with both), the two stay joined. Moved in a selection with only
  one of them, the drain comes off.
- **Detaching a condenser from the flask** keeps its drain: it is set down beside the flask, not
  moved by the player.

## Evidence for the record's change

The demo session, before and after: its whole output is the same but for one time.

| | Before | After |
|---|---|---|
| command 23, `submit beaker` | 2545.0 s | 2545.1 s (the step left open by the pour, finished before the drain comes off) |
| the beaker's water | 40.868202 g | 40.868202 g |
| the balloon's CO₂ | 0.895898 g | 0.895898 g |
| every element, mass, energy | unchanged | unchanged |

The record's digests change from command 23 to the end (three lines of `bench/sessions/demo.jsonl`):
a drain that has come off is part of the state.

## Tests

- **`test_lab.cpp`:**
  - the beaker placed away, then the condenser placed freely;
  - the condenser placed away;
  - either stowed;
  - the beaker handed in, with liquid in the condenser: nothing runs down any more, and every total
    is unchanged;
  - moved together, joined; one moved alone, parted.
- **`test_tutorial.cpp`, `test_ui.cpp`:** both playthroughs and the pointer demo complete with the
  condenser moved to a free square.
