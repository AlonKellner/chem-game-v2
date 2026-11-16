# chem-game-v2

A chemistry lab game in which simulated materials follow mechanical, physical and chemical laws.
Conservation of elements, mass, charge and energy is built into the simulation's two state-changing
operations and checked as exact integer invariants.

The game is engine-free C++20 behind a C API, hosted by Godot 4.7 through one GDExtension node.

- Design: [GAME_SPEC.md](GAME_SPEC.md)
- Working conventions: [CLAUDE.md](CLAUDE.md), [docs/WORKFLOW.md](docs/WORKFLOW.md)
- Physical data and sources: [docs/DATA_SOURCES.md](docs/DATA_SOURCES.md)

## Status

The 3-stage demo of GAME_SPEC §17 is playable with the pointer (`make run`) and by script
(`make demo`), and what is in the glassware is drawn by a spectral GPU pass (GAME_SPEC §12).
Still to come: refraction (Stage 11) and the demo's final sign-off (Stage 12). Each stage's notes
are in `docs/TDD_NN_*.md`.

## Build

Requires CMake 3.25+, a C++20 compiler, Python 3 and Godot 4.7 (`godot` on the PATH).

```bash
git clone --recurse-submodules <this repository>
make check     # engine-free build and tests
make native    # the Godot extension (the first build compiles godot-cpp)
make run       # the game
make gate      # every commit gate
```
