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
| 88.5 | Invariants: a vessel at one pressure, work between cells, the state function's self-test | |
| 88.6 | Goldens (evidence, the user's approval), docs, `make gates`, the user's sign-off | |

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

- **A sealed vessel's layers boil by the room's pressure** (until 88.4; so since Stages 28 and 47): a layer is an
  open cell at 1 atm whatever its vessel's pressure, and boils at 100 °C in a flask at 2 atm. Benign so far (a hot
  layer rises, and the top boils where its pressure says); wrong for a pressure cooker.

