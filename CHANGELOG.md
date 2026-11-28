# GAME_SPEC version history

## 0.91.1 (2026-11-28, Stage 90, merged after Stage 91)

- §12.2 (Stage 90, `docs/TDD_90_EXPOSURE.md`; the user's proposal and choices: the brightest block, darken and
  brighten, pupil-like): the display's exposure, metered on the GPU over 16 x 9 blocks and adapted a frame at a time
  (closing in 0.1 s, opening in 0.7 s), exactly 1 in the room at its full light; every pixel shown again as it moves;
  O4 at the exposure read back.
- §12.2: the room's lights (the top bar's "lights", 100 to 1 %): what emits light the brighter against them, the eye
  opening up to 8 times. A lamp's lines in eleven stretches, every line in one.
- §12.3: a bright flame fading no longer shows black pixels (Stage 82's): a pixel's history measures a change at the
  display's exposure and, beyond the display, by the light's amount.

## 0.91.0 (2026-11-28, Stage 91; signed off by the user: "Approved, merge and proceed")

- §9 (Stage 91, `docs/TDD_91_REDOX.md`; `docs/CURRICULUM.md`, step F's second part: experiments 38 and 47): **redox in
  solution**, electron transfers as reactions balanced in atoms and charge; fast: triiodide (Palmer et al.'s K 698),
  iodine with thiosulfate, permanganate with iron(II) in acid (log K 62.8); slow, the solution law (§7): **persulfate
  oxidising iodide** (Jette and King's k at their ionic strength, Ahmed et al.'s 51.39 kJ/mol), the iodine clock.
- Manganese, MnO₄⁻, Mn²⁺, I₂(aq), I₃⁻, S₂O₃²⁻, S₄O₆²⁻, S₂O₈²⁻ (NBS's rows, OBIGT's heat capacities and volumes) and the
  salts Na₂S₂O₃, K₂S₂O₈, KMnO₄, MnSO₄; §12: permanganate's purple (McBeath et al. 2020's measured spectrum), triiodide's
  and iodine's yellow-brown (Pursell and Pursell 2016's). Starch is not modelled (no measured constant).
- §13.7: the experiments `iodine_clock` (four clock times by two spectrophotometers: the orders in iodide and
  persulfate, k and Ea) and `redox_titration` (iron(II) by permanganate, its own pink the end point; iodine by
  thiosulfate, its yellow gone).

## 0.88.1 (2026-11-28, Stage 85, merged after Stages 87 and 88; signed off by the user: "Approved, merge and proceed")

- §13.7 (Stage 85, `docs/TDD_85_ANALYSIS.md`; `docs/CURRICULUM.md`, step E's experiments): `gravimetric` (10), `qualitative`
  (12), `lechatelier` (42), `kc` (43, Benesi and Hildebrand's), `ksp` (44), `hardness` (52) and `spectra` (54);
  experiment 51 moved after steps F and I (the user's scope).
- §13.2: a report's answer may be choices (the ions present, a shift's direction), checked against the lab; an unknown
  drawn by the seed from a list (`one_of`); the conditions `probe_on` and a burner's `flame_umol`.
- §12.6: discharge lamps (H, He, Hg, Ne: NIST's lines and relative intensities), a tube of glowing gas, its power; the
  spectral viewer lists the lines it sees.
- §13: a sintered glass filter crucible (its frit under a pump's suction, a wet cake); a drying oven; bulb pipettes of
  5 and 10 mL (ISO 648); a drained pipette draws again; a flame test holds every metal, those with lines coloured.
- §9: iron(III)'s Pitzer pairs and FeSCN2+'s with nitrate, fitted to Below, Connick and Coppel's 146 at 0.40 and 123 at
  1.00 (log K 3.509 to 2.891). §8: no fast equilibrium carries a solution past the activity model's range (a filter
  funnel's first drop of limewater, dried in an empty beaker's air, had lowered the world's entropy); the fast
  equilibria's rounds 256 to 1024 (a hardness titration's flask).

## 0.88.0 (2026-11-28, Stage 88; signed off by the user: "Approved, merge and proceed")

- §6 (Stage 88, `docs/TDD_88_PRESSURE.md`; the first of the two stages the user asked for after Stage 79, "the proper
  mechanisms ... in a correct and consistent manner"): **liquids that give**: a sealed cell's liquids are
  compressible (water's by Kell 1975, 4.59e-10 /Pa at 20 °C), their Gibbs energy `G0(T) + V_s p + V_l (p − κ p²/2)`,
  the cell's volume, energy, entropy and every chemical potential from it. Every sealed state has one pressure: gas
  over liquid, liquid alone (brim-full water warmed rises 4.5 bar a kelvin at 20 °C), or nought with a void that
  boiling fills (a cavity at the vapour pressure). Open cells unchanged to the bit. V2 at a sealed cell's own
  pressure; V3's roomless cell one filled with what does not give.
- §6: **a vessel's one pressure**: a sealed vessel's layers stand at its pressure, each taking the volume it needs
  (`pressed`, `over`; `Simulation::size_vessels`), the work of each one's change of volume booked between it and
  the top as work (`World::work`, W2); a cell's resizing an entry of the step's ledger, replayed by T1. Water in a
  layer boils where its vessel's pressure says (it boiled at 100 °C whatever the pressure): a sealed flask in a bath
  at 150 °C comes to 6 bar with its layers' water liquid at 149 °C. A layer feels its vessel as it grows (its
  **cushion**: its share of the vessel's free volume, a pressure law stiffer than any gas's own compression), so its
  boiling takes no more room than its vessel has; what moves between a vessel's cells takes its volume with it; P2
  (a sealed vessel at one pressure). A capped syringe of layered solutions, which failed T2 at its first step on
  main, holds every invariant under hands jerked at random.
- The column's diffusion trims what it moves to what is neutral before reckoning its energy (it carried the energy
  of ions it did not move); a column that spills closes up (L1).
- The `pressure` session re-recorded with the user's approval (2026-11-28): every printed result main's, line for
  line; its six digests while the flask is open unchanged, the twelve after it is stoppered moved. Recorded again on
  Stage 82's tables after main was merged in: the same six of main's unchanged, the same twelve moved, the printed
  results the same but for lithium's and strontium's totals (nought).

## 0.87.0 (2026-11-28, Stage 87; signed off by the user)

- §7 (Stage 87, `docs/TDD_87_SOLUTION_KINETICS.md`; `docs/CURRICULUM.md`, step F's first part: experiments 37, 39 and
  40): **the solution law**, a reaction among dissolved species at `k(T) · Π m_j^{n_j}` in their molalities, times
  Brønsted and Bjerrum's activity factor (each species' activity coefficient over the activated complex's, the primary
  salt effect: the limiting law in dilute solution) and the affinity factor; **catalysts**, any ordered species in
  none of its terms (the rate changes, the equilibrium is the reaction's own); a step exact in its first reactant.
- §9, §12: **crystal violet** fading in base (Begum et al.'s 0.234 /M/s at 25 C, Thompson and Ting's temperatures,
  Ritchie's pK_R+ 9.39; its violet PhotochemCAD's, scaled to Begum's 93,587 at 592 nm); **hydrogen peroxide**
  decomposed by **iodide**, a catalyst (Liebhafsky's 1.15e-2 /M/s, 54 kJ/mol); iodine, I⁻, KI, H₂O₂(aq).
- §13.7: the experiments `crystal_violet` (a first-order decay by two spectrophotometers, the order in hydroxide at one
  ionic strength, k₂) and `peroxide` (oxygen in gas syringes: the order in iodide, none without it).

## 0.82.0 (2026-11-28, Stage 82; signed off by the user: "Approved, merge and proceed")

- §9 (Stage 82, `docs/TDD_82_FLAMES.md`; `docs/CURRICULUM.md`, step E's third part): lithium's and strontium's ions
  (NBS, OBIGT, the CRC), their chlorides' and nitrates' Pitzer pairs (Pitzer and Mayorga).
- §12.5: **a flame's light**: NIST's atomic lines by Boltzmann's populations at the flame's temperature and the
  monohydroxides' bands (Meyerriecks and Kosanke) at a calibrated share; each line's light on the two nanometres beside
  it, its light and centre kept; added to the flame cone's glow, read by the pass, the truth and the spectral viewer.
- §13: `flame_test`: a loop's drop of a solution in a lit burner's flame, fading in 3 s; on the screen, a vessel of a
  solution carried onto a lit burner. The free lab's 0.1 M sodium, potassium, lithium and strontium chlorides and
  copper(II) sulfate.
- §12: light brighter than the display is scaled down by its brightest channel, keeping its hue (the user's choice:
  until then it was drawn white); a flame's narrow lines cut the pass's bands, so a pixel reads each every frame.
- §20: the drop is the flame's display state, not matter.

## 0.81.1 (2026-11-28, Stage 79, merged after Stages 78, 80, 81, 84 and 86; signed off by the user: "Approved, merge and proceed")

- §6 (Stage 79, `docs/TDD_79_SYRINGE.md`; `docs/CURRICULUM.md`, step G's first part: experiments 27 and 28): **pistons**:
  a rigid cell's (or a column's) volume moved each step by the forces on its plunger, overdamped (its gas's pressure
  after the move against the room's, its friction and drag, a hand of at most 79.8 N toward a volume, a thumb's, Astin
  1999); **tightly joined vessels**, each its own, the exchange between them within the step (sub-steps: each piston
  alone by its sealed model, each connection down the true pressure, at a vessel's foot or top), a tube by
  Hagen–Poiseuille (an ordinary 4 mm tube, air's viscosity measured), a hydraulic link (two vessels joined by liquid
  at their feet) solved together, so a push on one is the other's pull; the hand's work along an edge from the mains
  the only work (T2 by the pressure after the move); W1; a step's start read at its volume then.
- §13: **the gas syringe**, its plunger's handle a physical control dragged on the bench (asked for by the user), its
  lock, its nozzle capped, on a vessel's mouth or tubed to another's; a bench's `setup`.
- §13.7: the experiments `syringes` (the user's four setups), `boyle`, `charles` (the user's cool and hot baths),
  `molar_volume`.
## 0.81.0 (2026-11-27, Stage 81; signed off by the user: "Approved, merge and proceed")

- §9 (Stage 81, `docs/TDD_81_COMPLEXES.md`; `docs/CURRICULUM.md`, step E's second part): **complexes**, each a fast
  equilibrium at its measured constant: copper's ammines (NBS's free energies), silver's (MINTEQ v4, NBS), zinc's and
  aluminium's hydroxo complexes (MINTEQ v4), iron(III)'s thiocyanates (calibrated to Below, Connick and Coppel's and
  Lister and Rivington's quotients), cobalt's tetrachloro (calibrated to Bjerrum, Halonin and Skibsted's α₄), chromate,
  hydrogen chromate and dichromate (NBS); **EDTA** and its complexes (MINTEQ v4) and **Eriochrome Black T** (Lindstrom
  1959, calibrated at 0.1 ionic strength). Each series step by step and each solid straight into its complex (a free
  ion under one quantum); the fast equilibria's rounds 64 to 256; a table's dissolved particles 64 to 128; chromium.
- §12: their colours, measured where open (Haupt's chromate, Bjerrum's tetrammine and CoCl₄²⁻, Ozutsumi's and Betts
  and Dainton's FeSCN²⁺, Lindstrom's indicator) and calibrated where not (dichromate's visible band, the user's
  choice; hydrogen chromate, the lower ammines, Fe(SCN)₂⁺, the indicator's red form).
- §13: the free lab's reagents (potassium thiocyanate, chromate and dichromate; 6 M ammonia, concentrated hydrochloric
  acid, 2 M sodium hydroxide; 0.0100 M EDTA, a pH 10 buffer and Eriochrome Black T).
- Flame emission is a stage of its own (the user's split).

## Stage 86 (2026-11-27; GAME_SPEC stays 0.81.0: no behaviour of the game changes; signed off by the user)

- The supplies' pictures (`docs/TDD_86_RENDERER_KEY.md`): each picture's key holds the renderer's, a hash made at
  build time of the 49 files a picture is rendered by (`RENDERER_KEY`: the per-pixel source, the optics' sources,
  colour, contents, glassware, the core's optics and math, `data/optics/` and the tables made of it), and each file says
  which renderer made it: another renderer's is taken as none, so a change to the renderer captures every picture again
  and leaves none of the old behind (ctest `renderer_key_moves`).

## Stage 84 (2026-11-27; GAME_SPEC stays 0.80.0: no behaviour of the game changes; signed off by the user)

- The gates (`docs/TDD_84_GODOT_GATE.md`): the Godot step builds and runs only what needs the engine (the extension,
  the import, the GUT suite: `make native PICTURES=0`), and the suite captures no supply's picture
  (`ChemLab.capture_pictures` off): a new worktree's step about 20 s, from about 200 s. `make native` captures the
  pictures bench by bench side by side, merged by key (`tools/merge_pictures.py`).

## 0.80.0 (2026-11-27, Stage 80, merged after 0.78.0; signed off by the user: "Approved, merge and proceed")

- §6, the wall in rings; §8, the fast equilibria (Stage 80, `docs/TDD_80_WALLS_AND_SEARCH.md`): a vessel's walls read
  their temperatures, levels, water's activity and pressure on a grid (0.01 K, a micrometre, 1e-6, 0.01 Pa), each
  ring's outside and contact worked out again only once that moved, a function of the state alone; an equilibrium's
  search seeded by the line through no extent and one quantum. The demo 1.227 to 1.104 million instructions a step
  (−10 %); the oxide gone 0.1 s later, the flask cool 0.2 s sooner, 0.12 mg more crystals; the sessions re-recorded
  with the user's approval.

## 0.78.0 (2026-11-27, Stage 78; signed off by the user: "Approved, merge and proceed")

- §12.3 (Stage 78, `docs/TDD_78_REFLECTION.md`; the user's request for reflection simulated as refraction is):
  **each material's own index, complex where it is a metal**: datagen's measured n + ik tables (`nk_table`, read in
  the dielectric function against the photon's energy), `chem::optics::extinction_index`; **liquid mercury** as
  Inagaki, Arakawa and Williams 1981 measured it (checked against Smith and Stromberg 1966 at 546.1 nm; normal
  reflectance 0.773 to 0.783); a vessel's liquid takes its liquids' index by volume (a row of k a set:
  `ROWS_PER_SET` 10); **Fresnel with a complex index** (`chem::optics::fresnel_reflectance`; the shader's own
  `metal_reflectance`): a metal turns back R of what comes to it and takes in the rest, in both tracers, without a
  branch, so its mirror image is in the kept beam.
- §12.3: **the room's faces ruled as the wall is** (the user's choice: a mirror shows them), their edge cells with
  them: a ray that comes to a face is put at its own place on it (the header's ninth vector: the ruling), read exactly
  by the pass and by the truth; their mean the faces' mean of before.
- §18: O6 restated for a complex index; **O28** (a mirror shows the room's ruled faces where the truth's rays find
  them); O27 reserved for clear surfaces' branches, split off to their own stage (the user's choice), built so far on
  branch `reflection-branches-wip`.
- §12.3 (the user's report: things held and beside the mercury unseen in it): **a ray turned back toward the eye or
  sideways meets the room's things in three dimensions**, a vessel by its surfaces, a flat thing as a box as deep as
  it is wide (the user's choice), in both tracers; a mirror's pixels follow every other body.
- §13: the experiment mercury (300 g of liquid mercury beside water and copper sulfate); a play of it, a beaker held
  beside it. §13.7 (the user's report: the experiments hard to navigate): **the experiments' list scrolled and
  searched** (one column, the wheel and a bar, a search field matching titles, summaries and ids; plays reach a row
  out of sight by `lab::scroll_to`).

## Stage 75 (2026-11-27; GAME_SPEC stays 0.77.0 (merged after Stages 71, 76 and 77): no behaviour of the game changes; signed off by the user: "Approved, merge and proceed")

- The step (`docs/TDD_75_STEP_SCALES.md`): 1.136 to 0.894 million instructions a step on the demo (−21 %), every
  digest as it was. A cell's revision and matter revision (`World::revision`, `matter_revision`): the thermal state,
  a cell's rest and a vessel's walls kept by them, a cell's sums over its matter and its mixtures kept while only
  energy moves (conduction), each hit held to the full state where assertions abort. The joint solve's mixtures once
  an iteration; a probe's setup and the activity model's range from the kept mixtures; the column's diffusion over
  what its layers hold.
- The gates: `make gates` prints the step's cost in instructions against main's where the branch left it, warning
  past 10 % (`gate_cache.py stepcost`, `step_costs.json` in the shared cache).

## 0.77.0 (2026-11-27, Stage 77; signed off by the user: "Approved, merge and proceed")

- §12.2 (Stage 77, `docs/TDD_77_GHOST.md`): **the ghost** the user found: a pile lowered slowly in a stirred bath (a
  fraction of a pixel a frame at the room's whole view) left its old shape behind, brighter, settled and never
  corrected. A pixel read by its own rays whose rays moved (its beam traced again) kept, by the drift rule, the
  corrections its old rays had found; it now starts again. Reproduced by `data/plays/ghost.json` on the GPU (a play's
  picture held to the truth by the host, `LAB_SHOT_PARITY`) and on the CPU (`test_ghost.cpp`). Nested rays moved to a
  stage of their own after Stage 78, by the user's choice.

## 0.76.0 (2026-11-27, Stage 76; signed off by the user: "Approved, merge and proceed")

- §9, §10 (Stage 76, `docs/TDD_76_METALS.md`; `docs/CURRICULUM.md`, step E's first part: C1's metals, C3's
  precipitates): **calcium, magnesium, barium, silver, iron, zinc, aluminium and cobalt**, nitrate, 23 salts, the
  hydrolysis of iron(III) and aluminium (NIST 46.3), and **13 precipitates** at MINTEQ v4's solubility products, each
  solid's S° set to it on NBS's enthalpy; Pitzer and Mayorga's and Harvie, Møller and Weare's pairs; gated on MINTEQ,
  wateq4f's analytic solubilities from 10 to 80 °C and NIST SRM 2193b's saturated calcium hydroxide from 5 to 50 °C.
  `chem_tool study precipitates`.
- §12: cobalt(II)'s and FeOH²⁺'s absorption from measured spectra; the precipitates' colours.
- §13: the free lab's new reagents (ten metal salts' solutions, limewater, chalk); the supplies' search counts a salt
  only as a share of what a vessel holds.
- §20: the precipitates' declared simplifications.

## 0.73.1 (2026-11-27, Stage 65, merged after Stages 72, 73 and 74; signed off by the user: "Approved, merge and proceed")

- §10 (Stage 65, `docs/TDD_65_TITRATIONS.md`; `docs/CURRICULUM.md`, step D's third part: experiments 14 to 20, C12):
  **a solution's conductivity**, each ion's Nernst–Einstein conductivity from its diffusion lowered by Debye, Hückel
  and Onsager's atmosphere over one ion size (Robinson and Stokes; 4.2 Å, calibrated), water's dielectric constant by
  temperature (Malmberg and Maryott); within 0.5 % of Shedlovsky's KCl and NaCl and Hamer and DeWane's HCl to
  0.1 mol/L, of Jones and Bradshaw's KCl standards at 0, 18 and 25 °C, and within 2 % of MacInnes and Shedlovsky's
  acetic acid. **Each ion's mobility with temperature** by Walden's rule with its own exponent (0.87; H⁺ 0.61), the
  column's diffusion with it. Invariant Q1. `chem_tool study conductivity`.
- §13: a conductivity meter (0.5 % of its reading, auto-ranging), a vessel's quantity `us_cm`; a room's
  carbon dioxide, 420 ppm by default (a bench's `co2_ppm`, 0 for none); a report's expression takes log10, ln and sqrt, and a quantity with no value yet is
  not one the bench lacks; the experiments' list in four columns.
- §13.7: the experiments `standardise`, `vinegar`, `carbonate_curve`, `buffer`, `salts_ph`, `antacid` and
  `conductivity` (curriculum 14 to 20).
- §7, §9: a surface reaction's transport carries the scarcer of its dissolved species (a salt dissolving into an acid
  that takes its ion away stopped after its first minute).
- §12.3: a vessel's liquid surface row (FORM z) in its content tag and its changed zone (a kept corner's route went
  stale where the surface moved under a level that stood: O13).
- §20: conductivity's and a room's air's declared simplifications.
## Stage 74 (2026-11-27; GAME_SPEC stays 0.73.0: no behaviour of the game changes; signed off by the user: "Approved, merge and proceed")

- The gates (`docs/TDD_74_COST_BY_WORK.md`): signals.12 holds three monitors' cost by the instructions the process
  retired, under a tenth of a frame's (5.9 %), not by 3 ms of the thread's CPU time, which a crowded machine's slower
  cores pushed past its bound in three gates of five. WORKFLOW: a cost is held by the work, not the clock.

## 0.73.0 (2026-11-27, Stage 73; signed off by the user: "Approved, merge and proceed")

- §6, the wall in rings (Stage 73, `docs/TDD_73_SMOOTH_FLOOR.md`): an unstable horizontal plate's convection (a
  floor heated from below, a base colder than the air under it) takes Lienhard's thick boundary layers' correction
  (8.37b) at every Nusselt number, not only under 10, where it left a step down of 7 % (`walls::unstable_plate`,
  one form for the floor and the disc). The floor's surface under its glass is one everywhere. The demo: the oxide
  gone and the flask cool 0.1 s sooner, 0.2 mg more distillate; the sessions re-recorded with the user's approval.

## 0.72.0 (2026-11-27, Stage 72; signed off by the user: "Approved, merge and proceed")

- §6, the wall in rings (Stage 72, `docs/TDD_72_STEP_CHEAPER.md`): a floor's inside surface under half its glass found
  by ITP to the bisection's bracket of 1e-9 K, not by the bisection (9 evaluations a step where it took 26); where
  the floor's convection steps down two such surfaces may stand. Every printed result of both sessions as it was;
  their digests re-recorded with the user's approval. A cell's temperature read from the simulation's kept thermal
  state by the columns and the lab (the same value).

## 0.71.0 (2026-11-27, Stage 71, merged after 0.72.0 and 0.73.1; signed off by the user: "Approved, merge and proceed")

- §8, §9 (Stage 71, `docs/TDD_71_ICE.md`; the curriculum's step I, C8): **ice**, H2O(s), from IAPWS R10-06 (melting at
  273.152519 K, 6006.8 J/mol). `water_freezing` a surface law limited by the heat its change carries (a new general
  step: a surface reaction that carries no dissolved species, Nu k / d (T − T_eq) / |ΔH|, Sano's form with Pr);
  **supercooling** by a nucleation barrier, e^(−B / ln² S) (`nucleation_barrier`), fitted to Wilson and Haymet's
  tubes; a solid with a grain grows as a mush; freezing-point depression through the water's activity, held to the
  International Critical Tables. §6: a lighter solid floats up a column; a column spills what no longer fits; **a
  vessel in a vessel** (`stand`): the other's volume integral less what stands in it (`World::reshape_column`), ring
  by layer contacts, as asked for by the user; a test tube (ISO 4142); a bench vessel at its own temperature. §12:
  ice's media (its long wave from Warren and Brandt's n + ik). §13.7: the experiments `freezing_point` and
  `heating_curve`. The generator's refusals each checked by their message (six had passed for the wrong reason).
  The goldens re-recorded with the user's approval (their measurements identical).

## 0.69.1 (2026-11-27, Stage 64, merged after Stages 67, 68, 69, 66 and 70; signed off by the user: "Approved, merge and proceed")

- §9, §10 (Stage 64, `docs/TDD_64_WEAK_ACIDS.md`; `docs/CURRICULUM.md`, step D's second part: C1's cap and first
  pack, C2): **weak acids, bases, indicators and the salts experiments 14 to 20 weigh**. New elements Cl, K, P, Br (32
  at most; 64 particles). Chloride, potassium, ammonium and ammonia, acetate and acetic acid, carbonate, hydrogen
  carbonate and dissolved CO₂, the phosphates, phthalate; NBS's ΔfH and S°, OBIGT's heat capacities and volumes;
  each weak acid a fast equilibrium at its measured constant (PHREEQC's, MINTEQ's, Hamer and Acree's), gated with
  IUPAC's pH standards. CO₂ and NH₃ between gas and solution by Henry's law, with bubbles past saturation. The
  indicators phenolphthalein, bromothymol blue and methyl red at their measured constants and spectra. NaCl, KCl,
  NH₄Cl, NaHCO₃, Na₂CO₃ and its decahydrate, sodium acetate and KHP at their measured solubilities (each crystal's S°
  the one NBS's own ΔfG implies; phthalate's anchor calibrated to KHP's). Pitzer and Mayorga's and Harvie, Møller and
  Weare's pairs. H⁺'s heat capacity on the ions' footing (water's ionisation −223.8 J/K mol).
- §4.6: a dissolved species' masters (`basis`), by which a retired unit is read back.
- §8: the joint solve takes a dissolved trace and a bubble's gases; a cell found at rest is left at rest while it is
  unchanged.
- §13: a bench opens at its fast equilibria at the temperatures it states (`build_settled`); the free lab's reagents
  (hydrochloric acid, sodium hydroxide, vinegar, ammonia, the three indicators, six salts).
- §20: the declared simplifications of the weak acids, the indicators and the salts.
## Stage 70 (2026-11-27; GAME_SPEC stays 0.69.0: no behaviour of the game changes; signed off by the user: "Approved, merge and proceed")

- The gates (`docs/TDD_70_GATES_AGAIN.md`): `make gates` 262 s uncached on the physics of Stages 63 and 67 (335 to 596
  s before it): the column's tests check each step once (not twice), D2's windows as the demo's fixture lists them (a
  long wait in two halves), ctest's measured costs shared by every worktree (a new worktree's first gate as short as
  the others'), a probe's dead evaluation gone. The goldens unchanged.

## 0.69.0 (2026-11-26, Stage 69; signed off by the user: "Approved, merge and proceed")

- §13, Save and load; §18, D2 (Stage 69, `docs/TDD_69_SAVES.md`): a lab saved in a step a command moved matter in (a
  tip, a pour) continues exactly as the saved one would have: its world refuses until the step closes what a step in
  progress refuses (`World::resume_step`), as the saved one did; the demo's save after its command 1 did not. A
  loaded lab reads as the saved one did: a save holds the last step's flows (not state), and a vessel finds its
  contacts as it loads, so its boiling's bubbles show at once. D2 holds at every command of the demo. The goldens
  unchanged.

## 0.66.0 (2026-11-26, Stage 66, merged after 0.67.0, Stage 68 and 0.69.0; signed off by the user: "Approved, merge and proceed")

- §9 (Stage 66, `docs/TDD_66_SEEDS.md`; the curriculum's C2, C16): **secondary nucleation**: where a surface law grows
  its solid and the cell is stirred, the crystals breed, `secondary · ε · M · (S − 1)^secondary_order` fragments a
  second (ε the stirring's W per kg of the liquid, M the crystals' kg), each one quantum; a still solution's seed only
  grows. The copper sulfates 2e10 and 1.7, calibrated (no measured rate is open): linear in ε and M, the order Lyall's,
  a stirred seed setting out a solution's excess 5 % over saturation half in a quarter of an hour. `chem_tool study
  seed`; the generator refuses a rate without its order; the debug panel says what the stirring breeds. §13.7: the
  experiment `seed` (curriculum 35). §20: equal spheres share a seed's mass with its fragments; the secondary rate
  calibrated. No golden moved.

## Stage 68 (2026-11-26; GAME_SPEC stays 0.67.0: no behaviour of the game changes; signed off by the user: "Approved, merge and proceed to fix the open bugs")

- The gates (`docs/TDD_68_GATES_FAST.md`, `docs/WORKFLOW.md`): `make gates` 2,579 s to 1,307 s, then (68.9, the user's
  compromises approved) 253 s uncached: the glassware's shapes made at build time, plays waited on a second at a
  time, D2 checked in windows from the demo's saves, the sanitizers on the short entries and the truth's
  comparisons every third sample in the gates, `make gates-full` for all of it. Before that: every golden
  and digest as it was. The invariants checked at a third of the cost and a step's copies of the state allocated
  once; a vessel's shape made once a process and its level found with fewer points of its pieces; a screen's view
  taken once a change; the demo played once for the O2 scan's twelve parts (a ctest fixture: its scenes); the
  sanitizer build's labs unchecked (`LAB_CHECK=0`, the release build checking every step); the Godot suite 1,396 s to
  11 s; the result cache at two levels (a merge gate takes what passed at one) and one for every worktree; one ccache
  for every worktree. The pressure session's golden record held by the gates (`pressure_session`). Found, open: a
  save just after the demo's command 1 does not continue as the record does (D2); a loaded lab's first view has no
  boiling's bubbles.

## 0.67.0 (2026-11-26, Stage 67)

- §6 (Stage 67, `docs/TDD_67_EXPANSION.md`; the roadmap's area 8c): **liquids that expand**: a liquid's and a
  dissolved species' molar volume at T is the table's times water's ρ(25 °C)/ρ(T), Kell's density (1975,
  `data/solutions.json`'s `density`, held to Lienhard's rows); solids keep the table's. Every volume of a liquid is
  read at its cell's temperature: a vessel's fill, level and picture, its overflow, what fits, drains, pours, marks,
  a column's layers, concentrations. **A sealed cell** from one Gibbs energy of its solids and liquids,
  G0(T) + V_c(T)(P − P_atm): its energy gains −T V_c'(T)(P − P_atm), its entropy −V_c'(T)(P − P_atm), each condensed
  species' potential v_i(T)(P − P_atm); its temperature is its energy's root. §18: V2, V3 and L1 at each cell's own
  temperature. §20: the condensed phases' Gibbs energy. The goldens re-recorded with the user's approval.

## 0.63.0 (2026-11-26, Stage 63; signed off by the user: "Approved, merge and proceed")

- §9, §10 (Stage 63, `docs/TDD_63_IONS.md`; `docs/CURRICULUM.md`, step D's first part, C2): **dissolved matter as its
  ions**, each a species: H⁺, OH⁻, Cu²⁺, SO₄²⁻, HSO₄⁻, Na⁺, Ni²⁺, CuOH⁺. The formula units retired (kept, taken as their
  ions by a bench, a save and the builder; read by the lab as what their ions make, by each element and the balance
  of protons); the reactions in ions, each with its written equation in formula units for the calculator and the
  planner. The bisulfate (PHREEQC's pitzer.dat) and copper's hydrolysis (minteq.v4.dat) as fast equilibria; sulfuric
  acid by Pitzer, Roy and Silvester; copper's interaction with the bisulfate calibrated to the acid's common-ion
  effect. Water holds its own ions; sodium sulfate reads a little over pH 7, copper sulfate 4.8.
- §8: the fast equilibria solved together after each round (a quasi-Newton step on their extents); past an ionic
  strength of 20 mol/kg the equilibria among dissolved species stand.
- §6, §7: a transfer moves ions only as neutral matter; a column's diffusion at no current (Nernst and Planck), a
  grain's transport an electrolyte's (Nernst and Hartley); the oxide's and malachite's surface rates refitted in the
  free hydrogen ions at their sources' conditions; a column's contrast summed over its species, its split the
  steepest step that can still be halved.
- §18: N1, electroneutrality. §12: a formula unit absorbs as its ions. §13: ions written as chemists write them; a
  dissolved trace that reads nothing is not listed. The demo waits for its oxide to be gone.
- §20: the bisulfate's and the hydrolysis's rows answered; the dimer left out (pH 4.8 against 4), Kw's heat capacity
  the formula units', the activity model's range. The goldens re-recorded (the user's approval).

## 0.62.0 (2026-11-26, Stage 62; signed off by the user: "Approved, merge and proceed")

- §13.7 (Stage 62, `docs/TDD_62_QUOTA.md`; `docs/CURRICULUM.md`, experiment 50): the experiment `quota` (copper
  sulfate crystals from malachite to a quota of 3.00 g, planned with the planner, made, and handed in against the
  plan's goal). (0.61.0 is another agent's Stage 61.) The goldens unchanged.

## 0.61.0 (2026-11-26, Stage 61, merged after 0.62.0; signed off by the user: "I agree, it looks good! Approved, merge and proceed")

- §4.3 (Stage 61, `docs/TDD_61_SUPPLIES.md`; the roadmap's area 8d): **the world open at its boundary**, between
  steps only: a cell added and supplied from outside, a cell retired (all it holds leaves), a cell frozen (it takes
  part in no process, its state standing). §18: **C9**, the world's totals its start's plus what was supplied less
  what was retired.
- §13: **Supplies** (the cabinet): what a bench gives without a square is a template, no part of the room's world,
  that never runs out; `take` (or any command naming one none of whose copies is out) grafts a copy into the world;
  a refused command leaves the lab as it was. **Storage** (the outbox): as many slots as it needs, goals on any,
  still simulated, used straight from it; what went to the cabinet goes there. **The trash** freezes, restores as
  it went in, and emptied sends its items out of the world.
- §13.1: Supplies and Storage scrolled, each with a **search** (`lab/search.hpp`: what an item is, holds, is made
  of, is for, and its state); each supply's box a **picture captured ahead** (`chem_tool capture`, `make pictures`;
  the Godot host gives them back and keeps those it captures apart).
- The pressure session's golden re-recorded with the user's approval (its physics unchanged).

## 0.60.0 (2026-11-26, Stage 60; signed off by the user: "Approved, merge and proceed")

- §13.1 (Stage 60, `docs/TDD_60_PLANNER.md`; `docs/CURRICULUM.md`, step C's last part, S3): **the planner**, the
  notebook's fourth tab: a target and its route back, each step's reaction and expected yield, what each step needs,
  carried or bought; set as a goal in the outbox.
- §13.2: `calculated` "plan".
- §13.7: the experiment `yield` (limiting reagent and percent yield of copper hydroxide).
- `docs/CURRICULUM.md`: experiment 35 (seeding) moved to a chemistry step with secondary nucleation (C16); 50 its own
  stage. The goldens unchanged.

## 0.59.0 (2026-11-25, Stage 59; signed off by the user: "Approved, merge and proceed")

- §13 (Stage 59, `docs/TDD_59_FILTERING.md`; `docs/CURRICULUM.md`, step C's second half, T6): **a filter funnel** with a
  Whatman grade 1 paper: it stands on a mouth and drains through its paper by Darcy's law from the paper's Herzberg
  time, keeping every grain; a drained residue is scraped out by the pour menu.
- §13.7: the experiment `separate` (copper oxide off copper sulfate, filtered and weighed).
- §20: the paper's simplifications. The goldens unchanged.

## 0.58.0 (2026-11-25, Stage 58; signed off by the user: "Approved, merge and proceed")

- §13.1 (Stage 58, `docs/TDD_58_CALCULATOR.md`; `docs/CURRICULUM.md`, step C's first half): the notebook's panel in
  three tabs; **the calculator** (molar mass and composition, moles, a solution's mass, a dilution, a reaction's
  stoichiometry, limiting reagent and yield; its working shown, its result written in the notebook); **the reference**
  (the elements, the species, the reactions, the model's own solubility). The formula grammar moved into the core.
- §13.2: the check `calculated`. A play may type.
- §13.7: the experiment `prepare` (a solution planned with the calculator, weighed, made up, worked out from the
  balance). The goldens unchanged.

## 0.57.0 (2026-11-25, Stage 57; signed off by the user: "Approved, merge and proceed")

- §7 (Stage 57, `docs/TDD_57_DISSOLVING.md`; `docs/CURRICULUM.md`, step B2): **transport to a grain** by Sano,
  Yamaguchi and Adachi's measured Sherwood number from the stirring's own dissipation (2 at rest), the old factor of
  three for stirring retired; **a crystal dissolves at transport's pace** (its surface step a thousand times its
  growth's), as real crystals do: stirred, minutes, not an hour.
- §13.7: the experiment `solubility` (copper sulfate's curve by the temperature its last crystal dissolves at; ln s
  against 1/T, the heat of solution).
- §20: the grain's transport. The goldens re-recorded with the user's approval: the demo's results by a hair (the
  oxide gone at 1010.1 s, not 1007.1; the crystals 9.197167 g; the distillate 37.764 g).

## 0.56.0 (2026-11-25, Stage 56; signed off by the user: "Approved, merge and proceed")

- §13 (Stage 56, `docs/TDD_56_RATES.md`; `docs/CURRICULUM.md`, step B's last part): the spectrophotometer's **heated
  cell holder** (20 to 80 C; a heater from the mains into the cuvette's lowest layer; saved, digested while set); the
  quantity `holder`. **A suspension dims the beam**: grains held up by stirring, twice their cross-section;
  the quantity `suspended`.
- §13.1: the holder's switch and steps, the cuvette's warmth in the readout; a notebook field clicked into is replaced
  by what is typed.
- §13.2: the condition `noted_count`.
- §13.7: the experiment `rates` (the order in the acid and the activation energy, timed by absorbance).
- `docs/CURRICULUM.md`: experiment 37 moved to step F (no instrument reads the solids' decompositions as they run);
  4 and 45 to a new step B2, after crystals dissolve by mass transfer (C15: the pentahydrate dissolves as slowly as it
  grows, an hour to saturate). §20: the holder's simplifications. The goldens unchanged.

## 0.55.0 (2026-11-25, Stage 55; signed off by the user: "Approved, merge and proceed")

- §13 (Stage 55, `docs/TDD_55_SPECTRO.md`; `docs/CURRICULUM.md`, step B's second half): **a spectrophotometer**, a
  single-beam instrument: a cuvette in its well, its wavelength tuned (380 to 780 nm), a blank, the absorbance read from
  the pass's own spectra through its slit's band and with its stray light, at the beam's height in the cuvette. **A
  cuvette**, 10 mm across inside. **A number written** in the notebook (`note`); the plot pairs a y with the x of its
  moment, or else the last before it.
- §13.1: the instrument and its controls on the screen; the notebook's write row (and Stage 54's faults seen in this
  stage's pictures: its button over the clock, its plot's "fit" past the panel's edge); **predictions** in place of the
  readout before a run; **progress** through the experiments, "done" and "next" in the list.
- §13.2: a report's item marked `predict`; conditions `predicted`, `noted` (`noted_value`) and `recorded_count`.
- §13.7: the experiment `beer` (Beer and Lambert's line and an unknown).
- §14: `cg_ui_progress`, `cg_ui_set_progress`; the Godot host keeps progress in `user://progress.json`.
- §20: the spectrophotometer's simplifications. The goldens unchanged.

## 0.54.0 (2026-11-25, Stage 54; signed off by the user: "Approved, merge and proceed")

- §13 (Stage 54, `docs/TDD_54_NOTEBOOK.md`; `docs/CURRICULUM.md`, step B's first half): **the notebook**, kept by the
  lab: `record` a reading, `log` one every so many seconds, `unlog`, `erase`; by instruments an instrument's reading,
  never the truth; saved, not digested. A table, a plot with each axis as it is, ln or 1/x, and a least-squares line.
  - **A pressure gauge** by instruments (0.1 kPa, ±0.5 kPa).
  - **Unknowns**: a bench's amount as a range, drawn once from its seed.
- §13.1: the notebook's panel; "+" beside each reading; the report on the card; the experiments list in up to three
  columns.
- §13.2: the tutorial's `report` (expressions over the lab's quantities, checked within a tolerance, beside the
  textbook's ideal); conditions `logging` and `recorded`.
- §13.7: the experiments `titration_curve` (an unknown acid's curve and end point) and `vapour` (water's vapour
  pressure, Clausius and Clapeyron's line).
- §20: a logger's and the fit's simplifications. The goldens unchanged.

## 0.53.0 (2026-11-25, Stage 53, merged after 0.59.0; signed off by the user: "Approved, merge and proceed")

- §6 (Stage 53, `docs/TDD_53_WALLS.md`; the roadmap's area 8b): **the glass in rings**. A glassware vessel's wall
  is its floor and rings of about 1 cm up to its brim, each a cell of its glass:
  - each conducts to the next through the glass (`conductivity` on wall species: DURAN's 1.2 W/(m K));
  - each loses to the room by its own outside at its own temperature: natural convection on its side, a floor's base
    as a disc facing down (Lienhard eqs. 8.35, 8.37, 8.39), radiation by glass's emittance (Lienhard Table 10.1);
    heat crosses half its glass to either side; the glassware's chosen `loss` is gone;
  - each touches the column's liquid layers and the gas over them by natural convection on a vertical wall
    (Churchill and Chu, as Lienhard gives them; air from Lienhard's Table A.6), set each step;
  - the floor touches the lowest liquid by Stage 45's heated floor; a burner heats the floor;
  - a floor boils with hysteresis: from its cavities' onset down to its boiling point (`bubbles::floor_nucleates`),
    so one a flame holds between the two boils steadily (the bubbles' haze had flickered between two pictures).
- §6: **a powder bed** on a floor takes its heat through the bed (Zehner and Schlünder, Kandula's form; the grains'
  conductivities by mineral analogues, calibrated).
- The lab: a vessel's view lists its rings' temperatures (`wall_c`); its glass is summed from the rings. The debug
  panel shows its floor's heat. Every glassware vessel's heating and cooling shift; both goldens re-recorded with the
  user's approval.
- §12.4: the pass and the thermal camera see each ring at its own temperature and each layer at its own, running
  continuously between their middles (a set's ninth row, `TEMPS_ROW`; `heat_of` in the per-pixel source).
- §20: the rings' simplifications.

## 0.52.0 (2026-11-25, Stage 52; signed off by the user: "Approved, proceed")

- §13 (Stage 52, `docs/TDD_52_VOLUMETRIC.md`; `docs/CURRICULUM.md`, step A's second half): **volumetric glassware**,
  each to its ISO standard's rows: a 50 mL burette, a 25 mL bulb pipette, 100 and 250 mL volumetric flasks, 10, 25 and
  100 mL measuring cylinders; and a foam-cup calorimeter (expanded polystyrene, a wall species).
  - **An outlet at its foot**: a burette or a pipette stands on a vessel's mouth and drains into it through its tip
    under its own head, as far open as its stopcock (`cap`); the tip's bore gives its standard's delivery time.
  - **`fill`, to the mark**: a pipette draws, a volumetric flask is made up; each mark at its piece's own true
    volume, within its class from the seed.
  - **Readings**: a burette's and a cylinder's scale to its class and a tenth of a division; a thermometer's lag.
  - `mark_off_ml`, `scale_ml`, `tap`: quantities for steps, results, monitors.
- §13.1: tall glassware drawn to fit; a stopcock, a mark; a pipette and a burette carried onto a vessel; "to the
  mark"; the tap's slider.
- §13.7: the experiments `standard`, `titration` and `calorimeter`.
- §20: the volumetric glass's, the thermometer's and the foam cup's simplifications. A mouth's diffusion path
  integrated finely (Stage 50's), and a species more: the goldens re-recorded with the user's approval.

## 0.51.0 (2026-11-25, Stage 51; signed off by the user: "Approved, merge and proceed")

- §6 (Stage 51, `docs/TDD_51_COLUMN.md`), more of the vertical dimension:
  - **a pour's stream mixes where it lands**: every pour lands as a layer of its own. Its kinetic energy is work on
    the column, Linden's 0.2 of it raising the potential energy; its entrainment comes first (Ricou and Spalding's
    0.32 Q0 x / d0, as far as Turner's fountain plunges, lifted into the top and paid for exactly) and the rest mixes
    from the top down. A gentle pour runs down the wall and lands at no speed. Stage 47's 2 mL/s rule is gone;
  - **a column's shape is its glassware's**: its layers' heights and its interfaces' areas, so a conical flask
    narrows.
- The roadmap: area 8b, the glass in layers (a fixed grid of rings, decided); area 8c, liquids that expand with
  temperature (their own stage).
- `docs/DATA_SOURCES.md`: a jet's entrainment (Ricou and Spalding 1961) and a fountain's height (Turner 1966), gated.
  §20: the stream's simplifications.
- Tests: energy checks compare ratios, doctest's `Approx` being absolute below 1.
- §6: the overflow brings its spill cell to its fast equilibria when it receives anything (P1; a gap since Stage 8).


## 0.50.0 (2026-11-25, Stage 50; signed off by the user: "Looks good! Approved, merge and proceed")

- §13 (Stage 50, `docs/TDD_50_MEASURED.md`; `docs/CURRICULUM.md`, step A): **the measured lab**.
  - **A balance** (`balance`, top-loading 0.01 g or analytical 0.1 mg): no cell, no edge; under a vessel as a
    burner is (`weigh`, `weigh` with `onto`, `unweigh`), left where its vessel stood when the vessel is lifted
    off; it reads its load's conventional mass (OIML D 28: less the air it displaces, so hot air reads light and
    carbon dioxide heavy), its tare (`tare`) and its linearity, to its readability; `OL` past its capacity.
  - **Readings by instruments**: a bench's `readings` and `seed`. Each vessel's thermometer, pH meter and marks
    read the truth with an offset drawn once from the seed, to their resolution; the readout shows them and a
    vessel's `label`, not what it holds.
  - **The hand**: `pour` and `tip` with `about` move the amount aimed at within 10 %; a `tip` with no species
    moves the solids in proportion.
  - `lab/instruments.hpp`; `data/instruments.json`; the marks in `data/glassware.json`; `reads_g`.
- §6 (the user's choice, 2026-11-25, when the weighing experiments showed a dish keeping its own gas): **gas
  exchanged at an open mouth**. An edge's `exchange` (area over length, `World::set_exchange`); a process after the
  drains moves each gas down its mole fraction at its diffusion in air (`air_diffusion` in `data/species.json`: NIST TN
  2279 and Incropera), the exact two-cell relaxation, with its enthalpy; the lab gives each open vessel's mouth
  Stefan's tube from what it holds to its brim. Open vessels evaporate at Stefan's rate; a dish's own carbon dioxide
  and steam leave it. E1 (§18), T1 setting exchanges aside; the sweeps exchange gas. A controller's trial counts a
  response under a millionth of its unit as none. Every session's record re-recorded (the demo's open beaker
  evaporates 0.375 g in its two hours).
- §13.1: the balance's body and seven-segment display, its gestures and "tare"; the readout by instruments; a
  typed amount in the pour menu (mL, or g of a solid), one "tip the solid" by instruments.
- §13.7: the experiments `hydrate` (the water in blue crystals) and `carbonate` (what heat takes from a
  carbonate).
- §16: the balance and the readings built. §18: I1. §20: the instruments' and the hand's simplifications.
## 0.49.0 (2026-11-25, Stage 49; signed off by the user: "Approved, merge and proceed")

- §6 (Stage 49, `docs/TDD_49_STIRRED.md`), Stage 47's open items:
  - **stirred, a layered column mixes as far as the turning pays**: of what the turning dissipates
    (`CellMeta::stirring`, set by the lab from its turning energy each step), Linden's mixing efficiency 0.2 raises
    the column's potential energy, from the bottom up; two layers exchange what it pays for and merge when it pays
    for all. The layers bench mixes in about a second as the turning builds; a short stir leaves it part mixed;
  - **an outlet at a layered vessel's bottom, and the cap of one upside down, draw its lowest layer**: their drains
    follow it at each step's boundary, as a burner's edge does.
- `docs/DATA_SOURCES.md`: the mixing efficiency (Linden 1979, Osborn 1980), gated by its measured test. §20: the
  stirred column's simplifications.

## 0.48.0 (2026-11-24, Stage 48; signed off by the user: "Works perfectly! Approved, merge and proceed")

- §13 (Stage 48, `docs/TDD_48_CAMERA.md`): **a thermal camera**, an 8–14 µm instrument in the cabinet.
  - Its frame is the size of its screen: over the screen (see-through), or fixed on the room.
  - Its screen is the renderer's sensor display of the frame, with its scale, centre spot, emissivity, range and
    the hottest and coldest marked, all the screen's own light.
  - Its settings: palette, emissivity, reflected temperature, marks, range auto or locked, gain. The commands
    `frame` and `thermal`, saved and not digested.
  - `lab/thermal.hpp`: the palettes (`data/thermal.json`), the span and auto gain with hysteresis, the search for
    the hottest and coldest a step at a time (O26).
- §13.1: the frame taken up by its handle, live while carried, let go on the room or home over its screen, Escape
  putting it back; the readout's controls and readings. **The cabinet scrolls** (the wheel over it, its bar), every
  item in a box of one size, instruments off in it.
- §18: O26. §20: the camera's simplifications.

## 0.47.0 (2026-11-24, Stage 47; merged after 0.48.0; signed off by the user: "Approved, merge and proceed")

- §6 (Stage 47, `docs/TDD_47_COLUMN.md`): **the vertical dimension**. A vessel of a standard shape is a column:
  - its own cell, with layer cells under it, grown four at a time as a gradient needs them; a vessel starts as one
    cell;
  - gas rises, solids sink, a lighter layer under a heavier overturns, alike ones merge;
  - stable ones exchange what is dissolved (Fick) and heat (k A / dz);
  - a steep step is resolved into thinner homogeneous layers, to half a millimetre.
- §6, §13: in the lab:
  - a layered vessel reads as a whole and layer by layer;
  - a gentle pour lays its liquid as a layer, a fast one stirs it in;
  - a burner heats a column's lowest layer;
  - a bench may start layered; save and load keep it.
- §12.1, §12.2: a layered vessel is a band of each layer's own liquid, read at the pixel's height
  (`BANDS_ROW`).
- §4.2, §18: an empty cell has no temperature and holds no energy (T3); L1 holds a column together.
- §9, §10: nickel sulfate in solution, green (NBS, Pitzer and Mayorga, Krumgalz, Shigematsu). Pitzer's third-virial
  term counted C twice: fixed (the demo's pairs have none).
- §13.7:
  - the experiment `layers`, two liquids layered (green over blue);
  - the prism and the grating in the list (a catalogue entry may name a scene);
  - the list in two columns past eight.
- From the user's first test:
  - the layers bench has a beaker of the blue, a bottle of the green and an empty beaker;
  - the pour menu's "pour gently" pours as a trickle;
  - a scene's revisions are unique across scenes, so the prism shows its dark room, not the lab's wall that was
    left on the GPU.
- From the user's second test:
  - a tipped vessel's layers lie level in the room, and it pours its top layer first;
  - a pour reads the whole layered vessel;
  - a held pour at speed no longer breaks C5.
- Water's properties by temperature are the core's (`solutions.json`'s `solvent`), boiling's bubbles' too.

## 0.46.0 (2026-11-24, Stage 46; signed off by the user: "Perfect, approved, merge and proceed")

- §12.4 (Stage 46, `docs/TDD_46_LWIR.md`): **the long-wave range**, 8–14 µm, what a thermal camera sees, in the same
  renderer as the visible.
  - Every row a segment at 20 nm. Water's n + ik measured (Hale and Querry), glass's as fused silica's (Kitamura
    et al.), Fluke's grey emissivities; the latex and the flames calibrated.
  - The room's long-wave light a black body at its temperature. Every body emits by its own (Kirchhoff, in the
    flat shading and the traced rays alike). Glass takes in what it neither passes nor turns away: opaque in the
    band, a vessel shows its outer glass.
  - A hot solid's glow is its temperature's; the glow row is retired.
  - One scene is the whole room at any view: the grid on the room's own squares, no body left out off the
    screen. A body can be hidden from a sensor's rays.
  - The sensor mapping: a sensor's display shows the long-wave light at the matching point of its frame, as its
    camera reads it, one step from the band's signal to a temperature, a palette's colour, its primaries
    driven to it, through what is in front.
  - The debug panel's "infrared": the whole picture as an 8–14 µm camera sees it.
- §18: O21 (the thermal radiance theorem), O22 (a ray's shares whole), O23 (the light's table its integral), O24 (the
  visible picture as it was), O25 (a sensor's pixel its truth). §20: the long-wave range's simplifications.

## 0.45.0 (2026-11-24, Stage 45; signed off by the user: "everything is approved, merge and proceed")

- §11 (Stage 45, `docs/TDD_45_BUBBLES.md`): **boiling's bubbles** (`lab/bubbles.hpp`, `data/boiling.json`).
  - A vessel heated from below is one bulk and a film at its floor: the floor's heat shared between natural
    convection (Raithby and Hollands) and nucleate boiling (Rohsenow).
  - Bubbles start where the glass's cavities hold a nucleus at the floor's pressure, the liquid's weight on it.
  - They leave at Fritz's size, rise at Baz-Rodríguez's speed, and condense in a cooler bulk (Chen and
    Mayinger), gone a fraction of a millimetre up in cold water, at the surface at the boil.
  - Derived each step from the burner's flow, not state: the world, its digest and the goldens are unchanged.
  - Seen as a white haze from the floor up to where they end, their extinction times 1 − g (0.85, Flatau et
    al.); a reaction's gas as bubbles to the surface.
- §12, §12.2: bubbles are a scatter medium (`data/optics/media.json`'s scatterers: albedo, efficiency, asymmetry),
  its albedo a seventh row of a vessel's spectra (`BUBBLE_ROW`); a body's vec4 6 holds a vessel's bubbles; the
  per-pixel source reads them wherever a haze is.
- §13.3: `bubbles_cm`, how high a vessel's bubbles reach. §13.7: the experiment `boiling`. §18: B1. §20.

## 0.44.0 (2026-11-24, Stage 44; signed off by the user: "Stage 44 is approved, merge and proceed")

- §9 (Stage 44, `docs/TDD_44_PRECIPITATION.md`): **precipitation and gas**.
  - Copper hydroxide, Cu(OH)₂(s) (SKB TR-00-13's row: log \*Ks 8.55 against the evaluated 8.64 to 8.67), comes
    out of copper sulfate with sodium hydroxide by a fast equilibrium, as grains of 0.3 µm, pale blue. An acid
    dissolves it again, another fast equilibrium; a copper sulfate solution alone holds a trace of it and reads
    about pH 5.
  - It turns to black copper oxide at Fukuda's measured first-order rate (127 kJ/mol): two years at 25 °C,
    half an hour under boiling water, seconds dry above 150 °C.
  - Malachite dissolves in sulfuric acid at Nicol's measured surface rate (order 0.65, 53.6 kJ/mol), its
    carbonate leaving as carbon dioxide. The demo's 0.57 mg of malachite left after its first stage now
    dissolves in its acid: the goldens re-recorded with the user's approval.
  - The pH is read only within the activity model's range (6 mol/kg left over of an acid or a base).
  - `chem_tool study precipitation`: the titration, every mixture at the closed form; the half-lives at theirs.
- §8: each fast equilibrium is probed again only once its cell has changed since (up to sixteen rounds); §9's
  particles: a precipitate comes as grains of its own size (`grain_um`).
- `docs/EXPERIMENTS_ROADMAP.md`: area 3 planned in full (bubbles from a heated floor's film, collapsing in a
  cooler bulk, quasi-steady, a scattering gradient), and two new areas, 8 the vertical dimension and 9 two
  liquids (the user's questions, 2026-11-24).
- §13.7: the experiments `precipitate` (a precipitate, black with heat, blue again in acid) and `fizz` (a gas
  from a solid into a balloon). §20: the hydrolysis's trace solid, the oxide's rate in a base, the pH's range.

## 0.43.0 (2026-11-24, Stage 43; signed off by the user: "Approved, merge and proceed")

- §9 (Stage 43, `docs/TDD_43_ACIDITY.md`): **acids, bases and pH**. Sodium hydroxide and sodium sulfate in
  solution (NBS 1982), the neutralisation a fast equilibrium (−55.84 kJ a water, so the solution warms), and
  the pH derived from what is left over of the acid or the base and water's own ionisation. Kw is held to
  IAPWS R11-24 from 0 to 100 °C; the new Pitzer pairs to Hamer and Wu and to Goldberg.
  `chem_tool study neutralisation`.
- §13.1, §13.3: the pH on the readout and the debug panel; `ph` for monitors, conditions and goals; a part's
  condition `signals`.
- §13.7: two experiments, diluting an acid and an acid meets a base.
- §20: the acid's pH and its neutralisation's heat without bisulfate; the sodium salts only in solution.

## 0.42.0 (2026-11-24, Stage 42, merged after Stage 43; signed off by the user: "It works perfectly! [...] Approved, proceed")

- §13, §12 (Stage 42, `docs/TDD_42_VIEWER.md`): **the spectral viewer**, an instrument of the cabinet that
  holds no matter (the world and its digest are the same with it or without). Its probe always reads: it is
  taken up from its holder and reads wherever it is carried, at every frame; let go, `probe` fixes it on an
  item or a part at that place of it in its own frame (it goes with it, turned or upside down), on the room,
  on its own screen (a frame late), or home. It reads the light that reaches the eye from a disc of 4 px, ray
  by ray through points uniform over it, coarse first, and settles to the disc's own integral (O19).
- §12.1: **an image's weights for an emitting texture.** The viewer's screen is an LED panel, each pixel
  three subpixels of smooth primaries (620, 530 and 450 nm), read exactly over any sample (O17). Its plot
  has a column a nanometre and a rainbow of each wavelength as the game shows a spike of it (through the
  pass within 3 on the eye metric).
- §13.1, §12.3: **a view of the room**, 1x to 16x about the pointer (the wheel, a pinch; two fingers or the
  middle button move it; Home, Ctrl/Cmd+0 or the top bar's "whole"), the top bar, the readout and the panels
  as they were. The room is laid out anew at it (O18: the same room larger). While the view moves nothing is
  traced, and its pixels are carried from the settled ones; at rest each pixel starts from them once (O20:
  a tenth closer, 96% keep their history).
- §14: `cg_ui_zoom`, `cg_ui_pan`, `cg_ui_view`, `CG_KEY_VIEW`, `cg_ui_play_shot`; `cg_optics` gains its
  images, its views and the room. Plays may set a bench out, turn the wheel and take pictures.

## 0.41.0 (2026-11-24, Stage 41; signed off by the user: "Works perfectly, approved, merge and proceed")

- §13.2 (Stage 41, `docs/TDD_41_EXPERIMENTS.md`): **a tutorial is strict or free**, by a switch on its card
  (strict as the screen opens, the player's at any time). Free, it only points: every gesture and the clock
  are the player's, while steps pass and runs fail as when strict. A tutorial's results may show any
  quantity's start, least, most and value now.
- §13.7 (new): **the experiments**, a catalogue (`data/experiments.json`) listed by the top bar's
  "experiments": boiling under a vacuum (pressure), hot and cold dissolving (heat given out and taken in),
  what speeds a reaction (stirring against warmth, with a thermostat the player makes); the demo; the lab
  with no steps. `chem_tool study solution_heat`. The roadmap of what comes next:
  `docs/EXPERIMENTS_ROADMAP.md`.
- §14: `cg_ui_set_strict`, `cg_tutorial.strict`, `cg_ui_experiments`, `cg_ui_choose`, `cg_ui_experiment`.

## 0.40.0 (2026-11-24, Stage 40; signed off by the user: "Approved, proceed")

- §18 (Stage 40, `docs/TDD_40_GATES.md`): O2 is checked at each nanometre from 380 to 780, the spectra's own
  points (it was each half nanometre, the halves the tables' interpolation).
- The gates (no behaviour of the game): a ctest entry for each lab test case, the heaviest split into parts
  or given threads by marks, the sanitizers beside the rest from the start; `make gates` 614 s to 320 and less.

## 0.39.0 (2026-11-24, Stage 39; signed off by the user: "Approved, merge and proceed")

- §13.3 (Stage 39, `docs/TDD_39_TRIAL.md`): **a controller's trial runs beside the lab**, two of its seconds
  at each of the lab's steps (it ran whole inside one step: 50 ms for the monitored scene's, seconds for an
  hour's). A controller set or rigged is untuned ("trying") until it ends; one tuned again keeps its gains
  until the new. A running trial is saved with its copies and is in the digest.
- §13.1: the budget's second piece is sized from what the first step cost (eight dear steps at once were a
  frame of 18 ms).
- §19: the measurements.

## 0.38.0 (2026-11-24, Stages 37 and 38; signed off by the user: "Approved, merge and proceed")

- §13 (Stage 38, `docs/TDD_38_LEFT.md`): **the trash's leftovers.** A part put in the trash keeps its wires
  and rigs to what was outside it, and they come back with it once both ends are on the lab again (a
  controller rigged again tunes itself); `empty_trash` and the trash box's "empty" button: everything in it
  leaves for good, an item to the cabinet, a part scrapped. The trash box's count ("trash (2)") and the
  emptying's notice say the number in it (both said 0: a count given as an integer to a format that takes
  doubles; found by the user).
- §18, §19, §20 (Stages 37, 38): O4's open pixels said in its row (none to eight a picture, once settled:
  `make screenshots` lets each picture settle 240 frames, Stage 37); O14 said to be unused; the frame's
  budget restated for a 120 Hz display; *PLANNED* taken off what is built (crystal populations, solute
  activity, decanting).
- Stage 37 (`docs/TDD_37_RIGHT.md`) measured and recorded, without changing the renderer: more points before
  a parted pixel stops (clears the CPU's off pixels, not the GPU's); the prism's slit cut where the middle
  ray's route changes (right, and twice O13's rays at a rim with one beam a record).

## 0.37.0 (2026-11-23, Stage 36; signed off by the user: "Approved, merge and proceed")

- §13.1, §19 (Stage 36, `docs/TDD_36_SPEED.md`): **a frame's cost the same at any speed.** A host gives
  the lab's steps a budget of the CPU's time in a frame (`cg_ui_set_budget`; the Godot host 3 ms): a frame
  runs whole steps of 1/60 s until it has spent it and carries the rest, a frame's worth at most; a heavy
  moment keeps a lower speed, shown beside the clock. The steps are the steps however they fall (D1); the
  session records are unchanged. Cheaper steps, the same values: a part's reading without the shape
  searches; a sediment's lie kept apart and found from a table per turn. Cheaper, not the same to the bit
  (the picture and a hand pour; not in a record): a hand pour's lie anchored on its volumes, the vortex's
  share by the secant. At 600×: a stirred vessel poured by hand 140 ms a frame to 4.7; three monitors and a
  controller 11 to 3.4; a controller's trial 1.2 s to 54 ms. Two things for the GPU's frames at the
  lab's steps were built, did nothing there, and were taken out: the spread of a step's pixels over two
  frames, and sending only the changed pages of the scene. `chem_tool study speed`; the cost play runs on at 600×.

## 0.36.0 (2026-11-23, Stage 35; signed off by the user: "Stage 35 is approved")

- §12.3, §11, §18, §19, §20 (Stage 35, `docs/TDD_35_ONE.md`): **one renderer.** A pixel's own rays,
  traced in the pass (Stages 29 and 33), are the only reading. Removed: the refraction maps and
  their knots, the tables of a held vessel's rays (stacks), the store of maps made before the game
  ran (`chem_tool maps`, `cg_optics_store`, `cg_ui_prepare`), the second pass's readings, the
  readings tried on the way (`LAB_BEAM` and its modes) and the measuring switches no gate needs;
  4,450 lines. `cg_optics` names the rectangles the tracing passes run over (`rectangles`). The
  picture did not change: the reference's numbers through the play are the same value for value,
  the GPU's frame and its pixels off the truth as before.
  Mended in the reading, found when it became the default: a thin line keeps its light through two
  layers (`O8`: a footprint's rows weighed by their width); a correction's place in its pixel is not
  tied to its wavelength. Open: a prism's slit where a ray is turned back inside the prism (3 pixels
  of a row wrong at rest, 5 after a turn; two tests marked, not loosened).
  §12.3 is rewritten for the one renderer: the tracer in the pass, a beam, a pixel as its beam's
  reading and the mean of its rays' corrections, beams kept and corners shared, the rays' budget.
- §18 (Stage 35): **O16, the pass in single precision is the pass.** The whole per-pixel source
  compiled in single precision on the CPU with the GPU's tolerances (`OpticsFrames` with `single`;
  `src/optics_pixel_host.inc` is what both arithmetics wrap the source in), held to itself in double
  over the play's bench. What it found of the GPU's few pixels off the truth (O4): they are not
  single precision's. The source's own frames stop with about one pixel in ten thousand visibly
  off, in double as in single (4 to 6 of 45,000 against the GPU's 0 to 9): the stopping rule's tail,
  and a noise estimate that forgets a rare large correction. Open, with what would mend it.

## 0.35.0 (2026-11-23, Stage 34; signed off by the user: "Approved, great work")

- §13, §13.1 (Stage 34, `docs/TDD_34_GESTURES.md`): **the screen's gestures.** A part is taken from
  the drawer with a press and put down with another; a condenser's outlet is led to a vessel by a
  line dragged from its end (`undrain` takes it off); **a trash**: `trash` and `restore`, 64 kept, an
  item's matter staying in the world and a part's settings with it; on the screen its box at the
  cabinet's foot, Backspace, and the undo key (`CG_KEY_UNDO`).

## 0.34.0 (2026-11-23, Stages 29 to 33; signed off by the user after playing: "Looks perfect", "Approved")

- §12.3, §18 (Stage 33, in progress, `docs/TDD_33_FAST.md`): **the pass made fast.** On the GPU
  (M3 Pro, the play of `make pass-cost`), a frame's median: a vessel carried 62 to 8.5 ms, poured by
  hand 97 to 8.4, stirred 58 to 8.4, settled and at rest at the display's rate (8.3); single frames
  still run to 14 to 16 ms at the simulation's steps where a stream runs and a reaction begins.
  What is settling waits while a thing moves (a beam traced counts against the rays' budget), a
  straight beam is read as its corners' quad, a plain pixel keeps its points through a change of
  colour, and pixels whose rays part have the budget first. The torus in
  closed form (its search was five sixths of the pass); a ray tries only the surfaces at its
  heights, may be turned back four times in a shape and meet sixteen surfaces, and then goes
  straight on as if the rest of the body were not there (the truth too); a correction is one ray, for one band in four where a beam's
  rays go one way; the corners of neighbouring pixels are traced once, in a pass of their own; what
  lies in a vessel is drawn to a sixty-fourth of a pixel; a pixel's beams are kept while what it
  looks through is where it was, and through a change of what lies in a vessel on rows its rays do
  not pass; a pixel whose rays have just moved shows its beam's reading alone that frame, and takes
  its corrections once it rests (0.64% of pixel-frames visibly off while things move; for the
  user's eye, `LAB_BEAM_AT_ONCE=1` the other way); a pixel beyond a turned vessel's outline is not
  its pixel; the pixels read by rays have a budget of their own; the fixed estimate reads a
  footprint as one quad, and a wall that is one under a footprint as that. O12 (a kept beam is the
  beam), O13 (a frame's rays are bounded), O15 (the pass's rays in single precision are its rays in
  double). A play: gestures as data (`lab/play.hpp`, `data/plays/cost.json`), timed by
  `make pass-cost`. The two session records re-recorded with the user's approval (2026-11-23:
  Stages 30 to 32's change of behaviour, evidence in `docs/TDD_32_VORTEX.md`).
- §12.2, §12.3, §18 (Stage 29, `docs/TDD_29_TRACER.md`): **a pixel's own rays, traced in the pass**:
  a pixel is its beam's exact reading and the average over time of its rays' corrections (off
  unless `LAB_BEAM=5`; `make run` sets it); a diffraction grating in the prism demo; O9 (the pass's
  rays are the truth's), O10 (four nodes speak for each stretch of the spectrum), O11 (a pixel's
  parts make it whole), O7 over bands. Its cost on the GPU is left as it is, by the user's decision.
- §11, §12.3, §18 (Stage 30, `docs/TDD_30_CONTENTS.md`): **what lies in a vessel, at any turn**
  (`lab/contents.hpp`): the liquid's surface level in the room, sediment a flat section under a
  pile, both tracers following a ray through liquid, air and sediment by where it is; a body turned
  by any angle about a pivot; the wall's table reaches a row beyond the screen; H1, H2.
- §6, §12.3, §13, §13.1, §20 (Stage 31, `docs/TDD_31_POUR.md`): **a pour by hand**: the vessel
  turns about its lip over the other's mouth and pours what stands over the lip, Torricelli's speed
  over the wet part of its mouth (a weir to an orifice, one law); a drain carries a share of the
  solids with its liquid and solids run by themselves; decanting, a dry powder; a stream that thins
  as it falls. A script's `pour {from, to, ml | all}` is unchanged.
- §6, §11, §18, §20 (Stage 32, `docs/TDD_32_VORTEX.md`): **a stirred liquid's vortex**, a cone whose
  size is its turning's energy, `E = (M g / A) V`, at most `M g D / 3`; **the stirrer's work is in
  the ledger** (an edge's power, from a source of work: a reservoir whose energy carries no
  entropy); the turning holds up the share of the solids its dissipation can (their weight in the
  liquid times Stokes' speed), and they settle at that speed. **The two sessions' records change**
  (the world has a cell more; the demo's stirred flask is 0.1 s quicker and its crystals 0.35 mg
  more): shown in TDD 32, recorded again only with the user's approval.

## 0.29.0 (2026-11-22, Stage 28)

- §6, §8: **sealed cells**: a rigid cell with no gas path to an elastic one is sealed (V3 keeps the
  mark), its energy `U + P_atm V` (its enthalpy at an atmosphere: open cells, and every record, are
  unchanged), its pressure `n R T / V_free`, its equilibria and reactions at that pressure. Water
  boils where the steam tables say at that pressure (within 2.5% of NIST from 10 kPa to 2 atm);
  cells sealed together share one pressure (T1: gas from the higher to the lower); opened, a sealed
  cell is at an atmosphere within the step. A cell is sealed only at the room's pressure (a starved
  one is refused). No drain runs in or out of a sealed cell.
- §13: **a rubber stopper** seals a neck and pops at its hold over the bore (30 N: +37 kPa in the
  flask); **a bottle's screw cap** seals it, a control; **glass bursts** past its rating (+300 kPa a
  bottle, +200 a flask; calibrated), its contents into the room and the tray, the vessel broken.
  Nothing pours into or out of what is sealed. A session of it recorded (`data/sessions/pressure.json`).
- §13.3: **a writer** writes on its input turning on, off, or either; **a notice part** hears only
  notices containing its text, and signals for its seconds; **a controller** tunes again once what
  it controls has changed by a tenth (no bump), warns when it cannot hold its target, and tries a
  cap; a vessel's **pressure** and a material's **volume** are quantities.
- §13.4: notices of pressure nearing what gives way, a stopper popping, glass bursting, a sealed
  vessel boiling below 100 °C, a pour running, stopping and running dry; the tray's spill counts
  what condenses in it as no spill.
- §13.6: the debug panel gives a vessel's pressure and its pour as a pour, and what a signal part is
  doing (a controller's trial, gains, error, integral, level).
- §13.1: the stopper carried onto a neck; a bottle's cap button; the readout's pressure; the
  notice part's and the writer's editors; the cabinet's shelves fit the bench.

## 0.28.0 (2026-11-22, Stage 27)

- §6, §13: **pouring is a control**: a vessel in a pouring position over another pours down a drain
  with a rate. Held (`hold`, `pour {rate}`): its tilt's share of the vessel's rate, its solids too at
  the full tilt; a script's `pour {ml | all}` waits it out and decants. Placed (`invert`, `cap`):
  upside down over another, held by a stand, its cap an orifice under its head of liquid
  (Torricelli, Cd 0.61, held to the law), a control rigged by parts and followed by controllers.
  Goldens re-recorded with the user's approval (the pours take their time; the results unchanged).
- §6: a drain may have a rate, and carry solids; a solution poured past a brim spills exactly its
  excess (the overflow took a share of the liquid's volume alone, and left the vessel under-full).
- §13.1: **an item is carried**: a click picks it up, a click puts it down; over a vessel the left
  button pours while held, the right button opens the full menu (upside down, with a cap); the right
  button on an item chooses it; dragging is for wires, sensing, sliders and rectangles.
- §12.3: a vessel set upside down is drawn so, held to the 3D truth; a pour's stream; supports that
  come with the glassware (a gauze over a tripod; a retort stand and a clamp).
- §13: a balloon or a condenser seals a neck, not a beaker's mouth (adjacency, by the mouth).
- Open: a held pour drawn tilted (the renderer reading a part-turned body).

## 0.27.0 (2026-11-22, Stage 26)

- §13.3: **sensing, drawn and guided**: a monitor senses one item, by a dashed line from its sensing
  port, at its foot, onto a vessel, a balloon or a burner (`sense`, `unsense`); its settings open only
  then, in steps of what the item has now: the kind (a material, the vessel, other), the material, the
  measure, then the comparison. A material's amount (mol), a vessel's total mass and its solids are
  quantities now. The tutorial teaches it (36 steps).
- §13.3: **controllers**: a part that holds what it senses at a target, by a level from 0 to 1 its rigs
  follow (a burner's valve to it, a vessel's stirring by PWM); the player sets the target and a speed
  (gentle, normal, fast); it tunes itself by trying what it controls on two copies of the lab, fits the
  response, and takes SIMC's PI gains (Skogestad 2003, held to its published margins), with no wind-up.
  §18: G3 (its level), G4 (its PWM). Water in the flask held at 60 C within a degree at every speed.
- §12.3: a square of the screen lists fifteen bodies, not seven: two signal parts side by side were
  half hidden; a dashed line is one body.
- §12.3: **everything behind a held vessel is seen through it**: the GPU read six of a stack's
  bodies, so behind a held vessel only the last painted part showed (the user: "only one is ever
  visible"); now twelve, on the CPU and the GPU alike, the largest kept first where more stand behind.
- §12.3: **a stirred suspension is in its vessel's shape**: its haze was the vessel's rectangle taken
  as a cylinder, so in a conical flask the stirred copper oxide was a grey rectangle past the cone's
  sides; it is now along each ray's own path through the liquid, as the liquid's colour is.
- **A solution holds no solid**: the solids a vessel shows were what is condensed less what is liquid,
  which counted a solute's volume as a heap (a triangle in the acid's bottle); the view now gives its
  solids' own volume (`ItemView::solid_ml`), which the pass and the "solids" quantity read.
- A goal's value typed in the editor no longer reads past a valve's per cents (a crash, once the
  editor's draft grew).
- Open: a material's volume, a vessel's pressure; a controller tuned again as what it controls changes.

## 0.26.0 (2026-11-21, Stage 25)

- §12.3: **a shaped vessel's rows read between two knots**, each along the row's own outline, the light
  through each mixed by the share of the way: lines seen through a cone curve smoothly (the bands of
  four rows that jogged are gone). Knots refined in the rims' measure, at most 96, cropped to their
  outlines, made before the game runs; maps hold each ray's drop.
- **No maps made in play**: a glassware vessel's tables made ahead are found wherever it stands (its
  axis a whole pixel, exactly), and are made for the shelf's and the outbox's sizes too: placing a
  vessel cost up to 742 ms, now about 5. Maps lie where they were first put; the GPU is sent only what
  was added (`cg_optics.maps_changed_from`): lifting a vessel again sends nothing.
- **No box around vessels**: the room and the bodies are one picture. The optical pass shows the wall
  wherever no body is, so a window of any size scales them as one (the room was drawn sharp at the
  window's pixels, the pass stretched); and the wall is on the pixels the host paints (each shape on
  the pixels whose centres it covers, an outline centred on its edges).
- Open, documented: a held vessel over a shaped one shows steps in what is behind (the stacks' bands).
- Open, documented (docs/TDD_25_UPDOWN.md): coloured specks along vessels' floors and corners (maps of
  one piece in wavelength), and bending up and down in the renderer.

## 0.25.0 (2026-11-21, Stage 24)

- §12.3, §13, §20: **vessels in their whole shape**: glassware made to its standard (the low-form
  beaker, ISO 3819; the conical flask, ISO 1773; the screw-neck bottle, ISO 4796-1), its profile of
  lines and arcs read by the lab's capacity (each cell holds its shape's capacity to the brim: goldens
  re-recorded with approval), the liquid's level, the drawn proportions and the tracer (arcs as tori).
  The condenser stays as it was, until a condenser is made to its standard.
- §12.3: **the truth in three dimensions**, every body through the general tracer.
- §12.3: **a map for each band of a shaped vessel's rows**, read along each row's own outline near the
  rims (smooth outlines), made before the game runs (`chem_tool maps`, in `make native`). Rays through
  curved rows are taken as level in the renderer: bending up and down is Stage 25's.

- **Every number in `data/` has a known origin** (`data/sources.json`, ctest `data_sources` with
  its self-test): measured, with the tests that hold it to its source and its row in
  `docs/DATA_SOURCES.md`; calibrated, with why; or the game's design. A value with no entry, a
  named test or row that does not exist, or an entry that matches nothing fails the gate.
- **The audit's new checks**: the species table read back against its cited rows; molar masses
  against CIAAW's atomic weights (now documented); molar volumes against the cited densities; the
  apparent molar volumes against their densities; the hydrates' dissociation pressures against
  NBS and Cotti et al.; malachite's activation energy in Seguin's range; three more of the CRC's
  activity coefficients; water's absorption at 755–780 nm (Kou et al.); every glass vessel of
  every bench at ISO 3819's wall. The bench's calibrated equipment is documented.

## 0.24.0 (2026-11-21, Stage 23)

- §12: **measured colours**: copper sulfate solution's absorption from NBS Circular 484 (1949),
  malachite's reflectance from the USGS spectral library (GDS801).
- §7: **a surface reaction is two steps in series**: the reaction at the surface (with an order
  for its driver) and transport through the liquid, `(Sh D / d)(C − C_eq) / ν`, `C_eq` found from
  the affinity itself. Stirring raises transport alone, no longer every surface rate.
  - Ions' diffusion coefficients (Li and Gregory 1974), scaled as T / η by water's viscosity
    (IAPWS 2008); Nernst–Hartley for the salt and the acid.
- §7, §9, §20: **every rate measured**: malachite (Koga 2021), the oxide in acid (Majima 1980,
  second-hand, flagged low), the pentahydrate's growth (Tanimoto 1963) and nucleation (fitted to
  Sheng 2012's induction times, a lower bound), the dehydrations (Cheng 2019, Yu 2007).
  - The oxide is controlled at its surface: stirring hardly speeds it; warmth does (69 kJ/mol).
  - Crystals start rarely and grow by diffusion: the demo's crop is about 1,150 crystals of 1.9 mm
    after two hours near room temperature (a million of 0.2 mm in fifteen minutes before).
- **Fetched data is held to its source**: every measured or fitted value has a gate test at its
  source's own conditions (the kinetics, the transport, the first crystal inside Sheng's induction
  times; NBS's absorbancies and USGS's reflectances read back through the model's spectra).

## 0.23.0 (2026-11-21, Stage 22)

Signed off by the user on 2026-11-21 ("Approved, proceed").

- §12.3: **everything that takes space is physical.** A burner and its flame, a condenser's outlet,
  a signal part on the bench (its face, its lamp, its port; a timer's strip of time left), wires
  and rigs are bodies of the optical pass: bent, tinted and seen through what is held, as a vessel
  is. Three shapes: the slab and the bar (opaque, lit by the room) and the cone of glowing gas (a
  flame emits along its path and absorbs nothing), of calibrated surfaces and emitters
  (`data/optics/media.json`), shared by set.
  - No text on the bench but for what is chosen; a part is known by its kind's colour, its state
    by its lamp.
  - The scene holds 256 bodies (16 before); the screen in squares of 40 px lists the bodies a pixel
    looks at.
  - At an opaque thing's edge the wall is read where it is uncovered; behind what bends nothing the
    wall itself is seen, not a flat backdrop.
- §13.1: **an item's controls are not on the bench**: a burner's valve and a vessel's stirring are in
  the readout's column for the item chosen; a rig ends on the item itself.
- §18: O2 counts what bodies emit; the truth reads the drawn wall exactly over its samples.
- A GPU bug fixed: a pixel covered again by content it had shown before stayed blank.
- The gates: a ctest entry a test file, the reference's heavy loops on threads, sparse samples and
  -O2 under the sanitizers. The closing gate 37 min -> under 3 min.

## 0.22.0 (2026-11-20, Stage 21)

Signed off by the user on 2026-11-20 ("Looks good! Approved, proceed").

- §12.3: layers of refraction. What the player carries is a layer of its own, seen in front of
  what stands, and what stands is seen through it.
  - **One world, in pixels.** The wall stands 284 px behind the bench's plane for every body (it
    was 20 cm at each body's own scale); what is held stands 120 px nearer the eye. Up to six
    layers.
  - **The chain.** A round vessel's map answers a ray from anywhere, by its distance from the
    axis: a ray is followed layer by layer through the bodies' maps, with no tracing. Against
    both bodies traced together it lands within 1e-8 of a pixel.
  - **The stacks.** A held vessel's rays are followed on the CPU into a table (in bands of rows,
    at the spectrum's four nodes), which the pass reads as it reads a map. Each body is read
    where the rays cross it: its liquid along their own chords, its haze, its heap, its surfaces.
  - **Two passes on the GPU.** The pass over the screen holds one vessel's reading alone; a
    second, over the rectangles of what is held or is a prism, holds the rest.
  - **Glow is a light in the pass**: each body's glow at 1 nm, bent and absorbed by what is in
    front of it. Nothing is added to a pixel outside its history.
- §12.3: **no outline is drawn.** A vessel is its glass and what it holds; a line drawn over the
  pass is not bent by what is held before it. The line at a liquid's surface likewise.
- §12.3: **a vessel's floor** is glass all through (`floor_mm`, calibrated: 2.6 mm at 250 mL), a
  third core of its map; what the vessel holds rests on it.
- §18: `O5`, `O6`, `O7` and `O8` through layers. The O8 check's own truth finds images thinner
  than its scan.
- §19: a restarting pass 1.64 ms with nothing held (1.77 before the stage) and 9.6 ms with a
  vessel held; a settled pass nothing; following a carried vessel's rays 1.4 ms a frame on the
  CPU. Following rays in the pass cost 344 ms a frame: per-ray arrays are slow on the GPU.
- §20: a ray is not followed into a neighbour on its own layer; a body that is not round is read
  in a stack as seen straight on; a thin line seen only in a stack's rims keeps its light to 3%;
  what the screen still draws over the pass (the burner, signal parts, wires) is not bent.
- *PLANNED:* everything that takes space is a body of the optical pass, and an item's controls
  leave the grid for a panel of actions (Stage 22; §12.3, §13.1); a vessel's whole shape (lip,
  spout, flasks); a liquid's surface as a shape (meniscus, a stirred vortex, sloshing; §11).

## 0.21.0 (2026-11-20, Stage 20)

Signed off by the user on 2026-11-20 ("Approved, proceed").

- §12.3: a pixel is the light of all its rays, so a thin line on the wall seen through glass is
  never lost between two pixels.
  - **A general tracer, on the CPU.** A body is a list of surface patches (a quadric or a plane,
    the medium on each side); rays are 3D. The cylinder and the prism are data; spheres and
    cones later are more data.
  - **A refraction map for each body**, traced once: where a ray across the body lands at each
    wavelength, what its surfaces pass, its chord. Four samples a pixel, on the screen's own
    quarter pixels; cubic pieces in wavelength (a vessel's whole spectrum is one piece).
  - **The wall as a table** with exact integrals: no limit on drawn shapes, overlaps exact.
  - **The pixel pass traces nothing.** It reads maps and integrates the wall exactly over each
    part of the pixel's footprint and over a band's sweep of wavelengths. The search for cuts at
    wall edges and the random place in the pixel are gone.
  - **One light, two readings:** a compact one for maps of one piece (every vessel), the general
    one for the rest (a prism); a test holds them to the same light.
- §12.3: **real glass.** Each vessel's wall has a thickness of its own (`wall_mm`: ISO 3819's
  least wall for a beaker of its capacity), apart from its mass, which is its heat's. Before, the
  mass spread over the side and floor made walls two to three times too thick.
- §18:
  - `O7`: bending makes no brightness (the radiance theorem).
  - `O8`: a thin line keeps its light, wherever it lands.
  - `O5` restated: where every index is 1, a pixel is the average of its straight rays.
  - The truth is independent of the renderer: sub-pixel rays through its own tracer, the wall
    read from the shapes themselves.
- §19: a restarting pass costs 1.77 ms, against 1.07 ms before (accepted by the user); `make
  screenshots` takes 34 s, against 69 s. The cost is read between 8 and 24 extra passes.
- §20: what a map interpolates, four parts a pixel at most, a route's change read at each side's
  own ray, one thickness of glass all round.
- §12.3: layers of refraction (what the player holds, shown in front of what stands on the bench,
  with refraction and every spectral effect; baths; up to six) are *PLANNED* for Stage 21.
- `make pass-cost`: the optical pass's cost alone.

## 0.20.0 (2026-11-19, Stage 19)

Signed off by the user on 2026-11-19 ("Approved, proceed").

- §4.6, §10: concentrated solutions.
  - **Activities from ions.** A dissolved species names its ions (`data/species.json`); its
    activity is `Σ ν_j ln(m_j γ_j)`, the solvent's `ln x − φ n/n_liquid`.
  - **One excess function.** `γ_j` and `φ` are the exact derivatives of Pitzer's excess Gibbs
    energy, with its parameters at 25 °C (`data/solutions.json`): CuSO₄ from Pitzer and Mayorga
    (1974), the acid fitted as fully dissociated to Staples (1981).
  - **No heat of dilution.** The excess is all entropy, so the temperature and conservation are
    untouched.
  - **Volumes.** A dissolved species takes its apparent molar volume.
- §9: the four forms of the salt.
  - Each crystallises from the solution by the surface law with nucleation.
  - The hydrates lose water in three first-order steps (calibrated at their thermogravimetric
    peaks). The one-step calcination is gone.
  - The pentahydrate gives way to the trihydrate under its solution at 93.2 °C (measured 95.9).
- §7: the surface law's driver is its molality.
- CuSO₄(aq) is refitted (`Hf`, `S°`, `Cp`) to the CRC's solubility, within 2.6 % from 0 to 100 °C.
  CuSO₄'s published C^φ is left out: extrapolated, it turned the water's activity back up beyond
  65 mol/kg, which a debug build caught as P1 in a flask boiled nearly dry.
  The trihydrate and the monohydrate are verified (NBS 1982) and have calibrated colours.
- §18:
  - K1 gains the electrolyte oracles.
  - P1's rounding bound counts a minor gas: the steam over a cold condenser drawing dry air
    failed it, latent since Stage 5.
- §17: the demo closes the valve at 12 g of water, not 10. At 10 g the boiling liquor is past the
  trihydrate's saturation.

## 0.19.0 (2026-11-18, Stage 18)

Signed off by the user on 2026-11-18 ("Approved, proceed").

- §13.6 added: the debug panel.
  - **The core** keeps the last step's flows by process (`Simulation::flows`): each edge's energy
    and mass by reaction, heat, drain, settling and overflow, and each cell's extent of every
    reaction. They are the ledger's sums, whether the step was checked or not, at no measurable
    cost (4.40 us a step against 4.42).
  - **The lab** (`Lab::debug`) tells an item from the inside: its temperature and heat capacity;
    each species' amount, mole fraction in its mixture, molality and activity; its flows in the
    last step; each reaction that names what it holds, with why it happens (`explain` in
    `data/reactions.json`), its rate and affinity, and what limits it.
  - **What limits a reaction:** at equilibrium; used up; no rate law; held at its equilibrium; running
    backwards; too cold, with the temperature it is fast from, or its half-life; its surface,
    driver and stirring; the supersaturation and the crystals starting or growing.
  - **The screen:** "debug" by the clock opens the panel for the item selected, with "step" (one
    step), "export" (the save, written by the Godot host to `user://saves/`) and scrolling.
- §14: `cg_ui_take_export`.

## 0.18.0 (2026-11-18, Stage 17)

Signed off by the user on 2026-11-18 ("Approved, proceed").

- §13.5 added: goals on the outbox.
  - **Slots:** the outbox is six slots, one item to a slot; `submit {item, slot}`, or a drop onto
    a slot. With no slot, the first free one without a goal, so the sessions are unchanged.
  - **Goals:** a name and terms on what is handed in to a slot, met live. The bench's are the
    game's and locked: the interactive bench has three (CO₂ in the balloon, pure water in the
    beaker, 99.9 % pure crystals). The player makes, changes and takes off their own with `goal`
    and `ungoal`, or in the screen's goal editor.
  - **Read by everything else:** met and no longer met are notices (an event, a warning); a
    monitor reads a goal's `met`; a notice part hears one goal by its id; conditions read `goal`,
    `goals_met` and an item's `slot`.
  - **Saving:** saved and loaded, not digested; the sessions' records unchanged.
- §13.2: the tutorial hands each item in onto its goal's slot, and its goal is the game goals met.
  Its hand-in failures stay: what is handed in cannot be taken back.
- §13.3: a notice part or a writer may name a goal.
- A slot shows its goal's state at a glance: met (a green band, border and "met"), falling short
  (amber, "not met"), or open.

## 0.17.0 (2026-11-18, Stage 16)

Signed off by the user on 2026-11-18 ("LGTM, approved, proceed").

- §13.4 added: notices in levels (detail, event, warning, danger).
  - **What the lab notices:** reactions starting and stopping, at the level its data gives
    (`data/reactions.json`: `notice`); what appears and is used up, not what a hand moved;
    boiling and boiling dry; a balloon 80 % full; spills into the tray.
  - **At once:** bursts, failed invariants, timers at zero, rigs acting, alarms ringing.
  - **Saving:** saved and loaded, not digested; the sessions' records unchanged.
  - **The notice part** hears them, at a level, about an item.
  - **The writer** writes the player's own notice, at a level, when its input turns on: with
    notice parts, the notices are a bus. The screen now treats notice parts as parts (it drew
    them as equipment), and a wire dropped on one goes into its input.
  - **On the screen:** the newest beside the clock; a panel with a switch per level, details
    silenced at first.
- §4/§7 (the core): the simulation tallies how far each reaction with a rate law ran in each cell
  (`Simulation::tally`), for the notices; not state.

## 0.16.0 (2026-11-17, Stage 15)

Signed off by the user on 2026-11-17 ("Approved, proceed").

- §13: drains are removable.
  - A drain comes off when either of its vessels is placed elsewhere, moved apart from the other,
    stowed or handed in. Moved together, the two stay joined.
  - A condenser with a beaker under its outlet moves freely, and a beaker handed in is no longer
    fed by the outlet.
  - The screen's softened refusal of moving such a condenser is gone; the tutorial's step says
    the outlet stops dripping.
- The demo session's record, re-recorded with the user's approval: from the beaker's hand-in
  (command 23), one step to a boundary (2545.0 s → 2545.1 s) and the drain closed. Every measured
  result is unchanged (the beaker's 40.868202 g of water included).

## 0.15.0 (2026-11-17, Stage 14)

Signed off by the user on 2026-11-17 ("Approved, proceed").

- §13.3: circuits.
  - **Making one:** a selection's signal parts, circuits included, collapse into one compact part
    on the free square nearest them (`circuit`), keeping their layout inside.
  - **Signals:** what a circuit holds signals as it would out of it, with the same digest, step
    for step.
  - **Working inside:** parts made (`part ... in`) and placed in it; moved and scrapped with it.
  - **Giving back:** `uncircuit` returns its parts to the bench where they fit.
  - **Nesting:** to any depth.
  - **On the screen:** "create circuit" in the drawer; a circuit's square with its lamp; an open
    circuit's panel to work in, with "up" and "close"; "open" and "give back" in the readout.
- The Godot suite's frame test allows the step the engine's own earlier frames may leave owing (a
  carried fraction just under one adds up to 61): a flake of the commit gate, seen once.

## 0.14.0 (2026-11-17, Stage 13)

Signed off by the user on 2026-11-17 ("Very good, approved, proceed").

- §13.3 added: signals.
  - **Parts:** timers, monitors and alarms on the bench, out of an unlimited drawer.
  - **Wiring:** parts are wired to each other (inputs in parallel are OR, a chain is AND, no input
    is free) and rigged to the lab's controls: a vessel's stirring, a burner's valve, a timer's
    reset.
  - **Evaluation:** every step, all at once, loops included; every link takes a step.
  - **Rigs:** they set a control on their source's turning on and off, or on any change. The last
    event wins, the player's hand included.
  - **Alarms:** an alarm stops the clock in the step it rang, and shows what set it off.
  - **In the lab:** signals are saved, loaded and digested. Commands: `part`, `scrap`, `set`,
    `wire`, `unwire`, `rig`, `unrig`, `reset`, `ack`, `wait_until {rings}`. §18: G1, G2.
- §13: the bench is a grid of squares (`chem-bench/2`, 28 × 15). Items stand on footprints by kind
  and volume, and `chem-bench/1` still reads. `move` moves several things together, all or none.
  The demo session's record is unchanged.
- §13.1: the selection: a click, Shift/Ctrl/Cmd, a rectangle, Escape; a selection carried
  together. The drawer stands beside the outbox; the part editor; a vessel's stir button at its
  foot. Pointer modifiers through `cg_ui_pointer`; `cg_ui_signals`.
- §13.2:
  - conditions are the shared module's;
  - purity (a species' share) and a balloon's fill are quantities;
  - checks may compare any quantity, and read parts, wires, rigs and the part editor;
  - the tutorial teaches a monitor rigged to the burner and wired to an alarm (35 steps).
- §13.2: under a tutorial, every editor gesture and key is the step's own, and "set" is refused
  unless the step's checks would then hold: a monitor or a rig cannot be set wrong. In the
  editor the pointer outlines the first gesture not yet done: a value box until it holds the
  step's value, and only then "set".
- The screen's timers of the first draft of this stage are gone, never released.

## 0.13.0 (2026-11-17, Stage 12)

Signed off by the user on 2026-11-17 ("Works perfectly! Approved, proceed").

- §17: the demo checked end to end with the final renderer, headless, through the screen and on the
  GPU; its measured results unchanged.
- §13.2 added: the tutorial: the demo in 28 steps, each with what to do, why, and checks read from
  the lab; failures that end a run until a reset; the goal, §17's "done when"; the card above the
  readout and a pointer at the next gesture. §14: `cg_ui_tutor`, `cg_ui_tutorial`.
- §13.2: a running tutorial takes only its step's gestures, and holds the clock while it asks
  for one; its steps without gestures wait on the lab.
- §13.1: a drop lands only on what stands on the bench: never on what is in the cabinet or
  handed in (a beaker dropped into the outbox over the balloon was refused, "takes nothing").
- §6: a rigid cell's excess gas is reckoned by the same arithmetic as T1's (gas + condensed −
  volume). Reckoned as gas − (volume − condensed), an idle vessel full to the last bit could
  expel a quantum T1 judged it had no excess for: an invariant failure after five hours of lab
  time, found by the tutorial played at a person's pace. The sessions' records are unchanged.
- §13.1: a condenser dragged off its flask while a beaker stands under its outlet stays beside the
  flask, without a refusal shown for the move along the bench.

## 0.12.0 (2026-11-17, Stage 11)

Signed off by the user on 2026-11-17 ("Looks very good, approved, proceed").


- §12.3: refraction and Fresnel built: a vessel is a glass cylinder of its wall's mass; water's
  and glass's indices as spectra at 1 nm (Daimon and Masumura; Schott's BOROFLOAT 33); the ray's
  turn, where it meets the wall 20 cm behind, and its chord through the liquid, exact for
  concentric cylinders, at each point's wavelength; Fresnel at each surface, what is turned away
  showing the room in front; the bench's drawing (the draw list's first part) carried as flat
  greys a bent ray lands on.
- §18: O5 (indices of 1 change nothing) and O6 (Fresnel loses no light) added; O1 and O2 restated
  for spectra and for the room in front.
- §19: the pass's cost remeasured (0.1 ms settled, 0.95 ms with every history starting); Stage
  10's 1.9 and 11 ms withdrawn (a busy machine).
- §20: what does not bend, and the calibrated room, declared.
- §12.2 item 6: a pixel's bands cut where its light is known to jump in wavelength (a bent view
  crossing a drawn edge, a change of route through the glass, a narrow absorption line); the
  content key holds coverage and the geometry of the glass a view crosses. The false positives
  and negatives measured with and without the cuts (`chem_tool study frames`), and on the scene
  test replayed with a refracting ball.
- §12.3: the prism demo: flint glass (N-SF11) traced through flat faces, a slit of white light,
  a dark room.

## 0.11.0 (2026-11-17, Stage 10)

The revision, signed off by the user on 2026-11-17 ("LGTM, approved, proceed"):

- §12: the full design restated around spectral textures: every distribution carried in the same
  wavelength bands, which may follow the scene; refraction re-evaluates the scene behind in the
  same pass, with the index a texture; fluorescence a matrix between bands; the probe at 1 nm on
  the CPU; pure water's absorption and scattering.
- §12.1 added: spectral textures (channels mixed by constant, gradient, image or live weights) and
  effects (absorb, reflect, emit, light, scatter, fluoresce, refract) assigned apart from the
  format; layered objects; the printed picture (CMYK) as the worked example.
- §12.2 (was §12.1): spectral samples are bands; their cuts fixed or adapted to the scene; the
  measurement moved to what the screen shows, judged by the eye metric fitted to the user's blind
  marks, over random spectra of eleven families; the rival representations measured and why each
  was taken or not; the band decision planned.
- §12.3 (was §12.2): the demo's optical pass, unchanged.
- §12, §12.2: the representation decided by the user's blind tests (rounds 4 to 6): each light's
  energy in equal bands weighted by the display's curves, one point per band moved every frame,
  each pixel's history started again when its content changes; the eagerness map planned as the
  way a frame's budget of points is shared out. §12.1: a channel has a line list beside its 1 nm
  continuum. §12: colours no display has are mapped keeping lightness and hue (OKLCh), onto the
  display's own gamut; refraction disperses by each point's own index; fluorescence re-emits as a
  light of its own.
- §12.2: the eagerness map replaced after its false positives and negatives were measured on a
  replay of the scene test: a pixel's history is kept by an exact content key and a fixed estimate
  (8 bands at their middles), holding what the moving points add to it; points by need (the
  pixel's own noise on the eye metric, an expected error of 2, 64 for a fresh pixel, none once
  done after 4 frames and 128 points, never fewer than 16 in a frame); the budget a ceiling.
  Round 8's blind scene test: as good as or better than 16 points everywhere at a twelfth of the
  work. The measured table added.
- Built (§12, §12.2, §12.3, §14): each light's energy table; absorbers' narrow lines; pure water as
  measured (`data/optics/water.csv`); the eye metric, OKLCh gamut mapping onto sRGB; the scene as
  spectra at 1 nm; one per-pixel source as the Godot host's compute shader and the C++ reference,
  each pixel's state on the GPU. `cg_optics` gains the spectra, lines and light table; the Godot
  host's pass is a compute shader shown by a `TextureRect`, with a budget property.
- §18: O3 restated over 1,500 random spectra of twelve families on the eye metric; O4 restated as
  the GPU's settled picture against the truth on the eye metric. §19: the pass's cost measured.
  §20: water's scattering as loss only; gamut mapping onto sRGB; the room's the only light, a
  glow added outside the history.

### Stage 10's first build (2026-11-16), superseded by the revision where they differ

- §12: the pipeline restated around one table; §12.1 with the measurement and its decision: 24
  bands, each an equal share of what the eye sees, worst ΔE₀₀ 0.80 and mean 0.19 over the demo's
  110 colours; §12.2 added: the demo's optical pass (the lit wall, bodies of revolution with real
  path lengths, liquids, heaps, suspensions, glow, the balloon's rubber, coverage, the table, the
  shared shader source and its reference build).
- §13.1: drawing is in three layers; the bench is a lit wall; a liquid's surface is a drawn line.
- §14: the optical pass through the C API; the host's shader pass.
- §18: O1, O2, O3 and O4 implemented and restated as built; UX4 tested.
- §19: the optical pass measured at 0.21 ms.
- §20: the optics' declared simplifications.

## 0.10.0 (2026-11-16, Stage 9)

- §13.1 added: the screen, engine-free: its layout, its gestures and the commands they become,
  the clock, the invariants shown, the readout, flat drawing.
- §13: a burner goes under a vessel straight from the cabinet.
- §14: the screen through the C API; the Godot host.
- §18: UX7 (the demo is playable with the pointer alone).

## 0.9.0 (2026-11-16, Stage 8)

- §6: overflow into a world's spill cell; with one, a drain is not limited by what fits below.
- §13: places (cabinet, bench grid, on, under, outbox) and the `place` and `stow` commands;
  equipment is used on the bench; `submit` lifts a vessel off its burner; a balloon's capacity
  and bursting; events; save and load; a pour leaves a step in progress.
- §18: D2 implemented; V2 with the spill cell.
- §19: the step cost after caching, and with Stage 8's additions.
- **The demo's golden record changed** (`bench/sessions/demo.jsonl`): the bench gained its tray
  (one more cell, 300 g of glass), and handing in the flask now takes one step to lift it off
  the burner. Every measured result of the three items handed in is identical to Stage 7's.

## 0.8.0 (2026-11-16, Stage 7)

- §13 rewritten around what is built: the bench as data, the twelve commands, refusals, reports,
  the digest, sessions and their records. The full design's remaining parts are listed.
- §14: the lab through the C API; `chem_tool run` and `bench`.
- §17 rewritten with the measured run: every number there is what the demo session produces.
- §18: D1, D4, R1 and L1 implemented; UX2, UX3 and UX5 tested. L1 states its one exception (a
  command that changes connections first reaches a step boundary).
- §19: the demo's measured step cost.
- §4.2 (code): an edge may be added during a step; a solid's particles move with it.

## 0.7.0 (2026-11-16, Stage 6)

- §4.6: activities of solvent and dissolved species. The solvent's is `−n_dissolved/n_liquid`,
  not the mole fraction of the earlier draft, which is inconsistent with the solutes' and let
  entropy fall. A solute needs a solvent (X2).
- §7: the surface law, its exact step, the driver, stirring.
- §9: particles, nucleation, the derived crystal size; the demo subset rewritten: one-step
  dehydration (the lower hydrates' data is not verified), neutral solutes, no solute volume.
- §18: X1 restated for particles; X2 added; the solubility oracle.
- §20: seven simplifications declared, among them one that is wrong in sign: left-over acid
  raises the salt's solubility in this model and lowers it in reality.
- Data: the pentahydrate and the sulfate ion verified; CuSO₄(aq) fitted to the handbook's
  solubility; four rate constants calibrated and labelled so.

## 0.6.0 (2026-11-16, Stage 5)

- §4.5: the step's order restated: rate reactions, heat, drains, then settling (fast equilibria
  and volumes together, last).
- §6: drains; settling takes a cell's equilibria again after it draws gas; a starved cell is now
  also one whose fast reaction outruns eight fillings a step; a cell is never left over-full.
- §8: the equilibrium law and its search; what follows from it.
- §18: P1 and V2 implemented. P1's tolerance restated: it was "a few quanta of the reaction",
  which does not bound the rounding of gas moved after the equilibrium is taken.
- §20: four simplifications declared (ideal steam: water boils at 100.44 °C; no nucleation
  barrier; instantaneous drains; no overflow yet).

## 0.5.0 (2026-11-15, Stage 4)

- §4.1: wall species; molar volumes derived from densities.
- §6 demo subset written out: elastic and rigid cells, the tree, expelling and drawing, the
  enthalpy carried, the sealing rule and its refusal, starved cells.
- §18: V1 and V3 implemented; T1 extended to gas; V2 moved to Stage 5 (it is about liquids).
- §20: four simplifications declared (no partial vacuum, no gas diffusion, air as N₂ and O₂,
  walls at their contents' temperature).
- Data: densities of water, CuO, malachite and CuSO₄ verified; latex added as the balloon's wall.

## 0.4.0 (2026-11-15, Stage 3)

- §4.5 rewritten: processes are applied in sequence, each from the state the last one left, each
  capped at its own equilibrium. The earlier design (every rate from one frozen state, competing
  processes scaled together) is dropped, with the reason. Heat conduction is no longer an
  exception; it is the rule.
- §4.6: activities defined (gases now, solutes in Stage 6); the affinity factor made symmetric
  and bounded, so a reverse rate cannot diverge.
- §7: the first-order law and its exact step; a reaction needs thermodynamic data for every
  species it names.
- §18: T1 extended to reactions; D3 at chemical equilibrium; M1, M2, M3 and the malachite K1
  oracle implemented.
- §20: three simplifications declared (calibrated kinetics, the reverse rate's form, sequential
  application).
- Data: malachite verified (Richardson and Brown 1974; Kiseleva et al. 1992).

## 0.3.0 (2026-11-15, Stage 2)

- §3: the core's own `exp` and `log` implemented and pinned; the per-step range limit stated.
- §4.2: reservoirs are energy accounts at a fixed temperature, holding no matter; a world has
  temperatures when built with one, and then every cell must have a temperature.
- §4.5: heat conduction is relaxed pairwise in edge order, the declared exception to
  frozen-state evaluation.
- §5: the exact two-body relaxation and its quantisation rule (no overshoot, static at rest).
- §18: T1, T2, T3, S1, D3 and the first K1 oracle implemented; T2's tolerance restated as the
  rounding bound of its own sum (was a guess); UX6 tested.
- Data: thermodynamic values for water, steam, CO₂, N₂, O₂, CuO, CuSO₄ and glass verified against
  the NIST Chemistry WebBook and ISO 3585; the other species have none yet and cannot be in a
  cell with a temperature.

## 0.2.0 (2026-11-15, Stage 1)

- §4.1–§4.3 implemented: species with atoms and molar mass derived from the formula, cells that
  store integer amounts and energy, edges with phase filters, the two primitives, refusals, the
  ledger.
- §4.2: matter and energy enter a world only as initial conditions; totals that do not fit the
  integer range are refused at build; the topology changes at a step boundary only.
- §4.3: a `Reaction` cannot be constructed unbalanced; unbalanced data fails the build.
- §18: C1–C7 implemented; C8 (a transform's change is balanced) and L0 (a refused call changes
  nothing) added; S1 moved to Stage 2 (Stage 1 has no floating-point state).

## 0.1.0 (2026-11-15, Stage 0)

- First specification: principles, numeric conventions, the conservation kernel, every system in
  full with its demo subset, the 3-stage demo, the invariant catalogue, declared simplifications.
- Implemented: the build identity and the error and log plumbing (§14).
