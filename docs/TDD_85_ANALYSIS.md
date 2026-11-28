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
| 85.2 | Experiments 44 (Ksp of calcium hydroxide, a common ion), 52 (water's hardness by EDTA), 43 (Kc of FeSCN²⁺; small pipettes), 42 (Le Chatelier); the activity model's range bounded (found by 44) | |
| 85.3 | A wet cake, a porcelain crucible, a drying oven; experiment 10 (gravimetric sulfate or chloride) | |
| 85.4 | Experiment 12 (qualitative analysis, flame tests); a test-tube rack | |
| 85.5 | Discharge lamps (H, He, Hg, Ne); experiment 54 (atomic spectra, the Rydberg constant) | |
| 85.6 | Documents, `make gates`, the user's sign-off | |

## Open

- **Calcium chloride boiled down** loses its temperature (T3) near dryness, as it did before this stage (its water held
  at the range's edge, heated on); lithium chloride, held there too, keeps every invariant. A solution held at the
  edge heats on with its water: boiling past the activity model's range is not modelled.
