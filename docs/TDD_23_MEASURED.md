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
| 23.4 | The spec: the transport law, an order for the driver, nucleation | Done (GAME_SPEC §7, §9, §20) |
| 23.5 | The core: transport in series with the surface; measured diffusion coefficients | Done |
| 23.6 | The kinetics' values, each with an oracle test at its source's conditions | Done (`test_measured.cpp`) |
| 23.7 | The demo and the tutorial; the goldens' evidence for the user | Done: re-recorded with the user's approval |
| 23.8 | Pictures; the user judges and signs off | Done: signed off 2026-11-21 (GAME_SPEC 0.24.0) |

## What the research found (2026-11-21)

| Item | Found | Confidence |
|---|---|---|
| The solution's absorption | NBS Circular 484 (1949), Table 4, 350–750 nm; Davenet et al. 2025 at 790 nm | high 550–750 nm |
| Malachite's reflectance | USGS splib07a GDS801 (synthetic powder) | high |
| CuO, the four forms of the salt, latex: reflectance | not found in any library searched | stay calibrated |
| Malachite's decomposition | Aoki, Yamamoto, Koga, PCCP 23, 15107 (2021), ESI Tables S2, S3: two steps, Ea 147.6 kJ/mol, A 1.15e12 and 6.05e11 /s | high |
| CuO in dilute sulfuric acid | Majima et al., Metall. Trans. B 11, 209 (1980) via Nicol and Akilan, Hydrometallurgy 178, 7 (2018), Table 4: 2.5e-7 mol/cm²/min at 0.2 M, 25 °C; half order in H⁺; surface controlled (no effect of agitation); Ea 52–86 kJ/mol across four acids | low (second-hand) |
| The pentahydrate's growth | Tanimoto, Kobayashi, Fujita, Kagaku Kogaku 27, 424 (1963): surface step first order in the concentration difference, kr 0.030, 0.063, 0.120 m/h at 20, 30, 40 °C, Ea 51.5 kJ/mol; in series with mass transfer | high |
| The pentahydrate's nucleation | no rate law; induction times (Sheng, PhD thesis, Leeds 2012, Table 5.2) fit B = 0.3269 (S − 1)^3.109 per kg of water per second (the first crystal inside each of Sheng's measured ranges: `test_measured.cpp`) | medium |
| Dehydration | Cheng et al., J. Therm. Anal. Calorim. 135, 2697 (2019), abstract: Ea 71.3, 112.1, 165.2 kJ/mol | medium |

The full reports' source lists are kept with the rows in `docs/DATA_SOURCES.md` as each value is used.

## What was built (23.4–23.7)

- **The surface law's two steps** (`surface_steps`, `chem/sim.hpp`): at the surface
  `k(T) m_driver^q f(A)`; by transport `(Sh D / d)|C − C_eq| / ν`, `Sh = 2 × mixing`; in series.
  `C_eq` comes from Newton's steps in `ln n` on the affinity: one or two near equilibrium. A first
  version took the activity as ideal (`a ∝ m^ν`): copper sulfate's coefficient falls so steeply
  that its gap came out half the true one, and a crystal dissolving into pure water got a
  twenty-ninth. Found by the oracles, once their comparisons were made relative (doctest's
  `Approx` has a scale of 1, under which every rate of 1e-5 compares equal).
- **Diffusion** (`diffusion`, `solvent_viscosity`, `chem/thermo.hpp`): the particles' weighted
  harmonic mean (Nernst–Hartley for two ions), as T / η; the data in `data/solutions.json`;
  refused when negative or not rising (`SpeciesTable::make`), or naming an unknown ion, or a
  driver's order with no driver (`datagen_refuses_*`).
- **The debug panel** names the step that limits: "limited at its surface", "limited by transport
  to its surface", or both alike, and how much faster stirring would make it.
- **The demo and the tutorial**: the crystals' wait is two hours of the lab's time (twelve seconds
  at 600x); the stirring step's reason reworded.

## The evidence for the goldens (23.7)

The demo, `make demo`, before and after:

| | Before (calibrated) | After (measured) |
|---|---|---|
| Malachite gone (stage 1) | 229 s | 139 s (Koga's rate is 20 times the old near 250 °C) |
| The oxide gone, stirred and warm (stage 2) | 228 s | 529 s |
| Crystals in the flask | 9.27 g in about 1,000,000 crystals of 0.20 mm, 15 min after cooling | 9.21 g in 1,155 of 1.88 mm, 2 h after cooling |
| Simulated time | 2,680 s | 9,167 s |
| Step cost (`make demo-bench`) | 6.94 µs | 6.25 µs |

With the crystals' wait left at 15 minutes, the crop would be 0.76 g: the goal (8 g) is reached
after about 80 minutes, the whole crop by three hours.
