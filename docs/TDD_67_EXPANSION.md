# TDD 67: Liquids that expand (Stage 67)

## The request

The roadmap's area 8c (`docs/EXPERIMENTS_ROADMAP.md`), given a stage of its own by the user: a liquid's volume follows
its temperature. Until now every molar volume was the table's, at 25 °C, at every temperature: a beaker's level, its
overflow, what fits in it, a pour's millilitres, a column's layers and a sealed flask's free volume did not move as
the liquid warmed, though water grows by 4% from 25 to 100 °C (Stage 47's buoyancy alone scaled a layer's density
by the solvent's expansion).

What the code did (mapped before the spec): `fill_of` took a temperature for its gas only; a sealed cell's free
volume was read at 25 °C (`free_volume`, and the world's energy for a sealed cell set at a temperature); its
temperature was linear in its energy; the drains, the overflow, the column's levels and its top's size, V2, V3 and
L1 and the lab's millilitres read the table's molar volumes as they were.

## The design

- **One expansion** (`chem::expansion`): a liquid's and a solute's molar volume at T is the table's times
  `e(T) = ρ(25 °C)/ρ(T)`, ρ water's at 1 atm by Kell's correlation (1975, eq. 16; `data/solutions.json`'s `density`,
  `docs/DATA_SOURCES.md`); beyond its 0–150 °C, its value and slope at its end. Solids keep the table's. Smooth, with
  its first two derivatives, because a sealed cell's energy holds the first.
- **Every volume of a liquid at its cell's temperature**: `fill_of` (a vessel's fill, level, picture), the
  overflow (a brim-full vessel warmed spills what it grows by), the room below a drain (at its temperature) and
  what runs (at the giver's), what lands for a jet, a column's layers and its top's size, concentrations, a pour's
  and a mark's millilitres (at the giver's), the bench's air at the room's temperature, the lab's view; V2, V3 and
  L1 at each cell's own.
- **An open cell's energy is unchanged**: at the room's pressure its liquid's work against the room is in its Cp.
- **A sealed cell, thermodynamically consistent**: its solids and liquids have the Gibbs energy
  `G0(T) + V_c(T) (P - P_atm)`, so
  - `E = H(T) - n_gas R T + P_atm V_free(T) - T V_c'(T) (P - P_atm)`, `P = n_gas R T / V_free(T)`;
  - its entropy gains `- V_c'(T) (P - P_atm)`;
  - each condensed species' chemical potential gains `v_i(T) (P - P_atm)` (its affinity, and the probe's).
  Its temperature is E's root by Newton's steps, from the linear model's (Stage 28's), each halved while it would
  leave the gas no room; its heat capacity is dE/dT. The probe at an extent solves the same root with what the
  extent makes. Without the consistent pair a sealed cell cooling to the room (its heat flow's entropy second
  order in the gap) would lose entropy at first order, which T2 (1e-9 J/K) refuses.
- **The world sets a temperature through the same function** (`chem::energy_at`), so a temperature set reads back.

## Phases

| Phase | Topic | Status |
|---|---|---|
| 67.1 | Kell's density: data, the table, the datagen, its gate test against Lienhard's rows | Done |
| 67.2 | The core: `expansion`, `fill_of` at T, the sealed cell's energy, entropy and potentials, its root, the probe; the world's `energy_at` | Done |
| 67.3 | The core's consumers: drains, overflow, columns, transport; V2, V3, L1 at the cell's temperature | Done |
| 67.4 | The lab: its millilitres, a pour's and a mark's, what fits, the bench's air | Done |
| 67.5 | Sessions, plays and the goldens (with the user's approval) | |
| 67.6 | GAME_SPEC 0.67.0, CHANGELOG, `make gates`, the user's sign-off | |

## Tests

`native/tests/test_expansion.cpp` (Stage 67):
- water's volume grows as Kell's density falls from 25 °C, a solute's with it, a solid's not at all; the expansion's
  derivatives against its differences, continuous past its range;
- a sealed cell's temperature is its energy's root: set at a temperature it reads back (1e-12), its heat capacity
  dE/dT;
- its energy and entropy are one potential's: `T dS/dE = 1` within 1e-7, up to 1.9 atm;
- its affinity is `T dS/dξ` at fixed energy, its liquid's potential at its pressure;
- a sealed flask nearly full of water heated squeezes its gas (230 mL in 250 mL, 20 to 89 °C: free 20.3 → 12.7 mL),
  every invariant held;
- a brim-full beaker heated spills what its water grows by, every invariant held.

`native/tests/test_measured.cpp`: `measured: water's density is Kell's, within Lienhard's rows (Stage 67)`.

Changed, with the reason: Stage 4's `gas` (water at 0 °C is 18.02 mL/mol, Kell's, not 25 °C's 18.07); Stage 23's
Sano check (the solution's viscosity over its density at its temperature, as the code now reads it); Stage 28's
saturation rows (249 mL of water at the row's temperature, as much at 25 °C no longer fits hotter; and 5e-5 for
its temperature: water set at no pressure, then at its vapour's, warms as it is squeezed, `T V' dP`, 14 J at 2 atm).

## Open
- **A sealed cell squeezed toward full.** As its liquid grows into the last of its free volume its gas's pressure
  climbs without bound, and `- T V_c' (P - P_atm)` with it: E(T) has a maximum, near 100 bar for a flask of water
  (where `dE/dT = Cv - V_c' P - T V_c'^2 P / V_free` reaches zero). Past it the cell has no temperature (V3: no room
  for its gas). The lab's stoppers and glass give way at a few atmospheres, long before; a real liquid's
  compressibility, which would hold the pressure finite, is not modelled (§20).
- **A solute's own expansion**: its apparent molar volume grows as the solvent does (Stage 47's convention); its
  own, measured (it differs, and changes sign for some ions), is not modelled.
- **Solids** do not expand (about 1e-5/K: a level moves by under 0.01%).
