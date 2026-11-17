# TDD 16: Notices in levels (Stage 16)

## The request

> About your suggestion of better failure messages and generalized lab notices - I like it,
> perhaps they should have levels, like logs, maybe DETAIL/EVENT/WARNING/DANGER or something like
> that? and the player can silence any level they want.

GAME_SPEC §13.4; the notice part, §13.3.

| Phase | Topic | Status |
|---|---|---|
| 16.1 | The core's tally of each rate-law reaction in each cell; each reaction's notice from its data | Done |
| 16.2 | The lab's notices: the detectors, read every quarter lab-second; what happens at once; saved and loaded, not digested | Done |
| 16.3 | The notice part: a level and an item; it signals for a step on a notice it hears | Done |
| 16.4 | The screen: the newest beside the clock, the panel and its switches, the notice part's editor; the drawer of four parts | Done |
| 16.5 | The user plays it (`make run`) and signs off | Pending |

## Decisions

- **Notices are edges, not states.**
  - A detector writes when what it reads changed since its last reading, a quarter of a
    lab-second ago.
  - Reactions have hysteresis: on above a ten-millionth of a mole a reading, off at none.
  - So does a balloon's fill: 80 % on, 70 % off.
  - The detectors read every quarter lab-second, like the screen's timers did, at no measurable
    cost: a 600× frame is 1.27 ms with or without them.
- **What a hand moved is not news.**
  - A pour or a tip marks both items for the next reading.
  - The demo writes no "H2SO4(aq) appeared" or "Cu2CO3(OH)2(s) appeared": the malachite breaking
    down and the copper oxide dissolving are what it notices.
- **A reaction's level and text are data** (`data/reactions.json`, `notice`; embedded by
  `chem_datagen` as `builtin_reaction_notes()`), so a new reaction says what it means without
  code.
- **The core counts how far each reaction ran** in each cell (`Simulation::tally`), at the one
  place reactions are applied. The lab clears it at each reading. It is history: not state, not
  digested, though a save holds it so a loaded lab's next reading is the same.
- **History, not state.** Notices are saved and loaded, so they continue the same; they are not in
  the digest. The sessions' records are unchanged.
- **No special "pause on warnings" switch.** The notice part, wired to an alarm, does that, as the
  plan proposed and the signals generalise.
- **Details start silenced.** They come often: a rig acting, a reaction stopping. Every level
  stays in the panel's list once its switch is on again.
- **`LabView::events`** stays the bursts it was. Notices are a list of their own, so nothing that
  read events changes.

## Tests

- **`test_notices.cpp`:**
  - **The demo session as the lab tells it:**
    - each thing once, at its level: the malachite breaking down; the oxide appearing and used up;
      boiling, and stopping; crystals growing, and appearing;
    - what was tipped and poured not noticed;
    - no warning, no danger;
    - in order; in the report.
  - **Warnings and dangers:** a small balloon over heated malachite, 80 % full and then burst; a
    flask with 5 mL of acid boiled dry; a cup brim-full under a condenser spilling into the tray.
  - **Signals:**
    - a timer at zero and the rig it set off, as details;
    - a notice part for warnings does not hear details, and is refused a level that is not one or
      an item that is not there;
    - for events about the flask, it rings an alarm when the malachite breaks down, and "alarm1
      rang" is written in that step.
  - **Saved mid-reading and loaded:** the same notices, and digests, for five more minutes.
  - **The screen:**
    - the newest heard beside the clock;
    - the panel, details off at first;
    - an event silenced, kept, and shown again;
    - a notice part out of the drawer, set to danger about the flask.
- **`make sessions-check`:** unchanged.
- **`make screenshots`:** the panel after the demo (picture 10); O4 unchanged.
