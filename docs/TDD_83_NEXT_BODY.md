# TDD 83: A ray's next body (Stage 83)

## The request

The user, at Stage 71's sign-off (2026-11-27), kept as Stage 77's second item and moved to a stage of its own: "when a
vessel is inside a vessel - rays should be rendered as entering the outer vessel, and then proceeding to hit any
surface along the way and reacting accordingly, including the inner vessel. For example, a ray may enter the outer
vessel, then enter the inner vessel, then exit the inner vessel, and then exit the outer vessel, and only then reach
a wall. It should be correctly simulated." Asked again on 2026-11-28 ("it doesn't look like the inner vessel is inside
the outer one ... Is that simulated?").

Stage 83 was chem-game-v2-8d's in the tubing plan approved on 2026-11-28 (94 -> 95 -> 83 -> 96). On 2026-11-28 the
user approved its handover to chem-game-v2-a7, by 8d's counter-proposal: 83 now, while 8d builds 95; clear-surface
reflection (branch `reflection-branches-wip`) after it as Stage 109, by the same hands, so the kept beam's record is
designed once. What 8d's Stage 96 (bent cylinders) needs of it:

1. a ray leaving a body meets the nearest body in 3D wherever it stands: beside it on its own layer, behind it, or
   inside its liquid;
2. a body has a depth of its own, a Z offset within its layer, in both tracers;
3. held on new scenes by O9, O15 and O16: a test tube in a beaker of water seen through the beaker, and two cylinders
   crossing at different depths;
4. nothing in the next-body search assumes a body's axis is upright (96 gives each patch a frame of its own; a tube
   is a chain of patches split into bodies of at most 64 patches, seen through each other);
5. the pass's cost reported (`make pass-cost`): a bench will hold ten tubes.

## What was there (a survey, 2026-11-28)

- Both tracers have two levels: a body's own tracer over its own patches in its own frame (the pass's `trace_shape`
  and `trace_body`, `native/game/shader/optics_pixel.glsl`; the truth's `trace::trace` and `exact_body`,
  `native/game/src/optics_trace.cpp`, `optics_reference.cpp`), and a world's loop layer by layer (`ray_met`,
  `exact_ray`): in each layer the bending body whose rows hold where the arriving line crosses the layer's plane and
  which the line meets first is traced, then the layer's flat things at that crossing, then the layer behind.
- A ray leaving a body heading away from the eye never met another body of its own layer. A ray turned back toward
  the eye or sideways met the nearest of every body in 3D (Stage 78.5: `box_meet`).
- A body's trace knows only its own surfaces and air around them (medium 0 has index 1).
- A vessel standing in another (`ItemView::stands_in`, Stage 71) was drawn on layer 1 (`ui.cpp`: "standing in a
  vessel, drawn before it: Open"): in front of the bath's glass and liquid, the bath's liquid never seen through it.
- The shapes' head (SHAPE_BLOCK 10 vec4s a body) has two numbers free: lie[38] and lie[39].
- The kept beam (KEPT_BEAM 25 vec4s) records the first two bodies that bent a ray.

## The design (GAME_SPEC §12.3, "A ray's next body")

- **Depth**: `OpticsBody::depth` (pixels), the head's lie[38]; a body's axis at `-layer * gap + depth`, a flat thing's
  plane there.
- **Within**: `OpticsBody::within` (a body's index, -1 none), the head's lie[39] (its index + 1). A group: a body
  that stands in none, and those standing in it and in them, at most four, ordered by how deep they stand.
- **A group traced at once** (both tracers, written apart): the ray's place and way in the outermost one's frame; each
  member's patches met in its own (its offset, turn and scale from the outermost's); a member's medium state kept
  for each; the medium where the ray is, the innermost member's whose state is not outside. A member's surface met
  inside a member that stands deeper is no meeting (its state alone changes). Leaving a member, the ray is in what of
  the next one out is there, asked at the place (`inside_medium`: its liquid, the air over it, its sediment).
  Fresnel and Snell between the two media's indices; absorption by the medium the ray is in; each member's own
  passing, turned share, absorption, chord and route; the first meeting's distance, the order of the slots.
- **The next body**: in a layer, after the group the arriving ray meets first, a ray heading away goes on to the
  nearest group of the same layer whose box it reaches with its own surfaces ahead (not the one it left), within the
  four bodies; flat things are read where the arriving line crosses their plane, as before. Candidates are groups'
  outermost bodies.
- **Signatures**: a body's signature of what its rays can reach takes in the bodies of its own layer that share its
  rows (a group's members among them).
- **The lab**: a vessel standing in another is on that one's layer, at its depth, within it.

## Phases

| Phase | Topic | Status |
|---|---|---|
| 83.1 | Spec (§12.3 "A ray's next body", §18 O29, §20), this TDD | done |
| 83.2 | The scene: `depth`, `within`, the head's lie[38], lie[39]; signatures of a layer's own bodies | |
| 83.3 | The truth: a group traced at once (`trace::trace_group`), `exact_ray`'s next body; O29's closed form | |
| 83.4 | The pass: `trace_group`, `ray_met`'s next body; O9, O29, O15 and O16 on the new scenes; every optics test as it was | |
| 83.5 | The lab: a vessel standing in another within it; the freezing point's bath on the CPU against the truth | |
| 83.6 | The GPU: `make pass-cost` (a bath with a tube, ten tubes' worth of bodies), `make screenshots`; the user's eye | |
| 83.7 | CHANGELOG, `make gates`, the user's sign-off | |

## Open
