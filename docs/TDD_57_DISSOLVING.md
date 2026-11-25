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
| 57.1 | The sources, GAME_SPEC, DATA_SOURCES | |
| 57.2 | The core: Sano's transport, dissolving at transport's pace; the measured gates | Done |
| 57.3 | The goldens: the evidence and the user's approval | Done |
| 57.4 | The experiment: the solubility curve | |
| 57.5 | This document, CHANGELOG, `make gates`, the user's sign-off | |
