# TDD 21: Layers of refraction (Stage 21)

## The request

> I want to support at least two layers, one for placed equipment and one for equipment held by the
> player, which should be displayed in front of the placed equipment layer. There could be more
> layers potentially for things like containers for containers (e.g baths), even up to 6.

> … a future stage for the player held equipment layer, which should be visualized in front of the
> grid placed equipment, including refraction, spectral effects and everything.

And, asked whether it was specified as a full spectral layer that does both refraction and every
other spectral effect: "that's my expectation". The plan was approved on 2026-11-20.

GAME_SPEC §12.3, §18 (O4 to O8), §20.

| Phase | Topic | Status |
|---|---|---|
| 21.1 | The spikes: a stack traced into one map; the chain of single maps against both bodies traced together | Done: see below |
| 21.2 | The spec: §12.3 (one world in pixels, the chain, glow as a light), §20 | Done |
| 21.3 | The scene: a body's layer, the wall and the gap in pixels, a map traced for its layer's distance | Pending |
| 21.4 | The truth: its own tracer through every body of the one world | Pending |
| 21.5 | The chain in the reference: the general reading rewritten; every pixel against the truth; O5 to O8 through two layers and six | Pending |
| 21.6 | A hot solid's glow as a light in the pass | Pending |
| 21.7 | The screen: a dragged item, and a carried selection, are the hand's layer; a person-paced test | Pending |
| 21.8 | The GPU host; O4 on every picture, one with a vessel held over another; the cost with nothing held and with a vessel held | Pending |
| 21.9 | Pictures; the user judges by eye and signs off | Pending |

## The spikes (2026-11-20)

**A stack traced into one map is too slow.** The spec of Stage 20 foresaw a map for each stack of
bodies, traced again when the stack moves. Measured on the CPU, a held 50 mL vessel 120 px in
front of a standing 250 mL one, both cores:

| The held vessel's axis from the standing one's | A map | Rays | Pieces in wavelength |
|---|---|---|---|
| 0 px | 7.6 ms | 19,361 | 8 |
| 20 px | 18.7 ms | 50,827 | 16 |
| 37 px | 12.4 ms | 35,937 | 16 |
| 60 px | 13.3 ms | 44,298 | 16 |
| 200 px (past it) | 1.2 ms | 5,720 | 2 |
| one vessel alone | 0.5 ms | 2,650 | 1 |

The pieces are the cause: the image of the standing vessel's rims moves with the wavelength
through the held one, and a map that holds both must follow that edge piece by piece. The map
would be traced every frame the hand moves.

**The chain is exact, and traces nothing.** A round vessel is the same from every side: a level
ray leaves it as it came, turned about the axis by an angle that depends only on its distance
from the axis. So one vessel's map answers a ray from anywhere. 3,779 rays through the held vessel
and then the standing one, at 20 offsets: the chain of the two answers against both traced
together lands within 2.1e-9 px, and passes the same light within 1.7e-13.

## Decisions

- **The chain, in the pixel pass**, not a map per stack: approved by the user. Maps stay as they
  are, traced once; a rim's image moving with the wavelength is found by arithmetic at each step.
- **One world in pixels.** The wall 284 px behind the bench's plane for every body (it was 20 cm
  at each body's own scale: 375 px for a 50 mL vessel, 259 px for the 500 mL one); the hand's
  plane 120 px in front. Both calibrated, for the user's eye.
- **Glow as a light in the pass**, in this stage: approved by the user.
- **Declared, not built:** a neighbour on a ray's own layer; a body that is not round in a chain
  (read as seen straight on); a vessel inside another.
