# TDD 07: The demo, headless (Stage 7)

## The request

> Please plan the full reimplementation of this game, where the definition of done is the minimal
> example, specifically the 3-stage interactive chemistry demo specified in the original repo,
> where CO₂ is extracted from Malachite, then create a Copper Sulfate solution, and finally
> recover recover pure water and grow Copper Sulfate crystals.

Stage 7 ties the systems together. The three stages run end to end as one session, played through
the C API by a script that does what a player would, with every invariant checked on every step.
There is still nothing to see: this is the demo's physics and its commands, complete, before any
graphics.

| Phase | Topic | Status |
|---|---|---|
| 7.1 | The lab layer: a bench as data, items over one closed world (`lab/lab.hpp`) | Done |
| 7.2 | Commands: heat, unheat, valve, stir, pour, tip, attach, detach, under, submit, wait, wait_until | Done |
| 7.3 | Refusals with reasons; the sealing rule at the level of equipment | Done |
| 7.4 | Reports and the SHA-256 digest of the whole state | Done |
| 7.5 | The lab through the C API | Done |
| 7.6 | `chem_tool run` and `bench`; sessions and their golden records | Done |
| 7.7 | The demo's bench and script; the measured results | Done |
| 7.8 | Invariants D1, D4, R1, L1; the step cost | Done |
| 7.9 | The user's sign-off | Pending |

## Decisions

- **Everything on the bench exists from the start.** The jar of malachite, the acid bottle, the
  balloon and the waste beaker are cells of the one world, so the world is closed from the first
  step and nothing is created in play. A command connects, disconnects or moves; none makes matter.
- **Commands are JSON objects.** One function carries every action across the C API
  (`cg_lab_command`), a session file is a list of them, and a host needs no binding per action.
- **`wait_until` is the player's eye.** The script does not know when the malachite is gone; it
  watches for it, with a timeout. A change to the physics moves the times, not the script.
- **A refusal changes nothing, with one stated exception.** Connections change at step
  boundaries, so a command that changes them first finishes a step in progress: at most 1/60 s.
- **An edge may be added during a step.** A step's trace holds the edges as they are at its end,
  so one added during it is among them and C7 still means what it says. Removing one mid-step
  stays refused. Without this, two pours at one boundary could not both get their connection.
- **A solid's particles move with it.** Tipping 2 g of the jar's 5 g takes two fifths of its
  grains.
- **Unchecked steps build no trace.** `Simulation::advance()` runs the same processes without
  copying the state. The digest is the same either way (D1 tests it), and the unchecked run is
  what a game frame will use.
- **The golden is a digest per command, not a dump.** 26 lines. A difference names the command
  where the session first departs from its record.

## Evidence

`chem_tool run data/sessions/demo.json` (`make demo`): 26 commands, 156,302 steps, 43 minutes of
simulated time, every invariant on every step.

| Handed in | Measured |
|---|---|
| The balloon, sealed | 0.896 g of CO₂, 64.8% of its gas, in 756 mL; 0.354 g of water condensed inside |
| The beaker | 40.87 g of water; nothing else but air |
| The flask | 8.935 g of CuSO₄·5H₂O in 447,472 crystals of 0.26 mm: 79% of the copper, 99.977% of the flask's solids. The rest is 2 mg of malachite that never decomposed |

Timeline: malachite gone at 229 s; flask below 30 °C at 566 s; oxide dissolved at 785 s; 10 g of
water left at 1043 s; flask at 21 °C at 1645 s; crystals left to grow until 2545 s.

Conservation, start to end, as integers: C, Cu, H, N, O, S and Si atoms, and the energy
(−13,316,534,030,943,915 quanta), are identical. The world's mass is 29,723.986252968 g at both
ends (most of it the room's air).

`chem_tool bench` (`make demo-bench`): 4.5 µs a step, 3700 times real time, 2.7 ms a frame at
600 steps a frame.

## Compared with the original specification

| Original | Measured here |
|---|---|
| "Captured 2g of Carbon Dioxide", balloon 1.2 L at 95% CO₂ | 0.90 g, 0.76 L, 65%: 5 g of malachite holds 0.995 g of CO₂, and the flask's air goes into the balloon first |
| Decomposition at 200 °C | Appreciable from about 300 °C |
| 100 mL of acid | 50 mL: 100 mL would leave more than half the acid in the liquor |
| Crystals form while boiling | They form on cooling |
| "Purity: 99.8%" | 99.977%, and it is a measurement: the impurity is undecomposed malachite |

## Known limits

- **The step cost is at the budget.** 4.5 µs of 5 µs. The reaction loop offers every reaction to
  every cell each step, and settling rebuilds the gas tree each step; both can be cached. It is
  worth doing before Stage 9 puts a frame around it.
- **The purity counts solids and liquids left in the flask.** Decanting is complete, so no acid
  film stays on the crystals (GAME_SPEC §20).
- **Stage 8's remaining parts:** placement on a grid, a balloon's capacity, overflow, save and
  load. None is needed to run the demo; all are needed to play it.

## Tests

- `lab: …` (9 cases): SHA-256's standard vectors, the bench as opened, malformed benches, 19
  refusals, sealing and single mouths, exact pours and tips, the same state however grouped and
  whether or not checked, the golden record with the demo's success criteria
- ctest `demo_session`: the session against its record, through `chem_tool` and the C API
- `capi_c_smoke`: a lab opened, commanded, refused and reported from C
