#include "lab/conditions.hpp"

#include "chem/species.hpp"
#include "lab/glassware.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace lab {

namespace {

bool fixed(const std::string &what) {
	return what == "celsius" || what == "gas_ml" || what == "condensed_ml" || what == "liquid_ml" || what == "solid_ml" || what == "mass_g" || what == "kpa" || what == "valve" ||
			what == "fill" || what == "met" || what == "ph" || what == "bubbles_cm" || what == "reads_g" || what == "mark_off_ml" || what == "scale_ml" || what == "tap" ||
			what == "absorbance" || what == "nm" || what == "holder" || what == "suspended";
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
	return chem::written(def.formula) + "(" + phase + ")";
}

// (Stage 63) a retired formula unit (copper sulfate, the acid, ...) read from what an item holds, which is its ions:
// as many of it as the item's dissolved matter makes (chem::formula_units: each element's, and the hydrogen ions').
// false for any other species.
bool unit_mol(const std::vector<SubstanceView> &contents, const std::string &species, double &mol) {
	const chem::SpeciesTable &table = chem::builtin_species();
	const chem::SpeciesId id = table.find(species);
	if (!table.has(id) || !table.species(id).retired) {
		return false;
	}
	std::vector<std::pair<chem::SpeciesId, double>> dissolved;
	for (const SubstanceView &s : contents) {
		if (s.phase == 'a') {
			dissolved.emplace_back(table.find(s.species), s.mol);
		}
	}
	mol = chem::formula_units(table, dissolved, id);
	return true;
}

} // namespace

double mol_of(const std::vector<SubstanceView> &contents, const std::string &species) {
	if (double mol = 0.0; unit_mol(contents, species, mol)) {
		return mol;
	}
	double mol = 0.0;
	for (const SubstanceView &s : contents) {
		mol += s.species == species ? s.mol : 0.0;
	}
	return mol;
}

const ItemView *item_of(const LabView &view, const std::string &id) {
	for (const ItemView &v : view.items) {
		if (v.id == id) {
			return &v;
		}
	}
	return nullptr;
}

double grams(const ItemView &v, const std::string &species) {
	if (double mol = 0.0; unit_mol(v.contents, species, mol)) {
		const chem::SpeciesTable &table = chem::builtin_species();
		return mol * static_cast<double>(table.molar_mass_ug(table.find(species))) * 1e-6;
	}
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
	if (q.what == "met") { // a goal (§13.5): 1 while it is met
		for (const Goal &g : view.goals) {
			if (g.id == q.item) {
				return g.met ? 1.0 : 0.0;
			}
		}
		return std::nullopt;
	}
	const ItemView *v = item_of(view, q.item);
	if (v == nullptr) {
		return std::nullopt;
	}
	if (q.what == "valve") {
		return v->kind == "burner" ? std::optional<double>(100.0 * v->valve) : std::nullopt;
	}
	if (q.what == "absorbance" || q.what == "nm" || q.what == "holder" || v->kind == "spectrophotometer") { // (Stage 55) what it reads, what it is set to
		if (v->kind != "spectrophotometer") {
			return std::nullopt;
		}
		return q.what == "nm" ? std::optional<double>(v->nm) : q.what == "absorbance" ? v->absorbance : q.what == "holder" ? v->holder_c : std::nullopt; // (holder: Stage 56)
	}
	if (q.what == "reads_g" || v->kind == "balance") { // a balance's display (Stage 50): none while it is off
		return v->kind == "balance" && q.what == "reads_g" && !v->overload ? v->display_g : std::nullopt;
	}
	PartKind part = PartKind::Timer;
	if (v->kind == "burner" || v->kind == "tray" || part_kind_of(v->kind, part)) {
		return std::nullopt; // a signal part holds no matter
	}
	if (q.what == "fill") {
		return v->kind == "balloon" && v->capacity_ml > 0.0 ? std::optional<double>(100.0 * v->gas_ml / v->capacity_ml) : std::nullopt;
	}
	if (q.what == "suspended") { // the share of its solids its stirring holds up (Stage 56; Stage 32's)
		return v->kind == "vessel" ? std::optional<double>(100.0 * v->suspended) : std::nullopt;
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
	if (q.what == "solid_ml") { // its solids, not its wall (§13.3, Stage 26)
		return v->solid_ml;
	}
	if (q.what == "kpa") { // its pressure (Stage 28): an open vessel's is the room's
		return v->pressure_kpa;
	}
	if (q.what == "bubbles_cm") { // how high bubbles reach over its floor (Stage 45): none without a liquid
		return v->liquid_ml > 0.0 ? std::optional<double>(v->bubbles_cm) : std::nullopt;
	}
	if (q.what == "ph") { // its liquid's (Stage 43): none without one
		return v->ph;
	}
	if (q.what == "mark_off_ml") { // (Stage 52) how its liquid stands to its mark: over it above 0, under it below
		return v->mark_ml ? std::optional<double>(v->condensed_ml - *v->mark_ml) : std::nullopt;
	}
	if (q.what == "scale_ml") { // (Stage 52) a burette's reading from its zero line
		return v->scale_ml;
	}
	if (q.what == "tap") { // (Stage 52) a burette's or a pipette's stopcock, how far open, %
		const glass::Vessel *g = v->kind == "vessel" && !v->glassware.empty() ? glass::find(v->glassware, v->nominal_ml) : nullptr;
		return g != nullptr && g->outlet ? std::optional<double>(100.0 * v->cap) : std::nullopt;
	}
	if (q.what == "mass_g") { // all it holds, its wall not
		double all = 0.0;
		for (const SubstanceView &s : v->contents) {
			all += s.wall ? 0.0 : s.g;
		}
		return all;
	}
	double g = 0.0, mol = 0.0, count = 0.0, size = 0.0, ml = 0.0;
	if (unit_mol(v->contents, q.what, mol)) { // (Stage 63) a retired formula unit: what its ions make
		const chem::SpeciesTable &table = chem::builtin_species();
		const chem::SpeciesId id = table.find(q.what);
		g = mol * static_cast<double>(table.molar_mass_ug(id)) * 1e-6;
		ml = mol * table.species(id).molar_volume * 1e6;
	}
	for (const SubstanceView &s : v->contents) {
		if (s.species == q.what) {
			g += s.g;
			mol += s.mol;
			ml += s.ml;
			count += static_cast<double>(s.particles);
			size = s.size_um;
		}
	}
	if (q.measure == "purity") {
		return 100.0 * purity(*v, q.what);
	}
	return q.measure == "count" ? count : q.measure == "size" ? size : q.measure == "mol" ? mol : q.measure == "ml" ? ml : g;
}

std::vector<Quantity> quantities_of(const ItemView &item) {
	std::vector<Quantity> out;
	PartKind part = PartKind::Timer;
	if (item.kind == "tray" || part_kind_of(item.kind, part)) {
		return out;
	}
	if (item.kind == "burner") {
		out.push_back(Quantity{ item.id, "valve", "g" });
		return out;
	}
	if (item.kind == "balance") { // what it reads (Stage 50)
		out.push_back(Quantity{ item.id, "reads_g", "g" });
		return out;
	}
	if (item.kind == "spectrophotometer") { // (Stage 55)
		out.push_back(Quantity{ item.id, "absorbance", "g" });
		out.push_back(Quantity{ item.id, "nm", "g" });
		out.push_back(Quantity{ item.id, "holder", "g" }); // (Stage 56)
		return out;
	}
	for (const char *what : { "celsius", "gas_ml", "condensed_ml", "liquid_ml", "solid_ml", "mass_g" }) {
		out.push_back(Quantity{ item.id, what, "g" });
	}
	if (item.kind == "vessel") {
		out.push_back(Quantity{ item.id, "kpa", "g" }); // Stage 28
		if (item.ph) {
			out.push_back(Quantity{ item.id, "ph", "g" }); // Stage 43: while it holds a liquid
		}
		if (item.liquid_ml > 0.0) {
			out.push_back(Quantity{ item.id, "bubbles_cm", "g" }); // Stage 45
		}
		if (item.mark_ml) {
			out.push_back(Quantity{ item.id, "mark_off_ml", "g" }); // Stage 52
		}
		if (item.scale_ml) {
			out.push_back(Quantity{ item.id, "scale_ml", "g" });
		}
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

QuantityKind kind_of(const Quantity &q) {
	if (q.what == "fill" || q.what == "valve" || q.what == "met" || q.what == "reads_g" || q.what == "tap" || q.what == "absorbance" || q.what == "nm" || q.what == "holder") {
		return QuantityKind::Other;
	}
	return fixed(q.what) ? QuantityKind::Vessel : QuantityKind::Material;
}

std::vector<std::string> materials_of(const ItemView &item) {
	std::vector<std::string> out;
	for (const SubstanceView &s : item.contents) {
		if (!s.wall && s.g > 0.0 && std::find(out.begin(), out.end(), s.species) == out.end()) {
			out.push_back(s.species);
		}
	}
	return out;
}

std::vector<std::string> measures_of(const ItemView &item, const std::string &species) {
	std::vector<std::string> out{ "g", "mol", "ml", "purity" }; // its volume (Stage 28)
	for (const SubstanceView &s : item.contents) {
		if (s.species == species && s.phase == 's' && !s.wall) {
			out.insert(out.end(), { "count", "size" });
			break;
		}
	}
	return out;
}

std::vector<std::string> quantities_of_kind(const ItemView &item, QuantityKind kind) {
	std::vector<std::string> out;
	if (kind == QuantityKind::Material) {
		return item.kind == "vessel" || item.kind == "balloon" ? materials_of(item) : out;
	}
	for (const Quantity &q : quantities_of(item)) {
		if (fixed(q.what) && kind_of(q) == kind) {
			out.push_back(q.what);
		}
	}
	if (kind == QuantityKind::Vessel) { // in the editor's order: the temperature, what it holds, then by phase
		static const char *const ORDER[] = { "celsius", "kpa", "ph", "bubbles_cm", "scale_ml", "mark_off_ml", "mass_g", "liquid_ml", "solid_ml", "condensed_ml", "gas_ml" };
		std::vector<std::string> ordered;
		for (const char *w : ORDER) {
			if (std::find(out.begin(), out.end(), w) != out.end()) {
				ordered.emplace_back(w);
			}
		}
		return ordered;
	}
	return out;
}

std::vector<QuantityKind> kinds_of(const ItemView &item) {
	std::vector<QuantityKind> out;
	for (const QuantityKind k : { QuantityKind::Material, QuantityKind::Vessel, QuantityKind::Other }) {
		if (!quantities_of_kind(item, k).empty()) {
			out.push_back(k);
		}
	}
	return out;
}

const char *kind_name(QuantityKind kind) {
	return kind == QuantityKind::Material ? "material" : kind == QuantityKind::Vessel ? "vessel" : "other";
}

std::string unit(const Quantity &q) {
	if (q.what == "celsius") {
		return "C";
	}
	if (q.what == "valve" || q.what == "fill" || q.what == "tap" || q.what == "suspended") {
		return "%";
	}
	if (q.what == "met" || q.what == "ph" || q.what == "absorbance") {
		return "";
	}
	if (q.what == "nm") {
		return "nm";
	}
	if (q.what == "holder") {
		return "C";
	}
	if (q.what == "mass_g" || q.what == "reads_g") {
		return "g";
	}
	if (q.what == "kpa") {
		return "kPa";
	}
	if (q.what == "bubbles_cm") {
		return "cm";
	}
	if (fixed(q.what)) {
		return "mL";
	}
	return q.measure == "count" ? "" : q.measure == "size" ? "um" : q.measure == "purity" ? "%" : q.measure == "mol" ? "mol" : q.measure == "ml" ? "mL" : "g";
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
	} else if (q.what == "solid_ml") {
		what = "solids";
	} else if (q.what == "mass_g") {
		what = "total mass";
	} else if (q.what == "reads_g") {
		what = "reading";
	} else if (q.what == "absorbance") {
		what = "absorbance";
	} else if (q.what == "nm") {
		what = "wavelength";
	} else if (q.what == "holder") {
		what = "cell holder";
	} else if (q.what == "suspended") {
		what = "solids held up";
	} else if (q.what == "scale_ml") {
		what = "burette reading";
	} else if (q.what == "tap") {
		what = "tap";
	} else if (q.what == "mark_off_ml") {
		what = "from its mark";
	} else if (q.what == "kpa") {
		what = "pressure";
	} else if (q.what == "ph") {
		what = "pH";
	} else if (q.what == "bubbles_cm") {
		what = "bubbles' height";
	} else if (q.what == "valve") {
		what = "valve";
	} else if (q.what == "fill") {
		what = "fill";
	} else if (q.what == "met") {
		what = "met";
	} else {
		what = species_name(q.what) + (q.measure == "count" ? " particles" : q.measure == "size" ? " particle size" : q.measure == "purity" ? " purity" : q.measure == "mol" ? " amount" : q.measure == "ml" ? " volume" : "");
	}
	const std::string u = unit(q);
	return q.item + ": " + what + (u.empty() ? "" : " (" + u + ")");
}

std::string shown(const Quantity &q, double value) {
	if (q.what == "met") {
		return value > 0.5 ? "met" : "not met";
	}
	if (q.what == "celsius" || q.what == "valve" || q.what == "fill") {
		return fmt("%.1f ", value) + unit(q);
	}
	if (q.measure == "purity") {
		return fmt("%.3f %%", value);
	}
	if (q.what == "mass_g") {
		return fmt("%.3f g", value);
	}
	if (q.what == "absorbance") { // (Stage 55)
		return fmt("A %.3f", value);
	}
	if (q.what == "nm") {
		return fmt("%.0f nm", value);
	}
	if (q.what == "holder") {
		return fmt("holder %.0f C", value);
	}
	if (q.what == "reads_g") { // to an analytical balance's 0.1 mg (Stage 50)
		return fmt("%.4f g", value);
	}
	if (q.what == "scale_ml" || q.what == "mark_off_ml") { // (Stage 52)
		return fmt("%.2f mL", value);
	}
	if (q.what == "kpa") {
		return fmt("%.1f kPa", value);
	}
	if (q.what == "ph") {
		return fmt("pH %.2f", value);
	}
	if (q.what == "bubbles_cm") {
		return fmt("%.2f cm", value);
	}
	if (q.measure == "ml" && !fixed(q.what)) {
		return fmt("%.2f mL", value);
	}
	if (q.measure == "mol" && !fixed(q.what)) {
		return fmt("%.5f mol", value);
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
	const double g[3] = { 1.0, 0.1, 0.01 }, c[3] = { 10.0, 1.0, 0.1 }, n[3] = { 10000.0, 1000.0, 100.0 }, m[3] = { 0.01, 0.001, 0.0001 };
	const double *pick = q.what == "mass_g" || q.what == "ph" ? g
			: fixed(q.what) || q.measure == "size" || q.measure == "ml" ? c
			: q.measure == "count"                ? n
			: q.measure == "mol"                  ? m
												  : g; // purity nudges as grams do
	for (int i = 0; i < 3; ++i) {
		out[i] = pick[i];
	}
}

std::vector<std::string> goal_quantities() {
	std::vector<std::string> out{ "celsius", "liquid_ml", "gas_ml", "ph" };
	for (const chem::SpeciesDef &s : chem::builtin_species().all_species()) {
		if (!s.wall) {
			out.emplace_back(s.id);
		}
	}
	return out;
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
	if (q.measure != "g" && q.measure != "mol" && q.measure != "ml" && q.measure != "count" && q.measure != "size" && q.measure != "purity") {
		why = "a species is measured as \"g\", \"mol\", \"ml\", \"count\", \"size\" or \"purity\"";
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

bool is_part(const ItemView &v) {
	return v.kind == "timer" || v.kind == "monitor" || v.kind == "alarm";
}

// a part named by its id or its kind
bool names(const LabView &view, const std::string &pattern, const std::string &id) {
	if (pattern.empty() || pattern == id) {
		return true;
	}
	const ItemView *v = item_of(view, id);
	return v != nullptr && v->kind == pattern;
}

bool range(const std::optional<double> &value, const std::optional<double> &at_least, const std::optional<double> &at_most) {
	return (!at_least && !at_most) || (value && (!at_least || *value >= *at_least) && (!at_most || *value <= *at_most));
}

bool part_holds(const PartPattern &p, const LabView &view) {
	for (const ItemView &v : view.items) {
		if (!is_part(v) || (!p.id.empty() && v.id != p.id) || (!p.kind.empty() && v.kind != p.kind)) {
			continue;
		}
		const Part &x = v.part;
		if ((!p.item.empty() && x.quantity.item != p.item) || (!p.what.empty() && x.quantity.what != p.what) || (!p.measure.empty() && x.quantity.measure != p.measure) ||
				(p.op && x.comparison.op != *p.op) || (p.watching && x.watching != *p.watching) || (p.signals && x.out != *p.signals) || (p.ringing && x.ringing != *p.ringing) || (p.rang && (x.rang_at >= 0) != *p.rang)) {
			continue;
		}
		if (!range(x.comparison.a, p.value_at_least, p.value_at_most)) {
			continue;
		}
		return true;
	}
	return false;
}

bool rig_holds(const RigPattern &p, const LabView &view) {
	for (const Rig &r : view.rigs) {
		if (!names(view, p.from, r.from) || (!p.item.empty() && r.item != p.item) || (!p.control.empty() && r.control != p.control) || (p.change && r.change != *p.change)) {
			continue;
		}
		if (!range(r.on, p.on_at_least, p.on_at_most) || !range(r.off, p.off_at_least, p.off_at_most) || (p.on_leaves && r.on.has_value() == *p.on_leaves) ||
				(p.off_leaves && r.off.has_value() == *p.off_leaves)) {
			continue;
		}
		return true;
	}
	return false;
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
	if (c.goal) {
		const auto g = std::find_if(x.view.goals.begin(), x.view.goals.end(), [&](const Goal &o) { return o.id == *c.goal; });
		if (g == x.view.goals.end() || !g->met) {
			return false;
		}
	}
	if (c.goals_met) {
		bool every = true, any = false;
		for (const Goal &g : x.view.goals) {
			every = every && (!g.game || g.met);
			any = any || g.game;
		}
		if ((every && any) != *c.goals_met) {
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
	if ((c.part && !part_holds(*c.part, x.view)) || (c.rig && !rig_holds(*c.rig, x.view))) {
		return false;
	}
	if (c.wire) {
		bool any = false;
		for (const Wire &w : x.view.wires) {
			any = any || (names(x.view, c.wire->from, w.from) && names(x.view, c.wire->to, w.to));
		}
		if (!any) {
			return false;
		}
	}
	if (c.noted) { // (Stage 55) written by the student, a number under a label
		int count = 0;
		for (const NoteEntry &e : x.view.notebook) {
			count += e.quantity.item.empty() && e.label == *c.noted && (!c.noted_value || std::fabs(e.value - *c.noted_value) <= 1e-9 + 1e-6 * std::fabs(*c.noted_value)) ? 1 : 0;
		}
		if (count < c.noted_count.value_or(1)) {
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
	if (c.logging || c.recorded) { // (Stage 54)
		bool logged = !c.logging, written = !c.recorded;
		for (const NoteLogger &l : x.view.loggers) {
			logged = logged || (l.quantity.item == c.item && l.quantity.what == *c.logging);
		}
		int count = 0;
		for (const NoteEntry &e : x.view.notebook) {
			count += c.recorded && e.quantity.item == c.item && e.quantity.what == *c.recorded ? 1 : 0;
		}
		written = written || count >= c.recorded_count.value_or(1);
		if (!logged || !written) {
			return false;
		}
	}
	if ((c.place && v.place != *c.place) || (c.on && (v.place != "on" || id_at(x.view, v.on) != *c.on)) || (c.under && (v.place != "under" || id_at(x.view, v.under) != *c.under)) ||
		(c.drains_to && id_at(x.view, v.drains_to) != *c.drains_to)) {
		return false;
	}
	if ((c.submitted && v.submitted != *c.submitted) || (c.stirred && v.stirred != *c.stirred) || (c.burst && v.burst != *c.burst) || (c.slot && v.slot != *c.slot)) {
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
			bool listed = false, water = false;
			for (const std::string &name : *c.holds_only) {
				listed = listed || name == s.species;
				water = water || name == "water";
			}
			// (Stage 63) where water may be, so may its own ions: what is dissolved of hydrogen and oxygen alone
			if (!listed && water && s.phase == 'a') {
				const chem::SpeciesTable &table = chem::builtin_species();
				const chem::SpeciesId id = table.find(s.species);
				bool own = table.has(id);
				for (int e = 0; own && e < table.element_count(); ++e) {
					const std::string_view symbol = table.element(e).symbol;
					own = table.species(id).atoms[static_cast<size_t>(e)] == 0 || symbol == "H" || symbol == "O";
				}
				listed = own;
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
