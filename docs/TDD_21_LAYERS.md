# TDD 21: Layers of refraction (Stage 21)

## The request

> I want to support at least two layers, one for placed equipment and one for equipment held by the
> player, which should be displayed in front of the placed equipment layer. There could be more
> layers potentially for things like containers for containers (e.g baths), even up to 6.

> … a future stage for the player held equipment layer, which should be visualized in front of the
> grid placed equipment, including refraction, spectral effects and everything.

And, asked whether it was specified as a full spectral layer that does both refraction and every
other spectral effect: "that's my expectation". The plan was approved on 2026-11-20.

GAME_SPEC §12.3, §18 (O4 to O8), §20.

| Phase | Topic | Status |
|---|---|---|
| 21.1 | The spikes: a stack traced into one map; the chain of single maps against both bodies traced together | Done: see below |
| 21.2 | The spec: §12.3 (one world in pixels, the chain, glow as a light), §20 | Done |
| 21.3 | The scene: a body's layer, the wall and the gap in pixels, a map traced for its layer's distance | Done |
| 21.4 | The truth: its own tracer, a ray from anywhere through every body of the one world | Done |
| 21.5 | The chain in the reference; every pixel against the truth; O5 to O8 through two layers and six | Done |
| 21.6 | The GPU: two passes; the chain moved to the CPU (the stacks); the cost | Done: see below |
| 21.7 | A hot solid's glow as a light in the pass | Done |
| 21.8 | The screen: a carried item, and a carried selection, are the hand's layer; a person-paced test | Done |
| 21.9 | O4 on every picture, one with a vessel held over another; `make gates` | Done |
| 21.10 | No outline drawn; a vessel's floor as glass (asked for by the user on seeing the pictures) | Done |
| 21.11 | The user judges the pictures by eye and signs off: "Looks good! Approved, proceed" (2026-11-20) | Done |

## The spikes (2026-11-20)

**A stack traced into one map is too slow.** The spec of Stage 20 foresaw a map for each stack of
bodies, traced again when the stack moves. Measured on the CPU, a held 50 mL vessel 120 px in
front of a standing 250 mL one, both cores:

| The held vessel's axis from the standing one's | A map | Rays | Pieces in wavelength |
|---|---|---|---|
| 0 px | 7.6 ms | 19,361 | 8 |
| 20 px | 18.7 ms | 50,827 | 16 |
| 37 px | 12.4 ms | 35,937 | 16 |
| 60 px | 13.3 ms | 44,298 | 16 |
| 200 px (past it) | 1.2 ms | 5,720 | 2 |
| one vessel alone | 0.5 ms | 2,650 | 1 |

The pieces are the cause: the image of the standing vessel's rims moves with the wavelength
through the held one, and a map that holds both must follow that edge piece by piece. The map
would be traced every frame the hand moves.

**The chain is exact, and traces nothing.** A round vessel is the same from every side: a level
ray leaves it as it came, turned about the axis by an angle that depends only on its distance
from the axis. So one vessel's map answers a ray from anywhere. 3,779 rays through the held vessel
and then the standing one, at 20 offsets: the chain of the two answers against both traced
together lands within 2.1e-9 px, and passes the same light within 1.7e-13.

## Decisions

- **The chain, not a traced map per stack**: approved by the user. Maps stay as they are, traced
  once. The plan put the chain in the pixel pass; it is followed on the CPU instead, into a table
  the pass reads, because of what the GPU measured (below). The method and its exactness are the
  plan's; where it runs is not.
- **One world in pixels.** The wall 284 px behind the bench's plane for every body (it was 20 cm
  at each body's own scale: 375 px for a 50 mL vessel, 259 px for the 500 mL one); the hand's
  plane 120 px in front. Both calibrated, for the user's eye.
- **Glow as a light in the pass**, in this stage: approved by the user.
- **Declared, not built:** a neighbour on a ray's own layer; a body that is not round in a chain
  (read as seen straight on); a vessel inside another.

## What was built (2026-11-20)

- **One world in pixels** (`OpticsRoom::wall_px`, `gap_px`; `OpticsBody::layer`): bodies are
  painted by layer; a map is traced for its layer's distance to the wall.
- **The chain** (`chain_at`, `native/game/shader/optics_pixel.glsl`, `OPTICS_FOLLOW`): a ray is
  followed layer by layer; in a layer, every body is read where the ray's line crosses the
  layer's plane as it arrives, and the body that bends, the first the line meets, turns it by its
  map. A map is read between its samples by the curve through four of them.
- **The truth** (`exact_ray`, `native/game/src/optics_reference.cpp`): its own tracer (which medium
  a point is in), a ray from anywhere, through every body of the one world.
- **The stacks** (`optics_stacks`): for each vessel that is held (or stands across another on its
  layer), its rays followed through the bodies behind it, at every quarter pixel across it, in
  each band of rows, at the spectrum's four nodes; cached by where its bodies stand and what they
  are.
- **The pass** reads a stack as it reads a map (`stack_band`): four parts a pixel, the wall
  integrated over each part's footprint and a band's sweep, the bodies read where the rays cross
  them, once a band. The general reading for one vessel (`general_band`) is Stage 20's, for the
  prism.
- **Two passes on the GPU**: the screen, with one vessel's reading alone; then the rectangles of
  what is held or is a prism (`cg_optics.chain`), with the rest.
- **Glow** as a sixth spectrum of each body, read in the pass.
- **The screen**: an item being carried, and a selection carried with it, are layer 1; pressed
  and not yet moved, an item still stands.

## What was measured

| | |
|---|---|
| The chain against both bodies traced together (the spike) | 2.1e-9 px |
| The chain through the maps against the truth's tracer, 1,934 rays | within 0.052 px of the screen |
| Every pixel through a held vessel and a standing one, against 64 rays (432 pixels) | worst 5.5 on the eye metric (visible above 8), mean 1.3 |
| A balloon behind a held vessel of water (69 pixels) | all but two within 2.1; on its outline 47 (as a balloon alone is on its outline) |
| A glowing heap through a held vessel | within 1.5% of the truth's light |
| O5, O6, O7 through two layers; O7 and the chain through six | hold |
| O8 through two layers | 1% for lines seen through the bodies; 3% for a line seen only in rims |
| O4, 14 pictures, one with a beaker held over the flask | largest difference 2, none visible |
| A restarting pass, nothing held | 1.54 ms (1.77 before the stage) |
| A restarting pass, a 250 mL vessel held over another | 7.2 ms |
| A settled pass | nothing a frame could show |
| Following a carried vessel's rays, on the CPU | 1.4 ms a frame (worst 2.3), about 11,000 rays |
| `make screenshots` | 29 s |

## The GPU, and what it taught

The plan put the chain in the pixel pass. It is exact there, and the picture was right the first
time it ran (O4, none visible). Its cost was not:

| What the pass did for a held vessel's pixels | A restarting pass |
|---|---|
| Followed each ray at every wavelength a band asked for | the GPU's fence timed out |
| Followed each ray once a pixel, at four wavelengths; bands by the cubic between | 344 ms |
| The same, with nothing in a band's loop but arithmetic on those rays | too slow to time |
| Its prologue alone (no band read) | 48 ms |
| Read a table of rays followed on the CPU; four parts; 72 bands | 14.2 ms |
| A level for each pixel (1, 2 or 4 parts) | 13.1 ms: dropped |
| A stack's pixel starts with 16 points | 9.9 ms |
| The bodies read once a band; the wall alone swept in steps | 6.9 ms |

- **The chain's code in the shader cost every pixel**, whether it ran it or not: a settled pass
  went from nothing to 0.58 ms, a restarting one from 1.86 to 2.15. Without the general reading in
  it the pass over the screen is faster than before the stage (1.54). Hence two passes.
- **Per-ray arrays are slow on this GPU**: a ray kept as arrays over the bodies it meets ran about
  a hundred times below its arithmetic. On the CPU the same ray costs 0.4 µs. Hence the stacks: the
  chain is followed where it is cheap, as a map is traced there, and the pass reads a table.
- **A stack's rays spread the wavelengths wide**, so a band is swept in many steps: the fixed
  estimate's eight wide bands cost as much as sixty-four narrow ones. Only the wall needs the steps.
- **The O8 check's own truth missed an image**: through two vessels a landing can cross the whole
  wall within a hundredth of a pixel, thinner than the check's scan. It now halves the screen
  wherever two of its rays land more than a twentieth of a pixel apart.

## Open

- **7.2 ms a frame while a vessel is carried over others** (and 1.4 ms on the CPU). Held still,
  nothing. What might bring it down: reading a stack's bodies once a pixel where its rays go one
  way; a pass over the stack's pixels alone, not its rectangle.
- **No outline is drawn** (the user, 2026-11-20: "all lab placed elements must have spectral
  colors and shapes and they should be propagated through the spectral pipeline"). A vessel's
  drawn outline and its liquid's drawn surface line are gone: a line the screen draws over the
  pass is not bent by what is held before it. A vessel is now its glass and its liquid alone: two
  rims, no floor, no lip.
- **A vessel's floor** (the user, 2026-11-20: "a thick base is important"): glass all through,
  a third core of the vessel's map; what it holds rests on it. 2.6 mm at 250 mL, calibrated from
  one supplier's table (`docs/DATA_SOURCES.md`: no standard read gives it). The wall behind is seen
  through it at -3.0 times (the paraxial figure for a rod); 368 pixels of a vessel's foot against
  64 rays each: worst 5.6 on the eye metric. With floors, a restarting pass costs 1.64 ms with
  nothing held and 9.6 ms with a vessel held (more bands of rows in a stack).
- **Its lip is not built.** ISO 3819 gives the brim's flare (5 to 15% wider than the body), not
  its bead; and a flared brim is wider than the body's rectangle and bends rays up and down.
  Planned with the rest of a vessel's shape (GAME_SPEC §12.3).
- **Still drawn over the pass, not in it**: the burner and its flame, a condenser's outlet, signal
  parts and wires, labels. A vessel carried across one of them is drawn under it. Planned as
  Stage 22: everything that takes space is physical, and an item's controls leave the grid for a
  panel of actions (GAME_SPEC §12.3, §13.1).
- **A liquid's surface** stays flat and level; its shape (meniscus, a stirred vortex, sloshing)
  is specified as planned (GAME_SPEC §11).
- **A ball's outline** blends by degrees (pre-existing): seen through a held vessel as seen
  straight, a pixel the outline cuts aslant can differ from the truth's sharp outline.

