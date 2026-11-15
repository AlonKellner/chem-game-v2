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
| H2SO4(aq) | 98.072 | −909.3 (fully ionised) | to be chosen so 1 M solution is about 3.9 J/g·K | 20.1 | 1.060 (1 M solution) | H high; Cp, S low | Stage 6 |
| CuSO4(aq) | 159.602 | −844.5 (infinite dilution); effective value fitted to solubility, about −836.5 | to be fitted | −79.5 (infinite dilution); effective value fitted | n/a | H medium; rest low | Stage 6 |
| CuSO4·5H2O(s) | 249.677 | −2279.7 | 280 | 300.4 | 2.286 | high | Stage 6 |
| CuSO4·3H2O(s) | 213.647 | −1684.3 | 205 | 221.3 | about 2.66 | medium | Stage 6 |
| CuSO4·H2O(s) | 177.617 | −1085.8 | 134 | 146.0 | about 3.2 | medium | Stage 6 |

### Oracles (test references, never inputs)

**Water saturation pressure, Antoine (mmHg, °C):** A = 8.07131, B = 1730.63, C = 233.426 for
1–100 °C; A = 8.14019, B = 1810.94, C = 244.485 for 99–374 °C. Confidence high. The two sets
differ by about 0.5% at 100 °C.

**CuSO4 solubility (g anhydrous per 100 g water).** Confidence medium-high; sources differ by ±0.5.

| °C | 0 | 10 | 20 | 30 | 40 | 50 | 60 | 80 | 100 |
|---|---|---|---|---|---|---|---|---|---|
| g | 14.3 | 17.2 | 20.5 | 24.4 | 28.7 | 33.8 | 39.5 | 55.5 | 75.4 |

**Hydrate dissociation:** 5→3 about 7.8 torr at 25 °C; in air, onsets near 63 °C (5→3), 110 °C
(3→1), 200–250 °C (1→0). Pentahydrate/trihydrate transition in contact with saturated solution
about 95.9 °C. Confidence medium.

### Kinetics (game-calibrated)

| Process | Form | Parameters | Confidence |
|---|---|---|---|
| CuO dissolution in dilute H2SO4 | `k(T) · f_stir · (c_acid / 1 M) · n0^(1/3) n^(2/3)` | Ea about 50 kJ/mol, k(25 °C) = 2e-3 /s, f_stir about 3 | low |

### Optics

| Medium | Model | Confidence |
|---|---|---|
| Cu²⁺(aq) | Gaussian in wavenumber, centre 12,350 cm⁻¹ (about 810 nm), FWHM about 5,000 cm⁻¹, peak ε about 12 /M·cm | peak medium, width low |
| Malachite powder | reflectance, roughly sRGB (70, 150, 110) | low |
| CuO | reflectance, roughly sRGB (30, 28, 28) | low |
| CuSO4·5H2O | reflectance, roughly sRGB (35, 95, 200) | low |
| CuSO4·3H2O, CuSO4·H2O | reflectance, roughly sRGB (190, 220, 235) | low |
| CuSO4 anhydrous | reflectance, roughly sRGB (235, 235, 225) | low |

## Constants

CODATA 2018: R = 8.314462618 J/mol·K (used: `chem/thermo.hpp`). T0 = 298.15 K. Standard atmosphere 101325 Pa (from Stage 4).
