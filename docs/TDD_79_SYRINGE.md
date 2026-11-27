# TDD 79: The gas syringe (Stage 79)

## The request

The curriculum's step G, its first part (`docs/CURRICULUM.md`: C7, T8; the roadmap's area 5), chosen by the user
after Stage 77's sign-off (2026-11-27): "Gas syringe + gas-law experiments". A glass gas syringe whose plunger is free
(it moves until the gas inside is at the room's pressure) or held at a volume the player sets (pushing it in does
P dV work on the gas), fitted to a flask's mouth like a balloon; Boyle's law, Charles's law and absolute zero, a
gas's molar volume. Valves on tubes, a vacuum pump, humidity and Graham's law are later.

## The design

The user, as the stage began (2026-11-27): "Holding the syringe handle should exert a force on it, the force of the
player hand should be limited"; four setups to show what a syringe does: a sealed syringe of liquid (it hardly moves:
a liquid does not give), a sealed syringe of air (pushed and let go, it springs back to about where it was), two
syringes of liquid joined (one pushed, the other moves out with it), two of air joined (one pushed, the other moves
too, a little behind: the air between gives); and "The handle should be a linearly dragable element, where dragging
it exerts the force. It should not be a side panel slider ... just make the plunger element have a physical drag
control feature on the handle" (physical controls for other things, a burner's, later).

### The core: a piston (GAME_SPEC §6)
- **A piston** (`World::set_piston(cell, Piston)`): a rigid cell's (or a column's whole) volume moved by a plunger of
  an area `A`, between its stops, by the forces on it: its gas's pressure on its face and the room's on its back,
  `(P - P_atm) A` outward; its friction, `f` against its motion (and holding it still within `f`); its drag, `c v`; a
  hand's force `F` (none when it is let go). Overdamped (its own mass, tens of grams, stores nothing a step's
  friction does not take): `c v = F + (P - P_atm) A - f sgn v`, with `P` the pressure after the step's move (implicit:
  stable for any stiffness, a liquid's included).
- **The hand**: held, it pulls the plunger toward where the player drags it, as fast as a step allows, with what
  force that takes, at most `F_most` (79.8 N: the average maximum of a thumb on a plunger, Astin 1999 by Vo et al.
  2016; a pull taken as much); past it, at `F_most`, the plunger moves only as its forces let it.
- **Joined pistons**: every cell joined by open edges to a piston's (gas between the cells, liquid between their
  layers) is one space at one pressure for its pistons: one root `P` for the step, each piston's move a monotone
  function of it, the space's gas at its free volume after them (its liquids and solids incompressible: the plunger
  stops where they fill the space; an open space is at the room's pressure).
- **The energy**: the hand's work `F dx` along the piston's edge from the lab's mains, the only work (E = U + P_atm V:
  the room's work on the plunger's back and the gas's on its face are inside it; friction and drag are heat in the
  cell): `F dx = (P_atm - P) dV + (c v + f) |dx|`, so the gas takes at least the reversible work of a compression and
  gives at most it on an expansion, and its entropy never falls (T2). A free plunger does no work: the cell's E stands.
- **Liquid between joined syringes**: a sealed column pushed past its liquids gives the excess along a liquid edge
  from its lowest layer to a joined column's, until their pressures are one (as `equalize` does for gas).
- The sealed mark is the world's own (V3): a capped syringe is a rigid cell its piston moves.

### The lab (GAME_SPEC §13)
- **A gas syringe** (`data/glassware.json`, `gas_syringe`): a graduated glass barrel of 100 mL (a bore of 32.6 mm,
  a scale of 120 mm: design), its nozzle at its foot (attached on a flask's mouth as a balloon is, capped, or joined
  to another syringe's nozzle by a tube), its plunger at the top of what it holds, a piston of a ground glass's
  friction and drag (design, from how a free plunger settles). Read: its volume (`ml`, by its scale), its pressure
  (`kpa`).
- **The handle, a physical control**: the plunger's handle is dragged along the barrel's axis on the bench (no
  panel slider): while the pointer holds it the hand pulls the plunger toward the pointer, limited to its force; let
  go, the plunger is free. A command for scripts and tutorials: `plunger {item, ml}` (held toward a volume),
  `plunger {item, free: true}`. The control is general (a line on the bench, a value along it) so that other
  controls can be made physical later.
- The plunger drawn in the barrel (a slab of its own, spectral) at its height.

### Experiments (GAME_SPEC §13.7)
- `syringes`: the four setups side by side: a sealed syringe of water, one of air, two of water joined, two of air
  joined; pushed and let go, each as it does.
- `boyle` (curriculum 27): a syringe of 100 mL of air, its nozzle capped; held and pushed in, 100, 90, 80, 70, 60 mL
  (the hand's most: about 52 mL), the pressure logged against the volume: P V constant.
- `charles` (curriculum 27): a free syringe of 50 mL of air, capped, stood in a beaker of water (Stage 71's vessel in
  a vessel) heated from 20 to 80 C: the volume against the temperature, its line extrapolated to nothing at about
  -273 C.
- `molar_volume` (curriculum 28): malachite in sulfuric acid in a flask with the free syringe on its mouth: the
  carbon dioxide it gives collected; its volume at the room's temperature and pressure over its moles: about 24
  L/mol.

## Phases

| Phase | Topic | Status |
|---|---|---|
| 79.1 | The core: a piston, the hand's force, joined spaces, liquid between joined columns; tests (T1, T2, V3; Boyle's limit, the hand's most, the four setups) | |
| 79.2 | The lab: the gas syringe, its plunger, attached, capped and joined, saved | |
| 79.3 | The screen: the handle a physical control (dragged), the plunger drawn | |
| 79.4 | The experiments `syringes`, `boyle`, `charles`, `molar_volume` | |
| 79.5 | GAME_SPEC, CHANGELOG, `make gates`, the user's sign-off | |

## Tests

(with each phase)

## Open

(to be found)
