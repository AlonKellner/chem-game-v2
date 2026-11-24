# TDD 38: What was left (Stage 38)

## The request

The user, 2026-11-23: "Let's tackle the rest of the remaining work"; chosen: the trash's leftovers (TDD 34,
Open), the cleanup and the documents (TDD 35 and its explorers' lists).

| Phase | Topic | Status |
|---|---|---|
| 38.1 | The trash: a part's wires and rigs to outside; emptying it; a vessel bursting in it; its list in the C API | Done (below) |
| 38.2 | Cleanup: the old maps' directory, `through_layer`, the tests' words of maps and the chain | Done |
| 38.3 | The documents: GAME_SPEC's stale tags and numbers, TDD 33 and 35's notes | Done |
| 38.4 | `make gates`; the user's sign-off | `make gates` passed; awaiting the user |

## 38.1 The trash

- **A part's wires and rigs to what was outside it come back with it.** `bin_part` keeps every wire and rig
  that touches what goes in (it kept only those wholly inside, and took the others off for good);
  `unbin_part`, whenever anything leaves the trash, puts back every kept wire and rig whose two ends are on
  the lab again, and drops those to what is gone for good. A part restored before what it was wired to has
  its wire back when that is restored too. A controller whose rig comes back tunes itself, as when it is
  rigged. `test_trash.cpp`: the parts' case and the circuit's case now come back joined as they were.
- **`empty_trash`** (a lab command, GAME_SPEC §13) and the trash box's **"empty"** button, shown while
  something is in it (`Ui::locate("trash:empty")`): everything in it leaves for good, as the oldest does past
  the trash's capacity, an item to the cabinet with all it holds, a part scrapped with what joined it; a note
  says how many. Refused when the trash is empty. Tested in `test_trash.cpp` and `test_ui.cpp`.
- **A vessel bursting in the trash** (`lab.cpp`, the burst: `forget_trash`, then to the cabinet, broken)
  cannot happen with the data as it is: nothing in the trash is heated (a burner under it is refused), and no
  reaction gives gas cold. The line stays, as a guard; a test would need a reaction or a bench that does.
- **The trash's list in the C API**: it is in the lab's report (`cg_lab_report`, `cg_ui_report`: `trash`,
  oldest first, each element with whether it is a part and the square it came from). Nothing to add.

## 38.2 Cleanup

- `engines/godot/data/maps/` (406 refraction maps made before Stage 35, untracked): the data copy
  (`make godot-data`, `rsync --delete`) no longer keeps it, so it went with the next copy.
- `through_layer` in the per-pixel source, read by nothing: deleted; its comment's mention made into what it
  said.
- The tests' comments and names that still said a map or the chain does what the pass's rays do now
  (`test_footprint`, `test_layers`, `test_truth3d`, `test_refraction`); notes of what was so before Stage
  35 are kept as history.
- Kept on purpose: the body's vec4 6 (the old map slot) is unused and named so in `optics_scene.hpp` (every
  reader names the slots by number: closing the gap is a change to the shader, the reference, the host and
  the tests for no gain); `optics_work` and `OPTICS_WORK_HOOK` (`test_cost.cpp`'s count of what a ray tries).

## 38.3 The documents

GAME_SPEC: §4.5's crystal populations, §8's solute activity and §9's decanting are no longer *PLANNED*
(built in Stages 6, 19 and 31); §19's frame is 120 a second with the lab's budget in it; §20 and O4's row say
the GPU's few off pixels as they are; O14 said to be unused. TDD 33's and 35's open notes say what has since
been done. The Makefile's screenshot comment says fourteen pictures.
