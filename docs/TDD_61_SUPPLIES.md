# TDD 61: Supplies, Storage and Trash (Stage 61)

## The request

The user, 2026-11-25 (after Stage 53's step cost): "We should have the following: 1. Supplies - things which are not
simulated at all, just like parts, once you take them out, they join the room simulation 2. Storage - things which
are put aside to save grid space, but are actually still simulated as part of the room 3. Trash - things stop
simulating here. Elements may not be put back into Supplies, they may be put into Storage or Trash. Today's
"Cabinet" should become the Storage, and it should not simulate anything, even the preview of the elements should
be pre-captured, not simulated live rendering." Asked which the cabinet becomes: **Supplies**, a stage of its own
after Stage 53 (the roadmap's area 8d).

During Stage 53's merge: "Storage should basically be the output tray, it should have goal exactly like it does
today, just be purposed for any type of storage, including just putting something aside while space is tight in
the lab grid. One important thing - both the Supplies (cabinet) and Storage (out tray) should have scrolling
capabilities, to allow an arbitrary amount of things to be contained in them. Also, I want textual search
functionality in both, and search results to match by keywords, some of which depend on the contents, some on the
vessel, and any other you think appropriate for the search functionality."

**Choices (the user, 2026-11-26):**
- **A supply is a stock that never runs out** ("Unlimited stock"): a template; each one taken out is a new item in
  the world, the supply stays. Matter enters the room from outside, and the conservation ledger counts it.
- **The trash freezes; emptying it removes** ("Frozen; empty removes"): in the trash nothing about an item
  changes; restored, it is exactly as it was trashed; emptied (or past the trash's capacity), it leaves the world
  for good, and the ledger counts what left.

What the code does today (mapped before the spec): nothing ever leaves the simulation. A cabinet item has its cells
and edges from the bench's load and steps like any other (its rings conduct, its mouth exchanges gas, its notices
fire); the trash keeps simulating (Stage 34's own test holds a trashed hot vessel to the cabinet's cooling); the
digest hashes every cell. The world cannot make a cell after its build except a column's layer.

## The design

**The world is open at its boundary, at two kinds of event, between steps only** (GAME_SPEC §4):
- `World::add_cell` (between steps): an empty cell appended; `World::supply(cell, amounts, energy)`: matter and
  energy brought into an empty cell from outside. `World::retire(cell)`: everything a cell holds leaves the world,
  its edges are closed, and it takes part in nothing again.
- `World::freeze(cell, on)`: a frozen cell takes part in no process (no reaction, no settling, no column, no edge
  carries anything to or from it); its state stands. A frozen cell's edges are kept, closed, and reopen as they
  were when it thaws.
- **The ledger** (the lab's): what was supplied and what was retired, as totals. **C9** (§18): the world's totals
  are what it began with, plus what was supplied, less what was retired, at every step; its self-test corrupts a
  supply.

**Supplies** (today's cabinet, the screen's left panel):
- A bench item without `at` is a supply: a template, not in the world. Each is made, once at load, in a world of its
  own (a bench of that item alone): that gives its picture and what it is made of, never stepped.
- **Taking one out** (`place` naming a supply, or a new `take`) grafts a copy of that template world's item into
  the room's world: its cells added and supplied at the room's temperature (as the bench describes them), its edges
  joined to the room, its air and the mains as the loader would. The first copy keeps the supply's id (`beaker`);
  the next are `beaker2`, `beaker3`... A command that names a supply none of whose copies is in the world takes one
  out first: to where it puts it (`heat` a burner under a vessel, `attach` a stopper), or else to Storage (`tip`
  from a jar), so every bench's sessions and tutorials keep their meaning.
- **Nothing goes back to Supplies**: carried to the panel, an item goes to Storage.
- **Pictures are captured ahead of time**: each supply's box is a picture taken once, of its template on its own,
  by the renderer's own source on the CPU (the single precision pass, settled), then cached: the benches in `data/`
  at build time (`make native`), any other bench at its first load, kept by the template's description's hash. The
  box draws its picture; nothing of it is traced live.

**Storage** (today's outbox, under the bench):
- As many slots as it needs (at least the six of today, always one free after the last that holds anything); a goal
  may be on any slot, and is checked as today.
- `submit` and `stow` both put an item in a slot (`stow`: the first free slot without a goal). An item in Storage is
  in the world and simulated (it cools, reacts, evaporates); taken out again with `place` (it was one way: `submit`).
- What went to the cabinet before goes to Storage now: a stopper that pops (when no square beside is free), a
  shattered vessel, `unheat`, `detach`, `unweigh` and `remove` with no room beside.

**Trash:** an item put in the trash is frozen (all its cells), its edges closed; `restore` thaws it exactly as it
was. `empty_trash` and the trash's capacity (64) retire it: its matter and energy leave the world through the
ledger, and it is no longer an item. Signal parts as today.

**Scrolling and search, in Supplies and in Storage:** each panel scrolls by rows (the wheel, a bar), and has a
field over it; typed words filter its boxes, each word the start of one of an item's keywords (any order, any
case). The keywords (`lab/search.hpp`, pure):
- what it is: its id and label; its kind (vessel, burner, balloon, stopper, balance, thermometer, camera, viewer,
  spectrophotometer, cuvette...), its glassware and its standard's words (beaker, conical flask, burette...), its
  capacity ("250", "250ml");
- what it holds: each species' id, its name and formula, its phase (solid, liquid, gas, solution, "dissolved"),
  "empty" for none;
- what it is made of: its wall (glass, latex, polystyrene);
- where it is for (Storage): its slot's number and its goal's name;
- its state: hot (over 40 °C), cold (under 10 °C), sealed, stoppered, boiling, broken.

## Phases

| Phase | Topic | Status |
|---|---|---|
| 61.1 | The core: add a cell, supply, retire, freeze; the ledger; C9 and its self-test | Done |
| 61.2 | Supplies: templates in worlds of their own; taking one out (graft); implicit take-out | Done |
| 61.3 | Storage: the outbox's slots unbounded, goals on any, `stow` to it, `place` from it | Done |
| 61.4 | The trash: frozen, restored as it was, emptied out of the world | Done |
| 61.5 | Save, load and digest: the ledger, the frozen, the retired, the supplies' copies | Done |
| 61.6 | The screen: the two panels scrolled, their search fields; `lab/search.hpp` | Done |
| 61.7 | The pictures: captured on the CPU, cached, built for `data/`'s benches | Done |
| 61.8 | Benches, tutorials, sessions and plays; the goldens (with the user's approval); the host | Done |
| 61.9 | GAME_SPEC 0.61.0, CHANGELOG, `make gates`, the user's sign-off | `make gates` passed (2122 s: ASan+UBSan, ctest, Godot 13 of 13, the goldens); awaiting the user's sign-off |

## What was built

- **The core** (`world.hpp`): `add_cell`, `supply`, `retire`, `freeze` (and `Builder::set_boundary` for a save),
  refused mid-step as a connection is; `CellMeta::frozen` and `retired`, `Edge::frozen` (an edge a freeze closed,
  opened again as it was). Every process skips a frozen cell (`react`, `overflow`, the settling's tree, a column's
  layers, the column's head); P1 skips it (its state stands as it was frozen). C9 (`check_C9`, `plus`, `minus`).
- **The lab**: a supply's template is a `Lab` of a bench of it alone (`Supply::made`), never stepped. `take` grafts
  its own cells (each layer under the cell over it), supplies them, and joins only the edges of its own cells (the
  template's air-to-room edge is not the copy's: found by the tests, Stage 61's agent), naming them by the copy's id.
  A command naming a supply none of whose copies is out takes one out (`find_or_take`); `Lab::command` saves the
  lab first and loads it back if the command is refused, so a refusal changes nothing. Storage: `to_storage` (its
  first free slot without a goal), every move that went to the cabinet. The trash: `freeze_item`, `retire_item`
  (`Item::retired`: kept so indices stand; its id never given again); a frozen item's walls are not set. Save and
  load: the cells' and edges' marks, each supply's bench and copies, the retired. The report lists the supplies and
  the conservation across the boundary.
- **The screen**: the supplies after the lab's items in its own list (`supply_base`), place "cabinet"; a supply's
  drop a `take` (onto a square at once; onto equipment its copy dropped, and the lab loaded back if that is
  refused); Storage's slots (`storage_count`, `shown_slots`, `slot_rect` by position), its bar and wheel; the
  search fields (`search:supplies`, `search:storage`), typed into before anything else. A supply's box is known to
  the pass apart from its first copy (`OpticsBody::named`, "supply:id"). The newest part found by its id.
- **The pictures**: `capture_one` composes the supply alone in the first box and reads the box with `optics_pixel`
  over the wall's grey (`wall_at`); within 0.002 of the pass. Keyed by FNV-1a of what is drawn (`picture_key`).
  `Ui::add_pictures`, `pictures`, `pictures_changed`, `capture_pictures`, `set_capture_ahead`; the C API's; the
  Godot host gives back `res://pictures/supplies.json` (built by `make pictures`) and `user://pictures.json`, and
  keeps what it captures. A capture costs 1 to 8 s a supply on the CPU (the free lab's twelve, 30 s; 2 s when all
  are kept).
- **Tests**: `test_boundary.cpp` (the core and C9), `test_supplies.cpp` (Supplies, Storage, the trash, save and
  load, search, the pictures); some seventy older tests restated by an agent to the new meaning (`(Stage 61: ...)`
  on each), which found the four bugs fixed above.
- **Goldens**: the pressure session's re-recorded with the user's approval: every measured value and event as
  before; its stopper and burner supplies (8 g supplied). The demo's unchanged (its bench has no supplies).

## The user's tests

- "When I press on a supply item, I want it to be picked up in my hand, already in full size and rendering": a
  press takes the copy out at once, in hand at its full size, simulated and drawn; put nowhere, the lab is loaded back.
  Then "it's invisible in my hand until I place it": it was held by its storage slot's place scaled to its full size,
  off the screen; held now where its box was pressed (the test holds it under the pointer, and seen).
- "Vessels rendered over the Supplies cabinet look super strange [...] the Supplies cabinet itself should not render
  as the background for the vessel. Same should be true for Trash and Storage": the panels' bodies carry `PANEL_BIT`,
  passed over by the pixel source wherever something in hand covers them; their outlines are drawn over the pass;
  behind glass the bench's grid goes on over them (the test: a flask held over the first box sees the same wall
  whatever supply the search puts there).

## Open

- (Settled) The free lab's step cost: idle, it steps at 0.23 µs (`chem_tool bench` of a ten-minute wait on
  `data/benches/lab.json`), against 18.7 µs on Stage 53's build and 6.5 before its rings: none of its twelve
  supplies is in the world until one is taken out.
- `lab_tests.signals.12` (three monitors' cost at 600x) passes alone and fails under the gate's full load by a
  tenth of a millisecond (3.13 against 3 ms), as it did in Stage 53's merge: its bound is the CPU's, not the code's.
