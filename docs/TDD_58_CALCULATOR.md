# TDD 58: The calculator and the reference (Stage 58)

## The request

The curriculum's step C, its first half (`docs/CURRICULUM.md`: S2, S4), after Stage 57's sign-off: the user's
"Approved, merge and proceed" (2026-11-25). The user's choice when the curriculum was planned: the student sets a
calculation up and the game works it out. Filtering (T6), the planner (S3) and the experiments that need them (3, 8,
35, 50) are the next stage's.

## The design

- `chem/formula.hpp`: the tables' formula grammar, moved from the data generator into the core, so the generator and
  the calculator read a formula alike ("CuSO4.5H2O", "Cu2CO3(OH)2").
- `lab/calculator.hpp` (pure): a formula's molar mass and its composition from the tables' standard atomic weights
  (any formula of the elements they hold; one of another element is refused, naming it); moles; a solution's mass
  (c V M); a dilution (c1 V1 = c2 V2); a reaction's stoichiometry from the masses given (each reactant's extent, the
  limiting one, what each term uses or makes; a product's mass alone says how far it went), its percent yield; a
  reaction's equation; the model's own solubility (the molality at which a crystallization's affinity is nought,
  searched upward from a dilute solution, then halved).
- The screen: the notebook's panel has three tabs, notebook, calculator, reference.
  - The calculator: molar mass, moles, solution, dilution, reaction (a reaction chosen with arrows, a mass for each
    term, what was got of its first product); its fields typed (a formula's letters, a number's digits), a field
    clicked into replaced by what is typed; its working shown as it is typed; "write ... in the notebook" writes its
    main result under its own label (M, n, m, V1, theoretical, yield).
  - The reference: the periodic table's first four periods, the elements the tables hold lit, with their weights;
    every species' formula, phase, molar mass, ΔfH, S° and Cp; every reaction's equation and law; copper sulfate's
    solubility at every ten degrees, the model's own.
- A tutorial's check `calculated`: the calculator shows a result of that kind.
- A play may type (`"type"`).

## Phases

| Phase | Topic | Status |
|---|---|---|
| 58.1 | The formula grammar in the core; `lab/calculator.hpp` and its tests | Done |
| 58.2 | The screen: the panel's tabs, the calculator, the reference | Done |
| 58.3 | The experiment: a solution by calculation | Done |
| 58.4 | This document, GAME_SPEC, CHANGELOG, `make gates`, the user's sign-off | `make gates` passed (1272 s: ASan+UBSan 1272, ctest 905, Godot 13 of 13, goldens unchanged); signed off by the user, 2026-11-25 ("Approved, merge and proceed") |

## The goldens

None moved: nothing of the lab changes; the calculator and the reference read the tables and write only notes.

## Measured

- CuSO4.5H2O 249.68 g/mol (Cu 25.45 %); Cu2CO3(OH)2 221.11; 250 mL of 0.0400 M from the pentahydrate 2.4968 g; 10.0
  mL of 0.100 M made up to 100 mL for 0.0100 M; 2.00 g of the oxide in 0.981 g of the acid: the acid limits, 1.596 g
  of the sulfate, 1.44 g got is 90.2 %.
- The model's solubility: 1.2375 mol/kg at 20 C and 2.5824 at 60 C, as Stage 57's runs settled to.
- `prepare` at the bench's seed: 1.2484 g planned, 1.2101 g weighed (a hand's few percent short), 0.04847 mol/L
  worked out against the flask's 0.04850.

**Tests**:

| Test | What it checks |
|---|---|
| `test_formula.cpp` | the grammar, from its new home |
| `test_calculator.cpp` (5) | molar masses against the tables' own, a formula refused; moles, a solution, a dilution; a reaction's limiting reagent, what it makes, its yield, from a product's mass; the model's solubility; on the screen the tabs, the reference's four pages, a formula refused saying why, a field replaced, the reaction's fields |
| `test_experiments.cpp` (3) | `prepare` strict (the plan, the balance's reading, the concentration within 0.5 %), at a person's pace, free |

## Open

- **The planner** (S3), filtering (T6) and their experiments: the next stage.
- **The reference's tables of pKa, E° and indicators** wait on the chemistry that has them (steps D to H).
- **A play's picture** is taken a few frames after its step, while the play goes on: a step of frames after each.
