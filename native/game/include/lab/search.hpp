#pragma once
// Search in Supplies and in Storage (GAME_SPEC §13.1, Stage 61), pure: an item's keywords, and whether a query
// matches them. A query's words, in any order and any case, each the start of one keyword.
#include "lab/lab.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace lab::search {

// What an item is called by (lowercase, each word of a name apart and whole):
// - what it is: its id and label; its kind (vessel, burner, balloon, stopper, balance, thermometer, camera, viewer,
//   spectrophotometer); its glassware ("conical flask") and its capacity ("250", "250ml");
// - what it holds: each species' id and its words, its formula, its phase (solid, liquid, gas, dissolved, solution);
//   "empty" when it holds nothing but air;
// - what it is made of: its wall's species (glass, latex, polystyrene foam);
// - what it is for: the goal on its slot (Storage), and the slot's number;
// - its state: hot (over 40 C), cold (under 10 C), boiling, sealed, stoppered, broken.
std::vector<std::string> keywords(const ItemView &item, std::string_view goal = {});

// (Stage 78) a text's keywords, as a name's are: lowercase, the whole and each of its words apart (an experiment's id,
// title and summary, for the experiments' list, GAME_SPEC §13.7)
std::vector<std::string> words(std::string_view text);

// true when every word of the query starts one of the keywords; an empty query matches everything
bool matches(std::string_view query, const std::vector<std::string> &keywords);

} // namespace lab::search
