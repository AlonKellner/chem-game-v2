# Data sources

Every physical constant and material property used by the game, with its source. A value enters
`data/species.json` or `data/reactions.json` only with a verified row here (GAME_SPEC §2,
principle 12). Molar masses are derived by the build from the formulas and the standard atomic
weights of `data/elements.json`.

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
10.1021/acs.cgd.4c00589), give 10 and 6 mbar for the first two (7.5 and 4.5 torr).

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
on a basis the text leaves unclear. The model gives 0.70 with 1.0 mol/kg of acid at 15 °C.

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

## Calibrated kinetics (Stage 6)

None of these is measured. Each is chosen so the demo plays in minutes, and is labelled here and
in GAME_SPEC §20.

| Process | Law | Parameters | Gives |
|---|---|---|---|
| CuO dissolving in the acid | surface, driven by the acid's molality | 1.15e6 mol/m²/s, Ea 50 kJ/mol | 3.6 g in 2.5 million grains under 100 mL of 1 M acid: 99% gone in 37 min at 20 °C still, 11 min stirred (×3), 3 min warmed and stirred |
| Pentahydrate growing and dissolving | surface | 3e-2 mol/m²/s, no activation energy | a crop finished about ten minutes after the burner goes off |
| Pentahydrate nucleating | with the above | 2e6 per kg of water per second × (S − 1)³ | about 140,000 crystals of 0.3 mm from the demo's flask |
| Pentahydrate losing its water (Stage 6, replaced) | first order, 5 → 0 | 7.5e7 /s, Ea 80 kJ/mol | 1e-2 /s at 150 °C, 5e-4 /s at 100 °C, a half-life of nine hours at 60 °C |
| The trihydrate, monohydrate and anhydrous salt growing, dissolving and nucleating (Stage 19) | surface, with nucleation | the pentahydrate's: 3e-2 mol/m²/s, 2e6 per kg per second × (S − 1)³ | no source gives them apart |
| The hydrates losing water, 5 → 3, 3 → 1, 1 → 0 (Stage 19) | first order | Ea 80 kJ/mol for each; prefactors 3.9e9, 4.3e8 and 4.5e5 /s | each step's rate constant at its DTG peak is the first-order peak condition at 10 °C/min, k = β Ea / (R Tp²): peaks at 90.0, 120.2 and 255.9 °C (Yu Yuye, *Asian J. Chem.* 19, 2023, 2007, Table 2, 5–10 mg in flowing nitrogen) |

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
| Malachite decomposition | first order in malachite | Ea = 150 kJ/mol; prefactor 4.7e10 /s | Order about one and Ea 131–159 kJ/mol (31.4–38.1 kcal/mol) from thermogravimetry: *Canadian Mineralogist* 13, 127 (rruff.info/uploads/CM13_127.pdf). Ea is taken inside that range. **The prefactor is calibrated for play**, not measured: it gives rate constants of 1e-3 /s at 300 °C and 1.3e-2 /s at 350 °C, so 5 g decomposes in minutes over a burner |

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
| Water as an absorber | The coefficients above, summed (scattering counted as loss from the beam), over water's own 55.345 mol/L (997.05 g/L at 25 °C over 18.015 g/mol), as a decadic molar absorptivity: so a solution's water absorbs as much as there is of it | Not modelled: in-scattered light, the temperature dependence of a (at most 0.012 /m/°C, near 740 nm) |

**Calibrated media** (`data/optics/media.json`). None of these spectra is measured: each is a
simple shape placed so that the substance has the colour it is known to have. The numbers quoted
as literature are from memory and were not checked against a source.

| Medium | Model | Basis | Confidence |
|---|---|---|---|
| CuSO₄(aq) | One Gaussian in wavenumber: centre 12,400 cm⁻¹ (806 nm), FWHM 4,700 cm⁻¹, peak ε 12 L/mol/cm | The d–d band of Cu(H₂O)₆²⁺: maximum near 800–810 nm with ε about 12 | peak medium, width low |
| Malachite | Diffuse reflectance 0.06 plus a Gaussian of 0.42 at 525 nm (width 45 nm) | A green mineral pigment | low |
| CuO | Diffuse reflectance 0.035, flat | A black powder | low |
| CuSO₄·5H₂O | Diffuse reflectance 0.05 plus a Gaussian of 0.5 at 465 nm (width 55 nm) | Blue crystals | low |
| CuSO₄·3H₂O | Diffuse reflectance 0.3 plus a Gaussian of 0.4 at 470 nm (width 70 nm) | "Pale blue" (*Handbook of Mineralogy*, bonattite) | low |
| CuSO₄·H₂O | Diffuse reflectance 0.62 plus a Gaussian of 0.18 at 475 nm (width 80 nm) | A bluish white powder: its crystals are "clear light blue" (COD 7061268), a powder paler | low |
| CuSO₄ | Diffuse reflectance 0.82, flat | A white powder | low |
| Latex | Transmittance of the reference thickness: 0.04, rising by 0.9 across 600 nm (width 18 nm) | A red balloon | low |
| H₂SO₄(aq), glass, gases | None: clear | Their absorption in the visible is far below what a few centimetres show | high for these path lengths |

**Verified wall thicknesses (Stage 20)** (`data/benches/`, each glass vessel's `wall_mm`):

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

**Calibrated room for refraction (Stage 11):** the wall stands 20 cm behind the vessels' axes;
the room in front of the bench, which the glass reflects, has a radiance of 0.3 of the room's white
(darker than the lit wall: what a lab looks like from the bench). Both are chosen for a plausible
bench, not measured. A solution's index is water's: the solutes' share is not modelled.

**Calibrated geometry of solids:** a heap's angle of repose 35° and packing fraction 0.5 (typical
of dry powders); a suspended particle's extinction efficiency 2 (the large-particle limit).
**The room:** its white is 100 cd/m² (a white page in ordinary indoor light).

## Constants

CODATA 2018: R = 8.314462618 J/mol·K (used: `chem/thermo.hpp`). T0 = 298.15 K. Standard atmosphere 101325 Pa (from Stage 4).
