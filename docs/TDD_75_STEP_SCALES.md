# TDD 75: A step that scales with what a cell holds (Stage 75)

## The request

After Stage 74's sign-off, the user asked which work came next, then: "Great ideas, proceed to plan them" and "If
this exploration reveals new potential ideas for work, plan those as well"; the plan approved: "Approved, proceed".
Stage 64 took the species table from 28 to 75 and the demo's step to 1.4 times its cost; every curriculum step adds
more. No behaviour of the game changes: every golden and digest as it was; GAME_SPEC stays 0.73.0.

| Phase | Topic | Status |
|---|---|---|
| 75.1 | The step's cost at every merge gate | Done |
| 75.2 | A cell's revision, and its matter revision | Done |
| 75.3 | The joint solve's mixtures once an iteration | Done |
| 75.4 | Each cell's mixtures kept: a probe's setup, the activity model's range | Done |
| 75.5 | The column's diffusion over what its layers hold; a probe's pairs past 64 | Done |
| 75.6 | Smaller: the gas exchange, a surface reaction's solvent | Done |
| 75.7 | `make gates`; the user's sign-off | gates passed; sign-off open |

Dropped from the plan, found in its exploration: ui.9, tutorial.1 and tutorial.2 do not replay the demo session
(another bench, other commands, the tutorial's monitor and rig, a person's slider): starting them from saves would
need a save of the screen and its tutorial and a setup that plays each once, so the gates' summed work would not
fall. A save of an experiment mid-play remains a possible player-facing feature.

## The cost, phase by phase

`chem_tool bench data/sessions/demo.json`, instructions a step (75.1's own line; run to run within about 0.3 %, one
in a busy moment up to 2 %):

| After | Instructions a step | Change |
|---|---|---|
| 75.1 (main's physics) | 1,136,336 | |
| 75.2, the revision | 1,041,287 | −8.4 % |
| 75.3, the joint solve's mixtures | 1,026,751 | −1.4 % |
| 75.2, the matter revision and its sums | 931,256 | −9.3 % |
| 75.4, the kept mixtures | 896,565 | −3.7 % |
| 75.5, the column's diffusion; the pairs | about 899,000 | (none in the demo) |
| 75.6, the smaller ones | 894,473 | −0.5 % |
| **The stage** | | **−21.3 %** |

The whole demo, `/usr/bin/time -l`, main against the branch back to back at a load of 33: instructions 9.37e11 to
7.40e11 (−21 %), cycles 2.68e11 to 2.25e11 (−16 %).

## 75.1 The step's cost at every merge gate

`chem_tool bench` prints `step cost: N instructions a step` (the process's retired instructions over the command
loop, `lab/work.hpp`, shared with Stage 74's test). `make gates` runs `gate_cache.py stepcost` on the demo beside
ctest (about a minute of one core): the count is kept in `~/Library/Caches/chem-game-v2/step_costs.json` by the
binary's content and the data, with what it was built from (the `native`, `data` and `bench` trees, so a commit of
documents alone, a sign-off, keeps it) when that has no edits; it is printed against the count for what main built
from where the branch left it (on main, its first parent's), with a `!!` line past `GATE_STEPCOST_WARN` (10 %). It
never fails. Main's first count is this stage's last (893,368), taken by the branch's own gate.

## 75.2 A cell's revision

`World::revision(cell)`: a number drawn from one counter for every world of the process by every write that changes a
cell's state or meta (`transfer` and `transform` that move something, `supply`, `retire`, `freeze`, a cell added, the
setters when the value changes, `refresh_sealed` where `sealed` changes, a disconnection refused). The same number is
the same state and meta, in a world and in its copies. `World::matter_revision(cell)`: the same, unchanged where only
energy moves (conduction, work).

What is kept by them: a cell's thermal state and its rest (Stage 64's `rest_key_`) by its revision, its sums over its
matter (`matter_sums`: formation enthalpy, heat capacity) and its mixtures (75.4) by its matter revision, a vessel's
walls (`set_walls`) by its cells' revisions. Each step a cell conducts along several edges and its thermal state was
derived again after each; its temperature now comes from the kept sums, the same expression on the same numbers.

Where an assertion aborts (Debug, the sanitizers), every hit is held to the full state and the kept sums and mixtures
to a fresh count: a write that drew no revision aborts there. The sanitizer gate ran with it (661 entries).

## 75.3 to 75.6

- The joint solve (`solve_together`) asked each of its reactions' affinities in turn on the same state, each building
  the cell's mixtures and the solution's excess anew: `affinities()` builds them once an iteration. `molality` no
  longer works out the excess it never read.
- A probe's setup (`AffinityProbe`) and the activity model's range (`stands`) read the cell's mixtures kept by its
  matter revision (`cell_mixtures`, `Simulation::mixtures`), not a scan of every species each time.
- A probe takes every pair only when its own pairs overflow its list of 64, not when the table's would: a table past
  64 pairs keeps its probes sparse (Stage 64's subset, the same bits by its design; the built-in table's 30 pairs take
  the same path as before).
- The column's diffusion passes over a species neither layer holds (its terms are signed zeros, its move none) and
  works out each one's diffusion once. The layers' levels stay inside the interface loop: each interface's moves
  change the next one's.
- The gas exchange tests a species' presence before its diffusion (a power); a surface reaction's solvent passes over
  an absent species before its volume at T (two of Kell's evaluations). The two copies of the exchange's cells stay:
  its moves read the state as the edge began.
- Left under 1 %: the drains' and a column's split's own thermal states, the joint solve's loops over every species.

## Tests

- `world: a cell's revision moves with every write of it, and with nothing else` (Stage 75): each writer moves it, a
  value set as it was or a refused or empty transfer does not, a copy keeps it and then draws its own, an energy alone
  leaves the matter revision.
- Every digest as it was after each commit (`make sessions-check`, the demo record's fixture, D2); the conservation
  sweeps and `check_all`'s equivalence unchanged.

## 75.7 The gates

`make gates`: the sanitizers first failed every entry that read a cell's kept mixtures. The guard compared the whole
particle array, and a `Mix` fills only as many particles as the table has (Stage 64), so the rest held whatever was
in memory. Nothing reads them; the kept mixtures now hold the table's particles alone and zeros past them. Then
every gate passed (603 s at a load of 45 to 48: not a measure of the gate), 653 entries under the sanitizers with
every guard on.

## Open

- What is left of a step: the fast equilibria's probes and Pitzer's excess at each extent, conduction, the lab's walls
  (its rings' outside, by their temperatures each step).
