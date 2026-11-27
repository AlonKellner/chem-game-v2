# TDD 78: Reflection traced, and each material's own index (Stage 78)

## The request

The user (2026-11-27), after seeing liquid mercury drawn opaque (branch `mercury`, the experiment "Liquid mercury,
beside water"): "I want reflective properties to be simulated by the renderer ... Just like we simulate refraction, we
also need to simulate reflection. Perhaps this means we need to randomly reflect/refract sampled rays depending on the
angle of attack of a ray, and the averaging will make it look as it should." Then: "Please do plan proper reflection,
one of the things I'll test is the visual realism of liquid mercury, but I will also look at general other tests and
materials." The plan approved: "I approve of your reflection plan" (2026-11-27).

## What was there (a survey, 2026-11-27)

- Both tracers (the pass's `trace_shape` in `native/game/shader/optics_pixel.glsl`, the truth's `trace()` in
  `native/game/src/optics_trace.cpp`) refract by Snell at every surface and follow total internal reflection; the share
  Fresnel turns away was summed as `turned` and shown as one radiance, the room in front. No reflected ray was followed.
- Every index was real; in the visible every liquid was water's. n + ik entered only the long-wave range, as an
  effective real index (`effective_index`). Datagen refused an index outside [1, 3).
- A ray that left a body toward the eye or sideways already went on to the room's cube (`room_exit`): the faces'
  white, or the black cells along its edges.

## The design (GAME_SPEC §12.3, "Reflection traced; each material's own index")

- **An index is n + ik** at each nanometre. A metal's is a measured table (`nk_table`: its source's own rows, read
  between them linearly in the dielectric function against the photon's energy). Liquid mercury's is Inagaki, Arakawa
  and Williams 1981's (ellipsometry), checked against Smith and Stromberg 1966 at 546.1 nm.
- **A vessel's liquid takes its liquids' index by volume**, n and k; a new row per set, `LIQUID_K_ROW`
  (`ROWS_PER_SET` 9 → 10, the temperatures still last).
- **A metal turns back by its complex Fresnel reflectance** (Born and Wolf §13.2, the mean of the s and p waves'),
  and the rest is absorbed in its skin: the ray never goes in. Deterministic: no branch is drawn, so a beam that meets
  mercury is its mirror image, kept and shared as any beam (O12, O13 untouched). The core's
  `chem::optics::fresnel_reflectance` (the truth's) and the shader's `metal_reflectance` (the pass's), written apart.
- **A branch at a clear surface** (both shares carry light): a ray's branch seed and Russian roulette by R in the
  correction rays; the beam stays the transmitted path and its turned share is read in the mirror direction.
- **The truth follows both branches** at a clear surface to eight events; the pass's roulette is held to it (O27).
- **Flat things with a gloss**: a surface may name an index (a burner's metal).

## Phases

| Phase | Topic | Status |
|---|---|---|
| 78.1 | Spec (§12.3, §18, §20); liquid mercury's n and k (datagen's `nk_table`, `extinction_index`); its gate | done |
| 78.2 | Complex Fresnel in the core; a metal liquid in both tracers (`LIQUID_K_ROW`, each liquid's own index); mercury held to the closed form, O9 and the truth's pixels | done |
| 78.3 | The room's faces ruled as the wall is, their edge cells with them (the user's choice: a mirror shows them); read exactly by both; O28 | done |
| 78.4 | The mercury bench (300 g), its play (a beaker held beside it); the GPU's pictures | done |
| 78.5 | The user's report: held items and things beside the mercury unseen in it. A ray turned back toward the eye or sideways meets every body in three dimensions (a flat thing a box as deep as it is wide: the user's choice), in both tracers; a mirror's pixels follow every other body | done |
| 78.6 | The user's report: the experiments' list, scrollable and searched | done |
| 78.7 | `make gates`; the user's eye and sign-off ("Approved, merge and proceed", 2026-11-27) | done |

## The split (the user's, 2026-11-27: "Split it off")

Clear surfaces as branches (glass's and water's own reflections traced) were built in both tracers, held ray by ray
(O27 passed), and broke about twenty optics tests: the kept beam shows what glass turns away as the room's mean while
the truth now traced it, so every first reading drifted from the truth (O8's thin lines, the turned vessels), and the
truth's sum over branches made several tests time out; a sensor's display (O25) read 0.36 against 1.01. That work is
kept on branch `reflection-branches-wip` for its own stage, which needs: the kept beam carrying a mirrored reading of
the room (its record on the GPU grows), a cheaper truth (fewer, heavier branches), and the long-wave's sensor path
followed through. With it: the long-wave range by complex Fresnel (`effective_index` retired) and flat things' gloss
(a burner's metal).

## Open

- A metal known only in the visible (mercury) takes its reddest measured index (774.9 nm, R 0.78) in the long-wave,
  where liquid mercury turns back more (its emissivity about 0.1): a thermal camera sees it a little too bright.
- A vessel's liquid is one medium: mercury under water is drawn as their volume mean (the layers of one vessel bend
  as one, as before; GAME_SPEC §20).
- Measured on the GPU (`make pass-cost PLAY=data/plays/mercury.json`, LAB_SHOT_PARITY): at 1x 59 pixels visibly off
  the truth (the mercury's rim); at 4x, after 120 frames, about 450 on the mercury, coloured specks where a mirror's
  rays part (the cube's own edges, a neighbour's outline), and the CPU's frames in double show the same: unsettled, not
  the GPU's. After 600 frames the 4x picture has 1,301 visible in all, its worst (27) the copper sulfate beaker cut
  by the screen's edge, as before this stage. A mirror settles in a few seconds at 4x.
- One pixel on a water beaker's rim (1226, 433 at 4x) is black on the GPU and 0.55 in the truth and in the CPU's single
  precision: a ray held in the rim's glass and turned back, chaotic, now sent by its exact way to a face's cell or an
  edge's (before, every such ray showed the room's one mean, whichever way it went). The GPU's float32 sends it
  elsewhere. Not followed further.
- A ray turned back meets a body on a layer behind its own only as the room's boxes do (a ray heading away again after
  it is not walked through the layers anew); four meetings at most.
- Where the mirror's image crosses the cube's own edge (from one face to the next) a pixel's rays part, and such a
  pixel may stop asking for rays a little off (the open class of Stage 35's TDD): inside the black of the edge cells,
  so it does not show.
