# TDD 50: The measured lab (Stage 50)

## The request

The curriculum's step A (`docs/CURRICULUM.md`, merged 2026-11-24), the user's "Approved, proceed"
(2026-11-25): the lab a first-year student measures in. Until now every reading was the exact truth,
amounts on the screen were a share of what a vessel holds, and nothing weighed anything.

Its number is 50: another agent's Stage 49 (Stage 47's open items) was in progress on main's next
number.

What it builds:

- **The balance** (T2), with tare: a top-loading one (0.01 g) and an analytical one (0.1 mg).
- **Readings by instruments** (T4): a bench may say its readings are an instrument's. A vessel then
  shows its thermometer's, its pH meter's and its marks' readings, each with its resolution and an
  error drawn once from the bench's seed, and its label, not what it holds. The free lab and the debug
  panel keep the exact truth.
- **Amounts** (T1): a pour's amount typed in mL, a scoop of a solid in g; by instruments, the hand that
  pours or scoops it lands within its own error, so what was taken is known only by weighing it.
- **Labels**: a vessel's own text from the bench ("copper sulfate, blue crystals").
- **The experiments**: the formula of a hydrate (curriculum 6), and a carbonate's mass lost (7).

The rest of step A (the measuring cylinder, the volumetric flask, the pipette, the burette, the
calorimeter, a thermometer's lag) is the next stage: it is mostly glassware's geometry.

## The design

**The balance** (`Kind::Balance`, `balance`, 4 × 2 squares). An instrument with no cell and no edge, like
the viewer: the world and its digest are the same with it and without it. It goes **under** a vessel as a
burner does (`weigh {balance, vessel}`; `unweigh {balance}` sets it beside), and so never under one with a
burner under it.

- **What it reads.** The load is the vessel, what it holds and what is on its mouth (a stopper, a balloon
  with its gas, a condenser and what it holds). A balance is calibrated with steel weights, so it reads a
  load's **conventional mass** (OIML D 28): `(m − ρ_air V) / (1 − ρ_air / 8000 kg/m³)`, with `V` all the
  load's own volume (its glass, its solids and liquids, its gas) and `ρ_air` the room's air from its state
  (its composition, 1 atm, its temperature). An open vessel's air weighs nothing on it; a hot vessel's air
  weighs less than nothing (it reads light), carbon dioxide in a beaker weighs something.
- **Its display**: the net reading, `conventional − tare`, to its readability; past its capacity `OL`. A
  load's mass is exact; the balance adds no error of its own beyond its readability and its linearity, an
  offset proportional to the load drawn once from the seed (`|offset| ≤ linearity`).
- **`tare {balance}`**: the tare becomes the load's conventional mass now. Saved; not digested.
- **Models** (`data/instruments.json`): `top_loading` (0.01 g, 620 g, linearity 0.02 g) and `analytical`
  (0.1 mg, 220 g, linearity 0.2 mg), after manufacturers' sheets for their classes (design).

**Readings by instruments.** A bench's `readings`: `truth` (as before) or `instruments`; its `seed` (an
integer, 0 without one).

- Each vessel has a **thermometer** (0.1 °C, ±0.2 °C) and, with a liquid, a **pH meter** (0.01, ±0.02):
  their readings are the truth plus an offset drawn once per vessel from the seed, within the stated
  accuracy, rounded to the resolution.
- Its liquid's volume is read by its **marks** (`data/glassware.json`: each size's mark spacing and the
  marks' error, ±5 % for a beaker or a flask, ±10 % for a bottle): the truth scaled by a share drawn once
  per vessel within that error, to the nearest mark.
- The readout shows those readings and the vessel's label, not its contents; the balance shows its
  display. Monitors, controllers, goals and the tutorial's checks still read the truth (§20).
- **An offset is a hash** (FNV-1a of the instrument's key, through SplitMix64, with the seed) taken to a
  uniform share in [−1, 1]: the same bench gives the same instruments on every machine.

**The hand.** `pour {from, to, ml, about}` and `tip {from, to, species?, g, about}`: with `about`, the
amount moved is the amount asked times `1 + 0.10 u`, `u` the hash of the seed, the step and the two
vessels in [−1, 1], and at most what there is. The screen sends `about` when readings are by instruments.
A `tip` with no `species` moves the solids in proportion to their masses (a scoop of a mixture).

**I1. A reading is its truth within its stated error**: for every vessel and balance, every reading is
within the instrument's accuracy plus half its resolution of its truth (the balance's truth the
conventional mass less the tare; the marks' within their error of the volume plus half a mark). It fires on
an offset drawn past its accuracy, and on a reading rounded to the wrong resolution.

**The gate test of the balance's physics** (`docs/DATA_SOURCES.md`, OIML D 28): OIML's own case, a kilogram
of water at 20 °C (1001.8 cm³) in air of 1.2 kg/m³, reads 998.95 g; and the lab's balance under a beaker of
the tables' water reads the closed form of its glass, water and air, to its readability.

## Phases

| Phase | Topic | Status |
|---|---|---|
| 50.1 | GAME_SPEC; `data/instruments.json`, the marks in `data/glassware.json`, their sources | Done |
| 50.2 | `lab/instruments.hpp`: offsets, readings, the conventional mass, I1 | Done |
| 50.3 | The lab: `Kind::Balance`, `weigh` (and `onto`), `unweigh`, `tare`; saved, not digested | Done |
| 50.4 | The lab: `readings`, `seed`, labels; the readings in the view; the hand's `about`; a tip of the solids; a balance's `reads_g` | Done |
| 50.5 | The screen: the balance's bodies and display, its gestures and readout; the readout by instruments | Done |
| 50.6 | The screen: a typed amount in the pour menu, a scoop in the tip menu | Done |
| 50.7 | The experiments: the hydrate, the carbonate; played strict, at a person's pace and free | Done |
| 50.8 | This document, CHANGELOG, `make gates`, the user's sign-off | |

## What was built

**`lab/instruments.hpp`** (pure).
- The data's instruments: two balances, a thermometer, a pH meter, the hand, the walls' densities (glass ISO 3585's
  2.23 g/cm³, latex 0.92) for the air a load's glass or rubber displaces.
- `share(seed, key)`: FNV-1a of the key through SplitMix64 with the seed, a uniform share in [−1, 1].
- `conventional_g`, OIML D 28's; `linearity_g`, in proportion to the load; `display_g`, the net with each side's
  linearity, to the readability; `marks_ml`; `display_text` and `plot_display`, the seven segments.
- **I1** (`check_I1`): a reading within its error and half its resolution of its truth, and a multiple of its
  resolution. It fires on an offset at 1.5 of the accuracy, a reading in hundredths where it reads tenths, and a
  balance off by twice its linearity.

**The lab** (`lab.cpp`, `lab.hpp`).
- `Kind::Balance` (`balance`, 4 × 2 squares, `model`): no cell, no edge. `weigh` puts it under a vessel where the
  vessel stands, as `heat` puts a burner; with `onto` the vessel goes onto it where it stands (its foot on the
  balance's, centred). One thing under a vessel: `heat` is refused over a balance, `weigh` over a burner.
- A vessel lifted off it (`place`, `stow`, `trash`, `submit`, a burst) leaves it where the vessel stood; a `move`
  of the vessel carries it along; `invert` is refused over it, as over a burner.
- Its view: its load's conventional mass (`gross_g`: the vessel, what it holds, what is on its mouth; the room's
  air from its own gas at 1 atm), its tare, its display (none off the bench), `overload`. The report lists it.
- A bench's `readings` and `seed`; a vessel's `label`. Each vessel's view has its thermometer's, its pH meter's
  and its marks' readings, in both modes; the lab's view says which the readout shows.
- `pour` and `tip` with `about`: the amount times `1 + 0.10 u`, `u` drawn from the seed, the step, the two vessels
  and the command, and at most what there is. A `tip` with no species moves the solids in proportion to their
  masses.
- Saved: a balance's model and tare, a label, the bench's readings and seed, each only when there is one (a lab
  without them saves as before). Digested: the seed when it is not 0 (the hand's draws depend on it).
- `reads_g`, a balance's quantity (`conditions.cpp`): for steps, results, monitors and goals.

**The screen** (`ui.cpp`).
- The balance: a case, a steel pan and a display (a screen body of the pass showing `plot_display`'s picture); a
  vessel on it stands on its pan. Carried onto a vessel it goes under it; a vessel carried onto it is set on it;
  carried out from under a vessel it comes out. Its readout: "tare" (`tare:<id>`), what it reads, what is on its
  pan, its model.
- By instruments, a vessel's readout is its label, its thermometer and pH meter, its marks and "a solid in it";
  the pour menu's slider speaks of its marks, its tip buttons are one ("the solid", `panel:tip:solid`), and what
  it moves is a hand's.
- The pour menu's amount (`panel:amount`), typed: mL of the liquid, or g of the solid from a vessel with none;
  Backspace, Enter, Escape; held to a strict step's typed gesture as the part editor's fields are.

**The experiments** (`data/benches/`, `data/tutorials/`, `data/experiments.json`).
- `hydrate`, *the water in blue crystals*: an analytical balance, a dish tared on it, about 2 g of the
  pentahydrate scooped in, heated until white, cooled to the room, weighed again (10 steps).
- `carbonate`, *what heat takes from a carbonate*: a top-loading balance, malachite weighed by difference, heated
  until black, cooled, weighed again (7 steps).

## Measured

- **The balance.** OIML D 28's own case reads 998.948 g. The lab's 250 mL beaker of 80 g of glass and 100 g of
  water reads 179.85 g on the top-loading balance: less by the air its glass and water displace. The room's air,
  nitrogen and oxygen with no argon, is 1.199 kg/m³ at 20 °C (real dry air 1.204). The same beaker of 0.004 mol of
  carbon dioxide reads 0.06 g more than of air; heated to 650 °C, less by the air its heat thinned.
- **`hydrate`**, at its seed: 2.178 g of crystals taken (aimed at 2 g). Heated white, then cooled in the air, the
  dish holds 1.231 g of the anhydrous salt, 0.160 g of the monohydrate and 0.024 g of the trihydrate: the dry
  salt took back some of its own water, still in the dish's air and the room's, as it cooled. The balance reads
  1.4139 g; x from the readings is 4.79 to 4.81 (strict, at a person's pace, free). A lab that cools its dish in
  the air gets about that; a desiccator is how a real one gets 5.
- **`carbonate`**: 1.548 g of malachite (aimed at 1.5 g) leaves 1.114 g of copper oxide, 71.95 % (the equation
  72.0 %); the balance reads the dish at 41.53 g with it and 41.12 g after: 0.02 g heavy, its own carbon dioxide and
  condensed steam (Open).

**Tests**:

| Test | What it checks |
|---|---|
| `test_instruments.cpp` (5) | OIML's case (the sources' gate); the draws' spread; I1 holds of 6000 readings and fires on three corrupted ones; a display's net, linearity and marks; the seven segments |
| `test_balance_lab.cpp` (6) | the closed form of a beaker's load, tare, a pour off it, the analytical balance; hot air light and CO2 heavy; nothing in the world (digests equal with and without), refusals, save and load, left where its vessel stood, carried by a move, left by a stow and a hand-in; I1 over a lab by instruments at three seeds, saved and digested; the hand's misses, the same on every run, otherwise at another seed, exact without `about`, the solids in proportion, all past what there is; `onto` from the bench and the cabinet, and its refusals |
| `test_experiments.cpp` (6) | each experiment strict with its own gestures, at a person's pace, and free; the hydrate's copper all in the dish or the jar, the balance its solids, x 4.5 to 5; the carbonate's 72 % |

## Open

- **An open vessel's gas never leaves it but by bulk flow** (§20: gases do not diffuse between cells), and both
  experiments show it on the balance. The carbonate's dish ends holding 0.028 g of its own carbon dioxide and
  0.010 g of water condensed from its own steam: it reads 41.12 g where its oxide alone would read 41.10, so the
  student's share left comes out 73.5 % where the equation says 72.0 %. The hydrate's dish keeps its steam to take
  back as it cools. A real dish loses both to the room by diffusion and convection within minutes. The fix is
  gas exchange at an open mouth (roadmap area 6; GAME_SPEC §8's slow evaporation from a still surface): it moves
  every open vessel, so every golden.
- **The hydrate's water taken back as it cools grows from what is left of the monohydrate.** A reverse rate is in
  proportion to the forward reaction's reactant (§20), so the dish's rehydration starts from the trace the
  heating left (under a milligram) and its size depends on how long the dish was heated. A real dry salt takes
  water back at its surface whatever is left. The outcome (x near 4.8) is what a lab that cools in the air gets;
  its cause in the model is not yet the right one. It wants a rate law for a solid taking up a gas at its surface.
- **The jar's crystals lose a little water to the room's dry air** (Stage 41's Open): 0.1 % of the copper of a
  jar of the pentahydrate is the trihydrate by the end of `hydrate`. Room humidity is roadmap area 6.
- **By instruments, monitors, controllers, goals and checks still read the truth**, and a sensing part's editor
  lists what a vessel holds (§20).
- **No thermometer's lag and no instrument's noise**: the calorimeter's stage (the curriculum's step A, its second
  half) brings the lag, and the volumetric glassware.
- **The free lab has no balance** in its cabinet yet.
