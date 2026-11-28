# TDD 89: Flow (Stage 89)

## The request

The second of the two stages the user asked for after Stage 79 (2026-11-27: "Plan the proper mechanisms required to
solve connected vessel compression, liquid and gas transfer, and related phenomenon in a correct and consistent
manner which will make realistic and theoretical sense"; the plan approved that day, Stage 88 its first, signed off
2026-11-28: "Approved, merge and proceed"). The user's choice of flow law: **the realistic one** (water between
syringes at a real syringe's pace; a nozzle's size chosen by measurement).

## What was found

1. **The tube's law is wrong where it is used.** Stage 79's `Edge::flow` is Hagen and Poiseuille's, which holds for a
   long tube below a Reynolds number of about 2300. Water through a 2 mm nozzle at a thumb's 74 kPa runs at
   Re ≈ 14 000, its drop the entry's and the exit's, not the wall's: Poiseuille lets some 750 mL/s through, a real
   nozzle about 20. Air through it, slow, is laminar but **developing**: a nozzle five bores long is all entrance.
2. **Sub-steps make the asymmetries.** A scheme that moves one vessel before its neighbours answer is stiffer one way
   than the other ("pushing is harder than pulling"). Stage 79's hydraulic link solved one pair together; three
   vessels, or a pair with gas on one side, still show it.
3. **Stage 79's pistons keep a clearance**, though since Stage 88 every sealed state has a pressure: a pair of pure
   water still stands (`docs/TDD_88_PRESSURE.md`, Open).
4. **T1 cannot judge a simultaneous solve by replaying its ledger**: at an entry of a chain a → b → c the replay
   reads b before it has given to c. Stage 79 exempted gas "in a space a piston moved"; the right check is of the
   solve itself (F1).

## The design (GAME_SPEC §6, Flow between vessels)

### A conduit (`chem/conduit.hpp`, pure)

An edge between two vessels may have a **conduit**: up to four round pieces in series, each a bore and a length (a
nozzle, a tube, a nozzle). A mass rate `m` through it takes a drop

    ρ ΔP = Σ_k  m² / (2 A_k²) · [ 64 x⁺ + N12(a_k, b_k) + e_k ],   x⁺ = L / (D Re)

- `Re_k = m D_k / (A_k μ)`: no density in it, so the right side is the same for a liquid and a gas; a gas takes the
  density at the mean of the two pressures, `ρ = M (P1 + P2) / (2 R T)`, which is the isothermal pipe's
  `M (P1² − P2²) / (2 R T)` without its acceleration term.
- `64 x⁺`: Poiseuille's drop. Over it the piece's entrance, seen two ways:
- `a_k`, attached: what Shah's laminar developing flow from a uniform profile,
  `Δp* = 13.76 √x⁺ + (1.25 + 64 x⁺ − 13.76 √x⁺) / (1 + 0.000212 / x⁺²)`, has over Poiseuille's: 1.25 heads long, the
  boundary layer's short.
- `b_k`, separated: a contraction's loss, `0.5 (1 − A_k / A_before)` where it narrows (from a vessel: 0.5), and what
  Churchill's 1977 friction factor (laminar, transition and turbulent in one formula, smooth wall) has over
  Poiseuille's, times `L / D`: nothing until the flow is turbulent.
- `N12(a, b) = (a¹² + b¹²)^(1/12)`: whichever picture loses more, joined as Churchill joins his regimes. No constant
  of mine.
- `e_k = (1 − A_k / A_after)²`: Borda and Carnot's expansion out of the piece (into a vessel: 1, its jet's energy).
- A gas never above the **choked** rate at the narrowest section,
  `A_min P1 √(γ M / (R T1)) (2 / (γ + 1))^((γ + 1) / (2 (γ − 1)))`.
- `ΔP(m)` rises with `m` everywhere (checked over Re 1 to 1e6, L/D 0.5 to 1000), so the rate at a drop is one root.

Held to what was measured (`docs/DATA_SOURCES.md`, Flow between vessels):

| What | Source | Agreement |
|---|---|---|
| a smooth pipe's friction, turbulent | McKeon, Zagarola and Smits 2005's fit of the Princeton Superpipe's data (within 1.4 % of it) | Churchill within 2.1 % of the fit, Re 1e4 to 1e7 |
| a short sharp-edged tube's discharge, l/d 2 to 10, Re_h 10 to 2e4 | Lichtarowicz, Duggins and Markland 1965's correlation of the measurements (±0.02 in `Cd`) | the law above within 0.03 in `Cd` everywhere, worst in the transition (Re_h ≈ 3000) |

Tried and left (each against Lichtarowicz, the worst `Cd` error): Churchill's friction with constant end losses
alone, 0.078 (too free between Re_h 100 and 3000: a short tube's laminar flow is developing); Shah's developing flow
added to the contraction's loss, 0.083 (too tight: the two are one entrance seen two ways, not two losses);
the larger of the two pictures, 0.032; their 12-norm, 0.029, but that takes a long laminar tube 6 % over Poiseuille (two equal terms joined are 2^(1/12) of one), so Poiseuille's drop is kept out of the join and only what each picture has over it is joined: 0.030, and Poiseuille's exactly where it holds. Sampson's creeping end correction (3π/8 of a radius a
side) made every variant worse below Re_h 300 and is left out: the correlation's own form is not creeping flow's
there, and no bench runs there.

### The closure (`native/core/src/closure.cpp`)

**A node** is a sealed vessel: a sealed rigid cell with the layers under it, at one pressure (Stage 88). An **end**
at the room's pressure is any cell that is not sealed. Nodes joined by conduits form a **group**; a node with a
piston and no conduit is a group of one.

The unknown is each node's pressure as the step ends. Given the pressures, everything follows directly, nodes taken
from the highest pressure down (what crosses runs down the pressures, so the highest gives only):

- **each conduit's crossing** over the step, by its law at the two end pressures. What crosses is what stands at the
  giver's port: at a foot its lowest liquid, layer by layer, then its gas; at a top its gas, or with none its
  liquid. A port that runs dry within the step gives the next for the time that is left.
- **what crosses carries its enthalpy at the giver's end state**, `h(T_end, P_end)`: the giver's whole content is
  brought to its end pressure and the share that leaves is pushed out at it: an expansion (or a compression) against
  the constant end pressure, which is a process the second law allows, whatever the step.
- **each piston's move**: Stage 79's law (`piston_move`) at the node's end pressure, read as the step's whole move;
  the hand's work `F dx` with it.
- **the volume its contents then need** at that pressure: each of its cells holding its energy (with what came in,
  and the hand's work in the top) takes the volume `V'` at which it stands at `P` (thermo's `at_pressure`), less
  what leaves.

A node's **residual** is the room its boundary gives less the room its contents need. It rises with its own
pressure (the plunger gives way, the contents are squeezed, more leaves) and falls with a neighbour's (more arrives).
At no pressure with room to spare the node stands at none: a void (which boiling then fills, Stage 88).

- **One node**: a bracket and regula falsi (Illinois'), as Stage 88's sizing, of which this is the general case.
- **Several**: Newton on the pressures with the Jacobian by differences, each step kept only if it lowers the largest
  residual (else halved); if it has not converged in its rounds, nested one-dimensional solves (exact, slow: two
  nodes), or sweeps of them (more). Sweeps alone converge too slowly to be the method: two syringes on a tube are
  coupled 20 to 10 000 times more strongly than either gives.

**Applied** by the primitives there already are: the hand's work along its edge and the plunger's move
(`World::move_piston`), what crosses by `transfer` (up a giver's layers with its volume, Stage 88's `lift_volume`),
then each column sized (`size_vessel`), which finds the pressure the solve found.

**In rounds with the fast equilibria** (in `settle`, before the open cells'): solve, apply, bring every cell of the
group to its fast equilibria; if any moved, solve the step's totals again (a piston's whole move at the new end
pressure, less what it has moved; a conduit's whole crossing by its law, less what it has passed that way), at most
four rounds. The equilibria have the last word (P1); they move a pressure back toward where it was (a vapour
condensing as it is squeezed), so the work booked at the solve's pressure stays on the second law's side.

An open vessel's plunger still moves first in the step, at the room's pressure (what it displaces runs out by the
step's own processes).

### Sealing

A conduit holds back what crosses it, so it is no gas path to the room: a vessel joined to the open air only through
one is sealed, at its own pressure, the open cell an end at the room's (a syringe on an open flask draws the
flask's air through its nozzle at the nozzle's pace).

### Checks

- **F1**: each crossing the closure made is in the trace (`StepTrace::crossed`: the edge, from, to, the pressures
  solved, the temperature, the mass, the energy): from the higher pressure to the lower, no more than the conduit's
  law passes at those pressures in the step, carrying those amounts' enthalpy at the giver's state; and each ledger
  entry marked a crossing is one of them. T1's exemption for "gas in a space a piston moved" goes: a marked crossing
  is F1's, anything unmarked is judged as before.
- W1 as it is, over the step's rounds. T2, the arbiter, unchanged.

### What goes

`PISTON_SUBSTEPS`, `PISTON_CLEARANCE`, `solve_hydraulic`, `run_join`, `run_liquid`, `run_gas`, `plan_piston`,
`move_piston_alone`'s sealed half, `Edge::flow`, `glass::poiseuille`, T1's `pushed_space`.

## Phases

| Phase | Topic | Status |
|---|---|---|
| 89.1 | Spec (§6, §18) and data: Churchill, Shah, the end losses, the choked rate; the measured friction and discharge they are held to | Sources found and the law chosen against them (above) |
| 89.2 | `chem/conduit.hpp`, pure: a conduit's drop for a rate and its rate for a drop, liquid and gas; closed forms and the measured gates | |
| 89.3 | `Edge::flow` becomes `Edge::conduit`; the lab's nozzles and tubing give their geometry; save, load, digest | |
| 89.4 | The closure: residuals, the solve, the apply, the rounds in `settle`; the sealing rule | |
| 89.5 | A port that runs dry within a step | |
| 89.6 | F1 and its self-test; T1's exemption removed | |
| 89.7 | Stage 79's machinery removed; the lab, the four experiments and their tests measured again; a nozzle's size | |
| 89.8 | Stage 28's `equalize`: sealed vessels joined without a conduit through the closure too, or left with reasons | |
| 89.9 | `chem_tool study flow`; the step's cost; docs; `make gates`; the user's sign-off | |

## Open

(as found)
