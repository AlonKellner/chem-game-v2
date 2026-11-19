# TDD 19: Concentrated solutions (Stage 19)

## The request

The plan's Stage 19 (approved 2026-11-17):
- one excess-Gibbs activity model, chosen by its fit;
- the lower hydrates and the anhydrous salt crystallising out of solution;
- the dissolved salt taking volume;
- CuSO₄(aq) re-fitted.

The oracles are the solubility curve, the 5↔3 transition and the boiling point. GAME_SPEC §4.6, §7,
§9, §10, §18 (K1, P1) and §20.

| Phase | Topic | Status |
|---|---|---|
| 19.1 | The data: NBS rows for the hydrates and ions, densities, Pitzer's parameters, Staples' acid, the CRC solubility, the transition, apparent molar volumes (`docs/DATA_SOURCES.md`) | Done |
| 19.2 | The spec: §4.6 activities from ions, §10 Pitzer's equations, §9 the four forms of the salt, §7 the driver's molality, §20 | Done |
| 19.3 | The core: ions in the species table, `data/solutions.json` embedded, the excess and its derivatives, the probe over every particle, dissolved volumes | Done |
| 19.4 | The data: CuSO₄(aq) refitted, three crystallisations and three dehydrations, calibrated spectra for the two hydrates | Done |
| 19.5 | P1's rounding bound for a minor gas (a latent failure, found on the way) | Done |
| 19.6 | The demo closes the valve at 12 g; the tutorial says why | Done |
| 19.7 | The golden records: the evidence below; approved by the user ("This looks perfect! Approved, proceed", 2026-11-19), re-recorded | Done |
| 19.8 | CuSO₄'s C^φ left out: `make gates` (the debug build) caught P1 in a flask boiled nearly dry; refitted, re-recorded after a second approval ("Approved, proceed", 2026-11-19) | Done |
| 19.9 | The user plays it (`make run`) and signs off | Pending |

## Decisions

- **Pitzer, with its parameters at 25 °C.**
  - The parameters do not depend on temperature, so the excess Gibbs energy over T does not
    either. The excess is then all entropy and the enthalpy is unchanged: the temperature still
    follows from the energy in closed form, and conservation is untouched.
  - The price is that the activity coefficients' temperature dependence has to live somewhere.
    It lives in a fitted heat capacity of CuSO₄(aq), +211.81 J/mol·K.
- **Chosen by the fit.**
  - Three CuSO₄ parameter sets were fitted to the CRC's solubility, each with three numbers
    (`Hf`, `S°`, `Cp`) over eleven points from 0 to 90 °C. The rms errors in molality were 1.35 %
    (Pitzer and Mayorga 1974), 1.9 % (Downes and Pitzer 1975) and 2.9 % (Höffler 2017 at 25 °C).
  - Pitzer and Mayorga's set is used without its C^φ (−0.0012), and the rms is then 1.49 %. See
    "Stable at every strength" below.
  - With only two numbers fitted (`Cp` as dissolving changes none), every set misses by 15 %
    at the ends: activities fixed at 25 °C cannot carry the curve's shape alone.
- **The acid, fully dissociated.**
  - Every published model has bisulfate, and none of its parameters carries over. So the acid's
    three numbers are fitted to Staples' recommended φ and γ± (16 molalities, 0.1 to 5 mol/kg,
    rms 0.0046), with the exponents 4.5 and 12.
  - The negative β's stand for the association left out.
- **Components keep their names; activities come from ions.**
  - The lab, its goals, the tutorial and the optics still speak of CuSO₄(aq) and H₂SO₄(aq). Each
    names its ions in `data/species.json`; datagen refuses ions whose elements or charges do not
    sum to the species'.
  - A shared ion is the common-ion effect: the acid's sulfate lowers the salt's solubility, as it
    does in reality. Before, it raised it.
- **One function, its derivatives exact.**
  - `ln γ_j` and the solvent's share are the analytic derivatives of one `G_ex`.
  - A test takes central differences of `Σ n ln a` along three reactions, at four strengths with
    and without acid, and finds them equal to `Σ ν ln a` within 1e-6. The random sweeps, which now
    give charged random solutes a Debye–Hückel term, find the entropy never falling.
- **The driver is a molality.** An electrolyte's activity goes as the cube of its strength. The
  surface law's rate counts the acid that reaches the oxide, which is its concentration. With the
  ideal model the two were the same, so Stage 6's calibration stands.
- **What follows, unwritten:**
  - The pentahydrate gives way to the trihydrate under its saturated solution at 93.2 °C
    (measured 95.9 °C).
  - The saturated solution boils at 104.8 °C.
  - The acid's last water stays: hot concentrated sulfuric acid is a liquid.
  - Boiled down, a pure solution of the demo's copper starts crystals at 10.0 g of water.
  - Boiled dry and heated on, it is all white anhydrous salt after 30 minutes, at 336 °C.
- **The demo closes the valve at 12 g, not 10.**
  - With the acid in the liquor, 10 g is past the trihydrate's saturation at the boil. Pale
    trihydrate crystallises, then turns back into pentahydrate as the flask cools, with a warning
    on the way.
  - That is the chemistry, and the tutorial now says so. Its "overheated" failure means dry
    crystals losing water: the monohydrate, the white salt, or a trihydrate with no liquid left.
- **P1 counted a minor gas wrong (latent since Stage 5).**
  - Gas moved after a cell's equilibrium rounds up to a quantum per species. For the steam over a
    cold condenser drawing dry air, that is 1 part in 3.6e7, against P1's allowance of 1 part in
    the whole gas (2.1e9).
  - On `main`, closing the valve at 11 g fails P1 the same way.
  - The bound is now RT Σ|ν_i| N (1/n_i + 1/n_gas) over the reaction's gases. Its self-test
    fires under the old bound and passes under the new one.
- **Stable at every strength (found by `make gates`).**
  - The debug build asserts on every invariant. On a different rounding path from the release
    build's, it took the boil-dry test's flask to a liquor of a few quanta at hundreds of mol/kg.
  - There, CuSO₄'s published C^φ (−0.0012) made a cubic term that turned the water's activity
    back up beyond 65 mol/kg. Evaporating then drove itself, and the equilibrium search, which
    rightly assumes the affinity falls, failed P1.
  - C^φ was fitted below 1.4 mol/kg, where it is within the noise, so it is left out. The
    refit: −835.990 kJ/mol, −51.520 J/mol·K and 211.81 J/mol·K, worst 2.6 %, transition 93.2 °C.
- **Cost.** The demo's step is 6.67 µs (4.27 before): 4.0 ms a frame at 600×.
  - The time is the boiling equilibrium's search, which evaluates the excess at each trial
    extent, and the three more crystallisations in the flask.
  - Each direction a reaction cannot go is now skipped before any rate is worked out. The excess
    computes each exponent once and is not computed for a probe's starting amounts.

## Tests

- **`test_solution.cpp`** (core):
  - **K1:**
    - the CRC's twelve points within 3 % (the worst 2.6 %, at 90 °C);
    - CuSO₄'s γ± at saturation within 4 % of both measurements;
    - the acid's φ and γ± within 2 % of Staples at five strengths;
    - the transition within 3 K of 95.9 °C;
    - the common-ion effect (the solubility falls with acid, 0.70 of it at 1 mol/kg);
    - a neutral solute (urea, a test table's) still boils 0.512 K higher per mol/kg.
  - **Gibbs–Duhem**, by central differences, as above.
  - **The heat of solution:** 14.56 kJ/mol, measured as the temperature drop.
  - **Stable at every strength:** the evaporation's affinity, and so the water's activity, falls as
    water leaves, to 400 mol/kg, at four shares of acid. With the published C^φ it fails at
    69 mol/kg.
  - **The dehydration:** at 150 °C, the monohydrate; at 300 °C, the white salt, in the same grains.
  - **Boiling:** boiled down, crystals start between 8 and 12 g of water; boiled dry and heated
    on, all anhydrous.
- **`test_invariants.cpp`:** P1 allows the rounding of a minor gas, and no more.
- **`tables.hpp`:** the built-in table without the trihydrate's data, for the refusals that need
  a species without data (every built-in species now has data), and with urea.
- **The lab:**
  - the debug panel's activity of 1 mol/kg acid, and the first dehydration "fast from about 94 C";
  - a flask of water boiled dry (the acid's, rightly, never is);
  - the tutorial at 12 g, by its gestures and at a person's pace.
- **`make sessions-check`:** changed. The evidence is below.

## The golden records: before and after

The demo session (`data/sessions/demo.json`), measured:

| | Before (`main`) | After |
|---|---|---|
| The malachite gone | 229.0 s | 229.0 s |
| The balloon | 0.896 g CO₂, 64.8 % of its gas | the same |
| The oxide gone | 785.3 s (219 s after the acid) | 793.7 s (228 s): 50 mL now holds less acid, its 1.8 mL taking room |
| The valve closed | at 10 g of water, 1042.9 s | at 12 g, 1042.1 s |
| The flask below 21 °C | 1645.0 s | 1720.2 s |
| The beaker | 40.868 g of water and nothing else | 38.914 g of water and nothing else (2 g fewer boiled off) |
| The crystals | 8.935 g of CuSO₄·5H₂O, 447,472 of 256 µm, 79 % of the copper | 9.274 g, 1,002,347 of 198 µm, 82 % of the copper |
| Their purity | 99.977 % | 99.977 % (the same 2 mg of malachite) |
| Conservation | every element, the mass, the charge and the energy unchanged | the same |

More crystals come out because the acid in the liquor now lowers the salt's solubility. Smaller
ones come out because, held down by the acid, the liquor is more supersaturated when they start.
The records differ from the first step: the acid bottle's solution now takes room, so it starts
with less gas.
