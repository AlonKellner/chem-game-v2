# TDD 33: The pass made fast (Stage 33)

## The request

Asked by the user, 2026-11-22, having seen the renderer that traces a pixel's own rays in the game
(Stage 29, `LAB_BEAM=5`; TDD 29's open phase 29.9): "plan an extensive optimization of the renderer to
make it run smoothly on the GPU, while still looking as good as it does. I want high fidelity, a high
frame rate, accurate settle state, and conserved invariants."

Decided with the user (2026-11-22):
- **120 frames a second everywhere** (8.3 ms a frame): settled, carrying, tilting, pouring, stirring.
- A whole-screen restart may come in over a few frames; no frame runs long.
- This Mac (M3 Pro, 1920 x 1080) only; no quality setting.
- End with one renderer: the maps, stacks, the store and their tests removed (Stage 34).

GAME_SPEC §12.2, §12.3, §18 (O12 to O15), §19.

| Phase | Topic | Status |
|---|---|---|
| 33.1 | Measuring: a play timed on the GPU; the reference's counters | Done |
| 33.2 | A ray made cheap | Done: the torus in closed form, surfaces by height, eight meetings allowed |
| 33.3 | Corrections apart from bands | Done: one ray a correction; one band in four where a beam's rays go one way, every band where they part (the picture in motion as it was) |
| 33.4 | Beams kept; shape and colour keys; corners shared; O12 | Done, in the reference and on the GPU: corners shared; a pixel's record in a pool by rectangle; shape tags apart from content tags, a record kept through a change of what lies in a vessel that its rays pass by |
| 33.5 | The pass's shape; the budget over frames | Done in part: a pixel stepped once a frame however many rectangles hold it; the chain's pass not run; pixels beyond a body's outline not its own; the rays' own budget, batches by tile. Not done: `path_of` once a pixel, beams traced counted in the budget |
| 33.6 | The reserve, where a gate failed | Used: corrections wait a frame where a pixel's rays have moved; the fixed estimate reads a footprint as one quad; the wall read as one radiance where it is one (below) |
| 33.7 | Pictures and O4 under this reading; the documents; the user's test | O4 under this reading: 2 pixels of 41,000 to 80,000 visibly off, at one place (Open); the user's test to come |

**Where it stands (2026-11-23, the second day's work): 8.3 to 8.4 ms a frame settled, carrying, pouring,
resting and stirring (the medians; 62, 97 and 58 at Stage 32), against the 8.3 asked.** Single frames
still run to 14 to 16 ms: where a vessel that has just come to rest takes its corrections, and at the
first steps of a reaction. What that costs in the picture while things move is for the user's eye (the
study below: 0.64% of pixel-frames visibly off while moving, 0.01% with corrections at once).

## What is in the tree

- **A play** (`lab/play.hpp`, `data/plays/cost.json`, "chem-play/1"): gestures as data, a frame of a
  120 Hz display at a time (`cg_ui_play`, `cg_ui_playing`; `ChemLab.play`, `playing`, `played`). The
  play sets the bench up, then its marks: `settled`, `carry` (the acid bottle carried to the flask),
  `pour` (held, turned and poured by hand), `stir` (the flask stirred, powder in it).
  `make pass-cost` (`engines/godot/tools/play.gd`) times each mark's frames on the GPU, then ten
  frames in which every history starts again. `native/tests/game/test_play.cpp` holds what the play
  is meant to do.
- **Counts where no GPU runs** (`lab::optics_work`, `OpticsFrames::rays`, `read`, `most_rays`;
  `native/tests/game/test_cost.cpp`, printed with `LAB_RAY_STATS=1`): the rays a frame of each mark
  follows, and what a ray tries.
- **The torus in closed form** (`quartic_roots`, `torus_meet`): Ferrari's roots of the quartic taken
  from where the ray is nearest the axis, each made exact by three of Newton's steps on the distance
  to the torus itself. No stepping, no halving.
- **Surfaces by height** (`trace_shape`; `optics_shapes` gives each patch the heights and rings it
  can be culled by, and each shape its reach): a ray tries only the surfaces its heights and its
  distance from the axis can meet.
- **The tracer in single precision on the CPU** (`native/game/src/optics_single.cpp`,
  `OPTICS_SHAPES_ALONE`; `lab::optics_passage`, `check_O15`; `test_single.cpp`): the same text the GPU
  runs, every sum a float's, held ray by ray to itself in double.
- **A correction is one ray** (`beam_correction`), read along the beam's own sweep carried to where
  the ray lands; and where a beam's rays all go one way, one band in four has one
  (`CORRECTION_STRIDE`), weighed as four.
- **What lies in a vessel is drawn to a sixty-fourth of a pixel** (`optics_compose`): a vortex nearing
  its size or a liquid losing vapour no longer restarts every pixel of its vessel at every frame.
- The beams' pass's groups are a setting (`LAB_BEAM_GROUP=x,y`), for measuring.

And from the second day (each measured below):

- **Eight meetings allowed** in a shape, in both tracers (`trace::MEETINGS_MOST`, `TRACE_MOST`; the user's
  call: "4 or 8"). Four cannot be: a ray through a liquid's surface needs five.
  **Replaced** (the user, having seen it: a vortex in a shallow liquid showed grey patches): a ray may be
  turned back four times in a shape (`TURNS_MOST`, `TRACE_TURNS`) and meet sixteen surfaces; after either
  it goes straight on from where it is, as if the rest of the body were not there, with what its surfaces
  have passed. A ray through a surface, a vortex and a floor is turned back at none, and is followed;
  nothing ends in darkness. Against 128 of each, 1.1 to 1.9% of the pixels read by rays differ visibly,
  the same with 32 meetings and 8 turns; under 0.5% of passages take more than eight meetings.
- **Corrections wait where rays have moved** (`beam_means`, `beam_deferred`; the user's call to try it): a
  pixel whose beam has just been traced, what it looks through having moved, shows its beam's reading
  alone that frame. A pixel whose colour alone changed has its beam kept (`beam_held`) and takes its
  points by need, as any pixel. `LAB_BEAM_AT_ONCE=1` (host and reference) is the old way, for the eye.
- **Beams kept on the GPU** (`OpticsCompute::kept_`, `CG_OPTICS_KEPT_VECS`): a record a pixel in a pool,
  a rectangle of `cg_optics.chain` after another; a rectangle that is where it was has its records.
- **A pixel is stepped once a frame** (the beams' pass stamps `aux.w` with the run): a pixel within two
  bodies' rectangles was stepped in each rectangle's pass, and the second step took the batch the first
  had deferred.
- **The between-node check's tolerance in single precision** (`BEAM_BETWEEN 0.5` in the host's head; 0.02
  in the reference): below it rounding alone halved stretches of the spectrum to 64, 1,300 rays a pixel.
- **A body's outline** (`beyond_outline`; `optics_shapes` writes 32 bands of each shape's reach by
  height): a pixel within a turned vessel's rectangle and beyond its profile is not its pixel.
- **The rays' own budget** (`optics_layout::RAY_POINTS`, `CG_OPTICS_RAY_POINTS`: 300,000 points a frame, less 8 for each beam traced the frame before; first 120,000
  over the screen; `LAB_RAY_POINTS` for measuring), their asks summed apart; and which pixels have a
  batch, where the budget gives fewer than one each, chosen by tile (`BATCH_TILE_X`, `_Y`).
- **The chain's pass is not run** where pixels trace their rays: it had no pixel.
- **The wall as one radiance** (`wall_one`, `beam_flat`): where every ray of a beam, or of a step of a
  band, lands in one cell of the wall's table on rows alike there, the footprint's integral is that.
- **The fixed estimate reads a footprint as its corners' quad** (`fan_quad`): one piece, not six. A pixel
  is its fixed estimate and the mean of what its batches found beyond it, so it comes to the batches'
  mean whatever the estimate is.
- **A change of what lies in a vessel passes most rays by** (the shapes' head, vec4s 7 to 9:
  `SHAPE_BLOCK` 10; `Path::content`, `zone`; `Traced::span` through to `Beam::span`; a fourth vec4 a
  corner): each body's content tag apart from its shape tag, now and at the scene before, with the rows
  of the screen between which they differ; each ray keeps the rows between which it met such a body; a
  record or a corner stands where the two do not meet. `test_kept.cpp` holds O12 frame after frame
  through the pour and the stirring.
- The play has a mark more, `rest` (the 90 frames after the pour); `LAB_PLAY_SERIES=1` prints every
  frame of every mark with what the CPU spent, `LAB_UPLOADS=1` what is sent to the GPU and the
  rectangles, `LAB_BEAM_DEFINE` adds a definition to the shader. A window with no optical pass no
  longer reports frames as if it had one.

## Measured

M3 Pro, 1920 x 1080, `make pass-cost` (the play; a frame's time from one `_process` to the next, the
display's rate at best: 8.3 ms).

| | Settled | Carry | Pour | Stir | Every history starting again |
|---|---|---|---|---|---|
| Stage 32 (the baseline) | 8.4 ms | 62.0 | 97.4 | 58.4 | 125.9 |
| The torus in closed form, surfaces by height | | | | | 74.1 |
| A correction one ray, one band in four everywhere (the CPU busy besides) | 31.2 | 42.7 | 56.9 | 41.5 | 50.4 |
| One band in four only where a beam's rays go one way; what lies in a vessel drawn to a 64th of a pixel | 8.3 | 30.8 | 46.3 | 13.3 | 56.5 |
| The corners of neighbouring pixels shared (a pass over the lattice first) | 8.4 | 24.6 | 37.3 | 11.3 | 40.4 |
| A batch counted as the corrections it took | 8.4 | 24.6 | 39.5 | 11.3 | 40.8 |

| **The second day** | | | | | |
| Eight meetings allowed | 8.4 | 19.8 | 34.5 | 10.0 | 30.3 |
| Corrections wait a frame (on any change) | 8.4 | 9.8 | 24.4 | 8.5 | 11.5 |
| Beams kept on the GPU; corrections wait only where rays moved; outlines | 8.4 | 9.2 | 24.3 | 8.5 | 22.4 |
| A pixel stepped once a frame | 8.4 | 8.4 | 12.2 | 8.6 | 21.8 |
| The between-node check above single precision's rounding | 8.4 | 8.3 | 10.7 | 8.4 | 21.8 |
| The chain's pass not run | 8.3 | 8.3 | 10.4 | 8.6 | 21.9 |
| The fixed estimate's footprint a quad | 8.3 | 8.4 | 8.4 | 8.3 | 16.1 |
| What lies in a vessel changed only on the rows it changed | 8.4 | 8.3 | 8.9 | 8.4 | 17.8 |
| The rays' budget 120,000 points, batches by tile | (settling: 10.8) | 8.5 | 8.4 | 8.4 | 16.0 |
| **The ideas tried at the user's word** | | | | | |
| A straight beam read as its corners' quad, in its batches too | (settling: 10.9) | 8.4 | 8.4 | 8.3 | 14.9 |
| Beams traced counted against the rays' budget; the budget 300,000 points | 8.3 | 8.4 | 8.4 | 8.3 | 16.3 |
| A plain pixel keeps its points through a change of colour; pixels whose rays part have the budget first | 8.4 | 8.4 | 8.4 | 8.4 | 16.8 |

(The worst single frames of the last row: carry 9 ms, rest 9, stir 14, pour 16. In the pour they are the
simulation's steps while the stream runs and the reaction begins: 13 to 15 ms, with frames of 4 to 9
between them.)

(The last row's `settled` mark is not settled: with the smaller budget the bench set up before it is
still taking its corrections; a frame that spends the whole budget is 10 to 12 ms. `rest`, the 90
frames after the pour, is 8.3 ms. The worst single frames of the last row: carry 11, pour 13, stir
15 ms. The maps' reading on the same play: carry 15 ms, pour 144.)

In the reference, the rays a frame follows over the whole screen (`test_cost.cpp`, a corner counted as
the quarter of a pixel's it is): carry 1,201,000 to 354,000; pour 2,350,000 to 634,000; stir 2,005,000
to 253,000. The GPU's time has fallen by less (2.5 times for the carry) than the rays (3.4 times): what
a ray costs there is not only its count.

The device's own timestamps (`capture_timestamp`) give nothing on Metal; the old slope over extra
passes fails with "timeout waiting for fence" on passes this heavy.

### Where a restarting pass's time went (the screenshot tool's bench, then the play's)

| What runs | A pass |
|---|---|
| Everything (TDD 29) | 97 ms |
| No correction rays | 46.4 ms |
| The beam's 21 rays alone | 39.8 ms |

So the cost is the count of rays: 21 for the beam and 32 for the corrections, and all the reading of
bands and the wall 6.6 ms.

### What a ray cost (the play's bench, every history starting again)

| Experiment (on the host's copy of the shader) | A frame |
|---|---|
| As it was | 126 ms |
| The torus's code compiled out | 21 |
| Present, never run | 26 |
| Roots by derivatives and Newton, in place of 48 steps and halving | 90 |
| And surfaces culled by height (a ray tries 8 quadrics and 1.7 tori, not 40 and 31) | 81 |
| Every span ruled out after its setup | 25 |
| No turning points looked for | 63 |
| A torus tried only at a shape's first search | 66 |
| **Closed form (Ferrari) and three steps on the distance** | 74 |
| The same, its roots computed and not used | 26 |
| At most 16 meetings in a shape (32 are allowed) | 53 |
| At most 8 | 40 |
| The pass's groups 2 x 32, 32 x 2, 64 x 1, 8 x 1, 4 x 4 | 73 to 78, all |

What these say:
- **A search that runs until it is done is what costs.** The torus's quartic was stepped 48 times a
  span and halved 20 a root, at every meeting: five sixths of the pass. Counting showed little
  arithmetic in it; ruling every span out before the search made it free. In closed form the solve
  itself costs nothing that can be measured (26 against 25).
- **What is left is rays that take many meetings.** Light that enters the glass at a fillet or a
  shoulder is turned back inside the wall again and again. In the reference 2% of passages through a
  shape take more than eight meetings and 0.5% all 32 allowed; the pass's time falls in a line with the
  meetings allowed, whatever the groups' shape (why, and what does not help: *The long rays*, below).
- **Single precision is not the cause**: the tracer compiled in single precision follows the same
  rays (0.06% go another way; the same share run long), `check_O15`.

Counts in the reference (`LAB_RAY_STATS=1`, `test_cost.cpp`; the screen sampled every eighth pixel):

| Mark | Pixels a frame reads | Rays a frame follows | A pixel read |
|---|---|---|---|
| settled | 9,500 | 597,000 | 62.8 |
| carry | 22,600 | 1,201,000 | 53.2 |
| pour | 44,100 | 2,350,000 | 53.3 |
| stir | 37,900 | 2,005,000 | 53.0 |

(Before the corrections were thinned: 21 rays for the beam and 32 for the corrections of sixteen bands,
for every pixel whose content changed; a pixel not yet settled, up to 149.) While stirring, every
pixel of the flask started again at every frame: its vortex's depth moved in its last digits.

### The picture in motion (`chem_tool study frames`, the bench, `LAB_BEAM=5`)

| | Visible pixel-frames | Persistent | Within 30 frames of a change | Longest run | The eye metric's mean |
|---|---|---|---|---|---|
| A correction for every band (one ray) | 0.07% | 0.06% | 0.54% | 74 | 0.22 |
| One band in four, everywhere | 0.75% | 0.23% | 4.68% | 126 | 0.37 |
| One band in four where a beam's rays go one way, every band where they part | 0.07% | 0.06% | 0.59% | 74 | 0.22 |

So fewer corrections where rays part is grain that takes longer to fade; it is thinned only where a
beam's rays all go one way (four pixels in five), and the picture in motion is as it was.

### Beams kept, in the reference (`native/tests/game/test_kept.cpp`)

- Each body has a **shape tag** (`optics_compose`; the shapes' head, its last vec4): its rectangle, form,
  glass and layer, how it lies and what lies in it, its indices, the vessel it is made as; and a signature
  of the tags of what stands behind it in its rows. A pixel's **shape key** (`Path::shape`, in `path_of`)
  sums the tags of the bodies over it and that signature; it is folded into the pixel's key, so a pixel
  whose rays may go another way is always read again.
- A pixel's beams are written to its **record** when its shape key moves (`keep_beam`, `kept_beam`,
  `beam_means`; `OPTICS_KEPT`, `kept_slot`) and read from there while it is the same: 22 vec4s a beam, what
  the middle ray met kept as the bodies and places, so a kept beam is read with what they hold now.
  `KEPT_MOST` beams a pixel (1: a vessel's; a pixel whose spectrum is read in more stretches, or through a
  grating, traces at every read as before).
- **O12** (`check_O12`, `OpticsFrames::kept_fresh`): through the whole play, every sixth frame, every
  eighth pixel: every kept beam is, value for value, the beam traced afresh, and the picture is, value for
  value, the picture with nothing kept. It fires where a liquid's level is moved under the same tags.
- A change of colour alone traces no beam: each pixel follows 21 rays fewer than with none kept.
- Through the play the rays followed fall only from 2.31 to 2.18 million: while things move, the pixels
  that read are those whose beams have just changed. What keeping saves is the pixels still settling, and
  changes of colour.

### Corners shared

A buffer of three vec4s a corner over the screen (1921 x 1081 corners, 100 MB; `corner_step`,
`OPTICS_LATTICE`, the host's fourth pass `OPTICS_CORNER_PASS` over each rectangle's corners before its
pixels). A corner is traced when the shape key at its place moves; a pixel's beam reads its four
corners there and traces its middle ray (five rays). The same beams: over a block of whole pixels the
picture is, value for value, the picture with each pixel's own corners, from 9.9 million rays in place
of 17.6 (`test_kept.cpp`).

### The long rays (33.6)

| Experiment (every history starting again) | A frame |
|---|---|
| With corners shared | 40.6 ms |
| The surfaces' loop bounded at 16 in place of 64 | 40.8 |
| Each ray first with eight meetings, those that ran out followed again whole after the others | 43.9 |
| At most 12 meetings in a shape (by the loop's bound, or by a variable under a bound of 32: the same) | 31.1, 30.4 |

- A picture from the GPU of each pixel's share of rays that take more than eight meetings shows what
  the reference counts: few, along fillets, shoulders and rims. The GPU's rays are sound.
- Following them after the others changes nothing: the pass does not wait on them ray by ray, as was
  supposed. Their own meetings are what cost, about four times a usual meeting each (they are the rays
  that try a torus at every meeting).
- **Allowing 12 meetings in place of 32 takes a quarter off a frame.** It changes what the truth is
  (light that leaves a wall after 13 to 32 turns inside it would count as lost): not done; a decision
  for the user, with pictures of the truth both ways to be made first.
- The same picture shows a waste: every pixel of a vessel's rectangle is read by rays, those outside
  its outline too (a third of a flask's or a bottle's rectangle), each a beam and its corrections of
  rays that meet nothing.

### What the second day found (each by a measurement on the GPU)

- **The meetings allowed** (`test_cost.cpp`, `LAB_RAY_STATS=most`): 2% of passages take more than eight
  meetings, most of them leaving with light. Against the truth of 128 meetings, of the pixels read by
  rays: 0.45% differ visibly with 32 allowed, 1.2 to 1.9% with eight, 1.2 to 2.2% with six, 13 to 16%
  with four. Letting a ray still inside go on as it is, instead of bringing nothing, is no nearer.
- **Corrections deferred** (`chem_tool study frames`, the bench): visibly off, of pixel-frames, 0.01%
  with corrections at once; 1.68% deferred on every change (pixels whose colour changes at every step
  never had them: runs of 300 frames); 0.64% deferred only where rays moved; 0.88% with that and the
  budget of 120,000 points (the longest run 76 frames); 0.64% again as the tree stands (the budget
  300,000 less the beams traced, the pixels whose rays part first; the longest run 61 frames). At once costs: carry 15 ms, pour 19, rest 17.
- **Two rectangles, two steps**: the carry's cost rose as the bottle's rectangle came over the flask's.
  A pixel in both was stepped twice, and the second step took the first's deferred batch.
- **Slow frames in runs of five** where a carried vessel came over another: with the between-node
  check off they were gone; with its landing half off, gone; with its tolerance 0.1, there; 0.5, nearly
  gone. In the reference (double) no pixel of those frames follows over 100 rays. So in single precision
  the middle ray between nodes lands 0.1 to 0.5 px off the cubic through them, for thousands of pixels,
  in a few frames of a few alignments. A passage's direction in single precision is within 1e-6 of
  double's for 96% of rays and within 1e-5 for 99.8% (`LAB_O9_COUNTS=1`), which does not explain half a
  pixel: see Open.
- **Nothing was sent to the GPU that cost a frame**: 340 kB a frame (the scene, the rows, the lights,
  the shapes), no stack and no map under this reading; the CPU's part of a frame is 1 to 3 ms.
- **With every ray traced and the beam's reading stubbed out, every mark ran at 8.3 ms**; with the
  footprint's integral alone stubbed out, the same. So the rays fitted a frame, and what did not was
  the wall under each footprint: six pieces, row by row, for each step of each of eight bands. In the
  reference 60% of beams are read over a wall that is one under them; a ternary did not spare the GPU
  the integral, a branch did; and the fixed estimate read as one quad brought the pour to the
  display's rate.
- **A level that rises** changes one row of the flask and one of the bottle a step
  (`test_kept.cpp`, `LAB_ZONES=1`); through the end of the pour and the stirring's start 288,000 rays
  are followed where 417,000 would be were each change everywhere.
- **At a reaction's start** a flask's colour moves so far each step that its pixels' histories start
  again, and all ask for points at every frame: what a frame then costs is the rays' budget. With
  nearly none (1,000 points) every frame of every mark is 8.3 ms.

### The ideas tried (2026-11-23, at the user's word: "try your ideas and see if they help")

- **A straight beam is its corners' quad** (`beam_fan`, `beam_at`; `Beam::straight` kept in the record):
  83% of the beams over a wall that is not one under them have their middle ray at the mean of their
  corners'; read as one quad, with the correction's own account of the beam (`beam_at`) the same quad, a
  pixel still comes to its rays' mean. With corrections at once: carry 15.1 to 12.3 ms, pour 19.0 to
  15.4, rest 16.6 to 13.8. With them deferred, little: by then the batches' readings were not what a
  frame waited for.
- **What a batch costs.** With a budget of 1,000 points every frame is 8.3 ms; with 30,000, 60,000 or
  120,000 a settling frame is 9.5 to 11 ms, and only lasts longer the smaller the budget. A batch (16
  bands read exactly, 4 to 16 rays) costs about three of a moving pixel's beam and estimate. So no budget
  both settles a vessel soon and keeps a settling frame at 8.3 ms.
- **Settling yields to motion** (`RAY_POINTS_A_BEAM`; the totals' third pair on the GPU,
  `OpticsFrames::beams_traced`): a beam traced in a frame counts as 8 points of the rays' budget, so
  while a vessel moves nothing else takes a batch, and at rest the budget is 300,000. Every mark's
  median is at the display's rate, and the carry's and the rest's worst frames are 9 ms.
- **A history carried through a change of colour** in proportion to the light, for every kept beam: no
  help by itself, and wrong for a rim (a rim shows more of the wall as its liquid darkens). Kept only
  for a plain pixel: its beam kept, its rays all one way, settled, and what its batches found less than
  an eye sees. Measured in the reference at the pour's end (`LAB_RAY_STATS=series`): 300,000 to 900,000
  points asked a frame, by the 6,000 pixels of the flask whose rays part, which start again at every
  step of the reaction, as they should.
- **Pixels whose rays part have the budget first** (a pixel whose rays go one way asks an eighth).
- **What is left in the pour** is the step itself: 29,000 pixels read their estimate again (the colour
  moved) and 11,000 beams are traced (the rows that changed, and the pixels whose rays part, whose
  records stand through no change of what lies in a vessel), in the one frame of two that the
  simulation steps in.
- **What is held settles as what stands** (the user's eye: thin bright lines through a held vessel took a
  while). A held vessel's pixels are a stack's (something stands behind it in its rows), and a stack's
  pixels began with a quarter of the points, a rule of the maps' reading, where a stack's points were
  dear; and a pixel whose rays go one way asked an eighth even where its batches had shown a need (a
  thin line its footprint misplaces). Neither now, where a pixel traces its rays. In the reference
  (`test_cost.cpp`, `LAB_RAY_STATS=held`: the bottle carried over the flask, then held still): visibly
  settled 6 frames after the hand stops, 27 before.
- **A ray that never reaches the wall shows the room in front** (the user's eye: grey sections, where a
  vortex or a surface turns rays back). Such a ray was put 100,000 pixels off to the side it went, and
  the pass read the wall's table there: the darkness beside the bench (0.02), beside reflections of a
  room of 0.3. The truth always had it as the room in front. Now the table's cells beyond 50,000 pixels
  are the room in front, and every such ray is put at one place (rays of one pixel turned back to the
  left and to the right made a footprint as wide as the table). O7's even room is even in front too.
- **A band's correction places cover the pixel** (`beam_read`): across by seven times the frame's point
  and down by nineteen times it. They lay on one line over 62% of the pixel's height, another for each
  band, and moved across in step with the wavelength within the band.
- **A beam whose middle ray's nodes part** (it reaches the wall at some wavelengths and not at others)
  is read at each wavelength's nearest node (`beam_weights`), in its bands and in its corrections alike:
  a cubic through a landing and the far place swept the whole wall between.
- **A pour by hand at 600 times the clock's speed ran at ten frames a second** (`test_cost.cpp`,
  `LAB_RAY_STATS=speed`: 100 ms of the CPU a frame). The lab asks how a held vessel's contents lie at
  every step, 300 a frame, and each answer is a search of sixty volumes (0.33 ms). Now nothing is asked
  while all that is in the vessel is less than it holds below its lip at its tilt
  (`Lab::Impl::may_run_over`; one volume, kept for the tilt), and `contents::lying` and `below` keep
  their last answer: 1.4 ms a frame held and turning. The same to the bit: both session records
  reproduce. While liquid runs each step is still the search (130 ms a frame at 600x, for the few
  frames a bottle takes to empty; 10 ms at 60x).
- **The wall beyond the screen and the room in front are endless white planes** (the user's word:
  "white planes with a grid texture"; `OpticsRoom::beyond`, `front`, set by the screen to the wall's
  radiance less what its ruling takes, 0.72). Beyond the screen's edges the wall was the darkness beside
  the bench or the edge row carried on, and the room in front a grey of 0.3. On the screen the wall is as
  it is drawn. The ruling itself is not carried on beyond the screen, nor seen in front: a ray that lands
  there is spread over many squares. Glass reflects a white room now: rims and reflections are lighter,
  and a dark liquid a little lighter (the demo's boiled-down flask is 8 darker than the dilute one in
  L*, where it was over 10).
- **The level under a volume from a table** (`contents::level_of`: the volume under 33 levels, kept
  for a vessel at a turn, then false position with Illinois's guard): the records reproduce. But a pour
  by hand does not ask it: `lying` has a halving of its own for the liquid (the inside less the powder
  and the vortex under it). The same done there takes a frame of running liquid at 600x from 132 ms to
  52, and a frame held at the lip from 17 ms to 1; and the demo's record no longer reproduces (the level
  differs in its last digits, and the stirring's work follows it). **Kept, with the user's approval
  (2026-11-23), the demo's record made again**: every one of its 69 measured lines is the same, every
  command ends on the same step, and the digests of 7 of its 26 commands differ (12 to 18, while the
  flask is stirred); the pressure session's record is untouched.
- Not tried: the wall as a texture (a batch's reading must be the integral its corrections are of; the
  estimate's is already one quad); rays on a sparse lattice and a ray's derivatives (the rays fit a
  frame; they would pay for corrections at once); an outline pixel as its covered share.

### The GPU's settled picture against the truth (O4, `LAB_BEAM=5 ... tools/screenshot.gd`)

**Now** (`make screenshots`, the second day's tree): 2 pixels visibly off in each picture that shows the
flask on the bench (the worst 16, both at (401, 684), four rows over the flask's foot, at its heel's
fillet); none in the others (the largest difference 3). What follows is the first day's run.


Run for the first time under this reading: of 41,000 to 80,000 pixels in bodies, 4 to 32 visibly off
(the eye metric above 8; the worst 16), the same ones after 40 settling frames and after 400, and with a
correction for every band. So it is not grain still fading, and not the thinning (which, counted by
bands, had added six pixels: a batch is now counted as the corrections it took). Not yet found: the
single precision's closed form at a few pixels, or the truth this check holds the pass to under this
reading (TDD 29: its exact reading loses parts of chaotic pixels). Whether it was so at Stage 32 is not
known: that tree was never run through this check.

## Open

- **O4 under this reading**: 2 pixels at a flask's heel, (401, 684) of the demo's bench, 16 on the eye
  metric. It is the single precision's: the reference, in double, settles there to within 1 of the
  truth (`test_cost.cpp`, `LAB_RAY_STATS=heel`; 3 to 4 on the row above). The heel is a fillet: the
  quartic's roots in single precision are the place to look.
- **O4 on the GPU with the planes white**: 2 to 10 pixels visibly off in a picture (2 before), the
  worst 14 to 17, at three places: the demo flask's heel (401, 684), and now (541, 530) and (135, 711),
  where part of a pixel's rays land beyond the screen's edge, which is white where it was dark. The same
  kind of pixel as the heel's; not looked into.
- **At a vortex's rim the pass settles on more blue and green than its rays bring**: two pixels of
  `test_lying.cpp`'s row, where a pixel's rays go three ways (to the wall near by, back to the room, far
  off the screen): 9 on the eye metric against the truth, 5 before a ray turned back showed the room in
  front. The pass's rays there are the truth's, ray for ray, and the pixel is settled, not grain. Its
  cause is not found; the test lets those two by, to 10, and counts them.
- **Half a pixel of spread in single precision** between a ray and its neighbours in wavelength, through
  two vessels, in a few alignments (above): the cause is not found. The pass is spared its cost by a
  tolerance (`BEAM_BETWEEN 0.5`), which also means a prism's stretches are halved on the GPU only where
  the sweep's own term asks (0.002 of the sweep).
- **For the user's eye**: the picture while things move with corrections deferred (rims and surfaces are
  their middle rays' until a vessel rests; `make run` against `make run LAB_BEAM_AT_ONCE=1`), and how
  long a vessel takes to sharpen once it rests under the rays' budget; and the truth with eight
  meetings allowed (rims and fillets a little darker in 1 to 2% of a vessel's pixels).
- **A frame that spends the whole budget is 10 to 16 ms**: after a vessel comes to rest (nothing then
  moves on the screen), and at the simulation's steps through a reaction. The simulation steps in one
  display frame of two, so a pour's cost comes in every other frame: spreading a step's pixels over
  both frames would halve the worst frame and leave half of them a frame late, which O12 as it is
  worded does not allow; the user's call.
- **Ideas not yet tried** (the user asked for them, 2026-11-23): the wall as a texture read through the
  hardware's anisotropic filter with the footprint as its gradients; rays on a sparse lattice,
  interpolated where a body's rays all go one way and refined where they part; a ray's derivatives
  carried through each refraction in place of corner rays and inner nodes; an outline pixel as its
  covered share of the body and the rest of the wall, from the outline in closed form; a carried
  vessel's beams moved with it where nothing stands behind it.
- **The reference's measuring hooks** added on the second day (`optics_ended`, `optics_even`,
  `optics_meetings_most`, the histogram in `check_O15`) are to go with the other measuring switches in
  Stage 34.
- `path_of` runs in each of the three passes a pixel is in; the scene, the rows and the lights are
  sent whole at every frame they change in any part (340 kB).
