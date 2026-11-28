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
  residual (else halved); what Newton leaves, each node closed inside the one before it (exact, slow: two or three
  nodes), or sweeps of each one's own solve (more). Sweeps alone converge too slowly to be the method: two syringes
  on a tube are coupled 20 to 10 000 times more strongly than either gives.
- **The slopes are taken where the node stands** (see Found): over a billionth of its pressure, no further than a
  quarter of the way to a neighbour's pressure, toward where its residual sends it.
- **To 1e-10** of the volumes a residual is the difference of. Pressures not found are not applied.

**Applied** by the primitives there already are: the hand's work along its edge and the plunger's move
(`World::move_piston`, which puts a sealed vessel's move in the ledger as a resize, so that a replay reads what ran
before it at the volume it then had), what crosses by `transfer` (up a giver's layers with its volume, Stage 88's
`lift_volume`), each marked a crossing; then each column sized (`size_vessel`), which finds the pressure the solve
found.

**Then the fast equilibria** (in `settle`, before the open cells'): every cell the closure touched is brought to
them; where any moved, each vessel with a plunger is closed again, alone (its plunger's whole move at the pressure it
now ends at, less what it has moved), at most four times. What crosses a conduit is found once in a step. The
equilibria have the last word (P1); they move a pressure back toward where it was (a vapour condensing as it is
squeezed), so the work booked at the solve's pressure stays on the second law's side.

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
| 89.1 | Spec (§6, §18) and data: Churchill, Shah, the end losses, the choked rate; the measured friction and discharge they are held to | Done |
| 89.2 | `chem/conduit.hpp`, pure: a conduit's drop for a rate and its rate for a drop, liquid and gas; closed forms and the measured gates | Done |
| 89.3 | `Edge::flow` becomes `Edge::conduit`; the lab's nozzles and tubing give their geometry; save, load (a save from before reads its tube's pieces again from the glassware), digest | Done |
| 89.4 | The closure: residuals, the solve, the apply, its rounds in `settle`; the sealing rule | Done |
| 89.5 | A port that runs dry within a step | Done |
| 89.6 | F1 and its self-test; T1's exemption removed | Done |
| 89.7 | Stage 79's machinery removed; the lab, the experiments and their tests measured again; a nozzle's size | Done: the glassware's own 2 mm nozzle kept (20 mL/s of water through two and the tube at a thumb's push: a second for the tutorial's 20 mL); the `syringes` tutorial's water pair held until its plunger gets there |
| 89.8 | Sealed vessels joined with no conduit (Stage 28's `equalize`) | Left as they are, with reasons (Open) |
| 89.9 | The step's cost; docs; `make gates`; the user's sign-off | `chem_tool study flow`; docs done; main's Stages 85 and 91 merged in (the spec 0.91.1); `make gates` passed (2026-11-28: core, ASan and UBSan, ctest's 1052 entries, the Godot suite, the goldens unchanged; the step's cost 1,344,641 instructions on the demo, +0.3 % against main's at ff5ac39); the user's sign-off to come |

## Tests

- `native/tests/test_conduit.cpp` (7): Churchill's factor (64/Re, the transition's rise, Blasius, what a rate takes
  rising throughout); Shah's drop (its two limits, never under Poiseuille's); a long thin tube Poiseuille's and 2.25
  heads, a liquid and a gas; the rate at a drop the root of the drop at a rate over 18 decades, either way through,
  and a nozzle onto a wide piece passing more nozzle first; a syringe's nozzle at a thumb's push (30 mL/s; 22.5 through
  two and the tube; Poiseuille's 2900); a gas choked into a vacuum and into a tenth, never falling as the giver's
  pressure rises; what is no conduit refused.
- `native/tests/test_measured.cpp`: the two gates (above).
- `native/tests/test_closure.cpp` (10): the conduit set, refused, and what it seals; a syringe open to the air only
  through its nozzle, 95.7 kPa at the least pulled 60 mL in a quarter of a second, at the room's again at rest;
  **two syringes of nothing but water** moved as one, 12.17 N in and out, the other 9.99995 and 9.99989 mL of 10;
  **a hydraulic press**, the thumb's 79.8 N held by 314.2 N on four times the area, one pressure; three syringes in
  a row, in and out with the same force; **air into a vessel at a third of its pressure**, choked at 0.7272 g/s (the
  closed form 0.7272), the giver down to 5 C and the taker up to 53 C, the energy standing to the quantum, and at one
  temperature again the entropy up by the two gases' `n R ln(P / P_f)` to 1e-5; **a port run dry within a step**
  (2 mL of water through a foot, then the air); the step's length (four tenths of a second of a thumb's push: the
  taker at 64.845, 64.844, 64.843 and 64.842 mL at four steps' length, one, a half and a quarter); **hands jerked at
  random** on three syringes in a row, six fillings (water with a bubble, nothing but water, air, mixed), weak hands
  and strong, a person's pace and none, 28 800 steps: every invariant on every step, no solve left short; F1's
  self-test (ten corruptions).
- `native/tests/test_syringe.cpp` (Stage 79's, on the closure): a sealed syringe of water now gives 2.1 μL of its
  50 mL to a hand's whole push (195.9 kPa) and is let out to 6.8 kPa by its pull, no cavity; a long thin tube stands
  its law's 388.6 Pa across it (Poiseuille's 373).
- `chem_tool study flow` (a ctest entry): the law against the Princeton fit and the measured discharge, row by row;
  what a syringe's nozzle and two on the tube let through from 10 Pa to 2 bar, water and air; two syringes of water
  through a thumb's push, 24.73 mL/s crossing where the law at the pressures solved gives 24.74, 30.51 mL in and
  30.51 out, every invariant on every step.
- The lab's 13 syringe tests, the six experiments on gas syringes and the lab's random hands pass unchanged, but the
  `syringes` tutorial played at a person's pace: see Found.

## Found

- **A long laminar tube came out 6 % over Poiseuille**: two equal terms joined by a 12-norm are 2^(1/12) of one. The
  join is of what each picture of the entrance has over Poiseuille's drop, not of the drops.
- **T1 read a cell's heat at the wrong volume.** With the plunger moving as the step settles, the heat that ran
  earlier in the step was replayed at the volume after the move: 5 mK off in water under a thumb, enough to read a
  flow as running from cold to hot. A sealed vessel's move is now in the ledger as a resize, replayed like a vessel's
  sizing (Stage 88).
- **A vessel comes to rest on a kink.** A plunger slowing approaches the edge of its friction without end, from the
  moving side; two vessels levelling approach the pressure at which what crosses turns about. A slope taken over a
  millionth of the pressure straddles the kink, and Newton's step with it lowers no residual. The first version
  crawled, fell back to sweeps and applied what it had: T2 by 9e-9 J/K, two steps after a push ended. Now the slope
  is taken where the node stands, the fallback is exact, and nothing short of the tolerance is applied.
- **"The law less what has passed" has no one answer.** Crossing again in the step's later rounds, by a budget,
  left stretches of pressure at which a conduit could pass nothing either way: a residual flat there, the nested
  solve's inner answers not single. What crosses is found once in a step.
- **A tolerance of the vessel's volume as it began** asked a syringe pulled from 0.5 mL to 120 mL in one step (a
  strong hand at no pace: a stress's move) for more than a double holds. It is of the volumes the residual is the
  difference of.
- **A nested solve needs its inner solves tighter than their own tolerance** where the outer vessel is the stiffer
  (a little water next to much air): what the inner leaves open, 3e-15 m^3 of 30 mL, is 40 times the outer's
  tolerance through a coupling of 9e-9 m^3/Pa.
- **A flick of the handle no longer moves the water.** Through two nozzles and the tube water runs at 20 mL/s at
  most: a third of a second's drag left the plunger 6 mL in, and the tutorial waiting for 15. The tutorial now says
  to hold the handle until the plunger gets there, and the test's pupil holds it as a person would.
- **Cheaper than what it replaces**: the lab's 13 syringe tests take 22 s of processor on the closure, 64.5 s on
  Stage 79's sub-steps.

## Open

- **Sealed vessels joined with no conduit** (Stage 28: a plain edge between two sealed cells) still share one
  pressure at once by `equalize`, gas run between them until they are level, after the closure. The plan had this
  retired ("an unrestricted joint becomes a wide conduit"). Left, because: it is the wide conduit's limit, exact,
  and no sub-step patch; no bench joins a plunger's vessel to another without a conduit (a syringe's nozzle is
  always one); and a conduit wide enough to be no restriction asks pressures told to 1e-14 for the volumes to
  close, at the edge of a double. What it costs: a plunger's vessel joined so (the core's tests only) is closed
  alone and levelled after, a step behind. To do it: the group's vessels closed as one node at one pressure, the
  gas between them found as `fill` finds an open cell's.
- **A builder cannot set a temperature in a sealed cell far under the room's pressure** (below about 0.29 atm of a
  diatomic gas): it first asks the cell's state at no energy, which has none there. Older than this stage; a save
  sets the energy and is not held by it. The test of air let into an emptier vessel starts it at 0.3 atm.
- **Newton leaves about one solve in a hundred to the nested one** under hands jerked at random (kinks still met
  within a billionth of the pressure). Exact, but the product of the nodes' solves: three nodes cost three hundred
  evaluations. An analytic Jacobian, each kink's side known, would end it.
- **A gas near its choked rate**: the mean-density law is cut at the choked rate, not blended into it; and it has
  no acceleration term. Not held to a measurement.
- **Pieces in series** are each a fresh entrance: the tube after a nozzle is taken as developing again.
- **A liquid's head** (`P + ρ g h` at a port: communicating vessels, a siphon, a manometer), a nozzle dipped into an
  open vessel as a gesture, a balloon's tension law, a valve's and a frit's conduit, a gas mixture's own viscosity:
  on the same mechanism, later.
