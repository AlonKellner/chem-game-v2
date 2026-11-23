# TDD 32: The vortex, and what it holds up (Stage 32)

## The request

The fourth stage of the plan of TDD 29 (the user, 2026-11-22): a stirred liquid's vortex, a cone at
the middle of the level surface; its size from its energy and the liquid's mass (a heavy liquid
stores more energy in a smaller vortex); the most energy from the mass and the space there is;
"prefer elegant arguments". Decided with the user: **stirring's work enters the energy ledger**, and
the demo's record is recorded again only with the measured change shown first.

GAME_SPEC §6 (work), §11 (the vortex, the turning's energy, what it holds up), §18, §20.

| Phase | Topic | Status |
|---|---|---|
| 32.1 | The law and the cone: `contents::swirl` | Done |
| 32.2 | Work in the kernel: an edge's power; its source | Done |
| 32.3 | The lab: the turning's energy, the vortex's share, the suspension and its settling | Done |
| 32.4 | The picture: the cone traced, the haze by the share held up | Done: O9 holds through it; a picture from the GPU |
| 32.5 | **The records: shown to the user, recorded again once approved** | **Waiting for the user** (below) |
| 32.6 | The user's test and sign-off | |

## The law

A liquid turning in columns (its speed `v` a function of the distance `r` from the axis alone) has a
surface whose slope balances the turning, `dh/dr = v² / (g r)`. In a round vessel of radius `R` and
depth `H` the volume its dip displaces, under the level the surface has at the wall, is
`V = ∫ π r² (dh/dr) dr = (π / g) ∫ v² r dr` (by parts), and its kinetic energy is
`E = ∫ ½ ρ v² · 2π r H dr = ρ π H ∫ v² r dr`. So, **for any `v(r)`**, `E = ρ g H V = (M g / A) V`:
the liquid's weight on a unit of its surface times the displaced volume. (Each column taken at the
liquid's depth: exact for a dip small beside the depth; a deep vortex's columns are shorter in the
middle, which this leaves out.) Checked for three profiles when the plan was made. The vortex is drawn as a cone in the proportions of the space it has
(the surface's ellipse of area `A`, the depth `D` under its middle): a share `s` of that space
displaces `s³ A D / 3`, so

    s = cbrt(E / E_most),   E_most = M g D / 3.

A heavier liquid holds more energy in a smaller vortex; a shallow one cannot make a deep one; at
`E_most` the vortex reaches the glass (or the sediment) and takes no more.

## What was built

- **`contents::swirl`**: `E_most` and the share for an energy (the depth is the liquid's under that
  vortex: two passes).
- **Work** (`chem/world.hpp`, `sim.cpp`): `Edge::power`, `World::set_power`, the process `Work`
  (after heat), its remainder under a quantum carried as heat's is.
  - **Its source cannot be the room.** Drawn from the room's reservoir it broke T1 and T2: work
    from one heat reservoir is what the second law forbids. The source is **a reservoir so hot that
    its energy carries no entropy** (10^300 K for the limit): the invariants hold as written, with
    no case of their own. The lab has one, `mains`, with an edge to every vessel.
- **The lab** (`set_stirring`, each step): the stirrer's power while there is liquid to turn;
  `dE/dt = P − E / τ` exactly over the step, never more than `E_most` (worked out again when what
  the vessel holds changes by a thousandth). `P` = 0.01 W, `τ` = 1.5 s: the game's choices.
- **What it holds up** (changed from the plan, which had the energy lift the solids half the
  liquid's depth: that takes no account of the grains' size, and left a long tail). **A power
  balance**: grains fall at Stokes' speed, `v = (ρ_s − ρ) g d² / 18 μ`; holding them up takes their
  weight in the liquid times `v`; the turning has `E / τ` to give, and holds up that share. Let go,
  they settle at `v` through the liquid's depth.
- **The picture**: `OpticsBody::vortex` from the lab's share; the sediment is what is not held up,
  the haze the share that is.

## Measured

- The law: for three vessels, two fills and three energies, `(M g / A) V` of the cone drawn is the
  energy asked, to a part in ten thousand (`chem_tool study vortex`, a ctest entry, prints it: in
  200 mL of water 15 mJ is a cone 4.56 cm deep that displaces 27.6 mL; in a liquid twice as heavy,
  3.48 cm and 13.8 mL).
- 200 mL of water in a 250 mL beaker: 15 mJ of 60 at the most, a vortex 71% of the largest that
  fits; after one `τ`, `1 − 1/e` of it; stopped, `1/e` after one `τ`. 40 mL: the vortex at the glass.
- **The ledger**: ten minutes of stirring leave the beaker 5.662 mK warmer than one beside it;
  6 J into 901 J/K with what it loses to the room is 5.664 mK. Every invariant each step.
- 12 g of copper oxide in 10^9 grains (15 µm, 0.68 mm/s): all held up; let go, held 7.5 s by the
  dying turning, then down through 5.2 cm in 76 s (the model within 5% of Stokes). In 10^5 grains
  (331 µm): 32% held up, the power balance's share within 2%.
- A controller rigged to a vessel's stirring now tunes on that vessel's temperature (stirring warms
  it, a little): its test senses a vessel beside it for the reason "nothing it controls moves".

## The records (32.5): what changed, for the user's approval

`make sessions-check` and the ctest entries `demo_session` and `lab_tests.lab` fail until the records
are made again (`make sessions APPROVED=1`).

- **`bench/sessions/pressure.jsonl`**: every digest differs (the world has a cell more, the mains,
  and an edge from it to each vessel). **Every measured value the tool prints is the same**: 49
  lines, none different, against the last commit's tool.
- **`bench/sessions/demo.jsonl`**: every digest differs, and the demo stirs its flask for 17
  minutes, which now warms it by the stirrer's work:

| | before | after |
|---|---|---|
| The oxide dissolved (command 12) | 1003.1 s | 1003.0 s |
| Boiled down (17) | 1228.8 s | 1228.7 s |
| Cooled (19) | 1919.8 s | 1919.7 s |
| Steps | 550832 | 550827 |
| CuSO₄·5H₂O crystals | 9.194995 g | 9.195349 g |
| Water distilled | 38.162295 g | 38.162333 g |
| CO₂ left in the flask | 0.000345 g | 0.000343 g |
| Elements, mass, charge, energy | unchanged | unchanged |

## Open

- A vortex is a cone, where a real one is a paraboloid in the middle and flatter beyond (§20).
- Stokes' speed is right for fine powder only (§20); water's viscosity at 20 °C whatever the liquid.
- The stirrer's work is heat at once (§20).
