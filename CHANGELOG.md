# GAME_SPEC version history

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
