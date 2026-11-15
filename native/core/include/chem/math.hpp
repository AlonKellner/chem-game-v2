#pragma once

// The core's own exponential and logarithm (GAME_SPEC §3). The C library's
// differ between platforms in the last bit, and the replays are bit-identical,
// so the core computes them from +, -, *, / alone (the fdlibm algorithms; under
// 1 ulp of error). Nothing under native/core may call the library's (ctest
// core_no_libm).
namespace chem {

// e^x. Overflows to +infinity above 709.78, underflows to 0 below -745.13.
double exp(double x);

// The natural logarithm. NaN for x < 0, -infinity for x == 0.
double log(double x);

} // namespace chem
