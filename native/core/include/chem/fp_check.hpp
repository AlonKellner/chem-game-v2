#pragma once

namespace chem {

// Returns a*b+c with the product rounded to double BEFORE the addition: two
// IEEE-754 roundings, never one fused rounding. The replays' bit-identity
// (GAME_SPEC §3) depends on this everywhere; chem_tests checks it on an
// FMA-sensitive triple so a toolchain that silently contracts fails loudly.
double fma_sensitive(double a, double b, double c);

} // namespace chem
