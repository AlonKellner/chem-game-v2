# TDD 46: The long-wave range (Stage 46)

## The request

The user wants a thermal camera (Stage 47): an instrument whose sensor frame is a rectangle the size of its screen,
see-through by default, showing every point's "infrared colour" with a thermographic palette, the hottest and
coldest spots marked, "like real temperature camera sensors". Through questions and answers (2026-11-24) the
design became two stages:

- **This stage**: the long-wave range in the renderer, seen through a debug infrared view (off by default).
- **Stage 47**: the camera itself.

What the user settled:

- **The sensor**: an LWIR microbolometer, 8–14 µm. Such cameras never see visible light: their germanium
  optics are opaque to it.
- **The rendering**: "the exact same pipeline as regular rendering, just for a different range of the spectrum
  and with a different mapping into actual colors". At a camera's screen a ray goes on, in the long-wave
  range, from the matching point of the frame. The long-wave spectrum becomes RGB in one step, "not
  spectral to RGB to spectral to RGB again". The cost is about a pixel's, not twice it.
- **One scene, complete at any view**: a frame may be anywhere in the room.
- **The long-wave data**: measured where it exists.

GAME_SPEC 0.46.0 covers it in §12.4 and §18 (O21 to O25).

| Phase | Topic | Status |
|---|---|---|
| 46.1 | GAME_SPEC §12.4 and O21 to O25; the data's rows in `docs/DATA_SOURCES.md` | Done |
| 46.2 | The core's long-wave data and its gates | Done |
| 46.3 | A row's long-wave segment, after every set's visible rows | Done |
| 46.4 | Each body's temperature in the scene; Kirchhoff in the three shadings and the two tracers; `glow_row` retired | Done |
| 46.5 | The long-wave light and the range a point is read in | Done |
| 46.6 | One scene at any view: the grid on the room's squares, no culling; bodies hidden from a sensor's rays | Done |
| 46.7 | The sensor mapping: the reference, the GPU host's pool of records (O25) | Done |
| 46.8 | The debug infrared view; O4 on its picture; `make screenshots` and `make pass-cost` | Done |
| 46.9 | This document, CHANGELOG, `make gates`, the user's sign-off | `make gates` passed (688 s: ASan+UBSan 688, ctest 416, Godot 12 of 12, goldens unchanged); the sign-off awaited |

Stage 45 (another agent's, boiling's bubbles) merged into main during this stage. It took the bodies' vec4 6 and
added a seventh row to each set. Main was merged into this branch (`a3ba205`), and a body's temperature moved to
its SET's y and its hidden bits to its GLOW's w.

## 46.2 The data

| Data | Source | Gate (`native/tests/test_measured.cpp`) |
|---|---|---|
| Liquid water's n + ik over 7.9–14.5 µm (`data/optics/water_lwir.csv`) | Hale and Querry 1973, at 25 °C | n 1.218 and k 0.0508 at 10 µm, k 0.199 at 12 µm, as printed; 638 /cm at 10 µm; its band emissivity at least Fluke's 0.93 |
| Glass's n + ik over 7.96–14.04 µm, fused silica's (`silica_lwir.csv`) | Kitamura, Pilon and Jonasz 2007, their model of Popova et al. 1972 | the Reststrahlen peak, R = 0.694 at 8929.5 nm, within 1 % and a step; opaque; the band emissivity within 0.1 of plate glass's 0.85 |
| Grey emissivities: plastics, rubber 0.95; glass 0.85; the burner's oxidised steel 0.8 | Fluke Process Instruments' tables, 8–14 µm | each row as tabled |
| Powders 0.9; the latex's transmittance 0.3; a flame's gas 0.01 /cm and soot 0.005 /cm at 10 µm | calibrated, low confidence (none measured found) | the flame's reading below |
| The sensor's response: flat over 8–14 µm | design | none |

The core (`chem/optics.hpp`):

- `lwir_n` and `lwir_k` at 20 nm, `normal_reflectance`, `effective_index` (the real index with the same normal
  reflectance) and `absorption_per_cm`;
- `lwir_emissivity`, `lwir_transmittance`, `lwir_flame`;
- `planck`, `band_signal` (the band's integral in closed form, the series of ∫x³/(eˣ−1));
- `brightness_kelvin` (a camera's temperature from its signal, by its emissivity and a reflected temperature,
  to a millikelvin);
- `lwir_energy_table`.

Fused silica stands in for the borosilicate (none tabulated over the band): medium confidence.

## 46.3–46.5 The second range

- **The rows.** Every row has a long-wave segment of 301 values. The segments lie after every set's visible
  rows; the header's first vec4's w says where they begin.
  - So the visible rows are laid out as they were, and no test that indexes them changed.
  - `row_at` reads the segment a wavelength falls in.
  - The scene fills each set's segment:
    - the liquid: water's absorption by its share of the volume;
    - the solids: 1 − ε;
    - the wall: glass's absorption per cm, or a membrane's depth;
    - the indices: n_eff;
    - a flame: its absorption per pixel of path, in the liquid's row.
- **The light.** The room's long-wave light is the second of two.
  - It is a black body at the room's temperature times the sensor's response, cut into equal shares of its
    signal; its colour is grey, so a pixel's sum is its signal in each channel.
  - The room's temperature is the header's vec4 4 z, from the lab's view (`LabView::room_celsius`).
- **The range.** `band_range`, a per-thread global, says which light, which places along the spectrum and which
  wall a point reads.
  - In the long-wave range the wall, the room's cube and the room in front are all the room's black body, 1.
  - So the wall's table is not read there.
- **Kirchhoff.** Every layer emits θ_T(λ)(1 − a − r):
  - in the flat shading (`shade_layer`), the refractor's own layer (`shaded`) and a traced ray's (`body_does`),
    in the pass's source and in the truth's reading;
  - θ is the black bodies' ratio in the long-wave range (exactly 1 at the room's temperature);
  - in the visible it is Planck over the room's radiance, which the light's table carries in its y, exactly 0
    below 500 K.
  - A hot solid's glow is now this. The glow row it had is retired, and the two agree to the floats the scene
    holds (1e-5).
- **The tracers' split.** The pass's tracer and the truth's take each medium's absorption along the way, and keep
  what the surfaces turned away apart.
  - A ray's record and a kept beam carry what its glass took in (`KEPT_VECS` 24 to 26).
  - In the visible, glass takes in nothing, and every sum is as it was.
- **Opaque glass ends a ray.** A ray that glass has taken in all but nothing of (pass under 1e-9) goes no
  further. It leaves from where it met the shape, as it came, so a stretch's rays go one way at every
  wavelength.
  - Without this, glass's index swings across its Reststrahlen band (n_eff 1 to 9.4), the stretch was halved
    again and again, and a pixel of the cabinet's condenser followed 216 rays where the visible's 25 (O13 failed
    in the play's infrared mark).

## 46.6 One scene at any view

- **The grid.** It lies on the room's own squares at the view: the header's sixth vec4 holds where the layout's
  (0, 0) is and how wide a square is. So a point of the room finds its bodies whether the view shows it or not.
- **No culling.** The scene keeps every body at every view; the traced passes still run only on the screen.
- **The test.** A point off the screen at 4× reads, in the long-wave range, what it reads laid out (to 1e-5,
  over 26 small slabs and the bench). The same test fails 267 times with the old grid's squares.
- **Hidden bodies.** A body can be hidden from a ray (`hidden`, a sensor's own case and display), in `path_of` and
  `ray_met`.

## 46.7 The sensor mapping

A sensor's display (`OpticsBody::Sensor` with an `OpticsRelay`) is a slab with no light of its own. Its block in
the images' buffer holds:

- the frame's offset, the emissivity and the reflected light;
- the palette's span, its hidden bit, its rectangle in the pool;
- a tag of what lies under its frame;
- the palette, as its primaries' drive;
- the primaries' light.

Each display pixel keeps a record in a pool (`relay_vec`, `relay_put`; the GPU host's binding 22, sized by
`cg_optics.relay_pool`):

- **Its sensor pixel's mean signal.** The frame's layout pixel matching the display's LCD pixel under it. Points
  are spread over it, each at its own share of the long-wave light, read as any point is (flat, or one ray where
  something bends). They are kept until the standard error is 0.1 % of the signal (about 0.06 K), at most 1,024.
- **Its three primaries' light through what is in front** (64 bands of the visible light), when the front changes.

The pixel shows its front's colour plus each primary through the front, times the palette's drive at the sensor
pixel's temperature, times the share of that subpixel the pixel covers:

- the temperature is the signal less the room it reflects, over its emissivity, as a black body's;
- one step from the band's signal to RGB, the palette applied to the mean.

O25 holds it within 8 on the eye metric of its truth. The truth is:

- the front by `optics_truth`;
- the primaries over 1,024 bands;
- the signal by the truth's own tracer on a 6 × 6 grid of the sensor pixel at 96 shares of the band.

It fires on a frame a pixel off, and on a hotter face under the frame (then settles to it again).

## 46.8 The GPU

**`make screenshots`** (Apple M3 Pro, after main's merge):

- 15 pictures, one of them the infrared view at the boiling.
- 0 to 11 pixels a picture visibly off, the largest 11 on the eye metric: Stage 42's 0 to 12 and 14, the known
  O4 pixels (`docs/TDD_37_RIGHT.md`).
- The infrared picture: 6 of 72,934.
- O4 compares the infrared view as it is shown: the reference shows a pixel's band signal as the view does.
- The windowed run's limit is now 120 s: the sixteenth picture took it past 90.

**`make pass-cost`**:

| Mark | A frame | The worst |
|---|---|---|
| settled | 8.35 ms | 10.0 |
| carry | 8.34 | 8.5 |
| pour | 8.36 | 21.7 |
| stir | 8.59 | 19.1 |
| fast | 8.34 | 14.0 |
| infrared (new) | 8.33 | 12.8 |
| a whole restart | 13.6 (in the infrared view) | |

- The machine was under another agent's builds (load 14): the same play at the stage's first measurement,
  before the early-out, was 6.9 ms a frame and 24.7 ms for a visible restart (25 before the stage).
- The infrared view costs what the visible does.

**What the infrared view shows.** At the boiling the flask is a yellow-orange, the condenser a warm magenta,
the flame's core white. The cool beaker, the bottles, the parts and the room are a uniform dark violet: everything
at the room's temperature is the room's light (O21).

## The new invariants

| ID | Test |
|---|---|
| O21 | `long-wave: in a room at one temperature every pixel reads the room's light, through anything`. The whole bench: vessels traced, a heap, a balloon, a face, a wire, a flame, the burner's metal, a held vessel. Every covered pixel at five wavelengths, to 1e-12. Fires on a body a millikelvin off, on bodies that emit nothing, and in the visible range |
| O22 | `long-wave: a ray's shares through absorbing glass are whole`. The pass's tracer and the truth's, 1e-12; opaque in the band, nothing taken in in the visible |
| O23 | `long-wave: the light's table is the band's integral`. 1e-7 of the closed form; fires on a table a kelvin off |
| O24 | `long-wave: the visible picture is as it was`. Every body below 500 K: the long-wave rows and light garbled, the temperatures changed, the picture bit for bit the same; fires on a heap at 900 K |
| O25 | `long-wave: a sensor's display shows its frame's long-wave light, settled to its truth` |

**The physics, measured** (an 8–14 µm camera at an emissivity of 1):

- a vessel of water at 70 °C reads 59.7 °C, its glass emitting at 0.8 and turning the room back;
- the slab behind it is not seen at all;
- the burner's flame at 1927 °C reads 119 °C: faint;
- oxidised steel at 200 °C reads 174 °C, a plastic face 194 °C.

## Open

- **The palette and the span of the debug view** are fixed: an iron bow, 5 K under the room to 95 K over it. The
  camera's own (Stage 47) are its settings.
- **A traced front over a display** is read flat: a vessel held before a camera's screen filters it as it
  lies, not as it bends.
- **The flame's long-wave data** is calibrated, not measured (Edwards' band parameters not read).
- **Absorbing glass beyond the outer surface.** In the long-wave range a ray absorbed in glass brings nothing
  from beyond it. A reflection from an inner surface, a few per cent of a few per cent, is counted as taken in.
- **The room is one temperature**: the wall, the cube and what the room in front sends are its black body.
  Hot things are not seen in a glass's reflection, as everything reflected is the room in front (§12.3).
