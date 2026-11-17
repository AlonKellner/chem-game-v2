#include "lab/conditions.hpp"

#include "chem/species.hpp"

#include <cmath>
#include <cstdio>

namespace lab {

namespace {

bool fixed(const std::string &what) {
	return what == "celsius" || what == "gas_ml" || what == "condensed_ml" || what == "liquid_ml" || what == "valve" || what == "fill";
}

bool is_gas(const std::string &species) {
	const chem::SpeciesTable &table = chem::builtin_species();
	const chem::SpeciesId s = table.find(species);
	return table.has(s) && table.species(s).phase == chem::Phase::Gas;
}

// the species' share of the item's gas, by amount
double gas_share(const ItemView &v, const std::string &species) {
	double all = 0.0, mine = 0.0;
	for (const SubstanceView &s : v.contents) {
		if (s.phase == 'g' && !s.wall) {
			all += s.mol;
			mine += s.species == species ? s.mol : 0.0;
		}
	}
	return all > 0.0 ? mine / all : 0.0;
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

const ItemView *item_of(const LabView &view, const std::string &id) {
	for (const ItemView &v : view.items) {
		if (v.id == id) {
			return &v;
		}
	}
	return nullptr;
}

double grams(const ItemView &v, const std::string &species) {
	double g = 0.0;
	for (const SubstanceView &s : v.contents) {
		if (s.species == species) {
			g += s.g;
		}
	}
	return g;
}

const SubstanceView *substance(const ItemView &v, const std::string &species) {
	for (const SubstanceView &s : v.contents) {
		if (s.species == species) {
			return &s;
		}
	}
	return nullptr;
}

double purity(const ItemView &v, const std::string &species) {
	if (is_gas(species)) {
		return gas_share(v, species);
	}
	double all = 0.0; // the solids and liquids, by mass, the walls not counted
	for (const SubstanceView &s : v.contents) {
		if (!s.wall && s.phase != 'g') {
			all += s.g;
		}
	}
	return all > 0.0 ? grams(v, species) / all : 0.0;
}

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
	if (q.what == "fill") {
		return v->kind == "balloon" && v->capacity_ml > 0.0 ? std::optional<double>(100.0 * v->gas_ml / v->capacity_ml) : std::nullopt;
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
	if (q.measure == "purity") {
		return 100.0 * purity(*v, q.what);
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
	if (item.kind == "balloon") {
		out.push_back(Quantity{ item.id, "fill", "g" });
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
	if (q.what == "valve" || q.what == "fill") {
		return "%";
	}
	if (fixed(q.what)) {
		return "mL";
	}
	return q.measure == "count" ? "" : q.measure == "size" ? "um" : q.measure == "purity" ? "%" : "g";
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
	} else if (q.what == "fill") {
		what = "fill";
	} else {
		what = species_name(q.what) + (q.measure == "count" ? " particles" : q.measure == "size" ? " particle size" : q.measure == "purity" ? " purity" : "");
	}
	const std::string u = unit(q);
	return q.item + ": " + what + (u.empty() ? "" : " (" + u + ")");
}

std::string shown(const Quantity &q, double value) {
	if (q.what == "celsius" || q.what == "valve" || q.what == "fill") {
		return fmt("%.1f ", value) + unit(q);
	}
	if (q.measure == "purity") {
		return fmt("%.3f %%", value);
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
	const double *pick = fixed(q.what) || q.measure == "size" ? c : q.measure == "count" ? n : g; // purity nudges as grams do
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
	if (q.measure != "g" && q.measure != "count" && q.measure != "size" && q.measure != "purity") {
		why = "a species is measured as \"g\", \"count\", \"size\" or \"purity\"";
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

// ------------------------------------------------------------- conditions

namespace {

const std::string &id_at(const LabView &view, int index) {
	static const std::string none;
	return index >= 0 && index < static_cast<int>(view.items.size()) ? view.items[static_cast<size_t>(index)].id : none;
}

Quantity quantity_of(const Cond &c) {
	Quantity q = *c.quantity;
	q.item = q.item.empty() ? c.item : q.item;
	return q;
}

} // namespace

bool holds(const Cond &c, const CondContext &x) {
	for (const Cond &a : c.all) {
		if (!holds(a, x)) {
			return false;
		}
	}
	if (!c.any.empty()) {
		bool one = false;
		for (const Cond &a : c.any) {
			one = one || holds(a, x);
		}
		if (!one) {
			return false;
		}
	}
	for (const Cond &n : c.nots) {
		if (holds(n, x)) {
			return false;
		}
	}
	for (const auto &test : c.extra) {
		if (!test(x)) {
			return false;
		}
	}
	if (c.conserved && *c.conserved != x.view.conserved) {
		return false;
	}
	if (c.broken && *c.broken != x.broken) {
		return false;
	}
	if (c.speed_at_least && !(x.speed >= *c.speed_at_least)) {
		return false;
	}
	if (c.selected && x.selected != *c.selected) {
		return false;
	}
	if (c.quantity) {
		const std::optional<double> v = measure(x.view, quantity_of(c));
		if (!v || !meets(c.comparison, *v, std::nullopt)) {
			return false;
		}
	}
	if (c.item.empty()) {
		return true;
	}
	const ItemView *found = item_of(x.view, c.item);
	if (found == nullptr) {
		return false;
	}
	const ItemView &v = *found;
	if ((c.place && v.place != *c.place) || (c.on && (v.place != "on" || id_at(x.view, v.on) != *c.on)) || (c.under && (v.place != "under" || id_at(x.view, v.under) != *c.under)) ||
		(c.drains_to && id_at(x.view, v.drains_to) != *c.drains_to)) {
		return false;
	}
	if ((c.submitted && v.submitted != *c.submitted) || (c.stirred && v.stirred != *c.stirred) || (c.burst && v.burst != *c.burst)) {
		return false;
	}
	if ((c.valve_at_least && !(v.valve >= *c.valve_at_least)) || (c.valve_at_most && !(v.valve <= *c.valve_at_most))) {
		return false;
	}
	if ((c.above_c && !(v.celsius > *c.above_c)) || (c.below_c && !(v.celsius < *c.below_c))) {
		return false;
	}
	if ((c.liquid_above_ml && !(v.liquid_ml > *c.liquid_above_ml)) || (c.liquid_below_ml && !(v.liquid_ml < *c.liquid_below_ml))) {
		return false;
	}
	if (!c.species.empty()) {
		const double g = grams(v, c.species);
		if ((c.above_g && !(g > *c.above_g)) || (c.below_g && !(g < *c.below_g)) || (c.purity_above && !(purity(v, c.species) > *c.purity_above))) {
			return false;
		}
	}
	if (c.holds_only) {
		for (const SubstanceView &s : v.contents) {
			if (s.wall || !(s.mol > 0.0)) {
				continue;
			}
			bool listed = false;
			for (const std::string &name : *c.holds_only) {
				listed = listed || name == s.species;
			}
			if (!listed) {
				return false;
			}
		}
	}
	return true;
}

std::string now_of(const Cond &c, const CondContext &x) {
	if (c.quantity) {
		const Quantity q = quantity_of(c);
		if (const std::optional<double> v = measure(x.view, q)) {
			return shown(q, *v);
		}
	}
	if (!c.item.empty()) {
		if (const ItemView *v = item_of(x.view, c.item)) {
			if (!c.species.empty() && c.purity_above) {
				return fmt("%.3f%%", 100.0 * purity(*v, c.species));
			}
			if (!c.species.empty() && (c.above_g || c.below_g)) {
				return fmt("%.3f g", grams(*v, c.species));
			}
			if (c.above_c || c.below_c) {
				return fmt("%.1f C", v->celsius);
			}
			if (c.liquid_above_ml || c.liquid_below_ml) {
				return fmt("%.1f mL", v->liquid_ml);
			}
			if (c.valve_at_least || c.valve_at_most) {
				return fmt("valve %.0f%%", 100.0 * v->valve);
			}
		}
	}
	if (c.speed_at_least) {
		return fmt("%.0fx", x.speed);
	}
	for (const Cond &a : c.all) {
		const std::string s = now_of(a, x);
		if (!s.empty()) {
			return s;
		}
	}
	return {};
}

} // namespace lab
