# TDD 76: Metals, their salts and precipitates (Stage 76)

## The request

The curriculum's step E, its first stage (`docs/CURRICULUM.md`: C1's metals, C3's precipitates), after Stage 65's
sign-off: the user's "Approved, merge and proceed" (2026-11-27), and the user's split of step E into three stages
(2026-11-27, "Three stages"): **the metals and their salts, each salt's Ksp, precipitates in general** (this stage);
then complexes and their spectra, and emission lines; then the experiments (10, 12, 42, 43, 44, 51, 52, 54) and the
rest of T6.

## What is there now (a survey, 2026-11-27)

- Thirteen elements (H, C, N, O, Na, S, Cu, Ni, Si, Cl, K, P, Br) under a cap of 32; copper's and nickel's ions; a
  precipitate as a fast equilibrium whose solid comes out as grains (`grain_um`: copper hydroxide, Stage 44); soluble
  crystals by the surface law (Stage 64's salts); the room's carbon dioxide (Stage 65).
- No calcium, magnesium, barium, silver, iron, zinc, aluminium or cobalt; no nitrate; no salt of low solubility but
  copper hydroxide.

## The design

- **New elements**: Ca, Mg, Ba, Ag, Fe, Zn, Al, Co (CIAAW's abridged weights).
- **New ions**: Ca²⁺, Mg²⁺, Ba²⁺, Ag⁺, Fe²⁺, Fe³⁺, Zn²⁺, Al³⁺, Co²⁺, NO₃⁻; and the hydrolysis that makes iron(III)
  and aluminium salts acidic, FeOH²⁺ and AlOH²⁺, each a fast equilibrium.
- **Precipitates**, each a fast equilibrium at its measured solubility product, its solid as grains: AgCl, Ag₂O,
  BaSO₄, BaCO₃, CaCO₃ (calcite), CaSO₄·2H₂O (gypsum), Ca(OH)₂, Mg(OH)₂, Fe(OH)₂, Fe(OH)₃, Zn(OH)₂, Al(OH)₃,
  Co(OH)₂.
- **Retired units** for the salts a bench or a calculation names (the chlorides, nitrates and sulfates of the new
  metals, silver nitrate, sodium and potassium nitrate, calcium hydroxide, iron(III) chloride...), each its ions' sum.
- **Data** (`docs/DATA_SOURCES.md`, "Metals and precipitates (Stage 76)"): ΔfH° and S° the NBS tables' (Wagman et al.
  1982); the ions' Cp° and V° OBIGT's (Shock and Helgeson), shifted to the model's footing by their charge (−216.35
  J/K and −16.74 cm³ a unit); each precipitate's S° set so that its solubility product is the measured one (MINTEQ
  v4's log K), its ΔfH° NBS's, as Stage 64 set its salts'.
- **The activity model**: Pitzer and Mayorga 1973's pairs for the chlorides and nitrates (pytzer's transcription:
  Ag⁺, Mg²⁺, Ca²⁺, Ba²⁺, Fe²⁺, Co²⁺, Zn²⁺ and Al³⁺ with Cl⁻ and NO₃⁻; Na⁺ and K⁺ with NO₃⁻), Harvie, Møller and
  Weare 1984's for calcium and magnesium with sulfate, hydroxide and the carbonates.
- **Colours** (`data/optics/media.json`): Co²⁺ pink and FeOH²⁺ yellow-brown from measured molar absorptivities; the
  white precipitates a white powder's reflectance; iron(III) hydroxide's brown, cobalt hydroxide's pink and silver
  oxide's near black calibrated to how each looks.
- **Gates** (`native/tests/test_metals.cpp`): each precipitate's solubility product against MINTEQ's at 25 C and
  across its range of temperature; a saturated calcium hydroxide's pH and calcium; iron(III) chloride's pH by its
  hydrolysis; silver chloride's and barium sulfate's solubility in water and in a common ion.

## Phases

| Phase | Topic | Status |
|---|---|---|
| 76.1 | The elements, the ions, the retired units; the activity model | done |
| 76.2 | The precipitates at their solubility products; hydrolysis | done |
| 76.3 | The colours | |
| 76.4 | The lab: supplies, the calculator's formulas, `chem_tool study precipitates` | |
| 76.5 | The goldens, the documents, `make gates`, the user's sign-off | |

## Open
