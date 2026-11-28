# TDD 87: Rates in solution, and catalysts (Stage 87)

## The request

After Stage 86 the user approved "proceed" to the next curriculum step, F (`docs/CURRICULUM.md`): solution kinetics
and redox, C4 and C5. Step F is three units of work (rates in concentrations and catalysts; redox in solution; metals
and displacement); this stage is the first: GAME_SPEC §7's planned rate law made real, with the experiments it
completes, 37, 39 and 40. GAME_SPEC 0.87.0.

| Phase | Topic | Status |
|---|---|---|
| 87.1 | Spec (§7); the sources read and their rows verified | Done |
| 87.2 | The solution law and catalysts in the core, tests green | Done |
| 87.3 | Crystal violet, hydrogen peroxide, iodide: data and gates | Done |
| 87.4 | The experiments `crystal_violet` (37, 39) and `peroxide` (40) | Done |
| 87.5 | The sessions' digests (the user's approval), docs, `make gates`; the user's sign-off | gates passed; sign-off open |

## 87.1 What there was

Two rated laws: `FirstOrder`, with one reactant's amount (all solid decompositions), and `Surface`, a solid's area
times one dissolved driver's molality to a power. No rate in concentrations, no catalyst (a zero coefficient is
refused, a driver must be a reactant). GAME_SPEC §7 already stated the planned form, `r = k(T) · Π a_j^{n_j} · f(A)`,
catalysts changing `k`.

## 87.2 The solution law (GAME_SPEC §7)

`"law": "solution"`, `Kinetics::Law::Solution`: in moles of the reaction per kg of the cell's solvent per second,
`r = k(T) · Π m_j^{n_j} · (Π γ_j^{n_j} / γ‡) · f(A)` over `orders` (one to four dissolved species, each ≥ 0).

- **Catalysts**: an order may name any dissolved species, one in none of the reaction's terms among them. It changes
  the rate; the equilibrium is the reaction's own (`f(A)`), and no step moves the catalyst.
- **The salt effect**: Brønsted and Bjerrum's activity factor, each `γ_j` the activity model's, `γ‡` a particle of the
  ordered species' summed charge with the model's Debye–Hückel part alone (`rate_activity`, `thermo.cpp`). In dilute
  solution it is the limiting law, `ln(k/k₀) = 2A z_A z_B √I`: held at 1e-4 ionic strength within 3 % for +1 with −1,
  +2 with +2, and an ion with a neutral species.
- **A step** (`solution_extent`, `sim.cpp`): exact in its first reactant with an order, the others and the activities
  held (order 1 an exponential; order n its closed form, nought where it runs out); backwards, the rate at the step's
  start; then the limiter and `reach()` as every rated law (T1, T2, D3 hold as they do).
- **Cost**: a reaction whose ordered species is absent is skipped before any probe.
- **Refused** (`Reaction::make`, `chem_datagen`): no order, a species that is not dissolved or is a formula unit, a
  negative or non-finite order, a species twice, a step's species not a reactant with an order, a prefactor not
  positive, a negative activation energy (ctest `datagen_refuses_solution_order_not_dissolved`).
- Read by the debug panel (what its rate goes with, what catalyses it) and the reference card.

Tests (`native/tests/test_solution_kinetics.cpp`): the rate's form; a step's extent its closed form at orders 1, 2 and
½, a rate that would finish a million times over; copper in excess ammonia (the ammine given this law) at its
pseudo-first-order closed form within 3e-3 over 100 s, the invariants checked every 10 s; a catalyst unchanged, the reaction
faster, the same equilibrium within 1e-6; twice the solution the same molality within 1e-9; the refusals; the salt
effect's limiting law.

## 87.3 The chemistry (`docs/DATA_SOURCES.md`, Solution kinetics (Stage 87))

- **Crystal violet**, CV⁺ + OH⁻ → CVOH, first order in each: k₀ calibrated so the model gives Begum et al. 2011's
  0.234 /M/s at 25 C and 7.4 mM hydroxide; Ea 60.3 kJ/mol fitted to Thompson and Ting 2004's three temperatures (the
  model's ratios 21.0 and 3.27 against their 21.2 and 3.47). The carbinol's S° gives Ritchie's pK_R+ 9.39. Its colour:
  PhotochemCAD's shape scaled to Begum's 93,587 at 592 nm (OMLC's file is mis-scaled).
- **Hydrogen peroxide with iodide**, 2 H₂O₂ → 2 H₂O + O₂(g), first order in the peroxide and in the iodide, a
  catalyst: k₁ 1.15e-2 /M/s and Ea 54 kJ/mol (Liebhafsky, as Levanov et al.'s manual tabulates it), 7.93e-3 at 20 C
  against Wong and Zhang's 7.86e-3. None without iodide.
- **Iodine** (element 26, after Stage 82's lithium and strontium), I⁻, KI, H₂O₂(aq) (NBS rows; Krumgalz's volume; Pitzer and Mayorga's KI and NaI; the CRC's and
  van Stroe-Biezen's diffusion), crystal violet's cation, chloride and carbinol.

Gates: `measured: crystal violet fades at Begum et al.'s rate...`, `measured: iodide catalyses hydrogen peroxide's
decomposition at Liebhafsky's rate...`, `measured: crystal violet's absorption is the measured spectrum's shape...`,
`measured: iodide's and hydrogen peroxide's rows are NBS's...`.

## 87.4 The experiments

- **`crystal_violet`** (experiments 37 and 39): two spectrophotometers at 590 nm, their holders at 25 C; 9 µM dye in
  0.030 and 0.015 M hydroxide, the weaker made up with sodium chloride to the same ionic strength. Played: ln A
  straight (R² 0.99999), k′ 0.00592 and 0.00292 /s, the order in hydroxide 1.02, k₂ 0.197 /M/s (under Begum's 0.234 by
  the salt effect at 0.03).
- **`peroxide`** (experiment 40): 10 mL of 3 % hydrogen peroxide in three flasks, 4 mL of 0.5 M, 0.25 M potassium iodide
  and water poured in, a gas syringe on each (Stage 79's). Played: the initial rates (the minute from when each plunger
  moves: ground to its barrel, it sticks a few seconds first) 0.300 and 0.143 mL/s, the order in iodide 1.06; the water's
  syringe never moves.

Each has the three tests of `test_experiments.cpp` (strict with its gestures, as a person, free).

## 87.5 The sessions and the gates

Both sessions' digests move with the table (a new element and six species): every printed result unchanged against
main's, line for line, but one line, the conservation table's new iodine row (`I 0 -> 0 quanta of atoms`): at c9ff920,
and again after main's Stage 82 was merged in, at 722b8cd. Re-recorded with the user's approval.

`make gates`: every gate passed (1,114 s of wall at a load from other sessions' tests). The step's cost on the demo:
1,271,044 instructions a step against main's 1,246,660 at 722b8cd (`chem_tool bench`, both builds side by side):
+2.0 %, the table's six species more (the two reactions themselves are skipped while their species are absent).

## Open

- Not modelled: crystal violet's own slow hydration in neutral water (Ritchie 1975, 1.94e-5 /s), peroxide's acid term
  (I⁻ + H⁺ + H₂O₂, 15 % of k₁ at pH 2), the iodine that real iodide-peroxide solutions turn faintly yellow with.
- The salt effect's own measurement for crystal violet (Turgeon and LaMer 1952) was not read; the model's is the
  activity model's.
- Step F's rest: redox in solution (permanganate, iodine and thiosulfate, persulfate: experiments 38 and 47), and
  metals and displacement (46, 11), each a stage of its own, numbered when taken. Experiment 41, the vanishing cross,
  needs fine particles' scattering in the optics.
