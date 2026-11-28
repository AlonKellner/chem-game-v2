# TDD 88: Pressure (Stage 88)

## The request

The user, after Stage 79's push and pull (2026-11-27): "Couldn't there be some generalized form which follows the
correct behavior while also being physically realistic and consistent with the rest of the simulation mechanics?";
then: "Plan the proper mechanisms required to solve connected vessel compression, liquid and gas transfer, and
related phenomenon in a correct and consistent manner which will make realistic and theoretical sense." The plan
(approved 2026-11-27; summarised in `docs/TDD_79_SYRINGE.md`'s Open) is two stages: this one, the equation of state
and a vessel's one pressure; Stage 89, a conduit's flow law and one implicit solve of joined vessels' pressures.

## What was found (the kernel read again, Stage 79's assumptions not taken for granted)

1. **The equation of state had holes.** `Sealed` (`native/core/src/thermo.cpp`) took liquids as incompressible: a
   sealed cell full of liquid had no pressure, nor a pocket of gas squeezed to nothing. Stage 79's clearance and its
   reading of "a state past the model" stood in for that.
2. **A sealed vessel's layers are at 1 atm**, only its top at its pressure (`strata.cpp`): liquid moved between a
   layer and a pressurised top, and the top made the size its layers leave, book no pressure work. The world's
   entropy hides it wherever heat flows and shows it wherever little does (a syringe).
3. **What leaves a sealed cell carried its enthalpy at 1 atm**, the flow work `v p` added by hand for liquids, 6 %
   off the enthalpy at the giver's own pressure for water at 20 °C.

## The design (GAME_SPEC §6, Liquids that give)

- **One potential, defined everywhere**: `G = G0(T) + V_s p + V_l(T) (p - κ(T) p²/2)`, `p = P - P_atm`. Volume,
  energy, entropy and every chemical potential from it, as Stage 67 did for expansion. The pressure at a temperature
  is a quadratic's one root that is not negative: gas over liquid, liquid alone, or nought with a void (which
  boiling fills: cavitation).
- **Enthalpy at the giver's state**: what crosses carries `h(T, P)`.
- **One pressure for a vessel**: a sealed vessel's layers are cells at its pressure, each taking the volume it
  needs there (isobaric, as an open vessel's layers are at the room's): a layer's energy and what it has taken of its
  vessel's volume give its temperature at that pressure, its fast equilibria run at it (water boils in a layer where
  the vessel's pressure says), and each step the vessel is sized: the pressure at which its top holds what its layers
  leave, the work `(P - P_atm) dV` of each layer's change of volume booked between it and the top.
  (As planned, layers were to be rigid sealed cells sized to the vessel's pressure. Read against the column's code,
  that fails: a layer's boiling is solved at its volume, and a rigid layer full of liquid has no room to give, some
  0.2 uL of steam a round. A layer is at a pressure, not at a volume.)

Open cells are at `p = 0`: every new term nought, their states unchanged to the bit.

## Phases

| Phase | Topic | Status |
|---|---|---|
| 88.1 | Spec (§6) and data: water's compressibility (Kell 1975), its row verified | Done |
| 88.2 | The compressible sealed model: `Sealed`, `fill_of`, `entropy`, `affinity`, `AffinityProbe::at`, V2, V3 | Done |
| 88.3 | Enthalpy at the giver's state (`enthalpy_over`); Stage 79's joins use it | Done |
| 88.4 | A vessel's one pressure: its layers at it (isobaric), the vessel sized each step, work between its cells | Done |
| 88.5 | Invariants: a vessel at one pressure (P2), work between cells (W2), their self-tests; a layer's cushion; what moves in a vessel with its volume; hands at random on sealed layered syringes | Done |
| 88.6 | Goldens (evidence, the user's approval), docs, `make gates`, the user's sign-off | The `pressure` session re-recorded with approval; docs done; `make gates` passed (2026-11-28: core, ASan and UBSan, ctest, the Godot suite, the goldens unchanged; the step's cost 1,265,059 instructions on the demo, +1.2 % against main's at 722b8cd); the user's sign-off to come |

## Tests

- `native/tests/test_measured.cpp`: `measured: water's compressibility is Kell's, at Jones and Harris's value and
  within their fit (Stage 88)`: 46.5 ppm an atmosphere at 20 °C, their eq. 7 within 2e-4 from 5 to 40 °C, its least
  near 46.5 °C, its ends, its slopes against centred differences.
- `native/tests/test_give.cpp` (the core):
  - a sealed vessel full of water has a pressure: the room's where it just fits; a millionth squeezed, 1e-6 over
    its compressibility at its entropy (the isothermal one over Cp/Cv, which the model gives as water's 1.0066 at
    20 °C); a void at no pressure with more room than it takes;
  - brim-full water warmed a kelvin by a bath, every invariant on every step: `P_atm + (1 − V/V_l(T))/κ(T)` to
    1e-5, 4.6 bar at 20 °C and 10.4 at 50 °C;
  - the pressure falling without a break as the volume grows, from water squeezed alone through a pocket of air to
    air over water; the pocket at the water's own volume at 5.3 bar, the quadratic's root;
  - one potential: `dE = T dS` at its volume, `dE = −(P − P_atm) dV` at its entropy, its heat capacity its energy's
    slope, for water at 2, 33 and 300 bar, a pocket, and air over water;
  - water with more room than it takes and no gas: no pressure, then boiled into the room, 3.16 kPa at 25 °C.
  - what crosses at the giver's state (`enthalpy_over`): a mole of water at a bar takes `v p` at 4 °C, 6 % less at
    20 °C, 15 % less at 50 °C; and it is the slope of its cell's own enthalpy `E + p V` by its water at that
    temperature and pressure (3.3679 J/mol at 3 bar, 49.0768 at 30 bar, under air or alone: the two to the digit).
  - a sealed vessel's layer at its pressure (88.4's model, pure): the temperature, entropy, volume and boiling's
    affinity of a sealed cell of the same contents at that pressure; `dS = dQ / T` warmed at its pressure; at the
    room's pressure an open cell's state to the bit; water in it boiling from 120 °C at 2 atm and from 69 °C at
    0.3 atm.
  - a sealed flask as a column, warmed on a bath at 95 °C for four minutes, every invariant on every step: its
    layers marked with its seal, each at its top's pressure within 1e-3 as a step ends, its cells' volumes the whole
    to 1e-12;
  - the same in a bath at 150 °C for ten minutes: 5.97 bar at 149.5 °C, its layers' water at up to 149.5 °C, 9.6 of
    its 11 mol still liquid in its layers and 0.006 mol as steam: a layer holds liquid past 100 °C where its
    vessel's pressure says it may.
  - P2, W2 and V3's layer mark, each firing on a corrupted trace and none on a clean one.
- `native/tests/game/test_pressure_lab.cpp`: a stoppered flask of two layered solutions warmed to 128 kPa, every
  invariant on every step, saved and loaded digest for digest.
- `native/tests/game/test_syringe_lab.cpp`: hands jerked at random (to an end or anywhere, let go, locked) on a
  capped syringe of two layered solutions, under 30 mL of air and brim-full, 1,200 steps each, every invariant on
  every step (twelve seeds of 2,500 steps one-off).
- Stage 67's own (`test_expansion.cpp`: one potential as it is heated, the affinity the entropy's slope) pass on the
  new model unchanged.

## Found

- **The model gives water's Cp/Cv.** A sealed cell of water squeezed at fixed energy stands 0.66 % over the
  isothermal figure: its compressibility at constant entropy, `κ_T (1 − T α² V / (κ_T Cp))`, from the one potential
  with nothing added (1.0066 at 20 °C, as measured for water).
- **Only one record moved.** Of 952 entries only the `pressure` session's golden failed, by its digests alone: its
  printed results (times, steps, the stopper at 138 kPa, the bottle at 401 kPa and 130 °C) are main's line for line,
  its six digests with the flask open unchanged. Re-recorded with the user's approval.
- **The flow work by hand was no entropy error between cells at one temperature.** Planning this stage I took
  Stage 79's `v p` (6 % over the enthalpy the pressure adds, at 20 °C) for a first-order fall of entropy. It is not:
  energy carried between two cells at one temperature leaves their entropy as it was to the first order, whatever
  the energy. What the exact enthalpy sets right is where the heat ends (the giver's and the taker's temperatures),
  and the entropy where they differ.
- **A layer's boiling at its vessel's pressure had no limit but its own heat** (found by hands jerked at random on
  a capped syringe of two layered solutions with no air: T2, the world's entropy down 1.5e-4 J/K in a step). A layer
  a little over saturation in a vessel with a microlitre of vapour space made half a millimole of steam in a round
  (a litre of it, at that pressure, in a 50 mL vessel); the next sizing found 76 bar, the steam condensed in the
  warmer top, and heat had been carried from the colder layers to it. So wherever a vessel's gas space cannot take
  what its layers would boil. A layer now feels its vessel as it grows: its **cushion**, its share of the vessel's
  free volume as last sized, and the pressure `P0 (c / (c - dV))^(5/3)` grown by `dV`: stiffer than any gas's own
  compression both ways, so the work it books is on the second law's side; a large cushion is a layer at a pressure,
  a void is free until it is filled and then a wall, none is a rigid cell (which is what the plan first meant a
  layer to be).
- **With a cushion, what moves inside a vessel must take its volume with it**: a layer that gave half its liquid at
  the volume it had taken stood, by its cushion, as if it had shrunk by half, and its pressure fell to a fifth. A
  move between two of a vessel's cells now shifts the giver's taken volume to the taker and carries the energy in
  that volume, `h - p v`.
- **Stage 28's "cells sealed together" took a column's layers for members of the sealed group**, and ran their
  equilibria and its gas levelling on them. A layer at a void's pressure flashed there (208 K). A column's edge is
  no gas path between sealed cells; a sealed vessel is sized before its layers' equilibria run.
- **A sealed bench vessel built with layers starts with its top the whole vessel's size** (its layers' volumes are
  taken from it only as the column first runs): a plunger's first push acted on a top that looked five times
  roomier than it was (1050 bar after it). A vessel is sized before its plunger is moved, too.
- **On main a capped syringe of two layered solutions, left alone, fails T2 at its first step**: sealed layered
  vessels were never right there (no bench has one). Here it runs two minutes clean.
- **The column's diffusion carried the energy of ions it did not move** (Stages 47 and 63; found at the 2480th step
  of one seed): what it asks to move is trimmed to what is neutral as it moves, and the energy of all of it went
  with the part; where nothing of it was neutral, alone, as heat between two layers whichever was the warmer (T1).
  Trimmed before its energy is reckoned.
- **T1 replays a step's ledger against each cell's state as the entry ran**, and read every cell at its volume and
  pressure as the step ended. A layer's pressure now moves several times in a step: a fast equilibrium that ran at
  one pressure was judged at another (a transform "against an affinity of 0.07 J/mol" in the first step), and heat
  that ran between two layers a microkelvin apart, the same way. A cell's resizing is now an entry of the ledger
  (`LedgerEntry::Kind::Resize`), and the replay reads each cell as it then was.
- **A layer's starting pressure was cleared from the trace before it was written** (mine, in the same hour): the
  first heat of a step was judged at the pressure the step ended with. Found by printing what conduction read.
- **The pressure session did not move**: its flask has no layers while it is stoppered (a boiled flask is mixed).
- **V3's self-test filled a sealed cell with water** to leave its gas no room: that state now has a pressure. It
  fills it with copper oxide; V2 reads a sealed cell's liquids at its own pressure.
- **The NIST reprint misprints the density's `b`** (Jones and Harris 1992, eq. 1: 16.897850e-3; Kell's, and the
  game's since Stage 67, 16.879850e-3, which the measured densities hold). Its compressibility (eq. 2) is checked
  the same way before use: against the paper's own stated value and its authors' independent fit.

## Open

- **Stage 79's pistons still keep their clearance** (`PISTON_CLEARANCE`, `plan_piston`'s reading of a state past the
  model): a sealed cell full of liquid has a pressure now, but a plunger's own solve does not use it yet, and a pair
  of syringes of pure liquid still stands. Stage 89's solve of joined vessels' pressures replaces that solve whole,
  the clearance with it.
- **A layer's heat capacity is its own at its pressure**, not against its cushion (a liquid's hardly differs: 0.7 %
  for water; a layer holding gas, more).
- **Solids give nothing** (about 1e-11 /Pa, a fiftieth of water's): a sealed cell of solids and gas with no room has
  no pressure still (V3).
- **An open syringe of layered liquids pushed home** (Stage 79's, with Stage 71's spill of a column; reached by no
  bench: nothing yet puts layered liquid in a syringe): its plunger's first steps left a layer holding liquid under
  emptied ones (L1; the same on main, at its 13th step). A column that spills now closes up (each layer under an
  emptied one moves up into its place), which holds L1; six steps on its top stands a hair over-full (V2), a move
  of the plunger too small to spill taken from the top's own volume. Stage 79's to finish, with the flow stage.
- **A sealed vessel's layers boil by the room's pressure** (until 88.4; so since Stages 28 and 47): a layer is an
  open cell at 1 atm whatever its vessel's pressure, and boils at 100 °C in a flask at 2 atm. Benign so far (a hot
  layer rises, and the top boils where its pressure says); wrong for a pressure cooker.

