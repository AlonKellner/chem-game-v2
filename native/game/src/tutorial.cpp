#include "lab/tutorial.hpp"

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

// A timer's condition, or the editor's, as a check names it (§13.3): every key given matches.
struct TimerPattern {
	std::string item, what, measure;
	std::optional<Comparison::Op> op;
	std::optional<double> value_at_least, value_at_most;
	std::optional<bool> fired, limited;
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
		// the screen's timers and the editor's draft, which only a tutorial reads (§13.3)
		extra_key = [](const std::string &key, const json &value, Cond &c, CondReader &r, bool &ok) {
			if (key != "editor" && key != "timer") {
				return false;
			}
			TimerPattern p;
			ok = static_cast<Reader &>(r).pattern(value, p);
			if (key == "editor") {
				c.extra.push_back([p](const CondContext &x) { return editor_holds(p, static_cast<const TutorialScreen *>(x.host)); });
			} else {
				c.extra.push_back([p](const CondContext &x) { return timer_holds(p, static_cast<const TutorialScreen *>(x.host)); });
			}
			return true;
		};
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

	void show_checks(const std::vector<Check> &list, const CondContext &x) {
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
