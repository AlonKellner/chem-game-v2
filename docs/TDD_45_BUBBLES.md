# TDD 45: Boiling's bubbles (Stage 45)

## The request

The user, 2026-11-24, after Stage 44: "When a solution is boiling it should have gas bubbles at the very least,
acting similarly to suspended particles, diffusing light. Also, the amount of bubbles should depend on the depth
of the liquid. [...] if it's heated from the bottom - the bubbles are created there, then they rise and spread
their heat to their surroundings, condensing back into liquid as they rise. Depending on the temperature
distribution of the liquid as well as the temperature and size of the bubbles, they might not even reach the
surface at all, but end at some midpoint."

The plan agreed, "Sounds good, document these ideas" (`docs/EXPERIMENTS_ROADMAP.md`, areas 3, 8 and 9):
- **This stage:** bubbles from a heated floor's film.
- **Later stages:** the vertical dimension, and two liquids.

GAME_SPEC 0.45.0: §11 (boiling's bubbles), §13.3 (`bubbles_cm`), §13.7 (the experiment), §18 (B1), §20.

| Phase | Topic | Status |
|---|---|---|
| 45.1 | The data, sourced: Lienhard's water and correlations, Fritz, Baz-Rodríguez, Chen and Mayinger, Flatau | Done (`docs/DATA_SOURCES.md`, "Boiling's bubbles (Stage 45)"; `data/boiling.json`) |
| 45.2 | `lab/bubbles.hpp`: the floor, the onset, the bubbles up the column; B1 | Done |
| 45.3 | The lab: a vessel's bubbles from its burner's flow; the readout, the debug panel, `bubbles_cm` | Done |
| 45.4 | The pass: a body's vec4 6, read wherever a haze is, white | Done |
| 45.5 | The experiment `boiling`; a play that photographs a boil on the GPU | Done |
| 45.6 | GAME_SPEC 0.45.0, `make gates`, the user's sign-off | |

## 45.1 The model, and why it is derived

**A vessel heated from below is one bulk and a film.**
- The bulk is one cell, as every vessel is (§5).
- Heating from below is unstable, and natural convection keeps it within a little of one temperature.
- The structure that matters for bubbles is a boundary layer about a millimetre thick at the hot floor.
- So the floor's temperature follows from the heat the floor takes, the bulk's temperature, and natural
  convection across that layer.

**Derived, not state.**
- A bubble lives a tenth of a second; the bulk's temperature changes over minutes.
- With the bulk one cell, where a bubble condenses changes nothing about where the energy goes: the burner's heat
  is the bulk's either way.
- So the bubbles are worked out each step from the last step's flows: the burner edge's heat into the vessel, and
  the gas its rated reactions made. It is a view of the state, as Stage 32's vortex is.
- The world, the digest and the goldens do not move. `make sessions-check` reproduces every record.

**The floor** (`lab::bubbles::boil`):
- **Its pressure:** the room's plus ρ g h.
- **Its boiling point:** the table's saturation at that pressure, under the solution's water activity.
- **Convection** (eqs. 8.37a–b, 8.35, L\* = A/P) carries what it can at the floor's temperature.
- **The onset:** where the floor's largest cavity holds a nucleus, 2σ/r = a_w p_sat − p. Glass's 3.5 µm is 8 K over
  the boiling point (Lienhard's Example 9.1).
- **Boiling:** past the onset, the floor's temperature is where convection and Rohsenow's boiling together carry the
  heat.

**A bubble** leaves at Fritz's size: 2.34 mm at 100 °C for 45°. It rises at Baz-Rodríguez's speed.
- Where the bulk is under the boiling point at its height, it shrinks: dd/dt = −2 h ΔT_sub/(ρ_v h_fg), with h from
  Chen and Mayinger.
- It is gone at 5 % of its size, or reaches the surface.
- The column is followed in steps of a 400th of the depth or 2 % of the bubble's size: 13 µs a vessel.

**What is seen.**
- The extinction a metre is 2 n πd²/4, with n the bubbles over the floor's area at their speed.
- Fitted as β₀ (1 − z/end)^p, where p keeps its integral up the column.
- Times 1 − g (0.85): bubbles scatter mostly forward, and only what they turn away from its path is a haze.
- **Bubbles a reaction gives off** are Fritz's size, rise to the surface, and are the same at every height.

| A 250 mL beaker, 300 W through its floor (`test_bubbles.cpp`) | Bulk 60 °C | Bulk 99 °C | At the boil |
|---|---|---|---|
| The bubbles end | 0.58 mm up | 14 mm up | at the surface (4.4 cm) |
| The floor | — | — | 108.9 °C; 30 000 bubbles of 2.3 mm a second |

At the boil they still shrink a little low down. The floor's boiling point is 0.12 K higher under 4.4 cm, so even a
bulk at its surface's boiling point is that much under it there: p is 0.15, not 0. Four times as deep holds 2.5
times the bubbles, not 4.

## 45.2 The gates

`native/tests/game/test_bubbles.cpp`:

| Measured | Source | Held |
|---|---|---|
| Water's rows; σ 58.9 mN/m at 100 °C, 72.0 at 25 °C | Lienhard Table A.3, eq. 9.2a (IAPWS) | exact; 0.2 % |
| Rohsenow: 800 kW/m² at 22 K, C_sf 0.016 | Lienhard Example 9.2 (his Fig. 9.2) | 3 % |
| The onset: 8 K over the boiling point for a 3.5 µm cavity | Lienhard Example 9.1 | 10 % (8.15 K) |
| A bubble's rise peaks at 0.15–0.17 cm, dips at 0.65–0.67 cm | Baz-Rodríguez et al. 2012 (Mendelson, Loth) | 15 % (0.161, 0.665 cm; peak 34 cm/s) |
| Convection: eq. 8.37a; within 10 % of Lloyd and Moran's 0.54 Ra^¼ | Lienhard; Lloyd and Moran 1974 | |
| g 0.85 | Flatau et al. 2000 | |

**Behaviour, and B1.**
- **Behaviour:**
  - Bubbles die near the floor in a cool bulk and reach the surface at the boil.
  - A deeper column holds more bubbles, and its floor boils hotter.
  - A reaction's gas rises to the surface.
  - On a bench, a beaker on a burner makes bubbles at once that die 0.3 mm up, and boils at 100.15 °C with
    bubbles to the surface.
- **B1:** its self-test fires on bubbles past the surface, a negative extinction, a holdup of one, and more heat
  than the floor took. A sweep of heats, temperatures and depths holds.
- **On the screen** (composed from a view):
  - Just over the floor the liquid reads L\* 100 where it read 88, white (a\*, b\* under 1.5).
  - Above where the bubbles end it is unchanged.
  - A reaction's gas whitens every height.
  - O1 holds, and so does O2, the pass against the truth.
  - O1 now also refuses a negative bubble field.

## 45.3 The lab

- `ItemView` (lab.hpp) gains:
  - the floor's temperature and its boiling point;
  - whether bubbles of steam leave it, and whether they reach the surface;
  - how high bubbles reach, their size, and the liquid's depth;
  - the three numbers the pass reads.
- **Which vessels:** an upright vessel of a standard shape on the bench that holds liquid water, its heat from its
  burner's edge and its gas from its reactions' extents.
- **The readout:** "boiling: bubbles of 2.3 mm reach the surface", or "bubbles from the floor, gone 0.3 mm up".
- **The debug panel:** the floor, its boiling point, the depth and where the bubbles end.
- **A monitor** reads `bubbles_cm` (vessel, while it holds a liquid).
- **A reading costs nothing more.** A monitor reads one item's quantity at every step at 600x, so its view leaves
  the bubbles out unless the reading is of them, as Stage 36 left the shapes out. Without that, three monitors
  cost 230 ms a frame at 600x; the limit is 3.

## 45.4 The pass

A vessel's vec4 6, unused since Stage 35, holds:
- the extinction a centimetre at the floor (times 1 − g);
- the height where the bubbles end;
- the exponent;
- a reaction's gas's extinction.

The per-pixel source reads it in `bubbles_at(body, y)`. Every haze adds it:
- the layer's own;
- `haze_along` on a refracted chord, now given its height;
- the shade of the body that bends;
- `body_does` for traced rays and beams.

What a haze turns back is mixed toward white by the bubbles' share (`white_at`). The Layer gains no field: the
source's own note says a field cost the pass 0.1 ms. The scene quantises the four numbers (3 % steps in extinction),
so a pixel keeps its history while a boil stands still. The reference and the truth read the same source, so they
agree by construction; the GPU compiles it into its three passes.

**On the GPU** (`make screenshots`):
- O4 is as it was: up to 12 pixels a picture visibly off, all at the cabinet's small glass, the known ones (Stage
  42's measure: 0 to 12, the largest 14; here the largest 11).
- `data/plays/boiling.json` (`LAB_SHOTS=… make pass-cost PLAY=data/plays/boiling.json`) photographs a beaker of the
  acid at full flame, cold, near the boil and boiling:
  - a white line at the floor;
  - a white band over the lower third;
  - the whole liquid white, densest at the floor.
- The first pictures, before the 1 − g, were an opaque white at the boil: 450 W makes 330 mL of steam a second, 20 %
  of the liquid by volume near the floor, a chord's optical depth of 15. With it, 2.3.

## 45.5 The experiment

`boiling`, *bubbles of a boil* (`data/benches/boiling.json`: 200 g of water, 5.9 cm deep; the lab's burner):
1. A burner under the beaker.
2. Full flame.
3. A monitor on `bubbles_cm`.
4. Above 5 cm.
5. They climb (to above 5 cm).
6. The boil: the monitor signals at 99.5 °C or more.

It is played to its end strict, strict at a person's pace, and free. A little after it ends, the bubbles reach the
surface and the readout says so. The fizz's step now points at its gas's bubbles.

## Open

- **A strong flame boils the floor from the start.** 450 W through 34 cm² (130 kW/m²) is more than natural
  convection carries at any bulk temperature, so the floor passes its onset at once: bubbles that die a third
  of a millimetre up, over 20 °C water. Real beakers on a Bunsen burner make vapour bubbles that collapse
  near the floor well before the boil, but the onset is usually seen warmer. A real flame heats the floor
  unevenly, and its glass spreads the heat; the onset's hysteresis and dissolved air are not modelled.
- **No superheated bulk, no bumping, no boiling chips.** The bulk's own boiling is still the equilibrium of
  §8, with no barrier. A surface's cavities (`surfaces` in `data/boiling.json`) are where chips will enter:
  a porous stone is large cavities, a small onset superheat.
- **The column is the floor's width.** It does not narrow with a conical flask. Bubbles do not grow as the
  pressure falls up it, nor merge.
- **A haze, not bubbles.** The pass sees a population, not each 2 mm bubble. A bubble as a body the pass
  traces is a later stage's (`defer-hard-renderer-issues`).
- **The condensing correlation** is used below its range (Re < 400) in a bubble's last moments, and was read
  second-hand (Herry et al. 2024).
