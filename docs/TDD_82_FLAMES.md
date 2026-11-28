# TDD 82: Flame emission (Stage 82)

## The request

The curriculum's step E, its third part: the flame test (`docs/CURRICULUM.md`, experiment 10), split off Stage 81 by
the user (2026-11-27, "Close 81, flame next"). A flame shows the lines of what is put into it, and the pass, the truth
and the spectral viewer all see them.

## The design

- **Lithium and strontium**: their ions (NBS's ΔfH° and S°, OBIGT's Cp° and V°, the CRC's diffusion), their chlorides'
  and nitrates' Pitzer pairs (Pitzer and Mayorga), each salt retired to its ions (GAME_SPEC §9). Data:
  `docs/DATA_SOURCES.md`, "Lithium and strontium (Stage 82)".
- **A flame's light** (GAME_SPEC §12.5, `data/flame.json`, `lab/flame.hpp`): NIST's atomic lines weighted by Boltzmann's
  populations at the flame's temperature (`parts`), the monohydroxides' bands (Meyerriecks and Kosanke), each molecule's
  light calibrated against sodium's atoms at the burner's temperature (`band_light`: copper's atoms are barely excited
  at 2200 K, 1e-4 of sodium's, so a share of its atoms' light would leave its flame dark; its green is its CuOH's);
  an element's brightness its atoms' Boltzmann sum against sodium's and its bands' (`brightness`). Data: `docs/DATA_SOURCES.md`, "Flame
  emission (Stage 82)".
- **The glow** (`flame::glow`): each line's light on the two nanometres beside it, in proportion to its nearness (its
  light and its centre kept exactly); a band a Gaussian normalised over the nanometres; in multiples of the room's light
  per nanometre, as `GLOW_ROW` holds an emitter's. The pass, the truth (`exact_through`) and the viewer (integer
  nanometres) read `GLOW_ROW` already, and O2's bound includes it.
- **Band cuts for the lines** (`flame::spikes`): on the GPU the first sodium picture showed no sodium: a pixel's moving
  points, spread by the room light's energy, can all miss a 3 nm spike in its first 128 and the pixel settles without
  it. Each narrow line's nanometres (merged, the brightest eight) go into the scene's lines as lines of no depth on the
  cone's body, and the pass cuts its bands there where a path glows (`line_cuts`): each spike is read by a point of its
  own every frame. A band (4 nm, Gaussian) needs none.
- **Light brighter than the display** (GAME_SPEC §12, the user's choice, 2026-11-28): until now a colour past OKLab's
  lightness 1 was drawn white, and a flame bright enough to see over the light bench is past it (strontium's first
  picture was white). Now a colour with a channel past 1 is scaled down by that channel (its hue and chroma kept, as a
  camera shows a flame), then gamut-mapped as before; in the shader (`gamut`) and on the CPU (`gamut_map`) alike. The
  rule is a channel past 1, not a lightness past 1, so that a fading flame's colour changes smoothly: a side effect is
  that the burner's plain flame (its blue past 1) is drawn a deeper blue than the pale blue it was.
- **The flame test** (`flame_test {burner, from}`): a loop's drop (0.02 mL) of a vessel's liquid into a lit burner's
  flame; the drop's share of each shown element's atoms joins the burner's `flame_load`, fading by exp(−t / 3 s)
  (`fade_flames`, each step). The load is display state, not matter (the burner holds no cell): saved, in the digest
  only when it holds something (so every golden stays), and declared (GAME_SPEC §20).
- **The screen** (`ui.cpp`): a vessel holding liquid carried onto a lit burner is a flame test; the burner's flame cone
  is given the load's glow (`OpticsBody::glow_add`), each element's amount quantised to 5 % steps so a fading flame keys
  a new scene only as it changes by a step (the things' set key hashes the glow).
- **The free lab** (`data/benches/lab.json`): 0.1 M sodium, potassium, lithium and strontium chlorides and copper(II)
  sulfate, 50 mL each.

## Phases

| Phase | Topic | Status |
|---|---|---|
| 82.1 | Lithium and strontium: elements, ions, salts' Pitzer pairs; gated | done |
| 82.2 | `data/flame.json`: NIST's lines, the monohydroxides' bands, the model's calibrated constants; `lab::flame`; gated (each element's strongest line, the bands' peaks, the alkalis' doublets 2 : 1) | done |
| 82.3 | The flame test: `flame_test`, the load fading, save, load and digest; the screen's gesture | done |
| 82.4 | The glow in the pass: `glow_add`, the scene's key; O1, O2; a sodium flame yellow, the viewer's 589 nm | done |
| 82.5 | The free lab's solutions; pictures on the GPU (`data/plays/flames.json`): the lines' band cuts, light brighter than the display keeping its hue, each molecule's own light; sodium orange-yellow, lithium and strontium crimson, copper green | done |
| 82.6 | Documents, `make gates`, the user's sign-off ("Approved, merge and proceed", 2026-11-28) | done |

## Tests

`native/tests/test_flames.cpp` (the ions' data), `native/tests/game/test_flame.cpp`: each line's share Boltzmann's;
the glow keeping each line's light and centre; the flame test filling, fading by 1/e in 3 s, refused unlit, kept by a
save; the screen's gesture; a sodium flame drawn yellow (O1, O2 hold) and seen at 589 nm by the viewer, 400 times its
580 nm; `measured:` each element's strongest line NIST's, the bands' peaks Meyerriecks and Kosanke's.

## Open

- A flame's glow is per pixel of path through it, at any view: a closer view shows the flame no brighter, as a real
  one, but a cone's path through more pixels sums more light; as every glowing gas's (Stage 22).
- **Exposure** (the user's proposal, 2026-11-28): the screen adapting as a pupil does, from the brightest region of a
  coarse grid of the picture (a sixteenth of the screen or so), darkening fast and brightening slowly, 1 at the normal
  room so every colour signed off stays; with dimmable room lights, dim light (fluorescence) seen. A renderer stage of
  its own, after this one.
- Potassium's lilac (its continuum and 404 nm doublet against the burner's own blue) and copper chloride's blue bands
  are not modelled: potassium's flame shows its 766 nm doublet, deep red at the eye's edge.
