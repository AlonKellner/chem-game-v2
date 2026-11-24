# TDD 47: The vertical dimension (Stage 47)

## The request

**After Stage 45** the user asked (2026-11-24):
- "What would it take to correctly model vertical temperature distribution in a vessel? [...] I do want to accurately simulate the vertical dimension of vessels at some point, allowing for things like water and oil to not mix permanently, but for them to have some heat transfer."
- Roadmap area 8 was planned; Stage 46 was another agent's, so this is Stage 47.

**Choices made during the stage:**
- **Phased layers.** Asked how to proceed, the user chose "Bands, in phases".
- **Gradients.** "adjacent cells should have a gradient between them, not always a crisp line. This gradient should affect the chemical reactions of each of the cells and their other interactions [...] Is it for now or a later stage?" The answer:
  - the foundation now: thin homogeneous layers where a step is steep;
  - the other considerations later (`docs/EXPERIMENTS_ROADMAP.md`).
- **The cocktail scene.** "Add a scene where there are two differently colored liquids in the same vessel where they do have some mixing gradient, but also have enough of a density difference to resist it [...] like syrup in a cocktail."
- **The prism in the list.** "Also, add the prism and grating scene to the list of experiments."
- **No fixed cell count.** "Why always nine? [...] Wouldn't it be better to plan the cells in groups where we start with a group of 4 cells, and every time we reach the limit we allocate 4 more [...]?" The column now grows as it needs.

GAME_SPEC 0.47.0 covers:
- §4.2: empty cells.
- §6: the vertical dimension.
- §10: Pitzer's C.
- §12.1 and §12.2: bands.
- §13.7: the prism scene and `layers`.
- §18: L1 and T3.
- §20.

| Phase | Topic | Status |
|---|---|---|
| 47.1 | The prism and the grating in the experiments' list: a catalogue entry may name a scene | Done |
| 47.2 | The core's column (`src/strata.cpp`): layer cells, column edges, the column's process; empty cells; L1 | Done |
| 47.3 | A column grows as it needs (blocks of four), a vessel starting as one cell | Done |
| 47.4 | The lab: a layered vessel read as a whole, gentle and fast pours, the burner on the lowest layer, a bench layered; save and load | Done |
| 47.5 | Nickel sulfate in solution, sourced and gated; Pitzer's C counted twice, fixed | Done |
| 47.6 | On the screen: a band of each layer's liquid; the experiment `layers` | Done |
| 47.7 | The goldens: a new species moves every digest; every measured value unchanged | Re-recorded with the user's approval, 2026-11-24 ("Re-record them") |
| 47.8 | GAME_SPEC 0.47.0, `make gates`, the user's sign-off | `make gates` passed (522 s: ASan+UBSan 522, ctest 364, Godot 12 of 12; the sanitizers found the band sets growing the scene's rows under the body's own row pointers, which were then read freed); awaiting sign-off |

## 47.1 The prism in the list

The prism demo was a Godot node of its own (`ChemOpticsDemo`), outside the game's screen.
- **The catalogue:** an entry may now be `{"scene": "prism"}`.
- **On the screen:** when it is chosen, the screen (`Ui`) holds a `PrismDemo` in place of the lab:
  - its frame, draw list, optical scene and pointer are the demo's;
  - the experiments' button and list are painted over it, and take their presses first;
  - the wheel turns the prism.
- **Hosts:** none changed, since they already paint whatever the screen gives them.
- **The list:** it has two columns past eight entries; eleven in one ran off a 1080-pixel screen.

## 47.2 The column

**Why cells, and thin ones.**
- A cell is homogeneous: one temperature, one composition (§4.2). Conservation rests on that, so a column is
  several cells.
- A gradient is resolved by thin homogeneous layers that differ from their neighbours. Each layer reacts
  (§7) and equilibrates (§8) as its own cell, so a gradient changes the chemistry layer by layer, as asked.

**In the core:**
- `CellMeta::layer` marks a layer cell (elastic; never the root of the air).
- `Edge::column` marks a column edge (anything may cross; never a gas path).
- `CellMeta::column_volume` and `column_area` belong to a column's top.

**Each step**, after the drains (`Process::Column`), `stratify()` runs:
- gas rises; layers in use stay together under the top; solids sink;
- a layer that is lighter than the one over it overturns (a part in 10⁹, past what rounding leaves between two
  halves of a layer);
- alike layers merge (contrast under 0.1, no steep step beside them);
- stable neighbours exchange solutes by Fick's law, no further than half the way to equal concentrations, and heat
  by k A / dz (conductances set at the step's start);
- the steepest step splits its thicker neighbour in halves, if a spare layer is there and the halves stay over
  0.25 mm;
- the top is resized.

**Contrast** is the larger of ΔT / 1 K and, over the dissolved species, the difference in mass fraction / 10⁻³.
Splits happen above 1; alike layers merge below 0.1.

**Empty cells.** An empty cell has no temperature and holds no energy:
- the builder allows one, and T3 checks its energy is 0;
- T2's entropy sum counts it as 0, since a layer that fills in a step was otherwise uncounted (the first T2
  failures, −190 J/K);
- moving all of a layer moves all its energy, so it is left at exactly 0.

**Settling** also projects each layer to its fast equilibria (P1). A layer that boils makes steam, which rises at
the next step.

**Gates** (`native/tests/test_column.cpp`):
- **Mixed:** the temperature is bit-identical to a world without the column.
- **Fick:** the first step moves 2.932e-9 mol up, against 2.93247e-9 by Fick.
- **Ordering and conservation:** after an hour every layer is in order of density, and all the copper is there
  (in either form: copper sulfate's trace hydrolysis to the hydroxide, Stage 44).
- **Overturning:** dense on top overturns at once; warm over cold stays; cold over warm overturns.
- **Gas, solids, stirring:** gas rises, solids sink, and stirring merges.
- **Refinement:** a steep step is resolved into half-millimetre layers. After ten minutes the copper sulfate
  profile across about 3 mm reads 0.11, 0.36, 0.60, 0.89, 1.14, 1.38 M, between the water above and the 1.49 M
  below.
- **Self-tests:** L1's and T3's fire on corrupted traces.

## 47.3 Grown as it needs

The first version gave every standard vessel nine layer cells. The user asked for no fixed number, so:
- **Nothing up front.** A vessel starts as one cell.
- **Growth.** At the step's start (`grow_columns`), a column whose last layer is in use (no spare), and whose
  steepest step would split, grows by `COLUMN_BLOCK` (4) empty layers under its bottom
  (`World::add_layer_under`).
  - Growth is decided from the state alone, so a saved and loaded lab grows as an unsaved one does.
  - The new cell is also in the step's starting snapshot (empty), so the trace stays well formed.
- **Gentle pours.** One that needs a spare grows the column the same way.
- **Reuse.** Emptied layers stay as spares; a world's cell ids are never reused.
- **The bound:** no layer is split below half a millimetre.
- **Seen:** a copper sulfate step grew its column to 17 layers in an hour.
- **Mixed vessels** carry no extra cell. The demo's and the pressure session's structure is as it was; only the
  new species moves their digests.

## 47.4 The lab

- **Reading a vessel.** A layered vessel reads as a whole: its contents summed, its temperature what they would
  come to mixed, its fill every layer's.
  - What a reader uses: `layered`, `whole`, `liquid_m3(Item)`, `grams(Item)`, `kelvin(Item)`, `fill(Item)`.
  - Who uses them: the view, the report, conditions, notices, stirring and pouring by hand.
  - The scripted pour takes from the surface, as physics.
  - `ItemView::layers` lists each layer from the top: its liquid, solids, temperature and contents. The layers
    are read from the world's column edges (`layers_of`), not stored on the item.
- **Pours.**
  - **Gentle:** a pour of no more than 2 mL/s (`GENTLE_POUR`: a trickle by hand, or `"gentle": true` on a
    scripted pour) first pushes the liquid there down a place (`push_surface`). What it pours lands as a new
    surface layer.
  - **Fast:** a faster pour, or one that speeds up past it, stirs back in what it laid (`stir_in_surface`) and
    lays nothing more.
  - **The rule goes by the pour's own rate,** not its last step's remainder, so a fast scripted pour's last trickle
    lays nothing.
  - **Order:** the push waits until every drain's rate is set; a drain's rate cannot change once the step has a
    transfer. That was the first build's bug: layers that would not stay.
- **Why not every pour.** Pouring 1 M base on 1 M acid lays nothing (the neutralisation experiment), because a real
  pour entrains what it lands in.
- **The burner** follows the lowest layer in use, its edge moved at a step boundary (`follow_columns`).
- **The bench.** A bench may list a vessel's `"layers"`, from the top down.
- **Save and load.** Cells save `layer` and `column`; edges save `column`; a pour saves `landed`. A layered lab
  saved and loaded goes on exactly.
- **The readout:** "layered: N layers, … C at the top, … C at the bottom".
- **The debug panel:** each layer's volume and temperature.

`native/tests/game/test_column_lab.cpp` shows:
- **The bench:** green over blue persists, and after ten minutes 12 layers resolve its boundary.
- **Pours:** a gentle pour floats the lighter liquid; a fast one mixes.
- **Stirring and heating:** stirring merges; a low flame warms the bottom to 35.9 °C under a 20.0 °C top, and they
  stay.
- **Save and load:** a lab saved and loaded goes on exactly.
- **The screen:** blue low (b\* −62) and green high (a\* −58); O2 holds there.

## 47.5 Nickel sulfate, and Pitzer's C

`nickel_sulfate_aq` is in solution only (`docs/DATA_SOURCES.md`, "Nickel sulfate (Stage 47)"):
- **Thermodynamics:** NBS 1982's "ai" row; its heat capacity a stand-in (copper sulfate's).
- **Activity:** Pitzer and Mayorga's pair. γ± is within 5 % of the CRC's at 0.1, 0.2, 0.5 and 1.0 mol/kg.
- **Diffusion:** the CRC's Ni²⁺, 6.61e-10 m²/s.
- **Density:** Krumgalz's apparent molar volume at 1 mol/kg, 0.81 cm³/mol; density 1.1504 g/cm³, within 0.3 %.
- **Colour:** three Gaussians whose sum gives Shigematsu's maxima, 4.72 at 394 nm, 1.80 at 650 nm and 2.06 at
  718 nm.

**A fault found by its gate.** Pitzer's third-virial term was written 2 m_c m_a (B + Z C), with Z over every ion.
It is m_c m_a (2B + Z C). C counted twice:
- nickel sulfate's γ± at 1 mol/kg read 9 % high;
- copper sulfate's and the acid's Cφ are 0, so the demo is untouched;
- the sodium salts' gates (Stage 43) hold as before.

## 47.6 On the screen

- **A set per layer.** A layered vessel's scene gives each layer a set of spectra whose liquid row is that
  layer's.
  - Concentrations are kept to 0.2 % steps, so a slowly spreading gradient keeps its pixels' histories.
  - The body's own set has an eighth row, `BANDS_ROW`: each band's set and the heights over the floor it lies
    between. At most 133 bands, from the bottom.
- **Reading it.** `band_rows(body, rows, y)` finds the band at a height. `liquid_at` now takes the pixel's or
  ray's height, at every site:
  - the shaded layer;
  - the bent body's own shade;
  - beams;
  - traced rays;
  - the reference's truth.
- **O1** refuses bands that are no set, or that do not rise.
- **The C API's `row_count`** is eight rows a set.

**On the GPU:**
- **O4** is as it was: the known pixels at the cabinet's small glass, the largest difference 11.
- **`data/plays/layers.json`** opens the experiment from the list and photographs it:
  - green over blue, a crisp boundary at first;
  - after a while at 600x, still nearly crisp, since the diffusion zone is a millimetre after twenty minutes, about
    a pixel;
  - stirred: one teal liquid, with its vortex.

**The experiment `layers`** (`data/benches/layers.json`) is 80 g of water with 0.064 mol of nickel sulfate over
80 g with 0.112 mol of copper sulfate. That is 1.12 over 1.22 g/cm³. It has no tutorial: speed the clock, stir,
warm it.

## 47.7 The goldens

Each cell's amount vector gained a species, so every digest moves. In the measured output (`chem_tool run`, Stage
45's build against this one), the demo and the pressure session are identical except for one added line, "Ni 0 ->
0 quanta of atoms". A mixed column never makes a layer, so a vessel's cells are as they were. Re-recorded once
the user approved it (2026-11-24).

## Open

- **A gradient's other causes** (the user's "other considerations"):
  - entrainment where a pour lands (a gentle pour lays nothing at its boundary but what it pours);
  - shear at an interface;
  - salt fingers (heat diffuses about 100 times faster than salt);
  - the Soret effect;
  - an interface overturning in plumes, not whole.

  Each needs sourced data, and each is a later stage (`docs/EXPERIMENTS_ROADMAP.md`, area 8).
- **Volumes do not expand.** A layer's buoyancy reads the solvent's measured expansion, but the core's volumes are
  25 °C's.
- **The column's cross-section is the floor's.** A conical flask's narrowing is not in its layers' thicknesses.
- **Bubbles are the vessel's,** read from its whole, not layer by layer.
- **A band ignores its liquid's narrow lines.**
- **Diffusion is slow, so the boundary is slow to blur.** It is a millimetre after twenty minutes and a centimetre
  after ten hours, as in the world: zoom in (Stage 42) or speed the clock to see it. The cocktail's own gradient
  is mostly from the pour.
- **Two liquids that do not mix at all** (oil and water) need a second liquid phase: roadmap area 9.
