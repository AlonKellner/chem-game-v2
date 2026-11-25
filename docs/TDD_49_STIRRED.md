# TDD 49: A stirred column, and drains from its bottom (Stage 49)

## The request

**After Stage 47** the user chose the next stage (2026-11-25): "Stage 47's open items". Stage 48 (the thermal
camera) was another agent's; Stage 50 (the curriculum's measured lab) is another agent's too.

The two items, from Stage 47's testing and its document's Open:
- **Stirring merged the layers at once.** The user, testing Stage 47: "after I stir the vessel, the layers
  immediately merge". A stirred column was made one cell in the step stirring began. Physically, a stirrer mixes a
  stratification as fast as its work can raise the fluid's potential energy, and a short stir leaves it part mixed.
- **An outlet at a layered vessel's bottom, or one upside down through its cap, drew from its top layer.** It should
  draw from its lowest.

GAME_SPEC 0.49.0 covers:
- §6: the stirred column; what leaves a layered vessel from its bottom.
- §20: the stirred column's simplifications.

## Phases

| Phase | Topic | Status |
|---|---|---|
| 49.1 | The mixing efficiency sourced (Linden 1979, Osborn 1980) and gated | Done |
| 49.2 | The core: `CellMeta::stirring`, `World::set_stirring`; the column mixes as far as the turning pays | Done |
| 49.3 | The lab: the turning's dissipation to the core each step | Done |
| 49.4 | Drains from a layered vessel's bottom follow its lowest layer | Done |
| 49.5 | GAME_SPEC 0.49.0, `make gates`, the user's sign-off | `make gates` passed (704 s: ASan+UBSan 704, ctest 381, Godot); the goldens unchanged; on the GPU the layered beaker stirred is green over blue as the vortex builds, the blue taking in the green from below, one teal in a second; awaiting sign-off |

## 49.1 The mixing efficiency

Linden (1979, *Geophys. Astrophys. Fluid Dyn.* 13, 3–23) reviews laboratory experiments that stir stable
stratifications: the share of the stirring's dissipated work that raises the fluid's potential energy rises from
zero with the Richardson number to a maximum of 0.2 ± 0.05. Osborn (1980) takes 0.2 for the ocean. The column takes
the maximum, a constant (`MIXING_EFFICIENCY`): a weakly stratified column mixes faster here than it would (§20).

The gate (`native/tests/test_column.cpp`, "measured: ..."): the experiment `layers`' two liquids, 80 g of water
each, one copy stirred at 1 mW for six steps and one still. The stirred copy's potential energy gains, over the
still one's, 19.996 µJ of the 20 µJ that 0.2 of the work pays.

## 49.2 The core

`CellMeta::stirring` (W) is what a stirrer's turning dissipates in a cell's liquid: a rate the column reads, not
state, set by whoever turns it before each step (`World::set_stirring`, at any time, as a drain's rate). In the
column's process, in place of "a stirred column is one cell":
- the step's budget is 0.2 × stirring × the step;
- it is spent from the bottom up: the stirrer stands on the floor, and the layer it stirs deepens up the column;
- two layers that exchange a volume v (each mixed as it lands) raise the potential energy by g (ρ_l − ρ_u) v d,
  d the distance between their middles. The formula is exact for two uniform layers of fixed volume: the upper
  gains (ρ_l − ρ_u) v / V_u of density over its height, the lower loses it, and the sum is g Δρ v (h_l + h_u) / 2;
- at v = V_u V_l / (V_u + V_l) the two are one, and merge whole; what the budget has left goes to the interface
  over them;
- where it pays for less, the two exchange what it pays for (their liquid and what is dissolved, each at its own
  temperature's enthalpy), and the step's budget is spent.

The joules raised are not taken from the liquid's heat: the core keeps no gravitational energy (§6's "kinematic"),
and they are under a millijoule against the turning's 0.01 W of heat.

Tests:
- `column: stirred, two layers become one in the time the turning pays for; a stir stopped short leaves them part
  mixed`. Half a second at 2 mW raises 0.2 mJ of the 0.89 that make them one, and the green is down in the blue.
  Stopped, the potential energy stays within 1% (diffusion aside). At the stirrer's full 10 mW, ten seconds make them
  one;
- `column: gas in a layer rises, ...`: stirring at 10 mW makes the column one in ten seconds.

## 49.3 The lab

`set_stirring` already kept the turning's energy (Stage 32: it builds as dE/dt = P − E / 1.5 s and dies away over
1.5 s once the stirrer stops). Each step the lab now gives the core its dissipation, E / 1.5 s, as the column's
stirring. The column therefore mixes as the turning builds: the experiment `layers` is still layered a quarter of a
second in and one by five seconds. A stir of 0.3 s, then off, leaves three layers. The top takes in a fifth of the
blue and stays the greener, and the bottom takes in some of the green.

Test: `column: a stirrer mixes a layered vessel into one as its turning builds; a short stir leaves it part mixed;
...`.

## 49.4 Drains from the bottom

`follow_columns` (the burner's edge following the lowest layer, Stage 47) now also moves the drains that leave a
vessel from its bottom:
- an outlet (`under`);
- the cap of a vessel upside down over another (`invert`).

At each step's boundary, each starts from the lowest layer that holds anything, its rate kept (`redrain`). It runs
before the pours' rates are set. A pour over the lip still draws from the top. Taking an outlet off, and a vessel
bursting, find the drain from whichever of its column's cells it leaves (`drain_from`).

Tests:
- `column: an outlet at a layered vessel's bottom draws its lowest layer first`. An outlet runs all of what is over
  it each step: the blue in the first step, the green still in the beaker;
- `column: a layered bottle upside down runs its lowest layer out through its cap first, the one over it after`.
  The blue in the first second, all of it in two minutes. Saved and loaded mid-pour, it goes on digest for digest.

## The goldens

Unchanged. No session stirs a layered vessel or drains one from its bottom.

## Open

- **The mixing efficiency is a constant.** It should fall at a high Richardson number (Linden 1979's curve), so a
  strongly stratified column would mix more slowly than here.
- **A pour's own mixing** where it lands (a stream's entrainment, Ricou and Spalding 1961; a fountain's height,
  Turner 1966) is still the gentle/fast rule of Stage 47. The user is fine with it for now.
- **The other Stage 47 items** stay open: volumes that expand with temperature, a conical flask's narrowing, bubbles
  layer by layer, a gradient's other causes.
