# Chem Game v2: Game Specification

Version 0.2.0 (history: `CHANGELOG.md`). This file is the single source of truth for the design.

Every system is specified here in full. Each system section ends with a **Demo subset** (what the
3-stage demo of §17 implements) and a **Status** line. A statement marked *(PLANNED, Stage N)* is
specified but not yet implemented; an unmarked statement in an implemented section is enforced by
a named test.

## 1. Vision and scope

A chemistry lab where materials follow mechanical, physical and chemical laws. The player handles
glassware, heat and reagents; what happens is computed, never scripted. A result shown to the
player (grams, litres, purity, colour) is a measurement of the simulated state.

The first deliverable is the 3-stage demo (§17): extract CO₂ from malachite, make a copper sulfate
solution, then recover pure water and grow copper sulfate crystals.

## 2. Principles

1. **Conservation is structural.** Matter and energy change only through two primitives that cannot
   create or destroy them (§4.3). Conservation is an integer equality, not a tolerance.
2. **Store what is conserved, derive the rest.** State is amounts and energy. Temperature, pressure,
   volume, concentration and colour are functions of state.
3. **One table of truth.** Every equilibrium (boiling, solubility, hydrate stability, reaction
   direction) is derived from one species table of formation enthalpy, entropy and heat capacity.
   Empirical curves (Antoine, solubility tables) are test oracles, never inputs.
4. **Every process is reversible in form.** A rate is a kinetic factor times an affinity factor
   that vanishes at equilibrium and changes sign past it. No process can run uphill.
5. **Invariants over examples.** Correctness is "these properties always hold", tested over random
   worlds and long runs, not a handful of hand-tuned scenes (§18).
6. **Measured, never scripted.** No message, threshold or reward is tied to a hard-coded outcome.
7. **Minimal first, each system alone.** A system is built and validated in isolation in its
   simplest honest form before it is connected to another.
8. **Homogeneous cells.** A container is one well-mixed cell. Spatial structure is expressed by
   connecting more cells, never by structure inside a cell.
9. **Closed world.** Everything matter or energy can reach is a cell: the room's air, the burner,
   the coolant, the cabinet, the outbox, the spill tray.
10. **Determinism.** The same commands give the same bits, at any frame rate and any time
    acceleration, on every platform (§3).
11. **The engine is a host.** The game is engine-free C++ behind a C interface; Godot draws and
    forwards input (§14).
12. **Every number has a source.** A physical constant or material property carries its source and
    a confidence in `docs/DATA_SOURCES.md`. A calibrated game value is labelled as calibrated.
13. **Simplifications are declared.** Where the model departs from reality the departure is listed
    in §20 with its size.

## 3. Numeric conventions and determinism

- **Amount of substance:** `int64` quanta of 1e-12 mol (range ±9.2e6 mol).
- **Energy:** `int64` quanta of 1e-9 J (range ±9.2e9 J).
- **Time:** an integer step counter; the step is 1/60 s of simulated time. Time acceleration runs
  more steps per frame; it never changes the step.
- **Derived quantities** are float64, in SI units (K, Pa, m³, kg, mol, J).
- **Rates** are computed in float64, then converted to whole quanta. The fractional remainder of
  each process is carried in that process's accumulator, which is part of the saved state.
- **No fused multiply-add, no fast-math** on any target (`native/cmake/fp.cmake`; test
  `toolchain: a*b+c is two roundings`).
- **No C-library transcendentals in the core** (`exp`, `log`, `pow` differ between libraries in the
  last bit). The core computes its own (`chem/math.hpp`, *PLANNED, Stage 3*; lint `core_no_libm`).
- **No unordered iteration and no threads inside a step.**
- **Decisions are not epsilon comparisons.** A branch that changes what happens (a phase appears,
  a reactant runs out, a vessel overflows) is taken on integer state or on the sign of a driving
  force. Tolerances exist only in the test layer and in the second-law check (§18, T2).

## 4. Conservation kernel

### 4.1 Species

A species is a (formula, phase) pair: `H2O(l)` and `H2O(g)` are different species. Phases: solid
`(s)`, liquid `(l)`, gas `(g)`, aqueous solute `(aq)`. Each species has:

- an element-count vector, derived by the build from its formula (never entered), and a charge;
- a molar mass, derived from the element counts and the standard atomic weights (never entered);
- standard formation enthalpy `Hf`, standard entropy `S°`, and molar heat capacity `Cp`
  (constant in the demo; a polynomial in the full model, §10);
- a molar volume for condensed phases;
- optical data (§12).

Molar enthalpy: `h_i(T) = Hf_i + Cp_i (T − T0)`, `T0 = 298.15 K`.
Molar entropy: `s_i(T) = S°_i + Cp_i ln(T/T0)`, plus the mixing terms of §4.6.

### 4.2 Cells and the closed world

A **cell** is a homogeneous region with one temperature and one composition. It stores only:

- `n_i`: the amount of each species (integer quanta);
- `E`: its energy (integer quanta). In an isobaric cell `E` is enthalpy `H`; in a rigid sealed cell
  it is internal energy `U` (§6).

A cell that can hold heat always contains an inert **wall species** (its glass or rubber), so its
heat capacity is positive even when it is otherwise empty.

**Temperature** is derived: `T = T0 + (H − Σ n_i Hf_i) / Σ n_i Cp_i`. Heat of reaction and latent
heat are never added by hand: converting species at fixed `H` changes `Σ n_i Hf_i`, and `T` moves.

**Reservoirs** are cells so large their state does not visibly change (the room, the coolant), or
accounts that hold only energy (a burner's fuel). The **world** is the set of all cells. Its totals
never change.

Matter and energy enter a world only as its initial conditions (`World::Builder`). A world whose
totals would not fit the integer range is refused when built; since the totals never change, they
fit forever after.

**Edges** join two different cells and say which phases may pass (gas; liquid with its solutes;
any; none, which is heat only). The set of edges changes only at a step boundary.

### 4.3 The two primitives

Cell state is private. Only two operations change it:

- **`transfer(edge, Δn, ΔE)`** moves amounts and energy along an edge between two cells. What one
  cell loses the other gains, in the same integers.
- **`transform(cell, reaction, extent)`** applies `n += ν · extent` inside one cell, with `E`
  unchanged. `ν` is the reaction's integer stoichiometric vector.

A `Reaction` value is balanced in every element and in charge: an unbalanced one cannot be
constructed (`Reaction::make` refuses it). The game's reactions are data
(`data/reactions.json`); the build embeds them through that same constructor, so an unbalanced
equation fails the build (`chem_datagen`; ctest `datagen_refuses_*`), and the generated tables
repeat the proof to the compiler as a `static_assert`.

A primitive call that cannot be applied exactly is **refused**: it names its reason and changes
nothing. Reasons: an unknown cell, edge or species; a cell that is not an end of the edge; a phase
the edge does not pass; a negative amount; more than the cell holds; a result outside the integer
range; a reaction of another species table.

Every primitive call is appended to the step's **ledger**. The state change of a step equals the
sum of its ledger entries (invariant C5).

### 4.4 Failure policy

A violated invariant aborts in Debug and sanitizer builds. In a release build it is reported
through the error sink (the Godot host shows it in the error log) and the simulation continues
(`chem/error.hpp`, `CHEM_ASSERT`). A player command that would break a structural rule is refused
with a reason; it never corrupts state.

### 4.5 The step

One step of simulated time, in this order:

1. Apply the player's commands queued for this step boundary; log them.
2. Derive per cell: `T`, volumes, molalities, activities, gas fractions.
3. Evaluate every kinetic rate from that frozen state.
4. Limit: cap each reversible process at its own equilibrium; where a species is demanded by
   several processes beyond what exists, scale all of them by the same factor; convert to quanta
   through the accumulators (an accumulator is cleared when its process is limited or its driving
   force changes sign).
5. Apply, in fixed index order: reactions, heat transfers, liquid transfers.
6. Project, visiting cells in flow order: receive inflow, solve liquid/vapour equilibrium (§8),
   expel or draw gas to satisfy the volume or pressure condition (§6).
7. Update crystal populations (§9).
8. Check invariants (every step in tests; sampled in release builds).

Because rates are evaluated from one frozen state and limited together, the result does not depend
on the order in which processes are listed.

**Stable forms.** A fast process uses the exact solution of its own linear problem over the step
instead of `rate × dt`: `n (1 − e^(−k h))` for first-order decay, exact two-body relaxation for
conduction, relaxation toward a solved equilibrium for crystal growth.

### 4.6 Entropy

World entropy is the sum over cells of `Σ n_i s_i(T)` plus ideal mixing terms: `−R Σ n_i ln y_i`
for the gas phase, and `−R ln m_i` per solute with water activity `a_w = n_w / (n_w + Σ n_solute)`
for an ideal-dilute solution. A reservoir exchanging heat `Q` at `T_res` changes by `−Q / T_res`.

The **affinity** of a process is `A = −Σ ν_i μ_i`, with `μ_i = h_i − T s_i` including the mixing
terms. Rates have the form `kinetic(T, …) × (1 − e^(−A/RT))`, so a process stops where `A = 0` and
cannot proceed where `A < 0`.

**Demo subset:** all of §4 except polynomial `Cp` and `U` cells.
**Status:** Stage 1 implements §4.1 (formula, phase, charge, derived atoms and molar mass), §4.2
(amounts, energy, edges; temperature is Stage 2), §4.3 and the ledger. §4.5 and §4.6 are
*PLANNED, Stages 2–3.*

## 5. Thermal

**Full design**

- **Conduction** along a thermal edge: `Q̇ = UA (T_a − T_b)`, where `UA` is the edge's conductance
  (wall material, thickness, wetted area). Integrated by exact two-body relaxation.
- **Convection** to a fluid reservoir: a conductance that depends on the temperature difference
  (natural convection, Rayleigh-number correlation) or on forced flow.
- **Radiation**: `Q̇ = ε σ A (T_a⁴ − T_b⁴)` between a surface and its surroundings.
- **Heat sources** are reservoirs with a temperature, never bare power sources: a flame is a hot
  reservoir coupled through a conductance set by the gas valve; an electric heater is a resistive
  element cell fed by an energy account. This keeps every heat flow second-law-safe.
- **Thermal mass of equipment**: walls, stands and thermometers are wall species or cells.
- **Spatial temperature differences** (a hot flask bottom and a cool neck) are modelled by
  splitting a vessel into connected cells.

**Demo subset:** conduction edges; a burner reservoir with a valve-scaled conductance; one combined
convective and radiative loss edge from each vessel to the room; a coolant reservoir on the
condenser.
**Status:** *PLANNED, Stage 2.*

## 6. Pressure, gas and liquid transfer

**Full design**

- **Pressure modes.** An *isobaric* cell (open to the room, or bounded by a slack membrane) stores
  enthalpy and sits at the pressure of what it is open to. A *rigid sealed* cell stores internal
  energy at fixed volume; its pressure is derived from the equation of state.
- **Equation of state.** Ideal gas, `P V = n R T`, with real-gas and vapour-liquid corrections as
  property models (§10).
- **Gas flow** along an edge is driven by the pressure difference through a conductance (orifice,
  tube, valve, capillary, frit), integrated implicitly. Composition moves with the flow (advection)
  and against concentration gradients (diffusion through the same opening).
- **Work is explicit.** A transfer carries `Δn · h(T_source)`. A flow between a `U` cell and an `H`
  cell books the flow work; a moving boundary (piston, membrane) books `P ΔV`.
- **Elastic vessels** follow a tension law: a balloon has an overpressure that depends on its
  stretch, and a burst pressure.
- **Liquids** have hydrostatic head: `P = P_gas + ρ g h`. Head drives pouring, dripping and
  siphons; gravitational potential energy is part of the ledger.
- **Liquid transfer** by pouring, pipetting, draining and overflow; a vessel filled past its
  capacity overflows into the spill cell.
- **Consequences:** sealed flasks that pressurise and burst, vacuum distillation, boiling points
  that move with pressure, heating by compression.

**Demo subset (the isobaric limit).** Every cell is at 1 atm. Gas flow is not a rate: it is the
algebraic condition that each rigid cell's gas fills exactly its free volume, solved over the tree
of gas edges rooted at an elastic or ambient cell. This is the full model's limit of infinite
conductance. A rigid cell that holds gas must have a gas path to an elastic or ambient cell; a
command that would seal one is refused. The balloon is a slack membrane with a capacity. Pouring
and dripping are kinematic (no gravitational energy).
**Status:** *PLANNED, Stage 4 (demo subset).*

## 7. Chemical kinetics

**Full design**

- A **reaction** has an integer stoichiometric vector, the cell phase it runs in, and a rate law.
- **Rate law:** `r = k(T) · Π a_j^{order_j} · geometry · (1 − e^(−A/RT))`.
  - `k(T) = A_pre · e^(−Ea/RT)` (Arrhenius). Temperature enters once.
  - Activities `a_j`: molality for solutes, partial pressure for gases, 1 for pure solids/liquids.
  - Geometry: for a reaction at a solid's surface, the surface area, derived from the solid's
    amount and particle population (shrinking-core form `n0^(1/3) n^(2/3)`).
  - The affinity factor makes every reaction reversible and stops it at equilibrium.
- **All reactions run concurrently.** There is no priority order. Competition for a reactant is
  resolved by the limiter of §4.5.
- **Mixing** scales transport-limited rates (stirring, convection), never equilibrium.
- **Catalysts and inhibitors** change `k`, continuously in their concentration.
- **No yield factors.** An imperfect yield is the result of side reactions and equilibrium, each
  balanced.
- **Unknown products** of harsh conditions (charring) are balanced pseudo-species with a declared
  composition.

**Demo subset:** malachite decomposition `Cu2CO3(OH)2(s) → 2 CuO(s) + CO2(g) + H2O(g)`;
`CuO(s) + H2SO4(aq) → CuSO4(aq) + H2O(l)` with a surface term and a stir factor.
**Status:** *PLANNED, Stage 3 (decomposition), Stage 6 (dissolution).*

## 8. Phase change

**Full design**

- A phase change is a reaction between two species of the same formula. Its equilibrium follows
  from the species table: liquid and vapour are in equilibrium where `a_w · p_sat(T) = y_w · P`,
  with `p_sat` derived from `Hf`, `S°`, `Cp` of the two species.
- **Fast phase changes** (boiling, condensation on a cold surface) are equilibrium projections:
  find the extent that restores equilibrium at fixed energy and fixed totals. It is a monotone
  one-dimensional root; dry-out and total condensation are its clamped ends.
- **Slow phase changes** (evaporation from a still surface into moving air, melting of a lump,
  sublimation) are kinetic, limited by transport through a boundary layer.
- **Solutes shift equilibria** through activity: boiling-point elevation and freezing-point
  depression are consequences, not rules.
- **Melting and freezing, sublimation, supercritical fluids** use the same construction with the
  corresponding species.

**Demo subset:** water liquid/vapour projection in the flask, the balloon and the condenser;
condensate drips to the beaker.
**Status:** *PLANNED, Stage 5.*

## 9. Solutions and crystallization

**Full design**

- **Dissolution and precipitation** are reactions between a solid and its solute. Solubility is
  where the affinity vanishes; it is derived, and compared with literature as an oracle.
- **Electrolytes** dissociate into ions with charge; activity coefficients come from §10.
  Acid/base equilibria and pH follow from the ions.
- **Hydrates** are distinct solid species. Which hydrate is stable follows from temperature and
  water activity.
- **Crystal population.** Each solid in a cell carries a particle count. Nucleation creates
  particles at a rate that rises steeply with supersaturation (and prefers surfaces, §11); growth
  and dissolution move mass. Mean size is derived from mass and count; the full model tracks a
  size distribution by moments. Slow cooling gives few large crystals, fast cooling many small.
- **Purity** of a solid product: inclusions and adhering mother liquor are amounts of other
  species in the same cell.

**Demo subset:** CuSO₄ as a neutral ideal-dilute solute with effective `Hf` and `S°` fitted to the
solubility curve; pentahydrate, trihydrate, monohydrate and anhydrous solids, converting by
dehydration reactions; particle count from nucleation, mean size derived; decanting.
**Status:** *PLANNED, Stage 6.*

## 10. Property models

**Full design.** The original prototype's thermodynamics set, as models layered on the species
table. Each must be consistent with it: an excess enthalpy is part of `h`, an activity coefficient
is part of `μ`, so conservation and the second law are untouched.

- **Volume:** ideal mixing plus Redlich-Kister excess volume (water/ethanol contraction).
- **Heat capacity:** Shomate polynomials; excess enthalpy of mixing (heat of mixing and dilution).
- **Vapour-liquid equilibrium:** activity coefficients (Margules, Van Laar), Raoult and Henry laws.
- **Electrolyte activity:** Debye-Hückel, then Pitzer for concentrated solutions.
- **Transport:** viscosity (Andrade, Grunberg-Nissan), diffusion (Stokes-Einstein), thermal
  conductivity (Filippov).
- **Surface tension** (Macleod-Sugden), **osmotic pressure**, **dielectric constant**.
- **Real gases:** virial or cubic equation of state.

**Demo subset:** none. Constant `Cp`, ideal gas, ideal-dilute solutions, additive volumes.
**Status:** specified; after the demo.

## 11. Surface effects

**Full design.** Structure at walls, expressed as extra cells and edges rather than structure
inside a cell.

- **Wall film:** a thin cell between bulk liquid and wall, where surface reactions, corrosion of
  the wall and preferential nucleation happen.
- **Deposits:** crusts, soot and residue are solids held by the wall cell; they add thermal
  resistance and change what is seen.
- **Wetting and meniscus**; **bubbles** as a transient gas population in a liquid.
- **Cleaning:** removing deposits by solvent, heat or abrasion.

**Demo subset:** none. Crystals nucleate in the bulk cell.
**Status:** specified; after the demo.

## 12. Optics

What the player sees is computed from composition. The renderer is 2D: each vessel is a body of
revolution seen from the side.

**Full design**

- **Spectral transport.** Light is a set of N wavelength samples in the visible band. N, the
  wavelengths and their weights are chosen by measurement (§12.1).
- **Absorption (Beer-Lambert):** `T(λ) = exp(−Σ ε_i(λ) c_i L)`, with concentrations from the live
  state and `L` the chord through the body of revolution at that pixel. Partial coverage at an
  edge is `T^coverage`.
- **Absorption spectra:** sums of line shapes (Gaussian and Voigt, with Doppler and pressure
  broadening for gases), a band-gap edge for solids, and solvent absorption.
- **Thermal emission:** Planck radiance times emissivity `1 − T(λ)` (Kirchhoff).
- **Scattering:** Rayleigh and Mie extinction by suspended particles and droplets; scattered light
  returns as in-medium haze and as an aura around the medium.
- **Reflection:** spectral diffuse reflectance of solids and powders under the room's illuminant.
- **Refraction:** at each interface the view ray bends by Snell's law using the analytic surface
  normal; the scene behind is sampled at the displaced position. Fresnel reflectance gives rim
  highlights. Dispersion uses a wavelength-dependent index (Cauchy), one displacement per sample.
  No ray tracing: one bend per interface, single bounce.
- **Fluorescence:** absorbed excitation, integrated over the excitation band (including
  ultraviolet), is re-emitted through an emission line shape times a quantum yield.
- **Illumination modes:** daylight (D65), ultraviolet lamp, darkness (emission only).
- **Spectrum probe:** the full-resolution spectrum under the cursor, plotted.
- **Exposure:** fixed, or adapted from a reduced-resolution luminance computed on the GPU.

**Pipeline.** The core emits a shape table (profiles of revolution) and a per-medium spectral
table. One GPU pass computes each pixel: reject by bounding box, chord length, accumulate optical
depth per sample, add emission and haze, add reflection, convert to XYZ then sRGB once. Refraction
adds one pass (the scene behind is drawn to a texture first). Nothing is read back from the GPU,
and a frame is redrawn only when the tables' revision changes. The per-pixel function is written
once, in a subset that compiles both as shader and as C++; the C++ build is the reference.

### 12.1 Choosing the spectral samples

1. **Reference:** float64 integration at 1 nm over 380–780 nm against the CIE 1931 observer and
   the D65 illuminant.
2. **Test set:** every medium the game can show, over its reachable range of concentration and
   path length; solids' reflectances; blackbody emission; overlapping media; synthetic narrow-band
   absorbers.
3. **Metric:** CIEDE2000 between reference and N-sample colour.
4. **Threshold:** maximum ΔE₀₀ ≤ 1.0 and mean ≤ 0.5 over the test set of shipped media.
5. **Decision:** the smallest N meeting the threshold; recorded with the measured table.
6. **Gate:** a test recomputes the error for the chosen samples and fails above the threshold.

**Demo subset:** absorption, thermal emission, analytic haze, reflection, daylight, fixed exposure;
then refraction and Fresnel as a separate stage.
**Status:** *PLANNED, Stage 10 (core optics), Stage 11 (refraction).*

## 13. Lab layer and interaction

**Full design.** The engine-free game layer (`native/game`, namespace `lab`).

- **Bench:** a grid on which equipment is placed. **Cabinet:** the inventory. **Outbox:** where
  products are submitted. Each is backed by cells.
- **Equipment** is data (`data/equipment.json`): a shape profile, its cells, its ports (mouth,
  outlet, jacket in/out), and what may connect to each port.
- **Connections** are rules matched by priority (flask on burner, balloon on mouth, condenser on
  mouth, beaker under outlet). A connection creates edges; supports (tripod, stand, clamps) appear
  automatically.
- **Actions** are semantic events: place, connect, detach, set valve, pour (amount or rate), stir,
  seal, decant, submit, set time acceleration, pause, step. A host maps input to events; a session
  script is a list of events with step numbers.
- **Refusals** carry a reason (a full vessel, a connection that would seal a rigid gas cell).
- **Reports** are pure functions of state: contents of a vessel by species and phase, mass,
  volume, temperature, purity, crystal count and mean size, world totals.
- **Debug panel:** per cell state, active processes and rates, heat flows, the conservation totals,
  per-system step time; pause, step, reset, export state.
- **Save and load:** the full state including accumulators; a loaded game continues identically.

**Demo subset:** burner, Erlenmeyer flask, balloon, acid bottle, stirring rod, condenser, beaker;
the actions above; the debug panel.
**Status:** *PLANNED, Stage 8 (layer), Stage 9 (Godot host).*

## 14. C API and hosts

- `native/capi/include/chem_api.h` is the game for any host. Rules: every struct starts with its
  `size`; enums are `int32` and never renumbered; the library owns all memory and returns borrowed
  views; no exception crosses the boundary, a failure is a status with `cg_last_error()`.
- **Hosts:** `chem_tool` (headless: run and replay sessions, reports, benchmarks, reference
  render) and the Godot host (`native/hosts/godot`: one node, `ChemLab`).
- **Layering:** core → lab → C API → hosts. Only `native/hosts/godot` and `engines/godot` name the
  engine (ctest `*_no_engine`); the Godot adapter includes only `chem_api.h`
  (ctest `godot_adapter_includes`); the Godot project holds at most 300 lines of GDScript outside
  tests (ctest `gdscript_budget`).

**Status:** Stage 0 implements the build identity, error and log plumbing.

## 15. Micro-view lens

**Full design.** A lens under the cursor shows a particle picture of the cell: a number of
particles per species that grows with the logarithm of its mole fraction, speed proportional to
temperature, solids on a vibrating lattice, liquids flocking, gases free; a reaction shows as
particles meeting and changing. The lens is an illustration driven by state, and changes nothing.

**Demo subset:** none. **Status:** specified; after the demo.

## 16. Substance identification

**Full design.** The player does not see composition directly. A sample is named by what has been
established about it: unknown, impure, or identified, by mole-fraction thresholds. Identification
propagates along known reactions and transfers. Instruments measure with stated uncertainty:
thermometer, manometer, balance, conductivity meter, pH meter, refractometer, spectrometer (the
spectrum probe of §12).

**Demo subset:** none; the debug panel shows true composition. **Status:** specified; after the demo.

## 17. The demo

Three stages on one bench. Quantities below are expectations computed from the data of
`docs/DATA_SOURCES.md`; the game reports what it measures.

**Stage 1: gas from malachite.** Place the burner, the flask on it, 5 g of malachite in the flask,
the balloon on the mouth. Open the burner valve. The powder decomposes appreciably from about
300 °C, turning from green to black; the balloon fills with the flask's air, CO₂ and water vapour.
Close the valve, seal and remove the balloon, submit it. Expected: about 1.0 g of CO₂; after
cooling, about 0.77 L at roughly 65 mol% CO₂ with condensed water inside; 3.6 g of CuO left.

**Stage 2: copper sulfate solution.** Let the flask cool. Pour 1 M sulfuric acid onto the CuO
(about 45 mL reacts completely with 3.6 g; the amount is the player's choice). The reaction is
slow when cold and unstirred, faster with gentle heat and stirring. The black solid disappears and
the liquid turns blue, deepening with concentration.

**Stage 3: water and crystals.** Mount the condenser and place the beaker under its outlet. Boil:
vapour condenses and drips into the beaker as pure water. When enough water is gone, close the
valve; as the flask cools the solution passes saturation and blue pentahydrate crystals grow.
Decant the remaining liquid. Submit the beaker and the flask. Heating the crystals too far, or
boiling dry, dehydrates them to pale lower hydrates and finally to the white anhydrous salt.
Excess acid from stage 2 never leaves the flask: it stays in the liquid, and is the reason to
decant.

**Done when** the outbox holds a sealed balloon containing CO₂, a beaker of pure water, and a
flask whose only contents are solid CuSO₄·5H₂O, with its measured mass, crystal count and mean
size reported, and the world totals of every element and of energy are the same integers as at the
start.

## 18. Invariant catalogue

Each invariant is a named pure check over a step trace (`chem/invariants.hpp`: `check_<ID>`,
`check_all`), with a self-test proving it fires on a corrupted trace
(`native/tests/test_invariants.cpp`), and is swept over random worlds
(`native/tests/test_sweeps.cpp`). A violation's message starts with its ID.

### Tier 1: player-facing

- **UX1. Nothing appears or vanishes.** Whatever the player does, the world's total of each element
  and of energy is unchanged. **Tested as:** `C1`, `C2`, `C3`, `C4` on every step of
  `sweeps: conservation holds on every step of every random world`.
- **UX2. Results are measurements.** A reported quantity is a pure function of the state.
  *(PLANNED, Stage 8; `R1`.)*
- **UX3. Speed does not change outcomes.** A session gives the same state at any time acceleration
  and frame rate. *(PLANNED, Stage 8; `D1`.)*
- **UX4. Colour tells the truth.** The displayed colour of a medium is the reference colour of its
  composition and path length within the §12.1 threshold. *(PLANNED, Stage 10; `O3`.)*
- **UX5. A refusal explains itself.** A refused action reports its reason and leaves the state
  untouched. *(PLANNED, Stage 8; `L1`.)*
- **UX6. Heat goes downhill.** Nothing warms above the hottest thing it touches without a reaction
  releasing heat. *(PLANNED, Stage 2; `T1`.)*

### Tier 2: systemic

The Stage column is where an invariant is implemented; rows of Stage 1 are implemented, later
rows are *PLANNED*.

| ID | Invariant | Kind | Stage |
|---|---|---|---|
| C1 | World total of each element is constant | exact | 1 |
| C2 | World mass is constant (follows from C1; summed independently, exactly, in 96 bits) | exact | 1 |
| C3 | World energy is constant | exact | 1 |
| C4 | World charge is constant | exact | 1 |
| C5 | Ledger closure: the step's primitive calls sum to its state change | exact | 1 |
| C6 | Every amount is non-negative | exact | 1 |
| C7 | A transfer runs along an open edge, between its two cells, and moves only phases the edge passes | exact | 1 |
| C8 | A transform's change is balanced in every element and in charge | exact | 1 |
| L0 | A refused primitive call leaves the state bit-identical | exact | 1 |
| T1 | Per process: flux × driving force ≥ 0 | sign-exact | 2 |
| T2 | World entropy does not decrease, within 1e-9 J/K + 1e-12 · Σ\|ΔS_cell\| per step | tolerance | 2 |
| T3 | Every thermal cell has positive heat capacity and a finite temperature above 0 K | exact | 2 |
| P1 | After projection: liquid present ⇒ vapour at saturation; absent ⇒ at or below it | tolerance | 5 |
| V1 | Rigid cell: gas volume + condensed volume = vessel volume | tolerance | 4 |
| V2 | No vessel holds more condensed volume than its capacity (excess is in the spill cell) | exact | 4 |
| V3 | No rigid cell with gas is sealed (demo subset of §6) | exact | 4 |
| X1 | Crystal count > 0 exactly when the solid's amount > 0 | exact | 6 |
| D1 | Batching invariance: N steps give the same bits however grouped into frames | exact | 8 |
| D2 | Save, load, continue equals the uninterrupted run | exact | 8 |
| D3 | An equilibrated world is bit-identical after 1e5 steps | exact | 2 |
| D4 | A session replay reproduces its recorded digest | exact | 7 |
| M1 | Shifting `Hf` by a per-element constant leaves `T` and `n` trajectories unchanged | metamorphic | 3 |
| M2 | Doubling every extensive quantity leaves intensive trajectories unchanged | metamorphic | 3 |
| M3 | Halving the step halves the error (first order) | convergence | 3 |
| K1 | Table-derived boiling point, saturation pressure, hydrate dissociation pressures, solubility and reaction-enthalpy signs agree with literature within stated tolerances | data oracle | 2–6 |
| R1 | Reports are pure functions of state | exact | 8 |
| L1 | A refused command leaves the state bit-identical | exact | 8 |
| O1 | Transmittance is within [0, 1] | exact | 10 |
| O2 | Output radiance ≤ input + emission, per sample | exact | 10 |
| O3 | N-sample colour within the §12.1 threshold of the reference | tolerance | 10 |
| O4 | GPU image matches the CPU reference within 2/255 | tolerance | 10 |
| S1 | No NaN or infinity in any output | exact | 2 |

Full-model additions (§6): mechanical equilibrium at rest, booked work equals `∫P dV`.

## 19. Performance budget

- 60 frames per second at 1920×1080.
- Simulation: one step under 5 µs for the demo's world, so 600× acceleration costs under 3 ms per
  frame. Measured by `chem_tool bench`.
- Rendering: the optical pass under 2 ms on an integrated GPU.
- Timing is evidence, recorded per stage; it is not a gate.

## 20. Declared simplifications of the demo

| Simplification | Size of the error |
|---|---|
| Every cell at 1 atm; balloon is a slack membrane | Ignores about 2 J of elastic energy against 2.35 kJ of reaction heat |
| No gravitational energy in the ledger | About 0.2 J per pour |
| One temperature per vessel | The whole flask reaches the solid's temperature in stage 1; heat losses are overstated |
| Constant heat capacities | A few percent over the demo's temperature range |
| Ideal gas; ideal-dilute solutions; CuSO₄ as a neutral solute with fitted `Hf`, `S°` | Solubility within about 6% of literature; no heat of dilution; concentrated acid outside the model |
| Additive volumes | Under 1% for the demo's solutions |
| Complete decanting | Real crystals keep a film of liquid |
| Kinetic parameters calibrated for play | Decomposition and dissolution times are plausible, not measured |
