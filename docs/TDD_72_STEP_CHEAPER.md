# TDD 72: The step cheaper (Stage 72)

## The request

The user, 2026-11-27, signing off Stage 70: "Approved, merge and proceed". Exact speedups, which keep every digest,
were nearly used up. Asked between that and a change of numerics, the user chose "Secant, re-record goldens": the
boiling floor's surface found by a faster search to the same tolerance, with every golden re-recorded once the
evidence (every printed result as it was) was in hand. GAME_SPEC 0.72.0.

| Phase | Topic | Status |
|---|---|---|
| 72.1 | Measured: where a step's time goes (the demo, a person's play of solubility) | Done |
| 72.2 | A cell's temperature from the simulation's kept thermal state, bit for bit | Done |
| 72.3 | The floor's surface by ITP to the bisection's 1e-9 K | Done |
| 72.4 | The goldens re-recorded (with the user's approval), the evidence | Done |
| 72.5 | `make gates`; the user's sign-off | gates passed; sign-off open |

## 72.1 Where a step's time goes

`sample` on `chem_tool bench data/sessions/demo.json` (the first half, heating and boiling) and on experiments.61
(solubility at a person's pace):

| | demo | solubility, a person |
|---|---|---|
| the fast equilibria's probes (`AffinityProbe::at`, `reach`) | 27 % | 19 % |
| Pitzer's excess (`complete`) | 16 % | 15 % |
| `set_walls` (the lab's walls each step) | 26 % | 19 % |
| of it, the boiling floor's surface (`floor_through`) | 13 % | 6 % |
| `thermal_state` | 9 % | 11 % |
| the screen's view | under 1 % | 1 % |

Since Stage 68 a play is its simulation: the view is 1 %. Played alone, experiments.61 takes 16 s; inside a gate,
70 s (the machine shared by both builds' ctest and other worktrees' gates). The gate's length is the work it sums.

Tried and left: a cell's fast equilibria skipped while its state is as it was when they last moved nothing. It hit
16 % of the probes in the demo, those already cheap (a cell where none can move), and saved nothing measurable;
Stage 64 added the same rest (`rest_key_`), kept.

## 72.2 The kept thermal state for every reader

`Simulation::temperature(cell)` is `World::temperature`'s value from the simulation's kept thermal state (Stage 53's
key: energy, amounts, volume, sealed, rigid, reservoir), derived again only when the key changed. The columns'
levels read it six times a cell a step (`column_kelvin`: the step's own flags inside the step, the key before it),
and the lab's walls and its views between steps. The same value, bit for bit: every digest as it was. 2 % of the
demo's play.

## 72.3 The floor's surface by ITP

`bubbles::floor_through` finds the floor's inside surface, under half its glass, where what crosses the glass is what
the surface passes on. It did this by bisection to a bracket of 1e-9 K, 26 evaluations a call on average over the
demo. It now uses ITP (Oliveira and Takahashi 2020, ACM TOMS 47(1) 5: regula falsi's point, truncated towards the
middle and projected into a ball that shrinks as a bisection's would; k1 = 0.2 / width, k2 = 2, n0 = 1), to the
same bracket: 9.2 evaluations a call, the two ends among them.

Regula falsi alone (Illinois's, with a halving when the bracket did not halve in two) was tried first and failed
where the root is the onset: there the flux steps up as boiling starts, the surface pins at the step, and it took two
evaluations a halving. ITP takes never more than one step past the bisection's count.

The test (`test_bubbles.cpp`, Stage 72) covers 1,728 floors: bulk 5 to 99.9 C, the middle 1e-6 to 300 K over it,
the glass's resistance 1e-3 to 3 K/W, 1 atm and 20 kPa, water and an activity of 0.9, fresh and nucleated. In each,
the surface is a root to 1e-9 K. Where the root is one, it is the bisection's to 8e-10 K. In 8 of them it is not one:
the floor's convection steps down 7 % where Lienhard's thick boundary layer's form (8.37b, a Nusselt number under
10) hands over to the thin one's (8.37a), so two surfaces stand within the step. Either is the floor's, and the
bisection's was the one its halvings came to (GAME_SPEC §6 states it).

## 72.4 The goldens

Every digest from the first boil on changes (the demo from command 12, the pressure session from command 2). The
evidence, before re-recording:

- `chem_tool run` of both sessions printed every result identically, old against new: what was measured, the
  notices, the submitted vessels' composition, the conservation table (75 and 53 lines), on Stage 70's physics and
  again on Stage 64's after main was merged.
- Every other test passes unchanged (813 entries), D2 at every command of the demo among them.

`make sessions APPROVED=1`, twice: on the branch, then after Stage 64 was merged.

The cost, `/usr/bin/time -l` on the demo's bench, back to back:

| | instructions | cycles |
|---|---|---|
| Stage 70's physics, before / after | 6.56e11 / 6.38e11 (-2.8 %) | 1.57e11 / 1.49e11 (-5.2 %) |
| Stage 64's physics (main), before / after | 9.58e11 / 9.32e11 (-2.8 %) | 2.72e11 / 2.63e11 (-3 %; at a load of 16 to 23) |

The floor is a smaller part of Stage 64's heavier step (75 species: its probes and Pitzer's pairs grew).

## 72.5 The gates

`make gates` on Stage 64's physics passed every gate in 473 s, at a load of 23 to 31 (other worktrees' gates on the
machine; the Godot step alone 290 s). The release ctest summed 5,515 s, against 2,556 s on Stage 70's physics on a
quieter machine. Its longest entries: `chem_tests.column.2` 324 s, the demo record 256 s, ui.9 225 s, trash.2 161 s,
controller.4 160 s, D2's windows (lab.14) 119 to 153 s. A quiet machine's measure of Stage 64's gate is still to be
taken.

## Open

- The probes and Pitzer's excess are now most of a step: two evaluations an equilibrium a cell a step, each the
  activity model at its own extent. Fewer would be another search, with other numbers.
- `bubbles::root` (the boiling point and the onset) halves a fixed 60 times, to the double's last bit; ITP would
  take it in a few. It is a small part of a step (a boiling floor's two roots a step).
- The floor's convection steps at Nu = 10 (8.37b to 8.37a): a smooth hand-over would make the surface one again.
