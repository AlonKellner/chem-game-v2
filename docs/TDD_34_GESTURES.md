# TDD 34: The screen's gestures (Stage 34)

## The request

The user, 2026-11-23, having signed off Stages 29 to 33:

- a condenser's outlet is led to a vessel by a line dragged from it, as a signal's wire or a sensing
  line is, not by putting the vessel down on the condenser;
- a part is taken from the drawer with a press and put down with another, as an item of the cabinet
  is, not only dragged out;
- a trash: where what is not wanted on the screen is put, by carrying it there or by Backspace; what
  was put there last is seen and may be taken up again; Ctrl+Z brings it back; its history is long (64).

GAME_SPEC §13 (the commands `undrain`, `trash`, `restore`), §13.1 (the gestures).

| Phase | Topic | Status |
|---|---|---|
| 34.1 | A part from the drawer: a press, and a press | Done (`test_ui.cpp`) |
| 34.2 | A condenser's outlet: a line dragged from its end; `undrain` | Done (`test_ui.cpp`; the tutorial's step) |
| 34.3 | The lab's trash: `trash`, `restore`, 64 kept, saved and digested only when not empty | Done (`test_trash.cpp`, 11 cases; both records reproduce) |
| 34.4 | The trash on the screen: its box, carrying to and from it, Backspace, the undo key through the C API and the host | Done (`test_ui.cpp`) |
| 34.5 | The user plays it | Signed off (2026-11-23) |

## What is in the tree

- **The drawer** (`Ui::Impl::making`, `making_carried`): a press and its release on a part of the drawer
  puts it in hand; the next press's release is where it goes; Escape, or a press off the bench, puts it
  back. Dragged out it goes where the button is let go, as before.
- **The outlet** (`outlet_rect`, `draining`, `drop_draining`; `locate` knows `outlet:<id>`): the end of a
  condenser's outlet is taken hold of as a part's port is. Let go on a vessel, `under` (after `undrain`
  where it ran into another); on nothing, `undrain`; on what is no vessel, said. A vessel put down on the
  condenser no longer stands under its outlet, and is told how.
- **`undrain {of}`** (lab): takes away what an outlet runs into; both vessels stay.
- **The lab's trash** (`lab.hpp`: `TRASH_MOST`, `LabView::trash`, `ItemView::place == "trash"`): an item
  goes as into the cabinet and goes on there as there; a part is moved out of the lab's parts into a
  list of its own, so nothing that goes over the parts (the signals' step, G1 to G4, the squares, the
  digest) knows it, with its settings kept and its wires and rigs to anything outside taken off. A
  timer keeps what it has left. `restore` puts back the last, or the one named, where it stood.
- **The trash on the screen** (`TRASH`, at the cabinet's foot; `trash_top`, `hidden`): what was put
  there last stands in its box, with how many are in it; it is taken up as an item of the cabinet is.
  Backspace with nothing being typed puts what is in hand, or what is chosen, there; the undo key
  (`Ui::KeyUndo`, `CG_KEY_UNDO`; the host's Ctrl+Z and Cmd+Z; Delete is Backspace) brings the last back,
  chosen. A tutorial's step asks for neither.

## Open

- A trashed part's wires and rigs to what was outside it do not come back with it.
- A part leaves the trash for good only when 64 newer things are in it; there is no emptying it.
- A vessel that bursts in the trash goes to the cabinet broken: one line, with no test.
- The C API does not give the trash's list (the screen has it from the lab's view).
