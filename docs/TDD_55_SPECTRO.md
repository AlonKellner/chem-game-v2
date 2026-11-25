# TDD 55: The spectrophotometer, predictions and lessons in order (Stage 55)

## The request

The curriculum's step B, its second half (`docs/CURRICULUM.md`), after Stage 54's sign-off: the user's "Approved,
merge and proceed" (2026-11-25). Stage 54 gave the student a notebook, a plot and a report; this stage gives them a
spectrophotometer (T5), a prediction before a run, and the lessons in order with their progress kept (S6).

Its number is 55: another agent's Stage 53 (the glass in layers) is still in progress.

What it builds:

- **A spectrophotometer** (T5): a single-beam instrument on the bench; a cuvette stands in its well; its wavelength
  is set (380–780 nm, the simulation's light); `blank` sets 100 % T through what stands in it now, at that wavelength;
  it reads the absorbance of what stands in it after. What it reads is computed from the same spectra the pass draws
  with (`chem/optics.hpp`: each solute's molar absorptivity at 1 nm, water's own absorption), through its slit's
  band, with its stray light: Beer and Lambert's law where they hold, and a real instrument's departures from them
  where they do not (a high absorbance flattens under stray light; a wavelength changed after the blank reads the
  lamp's own slope).
- **A cuvette**: a round glass cell, 10 mm across inside: its path.
- **Written entries**: `note {label, value, unit?}`, a number the student writes into the notebook (a standard's
  concentration); the plot pairs each y with the last x written at or before it.
- **Predictions** (S6): a report's item marked `predict` asks for the student's guess before the first step; the
  report shows it beside the result.
- **Lessons in order** (S6): the experiments list marks each completed one (its report checked) and the next in the
  catalogue's order; progress is the screen's, kept by the host between runs (`cg_ui_progress`, `user://`).
- **The experiment** Beer–Lambert (curriculum 34): standards, a calibration line, an unknown.

The rates, the half-life and the solubility curve (curriculum 4, 36, 37, 45) are the next stage's, on this one's
instrument and plot.

## The design

- `lab/spectro.hpp` (pure): `Spectrophotometer` (design data, `data/instruments.json`, `spectrophotometer`): its
  range, its slit's band (triangular, its FWHM), its stray light, its wavelength's error, its photometric resolution
  and the lamp (a tungsten filament's Planck spectrum at its temperature, as the detector sees it). `signal(nm,
  transmittance)`: the light that reaches the detector through a sample of that spectral transmittance, set at `nm`:
  the slit's band over lamp × T, plus stray light (a share of the lamp's whole). `absorbance(sample, blank)`:
  −log10 of the ratio, to its resolution. `sample_transmittance(path_cm, absorbers)`: 10^(−Σ ε c l), water's own.
- The lab: a `spectrophotometer` kind (matterless, like a balance); `insert {spectrophotometer, cuvette}`,
  `remove`; `tune {item, nm}`; `blank {item}` (the signal now and its wavelength, saved, not digested). The
  cuvette's liquid is read at the beam's height (15 mm over its floor): the layer there, or air if the liquid is
  lower. `ItemView`: `nm`, `absorbance` (none without a blank or a cuvette), `blanked_nm`.
- Quantities: `absorbance` and `nm` of a spectrophotometer (by instruments its reading; by the truth too: it is an
  instrument either way, the truth is ε c l, the report's).
- The screen: the case, its well, its display (seven segments: A to 0.001, and the wavelength) as bodies of the pass;
  its readout: the wavelength's slider and ±1 nm, `blank`, "+ A". Carrying a cuvette onto it inserts it.
- The notebook: `note`; the panel's "write" row (a label, a value).
- The tutorial: a report item's `predict` (true): fields at the start; a check `{"predicted": k}`.
- Progress: `Ui::progress()` / `set_progress(json)`: each experiment completed, its report's checks within; the
  list marks them and the next; the C API passes it, the Godot host keeps it in `user://progress.json`.

## Phases

| Phase | Topic | Status |
|---|---|---|
| 55.1 | GAME_SPEC; the instrument's data | Done |
| 55.2 | `lab/spectro.hpp`: the slit, the stray light, the lamp; the sample's transmittance; against Beer–Lambert | Done |
| 55.3 | The lab: the kind, the cuvette, insert, tune, blank; the beam's height; saved, not digested | Done |
| 55.4 | The screen: the case, the well, the display; the readout's controls; a cuvette carried onto it | Done |
| 55.5 | The notebook's written entries; the plot's pairing | Done |
| 55.6 | Predictions; progress and the list's marks; the C API and the host | Done |
| 55.7 | The experiment: Beer–Lambert's line and an unknown | Done |
| 55.8 | This document, CHANGELOG, `make gates`, the user's sign-off | `make gates` passed (1142 s: ASan+UBSan 1142, ctest 833, Godot 13 of 13, goldens unchanged); signed off by the user, 2026-11-25 ("Approved, merge and proceed") |

## The goldens

None moved: the instrument, its blank and wavelength, written notes, predictions and progress are read, not stepped,
and not digested; the new kind and the cuvette are appended (a save made before loads).

## What was built

**`lab/spectro.hpp`** (pure). `absorbance(sample)`: Beer and Lambert's sum at 1 nm from each solute's decadic molar
absorptivity (the pass's spectra) and water's own; `absorbance_at`, between the nanometres, narrow lines read
exactly. `signal(model, nm, sample)`: the lamp (Planck's law at 2900 K) under a triangular slit of 4 nm through the
sample, integrated at 0.1 nm, plus stray light (0.1 % of the band's light in a blank, of the lamp's whole visible
spectrum through the sample); a solid in the beam lets none through. `reading`: −log10 of the signal over the blank's,
to 0.001, none past 3. The displays' texts.

**The lab** (`lab.cpp`). The `spectrophotometer` kind (`Kind::Spectro`, appended), standing under a cuvette as a
balance stands under a vessel (`stands_under`: one rule for both); `insert` (a cuvette only, nothing on its mouth),
`remove`, `tune`, `blank`; the sample at the beam's height (`spectro_sample`: the column's layer there, air below the
liquid, opaque at a sediment); the true wavelength the one set plus ±2 nm from the seed. Saved (`spectro`: its
wavelength, its blank's signal and wavelength), not digested. `note`. The report's JSON.

**Conditions**: `absorbance`, `nm` (quantities of the instrument), `noted` (`noted_value`), `recorded_count`; the
tutorial's own `predicted`.

**The screen** (`ui.cpp`). The case, well and two displays as bodies; a cuvette drawn 30 px across; the slider (the
valve's, mapped over 380–780 nm), ±1 nm, blank, "+ A"; the notebook's write row; predictions in the readout's place
while nothing is selected; "done" and "next" in the experiments list; `Ui::progress`, `set_progress`.

**The C API and the host**: `cg_ui_progress`, `cg_ui_set_progress`; `ChemLab.progress_path`, `user://progress.json`,
loaded at start and written whenever it changes.

**The experiment** `beer` (10 steps): a prediction, the instrument, 750 nm, the blank, four standards, the unknown.

## Measured

- A 0.0499 M copper sulfate solution reads A 0.511 against Beer and Lambert's 0.510 at 750 nm; six standards from
  0.01 to 0.10 M lie on a line through zero within 0.003, its slope ε within 0.5 %.
- Stray light: an ideal 0.5 reads 0.499, an ideal 2.5 reads 2.453; past about 3, `OL`.
- Water blanked at 750 nm and read at 600 nm without a new blank: 0.223, the lamp's own fall between them.
- A cuvette filled below the beam reads the air against a water blank: −0.012.
- `beer`: the standards' slope 10.12 L/mol (R² 0.999999, intercept 0.0005) against NBS's 10.23; the unknown 0.0363
  mol/L against its true 0.0362.

**Tests**:

| Test | What it checks |
|---|---|
| `test_spectro.cpp` (2) | Beer and Lambert's line through zero, twice the path twice the absorbance, the blank zero; stray light's flattening, `OL`, the displays, the lamp's slope after a wavelength's change, a steep band's side |
| `test_spectro_lab.cpp` (3) | a cuvette alone fits; blank, read, tune, saved and loaded; the air under the beam; on the screen the slider, ±1 nm, a refused blank, a cuvette carried into the well |
| `test_experiments.cpp` (4) | `beer` strict (the line, the unknown, the report, the prediction beside it, progress and the list's marks), at a person's pace, free; progress kept and refused |

## Open

- **A cuvette's glass is not in the beam**: a blank in the same kind of cell would take it off; matched cells are
  assumed (§20).
- **Prepared standards**: the bench's cuvettes come filled; making them by dilution is experiment 2's (Stage 52).
- **The rates, the half-life and the solubility curve** (curriculum 4, 36, 37, 45) are the next stage's, on this
  instrument and the notebook's plot.
