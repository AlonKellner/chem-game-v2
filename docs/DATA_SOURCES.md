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
| CuSO4(aq) | −839.382 (fitted) | −10.095 (fitted) | −96.875 (chosen) | **Fitted, not tabulated.** See below. For reference, Atkins's ions give Cu²⁺(aq) + SO4²⁻(aq) = −844.50 kJ/mol and −79.5 J/mol·K |

**The fit of CuSO4(aq).** The game treats the dissolved salt as one neutral species in an
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

## Calibrated kinetics (Stage 6)

None of these is measured. Each is chosen so the demo plays in minutes, and is labelled here and
in GAME_SPEC §20.

| Process | Law | Parameters | Gives |
|---|---|---|---|
| CuO dissolving in the acid | surface, driven by the acid's molality | 1.15e6 mol/m²/s, Ea 50 kJ/mol | 3.6 g in 2.5 million grains under 100 mL of 1 M acid: 99% gone in 37 min at 20 °C still, 11 min stirred (×3), 3 min warmed and stirred |
| Pentahydrate growing and dissolving | surface | 3e-2 mol/m²/s, no activation energy | a crop finished about ten minutes after the burner goes off |
| Pentahydrate nucleating | with the above | 2e6 per kg of water per second × (S − 1)³ | about 140,000 crystals of 0.3 mm from the demo's flask |
| Pentahydrate losing its water | first order | 7.5e7 /s, Ea 80 kJ/mol | 1e-2 /s at 150 °C, 5e-4 /s at 100 °C, a half-life of nine hours at 60 °C |

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
| CuSO4·3H2O(s) | 213.647 | −1684.3 | 205 | 221.3 | about 2.66 | medium | after the demo: no source found yet; inert until then |
| CuSO4·H2O(s) | 177.617 | −1085.8 | 134 | 146.0 | about 3.2 | medium | after the demo: no source found yet; inert until then |

### Oracles (test references, never inputs)

**Hydrate dissociation:** 5→3 about 7.8 torr at 25 °C; in air, onsets near 63 °C (5→3), 110 °C
(3→1), 200–250 °C (1→0). Pentahydrate/trihydrate transition in contact with saturated solution
about 95.9 °C. Confidence medium.

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

**Calibrated media** (`data/optics/media.json`). None of these spectra is measured: each is a
simple shape placed so that the substance has the colour it is known to have. The numbers quoted
as literature are from memory and were not checked against a source.

| Medium | Model | Basis | Confidence |
|---|---|---|---|
| CuSO₄(aq) | One Gaussian in wavenumber: centre 12,400 cm⁻¹ (806 nm), FWHM 4,700 cm⁻¹, peak ε 12 L/mol/cm | The d–d band of Cu(H₂O)₆²⁺: maximum near 800–810 nm with ε about 12 | peak medium, width low |
| Malachite | Diffuse reflectance 0.06 plus a Gaussian of 0.42 at 525 nm (width 45 nm) | A green mineral pigment | low |
| CuO | Diffuse reflectance 0.035, flat | A black powder | low |
| CuSO₄·5H₂O | Diffuse reflectance 0.05 plus a Gaussian of 0.5 at 465 nm (width 55 nm) | Blue crystals | low |
| CuSO₄ | Diffuse reflectance 0.82, flat | A white powder | low |
| Latex | Transmittance of the reference thickness: 0.04, rising by 0.9 across 600 nm (width 18 nm) | A red balloon | low |
| Water, H₂SO₄(aq), glass, gases | None: clear | Their absorption in the visible is far below what a few centimetres show | high for these path lengths |

**Calibrated geometry of solids:** a heap's angle of repose 35° and packing fraction 0.5 (typical
of dry powders); a suspended particle's extinction efficiency 2 (the large-particle limit).
**The room:** its white is 100 cd/m² (a white page in ordinary indoor light).

## Constants

CODATA 2018: R = 8.314462618 J/mol·K (used: `chem/thermo.hpp`). T0 = 298.15 K. Standard atmosphere 101325 Pa (from Stage 4).
