# TDD 80: The walls on a grid and a seeded search (Stage 80)

## The request

After Stage 75, the next items proposed were a cheaper search for the fast equilibria (2) and the lab's walls (3).
The user, 2026-11-27: "In the meanwhile you can start working on the other ideas, 2 and 3". Measured, what was left
in either needed the goldens re-recorded (the exact changes added up to 1 to 2 %); asked, the user chose both, and,
shown every printed result old against new, "Re-record". GAME_SPEC 0.80.0.

| Phase | Topic | Status |
|---|---|---|
| 80.1 | Measured on Stage 76's physics: where a step goes | Done |
| 80.2 | A vessel's walls from what they read on a grid | Done |
| 80.3 | An equilibrium's search seeded by a line | Done |
| 80.4 | The goldens re-recorded (with the user's approval), the evidence | Done |
| 80.5 | `make gates`; the user's sign-off | Open |

## 80.1 Where a step goes

`sample` on the demo's bench (49,532 samples, main at Stage 77): the fast equilibria's settling (`project`) 50 %, of
it the search (`reach`) 17 % and the joint solve 10 %; the lab's walls (`set_walls`) 15 %, of it each ring's loss to
the room 3.6 %, the contacts' convection into the air 2.5 %, the boiling floor 2 %.

- **Skipping a probe whose cell's matter has not moved** (81 % of the settles that reach the probes): ruled out. An
  equilibrium at its balance is within a quantum of it, its affinity's margin RT/n, about 1e-7 J/mol for a beaker's
  amounts; a step's temperature drift (about 1e-4 K) moves the affinity by about dH/T dT, 1e-2 J/mol. It moves.
- **The search**: 35.5 million calls, 78.2 million evaluations. 77.5 % find no move (one evaluation), 2.8 % one
  quantum, 11 % 2 to 1,000, 8.5 % more; those that move took 6.3 evaluations.

## 80.2 The walls on a grid

A vessel's walls are set each step from its rings' and cells' temperatures, its levels and what lies on its floor. The
vessel-wide key (Stage 53, Stage 75) held them while no cell changed. In the demo the vessels' matter moves every
step (a few quanta of vapour), so it seldom held.

Each ring's outside and each contact now read their inputs on a grid:
- temperatures to 0.01 K (`WALL_GRID_K`);
- levels to a micrometre (`WALL_GRID_CM`);
- the bed's grains' conductivity and the water's activity to 1e-6 (`WALL_GRID_ACTIVITY`);
- the pressure to 0.01 Pa (`WALL_GRID_PA`).

Each keeps what it last read (`Item::outside_read`, `Item::contact_read`, the floor's nucleation with it) and works
its conductance out again only once that has moved on the grid. The conductances are a function of the state alone,
so a lab saved and loaded sets what it would have (D2 holds). A band, a recompute only past 0.01 K since the last,
would have been memory a save does not hold. A convection's coefficient goes as dT^1/4: 0.01 K at 10 K is 0.025 %.

−5.2 %, less than the walls' 8 %: during the boil the gridded levels and the water's activity move often.

## 80.3 The seeded search

`reach` takes the affinity at no extent, the caller's. Once one quantum keeps its sign, the line through the two
says where the affinity crosses zero. That quantum and the next are asked: a pair across zero is the answer, in two
evaluations. Otherwise the existing secant and bisection go on from the narrowed bracket. −4.8 %.

Near the root a quantum's change of affinity (about 1e-7 J/mol) is under its rounding (about 1e-6), so another
search can stop a quantum away: the digests move.

## 80.4 The goldens

`chem_tool bench` on the demo: 1,226,593 to 1,103,724 instructions a step (−10.0 %).

What the sessions print, main against Stage 80:
- **The demo**:
  - the oxide gone (command 12) at 1584.6 s, not 1584.5 s;
  - the flask under 21 °C (command 19) at 6418.6 s, not 6418.8 s;
  - 820,751 steps, not 820,767;
  - the crystals 10.058506 g, not 10.058384 g (+0.12 mg);
  - the distillate 26.679208 g of water, not 26.679230 g;
  - nitrogen 0.312499 g, not 0.312500 g.
- **The pressure session**: 115,441 steps, not 115,440; the bottle bursts at 1796.9 s, not 1796.8 s.

Every other line is as it was. Every other test passes within its own tolerances (882 entries; only the two records
and the 20 that read the demo record's fixture failed before the re-record). `make sessions APPROVED=1`.

## Open

- What is left of a step: the probes' evaluations themselves (the activity model at each extent), the joint solve,
  conduction.
