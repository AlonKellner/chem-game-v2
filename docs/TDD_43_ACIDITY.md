# TDD 43: Acids, bases and pH (Stage 43)

## The request

The user approved Stage 41 on 2026-11-24 ("Works perfectly, approved, merge and proceed") and chose its
roadmap's first area as the next stage: "Acids, bases, pH as 43" (`docs/EXPERIMENTS_ROADMAP.md`, area 1).
Another agent holds Stage 42.

GAME_SPEC 0.43.0 covers it in four places: §9 (acids and bases), §13.1 (the readout), §13.3 (`ph`), §13.7 (two
experiments) and §20.

| Phase | Topic | Status |
|---|---|---|
| 43.1 | The data, sourced and verified: NBS's ions, IAPWS's Kw, Pitzer and Mayorga's pairs | Done (`docs/DATA_SOURCES.md`, "Acids and bases (Stage 43)") |
| 43.2 | The core: Na, NaOH(aq), Na₂SO₄(aq), the neutralisation, water's ionisation, the pH | Done |
| 43.3 | The gates: Kw against IAPWS, γ± against Hamer and Wu and Goldberg, the titration study | Done |
| 43.4 | The lab: `ph` in the view, the readout, the debug panel, monitors and goals; a part that signals | Done |
| 43.5 | Two experiments: diluting an acid, an acid meets a base | Done |
| 43.6 | The goldens: every digest moves (the tables grew); every measured value the same | Waiting on the user's approval |
| 43.7 | GAME_SPEC 0.43.0, `make gates`, the user's sign-off | |

## 43.1 The data

**What was used.** Each value was read from the source, and each table is checked to its source by a gate test:

| Data | Source | Gate |
|---|---|---|
| OH⁻, Na⁺ and SO₄²⁻(aq) thermo; NaOH and Na₂SO₄ "ai" (the sums of their ions) | NBS 1982 (Wagman et al.), scanned pages | — |
| Pitzer's pairs for NaOH and Na₂SO₄ | Pitzer and Mayorga 1973 | NaOH γ± (Hamer and Wu 1972) and Na₂SO₄ γ± (Goldberg 1981), within 2.5 % at each point |
| Water's ionisation: ΔH 55.836, ΔG 79.885 kJ/mol, ΔCp −223.8 J/mol·K (from NBS) | NBS | IAPWS R11-24, within 0.06 of pKw from 0 to 100 °C; the release's own uncertainty is 0.05 |

**What was left out.** The CRC's limiting diffusion coefficients of Na⁺ and OH⁻ could not be read from the
page, so they are not used. They are not needed: no surface reaction involves these ions.

## 43.2 The core

**The design: the smallest change that the demo cannot feel.**

- **No species for water's own ions.** Every cell holding water would then hold some, so every
  trajectory would move. Instead:
  - The strong acid and the strong base are fully dissociated electrolytes, their ions pooled as all
    ions are (§10).
  - The neutralisation is a fast equilibrium (§8's general solver). With K = 1/Kw², it uses up the
    lesser of the two, down to what water's own ions leave: 1.1e-8 mol of each at the equivalence, in
    75 g of water.
  - The pH is derived:
    - D = m_H − m_OH, the pooled ions' excess. The free hydrogen ions are m_H = D/2 + √(D²/4 + k), with
      k = Kw·a_w/(γ_H·γ_OH). Where D < 0 it is computed as k/(√(…) − D/2), so nothing near-equal is
      subtracted.
    - Where both ions stand at the equilibrium's product, the same quadratic returns m_H exactly.
- **The equilibria loop** took every equilibrium in turn while any moved. With a second one, the demo's
  cells would run water's evaporation a second time.
  - Now a round counts only the equilibria that could move in the cell (`live`). A cell without the
    neutralisation's species runs one round, as before.
- **Tables grow at their end.**
  - Na is the 8th element, of 16 allowed. The ions go from 3 to 5, of 16.
  - The two species and the reaction are appended.
  - `Lab::load` takes a save made with fewer species, the new ones absent. That keeps
    `bench/states/t1_expulsion_rounding.json` (15 species) loading.

## 43.3 The gates

- `native/tests/test_acidity.cpp`:
  - Kw against IAPWS.
  - γ± against the tables.
  - The neutralisation's heat: −111.672 kJ, −55.836 a water, the ionisation backwards.
  - The pH of pure water at four temperatures (pKw/2), of the acid and its tenfold dilution, of the
    base, and of the salt.
  - Mixed acid and base, short of, at, and past the equivalence: each neutralised in a step, with every
    invariant (P1 among them) and the closed-form warmth.
- **`chem_tool study neutralisation`** (a ctest entry) titrates 25 mL of 1 M acid with 1 M base. Every
  mixture is within 0.02 K of the closed form (0.001 K seen), and the pH jumps from 2.46 at 49 mL to 11.78
  at 51 mL.

**The equivalence point** reads pH 7.12 at 29.3 °C, where pKw/2 is 6.93 (see Open).

| Base added | pH | Temperature |
|---|---|---|
| 0 mL | 0.209 | 20.000 °C |
| 40 mL | 1.392 | 28.564 °C |
| 49 mL | 2.458 | 29.241 °C |
| 50 mL | 7.120 | 29.306 °C |
| 51 mL | 11.775 | 29.186 °C |
| 60 mL | 12.763 | 28.233 °C |

## 43.4 The lab

**pH on the screen and in the data**
- `ItemView::ph`: a vessel's, while it holds a liquid. It is also in the JSON report.
- The readout's row: "…C, …kPa, pH 1.07".
- The debug panel: "pH 1.07 (Kw 6.86e-15 at its temperature)".

**Monitors, conditions and goals**
- A monitor's guided list offers `ph` under "vessel" once the vessel holds a liquid.
- `ph` works in conditions, goals and tutorial results ("pH 1.85"). Its nudges are 1, 0.1 and 0.01.
- A part's condition may ask `signals`: its own signal on now. Before, only `watching` (its quantity set)
  and an alarm's `ringing` could be asked.

## 43.5 The experiments

Each experiment is in `data/experiments.json`. Each is played to its end in three ways: strict with its own
gestures, strict at a person's pace, and free.

**`dilution`, diluting an acid**
- 5 mL of the 1 M acid goes into 45 mL of water, then 5 mL of that, then 5 mL of that.
- pH 1.07, 1.85, 2.73 (the bottle: 0.21).
- A monitor on the last beaker's pH signals below pH 4.

**`neutralisation`, an acid meets a base**
- 40, 8 and 5 mL of 1 M sodium hydroxide go into 25 mL of 1 M sulfuric acid.
- The pH goes from 0.21 to 12.29 at most, and the temperature from 20.0 to 27.3 °C at most, giving 3.551 g of
  the salt.
- A monitor on the pH signals above 7.
- The base is 1 mol/kg (0.25 mol in 250 g of water). The first draft's 0.25 mol in 240 mL reached the
  equivalence at 48 mL, where the step meant to show 4 % of the acid still left.

## 43.6 The goldens

All six of `make sessions-check`'s digests move from command 0, along with the two golden tests of
`lab_tests` and `demo_session`:
- `digest()` hashes each cell's whole amount and particle vectors, one entry per species.
- Two more species make every vector longer.

What was measured is unchanged. `chem_tool run` was run on both sessions with Stage 41's build and with this
one, and the printed output differs only by the new element's line in the conservation table,
"Na 0 -> 0 quanta of atoms":
- every command's step;
- every warning and danger;
- every total.

Re-recording (`make sessions APPROVED=1`, and the lab tests' records) waits on the user's approval.

## Open

- **The equivalence's pH is 7.12, not pKw/2 (6.93 at 29 °C).** Pitzer's single-ion coefficients are a
  convention. In the Na₂SO₄ solution at the equivalence, the acid's fitted H⁺/SO₄²⁻ pair (β1 −8.17,
  β2 −24.3, fitted to Staples with no bisulfate) leaves γ_H different from γ_OH. It needs one of these:
  - the bisulfate equilibrium itself (HSO₄⁻ ⇌ H⁺ + SO₄²⁻, Ka2 about 1.0e-2), with the acid's pair refitted:
    a change to the demo's own solution, and so a behaviour change;
  - or a stated convention for the single ions (MacInnes's).
- **No solids of the sodium salts.** No NaOH pellets to dissolve, which would be the strongly exothermic
  experiment, −44.5 kJ/mol. And no Na₂SO₄·10H₂O crystallising out of a boiled-down solution, which stays
  dissolved at any strength.
- **No burette.** The pours go by the pour panel's slider, which takes at least 2 % of the source, so the
  last drops of a titration cannot be poured one at a time. A drain whose rate a valve sets is roadmap
  area 1's burette, with a controller at speed.
- **No indicator.** Its colour would follow the pH through the spectral pipeline: the next part of area 1.
