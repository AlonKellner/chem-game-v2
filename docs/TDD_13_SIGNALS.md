# TDD 13: Signals (Stage 13)

## The request

The stage began as timers that belong to the screen:

> Add a feature for the player to specify conditional timers, which track a quantity in any game
> element, then once the quantity fulfills some condition (GT, LT, equals, between, etc) the game
> is paused and the game element with the quantity is highlighted for the player to notice. These
> timers will be critical for gameplay, because many times the player would want to react when
> specific things happen, but still have speedups while they wait.

Before it was signed off, the user asked for the timers to become a signal system (reworked on
the same branch; the screen's timers were never merged):

> separate timers into three separate concepts:
> - Timers: actually measure time and give out a signal when the time is due
> - Monitors: track a value and give out a signal when the value fulfills a condition
> - Alarms: if they receive a signal, they alert the player, highlighting any related monitors and
>   their target elements or timers or any other signal source
>
> All three of these should be physical elements placable on the grid. The signal system should be
> riggable into lab actions, like stirring, with fully configurable options [...]
> Signals may be rigged in parallel (OR) or in series (AND [...] if nothing is connected it's fine).
> Purity should be a trackable value like any other value.

**The user's answers while it was planned:**
- **The grid:**
  > The bench grid should be more granular, and equipment should occupy more than one square.
  > Timers, Alarms, Monitors, Circuits and whatever belong on the grid, wherever the player wants
  > them.
- **The selection:** by a dragged rectangle, or one by one with Cmd, Shift or Ctrl; deselection
  too.
- **Parts:** an unlimited drawer.
- **Loops are allowed:** "two timers to set each other off, creating a timed cycle".
- **Rigs are inputs to a control.**
  > multiple inputs could be configured into a single control, and each treats the "on", or "off",
  > or "change" as state changing events, which override the previous state [...] It's just
  > simpler to let the player override the value, as if it's just another control input source.
- **A timer's reset is a control too,** and it acts once on an event.

Circuits are Stage 14. GAME_SPEC §13, §13.1, §13.2, §13.3, §18 (G1, G2).

| Phase | Topic | Status |
|---|---|---|
| 13.1 | The tutorial's conditions in the shared module (`lab/conditions.hpp`, read by `src/conditions_json.cpp`); purity and a balloon's fill as quantities; any quantity compared in a condition | Done |
| 13.2 | The bench as a grid of squares (`chem-bench/2`, 28 × 15), footprints by kind and volume; the demo session's record reproduced unchanged | Done |
| 13.3 | The selection (click, Shift/Ctrl/Cmd, rectangle, Escape) and `move`: several things along the bench, all or none | Done |
| 13.4 | Signals in the lab (`lab/signals.hpp`): timers, monitors, alarms, wires, rigs; evaluated every step at once; saved, loaded, digested; G1 and G2 | Done |
| 13.5 | The screen: the drawer, parts and ports, wires and rigs drawn, the part editor (a monitor's, a timer's, a rig's), the readout's buttons, a ring | Done |
| 13.6 | `cg_ui_signals`; pointer modifiers through `cg_ui_pointer`; the Godot host forwards Shift, Ctrl, Meta | Done |
| 13.7 | The tutorial teaches a monitor rigged to the burner and wired to an alarm (35 steps); the screen's timers removed | Done |
| 13.8 | The user plays it (`make run`) and signs off | Pending |

## Decisions

- **Signals belong to the lab.**
  - Rigs act on the lab, so parts are its state: saved, loaded, and in the digest once a part
    exists.
  - Where things stand is not in the digest, so the grid of squares changed no record: the demo
    session, its bench and commands moved onto squares, reproduces its record digest for digest.
- **Synchronous, every step.**
  - Every part reads its inputs as they were at the end of the step before; a rig is an input too.
  - Any wiring is therefore well defined, loops included, and the order parts were made in
    changes nothing (tested by reversing it).
  - Each link takes one step: the two timers that set each other off (10 s, 5 s) cycle every 902
    steps, on for 302, exactly, for an hour.
- **Evaluated every step, not every quarter second.**
  - A monitor reads one item's view, not the whole lab's.
  - Three monitors cost 0.69 ms per frame at 600× (1.26 ms of physics without them, 1.94 ms
    with them; Apple M3 Pro).
- **Events set state; the last wins.**
  - A rig acts only when its source's signal changes. A rig made while its source is on waits for
    its next change.
  - Rigs acting in the same step act in the order they were made.
  - The player's `stir`, `valve` and `reset` are one more input. Nothing is unplugged.
  - The lab keeps each control's last setting and who made it. G1 checks the control is as that
    setting left it.
- **A timer counts whatever its inputs.** Its inputs gate only its signal (the user's series:
  "they need at least one input signal to be on for them to send an on signal").
- **The fine grid** is 40 px squares.
  - Footprints leave room for a vessel's stand over a burner, and for what starts on its mouth.
  - A `chem-bench/1` file still reads, every footprint one square, so the tests' small benches are
    unchanged.
  - A drop puts the square the item was held by under the pointer. For an item out of the
    cabinet, that square is the one its body's middle stands on.
- **`move`, not several `place`s:** a selection moves all of it or none, with the reason, in one
  command.
- **The screen's controls:**
  - The stir button moved to a vessel's foot, beside its name. At the footprint's top corner it
    floated far above the vessel, and over a condenser.
  - The part editor pauses the clock, as the timer editor did.
  - A tutorial restricts drags from the drawer and from ports, and the editor's "set". The
    editor's other buttons stay the player's.

## Tests

- **`test_signals.cpp`** (12 cases):
  - the timer to the step, latched, reset;
  - every comparison, and "reaches" as a crossing;
  - gating's truth tables (OR, AND, free);
  - a chain of five answers in five steps, whatever the order the parts were made in;
  - loops: the two-timer cycle over an hour, a timer resetting itself (a one-step pulse);
  - an alarm on its rising edge only, and an alarm with no input;
  - rigs:
    - on, off and change;
    - the player overriding;
    - the next event winning;
    - the newest rig winning in a step;
  - refusals, each with its reason;
  - parts made, wired, set, moved with equipment, scrapped with their wires and rigs; names never
    used again; no parts digests as none;
  - a monitor rigged to the valve closes the burner at 9.996 g, and the alarm stops `advance` in
    the step it rang;
  - save and load mid-countdown, digest for digest; `wait_until {rings}`;
  - G1 and G2 firing on corrupted states;
  - the cost of three monitors.
- **`test_lab.cpp`:**
  - footprints by kind and size;
  - overlap and off-grid refused;
  - set down beside;
  - a `chem-bench/1` file's footprints of one square;
  - footprints saved and loaded;
  - `move`: all or none, and the digest unchanged.
- **`test_ui.cpp`:**
  - the selection: a click, Shift, Ctrl, Cmd, a rectangle, Escape, a group carried frame by frame,
    refused together, stowed together;
  - parts out of the drawer:
    - every editor: typed, nudged, refused, cancelled;
    - wires, the choice on a timer, rigs of every form;
    - the readout taking them off;
    - parts moved and scrapped;
  - **a person boiling down at 600×:** the monitor closes the valve below 10 g, the alarm stops
    the clock and names the chain, the watched flask is selected, playing on answers it.
- **`test_tutorial.cpp`:**
  - both playthroughs, exact and at a person's pace, complete the 35 steps with the signals;
  - purity, fill and any quantity compared, with their refusals.
- **The C smoke test and the Godot suite:** a timer out of the drawer, typed with keys, wired to
  an alarm; it rings at 2 s and pauses the clock.
- **`make screenshots`:** O4 unchanged at every moment (worst 2, none visible). The monitor's
  editor is a picture of its own.
- **`make sessions-check`:** unchanged throughout.
