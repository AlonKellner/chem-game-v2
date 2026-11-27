# TDD 84: The Godot gate, the engine's part alone (Stage 84)

## The request

After Stage 80, the gate's Godot step took 243 to 326 s whenever it was not cached, as long as the native tests. The
user, 2026-11-27: "I approve, start with the godot gate. In general, this repo should be as engine agnostic as
possible, native first, thin godot wrapper". No behaviour of the game changes; GAME_SPEC stays 0.80.0.

| Phase | Topic | Status |
|---|---|---|
| 84.1 | Measured: where the step's time goes | Done |
| 84.2 | The gate builds and runs only what needs the engine | Done |
| 84.3 | The suite captures no picture; `make native` captures them side by side | Done |
| 84.4 | `make gates`; the user's sign-off | gates passed; sign-off open |

## 84.1 Where the time went

A new worktree's `make native`, each part timed:

| Part | Time |
|---|---|
| Configure | 1 s |
| Build (1,161 objects, godot-cpp among them, from the shared compiler cache) | 9 s |
| The data's copy | 0 s |
| The supplies' pictures (`make pictures`) | 182 s |
| Godot's import | 9 s |
| The GUT suite (with the pictures present) | 5 s |

The pictures are the supplies' pictures in their cabinet (Stage 61): each bench opened and each supply captured by the
renderer's own source on the CPU, kept in a file by a key of the supply's look and contents. A new worktree has no
file; a branch whose supplies changed captures them again (Stage 80's gate: four benches). The suite reads none.

## 84.2 The gate

`gates.sh`' Godot step runs `make native PICTURES=0`: the extension, the data and the import, no capture. With no
pictures file, the host captures what it lacks as it runs (`cg_ui_set_capture_ahead`): a new worktree's suite took
38 to 42 s. The host has a property for it, `ChemLab.capture_pictures`: on as the game runs, off in the suite's
`before_each`, as Stage 68 did for the experiments' catalogue. The two tests that hold the host's defaults "as opened"
hold this one's too (on), then turn it off.

A new worktree's Godot step: the build 15 s, the suite 4 s, against about 200 s.

## 84.3 The pictures for the game

`make native` (and `make run`) still capture the pictures, now bench by bench side by side. Each bench starts from
what the file holds and writes a file of its own (`build/pictures`), and `tools/merge_pictures.py` merges them by
key, sorted as `chem_tool capture` writes them. Cold: 90 s against 148 s serial, at a load of 16 to 22; the same 36
pictures, the file byte for byte the serial capture's. One bench (the free lab, with most supplies) is most of it.
Again with every picture kept: 0 s.

## 84.4 The gates

`make gates`: every gate passed. The Godot suite ran (the host changed): 13 passing in 3 s, and the whole Godot step
5 s. The native entries came from the cache: their binaries are main's, byte for byte.

## Open

- A picture's key is the supply's look and contents (and a version, `pictures: 3`), not the renderer's code. A change
  to the renderer keeps the pictures already captured until their file is removed.
