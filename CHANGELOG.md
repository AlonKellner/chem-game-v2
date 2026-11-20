# GAME_SPEC version history

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
