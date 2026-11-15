# TDD 02: Temperature, heat and entropy (Stage 2)

## The request

> … simulating their connections using the physical, thermal, mechanical, and chemical laws
> required to realistically simulate the progression and changing of material concentrations
> across containers over time.

Stage 2 is the thermal law alone (GAME_SPEC §5), on the ledger of Stage 1: cells get a
temperature, edges conduct heat, and the second law joins the invariants.

| Phase | Topic | Status |
|---|---|---|
| 2.1 | Data: thermodynamic values for eight species verified against NIST and ISO 3585 (`docs/DATA_SOURCES.md`) | Done |
| 2.2 | The core's own `exp` and `log`, within one unit in the last place, bits pinned (`chem/math.hpp`) | Done |
| 2.3 | Temperature, heat capacity and entropy derived from state (`chem/thermo.hpp`) | Done |
| 2.4 | Reservoirs, conductances, worlds built at a temperature (`chem/world.hpp`) | Done |
| 2.5 | `Simulation`: the fixed step and heat conduction by exact two-body relaxation (`chem/sim.hpp`) | Done |
| 2.6 | Invariants T1, T2, T3, S1 with self-tests; D3; the first K1 oracle | Done |
| 2.7 | The thermal sweep; UX6 (the extremes of temperature never widen) | Done |
| 2.8 | `chem_tool study cooling` | Done |
| 2.9 | The user's sign-off | Done (2026-11-15: "Approved, proceed") |

## Decisions

- **Temperature is derived, in closed form.** `T = T0 + (H − Σ nᵢHfᵢ) / Σ nᵢCpᵢ`. Nothing stores a
  temperature, so nothing can disagree with the energy.
- **A reservoir is an energy account, not a big cell.** The room, a flame and the coolant have a
  fixed temperature and an integer energy that records what they received. The world stays closed:
  what the flask loses is in the room's account, to the quantum.
- **Exact relaxation instead of rate × dt.** Each edge applies the exact solution of its two-body
  problem over the step, so 1e9 W/K on a microgram is as stable as 2 W/K on a flask. Edges are
  applied in index order, each from the state the last one left: the one process that is not
  evaluated from the frozen start-of-step state (GAME_SPEC §4.5). Evaluating all edges from the
  frozen state would let several strong edges on one small cell overshoot together.
- **Quantisation that cannot run uphill.** A carried remainder is dropped when the temperature
  difference changes sign, and a transfer is capped at the whole quanta that would equalise the
  pair. T1 is therefore exact, not approximate, and a world at rest stops completely: no
  one-quantum flicker, remainders included (D3).
- **T2's tolerance is derived, not chosen.** The planning draft had `1e-9 + 1e-12·Σ|ΔS|`. The
  check sums entropies in float64, so its error is bounded by the rounding of that sum:
  `4e-16·Σ|S|` over the cells that changed, plus 1e-9 J/K. Only cells that changed are summed, so
  a large untouched cell adds no noise.
- **Unverified species are unusable, by construction.** A species has thermodynamic data only
  once its row is verified; a world with temperatures refuses a cell that holds one without. T3
  catches one appearing later. Malachite, the hydrates and the dissolved species wait for their
  stages.
- **The studies drive the core directly.** `chem_tool study` is a stage's physics against its
  closed form; the game itself will be played through the C API (Stage 7).
- **A range limit, stated.** One step moves at most 1e9 J along one edge, and an energy account
  at the end of the integer range takes no more. The sweep found this: two reservoirs 2900 K
  apart through 1e9 W/K ask for more than an int64 of quanta per step.

## Evidence

`chem_tool study cooling`: 100 g of water in a 100 g glass flask at 80 °C, a 20 °C room, 2 W/K.

- Worst error against Newton's law over 15 minutes (54,000 steps): 1.15e-11 K.
- World energy: the same integer at the start and the end.
- Entropy made: 9.195 J/K, non-negative in every step.
- Step cost: 0.27 µs (one cell, one reservoir, trace built, checks off).

The thermal sweep: 2000 random worlds, 120,000 steps, 514,000 heat transfers; every invariant
holds, and no step widens the range of temperatures.

## Known limit

A cell's temperature is computed from `H − Σ nᵢHfᵢ`, a difference of two large numbers. For a cell
holding megajoules of formation enthalpy the float64 error of that difference approaches one
energy quantum, so two such cells at rest may settle a few quanta (nanojoules, about 1e-11 K) from
exact equality. It does not affect conservation, which is in the integers.

## Tests

- `math: …` (3 cases): `exp` and `log` against the C library over 800,000 arguments, special
  values, pinned bits
- `thermal: …` (9 cases): the data oracle, a cell built at a temperature, refused worlds,
  reservoirs, Newton's law of cooling, two bodies, stability at 1e9 W/K, rest, repeatability
- `invariants: …` (5 new cases): the honest thermal step; heat uphill (T1, T2); entropy destroyed
  outside the ledger (T2); a cell without a temperature (T3); a value that is not finite (S1)
- `sweeps: heat flows downhill and entropy grows on every step of every random thermal world`
- ctest `chem_tool_study_cooling`
