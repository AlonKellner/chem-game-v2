# TDD 81: Complexes, their colours, and flame emission (Stage 81)

## The request

The curriculum's step E, its second part (`docs/CURRICULUM.md`: experiments 12, 42, 43, 51, 52, 54), as the user split
step E (2026-11-27, "Three stages"), and scoped (2026-11-27): **"All of it"**: complexes and their colours, EDTA and its
indicator, and flame emission; dichromate's colour **calibrated to its look** (no open source measures it in the
visible). Stage numbers: 80 is another session's (a vessel's walls on a grid), so this is 81 and step E's third part 82.

## The design

- **A complex** is a dissolved species formed and broken as a fast equilibrium with its parts: its constant its
  measured one at zero ionic strength, its heat its measured one on the model's own parts, its S° the one that gives
  the constant, its Cp° and V° its parts' (GAME_SPEC §9). Data: `docs/DATA_SOURCES.md`, "Complexes (Stage 81)".
- **Each series step by step, each solid straight into its complex**: amounts are quanta of 10⁻¹² mol, and a free ion
  under a precipitate or a complex is far under one; a complex formed from the free ion alone never takes the solid.
- **The fast equilibria's rounds** from 64 to 256: copper hydroxide dissolving in a little ammonia takes 130 to 250.
- **Colours**: measured tables where open (chromate, the tetrammine, CoCl₄²⁻, FeSCN²⁺), calibrated where not
  (dichromate's 440 nm band, hydrogen chromate, the lower ammines, Fe(SCN)₂⁺'s shape).

## Phases

| Phase | Topic | Status |
|---|---|---|
| 81.1 | The complexes: chromium, thiocyanate, the ammines, the hydroxo complexes, iron(III)'s thiocyanates, cobalt's tetrachloro, chromate and dichromate; their constants gated | done |
| 81.2 | Their colours; gated | done |
| 81.3 | FeSCN²⁺'s and CoCl₄²⁻'s constants calibrated in the model's activities (Below et al.'s Q₁ 146 at 0.40, Lister and Rivington's Q₂ 15.5 at 0.5, Bjerrum et al.'s α₄ 0.496 at 8.23 M HCl) | done |
| 81.4 | EDTA (MINTEQ v4's constants) and Eriochrome Black T (Lindstrom 1959's, calibrated at 0.1 ionic strength; his spectra); a hardness titration turns the dye from wine red to blue; the table's particles 64 to 128 | done |
| 81.5 | The free lab's reagents; the registries counted anew; the goldens re-recorded (digests only), approved | done |
| 81.6 | Documents, `make gates`, the user's sign-off | |

Flame emission (lithium and strontium, the NIST lines weighted by the flame's temperature, the flame's bands, emission
lines in the pass, the truth and the viewer) is a stage of its own: the user's split, 2026-11-27 ("Close 81, flame
next"), after the complexes were done.

## Open

- Debye and Hückel alone for an ion with no pair of its own in Pitzer's model takes a highly charged ion's activity
  too low and falling too fast: FeSCN²⁺'s constant is calibrated to Below et al.'s 146 at 0.40, but at 1.00 the
  model gives about 45 against their 123. A pair for iron(III) with nitrate (none found open) would mend it.
- CoCl₄²⁻'s constant is calibrated to Bjerrum et al.'s α₄ at 8.23 M HCl; about it the model's rise is three times
  steeper (0.0085 at 7.12 M against 0.166, 0.986 at 9.17 M against 0.765): its chloride's activity that far past
  Pitzer's range.
- CoCl⁺, CoCl₂ and CoCl₃⁻ are left out (with Pitzer and Mayorga's CoCl₂ pair, fitted as wholly dissociated, the four
  did not settle in strong hydrochloric acid); Zn(OH)₂(aq) and Al(OH)₃(aq), neutral and minor, likewise.
