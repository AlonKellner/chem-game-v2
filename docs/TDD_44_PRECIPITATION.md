# TDD 44: Precipitation and gas (Stage 44)

## The request

The user approved Stage 43 on 2026-11-24 ("Approved, merge and proceed"). The next area of the roadmap
(`docs/EXPERIMENTS_ROADMAP.md`, area 2: precipitation and gas) is the one that builds on Stage 43's base.
Stage 42 (another agent's, the spectral viewer) merged into main during this stage, and this branch was
rebased onto it.

GAME_SPEC 0.44.0 covers it in §8 (the equilibria's rounds), §9 (precipitation and gas, particles), §13.7 (two
experiments) and §20.

| Phase | Topic | Status |
|---|---|---|
| 44.1 | The data, sourced and verified: Cu(OH)₂'s row (SKB TR-00-13), its turning to oxide (Fukuda 2019), malachite in acid (Nicol 2018) | Done (`docs/DATA_SOURCES.md`, "Precipitation and gas (Stage 44)") |
| 44.2 | The core: equilibria probed again only when their cell changed; a precipitate's grains; four reactions | Done |
| 44.3 | The gates: solubility constants against NIST SRD 46 and wateq4f, the rates at their sources' conditions, `chem_tool study precipitation` | Done |
| 44.4 | Two experiments: a precipitate, a gas from a solid | Done |
| 44.5 | The goldens: every digest moves (a species added), and the demo's leftover malachite dissolves | Re-recorded with the user's approval, 2026-11-24 ("Re-record them") |
| 44.6 | GAME_SPEC 0.44.0, `make gates`, the user's sign-off | `make gates` passed (347 s: ASan+UBSan 347, ctest 212, Godot 12 of 12); signed off by the user, 2026-11-24 ("Stage 44 is approved, merge and proceed") |

## 44.1 The data

| Data | Source | Gate |
|---|---|---|
| Cu(OH)₂(s): ΔfG° −359.92, S° 87.0, Cp 87.85; ΔfH° −443.99 derived | Puigdomenech and Taxén 2000 (SKB TR-00-13), Table 2 | log \*Ks 8.55 against 8.674 (minteq.v4, NIST SRD 46) and 8.64 (wateq4f), within 0.15; CuO's 7.58 against 7.62 to 7.66, within 0.1 |
| Density 3.93 g/cm³ | the Handbook of Mineralogy (spertiniite) | the molar volume test |
| Grain 0.3 µm (calibrated) | the sphere of Fukuda's BET area | the grains test |
| Cu(OH)₂ → CuO + H₂O(g): Ea 127.0 kJ/mol, ln A 32.9 | Fukuda, Favergeon and Koga 2019 (the surface reaction step, dry N₂) | isothermal at 400 K, within 0.2 %; a half-life over 90 days in water at 25 °C (Cudennec: months) |
| Malachite + acid: A 1.44e7 cm/s, exp(−6447/T), order 0.65 | Nicol 2018, eq. 4 | his shrinking rate at 0.1 and 0.2 M, 25 and 45 °C, within 1 %; his 4.1e-7 and Kopylov and Orlov's 3.6e-7 mol/cm²/s |
| Colour (calibrated) | no measured spectrum found | the user's eye |

**Why not NBS or JANAF for the hydroxide.** NBS 1982 gives Cu(OH)₂ only an enthalpy of formation. JANAF's
1966 entropy (108.4) makes it 7.2 kJ more stable than CuO and water, a log \*Ks of 6.41 that no measured
solubility supports. With it, the classic experiment, the blue precipitate blackening, could never happen.
Every evaluation read has the hydroxide metastable, by 5.5 to 5.9 kJ.

**Malachite's rate, read twice.** Nicol prints k = 1.05e-4 cm/s and a rate of 4.1e-7 mol cm⁻² s⁻¹ in
0.2 mol/L acid. His eq. 4 with A = 1.44e7 cm/s and C in mol/cm³ gives a shrinking rate whose flux, at his
density, is exactly that 4.1e-7. So the rate counts moles of malachite, not of copper, and the game's
prefactor is his shrinking rate over its own molar volume.

## 44.2 The core

- **The equilibria's rounds** (`Simulation::project`). Stage 43 broke out of the rounds when fewer than two
  equilibria could move in a cell, which kept the demo's water from being taken twice. With four equilibria
  that rule no longer holds: copper sulfate and water make the hydroxide's dissolving live in every demo cell.
  - Now each equilibrium is probed again only once its cell has changed since it was last probed. One that
    cannot move, or that moved last, rests until another moves.
  - The result is exact (the same state gives the same probe), and a cell where one equilibrium moves settles in
    one round. The goldens did not move with it.
- **A precipitate's grains.** A solid that appears by a fast equilibrium came as one lump. A species may now
  give `grain_um`, and one that appears from a solution comes as spheres of that diameter. Copper
  hydroxide's 0.3 µm gives 1.8e13 grains for a gram; the oxide it turns to keeps them, so acid dissolves it as
  the fine powder it is.
- **Four reactions**, appended:
  - `hydroxide_precipitation`, CuSO₄(aq) + 2 NaOH(aq) → Cu(OH)₂(s) + Na₂SO₄(aq), equilibrium.
  - `hydroxide_dissolution`, Cu(OH)₂(s) + H₂SO₄(aq) → CuSO₄(aq) + 2 H₂O(l), equilibrium. It is the first two
    less the neutralisation, so the three settle together in a round or two. Without it the precipitation and
    the neutralisation would trade a few quanta for ever, and P1 would fail.
  - `hydroxide_dehydration`, Cu(OH)₂(s) → CuO(s) + H₂O(g), first order. Steam is the product, so the steam
    over it slows it, as Fukuda measured. Dry, its affinity is large and the rate is the measured one; under
    water, steam at saturation.
  - `malachite_dissolution`, Cu₂CO₃(OH)₂(s) + 2 H₂SO₄(aq) → 2 CuSO₄(aq) + CO₂(g) + 3 H₂O(l), the surface law
    driven by the acid, transport in series.
- **Hydrolysis follows.** A copper sulfate solution with no acid gives up a trace of the hydroxide, Cu²⁺ +
  2 H₂O → Cu(OH)₂ + 2 H⁺, by the dissolving equilibrium run backwards: 1.4e-6 mol from 0.05 mol in 100 g, pH
  5.1. Real solutions are acid for the same reason, through the ion CuOH⁺ (pH about 4 at 0.5 mol/kg). The demo
  always has acid in excess, so the demo does not feel it.
- **A pH within the model's range.** Boiled down past its water, a base has no solid to come out as, and its
  molality grows without end: the experiment's beaker read pH 17.6. `ph()` now gives none past 6 mol/kg left
  over (Pitzer and Mayorga's fits hold to 6). `test_acidity` holds 5.9 and 6.1 mol/kg on either side.

## 44.3 The gates

- `native/tests/test_precipitation.cpp`:
  - the solubility constants, and the metastability (−5.51 kJ, SKB's);
  - Fukuda's rate at 400 K in dry nitrogen; years in water;
  - Nicol's rate;
  - a base into copper sulfate short of, at and past the copper, each settled in the step with every invariant
    (P1 among them), the grains at their size;
  - an acid taking the base first, then the hydroxide; copper sulfate's own trace and its pH.
- **`chem_tool study precipitation`** (a ctest entry): 25 mL of 0.2 M copper sulfate titrated with 1 M base.
  Every mixture's hydroxide is the closed form's (half the base, up to the copper), and its warmth is within
  0.001 K of the tables' heat (−56.7 kJ/mol at 20 °C).

| Base added | Cu(OH)₂ | Copper left | pH | °C |
|---|---|---|---|---|
| 0 mL | 0.00002 g | 0.005 mol | 5.20 | 20.000 |
| 5 mL | 0.24392 g | 0.0025 mol | 5.35 | 21.134 |
| 9 mL | 0.43903 g | 0.0005 mol | 5.71 | 21.817 |
| 10 mL | 0.48780 g | 1.7e-8 mol | 7.93 | 21.965 |
| 11 mL | 0.48780 g | 1e-12 mol | 12.36 | 21.913 |

  And the hydroxide's half-life held in dry nitrogen: 2136 s at 100 °C, 163 s at 125 °C, 16.9 s at 150 °C,
  each within 0.1 % of ln 2 / k.
- **The optics test's flask** (`test_optics`, Stage 10) read darker boiled down than dissolved: L\* 31 to 45.
  That was the haze of the malachite left from the first stage. It dissolves now, and the thin layer's middle
  reads 56, as its truth does at that pixel. The test now holds each colour clearly away from the empty flask.
- **The cost play** (`data/plays/cost.json`) tips malachite into the acid; the powder is held up while it
  stirs, and gone after the half hour at 600x. The test reads it while it stirs.

## 44.4 The experiments

Each is in `data/experiments.json` and played to its end three ways: strict with its own gestures, strict at
a person's pace, and free.

**`precipitate`, a precipitate** (7 steps)
- 10 mL of the base brings half the copper out as pale blue copper hydroxide; 15 mL more brings out the rest,
  0.976 g, and leaves the liquid clear, base to spare, pH above 11.
- A full flame boils it down: under boiling water it hardly turns; once dry and hot it blackens, 0.795 g of
  copper oxide, in about eight minutes from the start.
- Cooled below 40 °C, 20 mL of the acid takes the base left over, then dissolves the oxide: blue again, the
  0.01 mol of copper gone round a cycle (about five minutes at 30 °C).

**`fizz`, a gas from a solid** (3 steps)
- 2 g of malachite tipped into 50 mL of 1 M acid, the balloon put on.
- It is gone in about two minutes. Its 0.009 mol of carbon dioxide pushes 220 mL of gas into the balloon:
  0.12 g of the gas is there, 0.28 g in the flask's own air. The acid turns blue, 2.87 g of copper sulfate.

## 44.5 The goldens

Every digest moves, because a new species makes each cell's vectors longer. In the measured output (`chem_tool
run`, Stage 43's build against this one) the pressure session is unchanged. The demo differs in one thing:
the 0.57 mg of malachite its first stage left in the flask now dissolves when the acid goes in. That gives
1.2 mg more pentahydrate (9.1966 g against 9.1953), and the copper oxide's wait is 0.1 s longer. Re-recorded
with `make sessions APPROVED=1` once the user approved it (2026-11-24).

## Open

- **No base speeds the turning.** In a strong base, copper hydroxide dissolves as Cu(OH)₄²⁻ and comes back as
  the oxide "very fast" (Cudennec and Lecerf 2003), so a boiled suspension in excess base blackens in minutes.
  No rate was found. Here it turns as in water, half in half an hour at 100 °C, and the experiment boils it
  dry to show it.
- **A precipitate's look.** It comes out as grains at once and, unstirred, lies as sediment as any solid does.
  Stirred, its 0.3 µm grains are held up, and let go they settle at Stokes' speed, about a centimetre in 20
  hours: a cloud for hours. A real precipitate is a cloud of flocs, grains clumped, that settles over minutes.
  Flocculation is not modelled. Its colour is calibrated, not measured (no spectrum found).
- **Hydrolysis to a solid.** Copper's hydrolysis gives a trace of the solid hydroxide, not the ion CuOH⁺: a
  0.5 mol/kg copper sulfate reads pH 5.1 where about 4 is measured.
- **No sodium carbonate.** A carbonate that dissolves, and its fizz with an acid, need Na₂CO₃ and the carbonic
  acid equilibria (roadmap area 2).
