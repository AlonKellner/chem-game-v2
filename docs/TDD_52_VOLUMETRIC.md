# TDD 52: Volumetric glassware and the calorimeter (Stage 52)

## The request

The curriculum's step A, its second half (`docs/CURRICULUM.md`), after Stage 50's sign-off: the user's "Looks
good! Approved, merge and proceed" (2026-11-25). Stage 50 measured mass; this stage measures volume to a class's
tolerance, and heat with a real thermometer.

Its number is 52: another agent's Stage 51 (the vertical dimension) was in progress on main's next number.

What it builds:

- **Volumetric glassware**, each to its ISO standard's rows (`data/glassware.json`): the burette (ISO 385), the
  bulb pipette (ISO 648), the volumetric flask (ISO 1042), the measuring cylinder (ISO 4788). Each piece's true
  volume at its mark, or its scale's error, is drawn once from the bench's seed within its class A tolerance.
- **A vessel with an outlet at its foot** (the burette, the pipette): it stands on another vessel's mouth, its tip
  in the neck (`attach`), and drains into it through its tip at Torricelli's rate under its own head, as far
  open as its stopcock (`cap`: a control, a slider, rigged, followed by a controller). The tip's bore is what gives
  the standard's own delivery time.
- **To the mark** (`fill {item, from}`): a pipette draws its mark's volume from a vessel; a volumetric flask is made
  up to its mark from one. The true volume at the mark is the piece's own.
- **A foam-cup calorimeter**: expanded polystyrene, a new wall species; its heat lost through its wall by the
  foam's conductivity.
- **A thermometer's lag**: by instruments, a thermometer reads its vessel through its own response, first order
  with a probe's time constant.
- **The experiments**: a standard solution and its dilution (curriculum 2), a titration with the burette (13), the
  heat of neutralisation in the calorimeter (22).

## The data

| What | Value | Source |
|---|---|---|
| Burette 50 mL, class A | ±0.05 mL; divisions 0.1 mL; scale 500–600 mm; at most 820 mm long; zero line 50 mm from the top, the lowest line 50 mm over the stopcock, the jet 50 mm under it | ISO 385:2005, Tables 1 and 2 |
| Its delivery time, class A | 60–100 s | ISO 385/2-1984 |
| Its outside diameter | 13 mm | a class A burette's catalogue (ASTM) |
| Bulb pipette 25 mL, class A | ±0.03 mL; at most 530 mm long; bulb at most 24 mm; delivery tube 7 ± 1 mm across, at least 220 mm long; suction tube at least 170 mm; inside at the mark at most 5.5 mm; wall at least 1.0 mm | ISO 648-1977, Tables 1–3 |
| Its delivery time, class A | 25–50 s | ISO 648-1977, Table 4 |
| Volumetric flask 100 / 250 mL, class A | ±0.10 / ±0.15 mL; neck inside at the mark 12–14 / 14–17 mm; height 170 / 220 mm; bulb 60 / 80 mm; base 40 / 55 mm; the mark in the neck's lower two thirds | ISO 1042-1981, Tables 1–3 |
| Measuring cylinder 10 / 25 / 100 mL, class A | ±0.1 / ±0.25 / ±0.5 mL; subdivisions 0.2 / 0.5 / 1 mL; at most 140 / 170 / 260 mm high; the top at least 20 / 25 / 35 mm over the highest line | ISO 4788:2005, Table 1 |
| The 100 mL cylinder's outside diameter | 29 mm | DURAN's class A cylinder |
| A thermometer probe's response | t90 10 s in stirred water: τ = 10 / ln 10 = 4.3 s | Vernier TMP-BTA's datasheet (a liquid-in-glass thermometer: 2–6 s, NBS Bulletin 8) |
| Polystyrene foam | conductivity 0.035 W/(m·K), heat capacity about 1.17 J/(g·K) near 293 K | NIST's cryogenic material correlations |
| An 8 oz foam cup | 81 mm across its top, 51 mm its base, 89 mm high; its wall about 2.5 mm; about 2.6 g | a cup maker's listing; J. Chem. Educ. 2025, 102, 1281 |

## The goldens

- 2026-11-25, approved by the user: a mouth's diffusion path (Stage 50) is integrated over 1000 slices, not 32 (the
  pipette's and the burette's bores change too fast for 32), and its table is 257 fills, not 65. Every open vessel's
  exchange is refined: the demo's open beaker ends with 37.786999 g of water, not 37.787113 g; its crystals 7 µg
  heavier; the pressure session's results unchanged to their printed places. Both records re-recorded.

## Phases

| Phase | Topic | Status |
|---|---|---|
| 52.1 | GAME_SPEC; the glassware's rows, the foam, the probe; their sources | |
| 52.2 | The profiles: cylinder, volumetric flask, pipette, burette, cup; marks; the tip's bore from the delivery time | Done |
| 52.3 | The lab: an outlet vessel on a mouth, its drain through its stopcock; `fill` to the mark; each piece's tolerance from the seed; the burette's reading | |
| 52.4 | The thermometer's lag; the volumetric readings | |
| 52.5 | The screen: tall glassware drawn to fit, the stopcock, the gestures, the readout | |
| 52.6 | The experiments: a standard solution, a titration, a calorimeter | |
| 52.7 | This document, CHANGELOG, `make gates`, the user's sign-off | |
