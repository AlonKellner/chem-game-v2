# Data sources

Every physical constant and material property used by the game, with its source. A value enters
`data/species.json` or `data/reactions.json` only with a verified row here (GAME_SPEC §2,
principle 12). Molar masses are derived by the build from the formulas and the standard atomic
weights of `data/elements.json`.

**Atomic weights** (`data/elements.json`): the abridged standard atomic weights of CIAAW (IUPAC's
Commission on Isotopic Abundances and Atomic Weights), "Abridged Standard Atomic Weights 2024"
(ciaaw.org/abridged-atomic-weights.htm, after the "Atomic Weights 2021" report; read 2026-11-21):
H 1.0080, C 12.011, N 14.007, O 15.999, Si 28.085, S 32.06, Cu 63.546 g/mol, every one as printed;
Na 22.990 (Stage 43, the same table); Cl 35.45, K 39.098, P 30.974, Br 79.904 (Stage 64, the same table, read
2026-11-26).
Check: each molar mass the build derives is these summed by the formula (water 18.015, the
pentahydrate 249.677): `sources: molar masses are IUPAC's atomic weights`.

**Every number in `data/` has an entry in `data/sources.json`** saying whether it is measured
(with the tests that hold it to its source and the phrases of its row here), calibrated (why, and
its row here) or a choice of the game's design; `tools/check_sources.py` (the ctest entries
`data_sources` and `data_sources_self_test`) fails on a value with no entry, a named test or row
that does not exist, and an entry that matches nothing.

## Verified species data (298.15 K, 1 bar)

These rows are in `data/species.json` and used by the game. Sources:

- **NIST**: NIST Chemistry WebBook, SRD 69 (webbook.nist.gov), retrieved 2026-11-15.
  - **CODATA**: Cox, Wagman and Medvedev, *CODATA Key Values for Thermodynamics*, 1984, as listed
    there ("CODATA Review value").
  - **Chase**: Chase, *NIST-JANAF Thermochemical Tables*, 4th ed., J. Phys. Chem. Ref. Data
    Monograph 9, 1998, as listed there.
- **Cp** is the Shomate fit of Chase 1998, as listed in the WebBook, evaluated at 298.15 K:
  `Cp = A + B t + C t² + D t³ + E/t²`, `t = T/1000`. The game holds it constant (GAME_SPEC §20).
- **ISO 3585**: borosilicate glass 3.3, mean specific heat 0.8 kJ/kg·K over 20–100 °C and density
  2.23 g/cm³; the same figures are in SCHOTT's DURAN data sheet.

| Species | ΔHf° (kJ/mol) | S° (J/mol·K) | Cp (J/mol·K) | Source |
|---|---|---|---|---|
| H2O(l) | −285.830 ± 0.040 | 69.95 ± 0.03 | 75.375 | NIST: CODATA; Cp from the liquid fit (298–500 K) |
| H2O(g) | −241.826 ± 0.040 | 188.835 ± 0.010 | 33.590 | NIST: CODATA; Cp from the gas fit (500–1700 K) evaluated at 298.15 K, which equals the JANAF table's 33.590 |
| CO2(g) | −393.51 ± 0.13 | 213.785 ± 0.010 | 37.130 | NIST: CODATA; Cp from the fit (298–1200 K) |
| N2(g) | 0 | 191.609 ± 0.004 | 29.124 | NIST: CODATA; Cp from the fit (100–500 K) |
| O2(g) | 0 | 205.152 ± 0.005 | 29.383 | NIST: CODATA; Cp from the fit (100–700 K) |
| CuO(s) | −156.06 | 42.59 | 42.246 | NIST: Chase; Cp from the fit (298–2000 K) |
| CuSO4(s) | −771.4 ± 1.2 | 109.2 ± 0.4 | 98.531 | NIST: CODATA (Chase lists −769.98 and 109.05); Cp from Chase's fit (298–2000 K) |
| Cu2CO3(OH)2(s), malachite | −1053.95 ± 2.1 | 166.3 ± 2.5 | 154.3 ± 0.93 | ΔHf°: Richardson and Brown, *Enthalpy of Formation of Malachite*, U.S. Bureau of Mines RI 7851, 1974 (−251.9 ± 0.5 kcal/mol, HCl solution calorimetry). S° and Cp: Kiseleva, Ogorodova, Melchakova, Bisengalieva and Becturganov, Phys. Chem. Minerals 19, 322–333, 1992 (adiabatic calorimetry, 3.8–300 K) |
| Glass, as SiO2 | −910.86 | 41.46 | 48.066 | ΔHf° and S° are quartz's (NIST: Chase; S° from its fit), a stand-in: the wall never reacts, so they cancel. Cp is ISO 3585's 0.8 kJ/kg·K × 60.083 g/mol |

Check: every row read back from the table the build embeds, and the liquid water fit's Cp:
`sources: every species' thermodynamic row is the one cited`.

Differences from the planning draft: CuO's ΔHf° is −156.06 (the draft had −157.3, another
compilation's value); glass Cp is 48.07 (the draft guessed about 50).

## Verified data for the solution and the crystals (Stage 6)

**Atkins**: Atkins and Jones, *Chemical Principles*, Appendix 2A, "Thermodynamic data at 25 °C"
(demo.webassign.net/question_assets/atjon6/supplemental_data.pdf), retrieved 2026-11-16. The
appendix reproduces the NBS tables (Wagman et al. 1982).

| Species | ΔHf° (kJ/mol) | S° (J/mol·K) | Cp (J/mol·K) | Source |
|---|---|---|---|---|
| CuSO4·5H2O(s) | −2279.7 | 300.4 | 280 | Atkins. Its ΔGf° there, −1879.7, is not used. Density 2.286 g/cm³: CRC Handbook, 92nd ed., p. 4.62, as cited by Wikipedia |
| H2SO4(aq) | −909.27 | 20.1 | 138.9 (stand-in) | ΔHf° and S° are Atkins's SO4²⁻(aq) (with H⁺(aq) at zero: the acid fully ionised). **Cp is a stand-in**: Atkins's value for the pure liquid acid, since the game treats the acid as one neutral dissolved species |
| CuSO4(aq) | −835.990 (fitted) | −51.520 (fitted) | 211.81 (fitted) | **Fitted, not tabulated**, in Stage 19 with the activities of GAME_SPEC §10 (below). Stage 6 fitted −839.382, −10.095 and chose −96.875 for an ideal-dilute neutral solute. For reference, Atkins's ions give Cu²⁺(aq) + SO4²⁻(aq) = −844.50 kJ/mol and −79.5 J/mol·K |

**The fit of CuSO4(aq) in Stage 6**, replaced in Stage 19. The game treated the dissolved salt as one neutral species in an
ideal-dilute solution. With the tabulated ionic values that model puts saturation near
0.002 mol/kg; the real figure is 1.3. So the dissolved salt's enthalpy and entropy are the two
numbers that make `CuSO4·5H2O(s) ⇌ CuSO4(aq) + 5 H2O(l)` reproduce the handbook's solubility: a
least-squares line through `ln(m) − 5 M_w m` against `1/T` at the five temperatures below, giving
a heat of solution of 11.168 kJ/mol and an entropy of solution of 39.255 J/mol·K. Its heat
capacity is chosen so that dissolving changes no heat capacity (`280 − 5 × 75.375`).

**Solubility of CuSO4** (g anhydrous per 100 g of water): CRC Handbook of Chemistry and Physics,
as reproduced by the Sciencemadness wiki (66th ed.) and Wikipedia (99th ed., pp. 5–179, in g/L).
**Medium confidence in the unit basis**: the two reproductions label the same numbers per 100 mL
and per litre; per 100 g of water is the handbook's usual basis and is what is used.

| °C | 0 | 10 | 20 | 60 | 100 |
|---|---|---|---|---|---|
| handbook, g | 14.3 | 17.2 | 20.5 | 39.5 | 75.4 |
| the game, g | 14.21 | 17.20 | 20.61 | 39.82 | 74.69 |

## Verified data for concentrated solutions and the hydrates (Stage 19)

**NBS**: Wagman et al., *The NBS tables of chemical thermodynamic properties*, J. Phys. Chem. Ref.
Data 11, Suppl. 2 (1982), srd.nist.gov/JPCRD/jpcrdS2Vol11.pdf. It is a scan with no text layer;
each row was read off the page image (retrieved 2026-11-18).

| Species | ΔHf° (kJ/mol) | ΔGf° | S° (J/mol·K) | Cp (J/mol·K) | Page |
|---|---|---|---|---|---|
| CuSO4·5H2O(cr) | −2279.65 | −1879.745 | 300.4 | 280. | 2-156 (the game keeps Atkins's −2279.7, the same table rounded) |
| CuSO4·3H2O(cr) | −1684.31 | −1399.96 | 221.3 | 205. | 2-156 |
| CuSO4·H2O(cr) | −1085.83 | −918.11 | 146.0 | 134. | 2-156 |
| CuSO4(cr) | −771.36 | −661.8 | 109. | 100.0 | 2-155 (the game keeps CODATA's −771.4 and 109.2, and Chase's Cp) |
| Cu2+(ao) | 64.77 | 65.49 | −99.6 | — | 2-154 |
| SO4 2−(ao) | −909.27 | −744.53 | 20.1 | −293. | 2-57 |
| H2SO4(ai) | −909.27 | −744.53 | 20.1 | −293. | 2-58: the acid exists in the table only fully ionised, as 2 H⁺ + SO₄²⁻ |

The free energies give the hydrates' dissociation pressures at 25 °C: 7.80 torr (5 → 3), 5.14 torr
(3 → 1) and 0.0104 torr (1 → 0). Cotti et al., *Cryst. Growth Des.* 2024 (doi
10.1021/acs.cgd.4c00589), give 10 and 6 mbar for the first two (7.5 and 4.5 torr). The game's
table gives 7.67, 5.17 and 0.0125 torr (10.2, 6.9 and 0.017 mbar): it keeps Atkins's pentahydrate
and CODATA's anhydrous salt, which moves the last by a fifth. Check: within 2 %, 2 % and 25 % of
NBS's, and 5 % and 20 % of Cotti's: `sources: the hydrates' dissociation pressures`.

**Densities.** CuSO4·3H2O: 2.663 g/cm³ measured on synthetic bonattite (*Handbook of Mineralogy*,
handbookofmineralogy.org/pdfs/bonattite.pdf; the structure gives 2.679 at 100 K, COD 7061267).
CuSO4·H2O: 3.382 g/cm³ from its crystal structure at 100 K (COD 7061268, Ibrahim and Boeré,
*New J. Chem.* 46, 5479, 2022). No measured density was found. **Medium confidence.**

**Pitzer's equations** (GAME_SPEC §10).
- A^φ = 0.392 at 25 °C and b = 1.2: Pitzer and Mayorga, "Thermodynamics of electrolytes. II",
  LBL preprint (escholarship.org/uc/item/1rc6r4vm), p. 8.
- CuSO4:

  | β(0) | β(1) | β(2) | C^φ | α1 | α2 | Range |
  |---|---|---|---|---|---|---|
  | 0.2358 | 2.485 | −47.35 | 0 (published −0.0012) | 1.4 | 12 | to 1.4 mol/kg |

  Pitzer and Mayorga, "Thermodynamics of electrolytes. III. 2-2 electrolytes", J. Solution Chem.
  3, 539 (1974), LBL-2282 preprint Table I (escholarship.org/uc/item/0q16265x). The same values
  are in Simoes et al. 2016, Table 1.
  - **Their C^φ is left out (Stage 19).** Fitted below 1.4 mol/kg, where it is within the
    fit's noise, it makes a cubic term. Extrapolated, that term turns the water's activity back
    up beyond 65 mol/kg: in a liquor boiled nearly dry, evaporating would then drive itself.
    Without it the water's activity falls at every strength (a test holds it to 400 mol/kg).
    The fit's rms rises from 1.35 % to 1.49 %.
  - Other sets disagree: Downes and Pitzer 1975 (0.2340, 2.527, −48.33, 0.0044); Höffler 2017
    at 298.15 K (0.2086, 2.6587, −58.29, 0.01924).
  - Each was tried in the fit below. Pitzer and Mayorga's fits the solubility best (rms 1.35%,
    against 1.9% and 2.9%), and is used, without its C^φ.
- H2SO4 as fully dissociated:

  | β(0) | β(1) | β(2) | C^φ | α1 | α2 |
  |---|---|---|---|---|---|
  | 0.12593 | −8.17145 | −24.32491 | 0 | 4.5 | 12 |

  **Fitted, not tabulated.** Fitted to the osmotic and mean activity coefficients of Staples,
  J. Phys. Chem. Ref. Data 10, 779 (1981), Table 42 (srd.nist.gov/jpcrdreprint/1.555648.pdf), at
  16 molalities from 0.1 to 5 mol/kg, with rms 0.0046.
  - The published models treat the acid with bisulfate, and their parameters do not carry over.
    The negative β(1) and β(2) stand for the association the model leaves out.
  - At 1 mol/kg the model gives φ 0.7203 and a_w 0.9618, where Staples gives 0.7255 and 0.96155.
- No mixing parameters (θ, ψ) are used. The one published θ(Cu²⁺, H⁺), −0.023 (Wang et al.
  2012, as listed by Justel et al. 2020), belongs to a model with bisulfate.

**Oracles for the activity model** (never inputs).
- CuSO4's mean activity coefficient at saturation, 25 °C: 0.0369 (Wetmore and Gordon) and 0.0360
  (Müller and Reuther) at 1.413 mol/kg, as listed by Sibarani et al. 2022, Table 12. The model
  gives 0.0357 at 1.4 mol/kg.
- The CRC's mean activity coefficients (0.150, 0.104, 0.062 and 0.042 at 0.1, 0.2, 0.5 and 1.0),
  as reproduced by a university handout, are of low confidence in their unit; the model gives
  0.147, 0.102, 0.061 and 0.042.

**Solubility of CuSO4**, mass % of the anhydrous salt: CRC Handbook, "Aqueous solubility of
inorganic compounds at various temperatures" (after Söhnel and Novotny), as reproduced at
olympiads-xyz-bg.vercel.app (edition not shown). The row is "CuSO4 12.4 14.4 16.7 18.0 19.3 22.2
25.4 28.8 32.4 36.3 40.3 43.5" for 0, 10, 20, 25, 30 … 100 °C.
- Linke–Seidell gives about 2% more at 20–25 °C (1.2833 and 1.413 mol/kg, as listed by Justel et
  al. 2019).
- Miles and Menzies (1937) list the pentahydrate up to 369.05 K and the trihydrate from there
  (as listed by Höffler 2017, Table 12): the transition under the saturated solution is
  **95.9 °C**. Höffler's model puts it at 93.75 °C.

**The fit of CuSO4(aq)**, refitted in Stage 19:
- Its `Hf`, `S°` and `Cp` are fitted, by least squares in ln m, to make
  `CuSO4·5H2O(s) ⇌ CuSO4(aq) + 5 H2O(l)` with the activities above reproduce the eleven points
  from 0 to 90 °C: **−835.990 kJ/mol, −51.520 J/mol·K and 211.81 J/mol·K** (rms 1.49 % in molality).
- For reference, the ions' sum in NBS is −844.50 kJ/mol and −79.5 J/mol·K.
- The positive Cp is an effective value. It carries the temperature dependence of the activity
  coefficients, which the model holds at 25 °C.
- **Its heats of solution** (Stage 41, `chem_tool study solution_heat`; derived, not fitted, and no new data):
  - CuSO₄(s) → CuSO₄(aq) gives **−64.59 kJ/mol**, so the water warms.
  - CuSO₄·5H₂O(s) → CuSO₄(aq) + 5 H₂O(l) gives **+14.56 kJ/mol**, so the water cools.
  - NBS's ions (−844.50) would give −73.1 and +6.0 kJ/mol at infinite dilution. The game has no heat of
    dilution (GAME_SPEC §20), and the fit to solubility carries the solution's enthalpy where the demo uses it.
  - The signs, and which salt warms, are the measured ones.

| °C | 0 | 10 | 20 | 25 | 30 | 40 | 50 | 60 | 70 | 80 | 90 | 100 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|
| handbook, g/100 g water | 14.16 | 16.82 | 20.05 | 21.95 | 23.92 | 28.53 | 34.05 | 40.45 | 47.93 | 56.99 | 67.50 | 76.99 |
| the game, pentahydrate | 14.41 | 16.63 | 19.75 | 21.67 | 23.81 | 28.79 | 34.61 | 41.22 | 48.59 | 56.75 | 65.78 | 75.83 |
| the game, trihydrate | 83.5 | 78.0 | 73.8 | 72.1 | 70.7 | 68.6 | 67.2 | 66.6 | 66.7 | 67.3 | 68.4 | 69.9 |

Derived, not fitted:
- The two curves cross at **93.2 °C**, the transition, against 95.9 measured.
- The saturated solution boils at 104.8 °C, at 4.45 mol/kg.

**The common-ion effect.** Holler and Peffer, NBS Bulletin 13 (1916), at 15 °C: "395 grams of
crystallized copper sulphate … in 1 liter of the saturated solution … addition of 100 grams per
liter of sulphuric acid reduces the solubility … to 215 grams per liter". That is a ratio of 0.54
on a basis the text leaves unclear. The model gave 0.70 with 1.0 mol/kg of acid at 15 °C (Stage 19). With the acid's
bisulfate (Stage 63) the acid alone would raise the salt's solubility, 1.9 times at 1 mol/kg: the bisulfate takes up
the sulfate it adds while the ionic strength lowers the 2:2 activity coefficients. Copper's own interaction with the
bisulfate is needed (Baes, Reardon and Moyer 1993, J. Phys. Chem. 97, 12343, fitted β0 and β1 of Cu²⁺–HSO₄⁻ with
θ(Cu²⁺, H⁺) and ψ(Cu²⁺, H⁺, HSO₄⁻); their values were not reachable: the paper's full text only). One parameter,
Cu²⁺–HSO₄⁻ β0 0.815 (β1 and Cφ nought), is calibrated to Holler and Peffer's ratio: the model gives 0.537, and
0.82, 0.69, 0.54 and 0.33 of water's at 0.25, 0.5, 1 and 2 mol/kg (low confidence: one point, its basis unclear).

**Apparent molar volumes** (GAME_SPEC §10), from densities at 20 °C: Engineering Toolbox,
"Density of aqueous solutions of inorganic salts and acids" (engineeringtoolbox.com, d_1958).
That page is unsourced, so **low confidence**. Water is 0.99820 g/cm³ at 20 °C.

| Solute | Mass % | Density | Apparent molar volume (cm³/mol) |
|---|---|---|---|
| CuSO4 | 1, 5, 10 | 1.0085, 1.0511, 1.107 | −3.4, −1.1, 2.7 |
| H2SO4 | 1, 5, 10, 20 | 1.0049, 1.0318, 1.0661, 1.1398 | 32.8, 34.3, 35.7, 37.2 |

The game uses 2.7 for CuSO4(aq) and 35.7 for H2SO4(aq): each taken where the demo's solution is
(0.7 and 1.1 mol/kg).

**Colours of the hydrates** (for the calibrated spectra below):
- CuSO4·3H2O is "pale blue" (*Handbook of Mineralogy*, bonattite).
- Single crystals are "clear grayish blue" (CuSO4·3H2O), "clear light blue" (CuSO4·H2O) and "clear
  colourless" (CuSO4), from COD 7061267–7061269.
- No reliable source describes the monohydrate as a powder.

## Acids and bases (Stage 43)

Sodium hydroxide, its salt with the acid, and water's own ionisation, for the pH (GAME_SPEC §9). All of it
was read 2026-11-24.

**Thermodynamic rows.** NBS tables (Wagman et al., *J. Phys. Chem. Ref. Data* 11, Suppl. 2, 1982), read
from the scanned pages, 298.15 K, 0.1 MPa:

| Species | NBS row, page | ΔfH° kJ/mol | S° J/mol·K | Cp° J/mol·K |
|---|---|---|---|---|
| OH⁻(aq) | ao, 2-38 | −229.994 | −10.75 | −148.5 |
| Na⁺(aq) | ao, 2-299 | −240.12 | 59.0 | 46.4 |
| SO₄²⁻(aq) | ao, 2-57 | −909.27 | 20.1 | −293 |
| NaOH(aq), `sodium_hydroxide_aq` | ai, 2-299 | **−470.114** | **48.1** | **−102.1** |
| Na₂SO₄(aq), `sodium_sulfate_aq` | ai, 2-307 | **−1389.51** | **138.1** | **−201** |

- Each salt's row is the sum of its ions, except NaOH's S°: the ions sum to 48.25, and NBS tabulates 48.1.
- The rows are NBS's, as the acid's are (its SO₄²⁻), and not CODATA's (Cox, Wagman and Medvedev 1989:
  OH⁻ −230.015, Na⁺ −240.34). One table, not two.
- Neither salt has an apparent molar volume in the game. Each takes no room (NaOH's is about −5 cm³/mol at
  infinite dilution): a declared simplification.
- **The heat of the neutralisation follows.** H₂SO₄(aq) + 2 NaOH(aq) → Na₂SO₄(aq) + 2 H₂O(l) gives
  **−111.672 kJ**, that is, −55.836 kJ for each water. That is the ionisation of water backwards.
  - NBS's "in n H₂O" rows (pp. 2-58, 2-300, 2-307) give −133.6 kJ for the same reaction at about
    0.55 mol/kg.
  - Most of the difference is the bisulfate the acid holds, which dissociates as it is neutralised:
    HSO₄⁻ → H⁺ + SO₄²⁻ gives −21.93 kJ/mol (NBS).
  - The game's acid is fully dissociated (GAME_SPEC §20), so it gives the infinite-dilution heat.

**Water's ionisation.** `solutions.json`'s `water_ionisation`, H₂O(l) → H⁺(aq) + OH⁻(aq), from NBS's OH⁻ and
H₂O(l) rows (H₂O: −285.830, −237.129, 75.291):

| Quantity | Value |
|---|---|
| ΔH° | **55.836 kJ/mol** |
| ΔG° | **79.885 kJ/mol** (pKw 13.995 at 25 °C) |
| ΔCp | **−223.8 J/mol·K**, held constant |

Against **IAPWS R11-24** (2024, which replaced R11-07; Arcis et al., *J. Phys. Chem. Ref. Data* 53, 023103),
pKw at saturation, Table 3, stated uncertainty 0.05:

| °C | 0 | 25 | 50 | 75 | 100 |
|---|---|---|---|---|---|
| IAPWS R11-24 | 14.95 | 13.99 | 13.26 | 12.70 | 12.25 |
| the model | 14.937 | 13.995 | 13.275 | 12.724 | 12.303 |

Gate: within 0.06 at each, `measured: water's ionisation constant is IAPWS R11-24's`.

**Pitzer's parameters** (`solutions.json`'s pairs), 25 °C, α 2, b 1.2. From Pitzer and Mayorga, *J. Phys. Chem.*
77, 2300 (1973), the LBL preprint's tables, read:

| Pair | β0 | β1 | Cφ | Valid to |
|---|---|---|---|---|
| Na⁺/OH⁻ (Table I) | 0.0864 | 0.253 | 0.0044 | 6 mol/kg |
| Na⁺/SO₄²⁻ (Table VI) | 0.01958 | 1.113 | 0.004974 | 4 mol/kg |

- Table VI prints Na⁺/SO₄²⁻ as (4/3)β0 0.0261, (4/3)β1 1.484 and (2^(5/2)/3)Cφ 0.00938. The β0, β1 and Cφ
  above are those divided back.
- There are no mixing terms (θ, ψ), as for the other pairs.

Mean activity coefficients, the gate at 2.5 % (`measured: sodium hydroxide's and sodium sulfate's mean
activity coefficients`):

| Solution | Source | γ± |
|---|---|---|
| NaOH 0.5 mol/kg | Hamer and Wu, *J. Phys. Chem. Ref. Data* 1, 1047 (1972), Table 19 | 0.685 |
| NaOH 1.0 mol/kg | Hamer and Wu, Table 19 | 0.674 |
| Na₂SO₄ 0.1 mol/kg | Goldberg 1981, *J. Phys. Chem. Ref. Data* 10, 671 | 0.4457 |
| Na₂SO₄ 0.5 mol/kg | Goldberg 1981 | 0.2684 |
| Na₂SO₄ 1.0 mol/kg | Goldberg 1981 | 0.2040 |

**The pH** is derived, not stored (GAME_SPEC §9). It is −log₁₀ of the hydrogen ions' activity:
- Their molality is what is left of the acid's over the base's, and water's own ions solve
  (m_H + x)(m_OH + x)·γ_H·γ_OH = Kw·a(H₂O).
- Each γ is Pitzer's single-ion coefficient: a convention, as every single-ion activity is.
- The acid's fitted activity gives 0.01 mol/kg sulfuric acid a pH of 1.86, against about 1.87 measured. There,
  the bisulfate holds a share of the hydrogen back.

**Not used (not verified):** the CRC's limiting diffusion coefficients of Na⁺ and OH⁻ (1.334e-9, 5.273e-9 m²/s)
could not be read from the CRC page. They are not needed: no surface reaction of these ions is modelled.

## Precipitation and gas (Stage 44)

Copper hydroxide, the precipitate a base brings out of copper sulfate; its turning into copper oxide when
heated; malachite dissolving in an acid, its carbonate leaving as carbon dioxide (GAME_SPEC §9). All of it was
read 2026-11-24.

**Copper hydroxide's thermodynamic row**, `copper_hydroxide`, Cu(OH)₂(s). From Puigdomenech and Taxén,
*Thermodynamic data for copper* (SKB TR-00-13, 2000), Table 2, p. 70, which takes it from Beverskog and
Puigdomenech 1997:

| Quantity | Printed | Used |
|---|---|---|
| ΔfG° | −359.92 kJ/mol | (with S°, gives ΔfH°) |
| S° | 87.0 J/mol·K | **87.0** |
| Cp | 86.99 + 23.26e-3 T − 0.54e6 T⁻² | **87.85** at 25 °C, held constant |
| ΔfH° | — | **−443.99** kJ/mol, from ΔfG° and CODATA's entropies of Cu, O₂ and H₂ |

- **Why not NBS or JANAF.** NBS 1982 (p. 2-154) gives Cu(OH)₂ only ΔfH° −449.8, with no ΔfG°, S° or Cp.
  JANAF (1966, Cu-014) gives −450.366, S° 108.366: with it the hydroxide is 7.2 kJ *more* stable than CuO and
  water, and its log \*Ks would be 6.41, against every measured solubility below.
- **The same source gives the demo's CuO.** Its CuO(cr), ΔfG° −128.29 and S° 42.6, is the game's CuO row
  (NIST: Chase, ΔfH° −156.06, S° 42.59). Robie and Hemingway 1995 (USGS Bull. 2131, p. 188): tenorite −156.1,
  42.60.
- **So the hydroxide is metastable.** Cu(OH)₂ → CuO + H₂O(l) is −5.51 kJ/mol (from minteq's constants −5.88,
  from wateq4f's −5.82). Cudennec and Lecerf 2003 (*Solid State Sciences* 5, 1471; the HAL author version): the
  hydroxide turns into the oxide, slowly in water, fast in a strong base.

**The gate: the solubility constants**, `measured: the tables' solubility constants of copper hydroxide and
copper oxide are the evaluated ones`. log \*Ks of M + 2 H⁺ → Cu²⁺ + water at 25 °C, I = 0, through the game's
own CuSO₄(aq) and H₂SO₄(aq):

| Solid | minteq.v4 (NIST SRD 46) | wateq4f (Ball and Nordstrom 1991) | llnl.dat | the model | Gate |
|---|---|---|---|---|---|
| Cu(OH)₂ | 8.674 (ΔrH −56.42 kJ) | 8.64 (−15.25 kcal) | — | **8.551** (ΔrH −54.39) | 0.15 |
| CuO | 7.644 (ΔrH −64.87) | 7.62 (−15.24 kcal) | 7.656 | **7.585** (ΔrH −56.49) | 0.1 |

- The solubility product follows: log Ksp = 8.551 − 2 × 13.995 = **−19.44**, Ksp 3.6e-20 (from minteq's 8.674,
  −19.32).
- Schindler et al. 1965 (*Helv. Chim. Acta* 48, 1204), the measurement itself, is behind a paywall: not read.
- Measured in pure water (Cudennec and Lecerf 2003): Cu(OH)₂ 1.3e-5 mol/L, CuO 2e-7 mol/L.

**Density**, 3.93 g/cm³: the Handbook of Mineralogy's spertiniite, D(meas) 3.93(2), D(calc) 3.94. The cell
Fukuda et al. cite (Oswald et al. 1990) gives 3.949.

**A precipitate's grain**, `grain_um` 0.3 (calibrated). Fukuda, Favergeon and Koga's copper hydroxide (below) had
a BET area of 5.28 m²/g, needles 3 to 5 µm long. The sphere of that area at 3.93 g/cm³ is 6/(ρ·A) = 0.29 µm.
A precipitate made in a beaker is not that sample, so this is the size it is given, not a measurement of it.

**Copper hydroxide into copper oxide**, `hydroxide_dehydration`, Cu(OH)₂(s) → CuO(s) + H₂O(g), first order.
Fukuda, Favergeon and Koga, *J. Phys. Chem. C* 123, 20903 (2019; the HAL version), quoting their 2018 fit in dry
N₂, three steps in sequence:

| Step | Ea kJ/mol | ln(A / s⁻¹) |
|---|---|---|
| induction | 196.8 ± 16.7 | 54.3 ± 5.1 |
| **surface reaction** | **127.0 ± 6.0** | **32.9 ± 1.8** |
| phase boundary | 124.6 ± 6.0 | 31.4 ± 1.8 |

- The game takes the surface reaction as the whole: prefactor e^32.9 = 1.9448e14 /s, Ea 127.0 kJ/mol.
- Water vapour slows it, by [P₀/p(H₂O)]^0.41 in their data. In the game, steam over it lowers its affinity,
  through the same steam's activity.
- What follows: a half-life of **2 years at 25 °C**, under water as dry (Cudennec: "can stay several months
  in pure water at room temperature"); **36 min at 100 °C**; **17 s at 150 °C**; their mass loss begins at
  about 420 K.
- Gate: `measured: copper hydroxide turns to oxide at Fukuda's first-order rate`, at 400 K in dry nitrogen.
- **Not modelled:** in a strong base the hydroxide dissolves as Cu(OH)₄²⁻ and comes back as CuO, "very
  fast" (Cudennec). No rate was found, so a boiled suspension in excess base blackens as slowly as in water.

**Malachite in sulfuric acid**, `malachite_dissolution`, Cu₂CO₃(OH)₂(s) + 2 H₂SO₄(aq) → 2 CuSO₄(aq) + CO₂(g) +
3 H₂O(l), the surface law. **Nicol 2018**, *Hydrometallurgy* 177, 214 (the Murdoch accepted manuscript): pure
gem malachite, sieved, 1 g in 600 mL, 580 rpm, 0.033 to 0.15 M acid, 25 to 45 °C.
- A shrinking particle controlled at its surface: 1 − (1 − X)^⅓ = t/τ, τ ∝ R.
- Order 0.65 in [H₂SO₄], first order in the proton's activity; Ea **53.6 kJ/mol** (eq. 4: exp(−6447/T)).
- Eq. 4: dR/dt = A exp(−6447/T) C^0.65, **A 1.44e7 cm/s** with C in mol/cm³. His k = 1.05e-4 cm/s is A·exp·ρ.
  "4.1 × 10⁻⁷ mol cm⁻² s⁻¹" in 0.2 mol/L acid at 25 °C is malachite's flux by his density; Kopylov and
  Orlov 1964 measured 3.6e-7.
- The game's prefactor is that shrinking rate over its own molar volume (3.8 g/cm³), with C in mol/kg:
  1.44e5 m/s × (1e-3)^0.65 × 17186 mol/m³ = **2.7767e7 mol/m²/s**, Ea 6447 R = **53.603 kJ/mol**, order 0.65 in
  the acid's molality.
- At 0.2 M and 25 °C: 3.97e-3 mol/m²/s (his 4.1e-3; Kopylov and Orlov's 3.6e-3).
- Gate: `measured: malachite dissolves in sulfuric acid at Nicol's surface rate`, at 0.1 and 0.2 M, 25 and 45 °C.
- Bingöl and Canbazoğlu 2004 (*Hydrometallurgy* 72, 159) is behind a paywall: not read.
- Transport in series with it (GAME_SPEC §7) takes over for coarse grains in still acid. Nicol's grains were
  kept in suspension at 580 rpm.

**Colour** (calibrated): no measured spectrum of copper hydroxide was found. The Handbook of Mineralogy says
"blue to blue-green", strongly pleochroic. The game gives it a diffuse reflectance of 0.18 with a band at
480 nm, 75 nm wide, peaking at 0.6 above it: a pale blue, lighter and a little greener than the pentahydrate.

**Not used (not verified):** an absorption peak at 600–650 nm on an AI-generated page, rejected.

## Boiling's bubbles (Stage 45)

What a vessel's heated floor does (GAME_SPEC §11): its natural convection, the onset of its bubbles, how
large they leave, how fast they rise, how fast they condense in a cooler bulk, and what they do to light.
`data/boiling.json`, all of it read 2026-11-24.

**Water's properties**: Lienhard, *A Heat Transfer Textbook* 6.00 (2024, ahtt.mit.edu), **Table A.3**,
saturated liquid water, the rows from 0.01 to 147 °C as printed (ρ, cp, k, α, ν, Pr, β). Stage 45 kept them in
`data/boiling.json`; from Stage 47 they are the core's, `data/solutions.json`'s `solvent` (`chem::solvent_at`),
for a column's buoyancy and conduction as well as the bubbles. His water data are from
IAPWS's formulations ("in agreement with IAPWS recommendations through 2007"). Read between rows.

**Surface tension**, `surface_tension`: Lienhard's **eq. 9.2a** (IAPWS), σ = 235.8 (1 − T/Tc)^1.256 [1 − 0.625
(1 − T/Tc)] mN/m, Tc 647.096 K: 58.9 mN/m at 100 °C (his Example 9.2's 0.0589), 72.0 at 25 °C.

**Natural convection off the floor** (code, `lab::bubbles::convection`): a plate heated from below, facing up,
the length A/P (Lloyd and Moran 1974).
- Below Ra = 1e7: Lienhard **eqs. 8.37a–b** (Raithby and Hollands): Nu = 0.560 Ra^¼ / [1 + (0.492/Pr)^9/16]^4/9,
  and where Nu < 10, Nu = 1.4 / ln(1 + 1.4/Nu). The text extracted from the PDF loses the fraction bars; read
  as these, each tends to the other's limit (1.4/ln(1 + 1.4/Nu) → Nu at large Nu), and the Prandtl factor
  divides, as in every Churchill–Usagi form.
- Above: **eq. 8.35**, Nu = 0.14 Ra^⅓ (1 + 0.0107 Pr)/(1 + 0.01 Pr).
- Against Lloyd and Moran's 0.54 Ra^¼ (measured by mass transfer, Sc 2200): within 10 % at water's Prandtl
  numbers. A 66 mm beaker floor at 4 K: Ra 1.3e6, h 650 W/m²K.

**The onset** (the floor's surface, calibrated): a bubble starts where the floor's largest cavity holds a
nucleus, 2σ/r = a_w p_sat(T) − p (Lienhard **eq. 9.1**). No measured onset superheat on glass was found. Lienhard's
**Example 9.1** pairs 8 K of superheat with a cavity of 0.0035 mm, and the game gives glass that cavity:
`cavity_um` 3.5. Chang's boiling-point experiments (Cambridge HPS, "The Myth of the Boiling Point") show water
in Pyrex beakers boiling above 100 °C, up to about 101 °C in the bulk; boiling stones bring it back. A bulk that
superheats is not modelled yet (GAME_SPEC §20).

**Nucleate boiling**: Rohsenow, Lienhard **eq. 9.4**: c_p ΔT/(h_fg Pr^s) = C_sf [q/(μ h_fg) √(σ/g Δρ)]^0.33.
- Table 9.2 gives C_sf for water on copper and platinum 0.013, nickel and brass 0.006, s = 1.0; none for glass
  (Pioro 1999's fuller table was not read). Glass takes **0.013** (calibrated), s 1.0.
- Gate: Lienhard's **Example 9.2**: C_sf 0.016 at 22 K gives 800 kW/m² (his Fig. 9.2's heater).
- Its typical error is 100 % in q and 25 % in ΔT (Lienhard).

**A bubble's size as it leaves**: **Fritz** (1935), d = 0.0208 θ √(σ/(g Δρ)), θ in degrees (as quoted in
"Life beyond Fritz", arXiv:2406.01989, eq. 9). No contact angle of water on lab glass was found; **45°**, Fritz's
own for water (calibrated): 2.34 mm at 100 °C.

**Its rise**: **Baz-Rodríguez et al. 2012** (*Rev. Mex. Ing. Quím.* 11, 269), eq. 1, V = (V₁⁻² + V₂⁻²)^−½: V₁
the potential flow's with its viscous correction (eqs. 2, 6), V₂ Lehrer's wave analogy, √(3σ/ρd + g d/2)
(eq. 8). Against measured pure water: their speed's extrema lie at about 0.15 and 0.65 cm across (Mendelson
1967, Loth 2010), theirs at 0.17 and 0.67; the game's at 0.161 and 0.665 cm, a peak of 34 cm/s
(`measured: a bubble's rise peaks near 0.15 cm`).

**Its condensing**: **Chen and Mayinger 1992** (*Int. J. Multiphase Flow* 18, 877), Nu = 0.185 Re^0.7 Pr^0.5,
400 < Re < 1e4, read in Herry et al. (MHMT 2024), not in the paper itself. A bubble shrinks as
dd/dt = −2 h ΔT_sub/(ρ_v h_fg), and is gone at 5 % of its first diameter. Below Re 400 (its last moments) the
correlation is used past its range. Isenberg and Sideman 1970, Warrier et al. 2002, Kim and Park 2011 and
Kalman 2003 are behind paywalls: not read.

**What they do to light**: bubbles far larger than the wavelength have an extinction efficiency of **2** (Flatau
et al. 2000, citing Bohren and Huffman), and scatter with little spectral dependence (Piskozub et al. 2004). Their
scattering goes mostly forward: the asymmetry parameter g is about **0.85** for bubbles in water (Flatau et al.
2000, m = 0.75 averaged over size parameters 100 to 300; a 2 mm bubble's is about 1.5e4, so medium confidence).
The game's bubbles are a haze of what their scattering turns away from its path, the reduced extinction
2 · (1 − g) · n · πd²/4 (the similarity relation): the light they bend a little goes on with the rest. They are a
**scatter medium** in `data/optics/media.json`'s scatterers, `bubbles`: a single-scattering albedo of 1 at every
wavelength (they absorb nothing), an efficiency of 2 and an asymmetry of 0.85; the pass reads the albedo as a
row of a vessel's spectra, as it reads every other medium's.

**Water's own boiling** is the table's (GAME_SPEC §8): the floor's boiling point is the table's saturation at the
pressure there, the room's plus ρ g h, lowered by the solution's water activity.

## Nickel sulfate (Stage 47)

Green nickel sulfate in solution, `nickel_sulfate_aq`: the second coloured liquid, for a column that holds two apart
(GAME_SPEC §6). In solution only: no solid nickel salt is made. All of it read 2026-11-24.

| Datum | Value | Source | Gate |
|---|---|---|---|
| Ni's atomic weight | 58.693 (abridged; 58.6934(4)) | CIAAW | molar masses |
| NiSO4 "ai" ΔfH°, S° | −963.2 kJ/mol, −108.8 J/mol·K (ΔfG° −790.3) | NBS 1982, p. 2-167; the sum of Ni²⁺ "ao" (−54.0, −128.9, p. 2-166) and SO₄²⁻ | the cited rows |
| Its heat capacity | 211.81 J/mol·K, a stand-in | NBS gives none: copper sulfate's (calibrated) | |
| Pitzer's pair Ni²⁺/SO₄²⁻ | β0 0.1702, β1 2.907, β2 −40.06, Cφ 0.0366, α1 1.4, α2 12 | Pitzer and Mayorga 1974 (LBL-2282, Table I), to 2.5 mol/kg | γ± against the CRC's 0.150, 0.105, 0.063, 0.043 at 0.1, 0.2, 0.5, 1.0 mol/kg, within 5 % |
| Ni²⁺'s diffusion at infinite dilution | 6.61e-10 m²/s | the CRC (Vanýsek): ½Ni²⁺ λ 49.6, D 0.661e-5 cm²/s, read second-hand (a search snippet); RTλ/zF² gives 6.60e-10 | |
| Its apparent molar volume | 0.81 cm³/mol | Krumgalz, Pogorelsky and Pitzer 1996 (JPCRD 25, 663), Table 5: V° −15.22 and its Pitzer volume terms, at 1 mol/kg (the game holds one volume; at 0.5 mol/kg the source's is −2.66) | density at 1 mol/kg 1.1504 g/cm³, within 0.3 % |
| Its colour | three Gaussian bands in wavenumber, 25 381, 15 385, 13 928 cm⁻¹ (FWHM 2200, 1800, 1800; peaks 4.72, 1.504, 1.815) | Shigematsu et al. 1960, Table 1: 394 nm 4.72, 650 nm 1.80, 718 nm 2.06 L/mol/cm (Ni²⁺ in water and in sulfuric acid alike); the widths read from his Fig. 1 (calibrated); Burke et al. 1972 (J. Res. NBS 76A, 469): 5.15 at 394 nm in sulfate | each maximum within 3 to 5 %, and within 10 % of Burke's |

**A fault found on the way.** Pitzer's third-virial term in the game read 2 m_c m_a (B + Z C), with Z over every
ion, where it is m_c m_a (2B + Z C) (Pitzer 1991). C counted twice. No pair with a Cφ had been gated where it
shows: copper sulfate's and the acid's Cφ are 0, and sodium hydroxide's and sulfate's are small. Nickel sulfate's
γ± at 1 mol/kg read 9 % over the CRC's, 5.6 % over Pitzer and Mayorga's own. Fixed in Stage 47; the sodium salts'
gates (Stage 43) hold as before.

## Measured kinetics (Stage 23)

Each rate from a measurement, read 2026-11-21, with an oracle test at the source's own conditions
(`native/tests/test_measured.cpp`). The fits were made through the model itself: its own
solubility, activities and affinity (GAME_SPEC §7).

| Process | Law | Parameters | Source and fit | Confidence |
|---|---|---|---|---|
| Malachite decomposing | first order in malachite | 6.05e11 /s, Ea 147.6 kJ/mol | Aoki, Yamamoto and Koga, *PCCP* 23, 15107 (2021), ESI Table S2: the second, main step (about 75% of the mass loss; Šesták–Berggren exponents m −0.13, n 1.04, p 0.75, close to first order), 5 mg in dry N₂. The first step (1.15e12 /s, 25% of the loss) is left out. Oracle: their isothermal run at 498 K, α = 0.99 after 589 min (ESI Fig. S8): 0.9991 | high |
| CuO dissolving in sulfuric acid | at the surface, half order in the acid's molality; transport in series | 1.1417e8 mol/m²/s, Ea 69 kJ/mol | 2.5e-7 mol/cm²/min (4.17e-5 mol/m²/s) in 0.2 M acid at 25 °C, a rotating disk (Nicol and Akilan, *Hydrometallurgy* 178, 7, 2018, Table 4, citing Majima et al., *Metall. Trans. B* 11, 209, 1980); half order in H⁺ for H₂SO₄, surface controlled, independent of agitation (Majima's abstract). k(25 °C) = 4.17e-5 / 0.2^½ = 9.317e-5; Ea the middle of the abstract's 52–86 kJ/mol across four acids (which is sulfuric acid's is not said). Oracle: 4.17e-5 at Majima's conditions, twice it at four times the acid, stirring under 1% faster for 10 µm grains | **low**: second-hand, the table's time unit doubtful (its malachite entry is five times another of Nicol's), and the order is in H⁺'s activity there, the acid's concentration here |
| Pentahydrate growing and dissolving | at the surface, transport in series | 3.105e8 mol/m²/s, Ea 58.79 kJ/mol | Tanimoto, Kobayashi and Fujita, *Kagaku Kogaku* 27, 424 (1963): the surface step N = kr (C − C_s), kr = 0.030, 0.063, 0.120 m/h at 20, 30, 40 °C (Table 1), in series with mass transfer (Eqs. 3, 4, 13). Fit: k_s = kr ΔC / f(A) at 1% over the model's saturation at each temperature (0.01027, 0.02349, 0.04789 mol/m²/s), then Arrhenius through the three (within 2.2%). Oracle: kr ΔC at each, within 3% | high for 20–40 °C; used at every temperature |
| Pentahydrate nucleating | with the above | 0.3269 per kg of water per second × (S − 1)^3.109, S = e^(A/RT) | Sheng, PhD thesis, Leeds (2012), Table 5.2: induction times 6,120–7,200 s at S = 1.080 (26 °C), 10,600–20,000 s at 1.058 (27 °C), 68,000–84,000 s at 1.036 (28 °C), a litre saturated at 30 °C. Fit: each as one nucleus in 0.98 kg of water over the geometric mean time; S converted to the model's activity ratio at Sheng's concentration (the model's own S there is 1.0787, 1.0585, 1.0387). Least squares through the three (within 12%) | **medium, a lower bound**: a nucleus must grow before it is seen, so the true rate is higher; extrapolated far past S = 1.09. Lyall et al. (2004) give a metastable zone of 3–4 °C at 0.2–0.55 °C/min and an order near 1.7; Tanimoto et al. (1962) first nucleation near 3.5 wt% over saturation |
| The trihydrate, monohydrate and anhydrous salt growing, dissolving and nucleating | surface, with nucleation | the pentahydrate's | no source gives them apart | **calibrated**: assumed equal |
| The hydrates losing water, 5 → 3, 3 → 1, 1 → 0 | first order | Ea 71.26, 112.12, 165.23 kJ/mol; prefactors 1.926e8, 1.1198e13, 2.4291e14 /s | Ea: Cheng et al., *J. Therm. Anal. Calorim.* 135, 2697 (2019), abstract (n-th order, Ozawa and nonlinear regression; n 0.09, 0.18, 0.90). Each prefactor puts the step's first-order peak, k = β Ea / (R Tp²), at its DTG peak at 10 °C/min: 363.14, 393.39, 529.08 K (Yu Yuye, *Asian J. Chem.* 19, 2023, 2007, Table 2, 5–10 mg in flowing nitrogen). Cheng's own log₁₀ A (8.40, 13.23, 15.09) are within a factor of 1.3, 1.5 and 5.1 of these | medium |

| Transport | Value | Source | Check |
|---|---|---|---|
| Ions' diffusion coefficients in water at infinite dilution, 25 °C (`data/solutions.json`, `diffusion`) | H⁺ 9.31e-9, Cu²⁺ 7.33e-10, SO₄²⁻ 1.07e-9 m²/s | Li and Gregory, *Geochim. Cosmochim. Acta* 38, 703 (1974), Table 1 | Nernst–Hartley gives CuSO₄ 8.70e-10 and H₂SO₄ 2.61e-9 m²/s: `measured: water's viscosity, and the ions' diffusion` |
| Water's viscosity, 0.01–150 °C (`data/solutions.json`, `viscosity`) | 17 points, 1.7902 mPa·s at 0.01 °C to 0.18262 at 150 °C; 0.88997 at 25 °C | NIST Chemistry WebBook, fluid properties at 0.5 MPa (Huber et al., IAPWS 2008, *J. Phys. Chem. Ref. Data* 38, 101, 2009), read 2026-11-21 | 45 °C read between points, 0.5973, against the WebBook's 0.59584 (0.25%) |
| The Sherwood number (Stage 57; until then 2, times 3 stirred, calibrated) | Sh = 2 + 0.4 (ε d⁴ / ν³)^(1/4) Sc^(1/3), ε the stirring's dissipation per kg of the liquid (the lab's, Stage 49), ν and Sc from the viscosity above and each species' diffusion; 2 at rest | Sano, Yamaguchi and Adachi, *J. Chem. Eng. Japan* 7(4), 255–261 (1974): suspended particles of 60–1100 µm (ion-exchange beads; dissolving granules of benzoic acid, KMnO₄, β-naphthol) in stirred vessels 9.5 to 40 cm across, within 30.8 % in Sh. Harriott, *AIChE J.* 8, 93 (1962): in agitated tanks 1.5 to 8 times a fixed sphere's at its terminal velocity | `measured: a stirred crystal dissolves by transport at Sano's Sherwood number, many times a still one's`: 25, 43 and 76 for a 0.53 mm crystal at 0.01, 0.1 and 1 W/kg |
| A crystal dissolving (Stage 57) | at transport's pace: its surface step 1000 times its growth's, in series (the copper sulfates' `dissolution: "transport"`; **calibrated**: large enough that transport sets the pace at any stirring, and the surface's factor still stops it at equilibrium) | Mullin, *Crystallization*, 4th ed. (2001): dissolution is generally diffusion controlled, growth by integration at the surface besides; for the pentahydrate even growth is 58 to 99 % transport's (Leeds, *Crystallisation of copper sulphate pentahydrate from aqueous solution in absence and presence of sodium chloride*, 2019) | `measured: a crystal in water dissolves by transport` |

The kinetics calibrated for play before Stage 23, for the record: the oxide 1.15e6 mol/m²/s with
Ea 50 kJ/mol, times the stirring; every hydrate's growth 3e-2 mol/m²/s with no activation energy,
and nucleation 2e6 per kg per second × (S − 1)³ (about 140,000 crystals of 0.3 mm from the demo's
flask, against about 1,150 of 1.9 mm now); the dehydrations Ea 80 kJ/mol each; malachite's
prefactor 4.7e10 /s with Ea 150 kJ/mol.

Dehydration temperatures in air, for comparison: two waters lost at 63 °C, two more at 109 °C,
the last at 200 °C (Galwey and Green 1999; Wiberg 2001; as cited by Wikipedia).

## Verified densities and walls

Densities give the molar volumes of solids and liquids (GAME_SPEC §6). Retrieved 2026-11-15.

| Species | Density (g/cm³) | Source |
|---|---|---|
| H2O(l) | 0.997048 at 25 °C, 1 atm | NIST Chemistry WebBook, fluid properties (Wagner and Pruss, IAPWS-95, J. Phys. Chem. Ref. Data 31, 387, 2002) |
| CuSO4(s) | 3.603 at 25 °C | Merck, copper(II) sulfate anhydrous, product 102791 |
| CuO(s) | 6.315 | Wikipedia's infobox for copper(II) oxide (no citation there); mineral references give tenorite 5.8–6.5. **Medium confidence.** The whole of the demo's CuO is 0.57 mL |
| Cu2CO3(OH)2(s) | 3.8 | The middle of the 3.6–4.0 range for malachite (Handbook of Mineralogy, Mindat, Webmineral, as cited by Wikipedia's infobox). **Medium confidence.** 5 g is 1.3 mL of a 250 mL flask |
| Glass (wall) | 2.23 | ISO 3585. Not used: a wall takes no room inside its vessel |

Check: each molar volume the build derives is the molar mass over these, the hydrates' included:
`sources: molar masses are IUPAC's atomic weights, and molar volumes the cited densities`. The
apparent molar volumes follow from the densities above within 3 %: `sources: the solutes' apparent
molar volumes`.

| Wall | Cp (J/mol·K) | Source |
|---|---|---|
| Latex, as C5H8 (natural rubber, the isoprene unit, 68.119 g/mol) | 130.1 | 1.91 J/g·K, the low end of NETZSCH's 1.91–2.08 J/g·K for natural rubber (analyzing-testing.netzsch.com); another compilation gives 1.55. A 2 g balloon is 3.8 J/K. ΔHf° and S° are set to 0: a wall never reacts, so they cancel |

Malachite's sources disagree on the enthalpy of formation from the oxides: −63.2 kJ/mol
(Richardson and Brown, used here) against −52.5 ± 5.9 (Kiseleva et al., oxide-melt calorimetry).
The difference moves the temperature above which decomposition is spontaneous by about 30 K and
does not change the demo.

## Verified kinetics

| Reaction | Law | Parameters | Source |
|---|---|---|---|
| Malachite decomposition | first order in malachite | Ea = 147.6 kJ/mol; prefactor 6.05e11 /s (Stage 23, above) | Koga's, inside the 131–159 kJ/mol (31.4–38.1 kcal/mol) of Seguin, *Canadian Mineralogist* 13, 127 (1975), whose rate constants are only drawn |

**Oracle checked in Stage 5:** water's saturation properties, NIST Chemistry WebBook (IAPWS-95,
Wagner and Pruss 2002), retrieved 2026-11-16, against what the table implies:

| T (K) | NIST p_sat (kPa) | Table p_sat (kPa) | NIST ΔH_vap (kJ/mol) | Table ΔH_vap (kJ/mol) |
|---|---|---|---|---|
| 298.15 | 3.16993 | 3.167 | 43.99 | 44.00 |
| 323.15 | 12.3519 | 12.31 | 42.91 | 42.96 |
| 348.15 | 38.5954 | 38.27 | 41.81 | 41.91 |
| 373.15 | 101.418 | 99.78 | 40.65 | 40.87 |

Normal boiling point: NIST 373.1243 K; the table gives 373.586 K (100.44 °C). The difference is
steam treated as an ideal gas with a constant heat capacity.

**Oracle checked in Stage 28: boiling at a pressure.** NIST Chemistry WebBook, saturation properties
of water by pressure (IAPWS-95), retrieved 2026-11-22, printed rows (6 digits). A sealed 250 mL flask
holding 249 mL of water and nothing else stands at its saturation pressure; the model's against
NIST's, at NIST's temperature (`native/tests/test_pressure.cpp`, tolerance 2.5%; confidence high):

| T (K) | NIST p_sat (kPa) | Model, sealed (kPa) |
|---|---|---|
| 318.656 | 9.84724 | 9.817 (−0.3%) |
| 332.990 | 19.7990 | 19.70 (−0.5%) |
| 354.179 | 49.4258 | 48.93 (−1.0%) |
| 372.876 | 100.431 | 98.82 (−1.6%) |
| 392.820 | 196.604 | 191.7 (−2.5%) |

The model falls further below the steam tables as the temperature rises: steam an ideal gas with a
constant heat capacity. A sealed flask of boiling water cooled to 60 °C stands at 19.84 kPa (NIST
19.946 at 333.15 K, −0.5%).

**Oracle checked in Stage 3:** from the table the decomposition absorbs 106.5 kJ/mol and makes
321.5 J/mol·K, so it is spontaneous above about 58 °C with both gases at 1 bar: stable on the
shelf, unstable well below red heat, as observed.

**Oracle checked in Stage 2:** water's heat of vaporisation at 25 °C from the table,
44.004 kJ/mol, against CODATA's 43.99 ± 0.07.

## Unverified draft

Every value from here to the end of the file was written from memory during planning and has
**not** been checked against a reference. None is used by code. The stage that needs a row
verifies it against the CRC Handbook, the NIST Chemistry WebBook or NBS tables, records the
citation above, and only then uses it.

Confidence: **high** (well-known tabulated value), **medium** (tabulated, but sources differ or
recall is uncertain), **low** (estimate or game calibration).

### Species still to verify

| Species | M (g/mol) | ΔHf° (kJ/mol) | Cp (J/mol·K) | S° (J/mol·K) | ρ (g/cm³) | Confidence | Needed by |
|---|---|---|---|---|---|---|---|

### Oracles (test references, never inputs)

**Hydrate dissociation:** verified in Stage 19 (above). In air, onsets near 63 °C (5→3), 109 °C
(3→1) and 200 °C (1→0) (Wikipedia, after Galwey and Green 1999; one secondary source).

### Kinetics (game-calibrated)

| Process | Form | Parameters | Confidence |
|---|---|---|---|

## Optics (Stage 10)

**Verified tables** (`data/optics/`, embedded at build):

| Table | Source | Check |
|---|---|---|
| CIE 1931 2° colour-matching functions, 380–780 nm at 1 nm (`cie_1931_2deg.csv`) | CIE, as distributed by the Colour & Vision Research Laboratory (cvrl.org) | Σȳ and the white point below |
| CIE standard illuminant D65, 380–780 nm at 1 nm (`d65.csv`) | CIE, as distributed by cvrl.org | Its chromaticity under the observer above comes out (0.3127, 0.3290): `colour: the illuminant is white` |
| sRGB primaries and transfer function | IEC 61966-2-1 | White maps to (1, 1, 1) within 0.2% |
| CIEDE2000 | Sharma, Wu and Dalal (2005) | Four of the paper's test pairs to four decimals: `colour: CIEDE2000 agrees with the published test pairs` |
| Planck's law: c₁ = 2hc² = 1.191042972e-16 W·m²/sr, c₂ = 0.014387769 m·K; 683.002 lm/W | CODATA 2018; the candela's definition | A black body at 2042 K comes out at 600,000 cd/m² within 3% (the old platinum standard of light): `optics: a hot solid glows` |
| Pure water's absorption a and scattering b, 1/m (`water.csv`), 380–750 nm | NASA Ocean Optics Protocols, Rev. 4, Vol. IV, Table 1.1, Erratum 1 (Mueller 2003; oceandata.sci.gsfc.nasa.gov, read 2026-11-16): a from Sogandares and Fry (1997) at 380–390 nm, Pope and Fry (1997) 400–715 nm, Kou, Labrie and Chylek (1993) 720–750 nm; b of pure fresh water from Buiteveld et al. (1994) | 550 nm: a + b = 0.0579 /m and 700 nm: 0.6245 /m come back from the embedded spectrum; three metres look blue-green: `optics: pure water, as measured` |
| Pure water's absorption, 755–780 nm | Kou, Labrie and Chylek (1993), Appl. Opt. 32, 3531, as tabulated by omlc.org (`kou93b`, 1/cm × 100); it meets the table above at 750 nm (2.848 against 2.857 /m) | b there extended by Morel's (1974) λ^−4.32 from 0.0004 /m at 750 nm; both far outside what the eye weighs |
| Water's refractive index n(λ), 20 °C (`media.json`, `indices`) | Daimon and Masumura (2007), Appl. Opt. 46, 3811: a four-term Sellmeier formula, 182–1129 nm, as given by refractiveindex.info (read 2026-11-17) | n at 589.3 nm comes out 1.3330 (the handbook's 1.3330 at 20 °C): `refraction: the indices` |
| Borosilicate glass 3.3 (lab glassware: DURAN, Pyrex, BOROFLOAT 33): refractive index at six wavelengths, 435.8–656.3 nm; density 2.23 g/cm³ (`media.json`, `indices`) | SCHOTT BOROFLOAT 33 technical details (schott.com, read 2026-11-17): n = 1.48015, 1.47676, 1.47311, 1.47133, 1.46953, 1.46916; nd 1.47140; νe 65.41 ("reference values, not guaranteed") | A Cauchy fit (n = A + B/λ² + C/λ⁴) at build, within 2e-4 of every tabulated value; nd and νe come back: `refraction: the indices` |
| Dense flint glass N-SF11 (the prism demo; `media.json`, `materials`): a three-term Sellmeier formula, 390–2500 nm (380–390 nm extrapolated) | SCHOTT Zemax catalogue 2017-01-20b, as given by refractiveindex.info (read 2026-11-17): B 1.73848403, 0.311168974, 1.17490871; C 0.0136068604, 0.0615960463, 121.922711 µm²; nd 1.78472, Vd 25.76 | n at 589.3 nm within 5e-4 of nd; a 60° prism's least deviation comes out 2 asin(n sin 30°) − 60°: `refraction: a 60-degree prism` |
| Malachite, diffuse reflectance, 380–780 nm every 10 nm (`malachite.csv`; Stage 23) | USGS Spectral Library Version 7 (Kokaly et al. 2017, USGS Data Series 1035, doi:10.3133/ds1035; data doi:10.5066/F7RR1WDJ; public domain), splib07a Record 18922, "Malachite GDS801 synthetic" (Kremer Pigments dry powdered pigment), ASD FR spectrometer, absolute reflectance, 1 nm; spectral purity code 1a. Read 2026-11-21 from a mirror of the ASCII release (the USGS servers refused the connection), and read again independently by a second reader: the two agree at every 20 nm | Peaks at 0.40 near 520 nm as other samples do (a natural Bisbee sample 0.23, a synthetic Aldrich sample under 45 µm 0.47, ECOSTRESS C-7A 0.71: levels differ with grain size and sample). **High confidence for this sample**; a powder's level depends on its grain; every published row the model reads back: `measured: malachite reflects as the USGS library's GDS801` |
| Copper sulfate solution, molar absorption coefficient (decadic), 350–750 nm (`copper_sulfate_aq.csv`; Stage 23) | NBS Circular 484 (K. S. Gibson, *Spectrophotometry (200 to 1,000 millimicrons)*, 1949), Table 4: the copper sulfate standard, 20.000 g CuSO₄·5H₂O and 10.0 ml H₂SO₄ (sp. gr. 1.835) a litre, 10.00 mm, 25 °C, absorbancy against the solvent, every 10 nm (after Davis and Gibson, NBS Misc. Publ. M114, 1931, Table 5: Beer's law holds from 5 to 40 g/l). Divided by 0.07986 mol/L (the salt assayed at 99.7 % of its copper) and 1 cm. Read 2026-11-21 by transcription from three scanned copies (Circular 484, NBS LC-1017 and its 1967 revision), each row checked against T = 10^−A; not read by eye here (the scans could not be rendered) | 750 nm gives 10.2, leading into 12.05 at 790 nm measured apart (Davenet et al., arXiv:2512.01458, 2025); peak near 805–815 nm (Qiu et al., PCCP 2015; Fonseca et al., J. Biomed. Opt. 22, 125007, 2017). **High confidence 550–750 nm; low below 500 nm**, where the absorbancy (0.001 to 0.009) is near its stated uncertainty (0.001). The Gaussian it replaces absorbed a third too much on the band's short side (3.9 against 2.8 at 650 nm); NBS's absorbancies from the model's own spectrum, 380–750 nm, and the interpolation to 790 nm: `measured: the copper sulfate standard's absorbancy is NBS Circular 484's` |
| Copper sulfate solution, 760–780 nm | Straight between NBS's 750 nm (10.23) and Davenet et al.'s 12.05 at 790 nm (Table 2: nine solutions, a regression). **Interpolated, not measured**; the eye weighs these wavelengths at under a hundredth of 555 nm's | |
| Water as an absorber | The coefficients above, summed (scattering counted as loss from the beam), over water's own 55.345 mol/L (997.05 g/L at 25 °C over 18.015 g/mol), as a decadic molar absorptivity: so a solution's water absorbs as much as there is of it | Not modelled: in-scattered light, the temperature dependence of a (at most 0.012 /m/°C, near 740 nm) |

**Calibrated media** (`data/optics/media.json`). None of these spectra is measured: each is a
simple shape placed so that the substance has the colour it is known to have. The numbers quoted
as literature are from memory and were not checked against a source.

Searched and **not found measured** (Stage 23: USGS splib07a, RELAB, ECOSTRESS, SSHADE, the
Winnipeg C-TAPE workbooks, CSIRO, RRUFF, the literature): visible reflectance of CuO (tenorite), of
the pentahydrate, the trihydrate, the monohydrate and the anhydrous salt, and of a red latex
balloon. These stay calibrated:

| Medium | Model | Basis | Confidence |
|---|---|---|---|
| CuO | Diffuse reflectance 0.035, flat | A black powder | low |
| CuSO₄·5H₂O | Diffuse reflectance 0.05 plus a Gaussian of 0.5 at 465 nm (width 55 nm) | Blue crystals | low |
| CuSO₄·3H₂O | Diffuse reflectance 0.3 plus a Gaussian of 0.4 at 470 nm (width 70 nm) | "Pale blue" (*Handbook of Mineralogy*, bonattite) | low |
| CuSO₄·H₂O | Diffuse reflectance 0.62 plus a Gaussian of 0.18 at 475 nm (width 80 nm) | A bluish white powder: its crystals are "clear light blue" (COD 7061268), a powder paler | low |
| CuSO₄ | Diffuse reflectance 0.82, flat | A white powder | low |
| Latex | Transmittance of the reference thickness: 0.04, rising by 0.9 across 600 nm (width 18 nm) | A red balloon | low |
| H₂SO₄(aq), glass, gases | None: clear | Their absorption in the visible is far below what a few centimetres show | high for these path lengths |

**Verified wall thicknesses (Stage 20)** (`data/benches/`, each glass vessel's `wall_mm`; from Stage 24
each vessel's glassware gives its wall and floor, Glassware above. The condenser keeps the bench's
own 0.8 mm and 1.9 mm, a 50 mL beaker's, until a condenser is made to its standard):

| Vessel | Wall (mm) | Source |
|---|---|---|
| 50 mL (the jar, the condenser) | 0.8 | ISO 3819 (Laboratory glassware: Beakers), Table 1, low form, the least wall thickness: read in the draft ISO/DIS 3819:2013 (iteh.ai's preview of ISO 3819:2015, read 2026-11-20); the same numbers in Wikipedia's "Beaker (glassware)", citing DIN EN ISO 3819:2015-12 |
| 250 mL (the flask, the beaker, the waste) | 1.1 | The same table |
| 500 mL (the acid's bottle) | 1.3 | The same table |

The standard gives the least wall a beaker may have; makers keep near it for resistance to
thermal shock, and thicken the floor and the rim, which are not modelled. **Confidence: high for
a beaker's side.** Every vessel of the demo is drawn as a beaker, whatever it is called. Before
Stage 20 the thickness was the wall's heat-holding mass spread over the side and the floor
(2.4 mm for the flask, 2.8 mm for the jar): too thick, and it bent the view through an empty
vessel far more than glassware does. The wall's mass (`wall_g`) is still its heat's.

**Calibrated things of the lab (Stage 22)** (`data/optics/media.json`: `surfaces`, `emitters`). None is
measured: each is a simple shape placed so that the thing has the colour the screen drew it in
before Stage 22, when it was drawn over the optical pass. **Confidence: low** (colours to be judged
by eye; measured spectra are a later stage).

| Thing | Reflectance, or light | Basis |
|---|---|---|
| A signal part's face, by kind | a timer 0.035 and 0.10 at 470 nm (width 60); a monitor 0.035 and 0.10 at 525 nm (width 50); an alarm 0.035 and 0.14 at 640 nm (width 60); a notice 0.035 and 0.13 at 585 nm (width 55); a writer 0.04, and 0.08 at 430 nm (width 40) and at 660 nm (width 50) | dark faces of distinct hues, so that a kind is known by its colour (the user: no text on a part) |
| A signal part's face, other | 0.045, flat | the screen's panel grey (#383838) |
| A circuit's face | 0.025, and 0.06 at 460 nm (width 60) | its dark blue (#283c50) |
| A wire | 0.08, flat | the wall's ink (#505050) |
| A rig | 0.15, and 0.22 at 460 nm (width 70) | its blue (#6e82a0) |
| A burner's metal | 0.12, flat | its grey (#606060) |
| A condenser's outlet (glass, not traced) | 0.07, and 0.06 at 470 nm (width 80) | the glass's ink (#4a5662) |
| A lamp's glass, dark | 0.03, flat | a dark glass |
| A lamp, green; amber | 1.2 times the room's light at 530 nm (width 30); 1.5 at 600 nm (width 30) | the screen's green (#78c88c) and amber (#e0a040), brighter than the wall as a lamp is |
| A flame, sooty | for each pixel of path: 0.08 at 650 nm (width 120) and 0.03 at 570 nm (width 60) | soot glowing, rising to the red |
| A flame, blue | for each pixel of path: 0.05 at 431 nm (width 14), 0.008 at 470 nm (width 25), 0.01 at 515 nm (width 30), 0.001 throughout | the band systems of CH (around 431 nm) and C2 (the Swan bands, 470 to 560 nm), from which a premixed flame's blue is (A. G. Gaydon, *The Spectroscopy of Flames*, 1974); their strengths placed by eye. Smooth at the pass's band scale: 6 nm wide bands were sampled by chance, and the GPU's settled flame was visibly not the truth's (O4) |
| The spectral viewer's case; its screen's black; its probe's head; its cable (Stage 42, design) | 0.09; 0.012; 0.35; 0.05, all flat | a dark instrument case, a display's black glass (its diffuse share: the rest of what it reflects is a gloss the pass does not show), a steel probe, a black cable |

**The viewer's primaries (Stage 42)** (`data/optics/media.json`: `emitters`: `led_red`, `led_green`,
`led_blue`). Design, calibrated: Gaussians (as the file's `width_nm`, a 1/e half-width) of 30 nm at 620,
530 and 450 nm, each a multiple of the room's light; the screen scales them so its full white is the
room's white (D65 at Y = 1). Smooth at the pass's bands (an emitter narrower than tens of nanometres is
not sampled reliably, `emitters_note`). Placed so that the screen shows each wavelength's colour on its
rainbow as the game shows a spike of that wavelength (the display's colour of the light of that
wavelength alone, at the brightness at which it is most saturated): measured over 400 to 700 nm on
the eye metric, the screen's colour of each against the game's, worst 1.6 and mean 0.31 (visible: 8).
Searched: widths 12 to 35 nm (worst 0 at 20 nm or less, 3.5 at 30 nm with centres 615, 535 and 460,
5.9 at 35 nm); centres 610 to 630, 525 to 540 and 445 to 460 at 30 nm. **Confidence: design** (a
real panel's primaries are narrower and its colour management its maker's; these are its colours, not
its spectra).

**Glassware (Stage 24)** (`data/glassware.json`): each vessel is made to its standard. Read
2026-11-21 from the standards' own preview pages (standards.iteh.ai: the scanned pages rendered and
read by eye, ISO 4796-1's from its text), every number as printed; each kind is held to its
standard's capacity rule by `capacity: the standards' own volumes`, and each row is read back by
`glassware: every kind's rows are its standard's`.

| Kind | Standard | Row used | Rules used |
|---|---|---|---|
| Low-form beaker | ISO 3819:1985, *Laboratory glassware — Beakers*, Table "Basic dimensions" (the same figures, heights within 2 mm, in ISO 3819:2015's table as reproduced by Wikipedia's "Beaker (glassware)", citing DIN EN ISO 3819:2015-12) | 50 mL: outside diameter 42, overall height 60, least wall 0.8 mm; 250 mL: 70, 95, 1.1 mm (tolerances ±5 %, §8.2) | §5: the overflow capacity exceeds the nominal by 10 %, or the nominal level is at least 10 mm under the overflow, whichever is the greater; §7.2: the side "slightly flared near the brim in a smooth and regular curve", the brim's diameter "of the order of 5 to 15 % greater than the diameter of the body", its edge "finished in a plane parallel with that of the base"; §8.3: an external radius at the junction of base and side of 15 to 20 % of the external diameter from 250 mL, at least 5 % below |
| Conical flask | ISO 1773:1997, *Laboratory glassware — Narrow-necked boiling flasks*, Table 1 | 250 mL: the body's outside diameter at its widest 85 ± 2, the neck's outside diameter 34 ± 1.5, overall height 145 ± 3, least wall 0.9 mm | §3.2: the nominal capacity is the series value nearest to but not above the real volume to the base of the neck (the series 25, 50, 100, 250, 500 …); §3.4.2: the neck's top "suitably reinforced"; §3.5.2: the neck 1 to 1.25 times its outside diameter long; §3.5.3: the radius where base meets side 15 to 20 % of the greatest outside diameter |
| Screw-neck bottle | ISO 4796-1:2016, *Laboratory glassware — Bottles — Part 1: Screw-neck bottles*, Table 1 | 500 mL: total height 176, height to shoulder 110, outside diameter 86 (all "approx."), least wall 1.5 mm, inside neck diameter at least 27 mm | §3.2: the nominal capacity is what a bottle of average wall holds filled to the turn of the shoulder; §3.3: to the base of the neck about 15 % more; §5.2.2: a radius at the base, the side cylindrical (25 mL to 2 L); §5.2.3: a radius at the shoulder; §5.2.4: the upper portion conical, met by the neck in as small a radius as manufacture allows; §5.2.6: the neck finished with a strengthening lip. A 500 mL bottle's thread is GL 45 (DURAN's catalogue, dwk.com, 500 mL GL 45, 86 mm across) |

Calibrated, within the standards' words (**low confidence**: chosen for a plausible vessel, to be judged by eye):

| Value | Chosen | Basis |
|---|---|---|
| A beaker's brim | 10 % wider than its body (the middle of 5 to 15 %), flared over a rise of 2.3 times the widening, in one arc tangent to the side | ISO 3819 §7.2 gives the widening, not the curve; the rise makes the brim leave at about 47° |
| A beaker's base corner | 17.5 % of the diameter at 250 mL (the middle of 15 to 20 %); 10 % at 50 mL (at least 5 %) | ISO 3819 §8.3 |
| A flask's base corner | 17.5 % of the greatest diameter | ISO 1773 §3.5.3: 15 to 20 % |
| A flask's neck | 1.125 times its outside diameter long (the middle of 1 to 1.25); met by the cone in an arc of a third of the neck's diameter; a bead on its top, a half-round as thick as twice the wall | ISO 1773 §3.4.2, §3.5.2 |
| A bottle's neck | outside 45 mm (GL 45), inside 33 mm (at least 27), 50 mm high (the thread and the plain neck under it), with a bead like the flask's | ISO 4796-1 Table 1, §5.2.6; the height set so that the bottle holds about 15 % more to the neck's base than to the turn of its shoulder (§3.3: the model gives 18 %; with a 25 mm neck it gave 38 %); the thread's own dimensions (DIN 168-1) not read |
| A bottle's radii | at the base 10 % of its diameter; at the shoulder 20 %; where the cone meets the neck 2 mm | ISO 4796-1 §5.2.2 to §5.2.4 ("suitable", "as small as possible") |
| A vessel's floor | 2.36 times its wall: 1.9 mm (50 mL beaker), 2.6 (250 mL beaker), 2.1 (conical flask), 3.5 (bottle) | The one supplier's 250 mL beaker below (a floor of 2.6 to a side of 1.1), as a ratio for every vessel; replaces Stage 21's floors (1.9, 2.6, 3.1 by capacity) |

**Calibrated equipment** (`data/benches/`): none is measured or checked against a source; each was
chosen when the bench was built, so that the demo runs at a pace a person can follow.

| Value | Where | Basis |
|---|---|---|
| A vessel's loss to the room, W/K (`loss`) | 0.1 (the condenser) to 0.9 (the flask) | chosen; free convection and radiation from a vessel of this size are of this order (not checked) |
| A wall's mass, g (`wall_g`) | 40 (the jar) to 250 (the acid's bottle); 2 for the balloon | chosen; it holds the wall's heat. The flask's 100 g agrees with one supplier's 250 mL beaker (100 ± 10 g, below) |
| The burner (`celsius`, `conductance`) | a reservoir at 1927 °C (2200 K), 0.25 W/K with its valve fully open | chosen: the flame as a hot body, not measured |
| The condenser's coolant (`coolant`) | 15 °C, 10 W/K | chosen: tap water, a conductance that takes the steam the demo makes |
| The balloon's capacity (`capacity_ml`) | 3000 mL | chosen: an ordinary party balloon of about 18 cm |

**Calibrated floors (Stage 21)** (`data/benches/`, each glass vessel's `floor_mm`). No standard
found gives a beaker's floor: ISO 3819 gives its wall alone.

| Vessel | Floor (mm) | Basis | Confidence |
|---|---|---|---|
| 250 mL | 2.6 | A supplier's table for a 250 mL borosilicate low-form beaker (indigoinstruments.com, item 55107, read 2026-11-20): height 98 ± 4 mm, diameter 68 ± 2 mm, "thickness 1.5-2.6 mm", weight 100 ± 10 g. Taken as its thinnest glass (the side) and its thickest (the floor). The weight is consistent: a 1.5 mm side and a 2.6 mm floor of those dimensions are 89 g of glass at 2.23 g/cm³; two other suppliers list 98 g and 102 g for a 70 × 95 mm beaker | low: one supplier, and the range's meaning is read into it |
| 50 mL | 1.9 | The 250 mL floor, in the ratio of ISO 3819's walls (0.8 to 1.1) | low |
| 500 mL | 3.1 | Likewise (1.3 to 1.1) | low |

To verify: a maker's drawing, or ASTM E960's table (not read: behind a paywall).

**Calibrated room for refraction (Stage 11, 21):** the wall stands 284 px behind the plane of the
bench's vessels, for every vessel (20 cm at a 250 mL vessel's scale, as it has been since Stage 11
for that vessel; until Stage 21 it was 20 cm at each vessel's own scale: 375 px for a 50 mL one,
259 px for a 500 mL one); what is held stands on a plane 120 px nearer the eye (8.5 cm at that
scale: a hand's reach in front of the bench); the room in front of the bench, which the glass
reflects, has a radiance of 0.3 of the room's white (darker than the lit wall: what a lab looks
like from the bench). All three are chosen for a plausible bench, not measured: the screen is one
world in pixels, since each item is drawn at its own scale. A solution's index is water's: the solutes' share is not modelled.

**Calibrated geometry of solids:** a heap's angle of repose 35° and packing fraction 0.5 (typical
of dry powders); a suspended particle's extinction efficiency 2 (the large-particle limit).
**The room:** its white is 100 cd/m² (a white page in ordinary indoor light).

## The long-wave infrared (Stage 46)

What a thermal camera's microbolometer sees, 8–14 µm (GAME_SPEC §12.1): the renderer's second range of the
spectrum, at 20 nm (301 points). Fetched 2026-11-24. Glass and water are opaque in it: what the camera sees of
a vessel is its outer glass.

**Measured** (`data/optics/water_lwir.csv`, `silica_lwir.csv`; `data/optics/media.json` `lwir.indices`):

| Value | Source | Read | Gate |
|---|---|---|---|
| Liquid water's n + ik, 7.9–14.5 µm | G. M. Hale and M. R. Querry, *Optical constants of water in the 200-nm to 200-µm wavelength region*, Appl. Opt. 12, 555–563 (1973), doi 10.1364/AO.12.000555, at 25 °C; read from the refractiveindex.info database (main/H2O/nk/Hale.yml) | every row over the band and one each side, as printed (0.2 µm steps to 10 µm, 0.5 µm after): at 10.0 µm n 1.218, k 0.0508; at 12.0 µm n 1.111, k 0.199 | `measured: water's long-wave index is Hale and Querry's at 10 um` (n and k at 10 µm, k at 12 µm, 638 /cm) |
| Glass's n + ik, 7.96–14.04 µm, as fused silica's | R. Kitamura, L. Pilon and M. Jonasz (Kitamura, Pilon and Jonasz), *Optical constants of silica glass from extreme ultraviolet to far infrared at near room temperature*, Appl. Opt. 46, 8118–8133 (2007), doi 10.1364/AO.46.008118: their model fitted to S. Popova, T. Tolstykh and V. Vorobev, Opt. Spectrosc. 33, 444–445 (1972) (amorphous quartz, 1400–200 cm⁻¹); read from the refractiveindex.info database (main/SiO2/nk/Popova.yml) | every row over the band and one each side (89 rows): the Reststrahlen peak, n 0.45931, k 1.9693 at 8929.5 nm (normal reflectance 0.694) | `measured: glass's long-wave reflectance peaks as Kitamura's fused silica does` (the peak within 1 % and a 20 nm step; opaque; the band emissivity within 0.1 of plate glass's 0.85) |

Fused silica stands in for the lab's borosilicate (BOROFLOAT 33's own constants are not tabulated over
8–14 µm): **medium confidence** for the glass. A soda-lime or borosilicate glass has a weaker Reststrahlen
band; the model's band emissivity at normal incidence is 0.79, Fluke's table gives 0.85 for plate glass.
Between rows the tables are read linearly, at 20 nm.

**Grey emissivities over 8–14 µm** (`media.json` `lwir.emissivity`): Fluke Process Instruments, *Emissivity
values for common materials* (non-metals) and *Emissivity of metals*, the 8–14 µm column (pages dated
23 March 2017, "to be used as a guide only"). **Medium confidence**: a supplier's table of typical values.

| Thing | Value | Row read |
|---|---|---|
| The parts' faces, the wire, the sensing and rig marks, the cap, the viewer's case, its probe's head and cable | 0.95 | Plastic, opaque: 0.95 |
| Rubber (a stopper, a tube) | 0.95 | Rubber: 0.95 |
| The outlet's glass, a lamp's glass, the viewer's screen | 0.85 | Glass, plate: 0.85 |
| The burner's metal | 0.8 | Steel, oxidised: 0.7–0.9 (the middle) |
| Water (the gate's bound) | 0.93 | Water: 0.93 (the model's own, from Hale and Querry: 0.985 at normal incidence) |

Also read (unused): stainless steel 0.1–0.8, iron oxidised 0.5–0.9, aluminium oxidised 0.2–0.4, copper
oxidised 0.4–0.8; the table's 8–14 µm cell for non-aluminium paint is ambiguous.

**Calibrated** (**low confidence**):

| Value | Chosen | Basis |
|---|---|---|
| A solid powder's emissivity (malachite, the copper oxide, the copper sulfates, the hydroxide) | 0.9 | not found measured over the band for these powders; Fluke's non-metals are 0.9–0.95 almost throughout, and a powder's rough face emits as a near black body |
| The balloon's latex: its transmittance at its reference thickness | 0.3, grey | not found measured for a balloon's membrane; natural rubber absorbs strongly in its bands (11.9 µm) and a membrane a tenth of a millimetre thick passes part of the band between them |
| A flame's gas: its absorption over the band | 0.01 per cm, grey | not found as band parameters to hand (Edwards' wide-band model's parameters are in textbooks, not read): set so that the burner's premixed flame, a centimetre or two of combustion products at 2200 K, reads tens of degrees above the room on an 8–14 µm camera at an emissivity of 0.95, as thermal images of a Bunsen flame show: faint, not hot |
| A sooty flame's soot | 0.005 per cm at 10 µm, as 1/wavelength | Rayleigh soot (Dalzell and Sarofim's n, k: κ = 4.9 f_v / λ) at a soot volume fraction of 10⁻⁶, a luminous laboratory flame's order |

**Design**: the sensor's response is flat over 8000–14000 nm (a microbolometer's band behind its window; a real
one falls off at the band's edges).

## Controller tuning: SIMC (Stage 26)

A controller's PI gains (`native/game/src/control.cpp`, GAME_SPEC §13.3) are Skogestad's SIMC rules:
S. Skogestad, "Simple analytic rules for model reduction and PID controller tuning", *Journal of
Process Control* 13 (2003) 291–309; read through the author's NTNU lecture slides ("SIMC Controller
tuning", skoge.folk.ntnu.no, read 2026-11-21).

| What | Value | Source | Held by |
|---|---|---|---|
| First-order process with a delay: `Kc`, `Ti` | `Kc = tau / (K (tau_c + theta))`, `Ti = min(tau, 4 (tau_c + theta))` | Skogestad 2003, eq. for "SIMC" PI | `control: SIMC's tight tuning ...` |
| Integrating process: `Kc`, `Ti` | `Kc = 1 / (k' (tau_c + theta))`, `Ti = 4 (tau_c + theta)` | Skogestad 2003 | the same |
| Tight control | `tau_c = theta` | Skogestad 2003 | the same |
| Margins at `tau_c = theta`, a lag no longer than `8 theta` | gain margin 3.14 (π), phase margin 61.4° | Skogestad 2003 (the loop is `e^-theta s / (2 theta s)`, so it follows in closed form) | the test computes both from the tuning: π to 1e-6, 61.35° to 0.2% |
| Margins at `tau_c = theta`, an integrator | gain margin 2.96, phase margin 46.9° | the lecture slides' table (Ms 1.70); from secondary sources, not the paper itself | the test: both to 0.5% |

Chosen for the game, not from the source: the speeds' closed-loop times (gentle `T`, normal `T / 4`,
fast `T / 20`, never under `theta`), the least delay (two steps), the PWM period (a tenth of `T`, 10 s
to 10 min) and the trial's end (half again the distance to the target, levelled off, or an hour).
The closed loop is held by `controller: water in the flask held at 60 C ...`.

## Pouring: an orifice (Stage 27)

A vessel set upside down pours through its cap as a sharp-edged orifice under the head of its liquid
(`native/game/src/lab.cpp`, GAME_SPEC §13): `Q = open × Cd × a × sqrt(2 g h)`, Torricelli's law with a
discharge coefficient.

| What | Value | Source | Held by |
|---|---|---|---|
| Discharge coefficient of a sharp-edged circular orifice | `Cd` = 0.61 | The standard textbook value, 0.60 to 0.62 for a sharp-edged orifice at high Reynolds number (e.g. F. M. White, *Fluid Mechanics*, the orifice's vena contracta, contraction ≈ 0.61, velocity ≈ 0.98–0.99) | `pour: upside down, its cap is an orifice ...`: one second's flow at the bottle's head against the law |
| Gravity | 9.80665 m/s² | standard gravity (CGPM 1901) | the same |

Chosen for the game (`data/glassware.json`, design in `data/sources.json`): each mouth's held-pour
rate at full tilt (a beaker 25 mL/s, a flask 12, a bottle 15), the cap's bore (4 mm), the widest
mouth a balloon's or a condenser's fitting seals (40 mm).

## Pouring by hand: a round mouth (Stage 31)

What runs out of a tilted vessel's mouth is Torricelli's speed over the wet part of the opening,
contracted (`native/game/src/contents.cpp`, `over_brim`; GAME_SPEC §13): a sharp-crested weir under
a small head, an orifice once the mouth is under liquid.

| What | Value | Source | Held by |
|---|---|---|---|
| The contraction of a jet leaving a sharp-edged opening | `Cd` = π / (π + 2) = 0.6110 | Kirchhoff's free-streamline solution for a jet from a slot in a plane wall (G. Kirchhoff, 1869; given in Lamb, *Hydrodynamics*, and in Batchelor, *An Introduction to Fluid Dynamics*, under free streamlines). A theory's value, not a measurement; sharp-crested weirs and sharp-edged orifices are measured to discharge 0.60 to 0.62 of the ideal (Rehbock's weir formula tends to 0.611 for a high weir; the orifice's row above, 0.61). Written from these standard results, not fetched again for this stage. Confidence: good for a thin sharp lip at a head of some millimetres or more; a thick rounded lip and a head of a millimetre (surface tension) pass less | `pour: a round mouth is a weir under a small head and an orifice under a deep one ...`: the value, and the law against its closed forms at small heads, deep, on its side and upside down |
| Gravity | 980.665 cm/s² | standard gravity (CGPM 1901) | the same |

Chosen for the game (GAME_SPEC §13.1): how fast a held vessel tilts (to its full tilt in 2.5 s) and
comes back upright (0.6 s).

## Stirring: a liquid's turning and what it holds up (Stage 32)

| What | Value | Source | Held by |
|---|---|---|---|
| Water's dynamic viscosity at 20 °C, 0.1 MPa | `μ` = 1.0016 mPa·s | IAPWS, *Release on the IAPWS Formulation 2008 for the Viscosity of Ordinary Water Substance* (the value at 20 °C and 0.1 MPa, 1.0016 mPa·s, is the ISO/TR 3666:1998 reference value). Written from the standard value, not fetched again for this stage. Confidence: the value itself is known to 0.17%; used for every liquid at every temperature (GAME_SPEC §20) | `vortex: its turning holds a liquid's solids up ...`: a fine oxide's settling time in the lab against Stokes' speed with this viscosity, within 5% |
| Stokes' settling speed | `v = (ρ_s − ρ) g d² / (18 μ)` | G. G. Stokes, 1851: a sphere falling slowly through a viscous liquid (its Reynolds number under about 1) | the same |

Chosen for the game (`native/game/src/lab.cpp`; GAME_SPEC §11): the stirrer's work, 0.01 W (of the
order of a 3 cm stir bar at 600 turns a minute, `Np ρ N³ d⁵` with a power number near 1: 0.02 W);
how long a liquid left alone keeps turning, 1.5 s.

## Stirring a layered column: the mixing efficiency (Stage 49)

| What | Value | Source | Held by |
|---|---|---|---|
| The mixing efficiency of a stirred stable stratification: the share of the stirring's dissipated work that raises the fluid's potential energy | `Γ` = 0.2 (`MIXING_EFFICIENCY`, `native/core/src/strata.cpp`) | P. F. Linden, 1979, "Mixing in stratified fluids", *Geophys. Astrophys. Fluid Dyn.* 13, 3–23 (doi:10.1080/03091927908243758): over the laboratory experiments it reviews, the mixing efficiency rises from zero with the Richardson number to a maximum of 0.2 ± 0.05 (the abstract, read 2026-11-25). T. R. Osborn, 1980, *J. Phys. Oceanogr.* 10, 83–89: 0.2 (a flux Richardson number of 0.17), the value used for the ocean since (as later authors report it; the paper not read). Confidence: the maximum, used as a constant: a weakly stratified column (a small Richardson number) mixes faster here than it would; ± 25% at the maximum | `measured: stirred, a layered column's potential energy rises at Linden's 0.2 of what the turning dissipates`: two layers of the experiment `layers` stirred at 1 mW for six steps, the potential energy gained against a still copy's, within 5% of 0.2 × the work |

The work the turning dissipates is the game's (above: 0.01 W while it turns, its energy decaying over 1.5 s
after); the mixing reads that dissipation, so a column mixes as the turning builds and dies away.

## A pour's stream: a jet's entrainment and how far it plunges (Stage 51)

| What | Value | Source | Held by |
|---|---|---|---|
| A round turbulent jet's entrainment: its volume flux at x down from a nozzle of diameter d0 | `Q(x) = 0.32 Q0 x / d0` (`JET_ENTRAINMENT`, `native/core/src/strata.cpp`; so 0.32 sqrt(π/4) sqrt(M) a metre, M = Q0 u0) | F. P. Ricou and D. B. Spalding, 1961, "Measurements of entrainment by axisymmetrical turbulent jets", *J. Fluid Mech.* 11, 21–32: m/m0 = 0.32 (x/d0)(ρ_s/ρ_0)^½ (the abstract, and the form as reproduced by later reports: a University of Utah report's OCR of the equation; a 1966 College of Aeronautics memo, m/m0 = 0.32 h/d, its own impinging jets about 0.3; searched 2026-11-25). Densities alike here (a liquid into a liquid), so the ratio is 1. Confidence: measured for gas jets into still air far from the nozzle; a stream poured a few centimetres into a beaker is short and confined, so ± 30% | `measured: a round jet entrains at Ricou and Spalding's 0.32 Q0 x / d0; ...`: `jet_entrains` against the source's form for nine jets |
| A forced fountain's height: a jet driven into a denser fluid | `z = 2.46 M^(3/4) B^(-1/2)` (`FOUNTAIN_HEIGHT`) | J. S. Turner, 1966, "Jets and plumes with negative or reversing buoyancy", *J. Fluid Mech.* 26, 779–792: salt water injected into fresh water, the final (steady) height (as later authors report it: Williamson et al. 2011, Burridge and Hunt; a range of 2.1 to 3.06 across later experiments; the initial height is about 1.43 times the final). Confidence: ± 20% | the same test: `fountain_reach` against the form |
| The share of the stream's kinetic energy that mixes | 0.2 (Linden 1979, as the stirrer's: above, Stage 49) | a stream's turbulence, as a stirrer's, raises the column's potential energy at most at this efficiency | `measured: a stream landing on a layered column raises its potential energy by Linden's 0.2 of its kinetic energy`: a step's gain against a still copy, within 5% (it is 47.85 µJ of 47.86) |

Chosen for the game: a pouring lip, an outlet or a cap stands 1 cm over the mouth it runs into (`LIP_GAP`,
`native/game/src/lab.cpp`; the screen's 10 pixels at its scale); a stream lands at the speed of its fall from there to
the liquid's surface, `sqrt(2 g h)`; a gentle pour runs down the wall and lands at none.

## The glass in rings (Stage 53)

| What | Value | Source | Held by |
|---|---|---|---|
| Borosilicate glass's thermal conductivity | 1.2 W/(m K) (`species.json`, `glass`) | SCHOTT, DURAN borosilicate glass 3.3, technical data: λ at 90 °C, 1.2 W/(m K); density at 25 °C 2.23 g/cm³ (read 2026-11-25). Confidence: a datasheet value, ± 5 % | `measured: a vessel's glass conducts at SCHOTT DURAN's 1.2 W/m K` |
| Natural convection on a vertical wall | Nu = 0.68 + 0.670 Ra^¼ [1 + (0.492/Pr)^{9/16}]^{−4/9} to Ra = 10⁹ (8.13a); {0.825 + 0.387 Ra^{1/6} / [1 + (0.492/Pr)^{9/16}]^{8/27}}² above (8.13b), properties at the film temperature (`lab/walls.hpp`) | Churchill and Chu (1975, Int. J. Heat Mass Transfer 18, 1323–1329), as Lienhard and Lienhard, *A Heat Transfer Textbook*, 6th ed. (v6.00, 2024), §8.3, eqs. 8.13a–b, pp. 419–420 give them: within a few per cent of the data up to Ra ≈ 10⁸ | `measured: a vertical wall's natural convection is Churchill and Chu's (Lienhard eqs. 8.13a-b) and air is Lienhard's Table A.6`: the forms at their own Ra and Pr |
| Air at 1 atm | ρ, cₚ, k, α, ν, Pr from 250 to 1000 K (`data/walls.json`) | Lienhard and Lienhard, Table A.6, p. 755 (the rows as printed) | the same test: 300 and 350 K as printed, 325 K between |
| A powder bed's effective conductivity | Zehner and Schlünder's stagnant bed: k_e / k_f = 1 − √(1−ε) + 2√(1−ε)/(1−λB) [(1−λ)B/(1−λB)² ln(1/(λB)) − (B+1)/2 − (B−1)/(1−λB)], λ = k_f/k_s, B = C ((1−ε)/ε)^{10/9}, C = 1.40 for broken grains (`walls::bed_conductivity`) | Zehner and Schlünder (1970), as M. Kandula (NASA, "On the effective thermal conductivity of porous packed beds with uniform spherical particles") gives it, eqs. 9a–c and 10, pp. 6–7: C = 1.25 spheres, 1.40 broken particles, 2.5 cylinders; validated for conductivity ratios 8 to 1200 and solid fractions to about 0.8, below about 2000. The bed's porosity is the sediment's (1 − PACKING, 0.5, calibrated: above) | `measured: a powder bed conducts as Zehner and Schlünder's stagnant bed`: the form for twelve beds |
| The grains' conductivities | malachite 3.59, the copper sulfate hydrates 1.32, anhydrous copper sulfate 5.91 W/(m K); copper oxide and hydroxide none | **Calibrated: searched and not found measured** (2026-11-25: malachite, tenorite, chalcanthite). Each is the nearest measured mineral as Robertson (USGS Open-File Report 88-441, 1988) tabulates it: calcite, a carbonate (8.58 CU, Horai 1971, p. 40); gypsum, a hydrated sulfate (3.15 CU, p. 30); anhydrite (14.12 CU, p. 30); 1 CU = 0.4184 W/(m K). A bed's grains are those of its solids that have one, weighted by volume; a bed with none conducts as its fluid. The bed's conductivity grows only as the logarithm of its grains' (about 1.6 times from a ratio of 100 to 1000) | the same test; and `walls: a powder on a flask's floor over a flame takes its heat through its bed` |

| A floor's base, a disc facing down into the room's air | warmer than the air (stable): Nu_D = 0.82 Ra_D^{1/5} Pr^{0.034} (8.39); colder (unstable), over L* = A/P = D/4: Nu = 0.560 Ra^¼ / [1 + (0.492/Pr)^{9/16}]^{4/9} (8.37a), 1.4 / ln(1 + 1.4/Nu) where that is under 10 (8.37b), to Ra = 10⁷; 0.14 Ra^⅓ (1 + 0.0107 Pr)/(1 + 0.01 Pr) above (8.35) (`walls::under_disc`) | Kadambi and Drake (1959) and Raithby and Hollands, as Lienhard and Lienhard, §8.4, eqs. 8.35, 8.37a–b, 8.39, pp. 435–437 give them | `measured: a ring's loss to the room is its outside's convection and its radiation`: the forms at their own Ra and Pr |
| Glass's total emittance | 0.94 to 260 °C, falling linearly to 0.74 at 540 °C, held beyond (`data/walls.json`, `emittance`) | Lienhard and Lienhard, Table 10.1, p. 546: glass, smooth, 0.94 at 40 °C; Pyrex 0.94–0.74 over 260–540 °C (its ends read as a line). Confidence: a handbook's typical values, ± 0.05 | the same test |
| Radiation to the room | e σ (T_w² + T_r²)(T_w + T_r) per unit area: eq. 1.34 factored exactly as eq. 2.28, a small body in a large room's transfer factor its emittance (eqs. 1.35, 2.31; σ CODATA 2018) (`walls::radiation`) | Lienhard and Lienhard, §2.3, pp. 71–72 | the same test |

Chosen for the game: a ring about 1 cm tall, at least 4 and at most 24 a vessel (`data/walls.json`). Each ring
loses to the room by its own outside: its side by Churchill and Chu into air over the vessel's height (the boundary
layer grows up the whole wall), the floor's base as a disc facing down, and radiation at its own temperature and
emittance; heat crosses half its glass's thickness to either side, in series with what it touches. A vessel whose
wall species has no conductivity or no emittance (the foam cup's polystyrene, until its own are sourced) keeps the
bench's `loss`, shared by its rings' outsides.

## Glass and stoppers under pressure (Stage 28)

Calibrated, not measured: no source found gives the pressure at which ordinary laboratory glass
bursts, nor the force that holds a rubber stopper in a neck. Searched 2026-11-22.

| Value | Used | Why |
|---|---|---|
| A bottle's glass, sealed by its screw cap | bursts past +300 kPa (`data/glassware.json`, `burst_kpa`) | DWK (DURAN): standard laboratory bottles are not designed to withstand pressure differentials; the *pressure plus+* bottle, a modified ISO 4796-1 geometry, is guaranteed from −1 to +1.5 bar (DIN EN 1595). A standard bottle is taken to fail at twice that guarantee |
| A conical flask | +200 kPa | its flat sides and base: weaker than a bottle's round shoulders |
| A beaker; a vessel made as no glassware (the condenser) | +150 kPa; +200 kPa | a beaker cannot be sealed (its mouth is too wide for a stopper); the condenser as a flask |
| A rubber stopper | holds 30 N (`benches/lab.json`, `hold_n`): pops at that force over its neck's bore, +37 kPa in the flask's 32 mm neck | dry rubber on a syringe's glass barrel holds about 10 N (friction measured, ONdrugDelivery, vial stopper testing); one pushed into a neck by hand, three times that. A vacuum holds a stopper in: it never pops inward |

Confidence: low; in the game's terms, the order of magnitude: a stopper pops well before the glass would
burst; a capped bottle heated with water in it bursts near 144 °C (its water's saturation at +300 kPa).

## Constants

CODATA 2018: R = 8.314462618 J/mol·K (used: `chem/thermo.hpp`). T0 = 298.15 K. Standard atmosphere 101325 Pa (from Stage 4).

## The balance (Stage 50)

| What | Value | Source | Held by |
|---|---|---|---|
| A load's conventional mass | `m_c = (m − ρ_air V) / (1 − ρ_air / ρ_ref)`, `ρ_ref` = 8000 kg/m³ | OIML D 28 (2004), *Conventional value of the result of weighing in air*: the mass of reference weights of 8000 kg/m³ that balance the load in air of 1.2 kg/m³ at 20 °C. A balance is adjusted with such weights, so what it shows is a conventional mass. Its own case: a kilogram of water at 20 °C (998.21 kg/m³, 1001.8 cm³) in air of 1.2 kg/m³ reads 998.95 g | `measured: a balance reads OIML's conventional mass` |
| The room's air | `ρ_air = P M / (R T)` of the room's own gas | the ideal gas: the room's composition (79 % N₂, 21 % O₂, dry: 28.85 g/mol, where real air's argon makes 28.96), 1 atm and its temperature; 1.199 kg/m³ at 20 °C, 0.4 % under real dry air's 1.204 (a buoyancy 5 µg lighter a mL) | `lab: a balance reads its load's conventional mass` |

Chosen for the game (`data/instruments.json`, design): a top-loading balance reads 0.01 g to 620 g within
0.02 g, an analytical one 0.1 mg to 220 g within 0.2 mg; a thermometer 0.1 °C within 0.2 °C; a pH meter 0.01
within 0.02; a pressure gauge 0.1 kPa within 0.5 kPa (Stage 54); a spectrophotometer (Stage 55) of a student's kind: 380 to 780 nm (the simulation's light), a 4 nm band, 0.1 % stray light, its wavelength within 2 nm, its absorbance to 0.001 up to 3, a tungsten lamp at 2900 K, its beam 15 mm over a cell's floor (the common centre height); a round cuvette 12 mm across, its wall 1 mm, so its path 10 mm; its heated cell holder (Stage 56) 20 to 80 C, 5 W/K up to 10 W; a hand misses an amount aimed at by up to 10 %. The marks on a beaker or a flask (every 10, 25
or 50 mL) within 5 %, a bottle's within 10 % (`data/glassware.json`). Confidence: these are of the order
makers state; they are not claims about any one instrument.

## Gases' diffusion in air (Stage 50)

An open vessel's gas exchanges with the room's air by diffusion through its mouth (GAME_SPEC §6). Each gas's
coefficient in air at 298.15 K and 1 atm, and its power of the temperature, `D = D298 (T / 298.15)^n`
(`data/species.json`, `air_diffusion`). Retrieved 2026-11-25.

| Gas | D298 (cm²/s) | n | Source | Held by |
|---|---|---|---|---|
| H₂O | 0.254 | 1.87 | NIST TN 2279 (D. R. Burgess Jr., *Self-Diffusion and Binary-Diffusion Coefficients in Gases*, 2024), Table 1a: H₂O in N₂, 0.254 at 298 K (2–3 %), ln D = −10.324 − 84.181/T + 1.620 ln T over 150–2000 K; n its local power at 298 K. Incropera's Table A.8 gives H₂O–air 0.26 | `measured: gases diffuse in air at NIST's and Incropera's coefficients` (within 1 % of NIST's fit from 280 to 450 K); `exchange: an open flask of water evaporates into dry air at Stefan's rate` |
| CO₂ | 0.160 | 1.75 | Incropera, *Fundamentals of Heat and Mass Transfer*, Table A.8, CO₂–air (via Wikipedia's *Mass diffusivity*, after Cussler); n Fuller, Schettler and Giddings' 1.75. **Medium confidence**: NIST TN 2279 lists CO₂–N₂ at 0.224 (1–3 %, its ref. 31) where the handbooks give 0.16–0.17 | the same |
| O₂ | 0.21 | 1.80 | Incropera Table A.8, O₂–air; n NIST TN 2279's O₂ self-diffusion fit at 298 K | the same |
| N₂ | 0.21 | 1.76 | calibrated: nitrogen's exchange in air is with its oxygen, O₂–air's coefficient; NIST TN 2279's self-diffusion of N₂ (0.209) and O₂ (0.215) bracket it; n its N₂ fit | — |

The exchange is Stefan's tube: a gas crosses the mouth at `D c G (x_vessel − x_room)` mol/s, `c` the gas's molar
density at 1 atm and the two cells' mean temperature, `G` the mouth's area over its length (the integral of
`dz / A(z)` from what the vessel holds up to its brim, inverted). Water's vapour pressure in the model at 20 °C is
2.338 kPa (2.339 measured), so an open beaker evaporates at Stefan's rate. Not modelled: convection, which a warm
vessel's gas really leaves by much faster than by diffusion (§20).

## Volumetric glassware (Stage 52)

Read from the standards' own pages (the iTeh previews of ISO 385:2005, ISO 385/2 and /3-1984, ISO 648-1977,
ISO 1042-1981 and ISO 4788:2005), 2026-11-25. Class A throughout.

| Piece | Tolerance | What else | Source |
|---|---|---|---|
| Burette, 50 mL | ±0.05 mL | divisions 0.1 mL; scale 500–600 mm (used: 550); at most 820 mm (750); zero line 50 mm from the top; lowest line 50 mm over the stopcock; jet 50 mm under it; delivery 60–100 s (80); outside 13 mm | ISO 385:2005 Tables 1–2; ISO 385/2-1984; a class A catalogue (ASTM) for the diameter |
| Bulb pipette, 25 mL | ±0.03 mL | at most 530 mm (510, a maker's); bulb at most 24 mm (22); delivery tube 7 ± 1 mm, at least 220 mm (230); inside at the mark at most 5.5 mm (4.5); wall at least 1.0 mm; delivery 25–50 s (35) | ISO 648-1977 Tables 1–4 |
| Volumetric flask, 100 / 250 mL | ±0.10 / ±0.15 mL | neck inside at the mark 12–14 / 14–17 mm (13 / 15.5); height 170 / 220 mm; bulb about 60 / 80 mm; base 40 / 55 mm; the mark in the neck's lower two thirds | ISO 1042-1981 Tables 1–3 |
| Measuring cylinder, 10 / 25 / 100 mL | ±0.1 / ±0.25 / ±0.5 mL | subdivisions 0.2 / 0.5 / 1 mL; at most 140 / 170 / 260 mm; the top at least 20 / 25 / 35 mm over the highest line (22 / 27 / 37); 100 mL outside 29 mm (DURAN) | ISO 4788:2005 Table 1 |

Derived, not given: a burette's and a cylinder's bore (the nominal volume over the scale's length, in the
standard's outside diameter); a flask's bulb radius (the one that puts the nominal volume a third up the neck:
59.8 and 81.2 mm against the standard's approximate 60 and 80); a pipette's bulb length (the nominal volume 50 mm up
the suction tube); a tip's bore (Torricelli's through a sharp-edged orifice, Cd 0.61, giving the delivery time
from the zero line or the mark: 0.69 mm for the burette). Confidence: the tolerances and the delivery times are the
standards' (the delivery times from the superseded 1984 and 1977 editions: the current ones' tables were past the
previews); the shapes within the standards' words.

**A foam cup** (the calorimeter's): an 8 oz expanded-polystyrene cup, 81 mm across its top, 51 mm its base, 89 mm
high (a cup maker's listing), its wall about 2.5 mm (J. Chem. Educ. 2025, 102, 1281: 5.09 mm for two nested cups).

## A filter funnel (Stage 59)

| What | Value | Source | Held by |
|---|---|---|---|
| A filter paper's permeance | Whatman grade 1: a Herzberg time of 150 s, so 100 mL / (150 s × 10 cm² × 10 cm) = 6.7e-3 cm/s per cm of head at 20 °C, as water's viscosity goes at other temperatures (Darcy) | Whatman grade 1 qualitative filter paper (Cytiva; Sigma-Aldrich's, Avantor's and Fisher Scientific's listings, 2026-11-25): Herzberg 150 s; particle retention 11 µm at 98 %. The Herzberg test: 100 mL of prefiltered water at 20 °C through 10 cm² of paper under a constant 10 cm head (Cytiva, *Principles of laboratory filtration*) | `measured: a filter paper passes water at its Herzberg time, at the test's own conditions` |

The funnel (design, after ISO 4798's form): a 60° cone 75 mm across on a stem 75 mm long and 8 mm across; a 110 mm
circle of the paper folded in quarters, its cone's slant its radius, 47.6 mm high. Through it the liquid drains by
Darcy's law, each patch of the wetted paper passing water in proportion to the head over it, the stem's column hanging
under it (Q = k 2π tan 30° / cos 30° (h³/6 + L h²/2)); the stem, once the cone is drained, runs out freely. The paper
keeps every grain (§20: a real paper lets grains under its retention through, and a fresh precipitate's fine grains
clump into flocs it keeps).

## A thermometer's lag (Stage 52)

| What | Value | Source | Held by |
|---|---|---|---|
| A thermometer probe's response | t90 = 10 s in stirred water (90 s in moving air, 400 s in still air); a first order's time constant t90 / ln 10 = 4.3 s | Vernier TMP-BTA's manual (a 20 kΩ NTC thermistor in a 4.0 mm 316 stainless sheath) | `measured: a thermometer reaches 90 % of a step in its probe's 10 s` |
| A laboratory thermometer, for comparison | time constant 2–6 s in vigorously stirred water by its bulb; one calorimetric thermometer 4.3 s stirred, 14.5 s unstirred | Harper, NBS Bulletin 8(4), 1912, Table II | — |

By instruments each vessel's thermometer follows its vessel as a first-order instrument with that time constant,
stirred or not (§20).

## A foam cup (Stage 52)

The calorimeter's cup: an 8 oz expanded-polystyrene cup (`data/glassware.json`, `foam_cup`; its shape above) of a
new wall species, `polystyrene_foam` (C8H8, the styrene unit).

| What | Value | Source |
|---|---|---|
| Its heat capacity | 1.17 J/(g·K) near 293 K: 121.86 J/(mol·K) of the styrene unit | NIST's cryogenic material correlations for polystyrene foam (near 293 K: 1161 to 1307 J/(kg·K) by the foam's density) |
| Its conductivity | 0.035 W/(m·K) near 293 K | the same (32 kg/m³) |
| Its mass | about 2.6 g | a cup's wall of about 52 cm³ at about 0.05 g/cm³ (calibrated) |
| Its heat lost (a bench's `loss`) | 0.12 W/K | calibrated: the foam's 0.035 W/(m·K) through its 2.5 mm over about 0.021 m² (0.29 W/K) in series with an outside film of about 7 W/(m²·K) over 0.025 m² (0.18 W/K) and a lid; a double cup with a lid lost 23.8 °C in 40 min (J. Chem. Educ. 2025, 102, 1281; its start unknown) |

Its light: searched and not found measured. Real foam scatters white; the pass has no wall that scatters light back,
so the cup's wall lets a flat 0.8 of the visible through (pale, where a third made it a smoked grey) and a fifth of
the long-wave (`data/optics/media.json`), calibrated.

## Liquids that expand (Stage 67)

What a liquid's and a dissolved species' volume grow by with temperature (GAME_SPEC §6): water's density at one
atmosphere, `data/solutions.json`'s `density`, read 2026-11-26.

**Kell 1975**: G. S. Kell, "Density, thermal expansivity, and compressibility of liquid water from 0° to 150 °C:
correlations and tables for atmospheric pressure and saturation reviewed and expressed on 1968 temperature scale",
*J. Chem. Eng. Data* **20** (1975) 97–105, doi:10.1021/je60064a005, **eq. 16**: air-free water at 101.325 kPa,
ρ = (999.83952 + 16.945176 t − 7.9870401e-3 t² − 46.170461e-6 t³ + 105.56302e-9 t⁴ − 280.54253e-12 t⁵) /
(1 + 16.879850e-3 t) kg/m³, t in °C (IPTS-68), 0 to 150 °C. Its coefficients as the correlation is widely
reproduced; no readable copy of the paper itself was reached (NIST's reprints fetched as images), so they are held
to independent measurements rather than to the printed page:
- Lienhard's Table A.3 (above, Stage 45), every row: within 0.07 kg/m³ from 0.01 to 100 °C (his rows are printed
  to 0.1), within 0.27 above (his are saturated liquid, at its vapour pressure, not at 1 atm);
- the density's maximum: 999.972 kg/m³ at 3.98 °C, its slope there zero;
- the table's own 25 °C water, 0.997048 g/cm³ (`data/species.json`): 997.045, within 3e-6.
Beyond 0–150 °C the game extends it by its value and slope at the end, so it and its slope are continuous (a
sealed cell's temperature is the root of its energy, which holds the slope).

**Confidence: high** (to 1e-4 over 0–100 °C, what a level or a sealed flask's pressure can show). The test:
`measured: water's density is Kell's, within Lienhard's rows (Stage 67)`.

**How it is used** (code, `chem::expansion`): a liquid's and a solute's molar volume at T is the table's (25 °C)
times ρ(25 °C)/ρ(T); a solute's apparent molar volume is scaled as the solvent's, as Stage 47's buoyancy already
did (its own expansion differs; not modelled). Solids keep the table's (about 1e-5/K, not modelled).

## Ions as species (Stage 63)

Dissolved matter as its ions (GAME_SPEC §9, §10): hydrogen, hydroxide, copper, sulfate, bisulfate, sodium, nickel
and CuOH⁺, each a species of its own. Read 2026-11-26 from PHREEQC v3's databases (USGS,
`github.com/usgs-coupled/phreeqc3`, `database/pitzer.dat` and `database/minteq.v4.dat`, as text).

**The ions' rows, split exactly.** The retired formula units' rows (sulfuric acid, copper sulfate, sodium
hydroxide, sodium sulfate, nickel sulfate, each cited in its own section) are split into their ions, H⁺(aq) at
nought by the tables' convention: SO₄²⁻ is the acid's row, Cu²⁺ copper sulfate's less it, Na⁺ half of sodium
sulfate's less it, OH⁻ sodium hydroxide's less Na⁺, Ni²⁺ nickel sulfate's less SO₄²⁻. Every solution keeps its
energy, entropy, volume and solubility; the single ions' values are not NBS's own (Cu²⁺'s is Stage 19's fit), and an
ion's apparent volume may be negative.

| Ion | ΔfH° kJ/mol | S° J/mol·K | Cp° J/mol·K | V cm³/mol |
|---|---|---|---|---|
| H⁺ | 0 | 0 | 0 | 0 |
| SO₄²⁻ | −909.27 | 20.1 | 138.9 | 35.7 |
| Cu²⁺ | 73.28 | −71.62 | 72.91 | −33.0 |
| Na⁺ | −240.12 | 59.0 | −169.95 | −17.85 |
| OH⁻ | −229.994 | −10.9 | 67.85 | 17.85 |
| Ni²⁺ | −53.93 | −128.9 | 72.91 | −34.89 |

Water's ionisation follows from them: H⁺ + OH⁻ → H₂O gives out 55.836 kJ (NBS's), log Kw 14.005 at 25 °C; its heat
capacity is the formula units' (the old fit's: −7.5 J/K·mol), not the measured −224, so pKw is 12.05 at 100 °C
against IAPWS's 12.25 (§20); the pH itself reads water's own measured ionisation (Stage 43).

**The bisulfate, from pitzer.dat.** SO4-2 + H+ = HSO4-: log K 1.988, ΔH 3.85 kcal (16.108 kJ), and its analytic
log K = −56.889 + 0.006473 T + 2307.9/T + 19.8858 log₁₀ T, whose slope at 25 °C gives ΔCp = 239.2 J/K·mol. Over the
model's own SO₄²⁻: ΔfH° −893.162, S° 112.19, Cp° 378.1. Its diffusion coefficient 0.731e−9 m²/s is the file's
`-dw`. The model's K2 is within 0.02 in log K of the analytic form from 0 to 100 °C (the gate:
`measured: the bisulfate's dissociation constant is PHREEQC's`). Its apparent volume is sulfate's, a stand-in.

The acid's activity model, from pitzer.dat (Pitzer, Roy and Silvester 1977 with the bisulfate): H⁺–SO₄²⁻
β0 0.0298, β1 0, Cφ 0.0438; H⁺–HSO₄⁻ β0 0.2065, β1 0.5556; Na⁺–HSO₄⁻ β0 0.0454, β1 0.398. With the bisulfate at its
equilibrium the acid's stoichiometric osmotic and mean activity coefficients keep to Staples's within 2% to
5 mol/kg (`solution: the data oracle: copper sulfate and the acid as electrolytes`); 0.1 mol/kg of the acid holds
0.073 mol/kg of bisulfate and reads pH 1.02.

**Copper's hydrolysis, from minteq.v4.dat.** Cu+2 + H2O = CuOH+ + H+: log K −7.497, ΔH 35.81 kJ. Over the model's own
Cu²⁺ and water: ΔfH° −176.74, S° −25.09; its heat capacity and volume Cu²⁺'s and water's together (the reaction's own
held at nought, the file giving neither). Its dimer, Cu₂(OH)₂²⁺ (log K −10.359), is left out: with no interaction
parameters for it, a solution boiled down held most of its copper in it (§20). 0.5 mol/kg copper sulfate reads
pH 4.8 (measured about 4).

**The surface rates in hydrogen ions.** The oxide's and malachite's rates were measured against the acid's
concentration; their driver is now the free hydrogen ion, so each is fitted again at its source's own conditions
through the model's settled acid (0.1 mol/kg: 0.1266 mol/kg H⁺ at 25 °C, 0.1186 at 45 °C; 0.2: 0.2453, 0.2307;
0.8: 0.9742): Majima's half order between 0.2 and 0.8 M becomes 0.5027 in H⁺, his 69 kJ/mol 70.22 with the
bisulfate's shift with temperature, the prefactor 1.6937e8; Nicol's 0.65 between 0.1 and 0.2 M becomes 0.6812, his
53.6 kJ/mol 55.372, the prefactor 5.186e7. Each gate holds the model to the source's law in the acid.

## Weak acids and bases (Stage 64)

Chloride, potassium, ammonium and ammonia, acetate and acetic acid, carbonate, hydrogen carbonate and dissolved CO2,
the phosphates and phosphoric acid; ammonia gas (GAME_SPEC §9, §10). Read 2026-11-26.

**Formation enthalpies and entropies: the NBS tables** (Wagman et al. 1982, J. Phys. Chem. Ref. Data 11 Suppl. 2,
srd.nist.gov/JPCRD/jpcrdS2Vol11.pdf, a scan read page by page), the ao rows, the convention H+ = 0: one table with
Stage 63's Na+, OH- and SO4 2-.

| Species | NBS page | ΔfH° kJ/mol | S° J/mol·K | Cp° (OBIGT) J/mol·K | V° (OBIGT) cm³/mol |
|---|---|---|---|---|---|
| Cl⁻ | 2-47 | −167.159 | 56.5 | −123.18 | 17.79 |
| K⁺ | 2-328 | −252.38 | 102.5 | 8.28 | 9.06 |
| NH₄⁺ | 2-65 | −132.51 | 113.4 | 65.86 | 18.13 |
| NH₃(aq) | 2-64 | −80.29 | 111.3 | 65.40 | 24.43 |
| NH₃(g) | 2-64 | −46.11 | 192.45 | 35.06 (NBS) | — |
| CH₃COO⁻ | 2-93 | −486.01 | 86.6 | 25.94 | 40.5 |
| CH₃COOH(aq) | 2-93 | −485.76 | 178.7 | 169.70 | 52.01 |
| CO₃²⁻ | 2-83 | −677.14 | −56.9 | −290.8 | −5.02 |
| HCO₃⁻ | 2-83 | −691.99 | 91.2 | −35.40 | 24.6 |
| CO₂(aq) | 2-83 | −413.80 | 117.6 | 216.98 | 33.5 |
| PO₄³⁻ | 2-73 | −1277.4 | **−220.35** (see below) | −521.33 | −30.6 |
| HPO₄²⁻ | 2-73 | −1292.14 | −33.5 | −243.93 | 5.4 |
| H₂PO₄⁻ | 2-73 | −1296.29 | 90.4 | −29.29 | 31.3 |
| H₃PO₄(aq) | 2-74 | −1288.34 | 158.2 | 98.74 | 48.1 |

- **PO₄³⁻'s entropy** is the one NBS's own ΔfG and ΔfH of PO₄³⁻ and HPO₄²⁻ imply, −220.35: its printed −222.
  (a row whose G-H-S constraint NBS relaxed) put pKa3 at 12.43 against the databases' 12.32 to 12.38.
- **Heat capacities and volumes: OBIGT** (the CHNOSZ package's database, github.com/cran/CHNOSZ,
  `inst/extdata/OBIGT/inorganic_aq.csv` and `organic_aq.csv`): Cp and V at 25 C and 1 bar as tabulated, from Shock
  and Helgeson 1988 (the ions), Shock, Helgeson and Sverjensky 1989 (NH₃, H₃PO₄), Shock et al. 1997 (PO₄³⁻), Shock
  1995 (acetate and acetic acid) and Plyasunov and Shock 2001 (CO₂); NBS prints none for most. Each value was
  checked against its row's HKF parameters (volumes to 0.01 cm³; Cp but for NH₃, CO₂ and NH₄⁺, whose tabulated
  values are kept). An ion's is shifted to the model's footing by its charge z: **Cp − 216.35 z, V − 16.74 z**.
- **The footing, 216.35 J/K a unit of charge.** Stage 63's ions are NBS's heat capacities less 216.35 z (Na⁺ −169.95
  against NBS's 46.4, OH⁻ 67.85 against −148.5), the sulfuric acid's stand-in carried through the split; H⁺ was left
  at nought, so every neutral salt had NBS's heat capacity but every reaction making H⁺ 216.35 J/K too much (water's
  ionisation −7.5 against −223.8). Stage 64 puts H⁺ on the same footing, **−216.35**, and moves the bisulfate's (to
  161.75, keeping pitzer.dat's 239.2 for its reaction) and CuOH⁺'s (to 364.635, keeping its reaction's nought) with
  it: water's ionisation then has NBS's −223.8, sulfuric acid's unit −293.8 against NBS's −293. The volumes keep H⁺
  at nought (the acid's apparent volume, 35.7 cm³/mol at its working strength, is right as it is), so an acid's
  volume is off by 16.74 cm³ a hydrogen ion at infinite dilution: a declared simplification.

**The constants that follow, against their sources at 25 C** (`measured: ammonia's, carbonic acid's and the gases'
constants are PHREEQC's from 0 to 60 C`, `measured: acetic acid's and phosphoric acid's constants are MINTEQ's`):

| Reaction | The tables | Source |
|---|---|---|
| CH₃COOH ⇌ CH₃COO⁻ + H⁺ | pKa 4.767 | minteq.v4.dat 4.757 (NIST 46.4) |
| NH₄⁺ ⇌ NH₃ + H⁺ | pKa 9.258 | phreeqc.dat 9.252, its analytic within 0.02 from 0 to 60 C |
| CO₂ + H₂O ⇌ HCO₃⁻ + H⁺ | pKa1 6.371 | phreeqc.dat 6.352 (within 0.05 to 60 C) |
| HCO₃⁻ ⇌ CO₃²⁻ + H⁺ | pKa2 10.337 | phreeqc.dat 10.329 (within 0.02 to 60 C) |
| H₃PO₄ ⇌ H₂PO₄⁻ + H⁺ | pKa1 2.149 | minteq.v4.dat 2.148 |
| H₂PO₄⁻ ⇌ HPO₄²⁻ + H⁺ | pKa2 7.199 | minteq.v4.dat 7.198 |
| HPO₄²⁻ ⇌ PO₄³⁻ + H⁺ | pKa3 12.342 | minteq.v4.dat 12.375, phreeqc.dat 12.346 |
| CO₂(g) ⇌ CO₂(aq) | log K −1.469 | phreeqc.dat −1.468 (within 0.02 to 60 C) |
| NH₃(g) ⇌ NH₃(aq) | log K 1.749 | phreeqc.dat 1.7966, wateq4f.dat 1.77, NBS's own 1.761 |

**Ammonia's diffusion in air**: Tang, Cox and Kalberer 2014 (Atmos. Chem. Phys. 14, 9233), Table 2, NH₃ in air
176 ± 10 Torr cm²/s at 296 K, that is 0.2316 cm²/s at 1 atm; carried to 298.15 K by the power 1.78 fitted to
Weissman 1964's NH₃–N₂ rows at 293 to 523 K (their Table 3): **0.2346 cm²/s, exponent 1.78**.

**The ions' diffusion**: the CRC Handbook's "Ionic conductivity and diffusion at infinite dilution" (Vanýsek, pp.
5-77 to 5-79): Cl⁻ 2.032, K⁺ 1.957, NH₄⁺ 1.957, acetate 1.089, HCO₃⁻ 1.185, CO₃²⁻ 0.923, H₂PO₄⁻ 0.959, HPO₄²⁻ 0.759,
PO₄³⁻ 0.824 (10⁻⁹ m²/s; its Na⁺, H⁺, OH⁻ and SO₄²⁻ agree with Li and Gregory's to the printed digit). Dissolved CO₂
1.92 and NH₃ 2.28, PHREEQC v3's phreeqc.dat `-dw`.

**The activity model**: Pitzer and Mayorga 1973 (read from the LBL preprint of J. Phys. Chem. 77, 2300, through the
Wayback Machine; pytzer's transcription of the published paper agrees): Table I HCl (0.1775, 0.2945, Cφ 0.0008),
NaCl (0.0765, 0.2664, 0.00127), KCl (0.04835, 0.2122, −0.00084), NH₄Cl (0.0522, 0.1918, −0.00301), KOH, NaH₂PO₄,
KH₂PO₄; Table II the acetates; Table VI K₂SO4, (NH₄)₂SO₄, Na₂HPO₄, K₂HPO₄, CuCl₂, NiCl₂ (their (4/3)β and
(2^(5/2)/3)Cφ unscaled); Table VIII Na₃PO₄ and K₃PO₄ ((3/2)β, (3^(3/2)/2)Cφ unscaled). The carbonates are Harvie,
Møller and Weare 1984's (Na⁺–CO₃²⁻ 0.0399, 1.389, 0.0044 and K⁺–CO₃²⁻ as pitzer.dat has them; Na⁺–HCO₃⁻ 0.0277,
0.0411 and K⁺–HCO₃⁻ 0.0296, −0.013, −0.008 as pytzer transcribes them: pitzer.dat's newer refits, Appelo 2015, were
not taken), K⁺–HSO₄⁻ pitzer.dat's. Gates: Hamer and Wu 1972 (J. Phys. Chem. Ref. Data 1, 1047, Tables 4, 16 and 28)
for HCl, NaCl and KCl within 2 % to 3, 6 and 4 mol/kg (`measured: hydrochloric acid's, sodium chloride's and
potassium chloride's mean activity coefficients are Hamer and Wu's`), and IUPAC's primary pH standards (Buck et al.
2002, Pure Appl. Chem. 74, 2169, Table 2): 0.025 mol/kg KH₂PO₄ + 0.025 Na₂HPO₄ reads 6.863 (6.865), 0.025 NaHCO₃ +
0.025 Na₂CO₃ 9.987 (10.012), within 0.04 at 10 and 40 C (`measured: the IUPAC primary pH standards`).

## Salts (Stage 64)

The crystals experiments 14 to 20 weigh: sodium chloride, potassium chloride, ammonium chloride, sodium hydrogen
carbonate, sodium carbonate and its decahydrate (the solid in contact with its saturated solution at 25 C), anhydrous
sodium acetate, and potassium hydrogen phthalate (KHP). Read 2026-11-26.

| Crystal | NBS page | ΔfH° kJ/mol | S° (the model's) | Cp° | density g/cm³ |
|---|---|---|---|---|---|
| NaCl | 2-301 | −411.153 | 72.31 (NBS 72.13) | 50.50 | 2.17 |
| KCl | 2-330 | −436.747 | 83.31 (82.59) | 51.30 | 1.988 |
| NH₄Cl | 2-68 | −314.43 | 94.70 (94.6) | 84.1 | 1.519 |
| NaHCO₃ | 2-314 | −950.81 | **95.33** (101.7) | 87.61 | 2.20 |
| Na₂CO₃ | 2-313 | −1130.68 | **126.50** (134.98) | 112.30 | 2.54 |
| Na₂CO₃·10H₂O | 2-314 | −4081.32 | 556.31 (562.7) | 550.32 | 1.46 (Merck) |
| CH₃COONa | 2-315 | −708.81 | 123.13 (123.0) | 79.9 | 1.528 |
| KHC₈H₄O₄ (KHP) | Kong et al. 2010 | −1028.05 | 229 (derived) | 211.14 | 1.644 (COD 1511782) |

- **Each crystal's entropy is the one NBS's own Gibbs energies imply** through its dissolution, S° = S°(its ions in
  water) − (ΔsolH − ΔsolG)/T, ΔsolG and ΔsolH from NBS's printed ΔfG and ΔfH of the crystal and of its "ai" row: NBS
  relaxed the G-H-S constraint of the carbonates, and NaHCO₃'s printed S° made its solubility half the measured.
  For the chlorides and the acetate the two agree within 0.7 J/K mol.
- **KHP**: Kong, Di, Xu, Yang and Tan 2010 (J. Chem. Eng. Data 55, 2185; NIST ThermoML): ΔfH −1028.05 ± 0.61
  kJ/mol, and Cp 211.14 J/K mol at 298.15 K between their points. Its S°, 229 ± 10 J/K mol, is their heat capacities
  integrated (182.90 from 78.7 to 298.15 K, trapezoids) and a Debye curve fitted to their 78.7 to 100 K points below
  (46): a derived value; only its difference from phthalate's anchor enters, and that is fitted to the solubility.
- **Solubilities, the gates** (`measured: each salt saturates its solution at its measured solubility`), the
  saturation index at the measured molality: NaCl 6.157 mol/kg at 25 C and 6.490 at 80 (IUPAC-NIST Solubility Data
  Series 47, the evaluators' values), +0.007 and +0.076 (Pitzer's 25 C parameters held); KCl 4.769 and 6.139 at 60
  (SDS 47), −0.045 and −0.052; NH₄Cl 7.393 (SDS 47), −0.10, past Pitzer and Mayorga's 6 mol/kg; NaHCO₃ 1.2207 (Zhou
  et al. 2017, ThermoML) and 1.720 at 50 C (Seidell 1919: Dibbits, Fedotieff), +0.004 and +0.003; the decahydrate
  2.79 at 25 C and 1.179 at 10 (Wells and McAdam 1907, Seidell 1919), +0.011 and +0.077; KHP 0.558 at 25 C and 0.710
  at 35 (Hendrixson 1920, Seidell 1940), nought by the calibration below. Anhydrous Na₂CO₃ dissolves past the
  decahydrate's saturation, as it does.
- **Their growth** is copper sulfate's pentahydrate's (Stage 23), a stand-in: no rate read. Each dissolves at
  transport's pace (Stage 57), which is what an experiment weighs it for.
- Not modelled: sodium acetate's trihydrate (no heat capacity read), the phosphates' hydrates, KHP's own Pitzer pair
  with K⁺ (none exists in the sources read).

## Indicators (Stage 64)

Phenolphthalein, bromothymol blue and methyl red, and phthalic acid (KHP's). Read 2026-11-26.

- **Phenolphthalein** (C₂₀H₁₄O₄): Tamura, Abe, Ito and Maeda 1996 (Anal. Sci. 12, 927, J-STAGE): the colourless
  lactone H₂PP and HPP⁻, pK1 9.05, the red dianion PP²⁻, pK2 9.50 (25 ± 2 C, 0.2 M phosphate buffers). Its fading to
  the colourless carbinol in strong base (Tam and Chau 1992: k1 0.43 per M per s; Tamura's own is fifty times slower)
  is not modelled. Its dianion's spectrum is **read from published plots**: Tamura's Fig. 1, the pH 11.5 curve, traced
  (552.8 nm, 3.71 × 10⁴ L/mol/cm, its second band near 376 nm), the wavelengths moved +2.8 nm to the paper's stated peak.
- **Bromothymol blue** (C₂₇H₂₈Br₂O₅S): the yellow monoanion and the blue dianion (Shimada and Hasegawa 2017,
  Spectrochim. Acta A 185, 104), the thermodynamic pKa 7.5; Schill's 7.12 at an ionic strength of 0.1 (quoted by
  Avdeef 2023) is the gate on the activity model, which gives 7.10 there. Spectra: Hoang et al. 2026 (J. Sci. Tech.,
  Le Quy Don Tech. Univ., Fig. 2(d)), traced every 5 nm and scaled to the 49th IChO's (2017) 16,600 at 430 nm and
  38,000 at 620 nm. The red form below pH 1 is left out (its spectrum unread).
- **Methyl red** (C₁₅H₁₅N₃O₂): the red zwitterion HMR and the yellow MR⁻, pKa 4.82 at 25 C (Ramette, Dratz and Kelly
  1962, from IUPAC's digitised pKa dataset; Tobey 1958 measured 5.02 at 27.2 C). Spectra: Tobey's Fig. 2, his plotted
  points over 2.97 × 10⁻⁵ M (his recipe): HMR 520 nm, 4.42 × 10⁴; MR⁻ 425 nm, 1.84 × 10⁴ (Merck's specification agrees;
  the IChO's 21,500 for the red form, half, is not taken). H₂MR⁺ below pH 2.5 is left out.
- **Their thermodynamics**: each fully deprotonated form anchored at nought (no reaction makes or breaks the dye), the
  others by the acid's constant with no heat of ionisation: none was read, so their colours do not move with
  temperature. Their diffusion, 3.8 to 4.5 × 10⁻¹⁰ m²/s, is the mean of the Wilke–Chang and Hayduk–Minhas correlations
  (no measurement read); acetic acid's 1.35 × 10⁻⁹ and phosphoric acid's 1.25 × 10⁻⁹ likewise.
- **Phthalic acid**: Hamer, Pinching and Acree 1945 (J. Res. NBS 35, 539): pK1 2.9496 at 25 C, ΔH −2.665 kJ/mol, ΔS
  −65.4, ΔCp −90.0; Hamer and Acree 1945 (J. Res. NBS 35, 381): pK2 5.4083, ΔH −2.074, ΔS −110.5, ΔCp −293.3 (minteq.v4.dat
  agrees to 0.001). Its anchor, phthalate's ΔfH −749.62 kJ/mol and S° 89.23 J/K mol, is **calibrated** so that KHP
  saturates at Hendrixson's 0.558 and 0.710 mol/kg (25 and 35 C): KHP's heat of solution then 28.1 kJ/mol. 0.05 mol/kg
  KHP reads pH 3.987 against IUPAC's primary standard 4.005 (`measured: phthalic acid's constants`), K⁺ with hydrogen
  phthalate given no Pitzer pair.
