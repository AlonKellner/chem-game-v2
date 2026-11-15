#pragma once

#include <string>

// The engine-free lab layer (GAME_SPEC §13): the bench, the equipment, the
// player's events, the reports. Stage 0: the build's identity alone.
namespace lab {

// "chem-game-v2 <GAME_SPEC version>"
std::string build_name();

} // namespace lab
