# TDD 69: A loaded lab is the saved one (Stage 69)

## The request

The user, 2026-11-26, signing off Stage 68: "Approved, merge and proceed to fix the open bugs": the two that Stage 68
found and left open (`docs/TDD_68_GATES_FAST.md`, "Open"). GAME_SPEC 0.69.0 (§13, Save and load; §18, D2).

| Phase | Topic | Status |
|---|---|---|
| 69.1 | A save made in a step in progress continues as the saved lab would have (D2) | Done |
| 69.2 | A loaded lab's first view is the saved one's: its boiling's bubbles | Done |
| 69.3 | `make gates` (916 s, the machine at a load of 45 to 63); the user's sign-off | signed off 2026-11-26 ("Approved, merge and proceed") |

## 69.1 A save made mid-step

**Found** (Stage 68, while D2 was made to start from a save at every command): the demo saved after its command 1,
the malachite just tipped into the flask, did not continue as its record does: command 2's step gave another digest.
Main's own build did the same.

**Why.** The tip moves matter by the world's primitives, so a step is in progress: the world's ledger holds the
tip's transfers, and the world refuses a change of its connections until the step closes (`Refusal::MidStep`),
`set_conductance` among them. At command 2's step the lab sets each vessel's walls (`set_walls`): the saved lab's
world, its ledger not empty, refused the rings' contacts' new conductances, and they were set at the next step; the
loaded world's ledger was empty (a save keeps the lab's `mid_step`, not the ledger), and it set them at once. A
step of difference in six conductances, and the digests parted.

**The fix.** `World::resume_step`: a step in progress since before the world was saved. Every refusal mid-step reads
it (`stepping()`, and `mid_step()`), and it ends when the step closes (`close_step`, `skip_step`). `Lab::load` calls
it for a save made mid-step. The loaded world refuses what the saved one refused, and sets the walls when it did.

**Held.** D2 (lab.13) now loads the demo at every command (26, not six) and plays each into the next command's
digest, every invariant checked: the same work as before (the windows divide the session among eight entries).
Without the fix it fails at command 2; a world test holds the resumed step's refusals (`world: a step resumed after a
load refuses what a step in progress does, until it closes`).

## 69.2 A loaded lab's first view

**Found** (Stage 68, while the O2 scan was made to read scenes made from saves): a lab loaded while a vessel boiled
showed no bubbles until its first step.

**Why.** A vessel's bubbles are made, each view, from the last step's flows (the heat its burner and its floor's
contacts passed, a reaction's gas) and from its contacts. A save held neither: the flows are not state, and the
contacts were found again at the loaded lab's first step.

**The fix.** `Lab::save` keeps the last step's flows (not in the digest: no golden moves), and `Lab::load` puts them
back (`Simulation::restore_flows`, only where they fit the world: as many edges and cells, or fewer, as a command
since the step may have added some) and finds each vessel's contacts as it loads (from the world's edges, as the
first step would have). Everything read from the flows reads as before the save: the bubbles, a reaction's rate in
the debug panel.

**Held.** `bubbles: a boiling beaker saved and loaded shows the bubbles it showed`: a beaker boiled to the surface,
saved and loaded, shows the same bubbles, floor and digest, and saves the same text again.

## The gates

`make gate` passed at each commit; the goldens unchanged (the sessions never save and load). `make gates` passed:
916 s, other worktrees' gates on the machine (a load of 45 to 63), and the sanitizers' tree of this new worktree
not yet measured, so its own limit left nothing out (730 entries ran, 35 left to the full gate by the release
tree's costs; the next run has its costs). Open: ctest's measured costs kept for every worktree, as the result
cache is, so a new worktree's first gate is as short as the others'.
