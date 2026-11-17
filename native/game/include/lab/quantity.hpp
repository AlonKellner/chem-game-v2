#pragma once

#include <cstdint>
#include <string>

// What can be watched in the lab (GAME_SPEC §13.3): a quantity of an item, and
// a comparison of it with one or two values. The functions on them are in
// lab/conditions.hpp.
namespace lab {

struct Quantity {
	std::string item;
	// celsius, gas_ml, condensed_ml, liquid_ml, valve, fill (a balloon's, % of
	// its capacity); or a species' id
	std::string what;
	// for a species: g (its mass), count (its particles), size (their size, um),
	// purity (its share, %: of the solids and liquids by mass, or of the gas by
	// amount for a gas)
	std::string measure = "g";

	bool operator==(const Quantity &) const = default;
};

struct Comparison {
	enum Op : int32_t { Above = 0, Below = 1, AtLeast = 2, AtMost = 3, Reaches = 4, Between = 5, Outside = 6 };
	Op op = Below;
	double a = 0.0, b = 0.0; // b for between and outside

	bool operator==(const Comparison &) const = default;
};

} // namespace lab
