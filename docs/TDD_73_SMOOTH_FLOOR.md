# TDD 73: A floor's convection without a step (Stage 73)

## The request

Stage 72 found that a floor's natural convection steps down 7 % at a Nusselt number of 10, so a floor under half its
glass had two surfaces within the step (8 of the test's 1,728 floors). Offered as the open item after Stage 72's
merge, the user, 2026-11-27: "Very well, go ahead". GAME_SPEC 0.73.0.

| Phase | Topic | Status |
|---|---|---|
| 73.1 | The source read again: where Lienhard puts the thick layers' correction | Done |
| 73.2 | One unstable plate's form, the correction at every Nusselt number | Done |
| 73.3 | Tests: continuous and rising; one surface everywhere | Done |
| 73.4 | The goldens re-recorded (with the user's approval), the evidence | Done |
| 73.5 | `make gates`; the user's sign-off | signed off by the user, 2026-11-27: "Approved, merge and proceed" |

## 73.1 The source

Lienhard and Lienhard, *A Heat Transfer Textbook* 6.00, §8.4, p. 436 (read again 2026-11-27): for horizontal plates
at lower Rayleigh numbers, Raithby and Hollands' 8.37a over L* = A/P, then "If Nu_L* ≲ 10, the b.l.s will be thick,
and the result should be corrected as follows: Nu_corrected = 1.4 / ln(1 + 1.4/Nu_L*)" (8.37b); "These equations are
recommended for 1 < Ra_L* < 10⁷." Above, eq. 8.35 (p. 435).

The corrected form tends to 8.37a's as Nu + 0.7, not as Nu. Taken only under 10, as the code did since Stage 45
(the floor) and Stage 53 (a base colder than the air), it stepped down by 7 % there. "≲ 10" names where the
correction matters, not a switch.

## 73.2 The form

`walls::unstable_plate(ra, pr)` is the one form for an unstable horizontal plate: 8.37a with 8.37b at every
Nusselt number below Ra = 10⁷, and 8.35 above. `bubbles::convection` (a floor into its liquid) and
`walls::under_disc` (a base colder than the air) both read it. The correction is 7 % at Nu = 10, about 4 % for a
beaker's floor 4 K over its water (Nu 16.3 to 17.0, h 648 to 676 W/m² K), and 2.5 % at Ra = 10⁷.

The hand-over to 8.35 at Ra = 10⁷ steps up by 2 to 6 %. Lienhard gives no blend, and a step up keeps the floor's
balance one-rooted, so it stays (open).

## 73.3 Tests

- `walls: an unstable plate's Nusselt number rises with Ra, continuous where 8.37b's correction once stopped`: Ra from
  1 to 10⁹ in steps of 0.1 % at Pr 0.7, 3 and 7. It always rises, never moves more than 0.1 % in a step below 10⁷,
  is 1.068 of 8.37a's at Nu 10, and 1.024 to 1.029 at Ra 10⁷.
- Stage 72's 1,728 floors: two roots in none (8 before); each surface is the bisection's to 8e-10 K.
- `measured: a heated floor's natural convection is Raithby and Hollands'`: the corrected value, still within 10 %
  of Lloyd and Moran's 0.54 Ra^¼ (6 % under it).
- `measured: a ring's loss to the room`: the disc's thick-layer case as before.

## 73.4 The goldens

What the sessions print, Stage 72 against 73:

- **The demo**: the oxide gone (command 12) 1576.0 s, not 1576.1 s; the flask under 21 C (command 19) at 6407.3 s,
  not 6407.4 s; 820,076 steps, not 820,081; the crystals 10.058322 g, not 10.058307 g; the distillate 26.953004 g of
  water, not 26.952782 g. Every other line is as it was.
- **The pressure session**: 115,440 steps, not 115,441; every measured value is as it was.

Every other test passes, the experiments' results within their own tolerances. `make sessions APPROVED=1`.

## 73.5 The gates

`make gates`, at a load of 18 to 52 (other worktrees' gates): every entry passed but `lab_tests.signals.12`, a
monitor's cost a frame held to 3 ms of its thread's time, which also failed at two of Stage 72's runs under load
and passes alone. Run again from the result cache (11 entries), every gate passed.

## Open

- `signals.12` fails under a heavy load (three gates of the last five, the machine at 20 to 50); a cost held by
  CPU time still feels a crowded machine.
- The hand-over at Ra = 10⁷ (8.37 to 8.35) steps up by 2 to 6 %; a blend would want Raithby and Hollands' own
  method, which was not read.
