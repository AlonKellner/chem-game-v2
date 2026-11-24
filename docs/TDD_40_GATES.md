# TDD 40: The gates as fast as the machine allows (Stage 40)

## The request

The user, 2026-11-24: "The full gates take a while to run. Please try to optimize them such that they'll run
as fast as possible". No behaviour of the game changes; GAME_SPEC stays at 0.39.0. `docs/WORKFLOW.md`.

| Phase | Topic | Status |
|---|---|---|
| 40.1 | Measured: where the gates' time goes | Done (below) |
| 40.2 | A ctest entry for each lab test case; parts and threads by marks; the counts checked | Done |
| 40.3 | The sanitizers beside the rest from the start | Done |
| 40.4 | Work that held nothing: the demo's replays checked again, the sanitizers' dense loops | Done |
| 40.5 | `make gates`; the user's sign-off | `make gates` passed (320 s) |

## 40.1 Measured (M3 Pro: 6 fast cores, 6 slow)

`make gates` took 614 s: the core build, then the release ctest (165 s) beside the Godot suite, then the
sanitizers' build and ctest (431 s), one after the other.

- **The release ctest kept 9 of the 12 cores busy** (1,446 s of CPU in 163 s), and its last 100 s were one entry
  alone: `lab_tests.optics`, 165 s, of which one test case, "through the demo the screen's scene holds O1 and
  O2", was 139 s and 580 s of CPU (40% of the run). It checks O2 at every 17th pixel (4,700) of every scene of
  the demo (23), each pixel at 801 wavelengths. Its time is the tracing through the glass, nothing else
  (profiled), and two pixels of the 4,700 take 1.6 to 3.7 s each, at every scene.
- **The sanitizers' ctest kept 7 of 12** (2,972 s of CPU in 401 s); its last minutes the same entry, 351 s.
- Each case of a file ran in its file's one process, one case after another.

## 40.2 A ctest entry for each case

- `chem_add_cases` (`native/CMakeLists.txt`): for each source file of `lab_tests`, an entry for each test case,
  `lab_tests.<file>.<n>`: doctest's `--source-file` and `--first=n --last=n` (the n-th case of the file passing
  the filters, by line). The cases are counted at configure time from the lines that start with `TEST_CASE(`;
  `lab_tests.<file>.count` checks doctest's own count against it (so a case written otherwise, or added
  without configuring again, fails the gate); every entry fails if it ran no case (doctest passes a range
  past the last); an edit to a test file configures again (`CMAKE_CONFIGURE_DEPENDS`). `chem_tests` stays an
  entry a file (its longest, 69 s under the sanitizers, is under the run's length).
- **One thread a case** (`LAB_THREADS=1`): side by side the cases fill the machine, and the reference's threads
  pay little where every core is busy (TDD 22: 20 s on one thread, 7.6 s on four, 14 s on twelve).
- **Marks.** `// ctest: parts N` before a case: N entries with `LAB_PART=i/N`, the case doing the i-th share of
  its heaviest loop (`tests/support/part.hpp`, `part().mine(k)`: by a splitmix hash of k, so the dear items that
  lie together are shared). The O2 scan is parted 12 ways by each scene's pixel (by the pixel alone the parts
  took 46 to 190 s: the dear pixels went to a few); "every pixel's colour is what its rays bring, through what
  is held" 2 ways. `// ctest: threads N`: N threads, N processors to ctest: "through a flask's curved rows" (4),
  whose "at most one pixel in 500 visible" is a statistic of the whole loop and so is not parted (tried:
  parted 4 ways a part had one visible in 341, and its limit was none).
- ctest runs the longest entries first once it has their times (`CTestCostData.txt`, kept in the tree).

## 40.3 The sanitizers beside the rest

`gates.sh` at the merge level starts the sanitizers' build and ctest first, in the background, then the core
build, the release ctest and the Godot gate: the cores are kept busy by both runs, and neither waits on the
other's last entries.

## 40.4 Work that held nothing

- **The demo's replays unchecked where the record is what is held.** "The demo session reproduces its golden
  record" and "a saved lab, loaded, continues exactly" (its six replays) hold the record's digests; the O2 scan
  holds the screen. The lab's state is the same checked or not (D1, held by its own test), and `demo_session`
  (`chem_tool run`) checks every invariant at every step of the same session. Unchecked: the scan's replay 14 s
  to 5.6 (a part each), the two lab cases from 148 s and 125 s under the sanitizers to under a minute.
- **The sanitizers' dense loops.** A parted case runs its first part only, on four threads (a random twelfth of
  the O2 scan's pixels over every scene, dense across: under the sanitizers it was every 51st pixel across,
  now every 17th, a twelfth of them). `things`' truth loop (`worst_of`) and O6's scan take every `sparse()`-th
  pixel across under the sanitizers, as the other truth comparisons did since Stage 22.
- Left whole: the prism's bands (`footprint`), whose "one band in 200" is a statistic; O7 through six layers;
  the kept beams through the play (a play is frame after frame).

## Measured

| | Before | After |
|---|---|---|
| `make gates` | 614 s | 320 s |
| the release ctest alone | 163 s (1,446 s of CPU) | 147 s (1,522 s) |
| the sanitizers' ctest alone | 401 s (2,972 s of CPU) | 201 s (about 2,000 s) |
| the release ctest's longest entry | 165 s | 93 s (a twelfth of the O2 scan) |
| the sanitizers' longest entry | 351 s | 207 s (`kept`: 206 s) |

## Open

- **The O2 scan through the demo is half the release ctest's work.** Ways, each a change to what it holds,
  for the user to choose: O2 at the spectra's own 1 nm (401 wavelengths, half the work; the tables are at 1 nm
  and between them is their interpolation); every 24th pixel (half); or the scenes whose pixels can have
  changed only (a pixel's key of what it sees, as the kept beams' shape key, with the contents).
- The sanitizers' longest entries (`kept` 206 s, the prism's bands 200 s, `things` 150 to 180 s) are under its
  run's length beside the release ctest; alone they would be its tail.
