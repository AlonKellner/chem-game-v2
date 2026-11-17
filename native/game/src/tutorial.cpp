#include "lab/tutorial.hpp"

#include "chem/species.hpp"
#include "lab/conditions.hpp"

#include <nlohmann/json.hpp>

#include <cstdio>
#include <cstdlib>
#include <optional>
#include <utility>

namespace lab {

using json = nlohmann::json;

namespace {

// A condition on the lab's view (GAME_SPEC §13.2): every key it names holds.
// A timer's condition, or the editor's, as a check names it (§13.3): every key given matches.
struct TimerPattern {
	std::string item, what, measure;
	std::optional<Comparison::Op> op;
	std::optional<double> value_at_least, value_at_most;
	std::optional<bool> fired, limited;
};

struct Cond {
	std::string item;
	std::optional<std::string> selected;
	std::optional<TimerPattern> editor, timer;
	std::optional<std::string> place, on, under, drains_to;
	std::optional<bool> submitted, stirred, burst, conserved, broken;
	std::optional<double> valve_at_least, valve_at_most, above_c, below_c, liquid_above_ml, liquid_below_ml, above_g, below_g, purity_above, speed_at_least;
	std::string species;
	std::optional<std::vector<std::string>> holds_only;
	std::vector<Cond> all, any, nots;
};

struct Check {
	std::string text;
	bool wait = false;
	Cond when;
};

struct Step {
	std::string part, title, text, why;
	std::vector<Check> checks;
	std::vector<TutorialGesture> gestures;
};

struct Result {
	std::string label, item, species;
	std::vector<std::string> show;
};

struct Context {
	const LabView &view;
	double speed;
	bool broken;
	const TutorialScreen *screen = nullptr;
};

bool matches(const TimerPattern &p, const TimerCondition &c, const std::string *text) {
	if ((!p.item.empty() && c.quantity.item != p.item) || (!p.what.empty() && c.quantity.what != p.what) || (!p.measure.empty() && c.quantity.measure != p.measure) ||
			(p.op && c.comparison.op != *p.op)) {
		return false;
	}
	if (p.value_at_least || p.value_at_most) {
		double v = c.comparison.a;
		if (text != nullptr) { // the editor's value as typed
			char *end = nullptr;
			v = std::strtod(text->c_str(), &end);
			if (text->empty() || end == nullptr || *end != '\0') {
				return false;
			}
		}
		if ((p.value_at_least && !(v >= *p.value_at_least)) || (p.value_at_most && !(v <= *p.value_at_most))) {
			return false;
		}
	}
	return true;
}

bool editor_holds(const TimerPattern &p, const TutorialScreen *screen) {
	if (screen == nullptr || screen->draft == nullptr) {
		return false;
	}
	const TimerDraft &d = *screen->draft;
	if (p.limited && d.limited != *p.limited) {
		return false;
	}
	if (p.what.empty() && !p.op && !p.value_at_least && !p.value_at_most) {
		return p.item.empty() || d.item == p.item;
	}
	for (size_t i = 0; i < d.conditions.size(); ++i) {
		if (matches(p, d.conditions[i], i < d.text.size() ? &d.text[i][0] : nullptr)) {
			return true;
		}
	}
	return false;
}

bool timer_holds(const TimerPattern &p, const TutorialScreen *screen) {
	if (screen == nullptr || screen->timers == nullptr) {
		return false;
	}
	for (const Timer &t : *screen->timers) {
		if ((p.fired && t.fired != *p.fired) || (p.limited && t.limited != *p.limited)) {
			continue;
		}
		for (const TimerCondition &c : t.conditions) {
			if (matches(p, c, nullptr)) {
				return true;
			}
		}
	}
	return false;
}

std::string fmt(const char *format, double v) {
	char buffer[64];
	std::snprintf(buffer, sizeof buffer, format, v);
	return buffer;
}

// 447472 -> "447,472"
std::string grouped(int64_t n) {
	std::string digits = std::to_string(n < 0 ? -n : n), out;
	for (size_t i = 0; i < digits.size(); ++i) {
		if (i > 0 && (digits.size() - i) % 3 == 0) {
			out += ',';
		}
		out += digits[i];
	}
	return n < 0 ? "-" + out : out;
}

const ItemView *item_of(const LabView &view, const std::string &id) {
	for (const ItemView &v : view.items) {
		if (v.id == id) {
			return &v;
		}
	}
	return nullptr;
}

const std::string &id_at(const LabView &view, int index) {
	static const std::string none;
	return index >= 0 && index < static_cast<int>(view.items.size()) ? view.items[static_cast<size_t>(index)].id : none;
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

// the species' share of the item's solids and liquids, by mass (its walls not counted)
double purity(const ItemView &v, const std::string &species) {
	double all = 0.0;
	for (const SubstanceView &s : v.contents) {
		if (!s.wall && s.phase != 'g') {
			all += s.g;
		}
	}
	return all > 0.0 ? grams(v, species) / all : 0.0;
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

bool holds(const Cond &c, const Context &x) {
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
	if (c.conserved && *c.conserved != x.view.conserved) {
		return false;
	}
	if (c.broken && *c.broken != x.broken) {
		return false;
	}
	if (c.speed_at_least && !(x.speed >= *c.speed_at_least)) {
		return false;
	}
	if (c.selected && (x.screen == nullptr || x.screen->selected != *c.selected)) {
		return false;
	}
	if ((c.editor && !editor_holds(*c.editor, x.screen)) || (c.timer && !timer_holds(*c.timer, x.screen))) {
		return false;
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

// the measured value a condition waits on, for the player to watch
std::string now_of(const Cond &c, const Context &x) {
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

// ------------------------------------------------------------- reading

struct Reader {
	const LabView &bench;
	std::string why;

	bool fail(const std::string &reason) {
		if (why.empty()) {
			why = reason;
		}
		return false;
	}

	bool item(const json &j, const char *key, std::string &out) {
		if (!j.is_string()) {
			return fail(std::string("\"") + key + "\" is not an item's id");
		}
		out = j.get<std::string>();
		return item_of(bench, out) != nullptr || fail("the bench has no item \"" + out + "\"");
	}

	bool number(const json &j, const char *key, std::optional<double> &out) {
		if (!j.is_number()) {
			return fail(std::string("\"") + key + "\" is not a number");
		}
		out = j.get<double>();
		return true;
	}

	bool flag(const json &j, const char *key, std::optional<bool> &out) {
		if (!j.is_boolean()) {
			return fail(std::string("\"") + key + "\" is not true or false");
		}
		out = j.get<bool>();
		return true;
	}

	bool species(const std::string &name) { return chem::builtin_species().has(chem::builtin_species().find(name)) || fail("no species \"" + name + "\""); }

	bool cond(const json &j, Cond &c) {
		if (!j.is_object() || j.empty()) {
			return fail("a condition is not an object with keys");
		}
		for (const auto &[key, value] : j.items()) {
			bool ok = true;
			if (key == "item") {
				ok = item(value, "item", c.item);
			} else if (key == "place") {
				ok = value.is_string() || fail("\"place\" is not a place");
				if (ok) {
					c.place = value.get<std::string>();
					ok = *c.place == "cabinet" || *c.place == "bench" || *c.place == "on" || *c.place == "under" || *c.place == "outbox" || fail("no place \"" + *c.place + "\"");
				}
			} else if (key == "on" || key == "under" || key == "drains_to") {
				std::string id;
				ok = item(value, key.c_str(), id);
				(key == "on" ? c.on : key == "under" ? c.under : c.drains_to) = id;
			} else if (key == "submitted") {
				ok = flag(value, "submitted", c.submitted);
			} else if (key == "stirred") {
				ok = flag(value, "stirred", c.stirred);
			} else if (key == "burst") {
				ok = flag(value, "burst", c.burst);
			} else if (key == "conserved") {
				ok = flag(value, "conserved", c.conserved);
			} else if (key == "broken") {
				ok = flag(value, "broken", c.broken);
			} else if (key == "valve_at_least") {
				ok = number(value, "valve_at_least", c.valve_at_least);
			} else if (key == "valve_at_most") {
				ok = number(value, "valve_at_most", c.valve_at_most);
			} else if (key == "above_c") {
				ok = number(value, "above_c", c.above_c);
			} else if (key == "below_c") {
				ok = number(value, "below_c", c.below_c);
			} else if (key == "liquid_above_ml") {
				ok = number(value, "liquid_above_ml", c.liquid_above_ml);
			} else if (key == "liquid_below_ml") {
				ok = number(value, "liquid_below_ml", c.liquid_below_ml);
			} else if (key == "above_g") {
				ok = number(value, "above_g", c.above_g);
			} else if (key == "below_g") {
				ok = number(value, "below_g", c.below_g);
			} else if (key == "purity_above") {
				ok = number(value, "purity_above", c.purity_above);
			} else if (key == "speed_at_least") {
				ok = number(value, "speed_at_least", c.speed_at_least);
			} else if (key == "species") {
				ok = value.is_string() || fail("\"species\" is not a species' id");
				if (ok) {
					c.species = value.get<std::string>();
					ok = species(c.species);
				}
			} else if (key == "holds_only") {
				ok = value.is_array() || fail("\"holds_only\" is not a list of species");
				if (ok) {
					c.holds_only.emplace();
					for (const json &s : value) {
						ok = ok && (s.is_string() || fail("\"holds_only\" is not a list of species")) && species(s.get<std::string>());
						if (ok) {
							c.holds_only->push_back(s.get<std::string>());
						}
					}
				}
			} else if (key == "all" || key == "any") {
				ok = (value.is_array() && !value.empty()) || fail("\"" + key + "\" is not a list of conditions");
				for (size_t i = 0; ok && i < value.size(); ++i) {
					Cond inner;
					ok = cond(value[i], inner);
					(key == "all" ? c.all : c.any).push_back(std::move(inner));
				}
			} else if (key == "selected") {
				std::string id;
				ok = item(value, "selected", id);
				c.selected = id;
			} else if (key == "editor" || key == "timer") {
				TimerPattern p;
				ok = pattern(value, p);
				(key == "editor" ? c.editor : c.timer) = p;
			} else if (key == "not") {
				Cond inner;
				ok = cond(value, inner);
				c.nots.push_back(std::move(inner));
			} else {
				ok = fail("a condition has no key \"" + key + "\"");
			}
			if (!ok) {
				return false;
			}
		}
		const bool per_item = c.place || c.on || c.under || c.drains_to || c.submitted || c.stirred || c.burst || c.valve_at_least || c.valve_at_most || c.above_c || c.below_c ||
				c.liquid_above_ml || c.liquid_below_ml || !c.species.empty() || c.holds_only;
		if (per_item && c.item.empty()) {
			return fail("a condition on an item names no \"item\"");
		}
		if (!c.species.empty() && !c.above_g && !c.below_g && !c.purity_above) {
			return fail("a condition names \"" + c.species + "\" but no \"above_g\", \"below_g\" or \"purity_above\"");
		}
		if ((c.above_g || c.below_g || c.purity_above) && c.species.empty()) {
			return fail("a condition on an amount names no \"species\"");
		}
		return true;
	}

	bool pattern(const json &j, TimerPattern &p) {
		if (!j.is_object()) {
			return fail("a timer's pattern is an object");
		}
		for (const auto &[key, value] : j.items()) {
			bool ok = true;
			if (key == "item") {
				ok = item(value, "item", p.item);
			} else if (key == "what" || key == "measure") {
				ok = value.is_string() || fail("\"" + key + "\" is not a name");
				if (ok) {
					(key == "what" ? p.what : p.measure) = value.get<std::string>();
				}
			} else if (key == "compare") {
				Comparison::Op op = Comparison::Below;
				ok = (value.is_string() && op_of(value.get<std::string>(), op)) || fail("no comparison \"" + value.dump() + "\"");
				p.op = op;
			} else if (key == "value_at_least") {
				ok = number(value, "value_at_least", p.value_at_least);
			} else if (key == "value_at_most") {
				ok = number(value, "value_at_most", p.value_at_most);
			} else if (key == "fired") {
				ok = flag(value, "fired", p.fired);
			} else if (key == "limited") {
				ok = flag(value, "limited", p.limited);
			} else {
				ok = fail("a timer's pattern has no key \"" + key + "\"");
			}
			if (!ok) {
				return false;
			}
		}
		if (!p.what.empty()) {
			std::string why_not;
			if (!valid(Quantity{ p.item, p.what, p.measure.empty() ? "g" : p.measure }, why_not)) {
				return fail(why_not);
			}
		}
		return true;
	}

	bool check(const json &j, Check &out) {
		if (!j.is_object() || !j.contains("text") || !j["text"].is_string() || !j.contains("when")) {
			return fail("a check is not {\"text\", \"when\"}");
		}
		for (const auto &[key, value] : j.items()) {
			if (key != "text" && key != "when" && key != "wait") {
				return fail("a check has no key \"" + key + "\"");
			}
		}
		if (j.contains("wait") && !j["wait"].is_boolean()) {
			return fail("\"wait\" is not true or false");
		}
		out.text = j["text"].get<std::string>();
		out.wait = j.value("wait", false);
		return cond(j["when"], out.when);
	}

	bool checks(const json &j, const char *what, std::vector<Check> &out) {
		if (!j.is_array()) {
			return fail(std::string("\"") + what + "\" is not a list of checks");
		}
		for (const json &e : j) {
			Check c;
			if (!check(e, c)) {
				return false;
			}
			out.push_back(std::move(c));
		}
		return true;
	}

	static bool target(const json &j) { return j.is_string() && !j.get<std::string>().empty(); }

	bool gesture(const json &j, TutorialGesture &g) {
		if (j.is_object() && j.size() == 1 && j.contains("type") && target(j["type"])) {
			g.type = j["type"].get<std::string>();
			return true;
		}
		if (j.is_object() && j.size() == 1 && j.contains("click") && target(j["click"])) {
			g.what = j["click"].get<std::string>();
			return true;
		}
		if (j.is_object() && j.size() == 1 && j.contains("drag") && j["drag"].is_array() && j["drag"].size() == 2 && target(j["drag"][0]) && target(j["drag"][1])) {
			g.drag = true;
			g.what = j["drag"][0].get<std::string>();
			g.to = j["drag"][1].get<std::string>();
			return true;
		}
		return fail("a gesture is not {\"click\": what}, {\"drag\": [what, to]} or {\"type\": text}");
	}
};

} // namespace

struct Tutorial::Impl {
	std::string title;
	std::vector<Step> steps;
	std::vector<Check> fails; // `text` is what went wrong
	std::vector<Check> goal;
	std::vector<Result> results;
	TutorialView view;
	std::vector<TutorialGesture> no_gestures;

	void show_checks(const std::vector<Check> &list, const Context &x) {
		view.checks.clear();
		for (const Check &c : list) {
			view.checks.push_back(TutorialCheck{ c.text, holds(c.when, x), c.wait, now_of(c.when, x) });
		}
	}

	void measure(const LabView &lv) {
		view.results.clear();
		for (const Result &r : results) {
			const ItemView *v = item_of(lv, r.item);
			if (v == nullptr) {
				continue;
			}
			std::string line = r.label + ":";
			const char *sep = " ";
			for (const std::string &s : r.show) {
				if (s == "g") {
					line += sep + fmt("%.3f g", grams(*v, r.species));
				} else if (s == "purity") {
					line += sep + fmt("%.3f%% pure", 100.0 * purity(*v, r.species));
				} else if (s == "gas_share") {
					line += sep + fmt("%.1f%% of its gas", 100.0 * gas_share(*v, r.species));
				} else if (s == "crystals") {
					const SubstanceView *sv = substance(*v, r.species);
					line += sep + grouped(sv != nullptr ? sv->particles : 0) + fmt(" crystals of %.2f mm", sv != nullptr ? sv->size_um / 1000.0 : 0.0);
				}
				sep = ", ";
			}
			view.results.push_back(line);
		}
		view.results.push_back(lv.conserved ? "every element, mass and energy: unchanged" : "the world's totals moved");
	}

	void fill(const Context &x) {
		view.step = view.step < 0 ? 0 : view.step;
		if (view.step < static_cast<int>(steps.size())) {
			const Step &s = steps[static_cast<size_t>(view.step)];
			view.part = s.part;
			view.title = s.title;
			view.text = s.text;
			view.why = s.why;
			show_checks(s.checks, x);
		} else {
			view.part = title;
			view.title = "The goal";
			view.text.clear();
			view.why.clear();
			show_checks(goal, x);
		}
	}
};

Tutorial::Tutorial(std::unique_ptr<Impl> impl) :
		impl_(std::move(impl)) {}
Tutorial::~Tutorial() = default;

std::unique_ptr<Tutorial> Tutorial::open(std::string_view tutorial_json, const LabView &bench, std::string &why) {
	const json doc = json::parse(tutorial_json, nullptr, false);
	if (doc.is_discarded() || !doc.is_object()) {
		why = "the tutorial is not JSON";
		return nullptr;
	}
	if (doc.value("format", "") != "chem-tutorial/1") {
		why = "the tutorial is not \"chem-tutorial/1\"";
		return nullptr;
	}
	auto impl = std::make_unique<Impl>();
	Reader r{ bench, {} };
	impl->title = doc.value("title", "Tutorial");
	const json steps = doc.value("steps", json::array());
	if (!steps.is_array() || steps.empty()) {
		why = "the tutorial has no steps";
		return nullptr;
	}
	std::string part;
	for (const json &j : steps) {
		Step s;
		if (!j.is_object() || !j.contains("title") || !j.contains("text") || !j.contains("checks")) {
			why = "a step is not {\"title\", \"text\", \"checks\", ...}";
			return nullptr;
		}
		for (const auto &[key, value] : j.items()) {
			if (key != "part" && key != "title" && key != "text" && key != "why" && key != "checks" && key != "play") {
				why = "a step has no key \"" + key + "\"";
				return nullptr;
			}
		}
		part = j.value("part", part); // a part goes on until the next names another
		s.part = part;
		s.title = j.value("title", "");
		s.text = j.value("text", "");
		s.why = j.value("why", "");
		if (!r.checks(j["checks"], "checks", s.checks) || (s.checks.empty() && !r.fail("the step \"" + s.title + "\" has no checks"))) {
			why = r.why;
			return nullptr;
		}
		for (const json &g : j.value("play", json::array())) {
			TutorialGesture gesture;
			if (!r.gesture(g, gesture)) {
				why = r.why;
				return nullptr;
			}
			s.gestures.push_back(std::move(gesture));
		}
		if (s.gestures.empty()) {
			for (Check &c : s.checks) {
				c.wait = true; // a step that asks for no gesture waits on the lab
			}
		}
		impl->steps.push_back(std::move(s));
	}
	if (!r.checks(doc.value("fails", json::array()), "fails", impl->fails) || !r.checks(doc.value("goal", json::array()), "goal", impl->goal)) {
		why = r.why;
		return nullptr;
	}
	for (const json &j : doc.value("results", json::array())) {
		Result res;
		if (!j.is_object() || !j.contains("label") || !j.contains("item") || !j.contains("species") || !r.item(j["item"], "item", res.item) || !j["species"].is_string() ||
				!r.species(j["species"].get<std::string>())) {
			why = r.why.empty() ? "a result is not {\"label\", \"item\", \"species\", \"show\"}" : r.why;
			return nullptr;
		}
		res.label = j["label"].get<std::string>();
		res.species = j["species"].get<std::string>();
		for (const json &s : j.value("show", json::array({ "g" }))) {
			const std::string name = s.is_string() ? s.get<std::string>() : "";
			if (name != "g" && name != "purity" && name != "gas_share" && name != "crystals") {
				why = "a result shows \"g\", \"purity\", \"gas_share\" or \"crystals\"";
				return nullptr;
			}
			res.show.push_back(name);
		}
		impl->results.push_back(std::move(res));
	}
	impl->view.steps = static_cast<int>(impl->steps.size());
	std::unique_ptr<Tutorial> t(new Tutorial(std::move(impl)));
	t->impl_->fill(Context{ bench, 1.0, false, nullptr });
	return t;
}

void Tutorial::follow(const LabView &lv, double speed, bool broken) {
	TutorialScreen screen;
	screen.speed = speed;
	screen.broken = broken;
	follow(lv, screen);
}

void Tutorial::follow(const LabView &lv, const TutorialScreen &screen) {
	Impl &m = *impl_;
	const double speed = screen.speed;
	const bool broken = screen.broken;
	const Context x{ lv, speed, broken, &screen };
	if (m.view.state != TutorialView::Running) {
		return;
	}
	if (broken) {
		m.view.state = TutorialView::Failed;
		m.view.failure = "The lab stopped: an invariant failed (the readout names it). Press reset to start again.";
		return;
	}
	for (const Check &f : m.fails) {
		if (holds(f.when, x)) {
			m.view.state = TutorialView::Failed;
			m.view.failure = f.text;
			m.fill(x);
			return;
		}
	}
	const int count = static_cast<int>(m.steps.size());
	while (m.view.step < count) {
		bool all = true;
		for (const Check &c : m.steps[static_cast<size_t>(m.view.step)].checks) {
			all = all && holds(c.when, x);
		}
		if (!all) {
			break;
		}
		++m.view.step;
	}
	m.fill(x);
	if (m.view.step == count) {
		for (const Check &g : m.goal) {
			if (!holds(g.when, x)) {
				m.view.state = TutorialView::Failed;
				m.view.failure = "Everything is handed in, but this does not hold: " + g.text + ".";
				return;
			}
		}
		m.view.state = TutorialView::Complete;
		m.measure(lv);
	}
}

void Tutorial::restart() {
	impl_->view.state = TutorialView::Running;
	impl_->view.step = 0;
	impl_->view.failure.clear();
	impl_->view.results.clear();
}

const TutorialView &Tutorial::view() const {
	return impl_->view;
}

const std::vector<TutorialGesture> &Tutorial::gestures() const {
	const Impl &m = *impl_;
	if (m.view.state != TutorialView::Running || m.view.step >= static_cast<int>(m.steps.size())) {
		return m.no_gestures;
	}
	for (const TutorialCheck &c : m.view.checks) {
		if (!c.wait && !c.done) {
			return m.steps[static_cast<size_t>(m.view.step)].gestures;
		}
	}
	return m.no_gestures;
}

bool Tutorial::waiting() const {
	const Impl &m = *impl_;
	if (m.view.state != TutorialView::Running || m.view.step >= static_cast<int>(m.steps.size())) {
		return false;
	}
	bool open = false;
	for (const TutorialCheck &c : m.view.checks) {
		if (!c.done && !c.wait) {
			return false;
		}
		open = open || !c.done;
	}
	return open;
}

const std::string &Tutorial::title() const {
	return impl_->title;
}

} // namespace lab
