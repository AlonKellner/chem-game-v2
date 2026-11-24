# GAME_SPEC version history

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
