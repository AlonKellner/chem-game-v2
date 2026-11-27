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
| 78.3 | The truth: both branches at a clear surface; O6, O7, O21, O22 restated; O27 | |
| 78.4 | The pass: the branch seed and roulette in the corrections; the beam's turned share in the mirror direction | |
| 78.5 | The mirror's beam (R ≥ 0.2) where it is needed; cost on the GPU (`make pass-cost`) | |
| 78.6 | The long-wave range by complex Fresnel (`effective_index` retired); flat things' gloss | |
| 78.7 | Scenes: the mercury bench, glass and water; `make screenshots` (O4); the user's eye; O28 | |
| 78.8 | Documents, `make gates`, the user's sign-off | |

## Open

- A metal known only in the visible (mercury) takes its reddest measured index (774.9 nm, R 0.78) in the long-wave,
  where liquid mercury turns back more (its emissivity about 0.1): a thermal camera sees it a little too bright.
- A vessel's liquid is one medium: mercury under water is drawn as their volume mean (the layers of one vessel bend
  as one, as before; GAME_SPEC §20).
- A ray turned back toward the eye meets no body on a nearer layer, nor a neighbour on its own (as a ray bent
  sideways did not, before).
