#pragma once
// A flame's light from what is in it (GAME_SPEC §12, Stage 82): pure. Each element in a flame emits its neutral
// atom's lines (NIST's), each by Boltzmann's population of its upper level at the flame's temperature, and the
// alkaline earths and copper their monohydroxides' bands (Meyerriecks and Kosanke's); the whole a multiple of the
// room's light at each nanometre per pixel of path through the flame, as an emitter's is (data/flame.json).

#include <array>
#include <string>
#include <utility>
#include <vector>

namespace lab::flame {

inline constexpr int NM = 401; // 380 to 780 nm at 1 nm

// what an element's light is made of at a temperature: each line's (or band's) centre (nm), its width (nm: 0 a line,
// as narrow as the flame makes it), and its share of the element's light (all of them summing to 1)
struct Part {
	double nm = 0.0, fwhm = 0.0, share = 0.0;
};
std::vector<Part> parts(const std::string &element, double kelvin);
// how bright an element's atoms are at a temperature against sodium's at 1927 C (Boltzmann's sum over its lines over
// its ground level's g), times what its bands add
double brightness(const std::string &element, double kelvin);
// the elements a flame shows
bool shows(const std::string &element);
// the light of what is in a flame (each element's mol) at its temperature: at each nanometre from 380, a multiple of
// the room's light there per pixel of path; each line's light put on the two nanometres about its centre in shares
// that keep its whole and its centre (a row read straight between its nanometres integrates to it exactly)
std::array<double, NM> glow(const std::vector<std::pair<std::string, double>> &elements_mol, double kelvin);
// how long a drop's metal lasts in a flame (s, its 1/e time) and what a loop carries of a solution (mL)
double fade_s();
double drop_ml();

} // namespace lab::flame
