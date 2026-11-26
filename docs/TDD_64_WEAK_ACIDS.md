# TDD 64: Weak acids, bases and indicators (Stage 64)

## The request

The curriculum's step D, its second stage (`docs/CURRICULUM.md`: C1's cap and first pack, C2), after Stage 63's
sign-off: the user's "Approved, merge and proceed" (2026-11-26), and the user's plan for step D (2026-11-26): the
core in ions (Stage 63), then **new elements, weak acids and bases and indicators** (this stage), then experiments
14 to 20 and conductivity (Stage 65).

## What is there now (a survey, 2026-11-26)

- Nine elements under a cap of 16; eight ions (H+, OH-, Cu2+, SO4 2-, HSO4-, Na+, Ni2+, CuOH+), sixteen particles at
  most. A retired formula unit is read back by each element's master ion (one atom of the element with oxygen alone),
  which cannot tell acetate's carbon from carbonate's.
- No weak acid but the bisulfate, no weak base, no gas that dissolves, no indicator.
- A bench sets its formula units as their ions at the stated temperature, and the first step's fast equilibria move
  its heat: the demo's acid, 20.000 C as loaded, was 17.44 C a second later (found by the peer session merging Stage
  63 into 67: the bisulfate forming, about 22 kJ/mol, from 0.19 mol).
- The ions' heat capacities are NBS's shifted by 216.35 J/K per unit of charge (Stage 63 split the old formula units,
  whose sulfuric acid had a stand-in heat capacity), H+ left at nought: every neutral salt's is NBS's, but a reaction
  that makes or uses H+ has 216.35 J/K too much (water's ionisation -7.5 against the measured -223.8).

## The design

- **Bases.** A dissolved species states its masters (`basis` in species.json: HCO3- is CO3 2- and H+; NH3 is NH4+ less
  H+; OH- is H+ given up), whole waters making up the rest; the table checks the sum. A species with none is a
  master of its own. `formula_units` reads a retired unit by them: each master's amount held, H+ for the protons
  (an acid in a base holds none), the fewest units its own masters make. This replaces Stage 63's element masters.
- **Caps**: 32 elements, 64 particles. **New elements**: Cl, K, P and Br (CIAAW's abridged weights).
- **New species**, every row cited (`docs/DATA_SOURCES.md`, "Weak acids and bases (Stage 64)"):
  - ions: Cl-, K+, NH4+, acetate, CO3 2-, HCO3-, PO4 3-, HPO4 2-, H2PO4-; neutral solutes: NH3, acetic acid, CO2,
    H3PO4; the gas NH3.
  - ΔfH and S° are NBS's rows (one table with Stage 63's Na+, OH-, SO4 2-); PO4 3-'s S° is the one NBS's own ΔfG
    implies (its printed −222 put pKa3 0.09 off). Heat capacities and volumes are OBIGT's (Shock and Helgeson's
    HKF values at 25 C; NBS prints none for most), shifted to the model's footing by the charge: −216.35 J/K and
    −16.74 cm³ a unit of charge.
  - retired units for the salts and acids a bench or a calculation names: HCl, NaCl, KCl, NH4Cl, KOH, sodium acetate,
    Na2CO3, NaHCO3, the sodium and potassium phosphates; each unit's row its ions' sum.
- **H+ on the ions' footing**: its heat capacity −216.35 J/K mol, the shift every other ion has; the bisulfate's and
  CuOH+'s moved with it to keep their reactions' own. Water's ionisation then has NBS's −223.8 (it had −7.5), so the
  tables' own pKw follows IAPWS (Kw at 60 C 13.05 against 13.02), and each weak acid's constant moves with
  temperature by its own heat capacity. Sulfuric acid's unit reads −293.8 J/K mol, NBS's −293, where it had the pure
  liquid acid's 138.9 as a stand-in.
- **Reactions**, each a fast equilibrium: acetic acid's, ammonium's, carbonic acid's two, phosphoric acid's three;
  CO2 and NH3 between gas and solution (Henry's law). Each is written with whichever of H+ and OH- is plentiful near
  its pKa (below 7 the acid gives H+; above, the base takes OH-): in a solution at pH 10 there are a few hundred quanta
  of H+ in a beaker, and an indicator's or ammonium's balance read from them would be coarse.
- **The gases**: CO2(g) ⇌ CO2(aq) and NH3(g) ⇌ NH3(aq) by the tables' Gibbs energies, nothing new in the solver: a
  layer with no gas makes a bubble only when what is dissolved exceeds what 1 atm of it holds (a gas absent from a
  cell is taken as forming pure, at the cell's pressure), so 0.1 mol/kg of CO2 gives off a bubble until 0.034 is left.
  An open vessel's headspace trades with the room at its mouth (Stage 50): ammonia and carbon dioxide leave a
  solution slowly, as they do.
- **The activity model**: Pitzer and Mayorga 1973's pairs for the chlorides, the hydroxide, sulfates, acetates and
  phosphates (their 2:1 and 3:1 values unscaled to ion pairs); Harvie, Møller and Weare 1984's for the carbonates;
  pitzer.dat's K+–HSO4-. Neutral solutes are ideal (no λ). No mixing terms (θ, ψ): at a buffer's strength they are
  below the gates' tolerance.
- **Diffusion**: the CRC Handbook's (Vanýsek) limiting values for the new ions, PHREEQC's for dissolved CO2 and NH3;
  a neutral solute's particle is named by its species' id so it can be given one.
- **A bench opens at its fast equilibria at the temperatures it states** (`build_settled`): built, settled, its
  settled amounts set again at the same temperatures, until settling moves no temperature by 1e-6 K. Saves are
  loaded as saved.
- **The joint solver takes a dissolved trace** (Stage 63 left out a reaction with a species under 64 quanta): in a
  carbonate at pH 10 the carbonic acid's balance draws its hydrogen ions through water's ionisation, a few hundred
  quanta a round, where it needs millions; together they move at once. A species that falls toward nothing during the
  solve still takes its reactions out of it.

## Phases

| Phase | Topic | Status |
|---|---|---|
| 64.1 | Bases, the caps, the elements; a bench at its equilibria | done |
| 64.2 | The weak acids and bases, the gases, their activity and diffusion; the gates | in progress |
| 64.3 | The solids: salts weighed in experiments 14 to 20, their solubilities | |
| 64.4 | Phthalate and KHP | |
| 64.5 | The indicators: their equilibria and spectra | |
| 64.6 | The lab: benches, supplies, readings, the calculator | |
| 64.7 | The goldens: the evidence and the user's approval | |
| 64.8 | This document, GAME_SPEC, CHANGELOG, `make gates`, the user's sign-off | |

## Open

- Ammonia's and carbon dioxide's volatility is the tables' (Henry's law at equilibrium in each cell); a real
  solution's surface exchanges at a finite rate, so an open beaker of ammonia loses it faster here than it would.
- A neutral solute's activity coefficient is one (Pitzer's λ left out): salting out is not modelled.
