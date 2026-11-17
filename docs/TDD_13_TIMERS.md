# TDD 13: Timers (Stage 13)

## The request

> Add a feature for the player to specify conditional timers, which track a quantity in any game
> element, then once the quantity fulfills some condition (GT, LT, equals, between, etc) the game
> is paused and the game element with the quantity is highlighted for the player to notice. These
> timers will be critical for gameplay, because many times the player would want to react when
> specific things happen, but still have speedups while they wait.

> Timers should have an optional maximum time as a second condition. In fact, timers should have
> only the time limit on by default, but multiple smart value conditions is added to the base time
> condition, and when there's at least one value condition - the time limit condition may be
> turned off.

The user's choices: values typed with the keyboard, with nudges beside them; one-shot timers that
can be re-armed. GAME_SPEC §13.3.

| Phase | Topic | Status |
|---|---|---|
| 13.1 | §13.3: the timer, its quantities and comparisons, firing, the editor, the list, keys | Done |
| 13.2 | `lab/conditions.hpp`: a quantity measured from the lab's view, comparisons | Done |
| 13.3 | `lab/timers.hpp`: arm, re-arm, remove, read every quarter lab-second | Done |
| 13.4 | The screen: selection, the editor (typed values, nudges, the clock paused while it is open), the list, firing (pause, outline, the readout), the clock read in quarters | Done |
| 13.5 | `cg_ui_key`, `cg_ui_timers`; the Godot host forwards the engine's keys | Done |
| 13.6 | The tutorial teaches a timer at the boil-down; checks on the selection, the editor and the timers; typed gestures | Done |
| 13.7 | The user plays it (`make run`) and signs off | Pending |

## Decisions

- **Timers belong to the screen, not the lab.** They only read the lab's view and pause the clock,
  so the lab, the sessions and their golden records are unchanged. A lab paused by a timer is the
  same, digest for digest, as one advanced the same number of steps without one (tested).
- **Read every quarter of a lab-second** (15 steps), at every speed. Reading the view costs 3.7 µs
  (Apple M3 Pro); at 600x a frame is 600 steps (0.91 ms) and 40 readings (0.15 ms). No lighter
  reader was needed.
- **Up to three value conditions, five timers**: what the editor and the list show without
  scrolling. Any or all for the value conditions; the time limit fires on its own, whichever comes
  first. A time limit can be switched off only while there is a value condition.
- **"Reaches"** holds when the value lies between two readings, ends included: a crossing from
  either side, or a touch.
- **The clock is paused while the editor is open.** It was found by the person-paced test. At
  600x, a person writing a timer with the burner still on low boiled the flask down to 1.5 g
  before the timer was armed. Arming or cancelling plays the clock on if it was playing.
- **A click on an item without moving it selects it.** The tutorial's guard now judges a drag
  when it is let go, not when it is taken, so a selection is never refused.

## Tests

`native/tests/game/test_timers.cpp`:

- Quantities measured from a view built by hand, and every comparison, "reaches" from both sides.
- Any and all; the time limit alone and with conditions, whichever first; one-shot; re-armed (its
  time limit from then); removed; what cannot be armed (no limit and no condition, a value that is
  not a number, between's ends reversed, an impossible time, a sixth timer).
- Written with the pointer and the keys:
  - typing (only what a number is made of), backspace, nudges, enter and escape;
  - the clock paused while the editor is open;
  - the flask's temperature above 50.9 °C fires at a quarter boundary;
  - the same lab without a timer had not crossed it a quarter earlier, and is the same digest
    for digest;
  - re-armed while already true, it fires at its first reading; a time limit alone fires at
    exactly its time.
- **A person boiling down at 600x:** with a timer on the flask's water below 10 g, the flame goes
  out at 9.965 g. Without one, the same person stops at 0.17 g.

`test_tutorial.cpp`: both playthroughs (exact, and at a person's pace) complete with the timer
steps: 34 steps, 37 gestures. The C smoke test arms a timer through the C API and the keys; the
Godot suite types it with the engine's own key events, and it fires and pauses the clock.
`make screenshots` adds the editor; O4 unchanged at every moment (worst 2, none visible).
