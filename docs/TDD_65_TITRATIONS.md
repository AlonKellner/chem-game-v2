# TDD 65: Titrations, buffers and conductivity (Stage 65)

## The request

The curriculum's step D, its third stage (`docs/CURRICULUM.md`: experiments 14 to 20, C12), after Stage 64's
sign-off: the user's "Approved, merge and proceed" (2026-11-27), and the user's plan for step D (2026-11-26): the core
in ions (Stage 63), new elements, weak acids and bases and indicators (Stage 64), then **experiments 14 to 20 and
conductivity** (this stage).

## What is there now (a survey, 2026-11-27)

- Stage 64's chemistry: the weak acids and bases at their measured constants, CO2 and NH3 by Henry's law, the three
  indicators with their spectra, KHP, the salts; in the free lab's supplies, not yet in an experiment.
- The tools: a burette and a pipette (Stage 52), a pH meter (Stage 50, a quantity of a vessel's liquid read through
  `instruments.json`), the notebook's log and line fit (Stage 54), the spectrophotometer (Stage 55), an unknown drawn
  from a range by a bench's seed.
- No conductivity. Each ion's diffusion coefficient at 25 C is in `data/solutions.json` (Li and Gregory; Vanýsek),
  scaled to other temperatures by Stokes and Einstein, T / viscosity: 9 % low for KCl at 0 C, and 16 % low for H+ at
  5 C, against what is measured (its protons hop, Grotthuss, faster than its viscosity says).

## The design

- **Conductivity (§10, C12)**, a property of a solution read from its ions, no new state:
  - each ion's limiting molar conductivity by Nernst and Einstein from its diffusion, `λ°_i = z_i² F² D_i / (R T)`;
  - lowered by the ions' atmosphere, Debye, Hückel and Onsager's relaxation and electrophoresis, extended by an ion
    size (Robinson and Stokes' form): `λ_i = λ°_i − (B1 z_i² λ°_i + ½ B2 z_i²) √I / (1 + B a √I)`, with I the ionic
    strength in mol/L and `B1 = 8.2046e5 / (εT)^1.5`, `B2 = 82.487 / (η (εT)^0.5)`, `B = 50.29 / (εT)^0.5` (Å⁻¹);
    water's dielectric constant ε by temperature (Malmberg and Maryott, NBS NSRDS 33's Table 2), its viscosity the
    table's own; one ion size, a = 4.2 Å, calibrated to KCl, NaCl and HCl at 0.1 mol/L;
  - the solution's `κ = Σ c_i λ_i`, the hydrogen and hydroxide ions at their exact balance with water (as the pH
    reads them), so pure water reads 0.055 µS/cm and water open to the room's CO2 about 1 µS/cm.
  - Exact for a 1:1 electrolyte in the limiting law, Kohlrausch's additivity at infinite dilution; for ions of higher
    charge the per-ion form is an approximation (declared).
- **An ion's mobility with temperature**: Walden's rule with an exponent of its own, `D ∝ T η^−p` (`λ° η^p` held):
  p = 0.873 for the ions, fitted to the KCl standards at 0 and 18 C (Jones and Bradshaw), 0.581 for H+, fitted to
  HCl's limiting conductance from 5 to 65 C (Hamer and DeWane, NSRDS 33). Diffusion and conductivity are one
  mobility, so the core's `diffusion` takes the exponent too: a behaviour change (the column's diffusion), its
  goldens brought with evidence.
- **The gates** (`native/tests/test_measured.cpp`, each at its source's conditions): the KCl standards (0.01, 0.1 and
  1 demal at 0, 18 and 25 C); KCl and NaCl from 0.0001 to 0.12 mol/L at 25 C (Shedlovsky); HCl from 0.0001 to 1 mol/L
  and its limiting conductance from 5 to 65 C (NSRDS 33); acetic acid from 2.8e-5 to 0.052 mol/L (MacInnes and
  Shedlovsky: the weak acid's speciation and its conductance together).
- **Invariant Q1**: a solution's conductivity is at least nought and at most Kohlrausch's limit, `Σ c_i λ°_i`; nought
  with no ions; a self-test on a corrupted reading.
- **The lab**: a conductivity meter in `instruments.json` (resolution and accuracy, its readings in µS/cm and mS/cm),
  a vessel's quantity `conductivity` read through it like the pH; the notebook logs it; the screen's readout and the
  sensing part's lists offer it.
- **The experiments** (each a bench, a tutorial with its report, played to its end strict, strict at a person's pace
  and free, `test_experiments.cpp`):
  - 14 `standardise`: sodium hydroxide of an unknown strength (drawn by the seed) against KHP weighed on the
    analytical balance, phenolphthalein's first lasting pink; the base's molarity.
  - 15 `vinegar`: vinegar diluted, its curve by the pH meter and the burette logged; the end point from the curve, the
    acid's concentration, its pKa at half the end point.
  - 16 `carbonate_curve`: sodium carbonate against hydrochloric acid, its two end points (phenolphthalein's, then
    methyl red's); the carbonate from the first, the total base from the second.
  - 17 `buffer`: an acetate buffer weighed and made; its pH against Henderson and Hasselbalch; its capacity, the same
    acid in it and in water.
  - 18 `salts_ph`: the pH of salts' solutions (NaCl, NH4Cl, sodium acetate, NaHCO3, Na2CO3); bromothymol blue's pKa by
    its absorbance at 616 nm in phosphate buffers.
  - 19 `antacid`: an antacid tablet (sodium hydrogen carbonate; calcium and magnesium are step E's) in excess standard
    acid, boiled on a burner to drive out its CO2 (in the flask's air too: Henry's equilibrium would give it back to
    the base), cooled, the excess titrated back with potassium hydroxide (so the flask's sodium is the tablet's, the
    report's truth); what a gram neutralises.
  - 20 `conductivity`: strong, weak and no electrolytes by the conductivity meter, in a room of 420 ppm of CO2; a
    conductometric titration of hydrochloric acid with sodium hydroxide, the acid diluted eightfold first so that the
    base bends its arms little, its two lines' crossing the end point.
- **`chem_tool study conductivity`**: the model against its closed forms (Onsager's limiting law; Ostwald's dilution
  law for acetic acid) and the measured tables, printed.
- **A salt dissolving into what takes its ion away** (experiment 19's tablet). Its surface reaction's transport limit
  found its gap by Newton's steps in the logarithm of its first dissolved species (Na+), its fellow (HCO3-) following:
  in an acid that turns each hydrogen carbonate to carbon dioxide at once, the fellow stood at a quantum beside
  0.2 mmol of sodium, the affinity's slope in ln n(Na+) a hundred million times its own, and eight steps moved the gap
  by nothing: the tablet stopped dissolving after its first minute, in an acid far from done with it. The species
  carried is now the dissolved one the solution has least of for its coefficient, the scarce ion whose gradient the
  transport is (the first of equals, as before): in water, or where the two come in their proportion, the same; the
  tablet dissolves in the acid in two minutes, as in water.
- **A renderer fault the exponents exposed** (O13, `lab_tests.kept.1`): with the cost play's flask stirred fast, 44 to
  90 pixels over its shoulder read otherwise with corners shared than with each pixel's own. A corner kept while its
  shape key and its content tag held had a route other than its fresh trace's: its ray's core (`trace_core`: in the
  liquid or over it, by the body's FORM z, the liquid's surface row) had moved. FORM z follows the vessel's condensed
  volume, not what lies in it, and a vessel whose contents are traced hashed neither FORM z nor anything that moves
  with it, so a surface row that moved under a level that stood left a kept route as it was. FORM z is now in the
  content tag, and the rows between its old and new value in the zone that changed: a corner or a beam whose rays
  pass there is traced again. It had been latent since Stage 33 (a scene where the two move apart had not been
  played).

## Phases

| Phase | Topic | Status |
|---|---|---|
| 65.1 | Conductivity in the core: the data, the model, Walden's exponents, Q1, the gates, the study | done |
| 65.2 | The lab: the conductivity meter, the quantity, the notebook, the screen | done |
| 65.3 | Experiments 14 and 15: KHP, vinegar | done |
| 65.4 | Experiments 16 and 17: two end points, a buffer | done |
| 65.5 | Experiments 18 and 19: salts' pH and an indicator's pKa, an antacid | done |
| 65.6 | Experiment 20: conductivity | done |
| 65.7 | The goldens: the evidence and the user's approval | done |
| 65.8 | This document, GAME_SPEC, CHANGELOG, `make gates`, the user's sign-off | |

- **The goldens** (approved by the user, 2026-11-27: "Approved, proceed"): the demo's first eight commands as they
  were; the copper oxide dissolved 8.5 s later (its ions a little slower in the hot acid by Walden's exponents than by
  Stokes and Einstein), the end 691 steps later; the crystals 10.0584 g in 1430 of 1805 um (10.0583 in 1405 of 1815);
  the beaker's water 26.68 g (26.95), its air and its distillate holding the room's carbon dioxide (0.23 mg; 0.02 mg
  dissolved). The pressure session's every command on its step, its warnings at their times; its totals the room's
  carbon dioxide's. Conservation unchanged in both.
- **The room's carbon dioxide on by default** (the user, 2026-11-27: "Let's make it on by default, but if specific
  experiments require it off - they may explicitly turn it off. The default should be on."): 420 ppm unless a bench's
  room names `co2_ppm` (0: none). Every bench's air holds it from this stage on; its goldens move with it.

- **What the room's carbon dioxide brought out**, each fixed:
  - *A settle that crawled.* A gentle pour laid a solution under-saturated in CO2 (its bottle's water had drawn its
    own air down) on a column of copper and nickel sulfate: its gas's balance trades with the bisulfate's, copper's
    hydrolysis and water's ionisation through H+, the traces (OH-, CO3 2-) reach()'s alone, a few quanta a round, and
    sixteen rounds left it 66 J/mol off (P1). Some thirty close it: the rounds are at most 64.
  - *A settle that cycled.* In a carbonate at pH 10 the joint solve's balance is not the reactions' own: it holds
    neutralisation and carbonic acid at their traces, a quantum past which they cannot run down 4 kJ/mol, and put
    back the 1110 quanta the round before had moved bicarbonate's deprotonation, round after round (P1 at 0.1 J/mol).
    A state met again after a joint solve is a cycle: the rounds left are the reactions' own alone, which settle to
    their quanta.
  - *An overflow a part in 1e12 over.* What runs out of a full vessel carries its solutes, whose enthalpy leaves the
    rest 1.4e-8 K warmer and grown past its volume (V2); it runs out to a ten-billionth under, and a solute whose
    apparent volume is below nought (OH-, the carbonates) is rounded down, not up.
  - *A vessel's own gas overwritten by its air.* A bench's carbon dioxide in a vessel (the balance's "gas" beaker)
    was replaced by the air the loader fills its room with; the air is added to it. The supplies' search counts the
    room's trace of carbon dioxide as air (an empty flask is empty).
  - *Real, kept.* Water's carbonic acid takes some micrograms more of a sediment of copper oxide (`pour.4`, to a part
    in 1e5); the balance's closed form has the air's 28.86 g/mol. Salts' pH (experiment 18) is measured with the air
    kept out (`co2_ppm` 0: open to it, 0.1 M sodium chloride reads 5.7).

## Open

- Heat of dilution (C11), in step D's scope in the curriculum, is left to a later stage: the activity model's
  parameters have no temperature derivatives (GAME_SPEC §10).
- The experiments' list wants a scroll past 32.

- **A carbonate's first end point stands at pH 8.67, not 8.34** (experiment 16). The flask's dissolved CO2 is at
  Henry's equilibrium with the flask's air at every step (Stage 64's open item: a real surface exchanges at a finite
  rate), so the carbon dioxide the hydrogen carbonate makes as it disproportionates leaves into the headspace at once
  and the pH there stands higher. Phenolphthalein's colour goes about 0.9 mL past the equivalence (near pH 8.3); the
  curve's step is where it should be (13.85 mL against 13.82), and the second end point is unaffected. It goes when a
  surface exchanges at a rate (a gas's evolution from a solution, GAME_SPEC §6).
- **The experiments' list holds 32** (Stage 65 brought it there): a fourth column past 24, eight rows of 92 px, each
  summary cut to what its row holds. Past 32 it runs off the screen again: it wants a scroll, or pages.
