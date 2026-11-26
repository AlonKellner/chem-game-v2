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
  from PHREEQC's pitzer.dat, CuOH+ from its minteq.v4.dat (`docs/DATA_SOURCES.md`, Ions as species). Only what is
  dissolved may be charged (the table refuses a charged solid or gas), and a world is built neutral.
- **The old formula units are retired**, kept in the table (it grows only at its end; an old save still loads): a
  bench's or a save's amount of one is taken as its ions (`copper_sulfate_aq` 0.05 mol: Cu2+ 0.05 and SO4^2- 0.05).
  No reaction names them. A lab's reading of one (a tutorial's check, a goal, a report's truth) is as many of it as
  the item's dissolved matter could make, element by element: copper sulfate reads the copper dissolved in any of its
  ions, the acid its sulfur and hydrogen, the bisulfate's included.
- **Reactions in ions.** The neutralisation is water's own ionisation reversed, H+ + OH- -> H2O, at its measured
  constant; the hydroxide's precipitation Cu2+ + 2 OH- -> Cu(OH)2; its dissolving in acid, Cu(OH)2 + 2 H+, the two
  together, is kept a fast equilibrium: in acid the hydroxide ions are a few quanta, and the hydroxide could leave
  only that many at a time by the other two (the joint solve drops it as dependent). New fast equilibria:
  HSO4- = H+ + SO4^2-, Cu2+ + H2O = CuOH+ + H+. The oxide and malachite dissolve in H+ (their surface rates fitted
  again at their sources' own conditions, H+ their driver); the crystals grow from Cu2+ and SO4^2-.
- **The dimer is left out.** Cu2(OH)2 2+ (MINTEQ's log K -10.36) with no interaction parameters held 70% of the copper
  of a solution boiled down to 0.07 g of water at 150 C: without Pitzer's terms for it, its share at strength is
  meaningless. With CuOH+ alone 0.5 mol/kg copper sulfate reads pH 4.8 (measured about 4; GAME_SPEC §20).
- **A reaction as a student writes it**: `"written"` in reactions.json, in formula units (CuO + H2SO4 -> CuSO4 + H2O);
  the generator refuses one that is not a whole multiple of the reaction once its units are their ions
  (`datagen_refuses_written_not_the_reaction`). The calculator, the planner and the reference use it.
- **Electroneutrality** (N1): no step changes a cell's charge. `World::transfer` moves ions only as neutral matter: what
  is asked is trimmed by the fewest quanta to no net charge (`neutral_part`), so every pour, drain and layer's move
  stays neutral without its caller knowing. A column's diffusion between layers is Nernst and Planck's at no
  current (a fast ion drags its counter-ion; a single salt crosses at Nernst and Hartley's coefficient, the gate
  holding Fick's rate at 2.932e-9 against 2.93247e-9 mol), each direction made neutral by a carrier of the opposite
  charge moving both ways; an ion carrying under a thousandth of the layer's conductance (a trace) is held to its own
  bound, or one trace would hold every ion back (it held them to a tenth). Transport to a grain carries an ion with
  the reaction's other ions on its side, at Nernst and Hartley's coefficient.
- **One solver for a cell's fast equilibria** (`Simulation::solve_together`): after each round of the per-reaction
  projections, those whose species are all there (none a gas; a dissolved one of 64 quanta or more) are solved
  together: a quasi-Newton step on their extents, the residuals the full affinities, the slope the ideal solution's
  (-RT sum nu_r nu_s / n), damped so no dissolved amount falls below a tenth; a solid may be used up (its reaction then
  leaves), as may a dissolved species fall to a few quanta, and a reaction found dependent (a zero pivot) leaves. The
  extents are rounded and applied only while each runs down its own affinity (T1); the next round's projections finish
  each to its quantum (P1 unchanged). A move of a quantum or so is left to the projections (no ping-pong).
- **The activity model's range**: past an ionic strength of 20 mol/kg the equilibria among dissolved species alone
  stand (`stands`, P1 exempts them): boiled down, a solution's bisulfate and hydrolysis otherwise ran to a third of the
  sulfate as bisulfate in the last trace of water.
- **pH**: −log10 of H+'s activity. The difference m_H - m_OH is what water's ionisation leaves unchanged, so `ph` solves
  the free ions from it as before, exact below a quantum of either; its Kw the measured (Stage 43), where the tables'
  own ionisation has the formula units' heat capacity (pKw 12.05 at 100 C against 12.25: GAME_SPEC §20).
- **The screen**: ions written as chemists write them (`chem::written`: Cu2+, SO4 2-, H+); a dissolved trace that reads
  nothing at the readout's resolution is not listed; a column's contrast between layers sums its species'
  differences (a salt split into ions keeps its own), and a split takes the steepest step that can still be halved.
- **The activity model**: sulfuric acid by Pitzer, Roy and Silvester (H+–HSO4- and H+–SO4^2-, with K2), in place of
  the fully dissociated fit.

## Phases

| Phase | Topic | Status |
|---|---|---|
| 63.1 | The data: ions' thermodynamics, HSO4-, CuOH+, sulfuric acid's Pitzer model; sources and gates | done (sources and gates: `docs/DATA_SOURCES.md`, Ions as species; copper with the bisulfate calibrated) |
| 63.2 | The tables: ion species, retired formula units; the loader's and the saves' conversion | done (the builder takes a retired unit as its ions; benches exactly, in quanta) |
| 63.3 | The reactions in ions; the surface rates fitted again | done (Majima 0.5027, 70.22 kJ; Nicol 0.6812, 55.372 kJ; the hydroxide's dissolving kept) |
| 63.4 | The solver: the fast equilibria together; electroneutrality; a column's diffusion as electrolytes | done (`solve_together`; N1; Nernst and Planck at no current; `stands`) |
| 63.5 | pH; the measured gates (acidity, precipitation, solution, measured) | done |
| 63.6 | The lab, the screen, the benches, tutorials and spectra in ions | done (readings by `formula_units`; written equations; spectra; the readout) |
| 63.7 | The goldens: the evidence and the user's approval | done (demo and pressure re-recorded with the user's approval) |
| 63.8 | This document, GAME_SPEC, CHANGELOG, `make gates`, the user's sign-off | gates passed (core, ASan+UBSan, Godot 13/13, goldens; ctest 593/593 rerun alone after `signals.12`'s timing ran over under the gates' load: 4.6 ms against 3, 1.8 alone); sign-off pending |

## Open

- **A trace of a solid darkens its vessel** (found in the demo's optics test, `optics: through the demo the screen's
  scene holds O1 and O2`). With the oxide's dissolving slower at its end (the sulfate made holds the last of the acid
  as bisulfate, so little free H+ drives it), the demo once boiled its flask down with 3 µg of the oxide's 2.5 million
  grains of 0.72 µm still in it: the flask read L* 24, b* -9 at the test's pixel, where with that trace tipped out it
  reads 60.3, -41.5, exactly main's. The pass agrees with its truth there, so it is the model of what lies in a vessel
  and how it is lit, not the pass: the solids' colour is their mass-weighted reflectance whatever their amount (a lone
  trace is its own black), and the haze's extinction from the grains' cross-sections (2 x 0.01 cm2 over 12 mL,
  under 0.2% a centimetre) and the heap at rest (under a pixel) are both far too small to darken it so. The cause
  is not found. The demo's session and tutorial now wait for the oxide to be gone (under a microgram, 79 s after a
  milligram), which is what "dissolved" means; the trace is no longer there to show it. To find: what in the lying
  model or the solid's colour lets micrograms cover a pixel.
