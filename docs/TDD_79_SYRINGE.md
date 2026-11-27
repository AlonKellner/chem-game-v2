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
  friction (0.3 N) and drag (70 N s/m: a free plunger settles in about 0.1 s). Read: its scale (`plunger_ml`), its
  pressure (`kpa`), the hand's force (`hand_n`), its lock (`locked`).
- **Its lock** (found as the tutorials were written: a reading of Boyle's law is taken with the hand off the plunger,
  which a free plunger would not allow): `plunger {item, lock}`, the plunger held where the hand leaves it by its
  clamp, up to 300 N.
- **The handle, a physical control**: the plunger's handle is dragged along the barrel's axis on the bench (no
  panel slider): while the pointer holds it the hand pulls the plunger toward the pointer, limited to its force; let
  go, the plunger is free. A command for scripts and tutorials: `plunger {item, ml}` (held toward a volume),
  `plunger {item, free: true}`. The control is general (a line on the bench, a value along it) so that other
  controls can be made physical later.
- The plunger drawn in the barrel (a slab of its own, spectral) at its height.

### Experiments (GAME_SPEC §13.7)
- `syringes`: the four setups side by side: a sealed syringe of water, one of air, two of water joined, two of air
  joined; pushed and let go, each as it does.
- `boyle` (curriculum 27): a syringe of 100 mL of air, its nozzle capped; locked, pushed to 80 and 60 mL (the hand's
  most: about 52 mL), each read once back at the room's temperature: P V constant.
- `charles` (curriculum 27; the user, 2026-11-27: "a cool bath and a hot bath, and we can submerge the syringe in one
  or the other - and the air inside it expands or contracts, visually moving the plunger"): a free syringe of 30 mL of
  air, capped, stood in ice water and then in hot water (Stage 71's vessel in a vessel): the volume against the
  temperature, its line extrapolated to nothing at about -273 C.
- `molar_volume` (curriculum 28): malachite in sulfuric acid in a flask with the free syringe on its mouth: the
  carbon dioxide it gives collected; its volume at the room's temperature and pressure over its moles: about 24
  L/mol.

## Phases

| Phase | Topic | Status |
|---|---|---|
| 79.1 | The core: a piston, the hand's force, joined spaces, liquid between joined columns; tests (T1, T2, V3; Boyle's limit, the hand's most, the four setups) | Done |
| 79.2 | The lab: the gas syringe, its plunger, attached, capped and joined, its lock, saved | Done |
| 79.3 | The screen: the handle a physical control (dragged), the plunger drawn, the tube, the panel's join | Done |
| 79.4 | The experiments `syringes`, `boyle`, `charles`, `molar_volume` | Done |
| 79.5 | GAME_SPEC, CHANGELOG, `make gates`, the user's sign-off | |

## Tests

- `native/tests/test_syringe.cpp` (the core): Boyle's law at a slow push (P V to 1e-5), the hand's most, a sealed
  syringe of air springing back within its friction's band, a fast push at least the adiabat's temperature and at most
  the hand's work's, a free plunger's energy standing, a sealed syringe of water standing on its liquid against the
  hand both ways, two of air joined (the other short by what the air gives), two of water joined (the water moving
  across), the refusals; W1 and its self-test on a corrupted trace. Every step under every invariant.
- `native/tests/game/test_syringe_lab.cpp` (the lab and the screen): its glassware; Boyle's law and the spring back;
  the hand's most (79.8 N, the source's); an open syringe, its cap, the refusals; two of air and two of water tubed;
  one on a flask's mouth; saved and loaded, digest for digest; the lock; the handle dragged on the screen at a
  person's pace.
- `native/tests/game/test_experiments.cpp`: each of the four experiments followed with its own gestures (strict and
  free) and at a person's pace; `syringes`' four setups, `boyle`'s P V, `charles`' contraction and expansion,
  `molar_volume`'s 24 L/mol, each with its report.

## Found

- **A physical control needs the lab's time.** A strict tutorial holds its clock while a step waits for a gesture:
  a plunger dragged then moved nowhere. The clock runs while a hand works a plunger (`Ui::held`); the tests' drag of
  a plunger takes a second and a half, as a hand's does.
- **A syringe's gas faced the glass to its brim**: in a cold bath its air settled at 8 C, the glass over its plunger
  in the room's air. Its gas now faces its glass to its plunger's face (`set_walls`).
- **Friction biases Charles's law**: a plunger held by 1 N on its bore stops 1.2 % short each way, absolute zero at
  -305 C; ground gas syringes are made nearly frictionless, and 0.3 N puts it at -277 C.
- **What stands in a vessel was hit after it** when it came earlier in the bench's list: the syringe in a bath could
  not be chosen. What stands in a vessel is hit first.

## Open

- A liquid is incompressible: a sealed syringe of water moves by its bubble alone, and with none its plunger stands
  on the liquid (a billionth of its volume left to its gas). Water's compressibility (4.5e-10 /Pa) would give 0.005 mL
  at a hand's most.
- Joined spaces are at one pressure: a tube's own resistance to flow (Poiseuille's) is not modelled, nor a liquid's
  head between joined syringes.
- A syringe whose nozzle is open spills what its plunger pushes past its liquid (the overflow), and draws in air,
  never liquid: drawing a liquid into a syringe is to come.
- The plunger and its handle are slabs of the pass, lit and opaque, drawn over the barrel; the tube a hanging bar.
  Not yet traced as glass (Stage 78's traced reflection, and nested rays, after it).
- The plunger's weight (tens of grams: 0.6 kPa on its bore) is not modelled.
