# TDD 00: Bootstrap (Stage 0)

## The request

> I want to pivot to an optimized C++ native core, with Godot as a thin wrapper, and GPU optimized
> logic for the optical physics rendering. […] Please plan the full reimplementation of this game,
> where the definition of done is the minimal example, specifically the 3-stage interactive
> chemistry demo. […] One thing I want to prioritize as well is having conservation laws as
> invariants for this game.

## What Stage 0 delivers

The repository, on the layout and conventions of `ricochet-game-v2`, with nothing simulated yet:

| Phase | Topic | Status |
|---|---|---|
| 0.1 | Layout: `native/{core,game,capi,hosts,tests,extern}`, `engines/godot`, `data`, `bench`, `docs` | Done |
| 0.2 | Build: CMake, the three trees, floating-point flags (no FMA contraction, no fast-math), warnings as errors | Done |
| 0.3 | Layering lints: `*_no_engine`, `godot_adapter_includes`, `gdscript_budget`, and the new `core_no_libm` | Done |
| 0.4 | Error policy: `CHEM_ASSERT` aborts in Debug, reports and continues in release; the C API's log sink | Done |
| 0.5 | C API skeleton (`chem_api.h`), C11 smoke host, headless host `chem_tool` | Done |
| 0.6 | Godot host: the `ChemLab` node, main scene, GUT smoke suite | Done |
| 0.7 | Gates: `make check`, `make gate`, `make gates`, the result cache | Done |
| 0.8 | `GAME_SPEC.md` 0.1.0: every system in full with its demo subset; invariant catalogue | Done |
| 0.9 | `docs/DATA_SOURCES.md`: the planning data, marked unverified | Done |
| 0.10 | The user's sign-off | Pending |

## Decisions

- **Names.** Core namespace `chem`, lab layer `lab`, C API prefix `cg_`, macros `CHEM_`, host node
  `ChemLab`, tool `chem_tool`.
- **`core_no_libm` from day one.** The core will compute its own `exp` and `log` (Stage 3) so
  replays are bit-identical across platforms. The lint exists before any math does, so the rule
  cannot be broken by accident.
- **No GPU pattern to copy.** `ricochet-game-v2` has no shaders; the renderer of Stage 10 is new
  (GAME_SPEC §12).
- **Not copied from ricochet:** the mpmath oracle, the Python host, the judge scripts, Clipper2,
  the sized-deallocation flag (it serves ricochet's allocator). They are added if a stage needs them.
- **Data is quarantined.** Values gathered during planning are in `docs/DATA_SOURCES.md` marked
  unverified; none is used by code.

## Tests

- `toolchain: a*b+c is two roundings, never a fused one (Stage 0)`
- `toolchain: the build names GAME_SPEC's version (Stage 0)`
- `error: release-mode CHEM_ASSERT reports through the sink and continues (Stage 0)`
- `error: report_error uses the installed sink; an empty sink is allowed (Stage 0)`
- `lab: the build's name carries GAME_SPEC's version (Stage 0)`
- ctest `capi_c_smoke`, `chem_tool_version`, and the lints of `docs/WORKFLOW.md`
- GUT `test_host_smoke.gd`: the extension registers `ChemLab`, it names the build, the main scene is the host
