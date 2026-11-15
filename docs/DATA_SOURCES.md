# Data sources

Every physical constant and material property used by the game, with its source and a confidence.
A value enters `data/species.json` or `data/reactions.json` only with a row here (GAME_SPEC §2,
principle 12).

**Status of this file: unverified draft.** Every value below was written from memory during
planning and has not yet been checked against a reference. The data stage of each system (Stages
2–6) verifies its rows against the CRC Handbook, the NIST Chemistry WebBook or NBS tables, records
the citation, and only then uses them. Until a row's "Verified" column is filled it must not be
relied on.

Confidence: **high** (well-known tabulated value), **medium** (tabulated, but sources differ or
recall is uncertain), **low** (estimate or game calibration).

## Species (298.15 K, 1 bar)

| Species | M (g/mol) | ΔHf° (kJ/mol) | Cp (J/mol·K) | S° (J/mol·K) | ρ (g/cm³) | Confidence | Verified |
|---|---|---|---|---|---|---|---|
| Cu2CO3(OH)2(s), malachite | 221.114 | −1054.0 (older tables −1051.4) | 154.3 | 166.3 (older 186.2) | 4.0 | medium | |
| CuO(s) | 79.545 | −157.3 | 42.3 | 42.6 | 6.31 | high | |
| CO2(g) | 44.009 | −393.51 | 37.1 | 213.7 | ideal gas | high | |
| H2O(l) | 18.015 | −285.83 | 75.3 | 69.9 | 0.997 | high | |
| H2O(g) | 18.015 | −241.82 | 33.6 | 188.8 | ideal gas | high | |
| N2(g) | 28.014 | 0 | 29.1 | 191.6 | ideal gas | high | |
| O2(g) | 31.998 | 0 | 29.4 | 205.1 | ideal gas | high | |
| H2SO4(aq) | 98.072 | −909.3 (fully ionised) | to be chosen so 1 M solution is about 3.9 J/g·K | 20.1 | 1.060 (1 M solution) | H high; Cp, S low | |
| CuSO4(aq) | 159.602 | −844.5 (infinite dilution); effective value fitted to solubility, about −836.5 | to be fitted | −79.5 (infinite dilution); effective value fitted | n/a | H medium; rest low | |
| CuSO4·5H2O(s) | 249.677 | −2279.7 | 280 | 300.4 | 2.286 | high | |
| CuSO4·3H2O(s) | 213.647 | −1684.3 | 205 | 221.3 | about 2.66 | medium | |
| CuSO4·H2O(s) | 177.617 | −1085.8 | 134 | 146.0 | about 3.2 | medium | |
| CuSO4(s) | 159.602 | −771.4 | 100 | 109.2 | 3.60 | high | |
| Borosilicate glass (as SiO2) | 60.08 | inert | about 50 | about 41.5 | 2.23 | medium | |

Molar masses are derived by the build from the element counts; the column is a cross-check.

## Oracles (test references, never inputs)

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

## Kinetics (game-calibrated)

| Process | Form | Parameters | Confidence |
|---|---|---|---|
| Malachite decomposition | first order, Arrhenius | Ea = 150 kJ/mol (literature spans about 100–210), A = 4.7e10 /s | low |
| CuO dissolution in dilute H2SO4 | `k(T) · f_stir · (c_acid / 1 M) · n0^(1/3) n^(2/3)` | Ea about 50 kJ/mol, k(25 °C) = 2e-3 /s, f_stir about 3 | low |

## Optics

| Medium | Model | Confidence |
|---|---|---|
| Cu²⁺(aq) | Gaussian in wavenumber, centre 12,350 cm⁻¹ (about 810 nm), FWHM about 5,000 cm⁻¹, peak ε about 12 /M·cm | peak medium, width low |
| Malachite powder | reflectance, roughly sRGB (70, 150, 110) | low |
| CuO | reflectance, roughly sRGB (30, 28, 28) | low |
| CuSO4·5H2O | reflectance, roughly sRGB (35, 95, 200) | low |
| CuSO4·3H2O, CuSO4·H2O | reflectance, roughly sRGB (190, 220, 235) | low |
| CuSO4 anhydrous | reflectance, roughly sRGB (235, 235, 225) | low |

## Constants

CODATA 2018: R = 8.314462618 J/mol·K; standard atmosphere 101325 Pa; T0 = 298.15 K.
