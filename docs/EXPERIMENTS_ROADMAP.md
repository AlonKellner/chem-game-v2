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

**Experiments**
- Boiling chips against bumping.
- Bubbles of steam, and of a gas a reaction gives off.
- Stirring against sloshing.
- A vortex, which is already built (§11, Stage 32).

**What the simulation lacks**
- Superheating: fast phase change has no barrier. §20 reads "No superheating, no supercooling, no fog".
- Nucleation sites: chips, scratches.
- Bubbles as bodies in the optical pass (§11, §12), and gas leaving a liquid at a finite rate.
- Sloshing: §11 reads "not built".

**Data to source:** the superheat a smooth glass vessel takes before it bumps. Calibrated, with why.

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
