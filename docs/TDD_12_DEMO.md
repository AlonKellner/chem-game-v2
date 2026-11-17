# TDD 12: The demo, signed off (Stage 12)

## The request

> Definition of done: the 3-stage interactive demo (CO₂ from malachite → copper sulfate solution →
> pure water recovered and copper sulfate crystals grown), physically honest, with conservation laws
> enforced as invariants, rendered by a GPU optical pipeline fast enough for a real game.

Stage 12 builds nothing new. It checks the whole demo once more, end to end, with the final renderer
(Stages 10 and 11), through every way it can be run, and hands it to the user to play and sign off.

| Phase | Topic | Status |
|---|---|---|
| 12.1 | The demo headlessly, through the C API (`make demo`): GAME_SPEC §17's "done when", item by item | Done |
| 12.2 | The sessions against their golden records (`make sessions-check`) | Done |
| 12.3 | The demo through the screen and on the GPU (`make screenshots`): five moments, O4, the pass's cost | Done |
| 12.4 | The stage-close gates (`make gates`: uncached, ASan and UBSan) | Done |
| 12.5 | The user plays it (`make run`) and signs off | Pending |

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

## Not done, and said so

- **The debug panel** (GAME_SPEC §13.1, §16: rates, heat flows, true composition), single-stepping
  and export from the screen are specified and not built; the readout shows each item's
  composition, and `chem_tool` reports everything.
- **Data not yet verified** (`docs/DATA_SOURCES.md`, the unverified draft): the trihydrate and the
  monohydrate, which no code uses (the demo dehydrates in one step); every optical spectrum but
  water's is calibrated to the substance's colour; the malachite rate's prefactor and three other
  rate constants are calibrated for play.
- **The repository has no licence** file.
- What GAME_SPEC §20 declares (the demo's simplifications) stands.
