# TDD 91: Redox in solution (Stage 91)

## The request

After Stage 87's sign-off the user approved "merge and proceed": step F's second part (`docs/CURRICULUM.md`), redox in
solution, C5, for experiments 38 (the iodine clock: initial rates, orders, Ea) and 47 (a redox titration: permanganate;
iodometric). Numbered 91 with the other sessions (83, 88 and 89 chem-game-v2-8d's, 90 perhaps chem-game-v2-6c's).
GAME_SPEC 0.91.0.

| Phase | Topic | Status |
|---|---|---|
| 91.1 | Spec (§7, §9); the sources read and their rows verified | Done |
| 91.2 | Manganese, iodine's forms, the sulfur oxyanions: data and gates | Done |
| 91.3 | The experiments `iodine_clock` (38) and `redox_titration` (47) | Done |
| 91.4 | The sessions' digests (the user's approval), docs, `make gates`; the user's sign-off | gates passed; sign-off open |

## 91.1 What there was

Stage 87's solution law and iodide; Fe²⁺ and Fe³⁺ (Stage 76) with nothing joining them; no manganese, no iodine but
iodide, no thiosulfate, tetrathionate or persulfate; no reaction that moves an electron. The core needs nothing new: a
reaction balanced in atoms and charge (§4.2) carries an electron transfer, its equilibrium its species' Gibbs energies.

## 91.2 The chemistry (`docs/DATA_SOURCES.md`, Redox in solution (Stage 91))

- **Species**: Mn (element 27), MnO₄⁻, Mn²⁺, I₂(aq), I₃⁻, S₂O₃²⁻, S₄O₆²⁻, S₂O₈²⁻ (NBS's rows, read off the scan's pages
  2-52, 2-57 and 2-191; OBIGT's heat capacities and volumes, as Stage 81's ions), and the formula units Na₂S₂O₃,
  K₂S₂O₈, KMnO₄, MnSO₄; Pitzer and Mayorga's Na₂S₂O₃ and MnCl₂; four ions' diffusion (the CRC's).
- **Reactions**: `persulfate_iodide` (the solution law: k calibrated to Jette and King 1929's at their ionic strength,
  Ea Ahmed et al. 1979's 51.39 kJ/mol); `triiodide_formation`, `iodine_thiosulfate` and `permanganate_iron`, fast
  equilibria (triiodide's entropy calibrated within NBS's rounding to Palmer et al.'s K = 698; permanganate's log K 62.8
  from the rows against 62.46 from the potentials).
- **Colours**: permanganate (McBeath, Wilkinson and Graham 2020: their figure's data, fitted per wavelength), triiodide
  and iodine (Pursell and Pursell 2016's SI, read off its vector drawing), each checked against other measurements.
- **Starch** is left out: no measured constant or absorptivity for its blue complex was found (Pesek et al. 2022: "not
  known"); both experiments read triiodide's own colour.

Gates (`native/tests/test_redox.cpp`): the four `measured:` cases; an iodine clock in a kg of solution whose iodine
appears at t = [S₂O₃²⁻]₀ / 2k[S₂O₈²⁻][I⁻] within 3 % (65.35 s against 64.67), every invariant held; permanganate and
thiosulfate each to their end point and no further, a bench settled either side.

## 91.3 The experiments

- **`iodine_clock`** (38): two spectrophotometers at 430 nm; four runs of 3 mL of iodide and thiosulfate with 1 mL of
  persulfate poured in: A (0.050 M iodide, 0.020 M persulfate, 1.0 mM thiosulfate), B half the iodide, C half the
  persulfate (potassium nitrate holding the ionic strength), D as A at 35 C. Each clock time read off the absorbance's
  log: where it leaves nought, less its first reading (recorded at the pour).
- **`redox_titration`** (47): 25 mL of about 0.1 M iron(II) in sulfuric acid titrated with 0.0200 M permanganate to the
  first lasting pink; 25 mL of about 0.05 M iodine in potassium iodide with 0.100 M thiosulfate until its yellow is
  gone.

Played (strict): the clocks A 131.7 s, B 261.5, C 264.2, D (35 C) 67.9; the orders 0.99 in iodide and 1.00 in
persulfate; k 3.80e-3 /M/s at an ionic strength of about 0.11 (Jette and King's 2.08e-3 at 0.025: the salt effect); Ea
50.6 kJ/mol (the source's 51.39: the cooler tube poured into D lowers its first seconds). The titrations: 24.56 mL of
permanganate, 0.0982 M iron(II); 24.70 mL of thiosulfate, 0.0494 M iodine (the bottles' molalities in their volumes).

Each has the three tests of `test_experiments.cpp` (strict with its gestures, as a person, free).

## 91.4 The sessions and the gates

Both sessions' digests move with the table (a new element and eleven species): every printed result unchanged against
main's, line for line, but the conservation table's new manganese row (`Mn 0 -> 0 quanta of atoms`): at 15796da, and
again after main's Stage 88 was merged in, at f7c0a09. Re-recorded with the user's approval. Main's Stage 85 (875355c, iron(III)'s Pitzer
pairs) merged in after: the goldens reproduce, and the redox gates and both plays give the same numbers.

`make gates` on the merged branch: every gate passed (1,408 s of wall at a load of 80 to 100 from other sessions' work).
The step's cost on the demo: 1,340,858 instructions a step, +3.1 % against main's at 875355c: the table's eleven
species and an element more (Stage 87's six gave +2.0 %); the four reactions are skipped while their species are
absent.

## Open

- Starch with iodine (its formation and its blue), for the clock's and the titration's classic end points: no measured
  constant or absorptivity found open.
- Not modelled: triiodide's own oxidation by persulfate (Jette and King: about half iodide's rate), persulfate's ion pair
  with potassium and the counter-ions' specific catalysis (Howells), permanganate's slow decomposition and its reaction
  with Mn²⁺ (the end point's fading), iodide's oxidation by air, thiosulfate's decomposition in acid (experiment 41's
  sulphur, which waits for fine particles' scattering in the optics), the temperature coefficients of triiodide's and
  iodine's absorptivities.
- No diffusion coefficient for I₂, I₃⁻ and S₄O₆²⁻ (none read): they move in a column only by stirring and overturning.
- Step F's third part, metals and displacement (experiments 46 and 11), is Stage 92.
