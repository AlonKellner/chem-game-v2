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
| 50.1 | GAME_SPEC; `data/instruments.json`, the marks in `data/glassware.json`, their sources | |
| 50.2 | `lab/instruments.hpp`: offsets, readings, the conventional mass, I1 | |
| 50.3 | The lab: `Kind::Balance`, `weigh`, `unweigh`, `tare`; saved, not digested | |
| 50.4 | The lab: `readings`, `seed`, labels; the readings in the view; the hand's `about`; a tip of the solids | |
| 50.5 | The screen: the balance's bodies and display, its gestures and readout; the readout by instruments | |
| 50.6 | The screen: a typed amount in the pour menu, a scoop in the tip menu | |
| 50.7 | The experiments: the hydrate, the carbonate; played strict, at a person's pace and free | |
| 50.8 | This document, CHANGELOG, `make gates`, the user's sign-off | |
