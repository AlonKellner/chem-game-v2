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
| 81.3 | FeSCN²⁺'s and CoCl₄²⁻'s constants in the model's activities (measured quotients, data being read) | |
| 81.4 | EDTA and Eriochrome Black T (data being gathered) | |
| 81.5 | Flame emission: lithium and strontium, the NIST lines and the flame's bands | |
| 81.6 | The lab: reagents, the calculator's formulas, `chem_tool study complexes` | |
| 81.7 | Documents, `make gates`, the user's sign-off | |

## Open

- Debye and Hückel alone for a complex within Pitzer's model takes a highly charged ion's activity too low (no pair
  of its own): FeSCN²⁺'s quotient at 0.4 ionic strength 43 against Below et al.'s 146 with the zero-ionic-strength
  constant (81.3 calibrates it).
- CoCl⁺, CoCl₂ and CoCl₃⁻ are left out (with Pitzer and Mayorga's CoCl₂ pair, fitted as wholly dissociated, the four
  did not settle in strong hydrochloric acid); Zn(OH)₂(aq) and Al(OH)₃(aq), neutral and minor, likewise.
