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
| 55.1 | GAME_SPEC; the instrument's data | |
| 55.2 | `lab/spectro.hpp`: the slit, the stray light, the lamp; the sample's transmittance; against Beer–Lambert | |
| 55.3 | The lab: the kind, the cuvette, insert, tune, blank; the beam's height; saved, not digested | |
| 55.4 | The screen: the case, the well, the display; the readout's controls; a cuvette carried onto it | |
| 55.5 | The notebook's written entries; the plot's pairing | |
| 55.6 | Predictions; progress and the list's marks; the C API and the host | |
| 55.7 | The experiment: Beer–Lambert's line and an unknown | |
| 55.8 | This document, CHANGELOG, `make gates`, the user's sign-off | |
