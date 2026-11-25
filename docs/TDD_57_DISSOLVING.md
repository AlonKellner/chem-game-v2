# TDD 57: Crystals that dissolve, and a solubility curve (Stage 57)

## The request

The curriculum's step B2 (`docs/CURRICULUM.md`, capability C15), after Stage 56's sign-off: the user's "Approved,
merge and proceed" (2026-11-25). Stage 56 found the pentahydrate dissolving as slowly as it grows: stirred, 5 g in
10 g of water took an hour to saturate, and heated slowly, its last crystal went 50 K above its saturation
temperature. A real crystal of half a millimetre dissolves in a stirred beaker in minutes. The solubility curve
(curriculum 4) and ΔH and ΔS from it (45) wait on this.

## What is wrong, and what is measured

- **The surface step is growth's.** Tanimoto, Kobayashi and Fujita (1963) measured the pentahydrate's growth as a
  surface step `kr (C − C_s)` in series with mass transfer; the model runs that step backwards for dissolving. A
  crystal dissolving has no lattice to build: its surface gives up its ions at once, and transport through the
  liquid's film sets the pace (Mullin, *Crystallization*, 4th ed., 2001: dissolution is generally diffusion
  controlled). For the pentahydrate even growth is largely transport's: 58 to 99 % of its resistance (Leeds,
  Crystallisation of copper sulphate pentahydrate, 2019).
- **The film is stagnant's, times three when stirred.** Transport is `Sh D / d` with Sh = 2 at rest and 6 stirred,
  a design factor. Measured: Sano, Yamaguchi and Adachi, *J. Chem. Eng. Japan* 7(4), 255–261 (1974): suspended
  particles of 60–1100 µm (ion-exchange beads; dissolving granules of benzoic acid, KMnO₄, β-naphthol) in stirred
  vessels of 9.5 to 40 cm: `Sh = [2 + 0.4 (ε d⁴ / ν³)^(1/4) Sc^(1/3)] φc`, ε the dissipation per mass, within 31 %.
  At rest it is Sh = 2. Harriott (*AIChE J.* 8, 93, 1962): in agitated tanks 1.5 to 8 times a fixed sphere's at its
  terminal velocity.
- **Water's viscosity** (for ν and Sc): the table the diffusion coefficients already scale by (IAPWS 2008 through
  the NIST WebBook, Stage 23).

## What it builds

- The surface law's transport by Sano's correlation, from the cell's own stirring (the dissipation the lab already
  sets, Stage 49) over its liquid's mass; `mixing` no longer multiplies it.
- A crystal's dissolving at transport's pace (`dissolution: "transport"` in a reaction's kinetics: the four copper
  sulfate crystals): its surface step a thousand times its growth's, in series, so that transport sets the pace and
  the surface's factor still takes the rate to nothing at equilibrium (D3: a step with no surface step at all moved a
  saturated solution by the 1 % its gap is worked out to).
- The experiment: a solubility curve by the temperature at which the last crystal dissolves, slowly heated; ln of
  the solubility against 1/T, ΔH of solution.

## The goldens

- 2026-11-25, approved by the user: transport by Sano's correlation and crystals dissolving at transport's pace move
  the demo's results a little: the stirred copper oxide gone at 1010.1 s, not 1007.1 s (its fine grains' film a
  little thicker than the old factor of three made it); the crystals 9.197167 g, not 9.196864 g; the distillate
  37.764 g of water, not 37.787 g. The pressure session's results are the same. Both records re-recorded.

## Phases

| Phase | Topic | Status |
|---|---|---|
| 57.1 | The sources, GAME_SPEC, DATA_SOURCES | Done |
| 57.2 | The core: Sano's transport, dissolving at transport's pace; the measured gates | Done |
| 57.3 | The goldens: the evidence and the user's approval | Done |
| 57.4 | The experiment: the solubility curve | Done |
| 57.5 | This document, CHANGELOG, `make gates`, the user's sign-off | `make gates` passed (893 s: ASan+UBSan 893, ctest 705, Godot 13 of 13, goldens unchanged; a first run's `signals.12`, a frame's budget of 3 ms for three monitors, read 3.06 under the full load and passed alone 3 of 3); awaiting the user's sign-off |

## What was built

**The core** (`sim.cpp` `surface_steps`, `thermo.cpp`). The surface law's transport, `Sh D / d` its gap, with Sh
Sano's from the dissipation per kg of the cell's liquid (`dissipation_per_kg`: the cell's `stirring` watts over its
liquid's and dissolved species' mass; `liquid_density`; water's viscosity from its table); `mixing` no longer read. A
reaction whose kinetics say `"dissolution": "transport"` (`Kinetics::dissolves_by_transport`, from the data
generator) has, when its solid dissolves, its surface step times `DETACHMENT` (1000), in series. The lab's debug
panel: what limits a reaction read at the stirring's own dissipation; "stirring would make it faster" at a stirred
beaker's 0.1 W/kg.

**The experiment** `solubility` (13 steps): a prediction; four beakers, each the burner under it, stirred, a gentle
flame; until its last crystal goes; its temperature recorded, its solubility written, the flame off.

## Measured

- Stirred, 3 g of the pentahydrate in 10 g of water is down to 0.018 g in five minutes (it was an hour); heated at
  about 1 K/min, a mixture's last crystal goes 3 to 9 K past its saturation temperature, faster heating later.
- A 0.53 mm crystal's Sherwood number: 25, 43 and 76 at 0.01, 0.1 and 1 W/kg (2 still).
- The model's solubility: 19.75, 23.8, 28.8, 34.6 and 41.2 g per 100 g of water at 20, 30, 40, 50 and 60 C (the
  handbook's 20.7, 25.0, 28.5, 33.3, 40.0); van 't Hoff's slope 13.4 to 15.7 kJ/mol rising with the temperature,
  15.0 over 25–60 C.
- `solubility` at the bench's seed: the last crystals at 40.4, 48.9, 55.8, 60.8 C for 27.1, 31.5, 35.7, 39.7 g;
  16.1 kJ/mol (R² 0.997); 32.4 g at 50 C.

**Tests**:

| Test | What it checks |
|---|---|
| `test_measured.cpp` | the oxide's and the pentahydrate's transport at rest and at 0.1 W/kg against Sano's formula; a crystal dissolving at transport's pace; Sano's Sherwood number at 0.01, 0.1, 1 W/kg |
| `test_solution.cpp` | its benches stirred by watts, not the old factor; equilibrium untouched (the oracle's saturated solution moves nothing) |
| `test_experiments.cpp` (3) | `solubility` strict (the curve and its heat of solution from the notebook, the report, the prediction), at a person's pace, free |

## Open

- **Cooling's side**: a solution cooled nucleates only deeply supersaturated (Sheng's rate, a lower bound; Lyall et
  al. found a metastable zone of 3–4 K), so the crystals' reappearing temperature is not a lesson yet.
- **Natural convection** about a crystal on an unstirred floor: Sh = 2 there, slower than a real still beaker.
