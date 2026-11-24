# TDD 39: A controller's trial beside the lab (Stage 39)

## The request

The user, 2026-11-24: "Approved, merge and proceed": Stage 36's request still open, "making all calculations
have a constant frame cost, regardless of the replay speed". What Stage 36 left (TDD 36, Open): a
controller's trial, 54 ms inside one step at any speed; the stirred hand pour's frames of 18 ms under the
budget. GAME_SPEC §13.3, §19.

| Phase | Topic | Status |
|---|---|---|
| 39.1 | Measured: where a trial's time goes | Done (below) |
| 39.2 | The trial beside the lab: two of its seconds a step; saved, digested; untuned meanwhile, or its gains held | Done |
| 39.3 | The budget's second piece sized from what the first step cost | Done |
| 39.4 | The stirred pour's steps | Measured; left (below) |
| 39.5 | GAME_SPEC §13.3, §19; `make gates`; the user's sign-off | `make gates` passed; signed off 2026-11-24 |

## 39.1 Measured

The monitored scene of `chem_tool study speed` (three monitors and a controller holding the oxide's
dissolving at a temperature), its rig: the trial ran 38 of its seconds on each of its two copies (4,560
steps), 40 ms of steps and 10 ms of the copies' whole views read each second; copying the lab 0.7 ms. Nothing
in it is slow but its length: a step of a copy costs what one of the lab's does (9 µs). A process that does not
level off runs the whole hour: 3,600 seconds, some four seconds of a frame.

## 39.2 The trial beside the lab

- `tune` makes the trial (`Lab::Impl::Trial`: the copies, the readings so far, the distance to the target, what
  the item held as it started, whether the controller was tuned before) and returns; `run_trials`, at each of
  the lab's steps after the world's, runs each trial `TRIAL_PER_STEP` (2) of its seconds, and an ended trial is
  fitted (`trial_ended`: the fit, SIMC's gains, the notice, as before). A copy's reading is the part's own
  (`reading`: the item alone, without the shape searches), not its whole view.
- **A controller set or rigged** is untuned until its trial ends (its level 0: G3 as before); `Part::trying`
  says its trial runs; the screen's word for it is "trying", its editor's line "trying what it controls, to
  tune itself", the debug panel's the same.
- **A controller tuned again** (what it controls changed, Stage 28) keeps its gains while its trial runs, and
  is not watched again meanwhile; at the end it takes the new with its level carried over (its integral set to
  give it), as before at once.
- **What a trial holds is the lab's state**: saved (`bench.trials`: each copy's own save whole, the readings,
  the distance, the basis) and loaded; in the digest (each copy's own digest, the readings); run by the lab's
  steps, so the same however the steps are grouped (D1). A lab with no trial saves and digests as before.
- **Dropped** when its controller is unset (`unsense`), senses another item, is scrapped, put in the trash
  (`bin_part`), or tuned again from the start (`set`, a rig made or taken off).
- No session record has a controller: `make sessions-check` unchanged. The fits are those of before (the
  controller tests' gains and settling times the same to the digits printed).
- Tests (`test_controller.cpp`): the existing cases step until no trial runs (`tried`: at most 2,000 steps, an
  hour's trial being 1,800); a new case: untuned and doing nothing while it tries, the same digest for five
  steps run at once or one by one, saved mid-trial and loaded it goes on digest for digest and is tuned at the
  same step, unsensed mid-trial it is never tuned; the tuning again: its gains held while it tries, no bump
  after. `test_ui.cpp`: the person's controller is "trying" once rigged, tuned within the ten seconds at 600×.

## 39.3 The budget's pieces

`Ui::frame` ran the first step alone, then eight, then as many as the time left held. Eight dear steps (the
stirred pour's, 2.2 ms each; a trial's, 2.3) were a frame of 18 ms. Now after the first, each piece is as many
as the time left holds at what the steps so far cost, eight at most until nine have run. `test_speed.cpp`
holds as it was.

## Measured (`chem_tool study speed`, M3 Pro; ms a frame, the mean and the worst)

| Scene | 600× under 3 ms, Stage 36 | Stage 39 | Speed kept |
|---|---|---|---|
| The oxide dissolving, three monitors and a controller | 3.11 (rig: 51 ms in the command) | 3.16 (4.89) | 563× |
| Water and sediment poured by hand, stirred | 4.65 (18.3) | 4.31 (5.97) | 559× |

A trial's step: 2.2 to 2.5 ms (two of its seconds on two copies), nineteen steps for the scene's trial.

## Open

- **The stirred hand pour's steps** (2.2 ms while it pours, against 10 µs): the vortex's share is worked out
  again at nearly every step (1,051 times over the scene: 957 because what the liquid holds up moved by a
  thousandth, 418 because the contents' signature moved: the volume by a two-hundredth). Ways: the share
  anchored as `by_hand` anchors its volumes (what is held up to the nearest sixty-fourth, the share between
  carried by the volumes), or a table of the share by volume and what is held up per turn. Under the budget the
  scene keeps 559× and its worst frame is 6 ms.
- **The GPU's alternating frames at 1×** while a vessel is stirred (TDD 36, Open): unchanged. The host uploads
  the scene only on the frames it changed and reads nothing back; the CPU's part of a frame is 0 to 1 ms; the
  GPU's own time a pass is not known on this backend.
- **A trial at 600×**: nineteen steps of 2.3 ms; under the budget the frames run one or two steps each while it
  runs, so the clock slows for a tenth of a second.
