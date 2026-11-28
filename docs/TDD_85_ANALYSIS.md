# TDD 85: Salts, precipitates and complexes: step E's experiments (Stage 85)

## The request

The curriculum's step E, its experiments (`docs/CURRICULUM.md`: 10, 12, 42, 43, 44, 52, 54), after its chemistry
(Stage 81, complexes; Stage 82, flame emission). Scoped by the user (2026-11-28):
- **"Seven now, 51 later"**: experiment 51 (alum from aluminium foil, the copper–ammonia salt precipitated with ethanol)
  needs a dissolving metal with hydrogen and ethanol as a second solvent, steps F and I; it moves after them.
- **"Choices in the report"**: an answer that is no number (which ions; which way an equilibrium shifts) is picked
  from a list on the card and checked against the truth; qualitative unknowns drawn by the bench's seed from a list.
- **"Wet cake, oven, crucible"**: a drained residue holds the water of its pores, dried in a porcelain crucible in a
  drying oven to constant mass.
- **"H, He, Hg, Ne"**: discharge lamps, NIST's lines, seen through the spectral viewer; hydrogen's Balmer lines and
  the Rydberg constant.

## The design

- **A report's choices** (GAME_SPEC §13.2; `TutorialReport::choices`, `single`): each choice a label and a truth, an
  expression over the lab's quantities that is above zero where the choice holds. The card shows them as chips; right
  where the picks are exactly the choices that hold. `report:K:I` locates a chip.
- **An unknown from a list** (`one_of`): a vessel's contents one of a list, chosen once by the bench's seed
  (`instruments::share(seed, "<item>:one_of")`); saved as the world it made.
- **The activity model's range** (GAME_SPEC §9; found by experiment 44): no fast equilibrium carries a solution past
  an ionic strength of 20 (`within_range`: along an extent the strength is K (A + B x) / (L + D x), so the bound is
  exact); P1 reads the same bound. A filter funnel's first drop of limewater, a milligram in an empty beaker's air,
  dried to a quantum of water, where Pitzer's sums diverge (Ca–OH's ln gamma near −3.5e5): the film's entropy was no
  function of its state, and the next drop, diluting it, lowered the world's entropy (T2, −0.037 J/K). Two other
  remedies were tried and dropped: the excess held at the range's edge along the composition's direction (exact
  Gibbs–Duhem, but a kink at the edge jumps the water's activity: three solution tests failed), and a fast precipitate
  standing past the range (the film's dilution, not its precipitate, was what lowered the entropy).

## Phases

| Phase | Topic | Status |
|---|---|---|
| 85.1 | A report's choices; an unknown from a list | done |
| 85.2 | Experiments 44 (`ksp`), 52 (`hardness`), 42 (`lechatelier`); the activity model's range bounded (found by 44); the fast equilibria's rounds 256 to 1024 (found by 52); a drained pipette refills; 5 and 10 mL pipettes (ISO 648; the 1 and 2 mL rows not made: in one wall a delivery tube 5 mm across holds more than 1 mL) | done |
| 85.3 | Iron(III)'s Pitzer pairs and FeSCN²⁺'s fitted to Below et al.'s two quotients (the user's choice: "Fix the activity"); experiment 43 (`kc`) by Benesi and Hildebrand in their medium (the user's choice, the standard past any measurement) | done |
| 85.4 | A filter crucible (sintered glass, in place of porcelain: what chloride is weighed in as silver chloride), its wet cake, a drying oven; experiment 10 (`gravimetric`, chloride) | done |
| 85.5 | Experiment 12 (`qualitative`): twelve salts, their tests, a flame test holding every metal; `flame_umol` | done |
| 85.6 | Discharge lamps (H, He, Hg, Ne), the viewer's lines, `probe_on`; experiment 54 (`spectra`) | done |
| 85.7 | Documents, `make gates`, the user's sign-off ("Approved, merge and proceed", 2026-11-28) | done |

## Open

- **Neon's colour**: NIST's relative intensities, gathered from several sources, give neon's blue-green lines (470–495
  nm) a quarter of its light, and its tube is drawn pink; a low-pressure neon glow is orange-red. A measured spectrum of a
  neon lamp (radiometric) would give its lines their weights.

- **A test-tube rack** (the rest of T6) is not built: tubes stand on the bench by themselves.
- **Experiment 51** (alum; the copper–ammonia salt with ethanol) waits for steps F and I (a metal dissolving with
  hydrogen, ethanol as a second solvent).
- **Iron(III) nitrate as the medium itself** (a textbook FeSCN²⁺ standard, 0.1–0.2 M): past Below et al.'s measurements,
  the model takes up 84 % of the thiocyanate (about 95 % really); experiment 43 is Benesi and Hildebrand's in their
  medium.

- **Calcium chloride boiled down** loses its temperature (T3) near dryness, as it did before this stage (its water held
  at the range's edge, heated on); lithium chloride, held there too, keeps every invariant. A solution held at the
  edge heats on with its water: boiling past the activity model's range is not modelled.
