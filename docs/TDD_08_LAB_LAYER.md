# TDD 08: The rest of the lab layer (Stage 8)

## What Stage 8 delivers

Stage 7 ran the demo. Stage 8 is what playing it needs from the engine-free layer, before any
host draws it: where things stand, what happens when a vessel or a balloon is overfilled, and
saving a lab. It begins with the step cost that Stage 7 left at its budget.

| Phase | Topic | Status |
|---|---|---|
| 8.1 | Step cost: the gas tree cached per topology, a cell's thermal state derived once, reactions that cannot run skipped | Done |
| 8.2 | Overflow: a rigid cell past its brim spills into the world's spill cell (`Simulation::overflow`) | Done |
| 8.3 | The bench's tray; a balloon's capacity and bursting; events | Done |
| 8.4 | Places: the cabinet, the bench's grid, on, under, the outbox; `place` and `stow` | Done |
| 8.5 | Save and load (`chem-save/1`), through the C API too | Done |
| 8.6 | Invariant D2; V2 with the spill cell | Done |
| 8.7 | The demo's golden record re-recorded | **Needs the user's approval** |
| 8.8 | The user's sign-off | Pending |

## The golden record changed

`bench/sessions/demo.jsonl` is different from Stage 7's. A changed golden is a behaviour change
and is the user's to approve (docs/WORKFLOW.md); it is committed on this branch so the branch is
green, and nothing is merged until it is approved.

**What changed, and why:**

1. The bench has a tray. That is one more cell in the world (300 g of glass at room temperature,
   joined to the room). The digest covers every cell, so every digest changed.
2. Handing in the flask now lifts it off the burner, which is a change of connections and takes
   the lab to a step boundary: the session ends at step 156,303, not 156,302.

**What did not change:** `chem_tool run data/sessions/demo.json --quiet`, saved before and after,
differs in three places only: the step count (by one), and the world's totals of Si, O, energy
and mass (by the tray's glass). The 22 lines that report the three items handed in are identical,
to the last printed digit: 0.895898 g of CO₂, 40.868202 g of water, 8.934965 g of CuSO₄·5H₂O in
447,472 crystals.

The caching of phase 8.1 was done first and separately, against the Stage 7 record: it reproduced
it digest for digest.

## Decisions

- **The cost is in the physics, not the bookkeeping.** Profiling put most of the time in the
  logarithms of the affinity evaluations of the phase equilibria. Caching the gas tree and the
  thermal states took 4.5 µs to 3.8; a per-cell thermal cache and a reordered search then gained
  nothing measurable and were kept only where they simplified.
- **Overflow is the simulation's, bursting is the lab's.** A vessel past its brim would violate V2
  at the end of the very step that filled it, so the spill has to happen inside the step. A
  balloon past its capacity violates nothing in the world; it is equipment failing, between steps.
- **A burst balloon comes off first.** The vessel's mouth opens to the room before the balloon is
  torn, so the vessel is never sealed and never left joined to a cell that is no longer a balloon.
- **Places do not decide physics.** Where an item stands is lab state, saved, reported, and not
  in the digest. A beaker is under a condenser because of the `under` command, not because of its
  grid cell. This keeps the demo's script and its physics independent of layout; adjacency rules
  can come with the host if play wants them.
- **Handing a vessel in lifts it off its burner.** The first version refused it; that made the
  demo's last command fail for a reason no player would accept.
- **A save holds whether a step is in progress.** A pour leaves one. The first save/load lost it,
  and a lab loaded just after a pour skipped the step the original took before its next change of
  connections. D2 found it: the demo is saved at six points, one of them right after the pour.
- **Numbers in a save are bits.** Conductances, volumes and carried remainders are written as the
  16 hex digits of their float64, so loading cannot round them.

## Evidence

- **D2:** the demo saved after commands 0, 4, 9, 12, 17 and 20, loaded, and continued:
  every later digest equals the record's. Saving the loaded lab gives the same text.
- **Overflow:** 60 g of water distilled into a 20 mL cup: the cup ends full to the brim (20.000 mL,
  no gas), over 30 g is in the tray, V2 holds on every step.
- **Bursting:** a 300 mL balloon on a flask of boiling water bursts once, comes off, and its gas
  is the room's; the flask is never sealed; the totals are unchanged.
- **Places:** about twenty commands on a small bench exercise the cabinet, the grid, joined items and the
  outbox.
- **Step cost:** 4.1 µs a step for the demo (nine cells with the tray), 2.5 ms a frame at 600
  steps a frame.

## Known limits

- **One item to a grid cell, and no adjacency.** A condenser can go on a flask from anywhere.
- **A burst is instantaneous and silent** in the model; the event is there for a host to show.
- **Step cost has about a fifth of its budget to spare.** A cheaper logarithm, or evaluating the
  phase equilibrium only in cells that changed, are the next places to look if a frame needs it.

## Tests

- `lab: …` (5 new cases): placing and using equipment, a balloon bursting, a vessel overflowing,
  save and load against the golden record, saves that are not saves
- `lab: a malformed bench …`, `lab: a refused command …`: more cases for the grid and the tray
- `capi_c_smoke`: a lab saved and loaded from C
