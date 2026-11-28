# TDD 93: The supplies' pictures in a store every worktree shares (Stage 93)

## The request

The user's `make run` at the repository's root was slow (2026-11-28): its checkout had no pictures, so every supply's
picture was captured, the free lab's 50 one after another in one process, on a machine at a load of 80 to 100 from
the sessions' gates. The user asked, after Stage 91, for the pictures captured side by side, and proposed a shared
cache: a worktree on main's renderer reuses main's pictures, one that changes the renderer keeps its own, and a merge
replaces main's. Offered a content-keyed store instead (no picture owned by a branch, nothing to do at a merge), the
user chose it. No behaviour of the game changes: GAME_SPEC stays 0.91.0.

| Phase | Topic | Status |
|---|---|---|
| 93.1 | A bench's supplies and their keys; one supply captured alone (the screen, the C API, `chem_tool`) | Done |
| 93.2 | The store and `tools/pictures.py`; `make pictures` building only `chem_tool` | Done |
| 93.3 | Tests, docs, `make gates`; the user's sign-off | gates passed; sign-off open |

## 93.1 What there was

`make native` (and `make run`) ran `make pictures`, which depended on `make core`: the whole `build-core` with its
tests linked (`chem_tests`, `lab_tests`), for `chem_tool` alone. Then every bench of `data/` was opened by a process of
its own (Stage 84), each starting from the tree's own file (`engines/godot/pictures/supplies.json`, gitignored),
capturing what it lacked, and the files were merged (`tools/merge_pictures.py`). The free lab holds most supplies, so
its process was most of a cold capture; a supply in several benches was captured by each that lacked it; and each tree
kept its own file, so a new worktree, or the root after a merge, captured everything again though another tree had the
same pictures (Stage 86: a picture's key holds the renderer's, so the same key is the same picture anywhere).

## 93.2 The store

- **By key**: `~/Library/Caches/chem-game-v2/pictures/RENDERER/KEY.json` (XDG_CACHE_HOME elsewhere; `PICTURES_STORE` or
  `--store` over it), a picture a file as `Ui::pictures` writes it. A key is a hash of the renderer's key and of all
  that is drawn of the supply (Stage 61's `picture_key`), so a picture in the store is right for any tree whose key it
  is. Nothing belongs to a branch: a branch that changes the renderer writes under its renderer's key, which is main's
  after the merge; a change taken back finds the old ones; two trees on one renderer share every picture.
- **Captured alone, side by side**: `chem_tool capture --keys BENCH...` lists each bench's supplies with their keys
  (`Ui::picture_keys`, `cg_ui_picture_keys`); `tools/pictures.py` takes each key once (the first bench that has it),
  and captures each missing one by itself, `chem_tool capture --only SUPPLY OUT BENCH` (`Ui::capture_picture`,
  `cg_ui_capture_picture`), as many at a time as there are cores. A picture comes back checked (its renderer and key
  the ones asked for) and is written whole (a file of its own, renamed over), so trees capturing at once only repeat
  work, never leave half a file.
- **The tree's file** (what the Godot host reads) is assembled from the store: this renderer's pictures of the benches
  given, sorted by key as `Ui::pictures` writes them, written only when it changes.
- **Removed**: a renderer's directory is touched each time it is used; one unused for 30 days (`--days`) is removed.
  A renderer's 50 pictures are about 2 MB.
- **Built**: `make pictures` configures `build-core` as `make core` does and builds the `chem_tool` target alone.

## 93.3 Tests and measures

- `lab_tests` `pictures: a bench's supplies listed with their pictures' keys, and one captured alone the same
  picture as all of them captured together (Stage 93)`: two supplies, their keys the pictures' own; each captured alone
  the very picture (pixels and all) the bench's capture gives, and nothing else; an unknown supply refused.
- ctest `pictures_store` (`tools/test_pictures.py`): two benches sharing a supply against a store of its own: cold,
  three supplies, two pictures, both captured; again none, the file the same; another tree's file from the same store,
  none, the same file; a picture lost from the store, it alone captured again; a renderer unused 40 days removed, one
  used 5 days ago kept.
- The same pictures as before: the 50 of `data/`'s 53 benches (70 supplies) against Stage 84's bench-by-bench capture,
  key for key and pixel for pixel.
- **Cost** (`make pictures`, the real store, renderer `ac3b3170…`): cold, 68 s for the 50 at a load of about 30 (37 s
  at about 26 in a store of its own); again, 0.9 s, the `chem_tool` build's check included. Stage 84 measured 90 s cold
  at a load of 16 to 22, Stage 86 111 to 240 s; a tree on a renderer any tree has used: under a second.

`make gates`: every gate passed (1,576 s of wall at a load of 30 to 100); the step's cost −0.2 % against main's (no
change to the simulation). Under the sanitizers the screen's test takes 293 s and `pictures_store` 185 s (four and seven
captures in the sanitized build): over the 20 s limit, so the gates' cache leaves them to `make gates-full`.

## Open

- The Godot host still keeps what it captures while the game runs (a supply first seen in play) in its own `user://`
  file, not the store: the store is a build's.
