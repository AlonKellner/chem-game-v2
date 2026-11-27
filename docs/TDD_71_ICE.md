# TDD 71: Ice (Stage 71)

## The request

The curriculum's step I, its first part (`docs/CURRICULUM.md`: C8; experiments 26 and 33; the roadmap's area 4),
chosen by the user after Stage 66's sign-off (2026-11-27). The user's choices:
- **A vessel in a vessel** (over a cold bath item or a cold plate): a test tube stands in a beaker; an ice bath and a
  salt-and-ice freezing mixture are made by the student.
  - "Each ring interacts with the layers it touches, heat transfer is by layer/ring, not full vessel to full vessel."
  - "The inner vessel should change the volume integral of the outer vessel, such that each layer is displaced the
    correct amount, increasing the level the correct amount."
- **Freezing with supercooling** (over a fast equilibrium): a cooled liquid dips below its freezing point until a
  first crystal starts, then warms back to it as ice grows (the textbook freezing curve).

Other liquids (C9) and burning (C10), the rest of step I, are later stages.

## What the data say (fetched, 2026-11-27; `docs/DATA_SOURCES.md`, Ice)

- **Ice Ih** (IAPWS R10-06(2009), Feistel and Wagner 2006): melting at 101 325 Pa at 273.152519 K; at it ice's
  h −333 354.87 J/kg and s −1220.7693 J/(kg K) against the liquid's at the triple point (Table 6), cp 2096.71 J/(kg K),
  ρ 916.721 kg/m³. With the liquid's (NIST WebBook, IAPWS-95): ΔH_fus 333.43 kJ/kg, 6.0068 kJ/mol; ΔS_fus 21.99
  J/(mol K); ice's absolute entropy at its melting point 41.36 J/(mol K) (Pauling's residual entropy in).
- **Freezing points** (International Critical Tables IV, 1928, Hall and Sherrill, pp. 256, 258): CuSO₄ 0.0025 to 1.0
  mol/kg (Δt/N 3.003 to 1.715 K kg/mol), its eutectic with the pentahydrate at 0.846 mol/kg, −1.43 °C; NaCl 0.001 to
  5.2 mol/kg, its eutectic with the dihydrate at 5.20 mol/kg, −21.12 °C.
- **Growth into supercooled water**: dendrites, v = 0.228 ΔT^2.39 mm/s (Lindenmeyer and Chalmers 1966) or
  4.83e-5 ΔT^2.78 m/s (Shibkov's data as Grivet et al. 2022 fit): 0.33–1.2 mm/s at 2 K.
- **Supercooling** (heterogeneous: set by the container and the water, little by the volume or the cooling rate):
  300 µL in borosilicate tubes at 4.5 K/min, median −9.4 to −12.0 °C, stirring no matter (Wilson and Haymet 2016,
  Table 1); one sample's 10–90 % width 0.7 K (Wilson, InTech); 0.4 K colder at six times the cooling rate (Vali 2008);
  a student's 6 mL tube in −18 °C air typically to about −5 °C (Tan et al. 2016).
- **Melting**: an 18 mm ice ball in turbulent water h = 5380 W/(m² K) (Machicoane et al. 2013).
- **Optics**: ice's n + ik from 44 nm to 2 m (Warren and Brandt 2008; n 1.3110 at 550 nm, k 2.3e-9); snow's albedo
  0.96–0.98 over the visible (Grenfell, Warren and Mullen 1994).

## The design

### Ice in the tables (GAME_SPEC §4.2, §8)
- `ice`, H2O(s), appended after Stage 64's species: density 0.916721 g/cm³, cp 37.77 J/(mol K) (IAPWS at its melting
  point), and hf and s0 at 298.15 K chosen so that with water's own constant cp the model's melting at 1 atm is
  IAPWS's, 273.1525 K, with its heat of fusion, 6.0068 kJ/mol (hf −292.7774 kJ/mol, s0 44.666 J/(mol K)); the model's
  ice then has IAPWS's absolute entropy at its melting point, 41.36 J/(mol K), as a check, not a fit. Its thermal
  conductivity for a powder bed and for its own melting (a source to fetch: 71.1).
- A solution's freezing point is where ice and the solution's water have one potential: through the water's activity
  (Pitzer's, §10), nothing more. Held to ICT's freezing points for both salts (a gate test).

### Freezing and melting as a surface law (GAME_SPEC §7, §9)
- `water_freezing`: H2O(l) → H2O(s), the surface law at ice's surface, with nucleation. Ice's own attachment is fast
  (its surface step large: calibrated, not limiting); what limits a pure substance changing phase at a surface is the
  heat its change gives out or takes in, carried to or from the surface. **A new step in series, general**: a surface
  reaction that carries no dissolved species is limited by heat, `Nu · k / d · (T − T_eq) / |ΔH|` per m², `T − T_eq`
  from the affinity, `A · T / ΔH`; `Nu` Sano's form with the Prandtl number for the Schmidt number (2 at rest, the
  stirring's dissipation raising it, Stage 57), `k`, `ν`, `Pr` the liquid's (`solvent_at`). Freezing and melting the
  same law: ice in warm stirred water melts by heat transfer (checked against Machicoane's 5380 W/(m² K)), a crystal
  in a supercooled liquid grows as heat leaves it.
- **Supercooling**: ice's first crystal by a nucleation barrier, `nucleation · (kg of water) · exp(−B / ln² S)` a
  second (the classical form; a new optional `nucleation_barrier` beside the power law the salts keep), fitted to a
  median freezing at about −10 °C in a glass tube cooled at 4.5 K/min with the width Wilson measured (about 4.4 per K
  near it). Once a crystal is there it grows, the latent heat warms the liquid back to its freezing point, and the
  plateau holds while the bath draws the heat away.
- A solid lighter than its liquid **floats** (GAME_SPEC §6, the column): it rises to the top liquid layer, where the
  column now sinks every solid. Ice floats on water, with 8 % above the surface.

### A vessel in a vessel (GAME_SPEC §6, §11, §13)
- **Stand**: `stand {item, in}`: a vessel stands in another, on its floor, upright; refused unless it fits (its outside
  within the outer's inside, its foot on the outer's floor or sediment). Taken out by `move` or `take`. Saved.
- **The volume integral**: the outer's inside cross-section at a height, less the inner's outside cross-section
  there (from its foot to its rim): every volume read through it, so each layer's height span, the level, the
  surface, the overflow and what fits move by what the inner displaces. A brim-full bath spills what a tube pushes
  over.
- **Ring to layer**: each ring of the inner vessel's wall faces each layer of the outer's that its height span
  overlaps, the overlap's area by the into-water law (Churchill and Chu, Stage 53) in series with half the glass, an
  edge of its own; above the outer's liquid, its gas (into air). Its floor's base faces the outer's lowest layer.
  Every contact is a ring and a layer, never a vessel and a vessel.
- **The screen**: carried over a vessel, "stand it in" beside "pour" and "tip in". Drawn inside the outer, the inner on
  the layer nearer the eye (the pass does not trace a body inside another's liquid: Open).
- **A test tube** (`data/glassware.json`): a round-bottomed tube, 16 × 150 mm, its rings, its tolerances none.

### Ice seen (GAME_SPEC §12)
- Ice's medium: Warren and Brandt's n + ik over the visible and 8–14 µm, so it absorbs and emits as ice does (a clear
  piece in water nearly invisible: n 1.31 against 1.33). Crushed ice's whiteness (scattering at its many faces) is not
  traced: Open. A floating solid drawn at the surface, its 8 % over it.

### Experiments (GAME_SPEC §13.7)
- `heating_curve` (curriculum 26): crushed ice in a beaker on a burner, a thermometer logged in the notebook: the
  plateau at 0 °C while it melts, the rise, the boil's plateau; the heat of fusion by a calorimeter (ice into warm
  water in the foam cup).
- `freezing_point` (curriculum 33): a test tube of water, then of a solution, stood in a beaker of ice and salt, its
  thermometer logged: the dip below the freezing point, the jump back, the plateau; the depression against the
  molality, the cryoscopic constant and a solute's molar mass.

## Phases

| Phase | Topic | Status |
|---|---|---|
| 71.1 | Measured first: ice's data (IAPWS, ICT, the growth, the supercooling, the optics, ice's conductivity); `chem_tool study ice` | Done: the model melts at 273.15227 K (IAPWS 273.152519), its heat of fusion 6006.84 J/mol (6006.8); CuSO₄'s lowering within 1.2 % of ICT to 0.8 mol/kg, NaCl's within 0.4 % to 0.1, 3 % at 1, 9 % at 4 (Pitzer's 25 °C parameters at 0 °C); a 0.3 g tube cooled at 4.3 K/min first freezes at −10.44 °C (Wilson and Haymet's −10.45) |
| 71.2 | The core: `ice`, `water_freezing`; heat-limited surface steps; the nucleation barrier; a solid's grain as a mush; the gate tests | Done; the goldens re-recorded with the user's approval (their measurements identical to main's, line for line; the digest moves with the species added) |
| 71.3 | The column: a lighter solid floats; a column's overflow | Done. The picture of a floating solid: Open |
| 71.4 | Ice seen: its medium in the visible (0.9, calibrated) and the long wave (0.970 from Warren and Brandt's n + ik) | Done |
| 71.5 | A test tube (ISO 4142); a vessel in a vessel: `stand`, the volume integral (`outside_below`, `World::reshape_column`), ring-to-layer contacts (`set_facings`), the bath held, saved and loaded; a bench vessel at its own temperature (`celsius`) | Done |
| 71.6 | The screen: "stand it in", a standing tube drawn at its bath's scale on the nearer layer, picked, labelled, put back in place | Done |
| 71.7 | The experiments `freezing_point` (curriculum 33) and `heating_curve` (curriculum 26) | Done |
| 71.8 | GAME_SPEC, CHANGELOG, `make gates`, the user's sign-off | `make gates` passed (712 s, merge level): core, 834 ctest entries, ASan+UBSan, the Godot suite (13 of 13), the goldens. Awaiting the user's sign-off |

## Tests

`native/tests/test_ice.cpp` (Stage 71):
- measured: water freezes at IAPWS's point with its heat of fusion, ice its entropy there (and its cp, density and
  conductivity, Carnahan's 612/T);
- measured: solutions freeze at the International Critical Tables' points (CuSO₄ and NaCl, 0.01 to 4 mol/kg);
- measured: water supercools in a glass tube to Wilson and Haymet's median, colder as it cools faster (0.2 to 0.8 K
  at eight times the rate);
- a supercooled tube jumps back to its freezing point and holds there while it freezes, every invariant held (within
  50 mK in 20 s; the plateau within 1 mK of it; a mush of over a thousand grains);
- melting in water is limited by the heat the water carries to it, Nu 2 at rest and more stirred;
- ice floats up a column through a denser solution to its topmost liquid layer, a denser solid still sinks.

`native/tests/game/test_bath.cpp` (Stage 71): a test tube is ISO 4142's; a tube stood in a beaker raises its level by
what it displaces (8.6 mL at 4.5 cm), the beaker held where it stands, taken out its own shape again; a brim-full
beaker spills what a tube pushes over (16.6 g for 18.0 mL); heat crosses ring by layer (a 40 °C tube in 2 °C water:
its floor 8.9 °C, its top ring 37.7 °C after 30 s; on the bench, in air, it cools far less); saved and loaded it goes
on exactly; on the screen, "stand it in"; measured: ice's long-wave emissivity is Warren and Brandt's.

`native/tests/game/test_experiments.cpp`: `freezing_point` and `heating_curve`, each strict with its own gestures, at
a person's pace, and free. `freezing_point`: the bath to −23 °C, the water supercooled to −9.7 °C, all ice at 0 °C;
the solution to −11.4 °C, then from −2.0 °C falling as its ice concentrates it. `heating_curve`: the cup from 40 °C
to 20.0 °C, 334.6 J/g; the ice from −10 °C, flat at 0 °C, then at 100.1 °C.

`datagen_refuses_barrier_with_order` (ctest); and each `datagen_refuses_*` now checks its own message: six of the
nine fixtures had been refused since Stage 63 for a species their file holds (an ion without its ions), passing for
the wrong reason; their copper ion given its ion.

## Open

- **A ghost on the GPU after a pour by hand** (the user, 2026-11-27: stirring the ice bath after pouring salt into it by
  hand, "the rendering leaves a strange ghost image of the pile, not settling to the expected background"; "the ghost
  image is in the target vessel after I pour"). Found so far:
  - the lab is right: the user's exports (the debug panel's, loaded by `Ui::open`, which now opens a save) rebuild on
    the CPU, where the renderer's frames and the truth's own tracer both draw the lowered pile and nothing else;
  - the GPU alone shows it: `data/plays/ghost.json` (the experiment chosen, its tutorial free, the jar carried over
    the bath and poured by hand, set down, the bath stirred; `LAB_SHOTS=... make pass-cost PLAY=data/plays/ghost.json`)
    draws the bath cleanly once the jar is set down (`docs/stage71/ghost-1-poured.png`), and once it is stirred an
    outline of the 50 mL jar, where it was held, stands inside the bath and stays (`docs/stage71/ghost-2-stirred.png`,
    the same five seconds later);
  - so the jar's image is kept in the GPU's own per-pixel state from when it was held there (the kept beams'
    records or the pixels' history images, `optics_compute.cpp`), and read again when the bath's content changes;
    the CPU's frames, which do not split the screen into the host's passes and rectangles, never show it.
  - the user, further (2026-11-27): "a sort of triangular pattern which changes from brighter to transparent and back
    to bright, repeating with lowering heights"; "it depends on the zoom level, when I zoom in a lot the ghost image
    does not occur, but the farther I zoom out, the more extreme the ghost effect is"; "it feels like the pixels
    settle on brighter values because something does not recalculate them properly, some of their bias is left
    behind". Their later exports render cleanly on the CPU too (one pile, no triangles), and the CPU's frames held to
    the area's truth while the stirred pile lowers (1,200 frames at 1x) never stray: the count apart is the wall
    alone, the same at every check, none brighter;
  - so what runs on the GPU and not in the CPU's frames: the host's pass for a view's change (`OPTICS_RESAMPLE_PASS`,
    each pixel's state carried, its corrections with it), its passes over the rectangles that bend (`cg_optics`), and
    its per-pixel records; the first hypothesis, a pixel's history carried across a view or a pass with its old
    corrections (its bias), and what keeps it there as the pile moves under it.
  The line is not yet found. By the user's choice it is the next renderer stage's first item, Stage 77 (with the nested
  rays below), from the play.

- **A floating solid's picture.** The pass draws a vessel's solids as a sediment on its floor, a flat section and a
  pile (`contents::lying`; the shader's and the reference tracer's pile): floating ice is drawn there too, white. A
  solid that floats needs a pile under the surface in both tracers (§12).
- **One vessel inside another** is drawn on the layer nearer the eye (§12.3): not seen through the bath's front wall
  and liquid, and the bath's liquid not seen through it. Tracing a body inside another's liquid needs both tracers to
  follow a ray into a neighbour on its own layer. **The next stage, by the user's choice (2026-11-27)**: "when a
  vessel is inside a vessel - rays should be rendered as entering the outer vessel, and then proceeding to hit any
  surface along the way and reacting accordingly, including the inner vessel. For example, a ray may enter the outer
  vessel, then enter the inner vessel, then exit the inner vessel, and then exit the outer vessel, and only then
  reach a wall. It should be correctly simulated" — in the per-pixel source (GPU, and its CPU copies in float64 and
  single precision) and in the truth's own tracer, held to each other (O4, O7).
- **A floating solid counted whole in its vessel**: a brim-full glass with ice spills its ice's whole volume's worth
  (about 9 % more than the liquid it displaces); the ice cannot stand over the brim (V2).
- **Sublimation and evaporation from ice** are not modelled: dry ice below 0 °C is stable in dry air.
- **The salt's dihydrate** (hydrohalite) is not modelled: ice and salt meet at the model's −23 °C, not −21.1 °C.
- **Pitzer's parameters at 0 °C** are their 25 °C values: sodium chloride's freezing points 3 % low at 1 mol/kg and 9 %
  at 4.
- **Dendrites**: ice grows into a supercooled liquid as a mush of grains of 100 µm (calibrated); a real dendrite's
  tip runs at Shibkov's speed, which the model's equal spheres do not follow.
- **A solution's freezing point** is read where its curve starts after the jump: its ice concentrates what is left, so
  its line falls, as a real one does.
- **The bench's `celsius`** sets a vessel's contents and glass; a jar of ice from the freezer warms in the room's air
  as any vessel does (its cubes melt if left: the calorimetry is done first).
