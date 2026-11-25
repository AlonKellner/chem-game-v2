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
- The screen: a "notebook" panel beside the notices: its table, its plot of two columns with each axis's
  transform and the fitted line; a "+" beside each reading in the readout records it (`record:<item>:<what>`); the
  panel logs the last column recorded every 1, 5 or 30 s.
- The tutorial: `report`: [{label, unit, truth (an expression: numbers, + − × ÷, parentheses, `item.what` or
  `item.what.measure`), ideal, tolerance, relative, why}]; at the end the card's fields take the student's numbers,
  checked against the truth then.

## Phases

| Phase | Topic | Status |
|---|---|---|
| 54.1 | GAME_SPEC; the gauge's data | |
| 54.2 | `lab/notebook.hpp`: readings, the fit, the transforms; the expression | |
| 54.3 | The lab: record, log, erase; unknowns; saved, not digested | |
| 54.4 | The screen: the notebook's panel, its table, plot and fit; "+" in the readout | |
| 54.5 | The report on the tutorial's card | |
| 54.6 | The experiments: a titration's curve, water's vapour pressure | |
| 54.7 | This document, CHANGELOG, `make gates`, the user's sign-off | |
