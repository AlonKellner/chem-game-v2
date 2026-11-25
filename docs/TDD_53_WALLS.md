# TDD 53: The glass in rings (Stage 53)

## The request

The user, 2026-11-25 (during Stage 51): "separating the vessel glass into the same layers as the liquids, each vessel
layer should have an independent heat transfer dynamic with the layer it touches, and vessel layers should have heat
transfer between them. The glass layers should also have gradients between them [...] decide on the approach which
you think is best, document it and let's implement it as a future stage." The decision is the roadmap's area 8b: a
fixed grid of glass rings, coupled by overlap. After Stage 51 merged the user said "proceed", and this is that
stage. Stage 52 (the measured lab's second half) is another agent's.

**Choices made during the stage:**
- **A powder on the floor:** "Packed-bed physics". The demo's malachite, heated only through the gas over it,
  no longer decomposed; it now takes the floor's heat through its bed (Zehner and Schlünder).
- **The goldens:** re-recorded with the user's approval (2026-11-25) after the evidence below.

GAME_SPEC 0.53.0 covers:
- §6: the wall in rings; a powder bed on a floor;
- §20: the simplifications.

## Phases

| Phase | Topic | Status |
|---|---|---|
| 53.1 | Data: borosilicate's conductivity (DURAN), Churchill and Chu's vertical wall and air's properties (Lienhard), Zehner and Schlünder's bed (Kandula), the grains' conductivities (mineral analogues, calibrated) | Done |
| 53.2 | `lab/walls.hpp`: rings from a vessel's shape, the correlations, the bed | Done |
| 53.3 | The lab: rings and their edges, `set_walls` each step, the burner on the floor, the bubbles' floor heat, save and load, the view | Done |
| 53.4 | A powder bed on the floor | Done |
| 53.5 | The demo's shifts: tests restated, the goldens re-recorded with the user's approval | Done |
| 53.6 | GAME_SPEC 0.53.0, `make gates`, the user's sign-off | `make gates` passed (1984 s: ASan+UBSan, ctest 517, Godot 13 of 13); `make screenshots` O4 as Stage 48 recorded (the largest 13 to 17 at x = 26), its headless and windowed alarms 120 s and 240 s (the demo's slower cooling takes more frames); awaiting sign-off |

## 53.1 Data

- **Borosilicate:** SCHOTT DURAN, 1.2 W/(m K) at 90 °C (`species.json`'s new `conductivity`, on walls and solids,
  parsed by datagen into `SpeciesDef::conductivity`; the other agent's foam cup, Stage 52, takes its value there too).
- **A vertical wall's natural convection:** Churchill and Chu as Lienhard gives them (*A Heat Transfer Textbook*,
  v6.00, eqs. 8.13a to 1e9 and 8.13b above, the film's properties); water's properties are the core's (Stage 47),
  air's are Lienhard's Table A.6 (`data/walls.json`).
- **A powder bed:** Zehner and Schlünder's stagnant bed as Kandula (NASA) prints it, eqs. 9a–c and 10, with C = 1.40
  for broken grains; the porosity is the sediment's (1 − PACKING).
- **The grains:** no measurement was found for malachite, tenorite or chalcanthite. Each takes the nearest measured
  mineral from Robertson (USGS OF 88-441), a calibrated value:
  - malachite as calcite, 3.59 W/(m K);
  - the copper sulfate hydrates as gypsum, 1.32;
  - the anhydrous salt as anhydrite, 5.91.

  Copper oxide and hydroxide have none. A bed's grains are those of its solids that have one, weighted by volume;
  a bed with none conducts as its fluid. A bed's conductivity grows only as the logarithm of its grains' own.

## 53.2 and 53.3 The rings

A glassware vessel's wall (`wall_g`) is its floor and rings up to its brim (`walls::rings_of`): about 1 cm each, at
least 4 and at most 24. Each is a cell of the glass, by the share of the glass it holds (the shape's integrals). Its
edges:
- **to the room:** the vessel's calibrated `loss` by its share of the outside;
- **to the next ring:** conduction through the glass, k A / dz between their middles;
- **to every cell of its column:** conductances set each step by `set_walls`. A ring's wetted height along each
  liquid layer is that layer's span from the shape's level table, times Churchill and Chu into water (the liquid's
  depth its length). Its dry height over the liquid goes against the gas in the top, into air (the dry wall's
  height). The floor and the lowest liquid use Stage 45's heated floor (`bubbles::floor_flux`: convection, and
  Rohsenow's boiling past the onset). A floor no warmer than the liquid passes heat as a wall into the liquid, and a
  dry floor into the gas.

A layer the column grows is connected to every ring at the next step. A burner heats the floor ring, not the liquid.
The bubbles take their floor's heat from the floor's edges into the column. A vessel's view lists its glass as before
(summed once from the rings) and its rings' temperatures (`wall_c`, in a full view only: a monitor's reading does not
need them). A save keeps the rings, and a load finds their edges again.

**The cost.** A step of a beaker of water was 590 µs at first. The shape's level was inverted every call and the
wetted area integrated for every contact every step. A table of 256 levels per glassware kind and a ring's area by
its share of the height brought it to 13 µs.

The Godot host's suite then failed on time: a frame of a second ran 36 of its 60 steps under its 3 ms budget while
the gates loaded the machine. Its free lab, all glassware in the cabinet, stepped at 31 µs against main's 6.5 µs.
The cuts:
- **exact, each one checked against the goldens digest for digest:**
  - the floor's boiling onset is looked for only where the floor can boil;
  - each vessel's ring and cell temperatures are read once a step;
  - the landing speed's level comes from the same table (this also removes Stage 51's cost);
  - a vessel whose rings and column are unchanged keeps its contacts' conductances. The setting is kept only if
    every temperature it needed was known and every setting was taken: a world refuses a conductance mid-step, and
    has no temperatures before its first step;
  - a cell of nothing but wall is passed over by the reactions and the fast equilibria;
  - a cell's thermal state is kept while its energy, amounts and the settings it reads are the same;
  - an edge's relaxation factor, 1 − exp(−G h / C), is kept while its G and C are the same;
  - monitors that read one item at one step share one view of it (dropped at any command): three monitors cost about
    1 ms a frame at 600x, not 2.7 (the gates, running the sanitizers, ctest and Godot side by side, had found the
    bound of 3 ms exceeded under their own load);
- **a design change:** rings of about 1 cm rather than 5 mm (a beaker's 9 and its floor). The demo's results moved
  by a hair: malachite in 40.6 s, the distillate 33.76 g, the crystals 9.123 g.

The free lab now steps at 16 µs (main: 6.5) and the demo at 34 µs (main: 57).

## 53.4 A powder on the floor

The bottom cell's solids lying on the floor (those not held up by stirring) form a bed whose thickness comes from the
shape: the volume they take up at the sediment's packing. While there is one, the floor's contact with that cell is
the bed's conduction over half its thickness: Zehner and Schlünder's conductivity, with the liquid as the bed's
fluid if any, else the gas. The test: a dry conical flask of 2 g of malachite on a full flame decomposes, the floor
hotter than the powder, the brim near the room.

## 53.5 What moved

Heat now goes through the glass:
- **Malachite in the demo decomposes in 42 s, not 140.** The flame heats the floor's few grams, which heat the bed.
- **The water heats faster.** Less of the flame's heat warms the whole flask, and a hot thin floor passes the rest.
- **Cooling is slower.** Inner convection is in series with the room loss, and it weakens as the temperatures close.
- **Measured values that moved:**
  - the demo's distillate, 33.6 g where it was 37.8: it stays warm longer and evaporates more from the open beaker
    (Stage 50);
  - the crystals, 9.13 g where they were 9.20;
  - the pressure session: the same events at the same pressures, later.
- **Tests restated, each with its reason:**
  - the debug panel's "heat with flask.floor";
  - the trash's six minutes (its floor gives up its heat first);
  - the circuit's 40 s;
  - the tutorial's distillate over 30 g;
  - the gentle pour's green counted over the copper (it lies as two layers);
  - the Godot host's timer, played in frames of a sixtieth of a second until it rings rather than in one frame of
    3.5 s (210 steps, over the frame's budget, Stage 36: the clock ran slow by design);
  - the controller's overshoot under 5 °C at every speed.
- **The controller.** The trial now fits the flask as an integrator, so the gentle speed overshoots a step of
  setpoint by about a tenth (4 °C on 40), more than the fast. That is open.
- **Goldens:** both sessions re-recorded with the user's approval. They were re-recorded once more, on Stage 52 and
  with the 1 cm rings, under the same approval; the changes from the run reviewed were within a hair.

## Open

- **A controller's setpoint weighting:** with an integrating process SIMC's PI overshoots any speed by about a tenth
  of the step; a weighted setpoint (or a ramp) would give the gentle speed its gentleness back.
- **The outside:** each ring's loss is the calibrated `loss` shared by area; natural convection and radiation on the
  outside (the long-wave range has the glass's emissivity) would replace it.
- **The thermal camera** sees a vessel at one temperature; a wall's rings could be its picture's gradient (a row of
  the set by height, as the bands are).
- **A burette** is 24 rings of about 3 cm.
- **The free lab** steps at 16 µs against main's 6.5: its idle cabinet's rings still conduct to the room each step.
  Settled by the user's Supplies (Stage 55, the roadmap's area 8d): the cabinet's items are then no part of the
  world until taken out.
