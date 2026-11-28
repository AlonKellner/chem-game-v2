# TDD 95: A tube is a vessel (Stage 95)

## The request

The user (2026-11-28): "Tubing should be a vessel, with layers and all the related simulated expectations from any
other vessel." Not vertical; "layers split along its length"; "movement along the tubing is by the pressure
difference at the ends"; all layers move, "the liquid front moves visually"; "The weight of the tube contents and
their orientation should affect the pressure at the ends, so situations like siphoning will work as expected."
Rigid tubing (glass) first. This stage is the core alone: no picture (96), no bench gesture (97).

Worked on branch `stage-95`, started from `stage-94` (awaiting its sign-off), in the worktree directory still
named `.claude/worktrees/stage-89`.

## The design

### A path (`chem/path.hpp`, pure)

Vertices in 3D, metres (z up, over the bench's datum), a fillet's radius at each inner vertex (0: a sharp corner,
taken as a quarter turn of the least radius), one bore and one wall. Straight runs and arcs. Gives: its length;
at an arc length s the point, the tangent and the height z(s); the volume to s (bore area times s); the number of
turns passed between two arc lengths (for a turn's loss). Presets: a straight run, an L, a U, an inverted U (a
siphon). Any polyline with fillets is a path; a helix and a hanging chain are Stage 98's.

### Slugs

- A tube's contents are **slugs**: cells in order along its path from its end a to its end b, each a sealed rigid
  cell (`CellMeta::slug`, its tube and its place kept by the world) whose volume is the span of the bore it fills.
  The slugs fill the tube exactly: the sum of their volumes is the tube's (new check, V4).
- Neighbours are joined by **run edges** (`Edge::run`): no gas path, nothing crosses but work as the interface
  between them moves, heat by conduction and what is dissolved by diffusion (the column's rules, over the bore's
  area and the two half lengths).
- Lagrangian: a slug keeps its matter; where it lies is the volumes before it. So a front moves as the matter does
  and nothing is smeared along the tube by the step.
- From the column (`strata.cpp`), reused: alike neighbours merge (same phase, temperatures and compositions within
  the column's contrast); conduction and Fick's diffusion between neighbours. Not reused: gravity's sorting between
  cells (a slug fills the bore: plug flow).
- **A slug that holds a gas and a liquid** after its fast equilibria (it boiled, or its vapour condensed) is sorted:
  its gas becomes a slug of its own at its higher end (by the path's heights at its two ends), its liquid the
  other. Solids ride in their slug.
- At most 16 slugs a tube: past that the two most alike neighbours merge.

### Each slug has its own pressure

In the closure (`closure.cpp`) every slug is a node: its pressure (at the middle of its span) an unknown, its
residual the room its span gives less the room its contents need there, as any sealed vessel's.

- **An interface** between slugs k and k+1 moves a volume `v` in the step (toward b positive: k grows, k+1
  shrinks), the root of

      P_k - P_k+1 = D_k(v) + D_k+1(v) + rho_k g (z_i - z_k) + rho_k+1 g (z_k+1 - z_i) + give v

  `D`: each slug's friction over its half next to the interface, at its own density and viscosity; `z_i`, `z_k`:
  the heights of the interface and of the slug's middle (the weight of each half over its rise; a gas's weight
  left out, as Stage 94); `give`: what the move itself takes off the drive (the stabilising part of d(weights)/dv
  from the path's slope at the interface and at the two middles), so a U-tube settles without passing level at any
  step's length.
- **A slug's friction is its share of the law Stage 89 gives the whole tube.** With F(s) = 64 x(s) + N12(a(x(s)),
  c + t(s)) the loss in velocity heads from the tube's inlet to a distance s along it (Stage 89's piece, without
  its expansion: x = s / (D Re), a Shah's developing excess, c the contraction at the inlet, t Churchill's friction
  over Poiseuille's), a stretch from s1 to s2 takes F(s2) - F(s1) at its own fluid's Reynolds number; each quarter
  turn in it a loss of its own (a measured coefficient: sourced and gated). The inlet is the end the stretch's own
  flow comes from. A tube holding one fluid end to end is Stage 89's conduit to the bit of its sum; `conduit.hpp`
  gets the stretch's law (`stretch_drive`).
- **At an end** the tube meets a vessel's port (Stage 94: a height, a head) or the room, or is closed. Given the
  pressures:
  - outward (the end slug's pressure and weight over the port's): the end slug's matter crosses to the vessel, by
    its half's friction and the expansion into the vessel; if all of it goes, the rest of the step's flow is the
    next step's (the next slug is then the end slug);
  - inward, what stands at the port alike the end slug: it joins the end slug;
  - inward, unlike (air into water, hot into cold): it begins **a new slug** at the end. Within the step it has no
    length and so no pressure of its own: it stands at the interface's pressure (the end slug's and its half's
    friction and weight), takes the room its contents need there, and the old end slug's span gives that room up.
    It is a cell from the step's end on.
- A slug emptied within a step is removed (its cell retired from the tube, kept for reuse).
- **Where a liquid slug's own weight takes its highest point under the pressure it can stand** (its saturation
  pressure where its liquid has a vapour; nought where it has none), its highest part is split off as a slug of
  its own, whose pressure is then that point's: it boils by its own fast equilibrium, or stands at nought with a
  void (Stage 88's third state). So a column closed at its top stands as high as the room's pressure holds it:
  10.33 m of water less its vapour, 760 mm of mercury (Torricelli's void), with no rule for barometers.
- **Energy.** An interface's move is work between its two slugs along the run edge (`World::work`, W2), each slug
  brought to its end pressure against that pressure as any node; what friction takes stays in the slug it was
  lost in (the heat (P_k - P*_k) v, never negative); what the weight's part takes or gives crosses from gravity's
  reservoir (Stage 94) to that slug: its mass times g times its centre's fall (Z1's row, for a slug).
- **F1 for an interface**: its row in the trace (the two pressures, the two halves' fluids, the heights, the give,
  the volume moved): the move is down its drive and within its law.

### In the step

- Before the closure: nothing. In the closure: the tube's nodes with the vessels' (a tube joins its two vessels'
  groups). After: every slug to its fast equilibria (as any cell), then the tube's own pass (`src/tube.cpp`): sort
  a slug that holds two phases, split where the weight asks, merge alike neighbours, remove the empty.
- With the step's processes: conduction and diffusion along the run edges.
- Walls (rings along the path) and the lab's contacts are Stage 97's; in the core a slug takes heat along whatever
  edges its world gives it.

### Stated limits (in the spec, and Open)

No inertia (a U-tube settles without swinging). Plug flow: right for water and air in bores under about 5 mm (a
bubble does not rise past water where the Eötvös number is under 3.37); in a wider bore the lighter passes the
heavier, Stage 98. No dispersion along a slug beyond diffusion. A slug's state at its middle's pressure (a
liquid's volume differs by its compressibility times half its head: 2e-6 a metre of water). One bore a tube.

## Verify (closed forms, core alone; `chem_tool study tubes`)

- One fluid end to end: Stage 89's rate to 1e-9 (the same sum).
- A siphon between two open vessels: its rate the law's for rho g (h1 - h2) with the tube's whole friction; stops
  level; breaks when the giver's surface passes its port (air comes in, the two legs run back); started empty it
  does not run.
- A U-tube of water on a syringe: the levels' difference rho g dh of the syringe's pressure over the room's.
- A column closed at its top over a dish: 10.33 m of water less its vapour's pressure; 760 mm of
  mercury-as-data (a liquid with no vapour: a void).
- A bubble between two slugs, pushed: Boyle.
- A front crosses 30 cm of tube in the time its law gives.
- Every invariant at every step under hands at random with tubes; V4, F1's and Z1's self-tests.

## Phases

| Phase | Topic | Status |
|---|---|---|
| 95.1 | Spec (§6 Tubes, §18), this document | Done |
| 95.2 | `path.hpp` and the core's trig (done); the stretch's law in `conduit.hpp` (one fluid = Stage 89's) | In progress |
| 95.3 | Tubes and slugs in the world (cells, run edges, V4; save, load, digest in the lab's world code) | |
| 95.4 | The closure: slug nodes, interfaces, ends (outward, inward alike) | |
| 95.5 | New slugs at an end; removal; the tube's pass (sort, merge); the weight's split | |
| 95.6 | Energy: work along runs, friction's heat, gravity's work; F1 and Z1 for an interface | |
| 95.7 | Conduction and diffusion along a tube | |
| 95.8 | The study, the hands at random; docs; `make gates`; sign-off | |

## Implementation notes (decided while drafting, 2026-11-28)

- **The core's own sin, cos, atan, atan2** (`math.hpp`: fdlibm's kernels; within 1 ulp of the library's over
  8 million points and by every multiple of pi/2 to 2000): a path's arcs need them and the core may not call the
  library's. Pinned bit for bit (`test_math.cpp`).
- **`path.hpp`, `path.cpp`**: pieces (runs, arcs, sharp corners), `point`, `tangent`, `height`, `mean_height`,
  `highest`, `lowest`, `turned`; refusals (`test_path.cpp`: an inverted U's length, continuity at every joint, the
  mean height against a walk, an arc's top inside it, out of the plane, a sharp corner).
- **A slug is a sealed rigid cell** (the closure's node as it is: `rigid && sealed && !layer`), its volume its
  span; an interface's move resizes the two (as `move_piston` does one: a Resize entry each).
- **The topology is fixed within a step** (the world's rule): a tube is a row of cells made ahead (16; grown at a
  step's boundary as a column's layers are), joined in order by run edges, its two end edges on its first and last
  cells. Slugs are the cells that hold something, in order; an empty cell has no volume. What crosses at an end
  to or from the nearest slug hops through the empty cells between (as a crossing is lifted up its giver's
  layers). The tube's pass, at the step's boundary: an end cell emptied by moving the row along (so unlike matter
  coming in finds an empty cell: the new slug), alike neighbours merged, a two-phase slug sorted into two cells,
  the weight's split.
- **Run and end edges are no gas path** (`Edge::tube`): a slug is sealed by the world's own rule.
- **The stretch's law**: F(s) = 64 x + N12(a(x), c + t) from the inlet (F at the inlet itself: the contraction c);
  a stretch (s1, s2) takes F(s2) - F(s1), from 0 the whole F(s2); the expansion at the outlet's end link. One
  fluid end to end: Stage 89's sum.
- **Bends**: research running (Ito, White, Mishra and Gupta, Hooper, Kittredge and Rowley); the data sourced and
  gated before it is used.
