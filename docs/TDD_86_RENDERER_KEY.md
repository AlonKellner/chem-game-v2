# TDD 86: The supplies' pictures keyed by their renderer (Stage 86)

## The request

Stage 84's Open: a picture's key was the supply's look and contents (and a version, `pictures: 3`), not the renderer's
code, so a change to the renderer kept the pictures already captured until their file was removed. The user,
2026-11-27: "Approved, merge and proceed to the renderer hash fix". No behaviour of the game changes; GAME_SPEC stays
0.81.0.

| Phase | Topic | Status |
|---|---|---|
| 86.1 | The renderer's key: a hash of the files a picture is rendered by, made at build time | Done |
| 86.2 | In each picture's key, and in each file: another renderer's pictures taken as none | Done |
| 86.3 | `make pictures` end to end: a renderer's change captures every picture again, the old left behind | Done |
| 86.4 | `make gates`; the user's sign-off | Done (signed off 2026-11-27) |

## 86.1 The renderer's key

`native/cmake/renderer_key.cmake` hashes a list of files, each by its name (relative to the repository, or a generated
file's own name) and its contents' SHA-256, into one SHA-256, of which the header `lab/renderer_key.hpp` (generated)
keeps 16 hex digits, `RENDERER_KEY`, and the files' count, `RENDERER_FILES`. The build runs it once any of the files
has changed: a build with none changed does nothing.

The files (`native/CMakeLists.txt`, `CHEM_RENDERER_FILES`, a glob: a new file of the optics is taken in by its name),
49 at this stage:

- the per-pixel source (`native/game/shader/*`) and every `optics_*` source and header of the lab (the truth's tracer,
  the reference, the scene's composition, the single-precision pass, the frames);
- colour (`colour.cpp`, `colour.hpp`), what lies in a vessel (`contents`), the glassware (`glassware`);
- the core's optics and math (`optics`, `math`);
- every file of `data/optics/`, and the two tables made of the data: `builtin_optics.cpp` (the media, the CIE observer,
  D65, each species' medium) and `glassware_built.inc` (every shape of `data/glassware.json`).

Why a list of files, not the include closure: the closure of the renderer's includes, each header with its source, is
nearly the whole library (`optics_scene.hpp` names the lab's views, and so `lab.cpp`, `sim.cpp` and the rest), and a key
of it would capture every picture again at almost any change. Why not a hash of the composed scene: it is exact for
what reaches the pixels, but a scene holds the whole screen's wall (the outbox's slots differ by bench), so one supply's
scene differs between benches and the benches' captures would undo each other.

What the key leaves to its version, `pictures` (3): `ui.cpp`'s own part, which bodies a view composes for a supply in
its box. It changes in most stages and seldom touches a picture; a change that does bumps the version.

## 86.2 Each picture's key, and each file

`picture_key` holds `RENDERER_KEY`, and `Ui::pictures` writes it at the file's top (`"renderer"`). `Ui::add_pictures`
takes a file of another renderer's, or one older than the key (none), as none: every supply is captured again, and what
the screen gives is its own renderer's alone. So both files a host keeps, the pictures built ahead
(`res://pictures/supplies.json`) and its own (`user://pictures.json`), leave the old pictures behind at the next one a
renderer's change makes. `chem_tool capture` writes what the screen gives always (it wrote the file back as it read it
when it captured nothing), and `tools/merge_pictures.py` refuses files of two renderers. `make pictures` merges the
benches' files alone: each already starts from what the file held.

Tests: ctest `renderer_key_moves` (the hash: the same files read from elsewhere, the same key; a byte changed or a
name, another; lists that do not pair up and a file that is not there, refused); `lab_tests` "pictures: a picture's
key holds the renderer's..." (the key's form and its files' count; a file given back is taken whole; one of another
renderer's, or one with no renderer, is taken as none, each supply drawn live by the pass until captured again, and
what is given is the screen's renderer's alone).

## 86.3 End to end

`make pictures` in a new worktree on Stage 81's supplies (45 pictures), a load of 14 to 21 from other sessions' tests:

| Run | Time | The file |
|---|---|---|
| Cold | 111 s | renderer `bc1b0141…`, 45 pictures |
| Again, nothing changed | 1 s | the same |
| A line added to `optics_trace.cpp`, rebuilt | 114 s | renderer `9061d440…`, 45 (the old 45 left behind) |
| The line taken out, rebuilt | 240 s (the load at 21) | renderer `bc1b0141…`, 45 |
| Again, nothing changed | 1 s | the same |

The key comes back with the files: the same files, the same key, in any worktree (the names are the repository's).

## 86.4 The gates

`make gates`: every gate passed, in 1,222 s of wall at a load of 14 to 21 from other sessions' tests (each entry two to
three times its time on a quiet machine). The Godot suite: 13 passing. The step's cost: 1,222,435 instructions a step on
the demo, no count yet for main's at bcb342b (the step is not this stage's: the renderer's key reaches no process).

## Open

- Pictures of the same renderer no supply of any bench shows any more (a supply's look or contents changed) stay in the
  file until the next change of the renderer.
