# TDD 23: Measured data (Stage 23)

## The request

The roadmap's measured data: each calibrated value replaced by a measured one with its source,
where one exists, and the goldens re-recorded with the user's approval. Decided by the user on
2026-11-21:
- **Stirring by a general transport law**: each surface reaction is its surface rate in series
  with transport to the particle (measured diffusion coefficients, the particle's size; stirring
  raises transport), not a factor on every rate.
- **Copper oxide's dissolution rate**: the measured rate, though second-hand, flagged low.
- **Nucleation fitted to measured induction times**: fewer, larger crystals.

GAME_SPEC §7, §9, §12, §20; `docs/DATA_SOURCES.md`.

| Phase | Topic | Status |
|---|---|---|
| 23.1 | Research: the solution's spectrum, the solids' reflectances, the kinetics | Done: see below |
| 23.2 | The solution's spectrum (NBS Circular 484) | Done |
| 23.3 | Malachite's reflectance (USGS splib07a GDS801) | Done |
| 23.4 | The spec: the transport law, an order for the driver, nucleation | Pending |
| 23.5 | The core: transport in series with the surface; measured diffusion coefficients | Pending |
| 23.6 | The kinetics' values, each with an oracle test at its source's conditions | Pending |
| 23.7 | The demo and the tutorial; the goldens' evidence for the user | Pending |
| 23.8 | Pictures; the user judges and signs off | Pending |

## What the research found (2026-11-21)

| Item | Found | Confidence |
|---|---|---|
| The solution's absorption | NBS Circular 484 (1949), Table 4, 350–750 nm; Davenet et al. 2025 at 790 nm | high 550–750 nm |
| Malachite's reflectance | USGS splib07a GDS801 (synthetic powder) | high |
| CuO, the four forms of the salt, latex: reflectance | not found in any library searched | stay calibrated |
| Malachite's decomposition | Aoki, Yamamoto, Koga, PCCP 23, 15107 (2021), ESI Tables S2, S3: two steps, Ea 147.6 kJ/mol, A 1.15e12 and 6.05e11 /s | high |
| CuO in dilute sulfuric acid | Majima et al., Metall. Trans. B 11, 209 (1980) via Nicol and Akilan, Hydrometallurgy 178, 7 (2018), Table 4: 2.5e-7 mol/cm²/min at 0.2 M, 25 °C; half order in H⁺; surface controlled (no effect of agitation); Ea 52–86 kJ/mol across four acids | low (second-hand) |
| The pentahydrate's growth | Tanimoto, Kobayashi, Fujita, Kagaku Kogaku 27, 424 (1963): surface step first order in the concentration difference, kr 0.030, 0.063, 0.120 m/h at 20, 30, 40 °C, Ea 51.5 kJ/mol; in series with mass transfer | high |
| The pentahydrate's nucleation | no rate law; induction times (Sheng, PhD thesis, Leeds 2012, Table 5.2) fit B = 0.374 (S − 1)^3.07 per litre per second | medium |
| Dehydration | Cheng et al., J. Therm. Anal. Calorim. 135, 2697 (2019), abstract: Ea 71.3, 112.1, 165.2 kJ/mol | medium |

The full reports' source lists are kept with the rows in `docs/DATA_SOURCES.md` as each value is used.
