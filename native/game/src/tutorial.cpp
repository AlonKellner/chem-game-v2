#include "lab/tutorial.hpp"
#include "lab/notebook.hpp"

#include "chem/species.hpp"
#include "conditions_json.hpp"
#include "lab/conditions.hpp"

#include <nlohmann/json.hpp>

#include <cstdio>
#include <cstdlib>
#include <optional>
#include <utility>

namespace lab {

using json = nlohmann::json;

namespace {

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
	// or a quantity of an item (Stage 41): shown as it was when the run began, its least and its most while it
	// ran (read each time the tutorial follows the lab, at least once a second of the lab's time), and now
	std::optional<Quantity> quantity;
	std::optional<double> start, least, most;
};

// What the screen's part editor holds, as a check names it (§13.3): every key given matches.
struct EditPattern {
	std::string editing, item, what, measure, control;
	std::optional<Comparison::Op> op;
	std::optional<double> value_at_least, value_at_most;
};

bool editor_holds(const EditPattern &p, const TutorialScreen *screen) {
	if (screen == nullptr || screen->edit.editing.empty()) {
		return false;
	}
	const TutorialEdit &e = screen->edit;
	const std::string &item = e.editing == "rig" ? e.item : e.quantity.item;
	if ((!p.editing.empty() && e.editing != p.editing) || (!p.item.empty() && item != p.item) || (!p.what.empty() && e.quantity.what != p.what) ||
			(!p.measure.empty() && e.quantity.measure != p.measure) || (!p.control.empty() && e.control != p.control) || (p.op && e.op != *p.op)) {
		return false;
	}
	if (p.value_at_least || p.value_at_most) {
		char *end = nullptr;
		const double v = std::strtod(e.value.c_str(), &end);
		if (e.value.empty() || end == nullptr || *end != '\0' || (p.value_at_least && !(v >= *p.value_at_least)) || (p.value_at_most && !(v <= *p.value_at_most))) {
			return false;
		}
	}
	return true;
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

// ------------------------------------------------------------- reading

struct Reader : CondReader {
	explicit Reader(const LabView &b) :
			CondReader{ b, {}, {} } {
		// the screen's part editor, which only a tutorial reads (§13.3)
		extra_key = [](const std::string &key, const json &value, Cond &c, CondReader &r, bool &ok) {
			if (key == "predicted") {
				// (Stage 55) at least that many of the report's predictions made, on the screen
				ok = value.is_number_integer() && value.get<int>() >= 1;
				if (!ok) {
					r.why = "\"predicted\" is how many predictions are made, at least 1";
					return true;
				}
				const int n = value.get<int>();
				c.extra.push_back([n](const CondContext &x) { return x.host != nullptr && static_cast<const TutorialScreen *>(x.host)->predicted >= n; });
				return true;
			}
			if (key == "calculated") {
				// (Stage 58) the calculator shows a result of this kind
				ok = value.is_string();
				if (!ok) {
					r.why = "\"calculated\" names what the calculator works out: mass, moles, solution, dilution or reaction";
					return true;
				}
				const std::string kind = value.get<std::string>();
				c.extra.push_back([kind](const CondContext &x) { return x.host != nullptr && static_cast<const TutorialScreen *>(x.host)->calculated == kind; });
				return true;
			}
			if (key != "editor") {
				return false;
			}
			EditPattern p;
			ok = static_cast<Reader &>(r).pattern(value, p);
			c.extra.push_back([p](const CondContext &x) { return editor_holds(p, static_cast<const TutorialScreen *>(x.host)); });
			return true;
		};
	}

	bool pattern(const json &j, EditPattern &p) {
		if (!j.is_object()) {
			return fail("an editor's pattern is an object");
		}
		for (const auto &[key, value] : j.items()) {
			bool ok = true;
			if (key == "item") {
				ok = item(value, "item", p.item);
			} else if (key == "editing" || key == "what" || key == "measure" || key == "control") {
				ok = value.is_string() || fail("\"" + key + "\" is not a name");
				if (ok) {
					(key == "editing" ? p.editing : key == "what" ? p.what : key == "measure" ? p.measure : p.control) = value.get<std::string>();
				}
			} else if (key == "compare") {
				Comparison::Op op = Comparison::Below;
				ok = (value.is_string() && op_of(value.get<std::string>(), op)) || fail("no comparison \"" + value.dump() + "\"");
				p.op = op;
			} else if (key == "value_at_least") {
				ok = number(value, "value_at_least", p.value_at_least);
			} else if (key == "value_at_most") {
				ok = number(value, "value_at_most", p.value_at_most);
			} else {
				ok = fail("an editor's pattern has no key \"" + key + "\"");
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
	std::vector<TutorialReport> report; // (Stage 54)
	TutorialView view;
	std::vector<TutorialGesture> no_gestures;

	void show_checks(const std::vector<Check> &list, const CondContext &x) {
		view.checks.clear();
		for (const Check &c : list) {
			view.checks.push_back(TutorialCheck{ c.text, holds(c.when, x), c.wait, now_of(c.when, x) });
		}
	}

	void measure(const LabView &lv) {
		view.results.clear();
		for (const Result &r : results) {
			if (r.quantity) {
				std::string line = r.label + ":";
				const char *sep = " ";
				const std::optional<double> now = lab::measure(lv, *r.quantity);
				for (const std::string &s : r.show) {
					const std::optional<double> &value = s == "start" ? r.start : s == "least" ? r.least : s == "most" ? r.most : now;
					line += sep + s + " " + (value ? shown(*r.quantity, *value) : std::string("-"));
					sep = ", ";
				}
				view.results.push_back(line);
				continue;
			}
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

	// what the quantities shown as results read now: the start, the least and the most so far
	void track(const LabView &lv) {
		for (Result &r : results) {
			if (!r.quantity) {
				continue;
			}
			if (const std::optional<double> v = lab::measure(lv, *r.quantity)) {
				r.start = r.start ? r.start : v;
				r.least = r.least ? std::min(*r.least, *v) : *v;
				r.most = r.most ? std::max(*r.most, *v) : *v;
			}
		}
	}

	void fill(const CondContext &x) {
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
	Reader r(bench);
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
	for (const json &j : doc.value("report", json::array())) {
		// (Stage 54) {label, unit, truth, ideal?, tolerance, relative?, why?}
		if (!j.is_object() || !j.contains("label") || !j["label"].is_string() || !j.contains("truth") || !j["truth"].is_string() || !j.contains("tolerance") ||
				!j["tolerance"].is_number() || !(j["tolerance"].get<double>() > 0.0)) {
			why = "a report's item is {\"label\", \"unit\", \"truth\": an expression, \"tolerance\" > 0, \"ideal\"?, \"relative\"?, \"why\"?}";
			return nullptr;
		}
		TutorialReport item;
		item.label = j["label"].get<std::string>();
		item.unit = j.value("unit", std::string());
		item.truth = j["truth"].get<std::string>();
		if (j.contains("ideal") && j["ideal"].is_number()) {
			item.ideal = j["ideal"].get<double>();
		}
		item.tolerance = j["tolerance"].get<double>();
		item.relative = j.value("relative", false);
		item.why = j.value("why", std::string());
		item.predict = j.value("predict", false);
		// what it names must be on the bench; a zero it divides by now may not be zero at the end
		double now = 0.0;
		std::string why_not;
		if (!evaluate(item.truth, bench, now, why_not) && why_not != "a division by zero") {
			why = "the report's \"" + item.label + "\": " + why_not;
			return nullptr;
		}
		impl->report.push_back(item);
	}
	for (const json &j : doc.value("results", json::array())) {
		Result res;
		if (j.is_object() && j.contains("quantity")) {
			// a quantity's start, least, most or value now (Stage 41)
			const json &qj = j["quantity"];
			Quantity q;
			std::string why_not;
			if (!j.contains("label") || !j["label"].is_string() || !qj.is_object() || !qj.contains("what") || !qj["what"].is_string() || !qj.contains("item") ||
					!r.item(qj["item"], "item", q.item)) {
				why = r.why.empty() ? "a result is not {\"label\", \"quantity\": {\"item\", \"what\", \"measure\"}, \"show\"}" : r.why;
				return nullptr;
			}
			q.what = qj["what"].get<std::string>();
			q.measure = qj.value("measure", std::string("g"));
			if (!valid(q, why_not)) {
				why = why_not;
				return nullptr;
			}
			res.label = j["label"].get<std::string>();
			res.item = q.item;
			res.quantity = q;
			for (const json &s : j.value("show", json::array({ "now" }))) {
				const std::string name = s.is_string() ? s.get<std::string>() : "";
				if (name != "start" && name != "least" && name != "most" && name != "now") {
					why = "a quantity's result shows \"start\", \"least\", \"most\" or \"now\"";
					return nullptr;
				}
				res.show.push_back(name);
			}
			impl->results.push_back(std::move(res));
			continue;
		}
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
	t->impl_->fill(CondContext{ bench, 1.0, false, "", nullptr });
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
	const CondContext x{ lv, speed, broken, screen.selected, &screen };
	if (m.view.state != TutorialView::Running) {
		return;
	}
	if (broken) {
		m.view.state = TutorialView::Failed;
		m.view.failure = "The lab stopped: an invariant failed (the readout names it). Press reset to start again.";
		return;
	}
	m.track(lv);
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
	for (Result &r : impl_->results) {
		r.start.reset();
		r.least.reset();
		r.most.reset();
	}
	impl_->view.state = TutorialView::Running;
	impl_->view.step = 0;
	impl_->view.failure.clear();
	impl_->view.results.clear();
}

const std::vector<TutorialReport> &Tutorial::report() const {
	return impl_->report;
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

bool Tutorial::would_pass(const LabView &lv, const TutorialScreen &screen, std::string &missing) const {
	const Impl &m = *impl_;
	if (m.view.state != TutorialView::Running || m.view.step >= static_cast<int>(m.steps.size())) {
		return true;
	}
	const CondContext x{ lv, screen.speed, screen.broken, screen.selected, &screen };
	for (const Check &c : m.steps[static_cast<size_t>(m.view.step)].checks) {
		if (!c.wait && !holds(c.when, x)) {
			missing = c.text;
			return false;
		}
	}
	return true;
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
