#pragma once

#include "lab/lab.hpp"
#include "lab/quantity.hpp"

#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// What can be watched in the lab, and how (GAME_SPEC §13.3): a quantity of an
// item measured from the lab's view, a comparison of it with one or two
// values, and conditions built of them. Signals, the tutorial and goals read
// the lab through these.
namespace lab {

// The quantity now; none when the item is not in the view, or the quantity is
// not one of the item's. A species the item does not hold measures 0.
std::optional<double> measure(const LabView &view, const Quantity &q);
// The quantities an item offers a monitor, in the editor's order: its
// temperature, gas, solids and liquids, liquid (a balloon then its fill);
// then each species it holds (the masses); a burner its valve alone.
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

const char *op_name(Comparison::Op op); // "above", "below", "at least", ...
bool op_of(std::string_view name, Comparison::Op &op); // "above", "below", "at_least", "at_most", "reaches", "between", "outside"
bool two_values(Comparison::Op op);
// finite values, and a <= b for between and outside
bool valid(const Comparison &c, std::string &why);
// Whether `now` meets it. Reaches needs the reading before: it holds when the
// value lies between the two readings, ends included.
bool meets(const Comparison &c, double now, std::optional<double> before);

// A species in an item: its grams, and its share as `purity` measures it (a fraction).
double grams(const ItemView &v, const std::string &species);
double purity(const ItemView &v, const std::string &species);
const SubstanceView *substance(const ItemView &v, const std::string &species);
const ItemView *item_of(const LabView &view, const std::string &id);

// What a condition reads: the lab's view, and what only the screen knows.
struct CondContext {
	const LabView &view;
	double speed = 1.0;
	bool broken = false; // the lab stopped on an invariant
	std::string selected; // the item selected, "" for none
	const void *host = nullptr; // what a reader's own keys read (Cond::extra)
};

// A signal part, a wire or a rig in the view (§13.3), as a condition names
// them: one that every key given matches. A part of a wire or a rig is named
// by its id or its kind ("monitor1", "monitor").
struct PartPattern {
	std::string id, kind, item, what, measure; // item, what, measure: a monitor's quantity
	std::optional<Comparison::Op> op;
	std::optional<double> value_at_least, value_at_most;
	std::optional<bool> watching, ringing, rang;
};
struct WirePattern {
	std::string from, to;
};
struct RigPattern {
	std::string from, item, control;
	std::optional<double> on_at_least, on_at_most, off_at_least, off_at_most;
	std::optional<bool> on_leaves, off_leaves, change;
};

// A condition on the lab (GAME_SPEC §13.2): every key it gives holds. Written
// as JSON in tutorials and benches; read by `read_cond` (src/conditions_json.hpp).
struct Cond {
	std::string item;
	std::optional<std::string> place, on, under, drains_to, selected;
	std::optional<bool> submitted, stirred, burst, conserved, broken;
	std::optional<double> valve_at_least, valve_at_most, above_c, below_c, liquid_above_ml, liquid_below_ml, above_g, below_g, purity_above, speed_at_least;
	std::string species;
	std::optional<std::vector<std::string>> holds_only;
	// any quantity of the item, compared: {"quantity": {"what", "measure"}, "compare", "value" | "values"}
	std::optional<Quantity> quantity;
	Comparison comparison;
	// signals (§13.3)
	std::optional<PartPattern> part;
	std::optional<WirePattern> wire;
	std::optional<RigPattern> rig;
	std::vector<Cond> all, any, nots;
	// a reader's own keys, each a test of the context
	std::vector<std::function<bool(const CondContext &)>> extra;
};

bool holds(const Cond &c, const CondContext &x);
// the measured value a condition waits on, for the player to watch: "3.214 g"; "" for none
std::string now_of(const Cond &c, const CondContext &x);

} // namespace lab
