# TDD 18: The debug panel, with reactions that explain themselves (Stage 18)

## The request

> I like your idea of explainable reactions, let's do that as well. Also, let's tackle the
> remaining work you mentioned.

The plan's Stage 18: the debug panel (the remaining work), with each reaction explaining itself.
GAME_SPEC §13.6.

| Phase | Topic | Status |
|---|---|---|
| 18.1 | The spec: §13.6 | Done |
| 18.2 | The core: the last step's flows by process (`Simulation::flows`), the ledger's sums, traced or not | Done |
| 18.3 | The data: each reaction's `explain`, embedded by `chem_datagen` beside its notice | Done |
| 18.4 | The lab: `Lab::debug`, an item's composition, flows and reactions, and what limits each | Done |
| 18.5 | The screen, the C API and the host: the panel, a single step, the export to `user://saves/` | Done |
| 18.6 | The user plays it (`make run`) and signs off: "Approved, proceed" (2026-11-18) | Done |

## Decisions

- **The flows are the core's.**
  - They are counted in `Simulation::transfer` and `transform`, the one place each step's
    primitives are applied, under the process being run. That makes them the ledger's sums by
    construction, and the test proves it step by step.
  - `advance()` keeps them as `step()` does: the screen runs unchecked.
  - They cost nothing measurable: 4.40 us a step against 4.42 before (`make demo-bench`).
- **What limits a reaction is worked out, not written.** The data says why a reaction happens
  (`explain`). The lab says what holds it back now, from its kinetics and the state, by the first
  rule that applies (§13.6). The rules:
  - **Too cold:** the first-order law's time constant 1/(k |f|) is over a minute. "Fast from" is
    the temperature where k = 1/60 s: Ea / (R ln(60 A)). For malachite that is 356 °C, which is
    why the demo's last 2 mg, drowned in the liquid at 21 °C, never decomposed (picture 13).
  - **At equilibrium:** the affinity is under a thousandth of RT, about 2.4 J/mol at room
    temperature. Crystals still growing a micromole a second there say so.
  - **Used up:** a reactant it would consume is gone, named even when the affinity points the
    other way, or cannot be weighed (no liquid for a solute): "no H2SO4(aq) left".
- **Every reaction that names what the item holds** is listed, those without a rate law too: the
  dehydrations, which only Stage 19's solutions will run, say they do not run on their own.
- **A single step is the clock's.** It pauses the clock and runs one step, checked against every
  invariant. Under a tutorial it waits as the clock does while a gesture is asked for.
- **The export is the save.** The screen hands it over once (`take_export`), and the Godot host
  writes it to `user://saves/lab-<ms>.json`. A loaded export continues digest for digest (D2).

## Tests

- **`test_flows.cpp`** (core):
  - Over 400 s of malachite heated into a balloon, checked every step: each edge's energy and mass,
    summed over the processes, equal the ledger's.
  - Heat conduction carries no matter, and runs from the hotter end to the colder (with T1 checked
    every step).
  - The decomposition's extent is counted, and the gas made is pushed into the balloon by the
    settling.
  - The same flows whether stepped traced or not.
- **`test_debug.cpp`:**
  - **Composition:** amounts and masses; the gas's mole fractions sum to 1; a solute's molality and
    activity; a solid's fraction and activity of 1; no wall.
  - **Limits:**
    - malachite too cold, fast from about 35x °C; hot, fast with its half-life and a rate; with
      heat in from the burner, out to the room, and gas out to the air; at the last quantum, used
      up;
    - copper oxide without acid ("no H2SO4(aq) left"); with it, limited by its surface and asking
      to be stirred; stirred;
    - a supersaturated solution with no crystals yet, then growing; water held at its equilibrium;
    - dry crystals: a dehydration with no rate law, the calcination too cold, fast from about
      16x °C.
  - **Checking:** the same flows with checking on and off.
  - **The screen:**
    - the panel asks for a selection, then shows the flask's reaction and limit;
    - a single step is one step, the clock paused;
    - scrolling;
    - the export is handed over once, and loads to the same digest, the same again 600 steps on;
    - under the tutorial, a step waits while a gesture is asked for.
- **GUT:** the debug panel's export written to `user://saves/` as a `chem-save/1`.
- **`make sessions-check`:** unchanged.
- **`make screenshots`:** the panel on the crystallising flask (picture 13).
