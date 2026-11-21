# TDD 26: Sensing, drawn and guided; controllers (Stage 26)

## The request

Asked for by the user, 2026-11-21:
- a monitor's sensing as a line of its own kind, placed by the same gesture as wires, not chosen
  from a list; its settings shown only once a vessel is sensed, only what is relevant, chosen in
  steps (kind, material, measure): "placing monitors should be a good, guided and non overwhelming
  experience";
- controllers, the next stage in the queue, chosen as a part of their own, tuned by a target and a
  speed alone (the game finds the gains), built on the new sensing.

GAME_SPEC §13.3 (*Sensing*, *A controller*, *A controller's rig follows its level*, the editor), §18
(G3, G4), docs/DATA_SOURCES.md (*Controller tuning: SIMC*).

| Phase | Topic | Status |
|---|---|---|
| 26.1 | Spec | Done |
| 26.2 | Sensing in the lab: `sense`, `unsense`; quantities by kind; new measures | Done |
| 26.3 | Sensing on the screen: the port, the dashed line, the guided editor; the tutorial | Done; the user's eye: "It works well!" |
| 26.4 | Tuning: the fit, SIMC, the margins (gate test at the source's figures) | Done |
| 26.5 | The controller in the lab: the law, PWM, follow rigs, the trial on copies, G3, G4, save, digest | Done |
| 26.6 | The controller on the screen: drawer, editor, rigging, readout | Done |
| 26.7 | The user's test and sign-off | |

## What was built

- **Sensing.** `sense {part, item}` sets what a monitor or a controller reads (`quantity.item`);
  another item forgets the quantity. `set` reads the sensed item, and one naming an item senses it,
  so every session before sensing reads as it did (goldens unchanged). On the screen: a sensing port
  at the part's foot, a drag onto a vessel, a balloon or a burner; a dashed line (one bar body, dashed
  in the pass: `OpticsBody::dash`).
- **The guided editor**: kinds with something in them (`kinds_of`), a kind's quantities
  (`quantities_of_kind`: a material's from what the item holds now, never its wall), a material's
  measures (`measures_of`: g, mol, purity; count and size for a solid); a list of one is chosen for
  the player. The comparison, or a controller's speed and target, only once the quantity is chosen.
- **Controllers.** A part's level `Kc (e + I / Ti)` within 0 and 1, every step, its integral kept
  from winding up at a limit; 0 while gated, unset or untuned. Rigs from it follow: a valve to the
  level, stirring by PWM (a period a tenth of the process's time, 10 s to 10 min), acting only where
  the control differs, with no notice each step.
- **Tuning by trial.** On set, rig or unrig: the lab saved and loaded twice, the controller scrapped
  in each (its rigs with it), its controls held full on in one and off in the other; read each
  second until the difference reaches half again the distance to the target, levels off, or an hour
  passes. `fit_process` (least squares over delays and time constants, and an integrator); `simc`
  (gentle `T`, normal `T / 4`, fast `T / 20`, never under the delay; the delay at least two steps).

## Measured

| Water in the flask held at 60 C | Within a degree after | Overshoot | Then within |
|---|---|---|---|
| gentle | 1571 s | 1.1 C | 0.02 C |
| normal | 624 s | 1.2 C | 0.001 C |
| fast | 162 s | 1.7 C | 1e-10 C |

The trial saw the flask as first order: 366 C for the whole valve (the start of it: boiling is past
the trial), over 381 s, no delay. Played by the pointer at 600x, the flask holds 60.0 C, the valve at
8%. SIMC's gains give the source's margins: π and 61.4 degrees for a lag no longer than 8 theta, 2.96
and 46.9 for an integrator (`test_control.cpp`).

## Found on the way

- **Half a part unseen** (the user, 2026-11-21): the screen's squares listed seven bodies each; two
  signal parts side by side straddle one, and their faces, lamps and ports (and a dashed line drawn a
  dash a body) left the first drawn out. Now fifteen, a dashed line one body
  (`test_wall_pixels.cpp`).
- **Only one part seen through a held vessel** (the user, 2026-11-21): a stack lists the bodies behind
  a held vessel, the nearest first, and the GPU's reading of it stopped at the sixth (`stack_light`:
  six, written in its loop); the CPU kept six. Behind a vessel held over two parts the first six were
  the vessel, a sensing line and the last part's face, lamp and ports: the other part was never read.
  Now `STACK_SLOTS` (12) on both sides; where more stand behind, those behind its own columns first,
  the largest first (a face before its lamp); a slot the rays cross nowhere is passed over unread.
  Carrying the flask over six parts: compose 4.5 ms, the stack 11.7 MB (6.1 at six slots).
  `test_wall_pixels.cpp` holds two rows of three parts behind it: it fails at six. A vessel's pass
  where every history starts again, held over another (`make screenshots`): 28.9 ms, now 32.0 (what
  is behind it is read now, all of it); passing over a thing the rays do not cross is for things that
  bend nothing (a vessel's slot passed over broke the layers' truth: 14.7 on the eye metric).
- **A crash typing a goal's value**: the editor read a valve's per cent past its three settings for
  any focused field from the third on (a goal's are 10 and on); the draft's new field moved what the
  stray read found, to a null. Bounded to the valve's fields.
- **The monitors' cost test** read wall time and failed under the gate's load: now the thread's own.

## Open

- A material's volume and a vessel's pressure as quantities.
- A controller tuned again as what it controls changes (a pour, a reaction): it is tuned when set or
  rigged; its integral holds a changed process, more slowly.
