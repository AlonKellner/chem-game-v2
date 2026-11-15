# chem-game-v2

A chemistry lab game in which simulated materials follow mechanical, physical and chemical laws.
Conservation of elements, mass, charge and energy is built into the simulation's two state-changing
operations and checked as exact integer invariants.

The game is engine-free C++20 behind a C API, hosted by Godot 4.7 through one GDExtension node.

- Design: [GAME_SPEC.md](GAME_SPEC.md)
- Working conventions: [CLAUDE.md](CLAUDE.md), [docs/WORKFLOW.md](docs/WORKFLOW.md)
- Physical data and sources: [docs/DATA_SOURCES.md](docs/DATA_SOURCES.md)

## Status

Stage 0 (bootstrap): the build, the gates and the specification. No simulation yet.
The first deliverable is the 3-stage demo of GAME_SPEC §17.

## Build

Requires CMake 3.25+, a C++20 compiler, Python 3 and Godot 4.7 (`godot` on the PATH).

```bash
git clone --recurse-submodules <this repository>
make check     # engine-free build and tests
make native    # the Godot extension (the first build compiles godot-cpp)
make run       # the game
make gate      # every commit gate
```
