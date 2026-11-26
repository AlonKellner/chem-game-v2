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
| 61.1 | The core: add a cell, supply, retire, freeze; the ledger; C9 and its self-test | |
| 61.2 | Supplies: templates in worlds of their own; taking one out (graft); implicit take-out | |
| 61.3 | Storage: the outbox's slots unbounded, goals on any, `stow` to it, `place` from it | |
| 61.4 | The trash: frozen, restored as it was, emptied out of the world | |
| 61.5 | Save, load and digest: the ledger, the frozen, the retired, the supplies' copies | |
| 61.6 | The screen: the two panels scrolled, their search fields; `lab/search.hpp` | |
| 61.7 | The pictures: captured on the CPU, cached, built for `data/`'s benches | |
| 61.8 | Benches, tutorials, sessions and plays; the goldens (with the user's approval); the host | |
| 61.9 | GAME_SPEC 0.61.0, CHANGELOG, `make gates`, the user's sign-off | |

## Open

- The free lab's step cost: measured once its cabinet is Supplies (it stepped at 16 µs, 6.5 before Stage 53).
