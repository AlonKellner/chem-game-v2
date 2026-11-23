# TDD 36: A frame's cost the same at any speed (Stage 36)

## The request

The user, 2026-11-23: "making all calculations have a constant frame cost, regardless of the replay
speed". Decided with the user the same day, between three ways:
- **A budget plus cheaper steps** (chosen). The step stays 1/60 s (GAME_SPEC §3; D1 and UX3 hold, the
  session records do not move). A frame runs steps until a budget of the CPU's time is spent and carries
  the rest; a heavy moment runs the clock slower; the steps that are dear are made cheap so that 600×
  is kept in ordinary play.
- Not: longer steps at high speed (the state would depend on the speed played at).
- Not: the budget alone.
- And: the GPU's worst frames, at the simulation's steps, spread over two frames of the display
  (tried, measured, and taken out: below).

GAME_SPEC §13.1, §19.

| Phase | Topic | Status |
|---|---|---|
| 36.1 | Measuring: `chem_tool study speed`; a mark at 600× in the cost play | Done |
| 36.2 | The budget: `Ui::set_budget`, `set_clock`, `achieved`; `cg_ui_set_budget`; the Godot host's 3 ms; the speed kept shown | Done |
| 36.3 | Cheaper steps | Done (what is below) |
| 36.4 | The GPU's step frames | The spread tried and taken out; the scene sent by its changed pages |
| 36.5 | GAME_SPEC §13.1, §19; `make gates`; the user's sign-off | |

## What is in the tree

- **`chem_tool study speed`** (`native/hosts/headless/speed.cpp`): the screen's frame (`Ui::frame` and
  the scene composed) timed headless at 1×, 60×, 600× and 600× under a budget of 3 ms, through nine
  scenes set up by the lab's own commands (the bench still; malachite decomposing; the oxide dissolving,
  stirred; the same with three monitors and a controller; boiling into the condenser; poured by hand: a
  bottle of acid, a beaker of water and sediment, the same stirred; under the tutorial). Columns: the
  steps a frame, the frame's mean and worst ms, µs a step, the scene composed, the speed kept.
  `CHEM_SPEED_ONLY=<part of a name>` runs some. A setup command over 20 ms is printed (a controller's
  trial).
- **The budget** (`lab::Ui::set_budget(ms)`, `set_clock`, `achieved`; `cg_ui_set_budget`). A frame runs
  its steps in pieces: the first step alone (it is the one checked against every invariant), then eight,
  then as many as the time left holds at what the steps so far cost; it stops when the budget is spent,
  having run at least one step. What it could not run is carried, no more than one frame's worth (so
  letting go of a heavy moment never runs a burst). An alarm still stops the lab in its step. Under a
  tutorial the same, a second at a time. 0, as a screen is opened: no budget (the tests' screens, the
  sessions); the Godot host gives 3 ms (`LAB_STEP_BUDGET_MS`). Beside the clock: "keeping 212x" where the
  speed kept is under nine tenths of the speed chosen.
- **Tests** (`native/tests/game/test_speed.cpp`), on a clock of the test's own (each step costs it a fixed
  time, so what a frame runs is exact): under a budget that holds 60 steps a frame at 600× (300 asked)
  each frame runs 60 or 61, the speed kept is 120×, and the lab is the lab of the same steps run straight
  through (its digest); let go, the next frame runs at most two frames' worth, then every step asked; a
  budget that holds them changes nothing; a step that costs more than the budget is still run, one a frame.
- **A part's reading leaves out the shape searches** (`item_view(item, shaped)`, `reading`): a monitor or a
  controller read a quantity from a whole view of its item, which searched how a stirred vessel's contents
  lie (a vortex: a dozen searches) at every step, for every part. The same value, without them.
- **A sediment's lie apart from the liquid's** (`contents::settled`): its flat section and pile depend on the
  vessel, its turn and the solids' volume alone, and were searched again for every vortex size tried; kept,
  the last four, and found from a table of 32 peaks a turn by false position (where halving from the whole
  height took forty searches of a thousand sections).
- **A hand pour's lie anchored on its volumes** (`by_hand`): what lies in a poured vessel is worked out for
  the liquid to the nearest 256th of the vessel's capacity, the sediment to the nearest 1024th and the
  vortex to the nearest 256th of its share; between, the surface stands over it by what is more over the
  surface's area, and what is more of either runs. The last sixteenth of a capacity (and a sixteenth of
  that of sediment) exactly: where little is left the anchor would be as much again as lies there (the
  tests of a pour run to its end caught it). A pure function of the contents: whatever came before.
- **The vortex's share** by the secant (`contents::swirl`, three or four searches for a dozen); a vessel's
  share kept until its energy has changed by a hundredth (it was a ten-thousandth), going between as the cube
  root of the energy. Neither is in a session's record: the vortex's share is the picture's and a hand pour's.
- **`contents::below`** keeps its last four answers (a pour asks for the lip's and the surface's in turn).
- **The host sends a scene's buffers by their changed pages** (`OpticsCompute::hold`: pages of a thousand
  floats compared with what the buffer holds).
- The cost play goes on at 600× after the stir (its mark `fast`, 360 frames).

The session records are unchanged (`make sessions-check`, digest for digest).

## Measured (`chem_tool study speed`, M3 Pro; ms a frame of a 120 Hz display, the mean and the worst)

| Scene | 600×, before | 600×, after | 600× under 3 ms | Speed kept |
|---|---|---|---|---|
| The bench still | 0.70 (0.84) | 0.73 (0.99) | 0.73 | 600× |
| Malachite decomposing | 0.93 (1.18) | 0.97 (1.24) | 0.98 | 600× |
| The oxide dissolving, stirred | 2.14 (8.58) | 1.99 (3.58) | 1.96 | 600× |
| The same, three monitors and a controller | 11.06 (1,214) | 3.40 (4.29) | 3.11 | 553× |
| Boiling into the condenser | 2.00 (2.59) | 2.16 (7.42) | 2.11 | 600× |
| A bottle of acid poured by hand | 0.73 (0.89) | 0.77 (0.88) | 0.78 | 600× |
| Water and sediment poured by hand | 2.80 (3.06) | 0.55 (0.94) | 0.55 | 600× |
| The same, stirred | 139.9 (9,782) | 4.74 (458) | 4.65 (18.3) | 600× over its frames |

At 1× every scene's frame is under 1.1 ms (the stirred pour was 16.8). A controller's trial (when it is
set, and when what it controls has changed by a tenth) was 1.2 s inside one step; it is 54 ms.

`make pass-cost` (the Godot host, the window): a frame's median 8.3 ms in every mark, 600× included
(`fast`: the worst 12.2); a frame in which every history starts again 12.5 ms (22 before the host sent
only the changed pages).

## The GPU's step frames, and the spread that was taken out (36.4)

At 1× the lab steps in one frame of a 120 Hz display in two. In the play's stir the frames alternate, 12
to 15 ms and 4 to 6 (`LAB_PLAY_SERIES=1 make pass-cost`). The remedy decided with the user was to spread
a step's pixels over two frames: a pixel whose content changed by the lab's steps alone (nothing in hand,
no gesture) took the change on the frame its place's parity said. Built, and in the reference it did what
it was for: over the flask's rectangle the step frames and the frames between came to the same rays and
points (65k and 55k rays before; level after). On the GPU it changed nothing: the same alternation, the
same worst frames with it on and off, twice each (stir's worst 14.4 and 13.7 off, 14.6 and 14.6 on; 600×
12.2 off, 12.3 and 13.3 on). Putting off every changed pixel, not half, changed nothing either. So the
step frames' cost is not the pixels' work, and the spread was taken out (with it an exception O12 would
have needed: a record a change old in a pixel put off).

What it is: not the scene's upload alone (sending only changed pages took the stir's worst from 21 to
14.6 ms and the restart from 22 to 12.5, and the alternation stayed); not the CPU (the game's frame, the
scene, the pass sent and the list painted are 0 to 1 ms each). The device's timestamps
(`capture_timestamp`) came back empty on this backend, so the pass's own time a frame is not known. The
sum of two frames is two periods of the display: the throughput is the display's, the frames uneven.

## Open

- **The GPU's alternating frames at 1× while a stirred vessel turns** (above): unexplained. To find it: a
  GPU time for each pass (a way the Metal backend gives, or the slope method of TDD 20 with extra passes),
  the stir's frames with the lab paused, and with the vortex held still.
- **A controller's trial** is 54 ms inside one step (two copies of the lab run up to an hour each). A
  frame in which a controller tunes again is that long at any speed. Ways: the trial's copies run beside
  the clock and their gains used from a fixed later step (deterministic: the copies start from the state of
  that step; the copies' own caches must be safe on another thread: `contents::full_tilt`'s map is static);
  or a shorter trial (it stops when the response is fitted).
- **The stirred hand pour's single steps** reach 18 ms while it pours (the budget cannot split a step):
  the vortex's share worked out again when the contents' signature moves (`set_stirring`'s `holds`), each
  a few searches of the liquid over a sediment that changes at every step. A table of the share by volume,
  as `plain_pour`'s, would make it a lookup.
- **Under the tutorial** the study's scene never steps (the tutorial waits for a gesture); a scene that
  plays the tutorial's gestures would measure its chunks of a second.
