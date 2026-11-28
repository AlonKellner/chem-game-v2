#pragma once
// Discharge lamps (GAME_SPEC §12.5, Stage 85): a gas's lines in the visible, NIST's, at the relative intensities its
// sources observed (data/lamps.json). Pure.
#include <array>
#include <string>
#include <utility>
#include <vector>

namespace lab::lamps {

// a gas data/lamps.json has lines for ("H", "He", "Hg", "Ne")
bool has(const std::string &gas);
// the gases, in the file's order
std::vector<std::string> gases();
// a lamp's light at `power` (0 to 1) at each nanometre from 380 to 780, as a multiple of the room's light there, per
// pixel of path through its tube; each line's light on the two nanometres beside it, its light and centre kept
std::array<double, 401> glow(const std::string &gas, double power);
// where it spikes, [from, to] nm, the brightest `most`: the pass cuts its bands there (as a flame's, flame::spikes)
std::vector<std::pair<double, double>> spikes(const std::string &gas, int most = 8);
// its lines, [nm, relative intensity]
const std::vector<std::array<double, 2>> &lines(const std::string &gas);

} // namespace lab::lamps
