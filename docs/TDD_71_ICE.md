# TDD 71: Ice (Stage 71)

## The request

The curriculum's step I, its first part (`docs/CURRICULUM.md`: C8; experiments 26 and 33; the roadmap's area 4),
chosen by the user after Stage 66's sign-off (2026-11-27). The user's choices:
- **A vessel in a vessel** (over a cold bath item or a cold plate): a test tube stands in a beaker; an ice bath and a
  salt-and-ice freezing mixture are made by the student.
  - "Each ring interacts with the layers it touches, heat transfer is by layer/ring, not full vessel to full vessel."
  - "The inner vessel should change the volume integral of the outer vessel, such that each layer is displaced the
    correct amount, increasing the level the correct amount."
- **Freezing with supercooling** (over a fast equilibrium): a cooled liquid dips below its freezing point until a
  first crystal starts, then warms back to it as ice grows (the textbook freezing curve).

Other liquids (C9) and burning (C10), the rest of step I, are later stages.

## What the data say (fetched, 2026-11-27; `docs/DATA_SOURCES.md`, Ice)

- **Ice Ih** (IAPWS R10-06(2009), Feistel and Wagner 2006): melting at 101 325 Pa at 273.152519 K; at it ice's
  h −333 354.87 J/kg and s −1220.7693 J/(kg K) against the liquid's at the triple point (Table 6), cp 2096.71 J/(kg K),
  ρ 916.721 kg/m³. With the liquid's (NIST WebBook, IAPWS-95): ΔH_fus 333.43 kJ/kg, 6.0068 kJ/mol; ΔS_fus 21.99
  J/(mol K); ice's absolute entropy at its melting point 41.36 J/(mol K) (Pauling's residual entropy in).
- **Freezing points** (International Critical Tables IV, 1928, Hall and Sherrill, pp. 256, 258): CuSO₄ 0.0025 to 1.0
  mol/kg (Δt/N 3.003 to 1.715 K kg/mol), its eutectic with the pentahydrate at 0.846 mol/kg, −1.43 °C; NaCl 0.001 to
  5.2 mol/kg, its eutectic with the dihydrate at 5.20 mol/kg, −21.12 °C.
- **Growth into supercooled water**: dendrites, v = 0.228 ΔT^2.39 mm/s (Lindenmeyer and Chalmers 1966) or
  4.83e-5 ΔT^2.78 m/s (Shibkov's data as Grivet et al. 2022 fit): 0.33–1.2 mm/s at 2 K.
- **Supercooling** (heterogeneous: set by the container and the water, little by the volume or the cooling rate):
  300 µL in borosilicate tubes at 4.5 K/min, median −9.4 to −12.0 °C, stirring no matter (Wilson and Haymet 2016,
  Table 1); one sample's 10–90 % width 0.7 K (Wilson, InTech); 0.4 K colder at six times the cooling rate (Vali 2008);
  a student's 6 mL tube in −18 °C air typically to about −5 °C (Tan et al. 2016).
- **Melting**: an 18 mm ice ball in turbulent water h = 5380 W/(m² K) (Machicoane et al. 2013).
- **Optics**: ice's n + ik from 44 nm to 2 m (Warren and Brandt 2008; n 1.3110 at 550 nm, k 2.3e-9); snow's albedo
  0.96–0.98 over the visible (Grenfell, Warren and Mullen 1994).

## The design

### Ice in the tables (GAME_SPEC §4.2, §8)
- `ice`, H2O(s), appended after Stage 64's species: density 0.916721 g/cm³, cp 37.77 J/(mol K) (IAPWS at its melting
  point), and hf and s0 at 298.15 K chosen so that with water's own constant cp the model's melting at 1 atm is
  IAPWS's, 273.1525 K, with its heat of fusion, 6.0068 kJ/mol (hf −292.7774 kJ/mol, s0 44.666 J/(mol K)); the model's
  ice then has IAPWS's absolute entropy at its melting point, 41.36 J/(mol K), as a check, not a fit. Its thermal
  conductivity for a powder bed and for its own melting (a source to fetch: 71.1).
- A solution's freezing point is where ice and the solution's water have one potential: through the water's activity
  (Pitzer's, §10), nothing more. Held to ICT's freezing points for both salts (a gate test).

### Freezing and melting as a surface law (GAME_SPEC §7, §9)
- `water_freezing`: H2O(l) → H2O(s), the surface law at ice's surface, with nucleation. Ice's own attachment is fast
  (its surface step large: calibrated, not limiting); what limits a pure substance changing phase at a surface is the
  heat its change gives out or takes in, carried to or from the surface. **A new step in series, general**: a surface
  reaction that carries no dissolved species is limited by heat, `Nu · k / d · (T − T_eq) / |ΔH|` per m², `T − T_eq`
  from the affinity, `A · T / ΔH`; `Nu` Sano's form with the Prandtl number for the Schmidt number (2 at rest, the
  stirring's dissipation raising it, Stage 57), `k`, `ν`, `Pr` the liquid's (`solvent_at`). Freezing and melting the
  same law: ice in warm stirred water melts by heat transfer (checked against Machicoane's 5380 W/(m² K)), a crystal
  in a supercooled liquid grows as heat leaves it.
- **Supercooling**: ice's first crystal by a nucleation barrier, `nucleation · (kg of water) · exp(−B / ln² S)` a
  second (the classical form; a new optional `nucleation_barrier` beside the power law the salts keep), fitted to a
  median freezing at about −10 °C in a glass tube cooled at 4.5 K/min with the width Wilson measured (about 4.4 per K
  near it). Once a crystal is there it grows, the latent heat warms the liquid back to its freezing point, and the
  plateau holds while the bath draws the heat away.
- A solid lighter than its liquid **floats** (GAME_SPEC §6, the column): it rises to the top liquid layer, where the
  column now sinks every solid. Ice floats on water, with 8 % above the surface.

### A vessel in a vessel (GAME_SPEC §6, §11, §13)
- **Stand**: `stand {item, in}`: a vessel stands in another, on its floor, upright; refused unless it fits (its outside
  within the outer's inside, its foot on the outer's floor or sediment). Taken out by `move` or `take`. Saved.
- **The volume integral**: the outer's inside cross-section at a height, less the inner's outside cross-section
  there (from its foot to its rim): every volume read through it, so each layer's height span, the level, the
  surface, the overflow and what fits move by what the inner displaces. A brim-full bath spills what a tube pushes
  over.
- **Ring to layer**: each ring of the inner vessel's wall faces each layer of the outer's that its height span
  overlaps, the overlap's area by the into-water law (Churchill and Chu, Stage 53) in series with half the glass, an
  edge of its own; above the outer's liquid, its gas (into air). Its floor's base faces the outer's lowest layer.
  Every contact is a ring and a layer, never a vessel and a vessel.
- **The screen**: carried over a vessel, "stand it in" beside "pour" and "tip in". Drawn inside the outer, the inner on
  the layer nearer the eye (the pass does not trace a body inside another's liquid: Open).
- **A test tube** (`data/glassware.json`): a round-bottomed tube, 16 × 150 mm, its rings, its tolerances none.

### Ice seen (GAME_SPEC §12)
- Ice's medium: Warren and Brandt's n + ik over the visible and 8–14 µm, so it absorbs and emits as ice does (a clear
  piece in water nearly invisible: n 1.31 against 1.33). Crushed ice's whiteness (scattering at its many faces) is not
  traced: Open. A floating solid drawn at the surface, its 8 % over it.

### Experiments (GAME_SPEC §13.7)
- `heating_curve` (curriculum 26): crushed ice in a beaker on a burner, a thermometer logged in the notebook: the
  plateau at 0 °C while it melts, the rise, the boil's plateau; the heat of fusion by a calorimeter (ice into warm
  water in the foam cup).
- `freezing_point` (curriculum 33): a test tube of water, then of a solution, stood in a beaker of ice and salt, its
  thermometer logged: the dip below the freezing point, the jump back, the plateau; the depression against the
  molality, the cryoscopic constant and a solute's molar mass.

## Phases

| Phase | Topic | Status |
|---|---|---|
| 71.1 | Measured first: ice's data (IAPWS, ICT, the growth, the supercooling, the optics, ice's conductivity); `chem_tool study ice` | |
| 71.2 | The core: `ice`, `water_freezing`; heat-limited surface steps; the nucleation barrier; the gate tests | |
| 71.3 | The column: a lighter solid floats; the lab's sediment and the picture's floating solid | |
| 71.4 | Ice seen: its medium in the visible and the long wave | |
| 71.5 | A test tube; a vessel in a vessel: stand, the volume integral, ring-to-layer contacts, save and digest | |
| 71.6 | The screen: "stand it in", drawing one inside another | |
| 71.7 | The experiments `heating_curve` and `freezing_point` | |
| 71.8 | Sessions and goldens (with the user's approval); GAME_SPEC, CHANGELOG, `make gates`, the user's sign-off | |

## Tests

(to be written with each phase)

## Open

(to be found)
