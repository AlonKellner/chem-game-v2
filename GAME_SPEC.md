# Chem Game v2: Game Specification

Version 0.20.0 (history: `CHANGELOG.md`). This file is the single source of truth for the design.

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
  last bit). The core computes its own from `+ − × ÷` (`chem/math.hpp`: `exp`, `log`, the fdlibm
  algorithms, within one unit in the last place; lint `core_no_libm`). Their bits are pinned by
  `math: the values are pinned, bit for bit, on every platform`.
- **Range.** One step moves at most 1e18 energy quanta (1e9 J) along one edge, and an energy
  account at the end of the integer range takes no more.
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
- a molar volume for condensed phases, derived by the build from its density;
- optical data (§12).

A **wall species** is a vessel's own material (glass, rubber). It holds heat, takes no room inside
the vessel, never moves along an edge and never reacts.

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

A **reservoir** is an energy account at a fixed temperature (the room, a flame, the coolant): it
holds no matter, its energy is what it has received (negative: what it has given), and its entropy
is that energy over its temperature. A large body of matter whose temperature should drift (a
water bath) is an ordinary cell. The **world** is the set of all cells and reservoirs. Its totals
never change.

A world **has temperatures** when it is built with a reservoir or with a cell's temperature set
(the cell's energy is then the enthalpy of its amounts at that temperature, to the nearest
quantum). In such a world every cell must have a temperature at every step: thermodynamic data for
every species it holds, and a positive heat capacity (invariant T3). A species has thermodynamic
data only once its row of `docs/DATA_SOURCES.md` is verified.

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
2. Reactions with a rate: in each cell, each in the order of the reaction table (§7).
3. Heat: each edge in index order (§5).
4. Drains: liquid runs down each drain edge in index order (§6).
5. Settling: each cell, deepest in the gas tree first, is taken to its fast equilibria (§8) and,
   if rigid, made exactly full (§6). This comes last, so a step ends with every cell at
   equilibrium and full.
6. Update crystal populations (§9). *(PLANNED, Stage 6.)*
7. Check invariants (every step in tests and in Debug builds; sampled in release builds).

**Sequential application.** Each process is evaluated from the state the previous one left and
applied at once, through the two primitives. Each application:

- conserves exactly (it is a primitive call);
- runs down its own driving force as the cell is at that moment (invariant T1), so it is
  second-law-safe whatever came before it;
- never passes its own equilibrium: the amount applied is capped at the last quantum before its
  driving force would change sign, so it cannot overshoot and two processes cannot oscillate
  against each other;
- is limited to what the cell holds.

The price is a dependence on the order of application, of first order in the step: at the game's
step it amounts to a time shift of about one step (measured: 0.2% of the amount at the steepest
moment of the malachite decomposition, `kinetics: halving the step halves the error`). The orders
are fixed, so the result is deterministic.

An earlier draft evaluated every rate from one frozen start-of-step state and scaled competing
processes together. It was dropped: a process applied after another in the same cell would then
run on a driving force that no longer exists, and T1 could not be exact.

**Quantisation.** A process's amount for the step is a float64; it becomes whole quanta through
that process's carried remainder (one per edge for heat, one per cell and reaction). The remainder
is dropped when the direction changes or when the amount is capped. Where less than one quantum
separates a process from its equilibrium, nothing moves and nothing is carried: a world at rest is
static (D3).

**Stable forms.** A fast process uses the exact solution of its own linear problem over the step
instead of `rate × dt`: `n (1 − e^(−k h))` for first-order decay, exact two-body relaxation for
conduction, relaxation toward a solved equilibrium for crystal growth.

### 4.6 Entropy

Each species has an **activity** `a_i` in its cell:

- **a gas:** its partial pressure over the standard pressure, `y_i P / P°` (`y_i` its mole
  fraction among the cell's gases, `P` the cell's pressure, `P°` = 1 bar);
- **a solid:** 1;
- **a dissolved species:** from the particles it is made of.
  - An electrolyte names its ions: CuSO₄(aq) is Cu²⁺ and SO₄²⁻, H₂SO₄(aq) is 2 H⁺ and SO₄²⁻. A
    dissolved species that names none is one neutral particle of its own.
  - Each particle `j` has a molality `m_j`: the sum over every dissolved species holding it,
    counted per 55.51 mol of the cell's liquid (per kg of water). It also has an activity
    coefficient `γ_j` (§10).
  - Then `ln a = Σ_j ν_j ln(m_j γ_j)`.
  - An ion shared between species is the common-ion effect. The acid's sulfate raises the
    salt's activity, so less of the salt dissolves.
- **a liquid:** the solvent, `ln a = ln x_i − φ n_particles / n_liquid`.
  - `x_i` is its fraction of the cell's liquids, and `φ` is the solution's osmotic coefficient
    (§10).
  - With nothing dissolved in a single liquid this is 1.

`γ_j` and `φ` both come from one function, the solution's excess Gibbs energy (§10): they are its
derivatives. That keeps the solvent consistent with its solutes (the Gibbs-Duhem relation), and
only a consistent pair makes the entropy a function of state.

The solvent's form is not the mole fraction `n_w / (n_w + n_dissolved)` of an earlier draft. The
random sweeps caught that inconsistent form as falling entropy.

A dissolved species needs a liquid: a cell that holds one and no liquid is not a valid state
(invariant X2). The dynamics cannot produce it: the last of the solvent has activity zero, so it
never leaves.

World entropy is the sum over cells of `Σ n_i (S°_i + Cp_i ln(T/T0) − R ln a_i)`, plus each
reservoir's energy over its temperature.

The **affinity** of a process is `A = −Σ ν_i μ_i`, with `μ_i = h_i(T) − T s_i(T, a_i)`. It is
positive when the process is spontaneous as written, zero at equilibrium, negative when its reverse
is spontaneous. A gas or a dissolved species that is absent has activity 0: making it has
infinite affinity, consuming it is impossible.

Rates carry an **affinity factor** `f(A)`: `1 − e^(−A/RT)` for `A > 0`, `−(1 − e^(A/RT))` for
`A < 0`. It lies in [−1, 1], has the sign of `A` and vanishes with it, so a process stops at
equilibrium and runs backwards past it.

**Demo subset:** all of §4 except polynomial `Cp` and `U` cells.
**Status:** Stage 1 implements §4.1 (formula, phase, charge, derived atoms and molar mass), §4.2
(amounts, energy, edges), §4.3 and the ledger. Stage 2 adds thermodynamic data, temperature,
reservoirs, the step loop with heat conduction (`chem/sim.hpp`) and the entropy. Stage 3 adds
reactions to the step, gas activities, affinity and the affinity factor.

## 5. Thermal

**Full design**

- **Conduction** along a thermal edge: `Q̇ = UA (T_a − T_b)`, where `UA` is the edge's conductance
  in W/K (wall material, thickness, wetted area). Integrated by exact two-body relaxation: over
  one step `h`, `Q = C_eff (T_a − T_b)(1 − e^(−UA·h/C_eff))` with `C_eff = C_a C_b / (C_a + C_b)`
  (against a reservoir, `C_eff` is the cell's capacity; between two reservoirs, `Q = UA ΔT h`).
- **Quantisation.** `Q` becomes whole quanta through the edge's carried remainder. The remainder
  is dropped when the temperature difference changes sign. A transfer never exceeds the whole
  quanta of `C_eff (T_a − T_b)`, the heat that would equalise the pair, so heat never passes the
  common temperature; below one quantum of that nothing moves and nothing is carried, so a world
  at rest is static.
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
**Status:** Stage 2 implements conduction, reservoirs and conductances that change at step
boundaries. Temperature-dependent conductances (convection, radiation) are *PLANNED* with the
equipment that needs them (Stage 8).

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

**Demo subset (the isobaric limit).** Every cell is at 1 atm. A cell of matter is **elastic** (a
balloon, the open air: it takes the volume its contents need, `n_gas R T / P` plus its solids and
liquids) or **rigid** (a flask: a fixed volume).

Gas flow is not a rate: it is the algebraic condition that each rigid cell is exactly full, gas
plus solids and liquids equal to its volume. This is the full model's limit of infinite
conductance. Each step, after the reactions and the heat:

- The cells form a tree rooted at the elastic cells, breadth first along the open edges that pass
  gas, in index order. Each rigid cell, deepest first, settles with its parent.
- **Over-full:** it expels the excess fraction of its gas, at its own composition and
  temperature. Its temperature does not change, so it is then exactly full.
- **Under-full:** it draws gas of its parent's composition and temperature. The drawn gas warms or
  cools on mixing with the cell (walls included), so the amount is the positive root of
  `(n + x) R T(x) / P = free volume`, a quadratic.
- Moving gas carries its enthalpy, `n · h(T of the giving cell)`.
- Expelling changes neither the cell's temperature nor its composition, so its fast equilibria
  (§8) still hold. Drawing changes both: the equilibria are taken again and the volume again,
  until the cell draws no more.
- A parent settles after its children, and nothing a parent does afterwards changes a child, so
  one sweep leaves every cell at equilibrium and every rigid cell full.

**Drains.** A drain is a liquid edge with a direction (a condenser's outlet over a beaker). Each
step all of the upper cell's liquid, with what is dissolved in it, runs into the lower cell,
carrying its enthalpy.

**Overflow.** A world may name one elastic cell where spills go (the bench's tray). A rigid cell
that holds more solids and liquids than its volume then spills the excess of its liquid, with
what is dissolved in it, into that cell, at the end of the step: it is left full to the brim. In
a world with no such cell, a drain gives a rigid cell only what fits and the rest stays above.

**No rigid cell is ever sealed:** each must have a gas path to an elastic cell. A world that breaks
this is refused when built, and a disconnection that would break it is refused (`WouldSeal`).

**Starved cells.** A rigid cell is left under-full, and flagged `starved` in the step's trace,
when its parent holds less gas than it needs (it draws all there is), or when a fast reaction in
it consumes gas as fast as it arrives (it draws its fill at most eight times a step). In reality
that is a partial vacuum; the demo's subset has no pressure below 1 atm to give it, so the cell is
treated as at 1 atm. A cell is never left over-full.

Pouring and dripping are kinematic (no rate, no gravitational energy).
**Status:** Stage 4 implements the demo subset for gas, Stage 5 the drains, Stage 7 pouring,
Stage 8 overflow and the balloon's capacity (§13).

## 7. Chemical kinetics

**Full design**

- A **reaction** has an integer stoichiometric vector, the cell phase it runs in, and a rate law.
- **Rate law:** `r = k(T) · Π a_j^{order_j} · geometry · f(A)`, `f` the affinity factor of §4.6.
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

- **First-order law** (`data/reactions.json`, `"law": "first_order"`): the rate is proportional to
  the amount of one reactant, `r = k(T) · n_in · f(A)`. Over one step `h` the extent is the exact
  solution of that decay, `(n_in / |ν_in|)(1 − e^(−k |f| h))` in the direction of `f`: any rate is
  stable, and a reaction that could finish many times over in one step simply finishes.
- **Surface law** (`"law": "surface"`): the reaction happens at the surface of one of its solids,
  `in`, a reactant (it dissolves) or a product (it grows):
  `r = k(T) · area · a_driver · mixing · f(A)`, with `k` in mol/m²/s.
  - The **area** is that of the solid's particles as equal spheres:
    `(36π)^(1/3) N^(1/3) (n v)^(2/3)`, from its amount `n`, molar volume `v` and particle count `N`
    (§9).
  - The **driver**, if any, is a dissolved reactant whose molality the rate is proportional to
    (the acid, for an oxide dissolving in it). It is its concentration, not its activity: the
    rate counts what reaches the surface (Stage 19; with ideal solutions the two were the same).
  - **Mixing** is the cell's stirring: 1 at rest. It multiplies this law's rates, which transport
    limits, and nothing else.
  - With this area the cube root of the amount moves linearly in time, so the step is exact: a
    solid that would dissolve many times over in a step is simply gone.
- A reaction runs in a cell only where every species it names has thermodynamic data.

**Demo subset:** malachite decomposition `Cu2CO3(OH)2(s) → 2 CuO(s) + CO2(g) + H2O(g)`, first
order in malachite; `CuO(s) + H2SO4(aq) → CuSO4(aq) + H2O(l)` with a surface term and a stir
factor.
**Status:** Stage 3 implements the first-order law and the malachite decomposition, Stage 6 the
surface law, stirring and the oxide's dissolution.

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

- **The equilibrium law** (`data/reactions.json`, `"law": "equilibrium"`): each step, in each
  cell, the reaction is taken to the last quantum before its affinity would change sign, or to the
  end of what the cell holds. The search is a secant step from the two ends with a bisection
  every third, on the affinity along the reaction's own extent at fixed enthalpy.
- Several fast equilibria that share a species in one cell are taken in turn, up to eight rounds.

**What follows without being written:** water under dry air evaporates until its partial
pressure is the saturation pressure, and cools doing it. A flask of boiling water sits at the
boiling point and makes vapour exactly as fast as heat arrives. Vapour in a cool cell condenses
and warms it. A condenser settles at the temperature where its coolant takes what the steam
brings.

**Demo subset:** water liquid/vapour projection in the flask, the balloon and the condenser;
condensate drips to the beaker.
**Status:** Stage 5 implements the equilibrium law and water's liquid/vapour equilibrium. Kinetic
phase changes, melting and solute activity are *PLANNED* (solutes: Stage 6).

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

**Particles.** Each solid in a cell is in a number of particles: a powder's grains, a crop of
crystals. The count is state, saved with the rest, but not a conserved quantity:

- a solid that appears is one lump; one that is used up has none (invariant X1);
- a solid made from a solid keeps its grains (the oxide from the malachite);
- **nucleation** adds particles where the surface law makes a solid from a supersaturated
  solution: `k_n · (kg of solvent) · (S − 1)^p` per second, `S = e^(A/RT)` the supersaturation.
  Each nucleus begins as one quantum of the solid (a crystal of about 6 µm);
- the mean size is derived: the solid's volume shared equally among its particles, as spheres.

Growth and nucleation compete for the same supersaturation, so slow cooling gives few large
crystals and fast cooling many small ones, without a rule saying so.

**The four forms of the salt.**
- CuSO₄·5H₂O, CuSO₄·3H₂O, CuSO₄·H₂O and CuSO₄ each crystallise from the solution, and dissolve
  into it, by the surface law with nucleation.
- Each hydrate loses water to the next in a first-order step that gives off steam:
  5 → 3 → 1 → 0.
- Which form is stable follows from the temperature and the water's activity, and nothing else
  says so:
  - Under its saturated solution the pentahydrate gives way to the trihydrate at 93.2 °C. This is
    derived, and measured at 95.9 °C.
  - Boiled down, a solution starts crystals near 10 g of water. A trihydrate that forms above the
    transition dissolves again as the solution cools, and the pentahydrate grows.
  - Boiled dry and heated on, the hydrates turn white as their water leaves.
- Water on the white salt dissolves it and grows the blue pentahydrate.

**Demo subset:**
- CuSO₄ and H₂SO₄ as electrolytes of their ions (§4.6, §10). CuSO₄(aq)'s `Hf`, `S°` and `Cp` are
  fitted to the solubility curve.
- The four forms of the salt, above.
- A dissolved species takes its apparent molar volume.
- Particles as equal spheres, one count per solid per cell.
- Decanting comes with the equipment (*PLANNED, Stage 8*).

**Status:**
- Stage 6 implements the pentahydrate, ideal-dilute solutions of neutral solutes, and dissolved
  species that take no volume.
- Stage 19 makes the solutes electrolytes and gives them their volumes, and adds the lower
  hydrates and the stepwise dehydration. Signed off by the user on 2026-11-19 (Stage 19).

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

**Electrolytes (Pitzer).** The solution's excess Gibbs energy, per kilogram of water `w`:

`G_ex / (w R T) = f(I) + Σ_c Σ_a 2 m_c m_a (B_ca + Z C_ca)`

- `I = ½ Σ m_j z_j²` is the ionic strength, and `Z = Σ m_j |z_j|`.
- `f(I) = −A_φ (4I/b) ln(1 + b√I)`, with `A_φ = 0.392` and `b = 1.2`.
- `B_ca = β0 + β1 g(α1 √I) + β2 g(α2 √I)`, with `g(x) = 2(1 − (1 + x) e^(−x)) / x²`.
- `C_ca = C^φ / (2 √|z_c z_a|)`.
- The sum runs over the pairs of a cation and an anion that `data/solutions.json` gives
  parameters for.

The derivatives of `G_ex` are the activity coefficients: `ln γ_j = ∂(G_ex/RT)/∂n_j`. Its
derivative by the water is the solvent's share, `−φ` of §4.6. Both are taken from one function,
so they are consistent by construction.

The parameters do not depend on temperature, so neither does `G_ex / T`. That means:
- There is no excess enthalpy: no heat of dilution. The cell's temperature still follows from its
  energy as in §4.2.
- The excess is all entropy, `S_ex = −G_ex / T`. That is the `−R ln γ` of each species in §4.6's
  sum.

**Demo subset:**
- Electrolytes by the equations above, with the parameters of 25 °C at every temperature:
  - CuSO₄: Pitzer and Mayorga (1974), without their C^φ: extrapolated, it turns the water's
    activity back up beyond 65 mol/kg.
  - H₂SO₄: fitted, as fully dissociated, to Staples' (1981) recommended values.
  - No mixing terms between ions of the same sign.
- A dissolved species takes a constant volume: its apparent molar volume.
- Constant `Cp` and an ideal gas.

**Status:**
- Stage 6: ideal-dilute solutions and additive volumes.
- Stage 19: electrolytes, and dissolved volumes. Signed off by the user on 2026-11-19.

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

- **Spectral textures (§12.1).** Everything with an effect on light (a container's wall, its
  contents, a solid, a label, the wall behind the bench, a light) describes that effect by a
  spectral texture: spectral distributions mixed, point by point, by weights. The format of a
  texture is independent of the effect it is given; the effect is assigned separately.
- **Spectral transport (§12.2).** Each light's energy is cut into bands of equal energy, as the
  display weighs it; each band keeps its exact energy and colour, and carries one point that reads
  everything the light passes through at an exact wavelength. The points move every frame, and
  each pixel averages its frames until its content changes. Energy is never lost, a spectral line
  of any width is a share of a light's energy like any other, and every distribution, whatever
  its effect, is read at the same points. The range is the visible, and the ultraviolet when a
  light or a fluorescent material in the scene needs it.
- **Absorption (Beer-Lambert):** `T(λ) = exp(−D(λ) ℓ)`, where `D` is the pixel's absorption
  coefficient (a texture: for contents, `Σ ε_i(λ) c_i` with concentrations from the live state)
  and `ℓ` the path through the layer at that pixel (for a vessel, the chord through the body of
  revolution). Partial coverage at an edge is `T^coverage`.
- **Absorption spectra:** sums of line shapes (Gaussian and Voigt, in wavenumber, with Doppler and
  pressure broadening for gases), a band-gap edge for solids, and the solvent's own absorption:
  pure water's measured spectrum, weak and rising toward the red, which leaves a beaker clear and
  turns a column of metres blue, with its molecular scattering.
- **Thermal emission:** Planck radiance times emissivity `1 − T(λ)` (Kirchhoff).
- **Scattering:** Rayleigh and Mie extinction by suspended particles and droplets; scattered light
  returns as in-medium haze and as an aura around the medium.
- **Reflection:** spectral diffuse reflectance of solids, powders and printed surfaces under the
  light that reaches them.
- **Refraction:** at each interface the view ray bends by Snell's law using the analytic surface
  normal, with the index `n(λ)` a texture like any other (Cauchy for glass and water; it may vary
  over an object). The scene behind is evaluated again at the displaced point, in the same pass;
  no spectral picture of the scene is stored. Fresnel reflectance gives rim highlights.
  Dispersion: each point bends by the index at its own wavelength, so the colour fringes settle
  over frames like everything else. No ray tracing: one bend per interface, single bounce.
- **Fluorescence:** light absorbed at the excitation wavelengths (the ultraviolet included) is
  re-emitted as a light of its own: the emission line shape, holding the absorbed energy times a
  quantum yield, with its own energy bands.
- **Illumination modes:** daylight (D65), ultraviolet lamp, darkness (emission only). A mode is a
  set of light textures.
- **Spectrum probe:** the full-resolution spectrum under the cursor, computed at 1 nm on the CPU
  from the same textures, and plotted.
- **Exposure:** fixed, or adapted from a reduced-resolution luminance computed on the GPU.
- **Colours no display has** (a spectral line, a deep copper blue, anything brighter than white)
  are brought into the display's gamut keeping their lightness and hue and giving up saturation:
  chroma reduced at constant lightness and hue in OKLCh, the method of CSS Color 4, onto the
  display's own gamut. Not clipped per channel, which shifts both lightness and hue. The user
  preferred mapping to clipping in the scene test. *Built (Stage 10)* onto sRGB, which is what the
  Godot host's 2D output is; onto Display P3 where a host can output it is *PLANNED*.

**Pipeline.** The lab layer emits the scene: the bodies on the screen (profiles of revolution),
each body's layers in the order light crosses them, each layer an effect with a spectral texture;
every texture's channels as 1 nm rows and line lists, its weights (constants in the scene, or
images), and every light's energy tables. One GPU pass computes each pixel: reject by bounding
box, find the path through each layer, and for each light's bands read the layers at the band's
point (mixing each texture's channels by their weights there), add the band's colour times what
gets through, and fold the frame into the pixel's history. Refraction stays in that pass: the
scene behind a refracting surface is evaluated again at the displaced point. Nothing is read back
from the GPU, and the scene is uploaded only when it changes. The per-pixel function is written
once, in a subset that compiles both as shader and as C++; the C++ build is the reference, frame
by frame (the points' sequence is deterministic).

### 12.1 Spectral textures and effects

**A spectral texture** has channels and weights:

- **A channel** is one spectral distribution `D_c(λ)`: a continuum at 1 nm over the range
  transport needs (a measured table, broad line shapes, an edge, a Planck curve, evaluated at 1 nm
  by the lab layer), plus a list of lines (centre, width, shape, strength) for structure narrower
  than a nanometre. Lines are read analytically at an exact wavelength, so a line of any width,
  and any number of them, keeps its energy (in a light) or its effect (in an absorber). Nothing
  about a line is special-cased: a narrow line and a broad band are the same kind of thing at
  different widths.
- **A weight** says how much of a channel there is at each point of the object: a constant, a
  gradient (up the height of a vessel, across a layer, along a wall's thickness), an image (a
  picture's colour separations), or a live quantity of the simulation (a concentration).
- **At a point**, the texture's distribution is the weighted sum `D(λ) = Σ_c w_c D_c(λ)`. However
  many channels made it, each pixel has exactly one spectral distribution per texture, and that
  distribution is what takes effect.

**An effect** is what that distribution does to the light. Any texture can be given any effect;
the format does not change:

| Effect | The distribution is | At a pixel |
|---|---|---|
| absorb | an absorption coefficient, per cm | multiplies the light by `exp(−D ℓ)`, `ℓ` the path through the layer |
| reflect | a diffuse reflectance, 0 to 1 | returns `D` times the light falling on it |
| emit | a radiance | adds `D` |
| light | a light's spectral power | lights what reflects; the backdrop, the ambient light and a lamp are lights |
| scatter | an extinction coefficient, with the fraction scattered | dims like absorb, and returns the scattered part as haze and aura |
| fluoresce | a pair: an excitation and an emission distribution, with a quantum yield | moves absorbed light from the excitation bands to the emission bands |
| refract | a refractive index | bends the view ray at the object's surfaces |

**An object is a stack of layers**, each an effect with a texture, in the order light meets them.
A flask of solution is: the glass (absorb, and refract), the contents (absorb: the composition;
scatter: what is suspended; reflect: what lies on the bottom), the glass again. A container with
several layers (two liquids that do not mix, a crust on the wall, a coating, a liquid stratified
after a pour) adds layers, or gives one layer a gradient.

**The live composition is a spectral texture.** A cell's species with optical spectra are its
channels and their concentrations its weights, uniform over a homogeneous cell (§2, principle 8).
Water is a channel like any other.

**Example: a printed picture.** A label on a bottle, a poster on the wall, a page on the bench. It
is two layers:

1. **reflect**: the paper, one channel at weight 1;
2. **absorb**, crossed twice (into the paper and back out): four channels, the absorption spectra
   of the cyan, magenta, yellow and black inks; their weights are the picture's four separations,
   one image (one RGBA texture holds all four), each scaled by its ink's thickness.

At a pixel the eye receives `light(λ) · R_paper(λ) · exp(−2 Σ_c w_c(u, v) k_c(λ) t)`. The picture's
colours come from real spectra, so the same print under daylight, by a flame's light, through a
beaker of blue solution standing in front of it, or under the ultraviolet lamp (where the paper's
optical brightener glows: a third, fluorescent layer) is computed, not tinted. This is a dye print,
where inks add their absorption. A halftone print is the same format with the dots drawn in the
weight images, or with the Neugebauer primaries (paper and each overprint of the inks) as the
channels of a reflect texture, weighted by their area fractions.

**More examples.** A dense solution poured under water before it mixes: one absorb texture whose
channel weights fall with height. A tinted glass that is thicker at the base: one channel, its
weight following the thickness. A pattern of light on the wall from a window: a light texture
with an image weight.

**On the GPU** a texture is its channels, each a 1 nm row of a table texture and a line list, and
its weights: constants in the scene table, or images sampled at the pixel, four channels to an
RGBA image. At a point's wavelength the pixel's distribution is the weighted sum of the channels
there; then the effect. A texture given the light (or emit) effect also has its energy tables
(§12.2), built on the CPU when it changes. Anything drawn as flat colour (the interface, labels of
the interface) is not optics and stays in the draw list.

The original prototype gave each material a palette of spectral textures (transmission, emission,
reflection) and each shape a mask; this generalises it: any number of channels, any weights, and
the effect assigned apart from the format.

### 12.2 Spectral samples

**Energy bands with moving points.** A pixel's light is a sum over the lights that reach it: the
room's light off the wall or a print, a lamp, a glowing solid, a gas, a fluorescent re-emission.
For each light:

1. **Its energy bands.** The light's spectrum (continuum and lines), times what the display makes
   of each wavelength (the magnitudes of the display's R, G and B matching curves summed), is cut
   into `n` bands of equal energy. Each band's exact colour, the light's own XYZ over it, is known.
   A light keeps two tables over its energy, built on the CPU at whatever resolution its lines need
   (a line a hundredth of a nanometre wide is resolved): the wavelength at each share of the
   energy, and the cumulative colour up to it. From them the GPU forms the `n` bands for any `n`.
2. **One point per band.** Each band carries one point at an exact wavelength inside it, placed
   by an offset that is the pixel's own and moves every frame (a golden-ratio step, so successive
   frames fill the band evenly). At its point the band reads every layer between the light and
   the eye: absorbers (continuum and lines), reflectances, inks, a gas's own reabsorption, the
   refractive index.
3. **The frame.** The pixel's colour this frame is the sum, over the lights and their bands, of
   each band's exact colour times what gets through at its point. The light's energy is exact in
   every band of every frame; only how its layers treat the inside of a band is estimated, and the
   moving points settle that over frames.
4. **The pixel's history.** Each pixel has a content key: everything its colour depends on that
   can change (which bodies cover it and what they hold, its path through each, the mixture of
   each, a reaction's progress, what lies behind). While the key is the same the history is valid.
   When the key changes, a fixed estimate is taken again: 8 bands, each read at its middle, no
   moving points. The history holds what the moving points add to the fixed estimate, so the
   pixel shows the fixed estimate now plus its history's average. If the same bodies still cover
   the pixel and the fixed estimate has moved by at most 4 on the eye metric since the history
   began, the history is kept, aged by how far the estimate moved (half its weight per unit);
   otherwise it starts again. A body moved by a pixel, a reaction a step further on, keep their
   history; a jump, a pour, a body arriving or leaving start it again. A spectral line in a light
   is just a share of its energy (one line with 30% of the energy owns 30% of the bands, however
   narrow it is), so nothing is special-cased and nothing is lost.
5. **The budget: points by need.** A pixel's points are shared among its lights by their
   strength there, at least two each. How many points a pixel takes is its own: from the spread
   of its frames on the eye metric it knows the noise of one point, and asks for as many more as
   bring its average's expected error to 2. A pixel whose history has just started asks for 64; a
   pixel that is done asks for none, but only after at least 4 frames and 128 points, so a lucky
   early estimate cannot stop it. A pixel never takes fewer than 16 points in a frame (a smaller
   batch of energy bands converges to a visibly wrong colour where a layer changes inside a
   band). Asks above the frame's budget are scaled down by the screen's mean ask (a mipmap chain
   on the GPU, nothing read back). The budget is a ceiling, the setting that trades speed for
   accuracy on weaker hardware; a still scene costs almost nothing.
6. **Where a pixel's light jumps in wavelength.** Two things make a pixel's light jump or spike
   within a band, and each has its own answer, so that a narrow window of wavelengths is never
   left to chance:
   - **A view bent by glass, sweeping across the wall.** The landing of a pixel's rays moves with
     the wavelength, across the edges of what is drawn there. This needs no sampling: the wall's
     light is integrated exactly over the band's sweep (§12.3), whatever edges it crosses. (Until
     Stage 20 each edge crossed was found by bisection and the band cut there, at most 24 a
     pixel.)
   - **A narrow absorption line** in what the pixel looks through: the band is cut at its core, a
     width and a half each side, each part its own exact colour and its own point. A light's own
     lines need none: its energy table gives each its share.

   Without them, a pixel that sees a thin slit through a prism, a window of 0.7% of the light's
   energy, can miss it in its first frames, measure no noise and stop for good; and the fixed
   estimate, as blind, keeps a stale history when the prism turns.
   The content key holds each layer's coverage and, through glass, where the ray crosses it.

**How the choice is measured:**

1. **Reference:** float64 integration at 1 nm over 380–780 nm against the CIE 1931 observer and
   the D65 illuminant (`chem/optics.hpp`; the tables are data, `data/optics/`).
2. **What the screen shows:** reference and method are both brought into the display's gamut
   (above), encoded to 8-bit sRGB and rounded, as a display receives them, and compared there.
3. **The eye metric**, fitted to the user's own marks in blind tests on their display: CIEDE2000
   with lightness weighted double (`kL = 0.5`), chroma a third (`kC = 3`) and hue a half
   (`kH = 2`), plus a term for a grey shown as a colour or a colour as a grey,
   `8 |ln((C₂ + 8) / (C₁ + 8))|` (`C` the CIELAB chroma). Above 8 a difference is visible: 46 of
   the 48 squares the user marked "not good enough" score above it, 30 of the 2,248 unmarked do.
4. **The test set:** random spectra from eleven families (Gaussian bands in wavelength and in
   wavenumber, Lorentzian, skewed, narrow lines, edges, power laws, vibronic progressions, smooth
   noise, interference ripple, flat), a twelfth of 3 to 40 lines a twentieth of a nanometre to two
   wide, kept as lines, and mixtures of them, at random depths; through a liquid,
   under a liquid over a solid, behind a tinted wall, and printed pictures; and every colour of
   the shipped media. To tell methods apart, hard spectra are drawn more often.
5. **Acceptance:** two blind tests, judged by the user on their display. A palette test: squares
   of the 1 nm truth beside the method, running live pixel by pixel and frame by frame, the
   method's name hidden, with a control palette that is the truth on both halves; it judges
   spectral accuracy, settled and in the first frames. A scene test: a live GPU scene (a printed
   poster, vessels dragged, jumping, poured into and reacting, a sodium lamp, a glowing gas, a
   glass of thin absorption lines), truth beside method; it judges motion.
6. **Decision:** energy bands with moving points, weighted by the display's curves, each band
   read with its own colour; each pixel's history kept by its content key and the fixed estimate
   (item 4); points by need, never fewer than 16 in a frame (item 5). *Built (Stage 10).* `O3`
   holds it to the user's threshold over 1,500 random spectra: settled (16 moving points, 64
   frames) none is visible, the worst 2.3 and the mean 0.11; the first frame after a change (64
   points) is visible on 1 in 1,500. The reference's own frames hold a still scene, a dragged
   body, a pour and a slow reaction to no pixel visibly wrong for 6 frames running (`optics: the
   renderer's frames ...`), and `O4` holds the GPU's settled picture to the truth.

**False positives and false negatives with cuts** (`chem_tool study frames`, the reference's own
frames against the truth): the prism demo, opened, turned and moved: visible 24.8% to 0.00% of
pixel-frames, persistent 23.1% to 0.00%, points a pixel a frame 15.3 to 1.55, wasted 7.25 to 0.80;
a bench of glass vessels dragged, jumping, poured into and reacting: none visible either way (the
content key's geometry took it from 1.23%, with runs of 92 frames, to none); sixteen glasses of
random hard spectra with narrow lines: none visible either way, the longest visible run 2 frames
to 0, wasted 0.79 to 0.86 (the lines' cuts cost a point each). The scene test replayed with its
dragged ball a refracting glass sphere of water: behind the ball, 16 points everywhere shows 8.4%
visible; the decided method, with or without cuts, none; water and glass bend too little for a
view to cross an edge between wavelengths, so the cuts change nothing there.

**What the user's eye decided** (rounds 4 to 8; details in `docs/TDD_10_OPTICS.md`):

- **Points that move every frame beat fixed bands.** With 8, 16 or 32 points and the last 16
  frames averaged, the user marked no square of a hard palette not good enough; fixed bands of
  the earlier kind (24) had four. At 32 points the user could not pick a worst square.
- **Points must follow each light's energy, not the eye.** Spread by the eye under daylight, 16
  points flicker on a lamp of spectral lines: 30 of 144 squares marked, "just terrible". Cut by
  energy, the same 16 left none marked; with the display's curves as the weight, settled squares
  average 0.51 on the eye metric, against 0.66 with the eye's X+Y+Z.
- **Bands that keep each band's energy are also good** (24 of them: none marked), which is the
  energy bands' inheritance; the moving points add the exact reading of thin lines and sharp
  absorbers inside a band.
- **Resetting a pixel's history on any change looked best in motion** ("movement is smooth,
  settling is perfect"). History carried with moving objects by a tolerance, and pixels frozen
  once settled, smeared and kept grain that never faded; they are not used.
- **History kept by the fixed estimate, points by need, looked as good or better at a twelfth of
  the work** (round 8: "most of the time I can't tell the difference, and when subtle differences
  do happen, they are quickly fixed"). Each point weighted by its own colour (unbiased at any
  number of points) looked "maybe slightly worse"; the band's colour is kept.

**False positives and false negatives** (round 8's scene replayed on the CPU against the truth,
every second pixel, 600 frames). A false positive is a point spent on a pixel whose colour did not
change and was already right (eye metric 4 or less); a false negative is a pixel shown visibly
wrong (above 8), persistent if for 6 frames or more.

| Method | Points a pixel a frame | Of them wasted | Visible | Persistent |
|---|---|---|---|---|
| 16 points everywhere, reset on any change | 16.0 | 98% | 0.3% (moving bodies 11.7%) | 0.0% |
| Eagerness, a mean of 16 | 9.0 | 93% | 0.1% | 0.0% |
| Eagerness, a mean of 4 | 3.3 | 72% | 6.4% (a reacting tube 56%) | 5.9% |
| Decided (item 4, item 5) | 1.3 | 61% | 0.0% (after a change 1.0%) | 0.0% |

The eagerness map failed on three counts: converged pixels kept spending (the time term), any
motion over a pixel reset it though its colour barely moved (93% of resets discarded a right
colour), and small batches converged to wrong colours (8 points: visible on 3.5% of pixels for
ever, 19.5% under a lamp of lines; 16: none, worst 3.1). The decided method's remaining waste is
the fixed estimate taken again where a key changed but the colour did not, the cost of checking.
Sharing a young pixel's history with its neighbours made moving bodies worse (1.2% to 3.5%) and
is not used.

**What was measured before** (the eye metric, 3,000 to 20,000 spectra):

- **Fixed bases are no better than bands.** At 24 numbers a spectrum, the share of visible errors
  is 0.77% for bands, 1.07% for piecewise-linear depth with knots adapted to the scene, 1.37% for
  a mixture of Gaussians, 1.47% for PCA, 1.73% for a cosine (Fourier) series. Any basis smooths a
  sharp feature of the optical depth before the exponential, and that is the error. Gaussians stay
  the form spectra are written in (absorption bands are Gaussian in wavenumber).
- **Two methods were more accurate and are not taken,** because they do not fit §12.1: an exact
  colour-against-path-length table for each body (visible on 0.02% of spectra) settles a body's
  colour as RGB, so nothing spectral can follow; learned virtual samples (correlated-k: 16 of them
  beat 96 bands on a library of 67 spectra) have no wavelength, so neither refraction nor
  fluorescence nor the ultraviolet can use them, and a texture's values would depend on its effect.
- **Bands with three strengths each** (a Gauss quadrature of the band's absorption) are accurate
  (16 bands: 0.04% visible), but the quadrature belongs to one spectrum and does not survive
  mixing channels per pixel.
- **Plain bands** need about 96 to reach the same quality on hard spectra (0.07% of random spectra
  visible, 1.5% of hard ones); 24 leave 1.0% and 17%. Adapting the cuts to a body's spectra
  roughly halves the error.

**The first measurement** (Stage 10, superseded by the method above): CIEDE2000 on unclipped
colours, over the demo's 110 colours. A band carried the average of each absorber's coefficient,
weighted by what the eye and the illuminant make of each nanometre:

| Bands | Cuts | Worst ΔE₀₀ | Mean |
|---|---|---|---|
| 8 | equal weight | 4.9 | 1.0 |
| 16 | equal weight | 2.5 | 0.46 |
| 14 | fitted to the test set | 0.74 | 0.24 |
| **24** | **equal weight** | **0.80** | **0.19** |
| 32 | equal weight | 0.49 | 0.11 |

The game rendered with these 24 bands (`chem::optics::game_bands`) until the decision above was
built, held to worst ΔE₀₀ ≤ 1.0 and mean ≤ 0.5 over the demo's colours (the first `O3`). The
bands stay in the core for `chem_tool study spectrum`.

### 12.3 The optical pass of the demo

**Light and backdrop.** The room is lit by D65. Behind the cabinet, the bench and the outbox is a
wall that reflects evenly (radiance 0.73 of the room's white); what is in the glass is seen by
the light that comes through it from that wall. The room's white is 100 cd/m², which is what a
glowing body is measured against.

**A body** is a vessel or a balloon, as the screen places it, in painting order:

- **A vessel is a cylinder** of its volume and of the proportions it is drawn with, seen from the
  side. At a pixel the path through it is the chord `2R·sqrt(1 − (x/R)²)`, in real centimetres.
- **Its liquid** fills it to the height its condensed volume reaches, and dims the light behind
  it by `exp(−Σ ε_i(λ) c_i L)` at each point's wavelength, with `c_i` the live concentration of
  each liquid or dissolved species that has an absorption spectrum: water itself, as measured,
  and the solutes, each with its narrow lines if it has any.
- **Its solids at rest** are a heap at a powder's angle of repose (35°, packing 0.5): a cone until
  it reaches the wall. The heap is seen by the room's light: its reflectance (a mixture's by
  mass), and, under liquid, the liquid there and back, one chord.
- **Its solids stirred** are suspended: each particle shades its own cross-section twice over
  (extinction efficiency 2), so the liquid's extinction is `2 N (π d²/4) / V`; light it removes is
  replaced by the room's light off the solid, through half a chord of the liquid.
- **A hot solid glows:** Planck's law times its emissivity `1 − reflectance`, integrated at 1 nm
  in the lab layer, in units of the room's white.
- **A balloon is a ball** of its volume. Its rubber is a wall crossed twice, aslant near the rim
  (`2t / cos θ`, at most four reference thicknesses), and thinner the further it is stretched
  (`t ∝ 1/R²`). What condensed inside lies at the bottom.
- **An edge covers a pixel by degrees:** depths are multiplied by the coverage (`T^coverage`), a
  solid is mixed in by it.

**The scene** (`lab/optics_scene.hpp`, `cg_ui_optics`): 98 vec4s (the room, and 6 for each of at
most 16 bodies: its rectangle, form, haze and wall, glow, and identity, a signature of its
contents and its lines); each body's three spectra at 1 nm (its liquid's optical depth per
centimetre, its solids' reflectance, its wall's depth); up to 256 narrow lines; the room light's
energy table (4,097 entries). **The renderer** (`native/game/shader/optics_pixel.glsl`, copied into
the Godot project) is the method of §12.2 for one pixel: its path, what gets through at a
wavelength, a batch of points, the fixed estimate, the frame's step and what it shows. The Godot
host runs it as a compute shader over the whole screen each frame, each pixel's state in four
float images on the GPU, the screen's total ask summed there for the next frame's budget; nothing
is read back. **The reference** is the same file compiled as C++ in float64 (`optics_through`,
`optics_truth`, `optics_pixel`, `optics_picture`, `OpticsFrames`, `optics_compare`).

**The picture is in three layers:** the draw list's first part (the room), the optical pass, the
draw list's second part (glass, labels, flames, a liquid's surface line, the readout and panels).

**Refraction and Fresnel** (Stage 11). A vessel is a glass cylinder around what it holds, and
light bends at each of its surfaces:

- **What is behind is drawn.** The room behind the bodies is the lit wall and what is drawn on it
  (the bench's grid, the panels' edges: the draw list's first part), each a flat grey of its own
  radiance. The scene carries, for each body, the drawn shapes its bent rays can reach, and a
  point reads the wall where its ray lands. Outside a body the same shapes are the flat drawing,
  so a vessel's rim meets the wall with no seam where nothing bends.
- **The glass.** Each vessel's wall is borosilicate glass of the wall's mass: its thickness is the
  mass over the density (2.23 g/cm³) and the area of the side and the floor. Its index is the
  glass's `n(λ)` (a Cauchy fit to Schott's table for BOROFLOAT 33); the liquid's is water's (the
  Sellmeier formula of Daimon and Masumura at 20 °C); above the liquid, air (1). Both are spectra
  at 1 nm like any other, so a point bends by the index at its own wavelength (dispersion).
- **The bend.** The camera looks straight in; the cylinder's axis is upright, so a ray bends only
  across it. Through concentric cylinders the ray keeps its distance `d` from the axis times the
  index (`n d` is constant), so at a surface of radius `r` between indices `n₁` outside and `n₂`
  inside it meets the normal at `asin(d / (n₁ r))` and leaves it at `asin(d / (n₂ r))`. The ray's
  total turn `δ` is twice the sum of those differences over the surfaces it crosses going in, and
  it leaves on a line at distance `d` from the axis, turned by `δ`: on the wall, `D` behind the
  axis, it lands at `(d − D sin δ) / cos δ` from it. `D` is the distance from the vessels to the
  wall behind them (calibrated: 20 cm). A ray whose `d / n` misses an inner surface stays in the
  glass.
- **The chord.** Inside a medium the ray's distance from the axis is `d / n`, so the liquid's
  chord is `2 sqrt(r² − (d / n)²)`, at each point's own wavelength: the path the light really
  takes, which Beer-Lambert reads.
- **Fresnel.** At each surface the unpolarised reflectance of the two angles (`R`, from Fresnel's
  equations; at normal incidence `((n₁ − n₂)/(n₁ + n₂))²`) is turned away; what gets through all
  four surfaces is `T = Π (1 − R)`. What is turned away shows the room in front of the bench,
  whose radiance is calibrated (0.3 of the room's white): a vessel's rims, where light meets the
  glass aslant, show more of the room and less of the wall, as glass on a bench does.
- **What it does not bend:** a heap of solid (its surface is seen as it lies), the balloon (thin
  rubber around gas), anything up and down (the liquid's surface, the floor of the vessel).

**A pixel is the light of all its rays** (Stage 20). A pixel is not a point: it gathers the light
of every ray through its area, and its colour is their average (a box filter).
- **The tracer** runs on the CPU, in float64, and never in a pixel's loop.
  - A body's shape is a list of surface patches: each a quadric or a plane (a cylinder, a sphere,
    a cone, a face), with its bounds and the medium on each side. A ray meets a patch by a
    quadratic, so there is no shape the tracer does not know, only lists it has not been given.
  - At each surface the ray refracts by vector Snell, losing what Fresnel reflects; where it
    cannot pass it is turned back, all of it.
  - It returns where the ray lands on the wall, what its surfaces let through, its liquid's chord
    along the ray's own path, and its route.
  - This stage gives it the upright cylinder (a glass shell around a core) and the prism.
- **A refraction map for each body.** Where a ray lands depends on the body (its shape, its glass,
  its indices, the wall's distance), the ray's place across it and the wavelength. It does not
  depend on where the body stands, what it holds or how full it is. So it is traced once and
  kept:
  - **Samples across the body:** four a pixel, on the screen's own quarter pixels, so a pixel's
    parts begin and end on samples wherever the body stands. Each whole pixel has its level: in
    how many equal parts (1, 2 or 4) it must be read for the map to be straight within each.
    Straight means:
    - the middle ray lands within a fiftieth of a pixel of the screen of where the ends put it
      (the error on the wall, over how fast the landing moves across the body there);
    - the slopes of a part's two halves are alike within 8%: a thin line's image is as bright as
      the slope where it lands is small, so its light is true within 4%;
    - what the surfaces pass is within 0.005, and the chord's square within 1%.
  - **A route's change** (a rim, a ray turned back, a fold) is in a part of the finest kind, and
    the map says how far along the part it is (found by halving).
  - **Pieces in wavelength.** A ray is kept at the four nodes of each piece (its ends and its
    thirds), the pieces even in 1/λ², which glass and water disperse nearly straight in. Within a
    piece a ray is the cubic through its nodes: where it lands, what passes, its chord's square.
    There are as many pieces as keep that cubic within the same fiftieth of a pixel (or a
    twentieth of a nanometre, where a ray leaves a face nearly along it and its landing runs away
    with the wavelength), and as keep a route's boundary from moving further than that between
    two nodes; at most 64. A vessel's whole spectrum is one piece: one cell is a ray at every
    wavelength (about 3,000 rays and 0.6 ms for a vessel's map). A ray whose route changes within
    a piece is its nearest node's ray there.
  - **Two cores:** liquid and air, for the rows of the screen under and over the liquid's surface.
  - **Past the body's rim the map is the identity:** a ray that misses the body goes straight.
  - A map is traced again only when its body's shape, glass or indices change, or the fraction of
    a pixel its axis stands at: a prism being turned, a vessel being dragged.
- **Brightness is not made or lost by bending.** Glass that squeezes a larger patch of wall into
  a pixel spreads the pixel's rays over more directions in step: the radiance along a ray is
  conserved (the radiance theorem). A uniform wall seen through any clear shape is as bright as
  it is (`O7`). What a pixel's rays gather does change: squeezed, a pixel sees more of the
  wall's fine lines, and each line keeps its light: summed over the pixels, it is what the rays
  that land on it bring (`O8`), over a wider image where the glass magnifies. Light
  concentrated onto a surface (caustics) is another matter, and is not built.
- **The wall is a table.** The edges of everything drawn on it cut it into cells, each of one
  radiance, the topmost shape's as on the screen. Each row of pixels has its profile across, with
  its running integral and that integral's integral, so the wall's light over any interval, and
  over an interval sweeping, is exact: however many shapes, however large, however they overlap.
- **The footprint.** A pixel is its parts. The rays of one land in an interval of the wall, a
  pixel high, and as the wavelength crosses a band that interval sweeps along the wall.
  - The wall's light is integrated exactly over the interval and over the sweep: the mean, along
    each boundary ray's sweep, of the row's integral (the footprint's width taken as its mean
    across the step). No edge is missed, in place or along the sweep.
  - A band is swept in straight steps, even in wavelength: as many as keep the sweep within a
    fiftieth of a pixel of the true landing (the map says how fast each pixel's landing bends in
    wavelength), at most 16. Each step is weighed by its share of the band's energy: a band is
    even in energy, not in wavelength.
  - What is smooth in wavelength (the liquids' absorption, what the surfaces let through) is read
    once a step, at the step's own point (the band's moving point, §12.2). What passes is the mean
    of the part's two boundary rays'; the liquid absorbs as the part's rays do together, in closed
    form, their chords' square taken straight across the part (as it is, exactly, where a chord
    rises as a root at a rim).
  - Where the route changes within a part (a quarter of a pixel), each side is its own ray's, by
    the share the map gives it. What passes and the chord are carried on to the change from the
    ray beyond (the chord by its square: at a vessel's rim it falls to nothing); the wall is read
    where the side's own ray lands.
  - **One light, two readings.** What a pass costs on the GPU follows how much there is to do in a
    band, whether it is done or not (measured: a branch never taken cost as much as one always
    taken). So a pixel through a map of one piece, which is every vessel's, is read by a reading
    with nothing of what it never needs; a pixel through any other map (a prism's) by the general
    one. Where a pixel's rays go one way the two are the same arithmetic, and a test holds them
    to the same light.
  - A row across a liquid's surface is its two cores' lights, each by its share of the pixel; a
    row across a vessel's top or floor is the bent view and the straight one, likewise.
  - The bodies are upright, so a ray is bent only across: the footprint's height is the pixel's.
    A body that bends up and down too (a sphere, later) has a map with rows of the screen as
    well, and a footprint cut by the wall's rows into such intervals.
  - Each item is drawn at its own scale, so the screen is not one world in centimetres: a ray bent
    by one body is not traced into another, and the first refracting body a pixel looks through
    bends it.
- **The truth** for the GPU (`O4`) and the two invariants is independent of the renderer: a
  brute-force average of a fine grid of sub-pixel rays, each traced exactly, every band read.

**Demo subset:** absorption, reflection, thermal emission, analytic haze, daylight, fixed exposure
(§12.3); every layer a texture with constant weights (the live composition, the powders by mass,
the rubber, the lit wall); pure water's absorption; then refraction and Fresnel as a separate stage.
**Status:** Stage 10 implements §12.3 by the method of §12.2: spectral textures (§12.1) with
constant weights (the live composition, the powders by mass, the rubber, the lit wall) and line
lists, the room's light in energy bands with moving points, each pixel's history kept by its
content key and points by need, gamut mapping onto sRGB, and pure water as measured. The glow of a
hot solid is its exact colour, added outside the history. Stage 11 adds refraction and Fresnel
through each vessel's glass and liquid, with dispersion, the bench's drawing seen bent, and the
prism demo: a prism of flint glass (N-SF11) traced through its flat faces, before a slit of white
light in a dark room (`make prism`). Stage 20 makes each pixel the light of all its rays, through a
general tracer (*in progress*). The
rest of the full design (lights besides the room's, image and gradient weights,
Voigt lines, band gaps, fluorescence, ultraviolet and dark modes, the spectrum probe, Rayleigh and
Mie auras, light cast through glass onto a surface (caustics, a rainbow on a wall),
adapted exposure, Display P3 output) is specified and not built.

## 13. Lab layer and interaction

The engine-free game layer (`native/game`, namespace `lab`): a bench of equipment over one closed
world, driven by commands.

**The bench** is data (`data/benches/*.json`, `chem-bench/2`): the room, and the items with what
they hold. Every item of equipment exists from the start, as cells of the one world; no equipment
is created or destroyed in play (the signal parts of §13.3 hold no matter, and come from a
drawer). Items:

- **vessel:** a rigid cell with a volume, a wall and a heat loss to the room. It starts open to
  the room's air and full of it. One with a `coolant` is a condenser.
- **balloon:** an elastic cell with a wall, and a capacity.
- **burner:** a reservoir at its flame's temperature, and its conductance with the valve open.
- **the tray:** the bench's own, under everything: where what overflows goes (§6).

**Places.** The bench is a grid of squares (28 × 15 in the demo's benches).
- **Footprints.** An item on the bench stands on a footprint of squares: the room for its body,
  what stands on its mouth starts above it, and a vessel's stand over a burner. Footprints are by
  kind and volume, or the bench's own `size`:
  - a vessel under 100 mL, 2 × 6; a larger one, 3 × 7;
  - a condenser, 2 × 5;
  - a balloon, 4 × 5;
  - a burner, 3 × 3;
  - a signal part, 1 × 1.

  Footprints never overlap; `at` is the footprint's top-left square.
- **Places.** An item is in the cabinet, on the bench, on a vessel's mouth, under a vessel (a
  burner), or in the outbox.
- **Setting down.** The lab sets an item down "beside" another (after `unheat`, `detach`, a
  burner left by `submit`) on the first free footprint: rows by how far their foot is from the
  other's foot, then columns by how far from it, the left first.
- **Using it.** Equipment is used on the bench: a command on an item still in the cabinet, or
  handed in, is refused.
- **The bench file** gives each item its square (`at`), or leaves it in the cabinet. A
  `chem-bench/1` file is a grid of cells, one item to a cell: each footprint is one square.

**Commands** are JSON objects with a `do`. They are everything a player can do; a host maps
input to them, and a session script is a list of them.

| `do` | Fields | Effect |
|---|---|---|
| `place` | `item`, `at` [column, row] | from the cabinet onto the bench, or along it (a signal part too); refused where something stands, off the grid, or while the item is joined to another |
| `move` | `items`, `by` [columns, rows] | several items and parts along the bench together, by one offset: all of them or none, with the reason (one would be off the grid, or land on something left behind) |
| `stow` | `item` | back into the cabinet; a signal part back to the drawer |
| `heat` | `burner`, `vessel` | puts the burner under the vessel, from the bench or straight from the cabinet (its valve as it is) |
| `unheat` | `burner` | takes it away, to stand beside the vessel |
| `valve` | `burner`, `to` (0 to 1) | sets the flame: the conductance to the vessel is `to` × the burner's |
| `stir` | `vessel`, `on` | stirring: the vessel's mixing is 3 while on (§7) |
| `pour` | `from`, `to`, `ml` or `all` | moves liquid, with what is dissolved in it in proportion, and its enthalpy |
| `tip` | `from`, `to`, `species`, `g` or `all` | moves a solid, with its particles in proportion |
| `attach` | `item`, `to` | puts a balloon or a condenser on a vessel's mouth (from the bench or straight from the cabinet): a gas connection, and the mouth closed to the room |
| `detach` | `item` | takes it off and sets it down beside the vessel: the vessel's mouth open to the room again; a balloon is then sealed |
| `under` | `item`, `of` | stands a vessel under another's outlet: a drain (§6). The drain comes off when either vessel is placed elsewhere, moved apart from the other, stowed or handed in (at a step boundary); moved together, they stay joined |
| `submit` | `item`, `slot` | hands it in, to a slot of the outbox (§13.5); it must not be on or hold anything. It leaves an outlet it stood under. A burner under it stays on the bench. With no `slot`, the first free slot without a goal, else the first free one; refused when the slot holds something, or none is free |
| `goal` | `slot`, `name`, `terms` | the player's own goal for a slot (§13.5), made or changed; refused on a game goal's slot |
| `ungoal` | `slot` | takes the player's goal off a slot; refused for a game goal |
| `wait` | `seconds` | advances time |
| `wait_until` | `item`, a condition, `timeout` | advances until `species` is `below_g` or `above_g`, or the item is `below_c` or `above_c`; or until the alarm `rings`; fails at the timeout |
| `part` | `kind`, `at` | a timer, monitor or alarm out of the drawer onto a free square (§13.3) |
| `scrap` | `part` | back to the drawer, with its wires and rigs |
| `set` | `part`, a monitor's `item`, `what`, `measure`, `compare`, `value` or `values`; a timer's `seconds` | its settings |
| `wire`, `unwire` | `from`, `to` | a part's signal into another's input |
| `rig`, `unrig` | `from`, `item`, `control`, `on` and `off`, or `change` | a part's signal onto a control: a vessel's `stir`, a burner's `valve`, a timer's `reset` |
| `reset` | `timer` | back to its preset, by hand |
| `ack` | `alarm`, or none for all | answers a ringing alarm |

- **Refusals.** A command that cannot be done is refused with its reason, and changes nothing: an
  unknown item, the wrong kind of item, a mouth that is taken, more than a vessel holds, liquid
  that does not fit, a connection that would seal a vessel (§6). One exception: a command that
  changes connections takes effect at a step boundary, so it first takes the lab to the end of a
  step in progress (at most one step, 1/60 s).
- **Pouring and tipping are instantaneous**, along a handling connection that passes liquid and
  solid and never gas. They leave a step in progress, so a change of connections after one takes
  the lab to that step's end first.
- **A balloon bursts** when its contents pass its capacity: it comes off the vessel it is on
  (whose mouth opens to the room), its gas is the room's, and it cannot be used again. The report
  lists it among the lab's events.
- **Save and load.** A lab saves as text (`chem-save/1`): every amount, energy and particle
  count, the connections, the processes' carried remainders, where each item stands, and whether
  a step is in progress. Numbers that are not integers are saved as their bits. A loaded lab
  continues exactly as the saved one would have (invariant D2).
- **Invariants.** With checking on (the default) every invariant of §18 is checked on every
  step. A failure stops the lab: it reports the invariant and takes no more commands.
- **Reports** are functions of state: per item its temperature, the volume of its gas and of its
  solids and liquids, and each species' amount, mass, particle count and size; and the world's
  totals of every element, of mass and of energy, with whether they are what they were at the
  start.
- **The digest** is the SHA-256 of the whole state: every amount, energy and particle count, the
  processes' carried remainders, the connections, the stirring, the step; and, once there is a
  part, the signals: every part's settings and state, the wires, the rigs. Where things stand is
  not in it. Equal digests are equal states.
- **Sessions** (`data/sessions/*.json`, `chem-session/1`): a bench and a script. A session's
  **record** (`bench/sessions/*.jsonl`) is the digest after each command; replaying the session
  must reproduce it (invariant D4).

### 13.1 The screen

The screen is engine-free too (`lab/ui.hpp`): a lab, a pointer, and a draw list. A host forwards
its pointer, calls a frame, and paints the list. Everything a player does on the screen becomes a
lab command here, so it is the same on every host and is tested without one.

**Layout** (1920 × 1080): the cabinet on the left; the bench's grid of squares in the middle; the
outbox under it, and the drawer of signal parts beside the outbox; on the right the readout; on
top the clock.

**Gestures.** Everything is a drag or a click:

| Gesture | Command |
|---|---|
| drag an item to a free cell of the bench | `place` |
| drag an item to the cabinet | `stow` |
| drag a burner onto a vessel | `heat` |
| drag a balloon or a condenser onto a vessel | `attach` |
| drag a vessel onto a condenser | `under` |
| drag a vessel onto another vessel | opens the pour panel: a slider for how much liquid, `pour`; a button for each solid, `tip` |
| drag an item onto a slot of the outbox | `submit` to that slot (§13.5) |
| drag something off what it is on, or a burner from under a vessel | `detach` or `unheat`, then whatever the drop means |
| drag the slider beside a burner | `valve` |
| click a vessel's stir button | `stir` |
| drag a selection along the bench | `move` |
| drag a part out of the drawer | `part`, and its settings open (§13.3) |
| drag from a part's port onto a part, a vessel, a burner | `wire`, or the rig's settings: `rig` |

**The selection.**
- A click on an item, without moving it, chooses it. A click with Shift, Ctrl or Cmd adds an item
  to the selection, or takes it away.
- A rectangle drawn from an empty square chooses everything on the bench it touches; with one of
  those keys held, it adds to the selection.
- A click on an empty square, or Escape, lets go of the selection.
- The readout shows the last item chosen.
- A dragged item of a selection of several carries all of them, one `move` (all of them or none).
  Dropped on the cabinet, the equipment is stowed and the parts go back to the drawer.
- The selection is the screen's, not the lab's. A tutorial takes one item at a time.

A refusal's reason is shown in the readout, and nothing has changed. A click outside the pour
panel closes it.

**The clock:** pause; 1×, 10×, 60×, 600×; reset. A frame advances the lab by its real time times
the speed, at most twenty simulated seconds a frame: beyond that the clock runs slow, never the
picture.

**Invariants on the screen.** One step of every frame is checked against every invariant; "check
every step" checks them all. The readout's first line is the conservation laws: every element,
mass and energy unchanged since the bench was opened, or not.

**The readout** shows what the pointer is on: its temperature, the volume of its gas and of its
solids and liquids, and each species' mass, particle count and size; then the lab's events and
the last refusal.

**Drawing.** What is in a vessel, and a balloon, is the optical pass of §12.3. The draw list is
flat and in two parts, one under that pass and one over it: the room and its grid under; glass as
lines, a liquid's surface as a line (a clear liquid is otherwise invisible until Stage 11's
refraction), a flame by its valve, labels, the readout and panels over.

**Full design, not yet built:** adjacency deciding what can join what; supports that appear with
the glassware; pouring at a rate; a debug panel of the processes' rates and heat flows,
single-stepping and exporting the state.

**Demo subset:** the benches of `data/benches/` (`demo.json` for the scripted session, `lab.json`
for play: everything in the cabinet), the commands and the screen above.
**Status:** Stage 7 implements the bench, the commands, reports, the digest and sessions; Stage 8
places, the tray, bursting and save/load; Stage 9 the screen and the Godot host; Stage 13 the grid
of squares and footprints, the selection and `move`.

### 13.2 The tutorial

A tutorial (`lab/tutorial.hpp`; data in `data/tutorials/`, `chem-tutorial/1`) is a play of the lab
in steps. It reads the lab and never commands it: everything is still done by the player's own
gestures, and a run with a tutorial is the same lab as one without.

- **A step** has a part (the demo's stage), a title, what to do, why (the chemistry), its checks,
  and the gestures it asks for, named as the screen's `locate` names things.
- **A check** is a condition on the lab's view with a text: an item's place, what it is on or
  under, what drains into what, handed in, stirred, burst; its valve, temperature, liquid volume;
  a species' mass, or its share of the solids and liquids; what alone it holds; the screen's speed;
  the world's totals unchanged.
  - Any of §13.3's quantities, compared: `{"quantity": {"what", "measure"}, "compare", "value"
    | "values"}`. Purity, a species' share (%), is one.
  - The signals: a part (`part`: its kind, what a monitor watches and how, whether an alarm rang),
    a wire (`wire`: from and to, by id or kind), a rig (`rig`: from, item, control, its
    settings).
  - The selection, and the part editor while it is open (`selected`, `editor`).
  - The goals (§13.5): an item's `slot`; a goal met (`goal`: its id); every game goal met
    (`goals_met`).

  Conditions combine with `all`, `any` and `not`. They are the shared module's
  (`lab/conditions.hpp`), as signals read them. A check is marked
  `wait` when the lab is waited on for it rather than a gesture; every check of a step that asks
  for no gesture is waited on. One that names an item the bench
  does not have, or a species the tables do not, is refused when the tutorial is opened.
- **One step at a time.** While a tutorial runs, the screen takes only the current step's
  gestures: a drag of the item it names, let go where it goes (onto the item it names, into a
  free square for a square, into the outbox or the cabinet); a click on the button, valve or
  slider it names. Anything else is refused, with what the step asks for, and changes nothing.
  While the step waits on the lab, no gesture is taken. The clock's buttons and reset are always
  the player's. Once the run is complete or has failed, the screen is the player's again.
- **The clock waits for the player.** While the step asks for a gesture, the lab's time does not
  pass; it runs only while every open check is waited on. The screen then advances the lab a
  second at a time and stops in the second the wait ends, at any speed: a player is never too
  slow for the lab, and a 600x clock cannot boil a flask dry while the player reaches for the
  valve.
- **Passing.** After every frame and every gesture, the current step is passed when all its checks
  hold, and so on while the next ones hold too: a step done early is passed when it is reached.
  The state decides, not the order the gestures came in.
- **Failing.** A tutorial lists its failures, each a condition with what went wrong; the first that
  holds ends the run, as does an invariant stopping the lab. A failed run stays failed until the
  screen's reset, which starts the lab and the tutorial again.
- **Completing.** Once every step is passed, its goal is checked: if every goal check holds the
  tutorial is complete, and it reports what was made, measured from the state; if one does not,
  the run has failed, and the card says which.
- **On the screen** the card stands above the readout: the part, the step's number and a progress
  bar, the title, what to do and why, and each check with a box, ticked when it holds, with the
  value it waits on ("now 1.509 g"). A pulsing outline points at the last of the step's gestures
  that is on the screen now (for a drag, both ends and an arrow), while a check that is not waited
  on is still open; with the item in hand, only where it goes.

**Demo subset:** `data/tutorials/demo.json` over `data/benches/lab.json`: the demo in 35 steps; Stage 13 teaches the signals at the boil-down: a monitor on the flask's water, rigged to close the burner's valve and wired to an alarm.
Its failures: the balloon burst; acid poured before the malachite has broken down (in liquid it
never gets hot enough); the crystals overheated to a lower hydrate or the anhydrous salt; the
balloon handed in without the CO₂, the beaker with more than water, the flask with its liquor or
without its crystals; anything else handed in. Its goal is the bench's three game goals met
(§13.5, which carry §17's "done when"), and the totals unchanged; it hands each item in onto its
goal's slot. The Godot host opens
it by default (`tutorial_path`).
**Status:** Stage 12. `tutorial: followed with its own gestures, step by step, it completes the
demo` plays every step's gestures through the screen and waits for the lab. `tutorial: played as a
person plays it, slowly, at the speed it asks for, it completes` plays them as a person does: a
second and a half before each gesture, the pointer moved across the screen frame by frame, a
slider taken at its knob, a drop into the outbox over what was handed in before, the clock at
600x.

### 13.3 Signals

Signals let the player act on the lab while its clock runs fast. They are **parts** on the bench
that watch the lab and act on it (`lab/signals.hpp`):

- **A timer** counts down.
- **A monitor** compares a quantity.
- **An alarm** stops the clock and shows the player what set it off.

Parts are **wired** to each other and **rigged** to the lab's controls. Parts belong to the lab:
they are saved, loaded and digested with it. A bench with no part digests as it did before them,
so the sessions' records are unchanged.

- **Parts** come from an unlimited drawer, and go back to it.
  - Commands: `part {kind, at}` (ids `timer1`, `monitor1`, … never used again); `scrap`, or
    `stow` (the drawer).
  - Each stands on one square of the bench. Parts hold no matter and are not cells of the world.
  - They are placed, and moved with a selection, like equipment.
- **Synchronous evaluation**, at the end of every step:
  - Every part reads its inputs' signals as they were at the end of the step before, and all are
    updated together.
  - So any wiring is well defined and deterministic: loops, and a part wired into itself,
    included.
  - Every link, a wire or a rig, takes one step (1/60 s), so a chain of N parts answers in N steps.
  - The order the parts were made in changes nothing.
- **Gating.**
  - A part with no inputs is enabled.
  - With inputs, a part is enabled while any of them is on: inputs in parallel are OR.
  - A part signals while its own condition holds and it is enabled. A chain of parts is therefore
    AND.
- **A monitor** (`set {part, item, what, measure, compare, value | values}`):
  - Its own condition is a comparison (§13.2's quantities and comparisons, purity included) of one
    item's quantity, read every step.
  - "Reaches" holds when the value lies between its last two readings, ends included.
  - A monitor not yet set signals nothing.
- **A timer** (`set {part, seconds}`):
  - Its preset is shown as hh:mm:ss.mmm and kept in steps; setting it resets it.
  - It counts down one step per step while the lab runs.
  - At zero its own condition holds, and stays held until the timer is reset.
  - Its **reset** control sets it back to its preset. Reset can be done by hand (`reset {timer}`)
    or by a rig, once per event.
- **An alarm** has inputs and no output.
  - It rings in the step its input turns on, and rings until it is answered (`ack`, or the screen
    playing on).
  - An alarm with no input never rings.
  - `advance` may stop at the end of a step in which an alarm started ringing. A session's `wait`
    does not stop there; `wait_until {rings}` waits for one.
- **Wires**: `wire {from, to}` and `unwire`. A wire from an alarm is refused, and so is a
  duplicate.
- **Rigs**: `rig {from, item, control, on, off}` or `rig {from, item, control, change}`, and
  `unrig`.
  - A rig is an input to a control:
    - a vessel's `stir`: true to stir, false to stop;
    - a burner's `valve`: 0 to 1;
    - a timer's `reset`: true.
  - It gives settings either for its source turning **on** and turning **off**, or for any
    **change**, never both. A setting left out, or null, leaves the control as it is.
  - **Events set state, and the last event wins.**
    - When a rig's source changes, as it was at the end of the step before, and the rig has a
      setting for that change, the setting is applied at the end of the step.
    - It is applied through the same paths as the player's own commands; there is no new physics
      path.
    - The control keeps that state until the next event from any input.
  - **Any number of rigs per control.** When several act in one step, they act in the order they
    were made, so the newest wins.
  - **The player is another input.** A `stir`, `valve` or `reset` command sets the control in the
    same way, and every rig stays attached.
  - The lab keeps each control's last setting, who made it (a part, or the player) and when.
- **Invariants:** G1 and G2 (§18).
- **On the screen:**
  - **Making and placing.**
    - The drawer stands beside the outbox. A part dragged out of it is made on the square where it
      is let go, and its settings open.
    - A part shows its name, its state (a timer's time left, a monitor's comparison), a lamp lit
      while it signals, and its port on its right.
    - A drag from a port is a wire:
      - onto a monitor or an alarm: a wire into it;
      - onto a timer: a choice, a wire into its input or a rig onto its reset;
      - onto a vessel or a burner: a rig, whose settings open.
    - Wires and rigs are drawn as lines, lit while their source signals.
    - Dropped on equipment or in the outbox, a part is refused, with the reason.
  - **The editor** (a panel like the pour panel):
    - a monitor's: the item, the quantity (g, count, size or purity of a species), the comparison,
      and values typed or nudged at three scales;
    - a timer's: its preset, typed or nudged by a minute, ten seconds or a second;
    - a rig's: its form, and each setting, a choice or a valve's per cent.

    Enter sets it, escape cancels, and the clock waits while it is open.
  - **The readout** for a part: its state, its inputs, its wires and rigs, each with a button that
    takes it off, and "edit" and "reset". For equipment, each control's last setting, and who made
    it.
  - **A ring** pauses the clock. It outlines, pulsing:
    - the alarm;
    - the parts upstream that set it off, and the items their monitors watch;
    - the wires between them.

    It selects the watched item, and names the chain in the message: "alarm1 rang: monitor1,
    flask: H2O(l) (g) below 10.000 g (now 9.996 g)". Playing on answers it.
- **A tutorial** restricts every gesture of the signals to its step's own:
  - the drags out of the drawer and from ports;
  - every button of the editor, its cancel and Escape included, so a part is never left half set;
  - the keys: text is taken only while it is still the start of what the step asks to type.

  When "set" is the step's last gesture, the command is first tried on a copy of the lab, and
  refused unless the step's checks would then hold, with what the step still asks for. A wrong
  setting cannot be set. In the editor the pointer outlines the first of the step's gestures that
  is not yet done: a choice until it is chosen, a value box until it holds the text the step
  types, and "set" only then.

- **Circuits.** A selection's signal parts on the bench, circuits included, collapse into one
  compact part: `circuit {parts}`, ids `circuit1`, …
  - **Where it goes.** The new circuit stands on the free square nearest the parts' top-left:
    rows by distance, then columns, the left first. It is usually the first part's own square,
    freed.
  - **Inside it.** The parts keep their layout on the circuit's own grid: their extent, at least
    6 × 4. They stand on no square of the bench.
  - **Signals.** A circuit signals nothing itself and takes no wire or rig. What is in it signals,
    and is wired and rigged, exactly as it would out of it. Wires and rigs that cross its edge are
    kept.
  - **The digest.** Where parts are kept is not the world: a lab with a circuit has the digest of
    the same parts loose, step for step.
  - **Working inside.**
    - `part {kind, at, in}` makes a part inside a circuit.
    - `place` moves a part within its circuit's grid.
    - `move` takes a circuit as one square, with all it holds; a part in a circuit moves with it.
    - `scrap` takes a circuit away with all it holds, at every depth, with their wires and rigs.
  - **Giving back.** `uncircuit {circuit}` puts its parts back on the bench, as they lie in it:
    - from its square, or the nearest place where all of them fit, in the same order;
    - refused, with the reason, where nowhere has room;
    - a circuit inside another is given back once the outer one is;
    - what was in it at depth stays in the circuits it was in.
  - **Nesting.** Circuits hold circuits, to any depth.
  - **On the screen:**
    - While the selection holds signal parts on the bench, the drawer offers "create circuit".
    - On the bench a circuit is a square that names it and how many parts it holds. Its lamp is lit
      while something in it signals out of it, and it is outlined when an alarm in it rings.
    - Wires and rigs to what is inside a closed circuit are drawn from its square.
    - The readout offers "open" and "give back". Open, the circuit's inside is a panel, over the
      outbox where it fits so the bench stays in sight. In it, its parts are moved, made out of the
      drawer, set, wired and rigged, also out to the bench; "up" opens the circuit around it, and
      "close" closes it. A part dragged out of the panel stays in it, with the reason.

**Demo subset:** all of the above; signed off by the user on 2026-11-17 (circuits: Stage 14, signed off the same day; the notice part and the writer: Stage 16, §13.4).
**Full design, not yet built:**
- controllers: a monitor with a target and PID gains whose output is a level, set on an analog
  control or as PWM on a binary one.

**Status:** Stage 13; circuits Stage 14.

### 13.4 Notices

The lab says what happened, as **notices**: the step, a level, the item it is about, and what it
says. The player may silence any level.

- **Levels:** detail, event, warning, danger.
- **What the lab notices.** The detectors read every quarter of a lab-second, and write a notice
  where the state changed since their last reading:
  - **A reaction with a rate law starting or stopping in an item:** when it moves more than a
    ten-millionth of a mole a reading, or none.
    - Its start's level and text are the reaction's own, from `data/reactions.json` (`notice`):
      an event for the malachite breaking down, a warning for the crystals losing their water.
    - Its stop is a detail.
    - Reactions of the equilibrium law (evaporation) are not noticed.
  - **A solid, liquid or dissolved species appearing in an item** (at least a milligram), or used
    up there (under a tenth of one): an event. Not when a hand moved it: an item poured or tipped
    from or into since the last reading writes no such notice.
  - **Boiling:** an item with liquid water at 99.5 °C or more boils, an event. It stops boiling
    below 99 °C, an event, or it boils dry, a warning.
  - **A balloon 80 % full or more:** a warning, once until it is below 70 % again.
  - **A spill into the tray:** a warning.
- **What happens at once:**
  - a balloon bursting, a danger;
  - an invariant failing, a danger;
  - a timer reaching zero, a detail;
  - a rig setting a control, a detail;
  - an alarm ringing, an event;
  - a writer's notice, at its level.
- **History, not state.** The notices, and what the detectors last saw, are saved and loaded:
  a loaded lab writes the same notices as one that was not saved. They are not in the digest. What
  is there when the bench opens is not news. The lab keeps the last 2000.
- **The notice part** (§13.3, out of the drawer) signals for a step when the lab writes a notice
  at or above its level, about its item if it names one: `set {part, level, item}`. Wired to an
  alarm, it stops the clock on, say, every warning.
- **The writer** (§13.3, out of the drawer) writes a notice of its own when its input turns on:
  `set {part, level, says, item}`.
  - Its words are the player's, at most 80 bytes; with none, "writer1 was signalled".
  - It is about its item, or about itself when it names none, so a notice part can hear one writer
    alone.
  - Like an alarm, it takes inputs and sends no signal; with no input it never writes. While its
    input stays on it writes once; it writes again when the input turns off and on.
  - Its notice is written at the end of the step its input turned on, and heard by notice parts
    in the step after: one more link.
  - So the notices are a bus: writers post to it, notice parts hear from it, and the lab posts
    its own notices beside theirs.
- **On the screen:**
  - The newest notice of the levels heard stands beside the clock.
  - "Notices" opens a panel over the readout: a switch for each level, and every notice heard,
    newest first, with its time and level.
  - A silenced level is still kept, and is shown again when its switch is turned back on. Details
    start silenced.
  - The notices are the player's under a tutorial too, like the clock.

**Demo subset:** all of the above; signed off by the user on 2026-11-18 (Stage 16).

### 13.5 Goals

The outbox is **six slots**, numbered 1 to 6, and a slot may hold a **goal**: what should be handed in there. A goal
does not judge the player: it says what is wanted, and whether what is there meets it now.

- **A goal** has an id, a name, its slot, and its **terms**: each a quantity of whatever is handed
  in there (§13.3's quantities without an item: `{"quantity": {"what", "measure"}, "compare",
  "value" | "values"}`), compared. It is **met** while a slot holds an item and every term holds
  of it, measured live: what is handed in still changes (it cools, it reacts).
- **The game's goals** come with the bench (`"goals"`, the same form, each with its `id`). They
  are locked: the player cannot change or remove them. A tutorial's goal can name them.
- **The player's goals** are made, changed and taken off with `goal {slot, name, terms}` and
  `ungoal {slot}`, on any slot without a game goal (ids `goal1`, … never used again).
- **Handing in** is to a slot (§13 `submit`): one item to a slot. A drop onto a slot on the screen
  hands it in there. A submit with no slot keeps the scripts as they were: the first free slot
  without a goal.
- **Notices** (§13.4): a goal becoming met is an event, about the goal's id ("goal met: the
  balloon holds carbon dioxide"); one no longer met, a warning. A notice part can hear one goal by
  its id.
- **Monitors** read a goal as a quantity: `{"item": <goal id>, "what": "met"}`, 1 while it is met
  and 0 while it is not (a command; on the screen a notice part hears a goal by its id).
- **Conditions** (§13.2) read them: `{"goal": <id>}` holds while that goal is met;
  `{"goals_met": true}` while every game goal is.
- **History, not state.** Goals and which slot holds what are saved and loaded, and are not in
  the digest; the sessions' records are unchanged.
- **On the screen** each slot shows its goal: the name, each term with its value now, and what was
  handed in there. Its state is seen at a glance: met, a green band and border and "met"; an item
  there that falls short, amber and "not met"; nothing there yet, "open". A slot without a game goal has "+ goal" (or
  "edit"), which opens the goal editor: a name, typed, and up to three terms, each a quantity
  (any species, or the temperature, the liquid, the gas), a comparison and a value.

**Demo subset:** the interactive bench's three game goals, in slots 1 to 3:
- `co2`, "carbon dioxide in the balloon": CO₂ at least 0.5 g;
- `water`, "pure water in the beaker": water at least 20 g, and at least 99.99 % of the solids
  and liquids;
- `crystals`, "copper sulfate crystals, 99.9 % pure": CuSO₄·5H₂O at least 5 g, and at least
  99.9 % of the solids and liquids.

The tutorial's goal is those three met, and the world's totals unchanged; its hand-in failures stay,
since what is handed in cannot be taken back. Signed off by the user on 2026-11-18 (Stage 17).

### 13.6 The debug panel

What the lab is doing, and why, for the item selected: the state's truth, the last step's flows,
and each reaction's account of itself (`Lab::debug`).

- **The last step's flows** (`Simulation::flows`, the core): what the step's two primitives moved,
  summed by process: reactions, heat, drains, settling (gas in and out, boiling and condensing),
  overflow.
  - For each edge and process: the energy and the mass moved from one end to the other.
  - For each cell and reaction (those with a rate law, then those of the equilibrium law): the
    extent applied.
  - They are kept whether the step was checked or not, are not state, and equal the ledger's sums
    (with T1's signs: heat from the hotter cell to the colder).
- **For the item** (`Lab::debug(item)`):
  - its temperature and heat capacity;
  - **what it holds:** each species' amount and mass; its mole fraction in its own mixture (of the
    gas, of the solution); a solute's molality; its activity;
  - **its flows** in the last step, each with what is at the edge's other end and the process: heat
    (W) and matter (g/s), positive into the item;
  - **each reaction** that names a species it holds: the equation, why it happens (the reaction's
    `explain` in `data/reactions.json`), its rate in the last step (mol/s), its affinity now
    (kJ/mol), and **what limits it**.
- **What limits a reaction**, worked out from its kinetics and the state, the first that applies:
  - no temperature to weigh it at;
  - **at equilibrium** (its affinity under a thousandth of RT);
  - a species it would consume is used up: "no CuO(s) left" (running backwards: "would run
    backwards, but there is no CuO(s)");
  - no rate law: it does not run on its own;
  - the equilibrium law: held at its equilibrium every step;
  - **running backwards**, with its rate;
  - the first-order law: **too cold** while its time constant 1/(k |f|) is over a minute, with the
    temperature from which it is fast ("too cold: 3e-9 mol/s at 120 °C, fast from about 357 °C");
    otherwise its half-life;
  - the surface law, using up a solid: its surface (the area of its particles), the activity of
    its driver, and its stirring;
  - the surface law, growing a solid: the supersaturation S = e^(A/RT), and new particles a second
    while none has started; undersaturated, the solid dissolves.
- **On the screen:** "debug" by the clock opens the panel over the readout, for the item selected.
  - "step" advances the lab exactly one step (1/60 s), the clock paused; under a tutorial, only
    while the tutorial lets the clock run.
  - "export" hands the host the lab's save (`chem-save/1`, `cg_ui_take_export`); the Godot host
    writes it to `user://saves/`. Loaded, it continues with the same digests (D2).

**Demo subset:** all of the above. Signed off by the user on 2026-11-18 (Stage 18).

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

- **The lab through the C API:** `cg_lab_open` (a bench, as text: the library reads no files),
  `cg_lab_command` (one command; `CG_ERR_REFUSED` or `CG_ERR_INVARIANT` with the reason in
  `cg_last_error`), `cg_lab_advance`, `cg_lab_set_checking`, `cg_lab_step`, `cg_lab_report`,
  `cg_lab_digest`.
- **`chem_tool run SESSION`** plays a session through the C API with every invariant checked,
  prints what was measured, and records (`--record`) or checks (`--check`) its digests.
  **`chem_tool bench SESSION`** plays it unchecked and times it.

- **The screen through the C API:** `cg_ui_open`, `cg_ui_pointer`, `cg_ui_frame`, `cg_ui_draw`
  (a draw list of rectangles, lines, polygons, circles and text, as flat arrays), `cg_ui_message`,
  `cg_ui_report`, `cg_ui_locate` (where a named thing is on the screen, for tests and scripts),
  `cg_ui_tutor` and `cg_ui_tutorial` (a tutorial over the screen, §13.2, and how far it is).
- **The optical pass through the C API:** `cg_ui_optics` (the scene of §12.3: the table, each
  body's spectra, the lines, the room light's energy table, with one revision),
  `cg_ui_optics_compare` (a host's settled picture of the pass against the truth: `O4`).
- **The Godot host** (`native/hosts/godot`: `ChemLab`, a `Node2D`): it reads the bench file,
  forwards the mouse, calls a frame in `_process`, paints the draw list's first part in `_draw`,
  the optical pass by a compute shader over the screen (`native/hosts/godot/optics_compute.cpp`:
  the per-pixel source of §12.3 in a wrapper that binds the scene's buffers and the pixels' state),
  shown by a child `TextureRect`, and the list's second part on another child. The pass needs a
  `RenderingDevice` (Forward+ or Mobile); headless it is off and says why. Its budget a pixel is a
  property (`optics_budget`, 16). No game logic, and no GDScript in play.

**Status:** Stage 0 implements the build identity, error and log plumbing; Stage 1 the tables;
Stage 7 the lab and the session tools; Stage 9 the screen and the Godot host; Stage 10 the
optical pass.

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

Three stages on one bench (`data/benches/demo.json`). The script of `data/sessions/demo.json`
plays them as a player would; the numbers below are what that run measures
(`chem_tool run data/sessions/demo.json`), not targets.

**Stage 1: gas from malachite.** The burner under the flask, the jar's 5 g of malachite tipped
in, the balloon on the mouth, the valve open. The powder decomposes from about 300 °C, green to
black; the balloon takes the flask's air, then CO₂ and water vapour. After 229 s less than 5 mg
of malachite is left: the balloon is taken off, which seals it, and handed in; the valve is
closed. *Measured:* the balloon holds 0.896 g of CO₂ (64.8% of its gas) in 756 mL, with 0.354 g
of water condensed inside. The other 0.099 g of CO₂ stayed in the flask or left through its open
mouth afterwards.

**Stage 2: copper sulfate solution.** Once the flask is below 30 °C, 50 mL of the 1 M sulfuric
acid is poured onto the oxide (about 45 mL would react it all), stirred, over a low flame. The
black solid is gone in 227 s and the liquid is blue.

**Stage 3: water and crystals.** The condenser on the flask, the beaker under its outlet, the
valve open. The solution boils; vapour condenses and runs into the beaker. With 12 g of water
left the valve is closed: with less, the boiling liquor would be saturated above the 93.2 °C
transition, and pale trihydrate would come out first (§9). As the flask cools the solution passes
saturation and blue pentahydrate crystals grow. The liquor is poured off into the waste beaker;
the beaker and the flask are handed in. *Measured (Stage 19):* 38.91 g of water in the beaker and
nothing else but air; 9.274 g of CuSO₄·5H₂O in the flask, in 1,002,347 crystals of 0.20 mm, 82% of
the copper. The flask's solids are 99.977% pentahydrate: the rest is the 2 mg of malachite that
never decomposed.

Heating the crystals too far, or boiling dry, turns them to the white anhydrous salt. Acid
beyond what the oxide needs stays in the liquor, and is the reason to pour it off.

**Done when** the outbox holds a sealed balloon containing CO₂, a beaker of pure water, and a
flask whose only solids and liquids are CuSO₄·5H₂O, with its measured mass, crystal count and
size reported, and the world's totals of every element and of energy are the same integers as at
the start. `lab: the demo session reproduces its golden record, and its results are what was
measured` asserts each of these.

**Status:** Stage 7: done headlessly, through the C API. Stage 9: playable in the Godot host,
in flat colours; `ui: the three stages of the demo, played with the pointer alone` plays it
through the screen. Stages 10 and 11: seen by light, refracted. Stage 12: checked end to end with
the final renderer, headless, through the screen and on the GPU, and made a tutorial (§13.2)
that teaches it step by step and checks each step (`docs/TDD_12_DEMO.md`); signed off by the user
on 2026-11-17.

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
  **Tested as:** `R1`: the report is computed from the state alone (`Lab::report() const`), and
  the demo's results are asserted from it.
- **UX3. Speed does not change outcomes.** A session gives the same state at any time acceleration
  and frame rate. **Tested as:** `D1`, `lab: the same commands give the same state, however the
  steps are grouped and whether or not they are checked`.
- **UX4. Colour tells the truth.** The displayed colour of a medium is the reference colour of its
  composition and path length within the §12.2 threshold. **Tested as:** `O3`, and `UX4: a
  solution's colour is that of its concentration and of the path through it` (the reference
  renderer's truth against the 1 nm colour, at five concentrations and two chords), the
  renderer's frames settling on the truth, with `O4` holding the GPU's settled picture to it.
- **UX5. A refusal explains itself.** A refused action reports its reason and leaves the state
  untouched. **Tested as:** `L1`, `lab: a refused command says why and changes nothing` (19
  refusals).
- **UX6. Heat goes downhill.** Nothing warms above the hottest thing it touches without a reaction
  releasing heat. **Tested as:** `T1`, and the extremes of temperature on every step of
  `sweeps: heat flows downhill and entropy grows on every step of every random thermal world`.

### Tier 2: systemic

- **UX7. The demo is playable with the pointer alone.** Every action of §17 is a drag or a click.
  **Tested as:** `ui: the three stages of the demo, played with the pointer alone`.

The Stage column is where an invariant is implemented; every row is implemented.

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
| T1 | Every process runs down its own driving force, as the cells were when it was applied: heat from the hotter cell to the colder (Stage 2); a reaction forward only while its affinity is positive, backward only while it is negative (Stage 3); gas only out of a rigid cell that is over-full or into one that is under-full (Stage 4) | sign-exact | 2–4 |
| T2 | World entropy does not decrease: over the cells that changed, Σ(S_after − S_before) ≥ −(1e-9 J/K + 4e-16 · Σ\|S\|), the bound of the float64 rounding in the sum | tolerance | 2 |
| T3 | In a world with temperatures every cell has one: data for every species it holds, a positive heat capacity, T > 0 | exact | 2 |
| P1 | Every cell is at its fast equilibria at the end of the step. For each reaction of the equilibrium law: nothing is left to go in the direction of its affinity (liquid absent: vapour at or below saturation), or the affinity is within what rounding leaves: R T Σ\|ν_i\| N (1/n_i + 1/n_gas) over the reaction's gases, n_i each one's quanta and n_gas the cell's (Stage 19: before, 2 N R T / n_gas, which a minor gas exceeds: the steam over a cold condenser drawing dry air), or N quanta of the reaction spend it (N = the table's species + 1, the quanta that moving gas rounds by) | tolerance | 5, 19 |
| V1 | A rigid cell is exactly full: gas + solids and liquids = its volume, within the volume of one quantum of each gas species plus 1e-9 of the cell's. Under-full only where the trace marks it starved | tolerance | 4 |
| V2 | No rigid cell holds more solids and liquids than its volume. In a world with a spill cell the excess liquid is there (Stage 8) | exact | 5, 8 |
| V3 | No rigid cell is sealed: each has a path, along open gas edges, to an elastic cell (demo subset of §6) | exact | 4 |
| X1 | A solid has particles exactly when it is there: count > 0 where its amount is > 0, 0 where it is 0; nothing else has particles | exact | 6 |
| X2 | A cell that holds a dissolved species holds a liquid to hold it | exact | 6 |
| D1 | Batching invariance: N steps give the same digest however grouped into frames, and whether or not they are checked and traced | exact | 7 |
| D2 | Save, load, continue equals the uninterrupted run: the demo saved at six points, one in the middle of a step, reproduces the rest of its record | exact | 8 |
| D3 | A world at rest is bit-identical, carried remainders included: thermal rest after 1e5 steps (Stage 2), chemical equilibrium after 5e4 (Stage 3) | exact | 2–3 |
| D4 | A session replay reproduces its record: the digest of the whole state after every command (`bench/sessions/demo.jsonl`; ctest `demo_session`) | exact | 7 |
| M1 | Shifting `Hf` by a per-element constant leaves `T` and `n` trajectories unchanged | metamorphic | 3 |
| M2 | Doubling every extensive quantity leaves intensive trajectories unchanged | metamorphic | 3 |
| M3 | Halving the step halves the error (first order), within 25% | convergence | 3 |
| K1 | Table-derived values agree with literature within stated tolerances. Stage 2: water's heat of vaporisation at 25 °C. Stage 3: malachite's decomposition is endothermic and becomes spontaneous between 25 and 127 °C. Stage 5: water's saturation pressure within 2% and heat of vaporisation within 0.6% of NIST from 25 to 100 °C; boiling point within 0.5 K. Stage 6: copper sulfate's solubility within 1.5% of the handbook from 0 to 100 °C (the dissolved salt's two numbers were fitted to those five points); water's ebullioscopic constant within 3%. Stage 19: the solubility within 3% of the handbook from 0 to 100 °C (three numbers fitted to the eleven points from 0 to 90 °C); copper sulfate's mean activity coefficient at saturation (1.41 mol/kg, 25 °C) within 4% of the two measurements; the acid's osmotic coefficient and water activity within 2% of Staples to 5 mol/kg; the pentahydrate giving way to the trihydrate under its saturated solution within 3 K of the measured 95.9 °C; the acid lowering the salt's solubility; a neutral solute's ebullioscopic constant still within 3% | data oracle | 2–6, 19 |
| R1 | Reports are pure functions of state | exact | 7 |
| L1 | A refused command leaves the lab as it was, apart from the at most one step that takes it to a step boundary | exact | 7 |
| O1 | Transmittance is within [0, 1]: in the renderer's scene every optical depth is at least 0, every reflectance within [0, 1], every line has a width | exact | 10 |
| O2 | No wavelength of a pixel comes out brighter than the brighter of the backdrop and the room's white; emission is carried apart | exact | 10 |
| O3 | Random spectra of every shape (§12.2's test set, 1,500, deterministic) are shown within the eye's threshold: settled, none visible and a mean of at most 0.25; the first frame after a change, at most 1 in 200 visible. It fires on batches of 2 points | tolerance | 10 |
| O4 | The GPU's settled picture of the optical pass is the truth: wherever a body covers at least a fiftieth of a pixel, no pixel is visibly different (eye metric above 8). It needs a GPU, so it is checked by `make screenshots`, not by the commit gate | tolerance | 10 |
| O5 | Where every index is 1, refraction changes nothing: each pixel's light is the average of its straight rays, with nothing reflected, at a rim too, where some miss the vessel (Stage 20: until then, its centre's ray, exactly); it fires on water's own index | tolerance (0.002 of the room's white against the truth's rays; 0.0002 against the straight path in the vessel's middle half) | 11, 20 |
| O6 | Fresnel conserves light: at each surface what is reflected and what gets through sum to what arrives, so in a room as bright in front as behind, clear glass and water change nothing; and no wavelength comes out brighter than the brightest of the wall, its drawn shapes and the room in front (`check_O2`) | tolerance (the scene's float32 radiances) | 11 |
| O7 | A uniform wall seen through any clear geometry is the wall's radiance: with absorption and reflection turned off, every pixel a body covers shows the wall's radiance (the radiance theorem). It fires on a renderer that weights the wall by the area its rays land on | tolerance (float32) | 20 |
| O8 | A thin line keeps its light: what a line drawn on the wall adds to a row of pixels seen through a vessel, summed, is what its rays bring (its radiance step, times what each ray's path lets through, over the width of screen whose rays land on it: wider where the glass magnifies), wherever it lands; the ends of the line's image are found by halving, on rays traced exactly. It fires on centre sampling, which loses a line between two pixels' centres or counts it whole | tolerance (1% of the line's light) | 20 |
| G1 | Every control of the lab is as its last setting left it, a rig's or the player's (§13.3): a vessel's stirring, a burner's valve | exact | 13 |
| G2 | Every part's signal is its own condition while its inputs, as they were at the end of the step before, let it; an alarm sends none | exact | 13 |
| S1 | No NaN or infinity in any output | exact | 2 |

Full-model additions (§6): mechanical equilibrium at rest, booked work equals `∫P dV`.

## 19. Performance budget

- 60 frames per second at 1920×1080.
- Simulation: one step under 5 µs for the demo's world, so 600× acceleration costs under 3 ms per
  frame. Measured by `chem_tool study` (Stage 2: 0.27 µs for one cell and a reservoir; Stage 3:
  0.43 µs for one cell, two reservoirs and the reaction table; Stage 4: 0.67 µs for a flask, a
  balloon and two reservoirs; Stage 5: 2.5 µs for the distillation's four cells and three
  reservoirs; trace built, checks off).
- **Measured** (`chem_tool bench data/sessions/demo.json`): the whole demo. Stage 7: eight cells
  and three reservoirs, 4.5 µs a step. Stage 8: the gas tree cached per topology and a cell's
  thermal state derived once, 3.8 µs; with the tray, the bursting check and overflow added,
  4.1 µs: 4050 times real time, 2.5 ms a frame at 600 steps a frame. The time is in the phase
  equilibria themselves (the logarithms of each affinity).
- Rendering: the optical pass under 2 ms on an integrated GPU. **Measured** (Stage 11, `make
  screenshots`, Apple M3 Pro, 1920×1080, refraction on, as the slope of the frame time over extra
  passes, each with its barrier; three runs): a settled pass about 0.1 ms, within the measurement's
  noise; a pass in which every covered pixel's history starts at once (66,726 pixels, the fixed
  estimate and 64 points each) 0.93 to 0.98 ms. Stage 10 recorded 1.9 and 11 ms: its runs' whole
  frames took 70 ms against about 3 ms since, so the machine was busy with something else then, and
  those numbers are withdrawn. The first fixed-band pass was 0.21 ms.
- **Stage 19:**
  - Electrolytes cost the demo 6.67 µs a step, against 4.27 µs on the same machine before: 4.0 ms
    a frame at 600 steps a frame.
  - The time is the boiling equilibrium's search through Pitzer's excess, and the three more
    crystallisations.
- Timing is evidence, recorded per stage; it is not a gate.

## 20. Declared simplifications of the demo

| Simplification | Size of the error |
|---|---|
| Every cell at 1 atm; balloon is a slack membrane | Ignores about 2 J of elastic energy against 2.35 kJ of reaction heat |
| No gravitational energy in the ledger | About 0.2 J per pour |
| One temperature per vessel | The whole flask reaches the solid's temperature in stage 1; heat losses are overstated |
| Constant heat capacities | A few percent over the demo's temperature range |
| Activity coefficients are those of 25 °C at every temperature; CuSO₄(aq)'s `Hf`, `S°` and `Cp` are fitted to its solubility | The solubility is within 2.6% of the handbook from 0 to 100 °C in water alone. There is no heat of dilution. The dissolved salt's fitted heat capacity (+212 J/mol·K, where real ions have strongly negative ones) carries the temperature dependence of its activity: the demo's solution holds about 4% too much heat per kelvin |
| The activity coefficients are extrapolated beyond their data: CuSO₄'s to 1.4 mol/kg, the acid's to 5 | In a solution boiled down past them only the trend is right; the lower hydrates crystallise there. CuSO₄'s C^φ is left out so that the water's activity falls at every strength |
| No mixing terms between the acid's and the salt's ions | The acid lowers the salt's solubility to 0.70 of it at 15 °C with 1 mol/kg of acid; one old measurement gives about 0.54 |
| Sulfuric acid is fully dissociated into 2 H⁺ and SO₄²⁻, its activity fitted as such | No bisulfate, no pH, no vapour of its own; its heat capacity is the pure liquid's |
| A dissolved species takes a constant volume, its apparent molar volume from densities at 20 °C (low confidence) | The acid's 35.7 cm³/mol is 1.8 mL in 50 mL of its solution; the true apparent volumes vary by a few cm³/mol over the range |
| The hydrates lose their water in three first-order steps with calibrated rates | One activation energy for all three (80 kJ/mol), and prefactors that put each step's fastest rate at its thermogravimetric peak (90, 120 and 256 °C at 10 °C/min). The times are plausible, not measured |
| Particles of a solid are equal spheres, counted per cell | No size distribution; crystals do not move with a liquid that is poured off |
| A crystal nucleus is one quantum, about 6 µm | Real nuclei are a thousand times smaller |
| Complete decanting | Real crystals keep a film of liquid |
| Pouring and tipping are instantaneous | No stream, no splash, no rate to control |
| The bench opens out of equilibrium | The acid's water wets the air above it and the condenser cools to its coolant in the first seconds |
| Kinetic parameters calibrated for play | Times are plausible, not measured. Malachite: Ea 150 kJ/mol is inside the measured 131–159; the prefactor is chosen so it decomposes in minutes at 300–350 °C. The oxide's dissolution, the crystals' growth and nucleation, and the dehydration are calibrated outright (`docs/DATA_SOURCES.md`) |
| A reverse reaction's rate is proportional to the same reactant as the forward one | Malachite reforms from CuO, CO₂ and steam only where some malachite is left; fully decomposed, it never does |
| Processes applied in sequence | A time shift of about one step |
| No pressure below 1 atm: a rigid cell that cannot draw enough gas is left under-full and flagged | Arises only when a closed flask cools below the temperature at which it was closed with nothing to draw from |
| Gas moves only by bulk flow; gases do not diffuse between cells | A flask and its balloon keep different compositions until gas flows between them |
| Air is 79% N₂ and 21% O₂ | Argon (0.9%) and the rest are left out |
| Steam is an ideal gas with a constant heat capacity | Water boils at 100.44 °C at 1 atm (373.124 K is 99.97 °C); its saturation pressure is within 2% from 25 to 100 °C |
| Fast phase change has no barrier | No superheating, no supercooling, no fog: vapour condenses the moment it is saturated |
| A drain is instantaneous and takes all the liquid | A condenser never holds a film of liquid for more than a step |
| A burst balloon's gas is the room's at once | No bang, no jet |
| One item to a grid cell, and position decides nothing | A beaker "under" a condenser is a connection, not a place |
| A vessel's wall is at its contents' temperature | The balloon's rubber and the flask's glass add heat capacity and nothing else |
| Every optical spectrum is calibrated to the substance's known colour, not measured | Colours are right in kind (the blue of copper sulfate deepens as it should with concentration and depth); the exact hue is the calibration's. `docs/DATA_SOURCES.md` |
| A vessel is a cylinder and a balloon a ball, whatever they are drawn as | Path lengths are those of a cylinder of the vessel's volume |
| Glass, the acid and gases are perfectly clear; water absorbs as measured, and what it scatters is counted only as lost from the beam; nothing refracts (until Stage 11) | A clear liquid is shown by a drawn line at its surface; metres of water would be blue, but the in-scattered light that makes a pool glow is not added |
| A heap of mixed powders has the mass-weighted mean of their reflectances | Real powders mix darker than that |
| Haze is single scattering, with the return path taken as half a chord | A stirred suspension's colour is approximate; its opacity follows the particles' count and size |
| A balloon is seen only by the light through it; a flame is drawn, not simulated | No sheen on the rubber; the burner is a hot reservoir with no chemistry |
| A colour outside sRGB keeps its lightness and hue and loses chroma (OKLCh, onto sRGB, not the display's own gamut); exposure is fixed | Deep copper blue is drawn as a less saturated blue of the same lightness; a body past about 1200 K is drawn at the display's white |
| What a vessel's surfaces reflect shows the room in front, one radiance, not traced; a ray bent by one body is not traced into another (each item is drawn at its own scale); a footprint's width is taken as its mean across one step of a band's wavelengths; between a map's samples and within its pieces a ray is interpolated (within a fiftieth of a pixel of the screen); a pixel is read in four parts at most; at a route's change each side's wall is read where its own ray lands (a quarter of a pixel at most, of rays that graze a surface) | A rim shows no reflected image of the bench; a ray leaving a vessel aslant toward its neighbour sees the wall instead; the interpolation is far under what the eye sees (`O4`) |
| A solution's index is water's; the solutes' share (about 0.01 for 1 mol/L of copper sulfate) is not modelled; a heap of solid, the balloon and anything up and down (a liquid's surface, a vessel's floor) bend nothing | A strong solution bends as water does; a pool in a balloon is seen straight |
| The room in front of the bench, which glass reflects, is one radiance (0.3 of the room's white), and the wall is 20 cm behind the vessels: both calibrated, not measured | Rims are darker than the wall, as on a lit bench; how much is the calibration's |
| The room's is the only light; a hot solid's glow is its exact colour, added to what the pixel shows outside its history | No lamp or glowing gas lights the scene yet (§12.2 is built for them); the glow is not dimmed by what lies in front of the solid |
