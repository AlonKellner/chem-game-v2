# TDD 14: Circuits (Stage 14)

## The request

> A set of signal elements may be collected into a Circuit, which is a compact element, which
> contains the individual elements inside it, making complex Circuits still compact and non
> cluttered on screen.

> Normally, multi select should just allow you to drag multiple elements around in sync, but when
> some of the selected elements are signal based, the "Create Circuit" option should be available,
> and pressing it should collapse all selecte signal elements (Alarms, Timers, Monitors, Circuits
> etc) into the new Circuit, and the new Circuit should be placed on a non occupied spot.

GAME_SPEC §13.3 (Circuits).

| Phase | Topic | Status |
|---|---|---|
| 14.1 | Circuits in the lab: `circuit`, `uncircuit`; parts made and placed inside; moved, scrapped and saved with all they hold; nesting; the digest of the parts loose | Done |
| 14.2 | The screen: "create circuit" from the selection, a circuit's square and lamp, the open circuit's panel (made, moved, set, wired inside and out), "up", "close", "open" and "give back" | Done |
| 14.3 | The user plays it (`make run`) and signs off: "Approved, proceed" (2026-11-17) | Done |

## Decisions

- **A circuit is a part of a fourth kind.** It holds other parts by their `in`, and signals
  nothing itself. Evaluation does not look at circuits at all: wires and rigs name parts, so what
  is in a circuit signals exactly as it would out of it.
- **Its digest is the parts' loose.** A circuit, and which part is in which, are where things are
  kept, like where things stand. They are not hashed: tested by two labs, one with the parts
  collapsed, digest for digest over 600 steps, and a save of each.
- **The new circuit's square** is the free square nearest the parts' top-left, searched after they
  have left the bench, so it is usually the first part's own square.
- **Inside**, the parts keep their layout. The circuit's grid is their extent, at least 6 × 4, so
  there is room to add to it.
- **Giving back** puts the parts back as they lie: from the circuit's square, or the nearest place
  where all of them fit. Where none has room, it is refused with the reason. A circuit inside
  another is given back only once the outer one is.
- **Wires and rigs out of a circuit** are drawn from its square while it is closed. While it is
  open, they are drawn straight from the part in its panel to the bench. That is simpler to read
  than the numbered ports at the panel's edge first planned.
- **The panel** stands over the outbox where it fits, so the bench stays in sight to wire out to.
  A part dragged out of it stays in, with the reason. A part goes into a circuit only when the
  circuit is made, or out of the drawer into the open one.
- **"Open" is a readout button**, like "edit". A double click was planned too, but the screen has
  none yet.

## Tests

- **`test_signals.cpp`:**
  - collapsed: the circuit's square, its grid, the parts' layout inside;
  - the digest of the parts loose, over 600 steps;
  - an alarm in it rings through it, and a rig from it acts;
  - its square taken on the bench, the inner parts' not;
  - inside:
    - made and placed;
    - refused off its grid, and on a part;
    - moved only with it;
    - a circuit not wired, not rigged, not made from the drawer;
    - a part already in one not collapsed again;
  - saved and loaded; given back as the parts lay;
  - nested: the inner circuit given back only with the outer one first; no room to give back
    refused; scrapped with all it holds, wires too.
- **`test_ui.cpp`:**
  - made with the pointer:
    - parts out of the drawer;
    - a rectangle around two of them;
    - "create circuit";
  - the parts gone from the bench and the circuit there;
  - opened:
    - a part moved in its grid;
    - a monitor made out of the drawer into it;
    - a wire out of it to the bench;
    - a part dragged out refused;
  - closed: an alarm in it rings, and the message names the timer;
  - nested: the circuit and a monitor into another, opened, "up", closed;
  - given back from the readout, at both depths.
- **`make screenshots`:** the parts chosen, the circuit, and its open panel (pictures 7–9); O4
  unchanged (worst 2, none visible).
