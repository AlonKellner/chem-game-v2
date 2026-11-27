# The curriculum: a first-year student's lab, and what it needs

The user, 2026-11-24: "The goal of this game is to be an aid to learn chemistry, so we need to list all of the
things which would help a chemistry student study. This includes performing experiments, but not just [...]
some things required precise calculations, I'd want some way to do some of those essential calculations
within the confines of the game [...] calculating quantities and such. Planning intermediate material goals
for an eventual target quota, etc. [...] this simulation is meant to be realistic, not theoretic. So when
theory says one thing but reality is not as simple [...] the simulation might behave slightly differently
than the theory suggests, and that's expected, because reality does the same thing."

The user's choices, the same day:
- The experiments follow the **generic first-year canon**: what most university General Chemistry I and II
  lab courses share. Years two and three are a sketch (the appendix).
- **The student sets a calculation up and the game computes it**, showing each step. A lesson may ask for
  the student's own answer first.
- **Truth per experiment**: a lesson may hide composition and hand out instruments with their resolution
  and error; the free lab and the debug panel keep the exact truth. After the student reports, the lesson
  shows the truth beside it.
- **The measured lab and its notebook come first** (steps A to C below): they need no new chemistry.

This file is what a stage reads to choose its experiments. The property areas of
`docs/EXPERIMENTS_ROADMAP.md` are the physics behind them; each step here names the areas it takes.

## Where the lab stands (GAME_SPEC 0.45.0)

- **Chemistry.** 8 elements (H C N O Si S Cu Na; the build's cap is 16, `MAX_ELEMENTS` in
  `native/core/include/chem/species.hpp`), 18 species and 15 reactions around copper, sulfuric acid and
  sodium hydroxide. Water is the only liquid. Two rate laws, first-order and surface; every reaction
  reversible by its affinity; heats from ΔHf alone; Pitzer's activities; boiling and boiling-point elevation;
  Beer–Lambert colour (copper sulfate the only coloured solute). Not modelled: weak acids, indicators,
  redox in solution, electrodes, ice, gas solubility, another liquid, an open vessel under 1 atm.
- **Lab.** Six kinds of item (vessel, balloon, stopper, burner, viewer, tray) and four glassware sizes.
  Reagents are what a bench fills its vessels with. On the screen a pour is a slider of a share and a solid
  tips all at once; a script can pour an exact volume (`pour {ml}`) and tip an exact mass (`tip {g}`).
  Every reading is the exact truth, shown as each species' grams; mol and molality only in the debug panel.
  There is no balance, burette, notebook, plot, calculator, table or report.
- **In flight**: Stage 46 (the long-wave range of the optics, a thermal camera's basis) and Stage 47 (a
  vessel's column of layers). The curriculum builds on both and plans neither again.

## Principles

1. **A difference from theory is one that reality has.** The student's number differs from the textbook's
   by what a real bench does: heat lost to the room and held by the glass, activity rather than
   concentration, water's vapour in a gas collected over it. Where the simulation differs from reality
   itself (GAME_SPEC §20: water boiling at 100.44 °C; dilute sulfuric acid's pH without bisulfate; copper
   sulfate's pH 5 against a measured 4; constant Cp; no heat of dilution; no thermal expansion), that
   simplification is removed before a lesson leans on it.
2. **Every lesson ends in a report**: the student's value, the textbook's ideal, the simulation's truth,
   and why they differ, each reason named.
3. **Instruments, not truth, in a lesson** (GAME_SPEC §16): each instrument with a resolution and a stated
   error, drawn deterministically (seeded, in the save and the digest).
4. **The closed world stays closed.** A reagent shelf is stock vessels that the bench declares; no matter
   appears from nowhere (`prefer-general-mechanisms`).
5. The roadmap's rules hold: data before code, studies before tutorials, invariants before examples, every
   experiment played to its end, strict, at a person's pace and free.

## Capabilities

### Tools (T)

| Code | Capability | Builds on |
|---|---|---|
| T1 | A reagent shelf of stock vessels; an amount typed in a pour; a scoop for part of a solid; a wash bottle | `pour {ml}`, `tip {g}` exist as commands; the screen's pour and tip menus (`native/game/src/ui.cpp`) |
| T2 | A balance, with tare | a matterless item, as the viewer is; `mass_g`, a vessel's `wall_g` |
| T3 | Measuring cylinder, volumetric flask, pipette, dropper, burette | `data/glassware.json`, `lab/glassware.hpp`; the burette is the inverted pour's orifice (`cap`, Torricelli) with a fine bore, which a controller can drive |
| T4 | Instruments with resolution and error; composition hidden per experiment | `measure` (`native/game/src/conditions.cpp`); the readout in `ui.cpp` |
| T5 | A spectrophotometer: a cuvette, a blank, absorbance at a chosen wavelength | the viewer (Stage 42); Beer–Lambert is in the pass already |
| T6 | Filter funnel and paper, crucible, evaporating dish, test tubes and a rack, a drying oven | a drain carries a share of the solids (`World::set_drain`) |
| T7 | A lidded calorimeter, a water bath, an ice bath, a hot plate | a vessel's `loss`; the condenser's coolant; `set_power` |
| T8 | Syringe or piston, a gas collected over water, a valve on a tube, a vacuum pump | roadmap area 5 |
| T9 | Electrodes, a wire, voltmeter, ammeter, power supply, salt bridge, conductivity probe | — |
| T10 | Separating funnel, fractionating column, melting-point block | Stage 47's layers; roadmap area 9 |

### Study aids (S)

| Code | Capability | Builds on |
|---|---|---|
| S1 | **The notebook**: a reading recorded at a moment, a table, columns derived from others, a plot, a straight-line fit with its slope and intercept; a logger part that samples a quantity on a timer | roadmap area 7; tutorial results (`tutorial.cpp`); signal parts |
| S2 | **The calculator**: molar mass from a formula; amount, mass and volume; preparing a solution and diluting one; stoichiometry on a reaction the student chooses; the limiting reagent; theoretical and percent yield. Later units add their modules: gas law, q = mcΔT, pH and buffers, an ICE table, Nernst | the tables' atoms and weights; a `Reaction`'s terms |
| S3 | **The planner**: a target quota (a species, a mass, a purity), the route the student chooses, and the intermediate amounts it needs, each a goal on the bench that is tracked as it is made | goals (`lab.hpp`), `conditions.hpp` |
| S4 | **The reference**: a periodic table, a card for each species and reaction, tables of solubility, pKa, E° and indicators, generated from the simulation's own data | `chem_tool tables` |
| S5 | **The report**: results entered and checked to a tolerance, the truth shown, theory against measurement | the studies' closed forms |
| S6 | Predictions before the answer; lessons in order, with the player's progress saved | roadmap area 7 |
| S7 | Unknowns: samples drawn from a seed, their composition hidden | T4 |

### Chemistry (C)

| Code | Capability | Roadmap area |
|---|---|---|
| C1 | More elements and salts: the cap raised; Cl, K, Ca, Mg, Ba, Ag, Fe, Zn, Al, I, Mn and others, with their solids and Pitzer's parameters | 1, 2 |
| C2 | Speciation: ions in simultaneous fast equilibria; bisulfate, weak acids and bases, polyprotic acids, buffers, hydrolysis; indicators as two forms with their spectra | 1 |
| C3 | Precipitates and complexes in general: the Ksp of many salts, complex ions and their colours | 2 |
| C4 | Homogeneous rate laws (orders in the concentrations), catalysts (GAME_SPEC §7, planned) | new |
| C5 | Redox in solution: permanganate, iodine and thiosulfate, peroxide, a metal displacing another | new |
| C6 | Electrochemistry: charge along a wire, a cell's potential from its affinity (E = A/nF, so Nernst follows from the activities), electrolysis as electrical work | new |
| C7 | Gases: a volume the player sets, pressure under 1 atm, finite flow, Henry's law, humidity, diffusion | 5, 6 |
| C8 | Ice, melting and freezing, freezing-point depression | 4 |
| C9 | Other liquids (ethanol, cyclohexane, acetone): a mixture's vapour and liquid in equilibrium | 9 |
| C10 | Burning as a reaction: a fuel, magnesium in air | new |
| C11 | Realism: heat of dilution, Cp(T), density(T), carbon dioxide taken up by a base, an open vessel drying out, liquid left wetting the glass, a solid that takes water from the air, a reagent's stated purity | 6, and each step |
| C12 | Conductivity, derived (the ions' mobilities are in `data/solutions.json` already) | new |
| C13 | Two liquids and a solute shared between them | 9 |
| C14 | Emission lines: flame tests, atomic spectra (the optics have lines; no species has data) | new |
| C16 | Secondary nucleation: a seed's or a crop's fragments, chipped off by stirring, grow as crystals of their own (Stage 60 found a 10 mg seed in a solution 5 % over saturation growing only to 15 mg in an hour; a real seed sets the excess out in minutes) | the crystals' kinetics (GAME_SPEC §9) |
| C15 | Crystals that dissolve by mass transfer: through the liquid's film, much faster than they grow (Stage 56 found the pentahydrate dissolving as slowly as it grows: an hour to saturate, its last crystal gone 50 K above its saturation temperature) | the crystals' kinetics (GAME_SPEC §9) |

## The experiments

**Now**: what the experiment needs of the chemistry is simulated today; only the tools and aids it names
are missing.

| # | Experiment | The student finds | Chemistry | Needs |
|---|---|---|---|---|
| | **1. Measurement and technique** | | | |
| 1 | Density; how accurate glassware is | a density; a pipette's true volume | Now | T1 T2 T3 S1 |
| 2 | A standard solution and a dilution series | a molarity | Now | T1 T2 T3 S2 |
| 3 | Separating a mixture | each part's mass, the recovery | Now (CuO from CuSO₄) | T2 T6 |
| 4 | Recrystallisation; a solubility curve | solubility against temperature; the yield | C15 | T2 T6 S1 |
| 5 | Simple distillation | the boiling point, the recovery | Now (the demo) | T4 |
| | **2. Stoichiometry** | | | |
| 6 | The formula of a hydrate | x in CuSO₄·xH₂O | Now | T2 T6 S2 |
| 7 | A carbonate decomposed | the mass lost, the gas's volume | Now (malachite) | T2 S2 |
| 8 | Limiting reagent and percent yield | the yield of Cu(OH)₂, then CuO | Now | T2 T3 T6 S2 |
| 9 | The empirical formula of magnesium oxide | Mg : O | C1 C10 | T2 T6 |
| 10 | Gravimetric sulfate or chloride | % sulfate (as BaSO₄), % chloride (as AgCl) | C1 C3 | T2 T6 |
| 11 | The copper cycle | percent recovery | three of five steps now; C1 C5 | T2 T6 |
| 12 | Kinds of reaction; qualitative analysis; flame tests | which ions | C1 C3 C14 | T6 S7 |
| | **3. Acids and bases** | | | |
| 13 | A strong acid's titration curve with a strong base | the equivalence point, a concentration | Now | T3 T4 S1 |
| 14 | Sodium hydroxide standardised against KHP, with an indicator | the base's molarity | C2 | T2 T3 |
| 15 | Vinegar; a weak acid's curve; an unknown acid's pKa and molar mass | pKa, molar mass | C2 | T3 T4 S7 |
| 16 | A polyprotic acid, or a carbonate: two endpoints | both equivalence points | C2 | T3 T4 |
| 17 | A buffer made, and its capacity | pH against Henderson–Hasselbalch | C2 | T2 T3 T4 |
| 18 | The pH of salts' solutions; an indicator's pKa by absorbance | pKa | C2 | T5 |
| 19 | An antacid by back titration | what it neutralises | C1 C2 | T2 T3 |
| 20 | Electrolytes' conductivity; a conductometric titration | strong, weak or none; the endpoint | C12 (C2) | T9 |
| | **4. Thermochemistry** | | | |
| 21 | A calorimeter's constant; a metal's specific heat | the cup's C; the metal's c | Now for water; a metal lump C1 | T7 T4 |
| 22 | Enthalpy of neutralisation | ΔH per mole | Now | T7 T4 T3 |
| 23 | Enthalpy of solution | ΔH, exo- and endothermic | Now (CuSO₄); other salts C1 | T7 T2 |
| 24 | Hess's law: CuSO₄'s hydration from two dissolutions | ΔH of hydration | Now | T7 T2 S2 |
| 25 | Enthalpy of combustion (a spirit burner) | ΔH per gram of fuel | C9 C10 | T2 T4 |
| 26 | A heating curve; ice's heat of fusion | the plateaus; ΔH of fusion | C8 | T7 S1 |
| | **5. Gases** | | | |
| 27 | Boyle, Charles, Gay-Lussac; absolute zero | the three laws; 0 K by extrapolation | Gay-Lussac now; C7 | T8 S1 |
| 28 | A gas's molar volume and R (hydrogen over water) | V_m, R | a CO₂ version now; C1 C7 | T8 T2 |
| 29 | A volatile liquid's molar mass (Dumas) | molar mass | C9 | T2 |
| 30 | Water's vapour pressure against temperature | ΔH of vaporisation (Clausius–Clapeyron) | Now (a sealed flask) | T4 S1 |
| 31 | Graham's law of diffusion | the ratio of speeds | C7 | later |
| | **6. Solutions** | | | |
| 32 | Boiling-point elevation | the ebullioscopic constant | Now | T4 T2 |
| 33 | Freezing-point depression; a solute's molar mass | the cryoscopic constant | C8 | T7 T4 |
| 34 | Beer–Lambert: a calibration line and an unknown | a concentration | Now (CuSO₄) | T5 T3 S1 S7 |
| 35 | Supersaturation and a seed | — | C16 | T2 |
| | **7. Kinetics** | | | |
| 36 | What sets a rate; the order in the acid; the activation energy | the order (0.5), Ea | Now (`rate`) | S1 |
| 37 | First-order decay and a half-life (crystal violet by absorbance; the solids' decompositions are read by no instrument while they run) | k, the half-life | C4 | T5 S1 |
| 38 | The iodine clock: initial rates, orders, Ea | the rate law | C1 C4 C5 | T3 S1 |
| 39 | Crystal violet with hydroxide, by absorbance | a pseudo-first-order k | C4 | T5 S1 |
| 40 | Peroxide with a catalyst: gas against time | the rate; the catalyst's effect | C4 | T8 S1 |
| 41 | Thiosulfate and acid (the vanishing cross) | rate against concentration | C4 | S1 |
| | **8. Equilibrium** | | | |
| 42 | Le Chatelier (thiocyanate, cobalt chloride, chromate, copper and ammonia) | the shift's direction | C1 C2 C3 | T6 |
| 43 | Kc of FeSCN²⁺ by absorbance | Kc | C3 | T5 S2 |
| 44 | The Ksp of calcium hydroxide; a common ion | Ksp | C1 C3 | T3 T6 |
| 45 | ΔH and ΔS from how a solubility changes with temperature | ΔH, ΔS | C15 (CuSO₄); borax C1 | T4 S1 |
| | **9. Redox and electrochemistry** | | | |
| 46 | The activity series of metals | the order | C1 C5 | T6 |
| 47 | A redox titration (permanganate; iodometric) | a concentration | C1 C5 | T3 |
| 48 | Galvanic cells; Nernst; a concentration cell | E, against Nernst | C6 | T9 |
| 49 | Electrolysis: copper plated, Faraday's constant | F | C6 | T9 T2 |
| | **10. Making something to a quota** | | | |
| 50 | Copper sulfate crystals from malachite, to a quota at a purity | percent yield against the plan | Now (the demo) | S3 S2 T2 |
| 51 | Alum; a copper–ammonia complex; its analysis | the yield, the composition | C1 C3 | S3 T6 |
| 52 | Water's hardness by EDTA | the hardness | C1 C3 | T3 |
| 53 | Aspirin, an ester or a soap | the yield, the melting point | C9 C13 | T10 |
| 54 | Atomic spectra through the viewer | the lines' wavelengths | C14 | the viewer |

## The order of work

Each step is a stage of its own, numbered when it is taken, by `docs/WORKFLOW.md` and the stage workflow of
`CLAUDE.md`, and ships the experiments it completes.

| Step | Stage | Scope | Experiments it completes |
|---|---|---|---|
| A | **The measured lab** | T1 T2 T3 T4; the calorimeter (T7) and the crucible (T6); composition hidden per experiment | 1, 2, 5, 6, 7, 13 (readings by hand), 22, 23, 24, 32 |
| B | **The notebook and the report** (Stages 54 to 56) | S1 S5 S6 S7; T5 | 13 (the curve), 30, 34, 36 (4, 37 and 45 moved: Stage 56) |
| B2 | **Crystals that dissolve** (Stage 57) | C15 | 4, 45 |
| C | **The calculator, the planner, the reference** (Stages 58 to 60, 62) | S2 S3 S4; filtering (T6) | 2 (by calculation: Stage 58), 3 (Stage 59), 8 (Stage 60), 50 (Stage 62) |
| C2 | **Seeds** (Stage 66) | C16 | 35 |
| D | **Acids, bases and buffers** (area 1; Stages 63 to 65) | C2; the cap of C1 and a first pack (Cl, K, acetate, ammonia, carbonate, phosphate); C12; heat of dilution (C11) | 14 to 20 (Stage 65; heat of dilution left to a later stage) |
| E | **Salts, precipitates and complexes** (area 2) | C1 C3 C14; the rest of T6 | 10, 12, 42, 43, 44, 51, 52, 54 |
| F | **Solution kinetics and redox** | C4 C5 | 11, 37, 38 to 41, 46, 47 |
| G | **Gases** (areas 5, 6) | C7; T8 | 27, 28, 31 |
| H | **Electrochemistry** | C6; T9 | 48, 49 |
| I | **Ice, other liquids, burning** (area 4; Stage 71: ice, C8, experiments 26 and 33) | C8 C9 C10; the rest of T7 | 9, 21 (a metal), 25, 26, 29, 33 |
| J | **Two liquids and a first organic** (area 9, on Stage 47) | C13; T10 | 53 |

Steps A to C need no new chemistry and complete about twenty experiments; from D on, each step adds a unit
of chemistry with its experiments.

### Step A in outline

Its spec section and its TDD document come first; this is the outline they start from.
- **Items**: a balance beside the six kinds of `native/game/src/lab.cpp` (its bench loader); glassware
  kinds and their ISO tolerance classes in `data/glassware.json` and `lab/glassware.hpp`; the burette on
  the inverted pour's orifice.
- **Amounts on the screen**: the pour and tip menus of `ui.cpp` take an amount; a scoop takes part of a
  solid.
- **Instruments**: a layer between `measure` and the readout, with resolution and error, seeded, saved and
  digested; a flag in `data/experiments.json` that hides composition.
- **Data**: stock vessels in the benches (`data/benches/`); every new number in `data/sources.json`.
- **Goldens**: a changed readout moves them; the evidence and the user's approval come before a re-record.

### Questions for their stages

Recommendations, each settled in its stage's spec:
- **A**: glassware read through the instrument layer at its tolerance first; graduations and a meniscus in
  the optical pass as far as the stage allows (`defer-hard-renderer-issues`).
- **D**: ions as charged species in their own right, with one simultaneous solver for a cell's fast
  equilibria, in place of today's probe of one reaction at a time and the derived pH (`thermo.cpp`).
- **H**: a wire as an edge that carries charge, so a cell's potential is an affinity and needs no new law.

## Appendix: years two and three (a sketch)

- **Third-year physical chemistry is the nearest**, since the engine is built on activities and
  conservation: activity coefficients from cells' potentials, partial molar volumes, a binary mixture's
  boiling diagram and its azeotrope, conductance and Kohlrausch's law, a bomb calorimeter, viscosity and
  surface tension. Mostly C6, C9, C11 and C12, with instruments.
- **Second-year analytical**: the statistics of repeated measurements (S1), potentiometric and
  ion-selective titrations (C6), standard addition, atomic absorption (C14). Chromatography needs transport
  along a column, which Stage 47's layers begin.
- **Second-year organic is the farthest**: many species and reactions, many solvents (C9, C13), reflux,
  extraction, thin-layer chromatography, spectra to identify a compound (Stage 46's 8–14 µm range covers
  only part of the infrared's fingerprint region).
