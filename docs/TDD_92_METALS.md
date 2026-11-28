# TDD 92: Metals and displacement (Stage 92)

## The request

After Stage 93's sign-off the user approved "merge and proceed": step F's third part (`docs/CURRICULUM.md`), metals
and displacement, experiments 46 (the activity series) and 11 (the copper cycle), with 28 (hydrogen's molar volume)
added by the user. GAME_SPEC 0.92.0.

The user's choices (2026-11-28, the plan and as the work met them):
- metals drawn **diffuse, their colour from measured n + k**, deposits darker (no renderer change);
- **zinc and iron in acid calibrated to class observation** (measured corrosion rates were then found and used);
- **the copper cycle from copper nitrate** (nitric acid and NO2 open);
- **experiment 28 added**;
- **bubbles stir**: a gas-making reaction's bubbles stir its liquid, a general mechanism, gated against Roald;
- **the base route to copper oxide**, calibrated: Cu(OH)4 2- (MINTEQ) and copper oxide growing from it.

| Phase | Topic | Status |
|---|---|---|
| 92.1 | Spec (§7, §9, §12); the sources read and their rows verified | Done |
| 92.2 | The metals, hydrogen, displacement, metals in acid; product grains; bubbles stir | Done |
| 92.3 | The metals' look (n + k by Fresnel, deposits darker, long wave) | Done |
| 92.4 | Copper's hydroxo complexes and the base route; the three experiments | Done |
| 92.5 | The sessions' digests (the user's approval), docs, `make gates`; the user's sign-off | open |

## 92.1 What there was

No solid metal (liquid mercury alone), no hydrogen; Cu2+, Zn2+, Fe2+, Mg2+ and Ag+ and their salts (Stage 76). The
surface law took a solid as its `in` and dissolved it; a solid it made came as one lump. Transport to a surface was a
stirrer's or diffusion's alone. A solid was drawn diffuse; mercury's metal reflection (Stage 78) is a liquid's surface's.

## 92.2 The chemistry (`docs/DATA_SOURCES.md`, Metals and displacement (Stage 92))

- **Species**: Cu, Zn, Mg, Fe, Ag (NBS's S and Cp read off the scan; NIST's densities; deposits as 20 µm grains,
  design); H2(g) (NBS; Marrero and Mason's 0.779 cm²/s in air); Cu(NO3)2 (retired).
- **Displacement**, ten pairs among Mg > Zn > Fe > Cu > Ag, and **magnesium in acid**: the surface law with the ion as
  driver, the surface far faster than transport, as Sareyed-Dim (Cu2+ on iron at the Levich rate, Ea 14.2 kJ/mol) and
  Roald (Mg in HCl, first order, Ea 17.2) measured. The model: Ea 17.2 for cementation, 14.2 for magnesium.
- **Zinc and iron in acid**: calibrated to Abdallah et al. 2026's 12.72 mm/year (99.99 % zinc, 1 M HCl, 294 K; Ea
  16.48) and Diki et al. 2021's 451 µA/cm² (mild steel, 1 M HCl, 298 K; Ea 92.23).
- **Core**:
  - a solid a surface reaction makes comes as grains no larger than its `grain_um` (`sim.cpp`, `react()`);
  - **bubbles stir**: the gas a cell's rated reactions made last step adds g u (W/kg) to the surface law's dissipation
    (`Simulation::bubbling_`); magnesium in Roald's still acid at 0.264 cm/min against his 0.20 (still liquid alone 0.0076).
- **Copper's hydroxo complexes** Cu(OH)3- and Cu(OH)4 2- (MINTEQ v4's constants; the hydroxide's dissolving in base
  taken as heatless, design) and **black copper oxide growing from Cu(OH)4 2-** (Cudennec and Lecerf's route; rate,
  nucleation and Ea calibrated to a class's copper cycle: half black in two minutes at 90 C in 0.1 M excess base, all in
  five; none in water or cold).

## 92.3 The look

`data/optics/metal_*.csv`: each metal's measured n + k (McPeak 2015 for copper and silver, Johnson and Christy 1974 for
iron, Palm 2018 for magnesium, Werner 2009 for zinc); `chem_datagen` turns a media entry's `nk` into its reflectance by
Fresnel at normal incidence, a diffuse colour; `fine` darkens grains under 100 µm (`optics::fine`, read where
`optics_scene.cpp` mixes a vessel's solids). Long wave: 1 − R at 10 µm (Ordal, Yang, Hagemann, Motulevich).

Gates (`native/tests/test_displacement.cpp`, eight cases): the rows; the colours; cementation and magnesium at their
transport limits and temperature dependence; iron and zinc at their corrosion rates; no displacement against the series
and the deposit's grains; the bubbles at Roald's still acid; copper's complexes and the base route.

## 92.4 The experiments

- **`hydrogen_volume`** (28): 0.050 g of magnesium ribbon in 20 mL of 1 M HCl, the hydrogen in a gas syringe; read once
  the flask is back under 20.5 C (the reaction warms it 6 K: read warm, 58 mL). Played: 52.7 mL; corrected for water's
  vapour, 25.05 L/mol (24.06 at 20 C), the rest the plunger's ground-glass friction holding it out as the gas cools
  (Stage 79's); R 8.66.
- **`activity_series`** (46): magnesium, zinc, iron and copper in 1 M HCl (three fizz); then five tubes, each shaken:
  magnesium puts zinc out, zinc iron, iron copper, copper silver, copper leaves zinc's ions alone. The report: the count
  that fizz, and the order (a single choice).
- **`copper_cycle`** (11): 0.500 g of copper as copper nitrate; hydroxide (sodium hydroxide in excess), oxide (heated in
  the excess base), sulfate (sulfuric acid in modest excess), copper (magnesium powder in excess), the leftover magnesium
  in hydrochloric acid; filtered, the dish cooled (warm, it read 10 mg low: its air buoys it) and weighed. Played: 0.4991 g,
  99.82 %.

Each has the three tests of `test_experiments.cpp` (strict with its gestures, as a person, free).

## Open

- Copper with nitric acid, NO and NO2 (gases are not drawn in the optics): the copper cycle's first step.
- A copper deposit speeding cementation 4–7 times; a ribbon's flat area (a piece is n equal spheres); natural
  convection at a grain (a still solution's transport is diffusion's: the experiments shake their tubes); a solid holds
  one grain size in a cell (zinc put out beside a zinc piece takes the piece's grains with it).
- True metal gloss (a solid metal's surface by Fresnel in the tracers), with the split-off reflection-branch work.
- Stage 81's zinc and aluminium hydroxo complexes carry water's ionisation heat (MINTEQ's delta_h 0 taken from the free
  ion); copper's are written from the hydroxide in base instead.
- Experiment 51 (alum; the copper-ammonia salt with ethanol) waits for ethanol (chem-game-v2-8d's Stage 99).
