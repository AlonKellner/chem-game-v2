# TDD 16: Notices in levels (Stage 16)

## The request

> About your suggestion of better failure messages and generalized lab notices - I like it,
> perhaps they should have levels, like logs, maybe DETAIL/EVENT/WARNING/DANGER or something like
> that? and the player can silence any level they want.

And, after playing it:

> Very good! Perhaps we could also have some part which creates a custom notice when it receives
> a signal. It's basically an event bus system if we have that

GAME_SPEC §13.4; the notice part and the writer, §13.3.

| Phase | Topic | Status |
|---|---|---|
| 16.1 | The core's tally of each rate-law reaction in each cell; each reaction's notice from its data | Done |
| 16.2 | The lab's notices: the detectors, read every quarter lab-second; what happens at once; saved and loaded, not digested | Done |
| 16.3 | The notice part: a level and an item; it signals for a step on a notice it hears | Done |
| 16.4 | The screen: the newest beside the clock, the panel and its switches, the notice part's editor; the drawer of four parts | Done |
| 16.5 | The writer: the player's own notice, at a level, about an item or itself, when its input turns on; the screen treats notice parts and writers as parts (it drew notice parts as equipment); the drawer of five | Done |
| 16.6 | The user plays it (`make run`) and signs off: "LGTM, approved, proceed" (2026-11-18) | Done |

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
- **The writer is the bus's other end.**
  - It writes on its input's rising edge, like an alarm rings: once while the input stays on.
  - It sends no signal. What hears it is a notice part, so a writer's notice and the lab's own
    are heard alike, and one more link (a step) is the bus's only cost.
  - It is about itself unless it names an item, so a notice part can hear one writer alone (a
    topic), or every warning whoever wrote it.
  - A notice part's and a writer's level are digested once a part exists: they decide what is
    signalled. A writer's words are not: they decide nothing.
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
  - **The writer:**
    - a timer at zero writes "the timer ran out" once, a warning about writer1, at the next step;
      a notice part hearing writer1 rings an alarm two steps later;
    - written again after the timer is reset and runs out; a writer with no input never writes;
      about the flask, in its own default words;
    - refused: a wire or a rig from it, words over 80 bytes, a level that is not one, an item not
      there; saved and loaded, the same parts and digest;
    - on the screen: out of the drawer, its words typed (a three-byte character erased by one
      backspace), no port; a notice part hearing it, with a port; a timer wired into it by a
      drag; edited again.
- **`make sessions-check`:** unchanged.
- **`make screenshots`:** the panel after the demo (picture 10); O4 unchanged.
