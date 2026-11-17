# TDD 17: Goals on the outbox (Stage 17)

## The request

> I don't like your idea of judging … the player sets "goals", which are places on the outbox
> which are reserved for specific target materials and purities … The game itself will have
> intermediate goals

GAME_SPEC §13.5; the commands, §13; the tutorial's keys, §13.2.

| Phase | Topic | Status |
|---|---|---|
| 17.1 | The spec: §13.5, the commands `goal`, `ungoal`, `submit` to a slot | Done |
| 17.2 | The lab: six slots; goals from the bench (locked) and the player's; met live; notices; a monitor's `met`; saved and loaded, not digested | Done |
| 17.3 | Conditions: `goal`, `goals_met`, an item's `slot` | Done |
| 17.4 | The screen: the slots with their goals, terms and values now; a drop onto a slot; the goal editor; a notice part may name a goal | Done |
| 17.5 | The tutorial: each hand-in onto its goal's slot; its goal, the game goals met | Done |
| 17.6 | The user plays it (`make run`) and signs off | Pending |

## Decisions

- **A goal is a state, not a verdict.**
  - It is met while its slot holds an item and every term holds of it, measured live from the
    lab's view: what is handed in still cools and reacts.
  - Nothing is scored, and no term can be `reaches`, an event.
- **Terms are §13.3's quantities without an item.**
  - The goal names a slot, not a thing: whatever is handed in there is measured.
  - So a goal is written as a monitor is (a species' mass, purity, count or size; the
    temperature, the liquid, the gas), and the editor is built from the monitor's.
- **Slots are numbered 1 to 6**, as the screen shows them.
- **A submit without a slot keeps the scripts.**
  - It takes the first free slot without a goal, else the first free one; the sessions' records
    are unchanged.
  - On the screen a drop onto the outbox always lands on a slot. `Ui::locate("outbox")` names the
    slot a submit with none would take, so existing gestures still hand in.
- **The game's goals are the bench's** (`"goals"` in `data/benches/lab.json`), locked. They carry
  §17's "done when", which the tutorial's goal checked by hand before.
  - `crystals`: CuSO₄·5H₂O at least 5 g, and at least 99.9 % of the solids and liquids. The demo
    makes 8.9 g at 99.977 %.
  - `water`: at least 20 g, and at least 99.99 % of the solids and liquids. The demo makes 40.9 g of
    nothing but water.
  - `co2`: at least 0.5 g. The demo makes 0.90 g.
- **The tutorial's hand-in failures stay.** The plan said they would stay only where no goal
  covers them. But nothing handed in can be taken back, so a wrong hand-in is a dead end the
  failure names, where a goal could only stay unmet.
- **Goals are history, not state**, like the notices.
  - They are saved and loaded, with which slot holds what.
  - They are not in the digest: a play with them digests as the same play without them.
- **Read by everything else.**
  - A goal becoming met is an event and one no longer met a warning, both about the goal's id, so
    a notice part can hear one goal.
  - A monitor reads `{"item": <goal id>, "what": "met"}`. On the screen, the monitor's editor lists
    equipment; a goal is heard by a notice part, whose editor lists goals.

## Tests

- **`test_goals.cpp`:**
  - **Live:** a game goal of warm water, met when the hot cup is handed in to its slot (an event
    at once). After a cold cup goes to the first free slot without a goal, the goal is no longer
    met as the cup cools (a warning); `goal` and `goals_met` follow it.
  - **The player's goals:** made (`goal1`), changed (the same id), taken off, and the next is
    `goal2`. The game's are locked.
  - **Refusals:** a slot out of range, no name, no terms, `reaches`, an unknown species, `met` as a
    term, an unknown measure, `between` with one value, a bench goal on slot 0.
  - **Handing in:** one item to a slot; with no slot, the free slots without a goal first, then the
    rest; a full outbox refuses.
  - **Reading goals:** a monitor reading `met` and a notice part hearing the goal ring their alarms.
    Saved and loaded: the same goals, slots, notices and digests. A play with goals digests as the
    same play without them, command for command.
  - **The screen:** a game goal's name in its slot, no editor for it; a goal of two terms made in
    the editor, a term taken off, the goal taken off; a drop onto slot 5; a drop onto the outbox
    to the first free slot without a goal; a notice part set to hear `crystals`.
- **`test_tutorial.cpp`:** a drop of the balloon onto the outbox is refused (not the slot the step
  asks for), onto slot 1 taken. The tutorial played by its gestures, and at a person's pace,
  completes on the game goals.
- **`make sessions-check`:** unchanged.
- **`make screenshots`:** the outbox's slots (pictures 4, 5, 10), the goal editor (picture 12).
