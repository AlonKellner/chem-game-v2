# TDD 54: The notebook and the report (Stage 54)

## The request

The curriculum's step B (`docs/CURRICULUM.md`; roadmap area 7, the lessons' own mechanics), after Stage 52's
sign-off: the user's "Approved, proceed" (2026-11-25). Stages 50 and 52 gave the lab instruments; a student also
writes down what they read, plots it, fits a line, and reports a result that is checked.

Its number is 54: another agent's Stage 53 (the glass in layers) was in progress on main's next number.

What it builds:

- **The notebook** (S1): a reading recorded at a moment (`record`), or every so many seconds (`log`, a logger);
  by instruments what the instrument reads, never the truth it hides. Saved with the lab; not digested (it changes
  nothing in the world).
- **A table and a plot**: the notebook's columns by label, a row a moment; any column against another or against
  time, each axis as it is, its logarithm or its reciprocal (of a temperature, in kelvin); a least-squares line
  through it with its slope, intercept and R².
- **The report** (S5): a tutorial's results the student types in at its end, each checked against the truth (an
  expression over the lab's quantities, worked out then) within its tolerance, beside the textbook's ideal and why
  they differ.
- **Unknowns** (S7): a bench's amount given as a range, drawn once from its seed.
- **A pressure gauge** by instruments: a sealed vessel's pressure read to 0.1 kPa within 0.5 kPa.
- **The experiments**: a titration's curve (curriculum 13), and water's vapour pressure against temperature with
  Clausius and Clapeyron's line (30).

The spectrophotometer (T5), predictions and lessons in order (S6) are the next stage's.

## The design

- `lab/notebook.hpp` (pure): an entry is its step, its label, its quantity, its value and unit, and whether an
  instrument read it. `reading(view, quantity)`: by instruments a thermometer's, a pH meter's, a balance's, a
  burette's, marks', a gauge's reading, and none for what no instrument reads (a species' mass); otherwise the truth.
  `fit_line`: least squares, its slope, intercept and R². `transform`: as it is, ln, 1/x (a Celsius temperature
  as 1/K).
- The lab: `record {item, what, measure?, label?}`; `log {…, every}` (every at least 1 s), `unlog`; `erase {entry?}`.
  Loggers record at the end of each step that reaches their time. `LabView::notebook`, `loggers`.
- The screen: a "notebook" panel over the bench (the top bar's "notebook", after reset): its table, its plot of two columns with each axis's
  transform and the fitted line; a "+" beside each reading in the readout records it (`record:<item>:<what>`); the
  panel logs the last column recorded every 1, 5 or 30 s.
- The tutorial: `report`: [{label, unit, truth (an expression: numbers, + − × ÷, parentheses, `item.what` or
  `item.what.measure`), ideal, tolerance, relative, why}]; at the end the card's fields take the student's numbers,
  checked against the truth then.

## Phases

| Phase | Topic | Status |
|---|---|---|
| 54.1 | GAME_SPEC; the gauge's data | Done |
| 54.2 | `lab/notebook.hpp`: readings, the fit, the transforms; the expression | Done |
| 54.3 | The lab: record, log, erase; unknowns; saved, not digested | Done |
| 54.4 | The screen: the notebook's panel, its table, plot and fit; "+" in the readout | Done |
| 54.5 | The report on the tutorial's card | Done |
| 54.6 | The experiments: a titration's curve, water's vapour pressure | Done |
| 54.7 | This document, CHANGELOG, `make gates`, the user's sign-off | `make gates` passed (1206 s: ASan+UBSan 1192, ctest 934, Godot 13 of 13, goldens unchanged; the vapour fix after it: the engine-free gate); signed off by the user, 2026-11-25 ("Approved, merge and proceed") |

## The goldens

None moved: the notebook is not digested, and the gauge, the unknowns and the report are read, not stepped. The
sessions' records are unchanged.

## What was built

**`lab/notebook.hpp`** (pure). `reading`: by instruments a thermometer's (`read_c`), a pH meter's (`read_ph`), a
burette's or a cylinder's scale or marks' (`read_ml`), a balance's display (`reads_g`), the gauge's (`read_kpa`); a
tap's, a valve's or a mark's state is the truth, not an instrument's; a species' amount none. `fit_line` (least
squares; refused under two points or with no spread in x). `transformed` (as it is, ln of a positive value, 1/x, a
Celsius value's as 1/K). `evaluate`: a recursive descent over numbers, + − × ÷, parentheses, unary minus and
quantities named `item.what[.measure]`, read through `conditions.hpp`'s `measure`.

**The lab** (`lab.cpp`). `record`, `log` (aligned to whole multiples of its period), `unlog`, `erase`; loggers
followed at the end of each step after the thermometers; at most 2000 entries. `LabView::notebook`, `loggers`; saved
under the bench's `notebook` only when there is one, loaded likewise; not digested. An amount given as `[lo, hi]`
drawn from the seed (`amount_of`, keyed by the item and the species). `ItemView::read_kpa`, the gauge
(`data/instruments.json`, `gauge`).

**Conditions** (`conditions.hpp`): `logging` and `recorded`, so a step can ask for a logger or an entry.

**The tutorial** (`tutorial.hpp`): `report`, its items parsed and their expressions tried on the bench when opened
(one naming what is not there is refused; a division by zero is left to the end).

**The screen** (`ui.cpp`). The top bar's "notebook" (after reset); its panel over the bench (`NOTEBOOK`): the last ten
entries, the plot with its axes' columns and transforms, the fitted line with its slope, intercept and R², the
loggers and their buttons. "+" buttons under a vessel's or a balance's readings. The report in place of the readout
once the run is complete: a field a result, typed, checked. The experiments list grows a third column past sixteen
(nineteen in two ran off the screen's foot).

**The experiments**: `titration_curve` (10 steps), `vapour` (8 steps).

## Measured

- `titration_curve`: 216 paired readings of pH and the burette across the run; the end point's truth twice the acid's
  moles over the base's concentration (the base 0.0997 M, as made), within 1 % of 2 × 25.00 mL × c / 0.0997.
- `vapour`: ln P against 1/T over the cooling, 41.7 kJ/mol with R² 0.9998, against the tables' 41.93 at 75 °C and
  the textbook's 40.65 at 100 °C.

**Tests**:

| Test | What it checks |
|---|---|
| `test_notebook.cpp` (3) | the fit, the transforms, the expression; record and log by the truth and by instruments, a species refused by instruments, saved and loaded, not digested, unlog and erase; an unknown drawn within its range, the same for a seed |
| `test_experiments.cpp` (6) | the two experiments strict with their own gestures (the notebook's pairs; the report within and off), at a person's pace, and free |

## The user's tests

- 2026-11-25: "The vapour experiment asks me to let the flask cool, but does not allow me to close the burner
  valve." Its step "Off, and stoppered" checked only the stopper, so the stopper put in first passed it, and strict
  the valve was refused from then on, the flame still under the flask while it was to cool. Split in two: "Off" (the
  valve shut), then "Stoppered" (the stopper in, the valve still shut). `experiment vapour: strict, the valve is shut
  before the stopper goes in, and stays shut while it cools` plays the stopper first and finds it refused.

## Open

- **Sensing parts and checks read the truth** (Stage 50's §20 row): a logger reads the instruments, a monitor does not.
- **The plot is the screen's alone**: its axes' choice and the fit are not saved; the notebook's entries are.
- **The report's numbers are not saved**: they are typed and checked at the end of a run.
- **The spectrophotometer, predictions and lessons in order** are the next stage's (`docs/CURRICULUM.md`, step B).
