#include "lab/conditions.hpp"

#include "chem/species.hpp"

#include <cmath>
#include <cstdio>

namespace lab {

namespace {

const ItemView *item_of(const LabView &view, const std::string &id) {
	for (const ItemView &v : view.items) {
		if (v.id == id) {
			return &v;
		}
	}
	return nullptr;
}

bool fixed(const std::string &what) {
	return what == "celsius" || what == "gas_ml" || what == "condensed_ml" || what == "liquid_ml" || what == "valve";
}

std::string fmt(const char *format, double v) {
	char buffer[64];
	std::snprintf(buffer, sizeof buffer, format, v);
	return buffer;
}

std::string grouped(double v) {
	const auto n = static_cast<int64_t>(std::llround(v));
	std::string digits = std::to_string(n < 0 ? -n : n), out;
	for (size_t i = 0; i < digits.size(); ++i) {
		if (i > 0 && (digits.size() - i) % 3 == 0) {
			out += ',';
		}
		out += digits[i];
	}
	return n < 0 ? "-" + out : out;
}

// "H2O(l)": as the readout names a species
std::string species_name(const std::string &id) {
	const chem::SpeciesTable &table = chem::builtin_species();
	const chem::SpeciesId s = table.find(id);
	if (!table.has(s)) {
		return id;
	}
	const chem::SpeciesDef &def = table.species(s);
	const char *phase = def.phase == chem::Phase::Gas ? "g" : def.phase == chem::Phase::Liquid ? "l" : def.phase == chem::Phase::Aqueous ? "aq" : "s";
	return std::string(def.formula) + "(" + phase + ")";
}

} // namespace

std::optional<double> measure(const LabView &view, const Quantity &q) {
	const ItemView *v = item_of(view, q.item);
	if (v == nullptr) {
		return std::nullopt;
	}
	if (q.what == "valve") {
		return v->kind == "burner" ? std::optional<double>(100.0 * v->valve) : std::nullopt;
	}
	if (v->kind == "burner" || v->kind == "tray") {
		return std::nullopt;
	}
	if (q.what == "celsius") {
		return v->celsius;
	}
	if (q.what == "gas_ml") {
		return v->gas_ml;
	}
	if (q.what == "condensed_ml") {
		return v->condensed_ml;
	}
	if (q.what == "liquid_ml") {
		return v->liquid_ml;
	}
	double g = 0.0, count = 0.0, size = 0.0;
	for (const SubstanceView &s : v->contents) {
		if (s.species == q.what) {
			g += s.g;
			count += static_cast<double>(s.particles);
			size = s.size_um;
		}
	}
	return q.measure == "count" ? count : q.measure == "size" ? size : g;
}

std::vector<Quantity> quantities_of(const ItemView &item) {
	std::vector<Quantity> out;
	if (item.kind == "burner") {
		out.push_back(Quantity{ item.id, "valve", "g" });
		return out;
	}
	for (const char *what : { "celsius", "gas_ml", "condensed_ml", "liquid_ml" }) {
		out.push_back(Quantity{ item.id, what, "g" });
	}
	for (const SubstanceView &s : item.contents) {
		if (!s.wall) {
			out.push_back(Quantity{ item.id, s.species, "g" });
		}
	}
	return out;
}

std::string unit(const Quantity &q) {
	if (q.what == "celsius") {
		return "C";
	}
	if (q.what == "valve") {
		return "%";
	}
	if (fixed(q.what)) {
		return "mL";
	}
	return q.measure == "count" ? "" : q.measure == "size" ? "um" : "g";
}

std::string label(const LabView &, const Quantity &q) {
	std::string what;
	if (q.what == "celsius") {
		what = "temperature";
	} else if (q.what == "gas_ml") {
		what = "gas";
	} else if (q.what == "condensed_ml") {
		what = "solids and liquids";
	} else if (q.what == "liquid_ml") {
		what = "liquid";
	} else if (q.what == "valve") {
		what = "valve";
	} else {
		what = species_name(q.what) + (q.measure == "count" ? " particles" : q.measure == "size" ? " particle size" : "");
	}
	const std::string u = unit(q);
	return q.item + ": " + what + (u.empty() ? "" : " (" + u + ")");
}

std::string shown(const Quantity &q, double value) {
	if (q.what == "celsius" || q.what == "valve") {
		return fmt("%.1f ", value) + unit(q);
	}
	if (fixed(q.what)) {
		return fmt("%.2f mL", value);
	}
	if (q.measure == "count") {
		return grouped(value);
	}
	if (q.measure == "size") {
		return fmt("%.1f um", value);
	}
	return fmt("%.3f g", value);
}

void scales(const Quantity &q, double out[3]) {
	const double g[3] = { 1.0, 0.1, 0.01 }, c[3] = { 10.0, 1.0, 0.1 }, n[3] = { 10000.0, 1000.0, 100.0 };
	const double *pick = q.what == "celsius" || q.what == "valve" || fixed(q.what) || q.measure == "size" ? c : q.measure == "count" ? n : g;
	for (int i = 0; i < 3; ++i) {
		out[i] = pick[i];
	}
}

bool valid(const Quantity &q, std::string &why) {
	if (fixed(q.what)) {
		return true;
	}
	const chem::SpeciesTable &table = chem::builtin_species();
	if (!table.has(table.find(q.what))) {
		why = "no quantity or species \"" + q.what + "\"";
		return false;
	}
	if (q.measure != "g" && q.measure != "count" && q.measure != "size") {
		why = "a species is measured as \"g\", \"count\" or \"size\"";
		return false;
	}
	return true;
}

const char *op_name(Comparison::Op op) {
	switch (op) {
		case Comparison::Above:
			return "above";
		case Comparison::Below:
			return "below";
		case Comparison::AtLeast:
			return "at least";
		case Comparison::AtMost:
			return "at most";
		case Comparison::Reaches:
			return "reaches";
		case Comparison::Between:
			return "between";
		case Comparison::Outside:
			return "outside";
	}
	return "?";
}

bool op_of(std::string_view name, Comparison::Op &op) {
	static constexpr std::pair<std::string_view, Comparison::Op> NAMES[] = { { "above", Comparison::Above }, { "below", Comparison::Below }, { "at_least", Comparison::AtLeast },
		{ "at_most", Comparison::AtMost }, { "reaches", Comparison::Reaches }, { "between", Comparison::Between }, { "outside", Comparison::Outside } };
	for (const auto &[n, o] : NAMES) {
		if (n == name) {
			op = o;
			return true;
		}
	}
	return false;
}

bool two_values(Comparison::Op op) {
	return op == Comparison::Between || op == Comparison::Outside;
}

bool valid(const Comparison &c, std::string &why) {
	if (!std::isfinite(c.a) || (two_values(c.op) && !std::isfinite(c.b))) {
		why = "a value is not a number";
		return false;
	}
	if (two_values(c.op) && c.a > c.b) {
		why = std::string(op_name(c.op)) + ": the low end is above the high end";
		return false;
	}
	return true;
}

bool meets(const Comparison &c, double now, std::optional<double> before) {
	switch (c.op) {
		case Comparison::Above:
			return now > c.a;
		case Comparison::Below:
			return now < c.a;
		case Comparison::AtLeast:
			return now >= c.a;
		case Comparison::AtMost:
			return now <= c.a;
		case Comparison::Reaches: {
			const double from = before ? *before : now;
			return (from <= c.a && c.a <= now) || (now <= c.a && c.a <= from);
		}
		case Comparison::Between:
			return now >= c.a && now <= c.b;
		case Comparison::Outside:
			return now < c.a || now > c.b;
	}
	return false;
}

} // namespace lab
