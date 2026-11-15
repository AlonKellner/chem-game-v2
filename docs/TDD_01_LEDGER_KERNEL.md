# TDD 01: The ledger kernel (Stage 1)

## The request

> One thing I want to prioritize as well is having conservation laws as invariants for this game,
> just like ricochet has invariants for tests, this game should have them as well. If possible,
> conservation laws should be baked into the code of the game, to make sure nothing violates any
> important conservation law. This include energy, mass, moles, and any other reasonable type you
> can think of.

## What Stage 1 delivers

The part of the simulation that makes conservation structural (GAME_SPEC §4.1–§4.3), and the
independent checks that witness it (§18). No physics yet: nothing in this stage knows a
temperature or a rate.

| Phase | Topic | Status |
|---|---|---|
| 1.1 | Integer quantities: amounts in 1e-12 mol, energy in 1e-9 J, overflow-checked (`chem/quantity.hpp`) | Done |
| 1.2 | Species tables; atoms and molar mass derived from the formula (`chem/species.hpp`, `native/datagen/formula.hpp`) | Done |
| 1.3 | `Reaction`: cannot be constructed unbalanced in any element or in charge | Done |
| 1.4 | Build-time embedding: `chem_datagen` refuses unbalanced data; the generated tables `static_assert` it again | Done |
| 1.5 | `World`: private cell state, edges with phase filters, `transfer`, `transform`, refusals, the ledger, `StepTrace` (`chem/world.hpp`) | Done |
| 1.6 | The invariant kernel: `check_C1`…`check_C8`, `check_all`, exact `Totals` (`chem/invariants.hpp`) | Done |
| 1.7 | Self-tests: every check fires on the corruption it exists to catch | Done |
| 1.8 | The conservation sweep over random chemistries and worlds | Done |
| 1.9 | C API and `chem_tool tables`: the built-in species and reactions | Done |
| 1.10 | The user's sign-off | Done (2026-11-15: "This looks good. Approved, proceed") |

## How conservation is baked in

Three layers, each independent of the others:

1. **Types.** Cell state is private to `World`. The only functions that change it are `transfer`
   (one cell loses exactly the integers the other gains) and `transform` (applies a `Reaction`,
   and a `Reaction` cannot exist unbalanced). There is no function that sets an amount after the
   world is built.
2. **Build.** The shipped reactions are data. `chem_datagen` builds them through the same
   `Reaction::make`, so an equation that does not balance stops the build; the generated C++
   carries `static_assert(all_balanced(SPECIES, REACTIONS))`, which is the compiler's own proof.
3. **Checks.** `check_all` recomputes the world's totals from snapshots and replays the ledger,
   sharing no code with the primitives. Debug and sanitizer builds will abort on a violation once
   the step loop exists (Stage 2); the tests run the checks on every step.

Because every quantity is an integer, each of these is an equality. There is no tolerance in
Stage 1.

## Decisions

- **Mass is summed in 96 bits.** `amount × molar mass` exceeds an int64 for a large cell, so
  `ExactMass` keeps two limbs. Mass conservation follows from element conservation; summing it
  separately is a second witness that would catch a wrong molar mass.
- **Formulas, not atom counts, are the data.** `data/species.json` gives `"Cu2CO3(OH)2"`; the
  build derives the atoms. A count and a formula can never disagree.
- **Atomic masses are integers** (micrograms per mole), read from decimal strings without passing
  through a float.
- **A refusal is all-or-nothing.** Every primitive validates completely before changing anything
  (invariant L0).
- **The topology changes at step boundaries only**, so a step's trace has one set of edges and
  C7 is well defined.
- **Totals are range-checked once.** A world whose totals overflow is refused at build; they never
  change afterwards, so no later check is needed.
- **Fixed bounds:** 16 elements per table, 8 species per reaction, molar mass under 2^30 µg/mol
  (about 1074 g/mol). Each is one constant.
- **No thermodynamic data yet.** `data/species.json` holds structure only; the values in
  `docs/DATA_SOURCES.md` stay quarantined until Stage 2 verifies them.

## Evidence

- The sweep: 2000 random worlds, 80,000 steps, about 233,000 applied and 246,000 refused calls;
  every invariant holds on every step, the totals equal the initial integers, and no refused call
  changed the state.
- `world: 5 g of malachite decomposes into the masses its formula fixes`: 0.995 g CO₂, 3.597 g
  CuO, 0.407 g H₂O, summing to 5 g exactly. The original specification's "2 g of CO₂" cannot be
  produced.
- `chem_tool tables` prints the derived molar masses and the balanced equations.

## Tests

- `formula: …` (3 cases): groups, hydrates, malformed formulas, equations
- `species: …` (5 cases): derived molar masses, refused tables, balanced and unbalanced
  reactions, the compile-time proof, the built-in tables
- `world: …` (7 cases): exact transfer and transform, every refusal with the state untouched,
  foreign reactions, topology at step boundaries, refused initial conditions, the malachite masses
- `invariants: …` (9 cases): the honest step, then one corruption per check with the checks that
  must and must not fire, malformed traces, the 96-bit mass
- `sweeps: conservation holds on every step of every random world`
- ctest `datagen_refuses_unbalanced_element`, `_unbalanced_charge`, `_unknown_species`,
  `_bad_formula`; `chem_tool_tables`; `capi_c_smoke` (the tables through the C API)
