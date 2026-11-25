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
- 2026-11-25, approved by the user: the foam cup's wall species appended to the table, so every cell hashes one more
  amount: every digest moves, every measured result of both sessions the same. Both records re-recorded. (And once on
  Stage 51's, merged meanwhile: the demo's results those approved.)

## Phases

| Phase | Topic | Status |
|---|---|---|
| 52.1 | GAME_SPEC; the glassware's rows, the foam, the probe; their sources | |
| 52.2 | The profiles: cylinder, volumetric flask, pipette, burette, cup; marks; the tip's bore from the delivery time | Done |
| 52.3 | The lab: an outlet vessel on a mouth, its drain through its stopcock; `fill` to the mark; each piece's tolerance from the seed; the burette's reading | Done |
| 52.4 | The thermometer's lag; the volumetric readings | Done |
| 52.5 | The screen: tall glassware drawn to fit, the stopcock, the gestures, the readout | Done |
| 52.6 | The experiments: a standard solution, a titration, a calorimeter | Done |
| 52.7 | This document, CHANGELOG, `make gates`, the user's sign-off | `make gates` passed (1098 s: ASan+UBSan 1098, ctest 699, Godot 13 of 13, goldens unchanged); the user's sign-off pending |

## What was built

**The glassware** (`glassware.hpp`, `glassware.cpp`, `data/glassware.json`). Each new kind's profile from its
standard's rows. Where the standard fixes a scale the bore follows from it: the burette's 50 mL over its 550 mm,
in its 13 mm outside diameter; a cylinder's nominal volume up to its highest line. A volumetric flask's bulb is a
true circular arc, its radius the one that puts the nominal volume a third up the neck. A pipette's bulb is as long
as puts the mark 50 mm up its suction tube. A tip's bore is the one that empties the scale (the burette) or the
nominal volume (the pipette) in the standard's delivery time through a sharp-edged orifice. `mouth_path`'s table is
257 fills, each integrated over 1000 slices.

**The lab** (`lab.cpp`).
- An outlet vessel `attach`ed onto a mouth: a drain from it to the vessel (`connect_drain`), the mouth left open;
  `set_outlets` sets its rate each step, `cap` × 0.61 × the tip's area × sqrt(2 g h), h its liquid's level over the
  tip. A pipette opens as it stands; a burette's stopcock is its `cap`, a control (slider, rig, controller).
  `detach` takes the drain off.
- `fill {item, from}`: the liquid with what is dissolved in it, in proportion, from `from` until the item's liquid
  stands at its mark (`mark_ml`: nominal + tolerance × the seed's share).
- The view: a piece's tolerance and divisions; a mark's true volume; a burette's reading from its zero line; by
  instruments the scales' readings with their errors.
- `follow_thermometers`: by instruments each vessel's thermometer a step on, first order; saved, not digested.
- Quantities: `mark_off_ml`, `scale_ml`, `tap`.

**The screen** (`ui.cpp`). Tall glassware drawn no taller than 400 px; a burette's stopcock key and a mark etched
round a neck, bodies of the pass. A pipette or a burette carried onto a vessel is set down there (`fill` or
`attach`), never a pour by tilting. "to the mark" in the pour menu into a volumetric flask (`panel:mark`). The tap's
slider (`cap:<burette>@`). The readout's readings.

**The calorimeter**: `polystyrene_foam`, a wall species (NIST's heat capacity near 293 K); a foam cup's shape; its
heat lost a bench's `loss`, 0.12 W/K, calibrated from the foam's conductivity; its light calibrated.

**The experiments**: `standard`, `titration`, `calorimeter` (10, 12 and 5 steps).

## Measured

- The burette delivers its scale in 80.0 s, half open at half the rate; the pipette its volume in 35.0 s.
- A volumetric flask made up stands at its own mark (99.9x mL at the seed's draw, within ±0.10 mL).
- A thermometer reaches 90 % of a step in 10.0 s; heating at a steady rate it reads the truth a time constant ago.
- `standard`: 2.745 g of the pentahydrate (0.01099 mol) in the first flask; the second holds 25.00 / 100.0 of it,
  by the pipette's and the flask's own volumes.
- `titration`: 0.00129 mol of sulfuric acid pipetted; 25.96 mL of the base to the end point; the pH 9.04 after the
  slow last drops.
- `calorimeter`: the cup 20.0 to 28.0 °C (the textbook's 8.8 K less the cup's loss and heat capacity).
- A frame at 600x, 4.1 ms (Stage 50's).

**Tests**:

| Test | What it checks |
|---|---|
| `test_glassware.cpp` | every new piece within its standard's rows; a mouth's path its table's |
| `test_volumetric_lab.cpp` (4) | the burette's delivery time and its stopcock's share, lifted off, saved mid-delivery; the pipette's mark within its class, drawn and delivered, the water conserved; a volumetric flask made up, a cylinder and a burette within their class (I1); a thermometer's lag under a heating ramp, saved |
| `test_instruments.cpp` | the probe's t90 (the sources' gate) |
| `test_experiments.cpp` (9) | the three experiments strict with their own gestures, at a person's pace, and free |

## Open

- **A pipette delivers all it holds**: a real "to deliver" pipette leaves its last drop and a film, calibrated in;
  here what it drew to its mark is what it delivers (§20).
- **A tip drains as an orifice**: a capillary's viscosity is not in it; the delivery time is held by the bore alone.
- **One time constant** for a thermometer, stirred or not; no thermometer as a body of the pass.
- **The foam cup's conductivity** waits on Stage 53's wall `conductivity`: its loss is the bench's, calibrated from
  the same 0.035 W/(m·K).
- **A foam cup is not white**: the pass's walls only pass and absorb light (a transmittance); a white foam scatters
  it back. The cup lets 0.8 through, pale; a wall that scatters, as bubbles do in a liquid (Stage 45), is the cause's
  fix, in the renderer.
- **No graduations drawn** on a burette or a cylinder: their readings are the readout's.
