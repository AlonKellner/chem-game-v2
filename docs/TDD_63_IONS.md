# TDD 63: Ions as species (Stage 63)

## The request

The curriculum's step D, its first stage (`docs/CURRICULUM.md`: C2), after Stage 62's sign-off: the user's
"Approved, merge and proceed" (2026-11-26), and the user's choices for step D (2026-11-26): **ions as species**, a
simultaneous solver for a cell's fast equilibria; **staged** as the core with today's chemistry (Stage 63), then new
elements, weak acids and bases and indicators (64), then the experiments and conductivity (65).

## What is there now (a survey, 2026-11-26)

- Every dissolved species is a neutral formula unit with fixed ions (`sulfuric_acid_aq` is 2 H+ and SO4^2-,
  `copper_sulfate_aq` Cu^2+ and SO4^2-, `sodium_hydroxide_aq`, `sodium_sulfate_aq`, `nickel_sulfate_aq`). The activity
  model (Pitzer) already works on the ions: a cell's particles are its species' ions, pooled.
- There is no H+, OH- or HSO4- species. The acid is fully dissociated; the pH is worked out afterwards from the pooled
  H+ and OH- (`chem::ph`), water's ionisation a constant beside the tables.
- Four fast equilibria (`neutralisation`, `hydroxide_precipitation`, `hydroxide_dissolution`, `water_vaporization`),
  each solved alone, in rounds (`Simulation::project`), at most 16.
- GAME_SPEC §20's rows this stage answers: no bisulfate (the neutralisation's heat 11 kJ short of its part), copper's
  hydrolysis as a trace of solid hydroxide (copper sulfate's pH 5 against a measured 4).

## The design

- **Ions are species.** H+, OH-, Cu2+, SO4^2-, HSO4-, Na+, Ni2+ and CuOH+, each aqueous, charged, its own particle
  (`ions`: itself). Their thermodynamic data at 25 C are the old formula units' split exactly, H+ at nought by the
  convention: SO4^2- is the old acid's, Na+ from the sulfate, OH- from the hydroxide, Cu2+ from copper sulfate's fitted
  values (Stage 19's), Ni2+ from nickel sulfate's; every existing solution keeps its energy and its solubility. HSO4-
  and CuOH+ from their measured constants.
- **The old formula units are retired**, kept in the table (it grows only at its end; an old save still loads): a
  bench's or a save's amount of one is taken as its ions (`copper_sulfate_aq` 0.05 mol: Cu2+ 0.05 and SO4^2- 0.05).
  No reaction names them.
- **Reactions in ions.** The neutralisation is water's own ionisation reversed, H+ + OH- -> H2O, at its measured
  constant; the hydroxide's precipitation Cu2+ + 2 OH- -> Cu(OH)2; its dissolving in acid follows from the two (its
  slot kept, no law). New fast equilibria: HSO4- = H+ + SO4^2-, Cu2+ + H2O = CuOH+ + H+. The oxide and malachite
  dissolve in H+ (their surface rates fitted again at their sources' own conditions, H+ their driver); the crystals
  grow from Cu2+ and SO4^2-.
- **Electroneutrality**: every cell's charge is nought (an invariant); what moves a dissolved species alone moves its
  counter-ions with it (a column's diffusion between layers: each electrolyte as a whole, Nernst and Hartley's).
- **One solver for a cell's fast equilibria**: Newton's steps on their extents together (each step's Jacobian from
  the affinities, the activity model and all), a reaction whose solid is absent and whose affinity would make it left
  out; its result rounded towards equilibrium, then each reaction projected alone as before to its exact quantum (P1
  unchanged). The rounds that remain are one or two.
- **pH**: −log10 of H+'s activity, a species' own.
- **The activity model**: sulfuric acid by Pitzer, Roy and Silvester (H+–HSO4- and H+–SO4^2-, with K2), in place of
  the fully dissociated fit.

## Phases

| Phase | Topic | Status |
|---|---|---|
| 63.1 | The data: ions' thermodynamics, HSO4-, CuOH+, sulfuric acid's Pitzer model; sources and gates | |
| 63.2 | The tables: ion species, retired formula units; the loader's and the saves' conversion | |
| 63.3 | The reactions in ions; the surface rates fitted again | |
| 63.4 | The solver: the fast equilibria together; electroneutrality; a column's diffusion as electrolytes | |
| 63.5 | pH; the measured gates (acidity, precipitation, solution, measured) | |
| 63.6 | The lab, the screen, the benches, tutorials and spectra in ions | |
| 63.7 | The goldens: the evidence and the user's approval | |
| 63.8 | This document, GAME_SPEC, CHANGELOG, `make gates`, the user's sign-off | |
