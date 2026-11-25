# Experiments: the roadmap after Stage 41

The user, 2026-11-24: "plan the addition of more experiments, similar to the current tutorial in that
they will have step by step instructions and have an optional non strict mode. The experiments should
explore more characteristics which are currently missing from our simulation and would be nice to
explore, like pressure, exo/endothermic reactions, acidity, or any other type of property you think is
crucial for beginner level chemistry. This is not the step for actual levels which allow the player to
learn chemical principles and theory, but it is supposed to explore all of the features which would be
required for step by step chemical principles lessons."

Stage 41 (`docs/TDD_41_EXPERIMENTS.md`) built the frame: strict and free tutorials, the catalogue and
its list on the screen (GAME_SPEC §13.7), results that read any quantity's start, least and most, and
three experiments on what the simulation already does (pressure, heat of solution, rate). This file is
what comes next. The areas are the user's choice (2026-11-24). Each one needs the simulation to grow
first, and each is meant to be its own stage, numbered when it is taken.

Every area follows the same rules as any stage:
- **Data before code.** Every new species and reaction has its row verified in `docs/DATA_SOURCES.md`
  and an entry in `data/sources.json`. A measured value also gets a gate test at its source's own
  conditions (`native/tests/test_measured.cpp`).
- **Studies before tutorials.** A `chem_tool study` holds the new physics to its closed form before any
  tutorial is written on it, as `study solution_heat` did for Stage 41.
- **Invariants before examples.** A new law comes with its named check (GAME_SPEC §18) and a self-test
  that fires on a corrupted trace.
- **Every experiment is played to its end** through the screen: strict, strict at a person's pace, and
  free (`native/tests/game/test_experiments.cpp`).

## What the simulation has today (for reference)

| Property | Where | State |
|---|---|---|
| Heat of reaction and of solution, seen as temperature | §4.2 (from ΔHf, nothing by hand) | Built; the `solution_heat` experiment |
| Pressure in a sealed vessel, the boiling point with it, the stopper and the glass giving way | §6, Stage 28 | Built; the `vacuum` experiment |
| Arrhenius rates, surface kinetics, stirring | §7 | Built; the `rate` experiment |
| Boiling, condensation, distillation, boiling-point elevation | §8 | Built; the demo |
| Solubility, crystals, hydrates, the common-ion effect | §9 | Built; the demo |
| Colour from spectra | §12 | Built (no indicators) |
| Monitors, controllers, rigs, alarms | §13.3 | Built; the `rate` experiment's thermostat |

## 1. Acids, bases and pH

**Built in Stage 43** (`docs/TDD_43_ACIDITY.md`):
- NaOH(aq) and Na₂SO₄(aq), the neutralisation and its heat, and the pH (derived, Kw held to IAPWS).
- `ph` for monitors and goals.
- The `dilution` and `neutralisation` experiments.

What is still to come of this area: bisulfate, weak acids, an indicator, a burette, and the sodium salts as
solids.

**Experiments**
- The pH of an acid as it is diluted, tenfold at a time.
- Neutralisation's heat: about −55.8 kJ/mol for a strong acid and base, seen as temperature.
- A titration to pH 7. A controller runs the burette at speed (§13.3, Stages 37 to 39).

**What the simulation lacks**
- Water's self-ionisation, H₂O ⇌ H⁺ + OH⁻ (Kw = 1.0e-14 at 25 °C), as a fast equilibrium (§8's machinery).
- OH⁻ as an ion.
- Bisulfate, HSO₄⁻ ⇌ H⁺ + SO₄²⁻ (Ka2 about 1.0e-2). §20 now reads "Sulfuric acid is fully dissociated
  ... No bisulfate, no pH".
- A strong base: sodium, a new element (Na), and NaOH(s) and NaOH(aq).
- Na₂SO₄(aq), the salt the neutralisation makes.

**Data to source**
- Kw(T).
- Ka2 of HSO₄⁻.
- ΔHf, S° and Cp of OH⁻(aq), Na⁺(aq), NaOH(s) and Na₂SO₄(aq): NBS (Wagman 1982) or CODATA.
- Pitzer's parameters for Na⁺/SO₄²⁻, Na⁺/OH⁻ and H⁺/HSO₄⁻ (Pitzer 1991; Kim & Frederick 1988).

**Measures and items**
- `ph` as a quantity of a vessel (§13.3), from the activity of H⁺: −log10(a_H⁺).
- A burette: a vessel whose drain runs at a rate its valve sets. Drains have only a fixed rate today
  (`set_drain`).

**Depends on:** nothing new. It is the first to build, because precipitation needs its base.

## 2. Precipitation and gas

**Built in Stage 44** (`docs/TDD_44_PRECIPITATION.md`):
- Cu(OH)₂(s) out of copper sulfate with the base, a fast equilibrium at the tables' Ksp; back into an acid;
  black copper oxide from it at Fukuda's measured rate.
- Malachite in sulfuric acid at Nicol's measured rate, its carbon dioxide into a balloon.
- The `precipitate` and `fizz` experiments.

What is still to come of this area: a sodium carbonate (and the fizz of a soluble one), the suspension's
cloud as it falls (its turbidity), and other precipitates (Ksp of each: barium sulfate, calcium carbonate).

**Experiments**
- CuSO₄(aq) + 2 NaOH(aq) → Cu(OH)₂(s), a pale blue precipitate. Heated, Cu(OH)₂ → CuO + H₂O, and it turns black.
- A carbonate in acid fizzes CO₂ into a balloon. With malachite, every species is already in the tables:
  Cu₂CO₃(OH)₂ + 2 H₂SO₄ → 2 CuSO₄ + CO₂ + 3 H₂O. A sodium carbonate version comes after area 1.
- The gas is tested: the balloon's CO₂ share (`gas_share`) is shown already.

**What the simulation lacks**
- Precipitation from two mixed solutions: nucleation of a sparingly soluble solid at its Ksp. The
  crystals of §9 come only out of a supersaturated solution of the same salt.
- A carbonate's dissolution kinetics in acid, with the gas leaving as it forms.
- The optics: Cu(OH)₂'s spectrum, and a suspension's turbidity. Its scattering is planned with the
  bubbles of area 3.

**Data to source**
- Ksp of Cu(OH)₂.
- ΔHf, S° and Cp of Cu(OH)₂(s).
- The rate of malachite (or of a carbonate) in acid, measured. The rule for fetched data applies.

**Depends on:** area 1 (NaOH).

## 3. Boiling, bubbling and agitation

The user, 2026-11-24 (after Stage 44): "When a solution is boiling it should have gas bubbles at the very
least, acting similarly to suspended particles, diffusing light. Also, the amount of bubbles should depend on
the depth of the liquid. [...] if it's heated from the bottom - the bubbles are created there, then they rise
and spread their heat to their surroundings, condensing back into liquid as they rise. Depending on the
temperature distribution of the liquid as well as the temperature and size of the bubbles, they might not even
reach the surface at all, but end at some midpoint." Agreed plan: this area is the next stage, "bubbles and
the heated floor"; the full vertical dimension (area 8) and two liquids (area 9) are stages of their own after
it.

**Built in Stage 45** (`docs/TDD_45_BUBBLES.md`): the model below, all but the superheated bulk, bumping and
boiling chips; the `boiling` experiment. Still to come here: a bulk that superheats and bumps, chips, a
column that narrows with its vessel, bubbles as bodies the pass traces, sloshing.

**Before Stage 45.** Boiling is a fast equilibrium on a vessel as one well-mixed cell: the steam leaves the liquid for
the vessel's gas at once, and nothing is seen. A gas a reaction makes in a liquid (the malachite's carbon
dioxide, Stage 44) leaves at once too. Only stirred powder scatters inside a liquid.

**The model: one bulk, and a film at the heated floor.** Heated from below a liquid is unstable: its warm
bottom rises, and natural convection keeps the bulk within a little of one temperature. What is not uniform is
a thin thermal boundary layer against the hot floor, about a millimetre, which can stand above the boiling point
while the bulk is well below it. So:
- **A floor film** (§11's wall film): a thin cell between the burner's floor and the bulk, heated through the
  glass and giving its heat to the bulk through a natural-convection coefficient (a Rayleigh correlation, its
  source a row of `docs/DATA_SOURCES.md`).
- **Bubbles start in the film** once it passes the boiling point at the floor's depth (the liquid's weight on
  it, about 0.3 K higher for 10 cm of water) plus the superheat a bubble needs to start on that surface:
  a few kelvin on scratched glass, more on smooth glass (bumping), little on a boiling chip.
- **They rise and condense.** A bubble rises at its terminal speed (about 0.2 to 0.3 m/s for a few
  millimetres) and gives its latent heat to a bulk cooler than its own boiling point, so it shrinks and
  collapses after a length set by that subcooling: roughly a millimetre at 10 K under, a centimetre at 1 K,
  ten at 0.1 K (estimates; each to be held to its source). Before the bulk boils, bubbles die near the floor
  (the kettle "sings"); within a few tenths of a kelvin of boiling, they reach the surface and the vessel
  boils. The latent heat they carry is a transfer from the film to the bulk: conserved, and the way a boiling
  vessel actually moves its heat up.
- **Depth matters twice**: a deeper column holds more bubbles at once for the same steam (a longer path), and
  its floor boils a little hotter.
- **Bubbles are quasi-steady, not state.** A bubble lives a tenth of a second; the temperatures change over
  minutes. Each step the population follows from the steam made, the rise speed and the collapse length: a
  holdup (the gas's share of the liquid) at each height, from the floor to where they end.
- **What is seen:** the bubbles are a scattering layer of the optical pass (§12), its density a gradient up
  the vessel's height, as stirred powder is a haze (`everything-physical-is-spectral`).
- **A gas a reaction makes** in a liquid leaves through bubbles the same way, from where it is made, with no
  collapse (it does not condense): the fizz of `fizz`.

**Experiments**
- Heating water: bubbles that collapse near the floor first, then reach the surface at the boil.
- Deep against shallow: the same burner, more bubbles in the deeper one.
- Boiling chips against bumping: a smooth vessel superheats, then boils in a burst.
- The fizz of a gas a reaction makes.
- Stirring against sloshing; a vortex, which is already built (§11, Stage 32).

**What the simulation lacks**
- The floor film, and the floor's own boiling point under the liquid's weight (§20: every open cell at 1 atm).
- Superheating: fast phase change has no barrier. §20 reads "No superheating, no supercooling, no fog".
- Nucleation sites: chips, scratches.
- Bubbles in the optical pass (§11, §12), and gas leaving a liquid at a finite rate.
- Sloshing: §11 reads "not built".

**Data to source:** a natural-convection correlation for a heated floor; bubble departure size and rise speed
in water; a bubble's condensation in subcooled water; the superheat at which glass and a boiling chip start
bubbles (calibrated, with why); light scattering by bubbles (geometric optics: extinction efficiency 2).

**Depends on:** nothing. The optics are the larger part (`defer-hard-renderer-issues`: find the cause,
document it, move on).

## 4. Freezing and melting

**Experiments**
- Ice melting in a beaker: the temperature holds at 0 °C until the ice is gone.
- A freezing curve: water cooled in a cold bath, the plateau at 0 °C.
- Freezing-point depression with a solute. Copper sulfate will do, and the same water activity gives
  boiling-point elevation today.
- Salt on ice, a freezing mixture.

**What the simulation lacks**
- H₂O(s), and melting and freezing as kinetic phase changes. §8 reads "Kinetic phase changes and
  melting are PLANNED".
- The latent heat of fusion, from ΔHf(ice).
- A cold bath item: a coolant like the condenser's, below 0 °C.
- Supercooling, along with superheating (area 3).

**Data to source**
- ΔHf, S° and Cp of ice.
- The melting rate of a lump. §8 already describes "melting of a lump" as a slow phase change.

**Measures and items**
- A vessel's ice (g) is a species' mass, so it needs nothing new.
- The cold bath.

**Depends on:** nothing.

## 5. Pressure and temperature

**Experiments**
- Gay-Lussac: a sealed flask of air heated, with its pressure against its temperature.
  `vacuum` gives only water's vapour.
- Charles: a balloon of air warmed and cooled, its volume with its temperature.
- Boyle: a syringe pushed in, with its pressure against its volume.
- A pressure cooker: water boiling above 100 °C under a weighted lid.

**What the simulation lacks**
- The balloon's tension law. §6 reads "Still to build". Today it is slack at 1 atm until it bursts.
- A piston or syringe: an elastic cell whose volume the player sets.
- Gas flow through finite conductances: valves, tubes, frits (§6).
- A vacuum pump.
- Pressure below 1 atm in an open vessel (§20).

**Data to source**
- Latex's tension curve (a calibrated fit).
- A pressure-cooker lid's mass over its vent.

**Measures and items**
- `kpa` already exists, and so do `gas_ml` and the balloon's `fill`.
- New items: a syringe, a valve on a tube, a weighted lid.

**Depends on:** nothing. The `vacuum` experiment is its first part.

## 6. Humidity and saturation

**Experiments**
- Water evaporating into dry air and into humid air.
- Dew on a cold flask.
- A solubility curve: copper sulfate dissolved to saturation at several temperatures. Possible now.
- A supersaturated solution seeded.

**What the simulation lacks**
- Water vapour in the room's air. The room is dry, so a jar of the pentahydrate on the bench slowly
  loses its water. The `solution_heat` experiment's jar raises that notice (Stage 41's Open).
- Relative humidity and the dew point as quantities.
- Gas exchange between a vessel and the room at a finite rate. Diffusion: §20 reads "gases do not
  diffuse between cells".
- Wall films (§11): condensate on the outside of a cold vessel.
- Gases dissolving in water, Henry's law (§10).

**Data to source**
- Water's vapour pressure (already in the tables, through steam).
- The room's humidity (a design value).
- The rate of exchange through a vessel's mouth (calibrated).

**Measures and items**
- `humidity` and `dew_c` of a vessel's gas, and of the room.
- A seed crystal, which is a jar of the pentahydrate.

**Depends on:** nothing. Its room humidity also settles the pentahydrate's notice.

## 7. The lessons' own mechanics

These are not chemistry, but every principle lesson will need them.

- **A measurement log.** The player records a reading (a value of §13.3's quantities) into a table on
  the card, at a step's own moment. The tutorial checks what was recorded against the state when it
  was recorded. Stage 41's `start`, `least` and `most` are the first step towards it.
- **A prediction.** A step asks before the lab shows the answer ("will it warm or cool?"). The answer
  is a choice on the card, kept and compared with the result, and never refused: a wrong prediction is
  the lesson, not a failure.
- **A plot.** A quantity against the lab's time, drawn on the card or in the readout as a flat
  overlay. It is not physical, so it does not go through the optical pass.
- **Lessons in order.** The catalogue grows prerequisites and a player's progress. That progress is
  the first state outside the lab that needs saving.

**Taken up by the curriculum** (`docs/CURRICULUM.md`, 2026-11-24): this area grows into its steps A to C,
the first to build: the measured lab (a reagent shelf, a balance, volumetric glassware, a burette,
instruments with their error, composition hidden per experiment), the notebook and the report (a table,
a plot, a fitted line, predictions, unknowns), and the calculator, the planner and the reference.

## 8. The vertical dimension

**Built in Stage 47** (`docs/TDD_47_COLUMN.md`):
- **What it is:** a vessel's column of layers, grown as it needs from one cell.
- **What it does:**
  - overturns where the lower layer is the lighter;
  - passes what is dissolved and heat between stable layers;
  - resolves a steep step into thinner layers.
- **In the lab:** gentle pours that layer, the burner on the lowest layer, a layered bench.
- **On the screen:** bands.
- **The experiment** `layers`.

**Still to come here** (the user's "other considerations" for a gradient, 2026-11-24):
- entrainment where a pour lands;
- shear at an interface;
- salt fingers (double diffusion: heat diffuses about 100 times faster than salt);
- the Soret effect (a solute drifting in a temperature gradient);
- an unstable interface overturning in plumes, not whole;
- volumes that expand with temperature;
- a column that narrows with its vessel;
- bubbles layer by layer.

The user, 2026-11-24: "What would it take to correctly model vertical temperature distribution in a vessel?
[...] I do want to accurately simulate the vertical dimension of vessels at some point, allowing for things
like water and oil to not mix permanently, but for them to have some heat transfer." A stage of its own,
after area 3.

**When a vessel is not one temperature.** Heated from below it nearly is (area 3). Heated from above, cooled
from below, or holding layers of different densities, it is stably stratified, and it stays so: conduction
alone takes hours to even out 5 cm of water (L² over water's diffusivity, about 1.4e-7 m²/s).

**The model: a column of layers.**
- A vessel's liquid is a stack of horizontal layers, each a cell of the closed world with its own composition
  and temperature: well mixed across, varying only up the height (the one-dimensional model of lakes and
  tanks). The vessel's shape gives each layer's height from its volume (`lab/contents.hpp`, Stages 30 to 32).
- **Convective adjustment**: where a layer is lighter than the one above it, they mix (fast, by a Rayleigh
  correlation's rate); where it is heavier, heat passes by conduction alone (water's 0.6 W/m·K).
- **Adaptive layers**: layers whose states are the same merge, so a well-mixed vessel is one cell and costs
  what it costs today; a layer splits off only when a gradient appears.
- **The liquid's weight**: each layer's pressure, and so its boiling point, from the liquid above it.
- **What touches what**: the burner heats the bottom layer, the walls lose heat from each layer they bound,
  the gas meets the top layer, solids settle into the bottom one, stirring mixes the column (the turning's
  energy, Stage 32), a pour by hand takes the top layer first.
- **Kept**: layers are cells, so conservation comes with `transfer` and `transform`. New invariants: the
  layers' volumes sum to the liquid's; no unstable layering outlasts the adjustment (as P1 holds the fast
  equilibria).
- **Seen**: each layer a layer of the optical pass (§12's stack of layers, which already provides for "a
  liquid stratified after a pour").

**Experiments**
- Warm water poured gently over cold: it stays on top, and the boundary blurs over hours.
- Heated from above (a lamp) against below: the one stratifies, the other mixes.
- A denser salt solution under water.

**Cost**: every golden moves; the one-cell closed forms of the boiling, cooling and distillation studies are
derived again; more cells to a vessel only where a gradient stands.

**Depends on:** area 3's floor film (the heated floor's boundary layer is the column's lowest layer).

## 9. Two liquids

After area 8. Water and an oil do not mix, yet pass heat between them.
- **A second liquid phase.** The simulation's liquid is one phase with water as its solvent (§10). Two
  liquids need liquid-liquid equilibrium (which pairs mix and how much), and an oil species with its data.
- **Separation**: shaken, two liquids are an emulsion; its droplets rise or settle at Stokes' speed and
  coalesce, and the column becomes two layers (area 8), the lighter on top.
- **Between the layers**: heat by conduction and each layer's own convection; a dissolved species shares
  itself between them by its partition coefficient (extraction).
- **Seen**: an emulsion is a cloud (droplets scatter, as bubbles do); the interface is a surface the optical
  pass traces (§11's liquid surface).

**Experiments**: oil on water; shaken and let settle; heated from below through the oil; iodine or a dye
shared between the layers (an extraction).

**Depends on:** area 8.

## The curriculum's areas

`docs/CURRICULUM.md` (2026-11-24) lists a first-year student's experiments and what each needs. Areas 1 to 9
carry most of its chemistry: area 1 its step D, area 2 its step E, area 4 its step I, areas 5 and 6 its step
G, area 9 its step J. It adds these areas, each a stage of its own when it is taken:

10. **Solution kinetics and redox** (step F): homogeneous rate laws in the concentrations, catalysts
    (GAME_SPEC §7, planned); redox in solution: permanganate, iodine and thiosulfate, peroxide, a metal
    displacing another. The iodine clock, crystal violet, the vanishing cross, redox titrations, the
    activity series.
11. **Electrochemistry** (step H): a wire as an edge that carries charge, a cell's potential from its
    affinity, electrolysis as electrical work, a conductivity derived from the ions' mobilities. Galvanic
    and concentration cells, Faraday's constant, conductometric titration.
12. **Burning and emission** (steps E and I): a fuel and magnesium burning in air as reactions, and the
    emission lines of flame tests and atomic spectra.
