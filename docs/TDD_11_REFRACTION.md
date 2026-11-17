# TDD 11: Refraction and Fresnel (Stage 11)

## The request

> Refraction and Fresnel (the plan's Stage 11: "visual sign-off").

Stage 10 drew a vessel as a straight path through its liquid against a wall that was the same
everywhere behind it. Stage 11 makes glass and liquids bend the light: each vessel is a glass
cylinder, the view through it is bent at every surface by the index at each moving point's own
wavelength, the liquid is crossed along the ray's real chord, the surfaces reflect the room in
front, and the bench's drawing behind a vessel is seen bent.

| Phase | Topic | Status |
|---|---|---|
| 11.1 | The spec (GAME_SPEC §12.3: the glass, the bend, the chord, Fresnel, what does not bend; O5, O6) | Done |
| 11.2 | Data: water's index (Daimon and Masumura 2007), borosilicate's (Schott BOROFLOAT 33) and density; the calibrated room | Done |
| 11.3 | `chem_datagen`: indices as spectra at 1 nm (a Sellmeier formula, or a Cauchy fit to a table, refused beyond 2e-4) | Done |
| 11.4 | The scene: indices, the glass's thickness, the bench's drawing as flat greys each body can see | Done |
| 11.5 | The per-pixel source: `bend_at` (where the ray lands, what passes, the chord), `backdrop`, Fresnel; the reference's `optics_bend` | Done |
| 11.6 | Tests: the indices, O5, O6, the lens, the chord, the rims, the bench, the frames settling | Done |
| 11.7 | The GPU: O4 with refraction; the cost remeasured | Done |
| 11.8 | The prism demo: flint glass traced through flat faces, a slit in a dark room (`lab::PrismDemo`, `cg_demo_*`, `ChemOpticsDemo`, `make prism`) | Done |
| 11.9 | It fails: pixels that see the slit through a narrow window of wavelengths stop wrong, and stay stale when it turns | Found |
| 11.10 | Band cuts where a pixel's light jumps (bent views crossing edges, route changes, narrow lines); the content key's geometry; `chem_tool study frames` | Done |
| 11.11 | The scene test with a refracting ball (the replay and the published page, round 9) | Done |
| 11.12 | The user's sign-off | Done (2026-11-17: "Looks very good, approved, proceed") |

## Decisions

- **Concentric cylinders are exact.** For a ray `d` from the axis, `n·r·sin θ = d` at every
  surface, so each angle is `asin(d / (n r))`, the turn is twice the sum of the differences going
  in, the ray leaves on a line `d` from the axis, and inside a medium it runs `d / n` from it. No
  tracing: a handful of `asin` a point.
- **What is behind is drawn, not uniform.** Against an even wall refraction shows only at the
  rims. The draw list's first part (the grid, the panels' edges) is turned into flat greys, and
  each body carries the ones its rays can reach (within three widths to the left and four to the
  right, at its height; up to 32). Outside the body the same lines are the flat drawing, so where
  nothing bends there is no seam.
- **A pixel looks from its centre.** Jittering the landing point within the pixel would
  antialias lines seen through glass for free, but the truth the GPU is held to (O4) samples the
  centre; integrating the truth over the pixel is too slow for the windowed check.
- **Fresnel by coverage.** A rim pixel the vessel covers in part turns away only that part of
  what its surfaces reflect; equal indices reflect nothing (a pixel just outside the cylinder was
  otherwise a grazing ray, all reflected).
- **A switch for the old tests.** `OpticsRoom::refraction` is on in the game; the Stage 10 tests
  turn it off, so absorption, reflection and emission are still tested alone. O5 ties the two.

## What it measures

- The indices: water 1.3330 at 589.3 nm; glass within 2e-4 of Schott's six values, Abbe number
  65.4 (Schott: 65.41).
- **A 6.3 cm vessel of water 20 cm in front of the wall is a lens:** the wall is seen through it at
  −2.166 times near the axis; paraxial optics says 1 − 2D(n − 1)/(nR) = −2.165. Inverted,
  magnified, symmetric about the axis, and stretched further in blue than in red.
- **The chord:** what the salt takes away is `exp(−τ L)` with `L = 2 sqrt(r² − (d/n)²)`, to 1e-5,
  longer than the straight chord off the axis.
- **Fresnel:** face on, glass then water lets through `(1 − R₁)²(1 − R₂)²` (92.5% at 550 nm); at the
  rim much less, so the rim shows the darker room in front. In a room as bright in front as
  behind, clear glass and water change nothing to 1e-7 (O6; the scene's radiances are float32).
- **The frames settle on the bent view:** through glass and a solution, with a grid behind, the
  renderer's frames reach a worst of 1.8 and a mean of 0.20 on the eye metric, then spend nothing.
- **O4** (`make screenshots`, Apple M3 Pro): at the five moments the GPU's settled picture is within
  3 of the truth on the eye metric, nothing visible.
- **Cost** (three runs): settled about 0.1 ms a pass; every covered pixel starting again 0.93 to
  0.98 ms.

## Found on the way

- Stage 10's recorded cost (1.9 ms settled, 11 ms the worst frame) was taken while the machine was
  busy: whole frames took 70 ms then, about 3 ms now. Remeasured, withdrawn in GAME_SPEC §19.
- `items()` of a temporary JSON object dangled in `chem_datagen`: the indices came out empty
  until the object was held in a local.
- At 0.5 mol/L and 680 nm six centimetres of solution let through about 1e-19: all a pixel shows
  there is the 2% of the room the glass reflects. True to life (a strong solution in glass shows
  the room in its surface); the chord test uses 0.02 mol/L.
- A commit went in on a failing gate: the gate's output was piped through `tail`, which hid its
  status. Amended before any push; the gate's own status is checked since.

## The prism, and the cuts

The user asked for a prism to look through at a thin source of light in a dark room. Through a
60° prism of N-SF11 (Schott: nd 1.78472, Vd 25.76) the least deviation comes out 2 asin(n sin 30°)
− 60° = 66.3092° to every printed digit, and violet turns 9.4° further than red. The slit spreads
into its spectrum, but the first build showed two failures, both from pixels whose light is a
narrow window of wavelengths (the slit seen at 665.8–687.2 nm, 0.7% of the light's energy):

- a pixel that missed the window in its first frames measured no noise and stopped for good, the
  dark room where the truth is a dim red (11 of 95 pixels on a row, after 240 frames);
- after a turn the fixed estimate, as blind to the window, said nothing had moved, so stale
  histories were kept: stripes of wrong colours.

A larger floor before stopping (512 points, 16 frames) left 2 of 95 stuck and did nothing for the
turn. The fix, at the user's choice and made general at their request: cut a pixel's bands
wherever its light is known to jump in wavelength (GAME_SPEC §12.2 item 6).

The user asked that false positives and negatives be shown to fall on the prism, the scene and
the palettes. `chem_tool study frames` (the reference's frames against the truth, every second to
fourth pixel):

| Scenario | Bands | Points a pixel a frame | Wasted | Visible | Persistent | Longest run |
|---|---|---|---|---|---|---|
| prism | no cuts | 15.26 | 7.25 | 24.78% | 23.05% | 240 |
| prism | cuts | 1.55 | 0.80 | 0.00% | 0.00% | 8 |
| bench | no cuts | 7.00 | 2.79 | 0.00% | 0.00% | 0 |
| bench | cuts | 7.03 | 2.80 | 0.00% | 0.00% | 0 |
| palettes | no cuts | 1.63 | 0.79 | 0.00% | 0.00% | 2 |
| palettes | cuts | 1.74 | 0.86 | 0.00% | 0.00% | 0 |

The bench (glass vessels dragged, jumping, poured into, reacting, before the grid) first showed
1.23% visible with runs of 92 frames in both columns: a dragged vessel's pixels above its liquid
kept their history, because nothing in the content key changed with where the view crossed the
glass. The key now holds each layer's coverage and the glass's geometry; a test fails without it
(21 pixels wrong) and passes with it.

The scene test itself (the CPU replay of round 8's page, every third pixel, 600 frames), its
dragged ball now a refracting glass sphere of water over the poster, the spot and the tube:

| Method | Points a pixel a frame | Of them wasted | Visible behind the ball | Visible, all | Persistent |
|---|---|---|---|---|---|
| 16 points everywhere | 16.0 | 97.2% | 8.4% | 0.3% | 0.0% |
| decided, no cuts | 1.6 | 43.8% | 0.0% | 0.0% | 0.0% |
| decided, cuts | 1.6 | 43.8% | 0.0% | 0.0% | 0.0% |

Water and glass bend too little for a view to cross an edge between wavelengths, so behind the
ball the cuts change nothing; they cost nothing either. The published scene test (round 9) shows
the same ball, with the three methods and the control, for the user's eye.

## Not done, and said so

- Light cast through glass onto a surface (a rainbow on a wall): not built (GAME_SPEC §12.3
  status); it needs a light besides the room's and its light followed forwards.
- The prism demo's slit is very bright in the red, where glass's index changes slowly and a
  pixel sees many nanometres of it: with fixed exposure the red end is near the display's white.
- A solution's index is water's; what lies in a vessel, the balloon and anything up and down bend
  nothing; only the first vessel a pixel looks through bends its view (GAME_SPEC §20).
- The wall's distance and the room in front are calibrated, not measured.
