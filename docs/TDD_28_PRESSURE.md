# TDD 28: Pressure, and the event parts (Stage 28)

## The request

Chosen by the user, 2026-11-22: the pressure work, with the event systems' fixes:
- the writer writes on its input turning on and off, or on a change; the notice part filters by a
  text the notice contains ("more flexible than a type") and signals for a duration after one;
- sealed vessels build pressure; a stopper pops, glass bursts (the user's choice: "stopper pops,
  glass bursts"); a partial vacuum by cooling a sealed vessel (no pump); the balloon's tension later;
- with it: the debug panel's gaps (pours as pours; signal parts from the inside), a material's
  volume as a quantity, a controller tuned again as what it controls changes.

GAME_SPEC §6 (sealed cells), §8 (pressure shifts equilibria), §13 (sealing, what gives way), §13.1
(the screen), §13.3 (quantities, controllers, the editors), §13.4 (notices, the two parts), §13.6
(the debug panel), §18 (V3, T1), §20; docs/DATA_SOURCES.md (*boiling at a pressure*; *Glass and
stoppers under pressure*).

| Phase | Topic | Status |
|---|---|---|
| 28.1 | Spec | Done |
| 28.2 | The core: sealed cells (`E = U + P_atm V`, their pressure, their equilibria at it), groups sealed together, V3 the mark, T1 between them; a starved cell not sealed | Done; records unchanged |
| 28.3 | The lab: the stopper, the screw cap (a control), glass bursting, pressure in the view; nothing poured in or out of what is sealed; the detectors | Done |
| 28.4 | The event parts: the writer's `writes`, the notice part's `contains` and `seconds` | Done |
| 28.5 | Controllers tuned again, held at a limit; a vessel's pressure and a material's volume as quantities | Done |
| 28.6 | The debug panel: pressure, pours, signal parts | Done |
| 28.7 | The screen: the stopper, the cap's button and body, the readout, the editors, the cabinet's shelves | Done |
| 28.8 | A session recorded (`data/sessions/pressure.json`) | Done |
| 28.9 | The user's test and sign-off | |

## Decisions

- **A sealed cell stores `E = U + P_atm V`.** Its internal energy plus the room's work on its volume.
  With condensed phases incompressible and the gas ideal, `E = H(T) - n_gas R T + P_atm V_free`: at
  an atmosphere and full it is the cell's enthalpy, which is what every open cell stores. So no
  stored integer changes, every record is unchanged, and a cell sealed or opened at the room's
  pressure keeps its temperature. Heat raises a sealed cell's temperature by its heat capacity at
  constant volume.
- **Opened, a sealed cell reads as an open one** at once: its `E`, as an enthalpy at an atmosphere,
  is what a sudden expansion against the room leaves; the settling then makes it full within the
  step. The gas left in a real vessel would cool more, the gas let out less: declared (§20).
- **Sealed only at the room's pressure.** A random world sealed a half-full cell and read the room's
  work on the unfilled volume as its energy: below 0 K. A real stopper goes into a vessel at the
  room's pressure, so the world refuses to seal a cell that is not full there (a starved one), with
  `WouldSeal`, which now means that.
- **Cells sealed together** share one pressure: each step, after their equilibria, gas runs from the
  higher pressure to the lower, carrying its enthalpy, by a bisection on the giver's share, deepest
  first, until none runs (eight rounds at most). T1 holds it to the direction.
- **The probe at fixed `E`.** A sealed cell's affinity along an extent follows its temperature at
  fixed `E` (`Cv(x) T(x) = Cv T - x (P_atm dV + dHf - dCp T0)`) and its free volume (what the extent
  makes takes room from the gas). A first version forgot the free volume's step: the sweep found a
  sealed cell left 0.012 J/mol off its equilibrium (P1), exactly the room's work on the volume a
  liquid-to-solid reaction freed.
- **No drain runs into or out of a sealed cell**: no gas could make way for the liquid.
- **The stopper holds by a force** over its neck's bore, so one stopper pops at the pressure each
  neck gives it. Calibrated (no source found): 30 N, +37 kPa in the flask's 32 mm neck. Glass bursts
  at a calibrated pressure per kind. Searched 2026-11-22; `docs/DATA_SOURCES.md`.
- **The screw cap is a control** (0 on, 1 off), like the upside-down cap, so a rig or a controller
  sets it; a pour's start or end forgets the cap's last setting (G1 caught it: the cap upside down
  and the cap upright are two controls on one item).
- **A sealed vessel boils** while its water goes to steam under a gas that is nearly all steam, not
  at a temperature: cooled, it condenses (not boiling); warmed a little, it boils below 100 °C. Its
  vapour's room is small, so the threshold is 10 µg a reading.
- **Controllers tune again** once what they control has changed by a tenth (mass, heat capacity) or a
  material came or went, not while a pour into it runs (a scripted pour re-tuned five times on the
  way), with no bump. The trial's copies never tune their own.
- **The notice part's text** matches whatever the case; its seconds hold from the step it hears a
  notice, another restarting them. A part as before digests as before.
- **The tray's spill** counts all it holds, gas too: a burst's hot water condensing in it was read as
  a spill every quarter second.

## Measured

- Water sealed alone with its vapour, against NIST (IAPWS-95): 9.817 kPa at 318.656 K (NIST 9.847),
  19.70 at 332.990 (19.80), 48.93 at 354.179 (49.43), 98.82 at 372.876 (100.43), 191.7 at 392.820
  (196.6): the model 0.3% to 2.5% low, falling further as it warms (steam ideal, its heat capacity
  constant).
- A sealed flask of boiling water cooled to 60 °C: 19.84 kPa (NIST 19.95).
- A sealed flask of air heated: its pressure over its temperature constant to 1e-6, the heat taken
  its constant-volume capacity's.
- Two flasks sealed together at 2 and 0.5 atm: 126.7 kPa both, 254.5 K and 359.8 K (the gas left
  behind expands, the gas pushed in is compressed).
- The pressure session: the flask boiled, stoppered and cooled to 70 °C; warmed, it boils sealed;
  heated, the stopper pops at 138 kPa (63 °C with air, 109 °C with steam alone); the capped bottle of
  200 g of water bursts at 401 kPa and 130 °C, after 278 s at the burner's full.
- A controller holding 80 g of water at 60 °C, 80 g more poured in: tuned again once, three seconds
  after the pour began (its end); held within a degree after.

## Open

- The balloon's tension (its overpressure against its stretch, rising then falling), declined for
  now by the user; a vacuum pump; siphons; glass imploding under a vacuum.
- A stopper's hold and glass's burst are calibrated, not measured: no source was found.
