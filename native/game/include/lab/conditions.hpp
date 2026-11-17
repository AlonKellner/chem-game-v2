#pragma once

#include "lab/lab.hpp"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

// What can be watched in the lab, and how (GAME_SPEC §13.3): a quantity of an
// item measured from the lab's view, and a comparison of it with one or two
// values. Timers and the tutorial read the lab through these.
namespace lab {

struct Quantity {
	std::string item;
	// celsius, gas_ml, condensed_ml, liquid_ml, valve; or a species' id
	std::string what;
	// for a species: g (its mass), count (its particles), size (their size, um)
	std::string measure = "g";

	bool operator==(const Quantity &) const = default;
};

// The quantity now; none when the item is not in the view, or the quantity is
// not one of the item's. A species the item does not hold measures 0.
std::optional<double> measure(const LabView &view, const Quantity &q);
// The quantities an item offers a timer, in the editor's order: its
// temperature, gas, solids and liquids, liquid; then each species it holds
// (the masses); a burner its valve alone.
std::vector<Quantity> quantities_of(const ItemView &item);
// "flask: temperature (C)", "flask: H2O(l) (g)"
std::string label(const LabView &view, const Quantity &q);
std::string unit(const Quantity &q);
// a value as the quantity shows it: "9.874 g", "101.2 C", "447,472"
std::string shown(const Quantity &q, double value);
// the three nudge steps for the editor, largest first
void scales(const Quantity &q, double out[3]);
// a known quantity name (one of the five, or a species of the tables) and measure
bool valid(const Quantity &q, std::string &why);

struct Comparison {
	enum Op : int32_t { Above = 0, Below = 1, AtLeast = 2, AtMost = 3, Reaches = 4, Between = 5, Outside = 6 };
	Op op = Below;
	double a = 0.0, b = 0.0; // b for between and outside

	bool operator==(const Comparison &) const = default;
};

const char *op_name(Comparison::Op op); // "above", "below", "at least", ...
bool op_of(std::string_view name, Comparison::Op &op); // "above", "below", "at_least", "at_most", "reaches", "between", "outside"
bool two_values(Comparison::Op op);
// finite values, and a <= b for between and outside
bool valid(const Comparison &c, std::string &why);
// Whether `now` meets it. Reaches needs the reading before: it holds when the
// value lies between the two readings, ends included.
bool meets(const Comparison &c, double now, std::optional<double> before);

} // namespace lab
