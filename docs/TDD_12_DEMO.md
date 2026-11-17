# TDD 12: The demo, signed off (Stage 12)

## The request

> Definition of done: the 3-stage interactive demo (CO₂ from malachite → copper sulfate solution →
> pure water recovered and copper sulfate crystals grown), physically honest, with conservation laws
> enforced as invariants, rendered by a GPU optical pipeline fast enough for a real game.

Stage 12 checks the whole demo once more, end to end, with the final renderer (Stages 10 and 11),
through every way it can be run. Then, at the user's request:

> Make the demo into a tutorial with step by step instructions and checks, and a way to complete
> the tutorial successfully

it turns the demo into a tutorial (GAME_SPEC §13.2), and hands it to the user to play and sign off.

| Phase | Topic | Status |
|---|---|---|
| 12.1 | The demo headlessly, through the C API (`make demo`): GAME_SPEC §17's "done when", item by item | Done |
| 12.2 | The sessions against their golden records (`make sessions-check`) | Done |
| 12.3 | The demo through the screen and on the GPU (`make screenshots`): five moments, O4, the pass's cost | Done |
| 12.4 | The stage-close gates (`make gates`: uncached, ASan and UBSan) | Done |
| 12.5 | The tutorial (GAME_SPEC §13.2): `lab::Tutorial`, `data/tutorials/demo.json`, the card and the pointer on the screen, `cg_ui_tutor`/`cg_ui_tutorial`, the Godot host's `tutorial_path` | Done |
| 12.6 | The user plays it (`make run`) and signs off | Pending |

## What it measures

`make demo` (the session `data/sessions/demo.json`, every invariant checked every step):

- **Stage 1:** the balloon is handed in sealed, 755.8 mL of gas: 0.896 g of CO₂ (64.79% of its gas),
  0.354 g of water condensed inside.
- **Stage 2:** the oxide dissolves in 50 mL of 1 M sulfuric acid, stirred over a low flame.
- **Stage 3:** the beaker holds 40.868 g of water and nothing else but air; the flask holds 8.935 g of
  CuSO₄·5H₂O, 99.977% of its solids and liquids (the rest is 2 mg of malachite that never
  decomposed), in 447,472 crystals of 255.5 µm.
- **Conservation:** carbon, copper, hydrogen, nitrogen, oxygen, sulfur, silicon and the energy are the
  same integers at the end as at the start (energy −17,865,742,609,077,165 quanta of 1e-9 J both);
  the mass 30,023.986252968 g both.
- `make sessions-check`: the session reproduces its golden record, digest for digest.

`make screenshots` (Apple M3 Pro, Metal, 1920×1080): the demo played with the pointer, at five
moments the optical pass alone, settled, against the truth on the eye metric (O4):

| Moment | Pixels in bodies | Largest difference | Visible |
|---|---|---|---|
| Set up | 37,946 | 2 | 0 |
| Decomposing | 52,024 | 2 | 0 |
| The pour panel | 59,822 | 2 | 0 |
| Boiling | 66,726 | 2 | 0 |
| Crystals | 66,726 | 2 | 0 |

The pass: 0.11 ms settled, 1.0 ms when every covered pixel's history starts at once.

## The tutorial

`data/tutorials/demo.json`, over the interactive bench (everything starts in the cabinet): 28 steps
in the demo's three stages. Each says what to do and why, and is passed when its checks hold, read
from the lab after every frame and gesture. A run ends in one of two ways:

- **Complete:** every step passed and the goal (§17's "done when") holding. The card then reports
  what was made, measured.
- **Failed**, until a reset: the balloon burst; acid on the malachite before it broke down; the
  crystals overheated; the balloon handed in without its CO₂, the beaker with more than water, the
  flask with its liquor or without its crystals; anything else handed in; an invariant stopping the
  lab.

Tests (`native/tests/game/test_tutorial.cpp`):

- **Played from its own data:** every step's gestures are played through the screen with the
  pointer, then the lab is waited on, step by step, to completion. 28 steps, 28 gestures, 1,650 s
  of the lab. Measured at the end: 0.896 g of CO₂ in the balloon (64.8% of its gas), 41.271 g of
  water in the beaker, 9.038 g of crystals, 99.980% pure, 460,013 crystals of 0.25 mm. Every
  element, mass and energy unchanged. Reset starts it again.
- **The pointer** points at the step's gesture, at the panel's button once the panel is open, and
  at nothing while the lab is waited on; a step done early is passed when reached.
- **Three failures** played (the jar handed in, acid before the decomposition, the beaker with
  acid), each ending the run until a reset.
- **Every kind of condition** is checked on a view built by hand, and bad tutorials (unknown
  items, keys, places, species, a species without an amount, no checks, a bad gesture) are refused
  with the reason.

The Godot host opens it by default; its GUT suite checks that it follows a drag. `make screenshots`
shows the card at each moment, with O4 unchanged (worst 2, none visible).

One gesture changed on the screen: a condenser dragged off the flask while the beaker stands under
its outlet is left beside the flask. The lab does not take the drain away, so a move along the
bench is refused. The screen no longer shows that refusal, since the condenser did come off.
The lab, the sessions and their golden records are unchanged.

## Not done, and said so

- **The debug panel** (GAME_SPEC §13.1, §16: rates, heat flows, true composition), single-stepping
  and export from the screen are specified and not built; the readout shows each item's
  composition, and `chem_tool` reports everything.
- **Data not yet verified** (`docs/DATA_SOURCES.md`, the unverified draft): the trihydrate and the
  monohydrate, which no code uses (the demo dehydrates in one step); every optical spectrum but
  water's is calibrated to the substance's colour; the malachite rate's prefactor and three other
  rate constants are calibrated for play.
- **A drain is never taken away** (found while writing the tutorial): once a vessel stands under a
  condenser's outlet, the condenser cannot be moved along the bench, and a beaker handed in stays
  joined to the outlet by its drain. Fixing it in the lab changes the sessions' digests, a
  behaviour change for the user to approve; it is left as it is.
- **The repository has no licence** file.
- What GAME_SPEC §20 declares (the demo's simplifications) stands.
