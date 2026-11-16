# TDD 10: The optical pass (Stage 10)

## The request

> … with GPU-optimized optical rendering …
>
> You may completely change the samples, select any numbers you think will suffice. However,
> measure the error, and come up with an acceptable threshold, after knowing the acceptable error
> you may decide the amount of samples in the visible range (4 or 6 or 8 or whatever).

Stage 10 replaces the flat colours with light: what is in a vessel is seen by the room's light
through it, computed per pixel on the GPU from the live composition and the real path length.

| Phase | Topic | Status |
|---|---|---|
| 10.1 | Spectra as data: CIE 1931 observer, D65, each species' medium (`data/optics/`), embedded by `chem_datagen` | Done |
| 10.2 | The 1 nm reference colour and band sets (`chem/optics.hpp`); CIE L\*a\*b\*, CIEDE2000, sRGB (`lab/colour.hpp`) | Done |
| 10.3 | The sample-count study (`chem_tool study spectrum`), the threshold, the choice of 24 bands, its gate (`O3`) | Done |
| 10.4 | The per-pixel function, one source for shader and reference (`native/game/shader/optics_pixel.gdshaderinc`) | Done |
| 10.5 | The scene from live state (`lab/optics_scene.hpp`: `optics_compose`), `O1`, `O2` | Done |
| 10.6 | The screen in three layers; the bench as a lit wall | Done |
| 10.7 | The C API (`cg_ui_optics`, `cg_ui_optics_compare`) and the Godot host's shader pass | Done |
| 10.8 | GPU against reference (`O4`) and the pass's cost, in `make screenshots` | Done |
| 10.9 | The user's sign-off | Pending |

## The sample-count study

`chem_tool study spectrum` (13 s; also a ctest). The test set is every colour the demo can show:
54 copper sulfate solutions (0.05 to 4.7 mol/L across 0.5 to 10 cm), the four solids bare and
under solution (28), the balloon's rubber at five thicknesses alone and in front of solution (10),
and black bodies from 800 to 2500 K (18). Each is computed at 1 nm and by n bands; the difference
is CIEDE2000. Threshold: worst ≤ 1.0 (about one just-noticeable difference), mean ≤ 0.5.

"even": equal widths over 400–700 nm. "weight": equal shares of what the eye sees (x̄+ȳ+z̄ times
D65). "fitted": the cuts moved one at a time to lower the error on this very test set. A band
carries each absorber's mean coefficient, or its mean and its slope ("tilt": the band's light is
then dimmed by `exp(−τ)·sinh(u)/u`). `*` is within the threshold.

```
    | mean only                                       | mean and tilt
  n | even            weight          fitted          | even            weight          fitted
    |   worst   mean    worst   mean    worst   mean  |   worst   mean    worst   mean    worst   mean
  3 |  50.023 18.707   42.049 10.092   21.769  7.715  | 144.599 23.910  140.546 37.507   44.751  8.498
  4 |  19.409  5.494   18.113  5.414    7.912  4.015  | 134.337 10.099  108.544  8.261    7.772  3.823
  5 |  23.043  6.057   31.002  5.855    4.330  1.648  |  50.309  6.145   50.422  4.012    4.758  2.394
  6 |  25.546  4.650    7.143  2.094    4.277  2.117  |  13.383  2.850   27.559  3.479    3.408  1.714
  7 |   5.850  2.586   18.544  3.130    3.316  1.072  |   9.420  2.214    8.732  1.705    2.831  1.416
  8 |  10.129  2.498    4.937  1.045    3.828  1.043  |   7.201  1.749   14.371  1.826    1.852  0.925
  9 |   6.716  1.937    9.806  1.573    1.340  0.576  |   4.084  1.252    6.219  1.110    1.409  0.704
 10 |   4.318  1.501    4.937  0.927    1.346  0.558  |   3.648  1.055    7.096  0.991    1.294  0.646
 11 |   4.850  1.327    4.750  0.906    1.455  0.520  |   2.944  0.867    5.530  0.853    0.946  0.474*
 12 |   3.401  1.087    3.246  0.836    1.298  0.620  |   2.273  0.717    3.169  0.557    0.768  0.384*
 13 |   3.214  0.969    2.510  0.580    1.048  0.417  |   1.983  0.631    4.577  0.667    0.654  0.327*
 14 |   2.842  0.829    3.202  0.639    0.741  0.241* |   1.647  0.528    2.379  0.419    0.580  0.291*
 15 |   2.379  0.709    1.509  0.414    0.756  0.378* |   1.454  0.463    3.185  0.491    0.543  0.273*
 16 |   2.242  0.656    2.463  0.455    1.084  0.238  |   1.209  0.411    2.150  0.358    0.437  0.218*
 20 |   1.366  0.407    1.269  0.274    0.563  0.178* |   0.788  0.260*   1.630  0.257    0.290  0.142*
 24 |   0.967  0.290*   0.798  0.193*   0.369  0.118* |   0.541  0.183*   0.977  0.169*   0.237  0.113*
 28 |   0.701  0.210*   0.663  0.148*   0.267  0.105* |   0.392  0.133*   0.717  0.124*   0.183  0.081*
 32 |   0.564  0.163*   0.490  0.110*   0.257  0.064* |   0.298  0.102*   0.583  0.098*   0.163  0.072*
```

What it says:

- **Few bands are far off.** With 4 to 8 bands the worst colour is wrong by 5 to 20: a strong
  solution is not the blue it should be. The cause is the exponential: the band's light that gets
  through is mostly at the band's clear end, and an average coefficient cannot say so.
- **Fitting the cuts helps a lot** (14 bands pass), **and is not to be trusted.** The cuts are
  fitted to the colours they are then judged on, all of them copper's. Non-monotonic rows (15
  passes, 16 does not) show how much of it is the search's luck.
- **The tilt halves the error** at a given n once n is about 10, and is worse below that. It
  costs a second number per band per medium and a `sinh` per band per pixel.

**The choice: 24 bands, equal-weight cuts, mean only.** Worst 0.80 (malachite under 3 cm of
3 mol/L solution), mean 0.19. It is the smallest set within the threshold whose cuts know nothing
of the media, so a new substance gets the same treatment as copper; the shader stays a plain
`exp` on six `vec4`s. Its five worst colours: 0.80, 0.61, 0.54 (malachite under solution), 0.49
(a body glowing at 800 K), 0.47 (1.3 mol/L across 10 cm). Synthetic 10 nm absorption lines are
off by up to 4.1: the demo has none; the full design's line spectra will need narrower bands or
the tilt, and the gate (`O3`) will say so when such a medium is added to the test set.

## Decisions

- **One source for the shader and the reference.** `optics_pixel.gdshaderinc` keeps to what GLSL
  and C++ read alike (no swizzles, no `out` parameters). Godot compiles it as the fragment
  shader; `optics_reference.cpp` includes the same file with `float` as `double` and a dozen
  small functions standing in for the shading language's. The GPU's picture is held to that
  build, so the tests of colour, path length, coverage, heaps, haze, glow and the balloon run
  without a GPU and still speak for the shader.
- **One table in, nothing back.** The host's whole knowledge of optics is one `uniform vec4
  scene[378]`, uploaded when its revision moves. Concentrations, band averages, the glow's
  Planck integral and the geometry are computed in float64 in the lab layer.
- **Seen by transmitted light.** Beer-Lambert needs light behind the glass, so the cabinet, the
  bench and the outbox are now a lit wall and the flat drawing on it is dark. The readout and the
  clock stay dark.
- **Real path lengths.** A vessel is a cylinder of its actual volume; the chord at each pixel is
  in centimetres. The same solution is paler near the rim and in a narrower vessel, as it is.
- **A heap, not a layer.** Five grams of powder across a flask's floor would be one pixel high.
  At a powder's angle of repose it is a cone a centimetre high, which is what it looks like.
- **Stirring is visible.** At rest a powder lies under the liquid; stirred, it clouds it, by its
  particles' own count and size.
- **The glow is absolute.** The room's white is 100 cd/m²; a solid's emission is Planck's law
  against that. Nothing in the demo is hot enough to see (350 °C gives a millionth of the room's
  light), which is true; the code is tested at 850 °C (a third as bright as a white page, red) and
  against the platinum standard.
- **A clear liquid gets a drawn surface line.** Water and acid absorb nothing over these paths
  and nothing refracts yet, so without the line the acid bottle looks empty. Stage 11's
  refraction is the real answer; the line is the stopgap.
- **O4 is not in the commit gate.** It needs a GPU and a window. `make screenshots` plays the
  demo headless first, then windowed under a time limit, and fails if any of the five pictures
  departs from the reference by more than 2/255.

## Found on the way

- The first GPU comparison failed at 92 pixels: the row just under the beaker. There the body
  covers the pixel by an amount that is zero in exact arithmetic; float64 made it barely positive
  and float32 did not, so one renderer drew the backdrop and the other drew nothing. The
  comparison now ignores pixels a body covers by less than a fiftieth, and says so.
- The GPU timer reads zero on this Metal driver, and the display hands frames out in whole
  refreshes. The pass is timed instead as the slope of the frame time between 100 and 300 extra
  passes.
- `lab::Lab` (CIE L\*a\*b\*) collided with `lab::Lab` (the lab): the colour is `CieLab`.

## Evidence

- `chem_tests` and `lab_tests`: `O3: the game's bands show every colour of the demo within the
  threshold`, and that it fires on 6 bands; `UX4: a solution's colour is that of its
  concentration and of the path through it`; the blue deepens with concentration and depth and
  only `c·L` counts; coverage at a surface and a rim; each solid bare and under solution within
  the threshold of its 1 nm colour; stirred against at rest; the glow at four temperatures and at
  2042 K; the balloon; `O1, O2: the checks hold on a scene and fire on a corrupted one`; the
  whole demo session with `O1` on every scene and `O2` on a grid of pixels after every command,
  the flask turning blue and then deeper blue.
- `make screenshots` (Apple M3 Pro, Metal, 1920×1080), GPU against reference where a body is:

  | Moment | Pixels in bodies | Largest difference | Over 2/255 |
  |---|---|---|---|
  | Set up | 37,946 | 1/255 | 0 |
  | Decomposing | 52,024 | 1/255 | 0 |
  | The pour panel | 59,822 | 1/255 | 0 |
  | Boiling | 66,726 | 1/255 | 0 |
  | Crystals | 66,726 | 1/255 | 0 |

- **Cost:** a frame with 100 extra passes 21.6 ms, with 300 extra 64.2 ms: **0.21 ms a pass** at
  1920×1080 (budget: 2 ms on an integrated GPU).
- The Godot host's suite: 7 tests (the new one: the pass is a shader between the two flat layers,
  and its table follows the screen).

## Not done, and said so

- Refraction and Fresnel: Stage 11.
- Every medium's spectrum is calibrated to a known colour, not measured (`docs/DATA_SOURCES.md`).
- No sheen on the balloon, no simulated flame, no fog; a mixture of powders is the mean of its
  parts; colours outside sRGB are clipped (GAME_SPEC §20).
