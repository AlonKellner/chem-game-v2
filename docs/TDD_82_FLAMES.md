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
  populations at the flame's temperature (`parts`), the monohydroxides' bands (Meyerriecks and Kosanke) at a calibrated
  share; an element's brightness its Boltzmann sum against sodium's (`brightness`). Data: `docs/DATA_SOURCES.md`, "Flame
  emission (Stage 82)".
- **The glow** (`flame::glow`): each line's light on the two nanometres beside it, in proportion to its nearness (its
  light and its centre kept exactly); a band a Gaussian normalised over the nanometres; in multiples of the room's light
  per nanometre, as `GLOW_ROW` holds an emitter's. No new shader code: the pass, the truth (`exact_through`) and the
  viewer (integer nanometres) read `GLOW_ROW` already, and O2's bound includes it.
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
| 82.5 | The free lab's solutions; pictures on the GPU | |
| 82.6 | Documents, `make gates`, the user's sign-off | |

## Tests

`native/tests/test_flames.cpp` (the ions' data), `native/tests/game/test_flame.cpp`: each line's share Boltzmann's;
the glow keeping each line's light and centre; the flame test filling, fading by 1/e in 3 s, refused unlit, kept by a
save; the screen's gesture; a sodium flame drawn yellow (O1, O2 hold) and seen at 589 nm by the viewer, 400 times its
580 nm; `measured:` each element's strongest line NIST's, the bands' peaks Meyerriecks and Kosanke's.

## Open

- A flame's glow is per pixel of path through it, at any view: a closer view shows the flame no brighter, as a real
  one, but a cone's path through more pixels sums more light; as every glowing gas's (Stage 22).
- Potassium's lilac (its continuum and 404 nm doublet against the burner's own blue) and copper chloride's blue bands
  are not modelled: potassium's flame shows its 766 nm doublet, deep red at the eye's edge.
