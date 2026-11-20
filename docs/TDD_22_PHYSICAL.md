# TDD 22: Everything that takes space is physical (Stage 22)

## The request

> I actually want all elements including things like signal parts and wires and burner - all
> should be physical. If it's an actual space taking part - it should have a spectral visual. The
> only non spectral colors and elements should be overlays like highlights and interaction
> elements. Interactive elements like the "stir" button and the burner slider should not be usually
> visible, but only when the element is selected, maybe even in an actions panel to the side and
> not in the lab grid, because it can obstruct the spectral rendering.

The plan was approved on 2026-11-20, with: colours calibrated, not measured; a part's state as a
lamp; the drip from a condenser's outlet a flat highlight for now. GAME_SPEC §12.3, §13.1, §20.

| Phase | Topic | Status |
|---|---|---|
| 22.0 | Quicker gates (asked for by the user): a ctest entry a test file, the reference's heavy loops on threads, sparse samples under the sanitizers. The sanitizer gate 35 min -> 5 min | Done |
| 22.1 | The spec; the calibrated rows | Spec done; rows pending |
| 22.2 | The scene: 256 bodies, spectra named by set, a grid of squares for a pixel to find its bodies | Pending |
| 22.3 | Three shapes: the slab, the bar, the cone; opaque and glowing | Pending |
| 22.4 | The screen: parts, lamps, wires, the burner and its flame, the outlet as bodies; nothing of the lab drawn over the pass, and a test that holds it | Pending |
| 22.5 | The controls in the readout's column; the tutorial, the tests and the tools | Pending |
| 22.6 | The GPU host; O4 on every picture, one of a vessel held over a part and a burner; the cost | Pending |
| 22.7 | Pictures; the user judges by eye and signs off | Pending |
