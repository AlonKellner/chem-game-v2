#pragma once

#include "lab/lab.hpp"
#include "lab/quantity.hpp"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

// The notebook (GAME_SPEC §13, Stage 54), pure: what a student reads of a quantity, a straight line through what was
// written down, an axis's transform, and a report's expression over the lab's quantities.
namespace lab {

// What is read of a quantity now: by instruments (`view.instruments`) what an instrument reads of it (a vessel's
// thermometer, its pH meter, its marks, a burette's scale, a balance's display, a sealed vessel's gauge), and none for
// what no instrument reads (a species' mass, a particle count); otherwise the truth. `instrument` says which.
std::optional<double> reading(const LabView &view, const Quantity &q, bool &instrument);

// A least-squares straight line, y = slope x + intercept, and its R^2; `ok` with two or more points of which the x
// are not all one.
struct LineFit {
	bool ok = false;
	int n = 0;
	double slope = 0.0, intercept = 0.0, r2 = 0.0;
};
LineFit fit_line(const std::vector<double> &x, const std::vector<double> &y);

// An axis as it is, its natural logarithm, or its reciprocal (a Celsius temperature's in 1/K)
enum class AxisTransform { Plain = 0, Log = 1, Inverse = 2 };
std::optional<double> transformed(double value, AxisTransform t, bool celsius);
const char *transform_name(AxisTransform t, bool celsius); // "", "ln ", "1/"

// A report's expression (Stage 54): numbers, + - * /, parentheses, unary minus, (Stage 65) log10, ln and sqrt of a
// parenthesised expression, and quantities named `item.what` or
// `item.what.measure` (a species' g, mol, ml...; a vessel's celsius, ph, liquid_ml...), each its truth now. False, with
// the reason, when it does not parse or a quantity is not there.
bool evaluate(std::string_view expression, const LabView &view, double &value, std::string &why);

} // namespace lab
