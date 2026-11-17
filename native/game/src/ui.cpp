#include "lab/ui.hpp"

#include "lab/conditions.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <utility>

namespace lab {

using json = nlohmann::json;

namespace {

struct Rect {
	float x = 0, y = 0, w = 0, h = 0;
	bool has(float px, float py) const { return px >= x && px < x + w && py >= y && py < y + h; }
	float cx() const { return x + w / 2; }
	float cy() const { return y + h / 2; }
};

// the screen's fixed parts
constexpr Rect CABINET{ 0, 64, 240, 1016 };
constexpr Rect BENCH{ 260, 120, 1120, 600 };
constexpr Rect OUTBOX{ 260, 760, 640, 300 };
constexpr Rect DRAWER{ 920, 760, 460, 300 }; // the drawer of signal parts (§13.3), beside the outbox
constexpr Rect EDITOR{ 440, 76, 720, 300 }; // a part's settings, or a rig's, being written
constexpr Rect READOUT{ 1400, 64, 520, 1016 };
constexpr Rect PANEL{ 600, 250, 440, 300 };
constexpr Rect TUTOR{ 1400, 64, 520, 476 }; // a tutorial's card, above the readout (§13.2)
constexpr Rect ROOM{ 0, 64, 1390, 1016 }; // the lit wall behind the cabinet, the bench and the outbox
constexpr float SPEEDS[] = { 1, 10, 60, 600 };

constexpr uint32_t BACKGROUND = 0x2a2a2aff, GRID = 0x444444ff, GLASS = 0xffffffcc, INK = 0xe6e6e6ff, DIM = 0x9a9a9aff, PANEL_FILL = 0x383838ff, BUTTON = 0x505050ff,
				   BUTTON_ON = 0x3c7a4eff, WARN = 0xe0a040ff, GUIDE = 0xffc83cff, GOOD = 0x78c88cff, BAD = 0xf05a5aff;
// on the lit wall: what is in the glass is seen by the light that comes through it (§12)
constexpr uint32_t WALL = 0xdededeff, WALL_GRID = 0xc4c4c4ff, WALL_INK = 0x505050ff, WALL_GLASS = 0x4a5662cc;
constexpr double WALL_RADIANCE = 0.7305, DARK_RADIANCE = 0.0232; // the linear values of WALL and BACKGROUND

uint32_t rgba(int r, int g, int b, int a = 255) {
	return static_cast<uint32_t>(r) << 24 | static_cast<uint32_t>(g) << 16 | static_cast<uint32_t>(b) << 8 | static_cast<uint32_t>(a);
}

uint32_t mix(uint32_t a, uint32_t b, double t) {
	t = t < 0 ? 0 : t > 1 ? 1 : t;
	uint32_t out = 0;
	for (int shift = 24; shift >= 0; shift -= 8) {
		const double ca = (a >> shift) & 0xff, cb = (b >> shift) & 0xff;
		out |= static_cast<uint32_t>(ca + (cb - ca) * t + 0.5) << shift;
	}
	return out;
}

std::string text_of(const char *format, double a = 0, double b = 0) {
	char buffer[160];
	std::snprintf(buffer, sizeof buffer, format, a, b);
	return buffer;
}

// words wrapped at about `width` characters
std::vector<std::string> wrapped(const std::string &s, size_t width) {
	std::vector<std::string> lines;
	std::string line;
	size_t at = 0;
	while (at < s.size()) {
		size_t end = s.find(' ', at);
		end = end == std::string::npos ? s.size() : end;
		const std::string word = s.substr(at, end - at);
		if (!line.empty() && line.size() + 1 + word.size() > width) {
			lines.push_back(line);
			line.clear();
		}
		line += (line.empty() ? "" : " ") + word;
		at = end + 1;
	}
	if (!line.empty()) {
		lines.push_back(line);
	}
	return lines;
}

// "00:05:00.000" (§13.3): hours, minutes, seconds and milliseconds
std::string clock_text(double seconds) {
	const auto ms = static_cast<int64_t>(std::floor(std::max(0.0, seconds) * 1000.0 + 0.5));
	char buffer[48];
	std::snprintf(buffer, sizeof buffer, "%02lld:%02lld:%02lld.%03lld", static_cast<long long>(ms / 3600000), static_cast<long long>(ms / 60000 % 60), static_cast<long long>(ms / 1000 % 60),
			static_cast<long long>(ms % 1000));
	return buffer;
}

// "1:30", "00:01:30.000", "90", "90.5": seconds; false for anything else
bool seconds_of(const std::string &text, double &seconds) {
	if (text.empty()) {
		return false;
	}
	double total = 0.0;
	size_t at = 0;
	int parts = 0;
	while (true) {
		const size_t colon = text.find(':', at);
		const std::string piece = text.substr(at, colon == std::string::npos ? std::string::npos : colon - at);
		char *end = nullptr;
		const double v = std::strtod(piece.c_str(), &end);
		if (piece.empty() || end == nullptr || *end != '\0' || !(v >= 0.0) || !std::isfinite(v) || ++parts > 3 || (parts > 1 && v >= 60.0)) {
			return false;
		}
		total = total * 60.0 + v;
		if (colon == std::string::npos) {
			break;
		}
		at = colon + 1;
	}
	seconds = total;
	return true;
}

// the part editor (§13.3): what is being written, until it is set
struct Draft {
	enum Edit { None, Monitor, Timer, Rig, Choose } edit = None;
	std::string part; // the part being set, or a rig's source
	Quantity quantity; // a monitor's
	Comparison::Op op = Comparison::Below;
	std::array<std::string, 2> text; // a monitor's values, as typed
	std::string preset; // a timer's, as typed
	std::string item, control; // a rig's: what it acts on; Choose: the timer a wire was dropped on
	bool change = false;
	std::array<std::optional<double>, 3> setting; // on, off, change; none: leave as it is
	std::array<std::string, 3> percent; // a valve's settings, as typed
};

constexpr const char *PART_KINDS[] = { "timer", "monitor", "alarm" };
constexpr Comparison::Op OPS[] = { Comparison::Above, Comparison::Below, Comparison::AtLeast, Comparison::AtMost, Comparison::Reaches, Comparison::Between, Comparison::Outside };
constexpr const char *OP_KEYS[] = { "above", "below", "at_least", "at_most", "reaches", "between", "outside" };
constexpr const char *MEASURES[] = { "g", "count", "size", "purity" };
constexpr const char *SIDES[] = { "on", "off", "change" };

bool is_part_kind(const std::string &kind) {
	return kind == "timer" || kind == "monitor" || kind == "alarm";
}

} // namespace

struct Ui::Impl {
	std::unique_ptr<Lab> lab;
	std::string bench; // to open it again
	LabView view;
	DrawList list;
	OpticsScene scene;
	std::string message;
	double speed = 1.0;
	bool paused = false;
	bool check_every_step = false;
	double owed = 0.0; // steps not yet taken
	// the pointer
	float px = 0, py = 0;
	bool down = false;
	int drag = -1; // the item in hand
	float grab_x = 0, grab_y = 0; // where in it it was taken
	int anchor_c = 0, anchor_r = 0; // the square of its footprint under the pointer
	float press_x = 0, press_y = 0; // where the button went down
	int mods = 0; // Ui::Mod: the keys held with the pointer
	// the selection (§13.1): what was chosen, in order; `selected` is the last of it
	std::vector<int> group;
	bool marquee = false; // a rectangle being drawn from (press_x, press_y)
	bool moved = false;
	int valve_drag = -1; // the burner whose valve is being turned
	bool slider_drag = false;
	int hover = -1;
	// the pour panel
	bool panel = false;
	int from = -1, to = -1;
	double pour_fraction = 1.0;
	// a tutorial (§13.2), and real time for its pointer's pulse
	std::unique_ptr<Tutorial> tutorial;
	double clock = 0.0;
	int selected = -1; // the last item chosen
	// signals (§13.3): the part editor, a part being made or wired, what rang
	Draft draft;
	bool picking = false; // the monitor's list of quantities is open
	int focus = -1; // the field typed into: 0, 1 a monitor's values; 2 a timer's preset; 3, 4, 5 a valve's setting on, off, on a change
	bool fresh = false; // the next key replaces the focused field
	bool resume = false; // the clock was playing when the editor opened
	int making = -1; // a part's kind, dragged out of the drawer
	int wiring = -1; // the part whose port a wire is drawn from
	std::vector<std::string> rung, chain; // the alarms that rang, and the parts that set them off, until the clock plays

	void refresh() { view = lab->view(); }
	// what a tutorial's checks read of the screen
	TutorialScreen screen_now() const {
		TutorialScreen screen;
		screen.speed = speed;
		screen.broken = lab->failed();
		screen.paused = paused;
		screen.selected = selected >= 0 ? id(selected) : std::string();
		static const char *const EDITING[] = { "", "monitor", "timer", "rig", "choose" };
		screen.edit.editing = EDITING[draft.edit];
		screen.edit.part = draft.part;
		screen.edit.quantity = draft.quantity;
		screen.edit.op = draft.op;
		screen.edit.value = draft.text[0];
		screen.edit.item = draft.item;
		screen.edit.control = draft.control;
		return screen;
	}
	void follow() {
		if (tutorial) {
			tutorial->follow(view, screen_now());
		}
	}
	void play() {
		paused = false;
		if (!rung.empty()) {
			act({ { "do", "ack" } }); // the alarms are answered
			rung.clear();
			chain.clear();
		}
	}

	// ---- a running tutorial allows only its step's gestures (§13.2)
	bool guided() const { return tutorial && tutorial->view().state == TutorialView::Running; }
	// its clock stands while the step asks for a gesture
	bool held() const { return guided() && !tutorial->waiting(); }
	bool asks_click(const std::string &what) const {
		for (const TutorialGesture &g : tutorial->gestures()) {
			if (!g.drag && (g.what == what || (what.back() == '@' && g.what.rfind(what, 0) == 0))) {
				return true;
			}
		}
		return false;
	}
	bool asks_drag(int index) const {
		for (const TutorialGesture &g : tutorial->gestures()) {
			if (g.drag && g.what == "item:" + id(index)) {
				return true;
			}
		}
		return false;
	}
	// refused, with what the step asks for instead
	bool not_now() {
		const TutorialView &t = tutorial->view();
		message = tutorial->waiting() ? "Not now: wait for the lab (" + t.title + ")" : "Not now. The tutorial asks: " + t.text;
		return false;
	}
	bool allowed_click(const std::string &what) { return !guided() || asks_click(what) || not_now(); }
	// The editor's command, as the step's last gesture: tried on a copy of the
	// lab first, and refused if the step's checks would not then hold (§13.2):
	// a tutorial cannot be left with a part or a rig set wrong.
	bool guided_commit(const json &command) {
		if (!guided() || tutorial->gestures().empty() || tutorial->gestures().back().what != "edit:set") {
			return true;
		}
		std::string why;
		std::unique_ptr<Lab> copy = Lab::load(lab->save(), why);
		if (!copy || !copy->command(command.dump(), why)) {
			return true; // refused by the lab itself: it says why
		}
		TutorialScreen after = screen_now();
		after.edit = TutorialEdit{}; // set, the editor closes
		std::string missing;
		if (tutorial->would_pass(copy->view(), after, missing)) {
			return true;
		}
		message = "Not that: the tutorial asks for " + missing + ". " + tutorial->view().text;
		return false;
	}
	// what a step's typed gesture allows: text that is still the start of what it asks to type
	bool allowed_text(const std::string &text) {
		if (!guided()) {
			return true;
		}
		for (const TutorialGesture &g : tutorial->gestures()) {
			if (!g.type.empty() && g.type.rfind(text, 0) == 0) {
				return true;
			}
		}
		message = "Not that: the tutorial asks you to type what its step says. " + tutorial->view().text;
		return false;
	}
	// a drop where the step's drag goes: onto its item, into a free square, the outbox or the cabinet
	bool guided_drop(int index, int target, float x, float y) {
		if (!guided()) {
			return true;
		}
		for (const TutorialGesture &g : tutorial->gestures()) {
			if (!g.drag || g.what != "item:" + id(index)) {
				continue;
			}
			const std::string &where = g.to;
			if ((where.rfind("item:", 0) == 0 && target >= 0 && id(target) == where.substr(5)) || (where.rfind("square:", 0) == 0 && target < 0 && BENCH.has(x, y)) ||
					(where == "outbox" && OUTBOX.has(x, y)) || (where == "cabinet" && CABINET.has(x, y))) {
				return true;
			}
		}
		return not_now();
	}

	// the bench's squares (§13): 40 px on a bench of 28 x 15
	float square() const { return std::fmin(BENCH.w / static_cast<float>(view.cols), BENCH.h / static_cast<float>(view.rows)); }
	Rect square_rect(int col, int row) const { return Rect{ BENCH.x + static_cast<float>(col) * square(), BENCH.y + static_cast<float>(row) * square(), square(), square() }; }
	// an item's footprint on the bench
	Rect foot(const ItemView &v) const { return Rect{ BENCH.x + static_cast<float>(v.col) * square(), BENCH.y + static_cast<float>(v.row) * square(), static_cast<float>(v.w) * square(), static_cast<float>(v.h) * square() }; }
	static constexpr float STAND = 34; // a vessel's foot above its footprint's floor; with a burner under it, 34 + 94
	// the square of an item's footprint its body's centre is on, as it would stand on the bench
	void body_square(int index, int &c, int &r) const {
		const ItemView &v = view.items[static_cast<size_t>(index)];
		if (is_part_kind(v.kind)) {
			c = r = 0;
			return;
		}
		float w = 0, h = 0;
		size_of(v, w, h);
		c = std::clamp(static_cast<int>(static_cast<float>(v.w) * square() / 2 / square()), 0, v.w - 1);
		r = std::clamp(static_cast<int>((static_cast<float>(v.h) * square() - STAND - h / 2) / square()), 0, v.h - 1);
	}

	// how large an item is drawn
	void size_of(const ItemView &v, float &w, float &h) const {
		if (v.kind == "burner") {
			w = 56;
			h = 26;
		} else if (v.kind == "balloon") {
			const double r = v.burst ? 10.0 : std::fmin(90.0, 14.0 + 6.0 * std::cbrt(v.gas_ml + v.condensed_ml));
			w = h = static_cast<float>(2.0 * r);
		} else if (v.condenser) {
			w = 44;
			h = 120;
		} else {
			w = static_cast<float>(40.0 + 8.0 * std::cbrt(v.volume_ml));
			h = w * 1.25f;
		}
	}

	// its slot among the items of a place, in order
	int slot(int index, const char *place) const {
		int n = 0;
		for (int i = 0; i < index; ++i) {
			if (view.items[static_cast<size_t>(i)].place == place && view.items[static_cast<size_t>(i)].kind != "tray") {
				++n;
			}
		}
		return n;
	}

	// where an item is drawn (not counting a drag)
	Rect rect(int index) const {
		const ItemView &v = view.items[static_cast<size_t>(index)];
		float w = 0, h = 0;
		size_of(v, w, h);
		if (v.place == "cabinet") {
			const Rect box{ 20, 96 + static_cast<float>(slot(index, "cabinet")) * 118, 200, 110 };
			const float scale = std::fmin(1.0f, std::fmin(box.w / w, (box.h - 24) / h));
			return Rect{ box.cx() - w * scale / 2, box.y + box.h - 22 - h * scale, w * scale, h * scale };
		}
		if (v.place == "outbox") {
			const Rect box{ OUTBOX.x + 12 + static_cast<float>(slot(index, "outbox")) * 160, OUTBOX.y + 40, 150, 230 };
			return Rect{ box.cx() - w / 2, box.y + box.h - 30 - h, w, h };
		}
		if (v.place == "on" && v.on >= 0) {
			const Rect holder = rect(v.on);
			return Rect{ holder.cx() - w / 2, holder.y - h - 4, w, h };
		}
		if (v.place == "under" && v.under >= 0) {
			const Rect vessel = rect(v.under);
			return Rect{ vessel.cx() - w / 2, vessel.y + vessel.h + 64, w, h };
		}
		if (is_part_kind(v.kind)) {
			const Rect f = foot(v);
			return Rect{ f.x + 3, f.y + 3, f.w - 6, f.h - 6 }; // a part fills its square
		}
		// on the bench: standing on its footprint's floor, raised when a burner is under it
		const Rect c = foot(v);
		float lift = STAND;
		for (const ItemView &other : view.items) {
			if (other.kind == "burner" && other.under == index) {
				lift = 34 + 94; // the burner, and its flame, under it
			}
		}
		return Rect{ c.cx() - w / 2, c.y + c.h - lift - h, w, h };
	}

	// the item under a point, the topmost; not the tray, not `except`; with
	// `on_bench`, only what stands on the bench (what can be joined or poured into)
	int item_at(float x, float y, int except = -1, bool on_bench = false) const {
		for (int i = static_cast<int>(view.items.size()) - 1; i >= 0; --i) {
			const std::string &place = view.items[static_cast<size_t>(i)].place;
			if (i == except || view.items[static_cast<size_t>(i)].kind == "tray" || (on_bench && place != "bench" && place != "on" && place != "under")) {
				continue;
			}
			Rect r = rect(i);
			r.x -= 6;
			r.y -= 6;
			r.w += 12;
			r.h += 12;
			if (r.has(x, y)) {
				return i;
			}
		}
		return -1;
	}

	// a burner's valve: a slider beside it, while it is on the bench or under a vessel
	bool valve_rect(int index, Rect &r) const {
		const ItemView &v = view.items[static_cast<size_t>(index)];
		if (v.kind != "burner" || (v.place != "bench" && v.place != "under")) {
			return false;
		}
		const Rect body = rect(index);
		r = Rect{ body.x + body.w + 10, body.y + body.h - 84, 16, 84 };
		return true;
	}

	// a vessel's stirring button, while it stands on the bench with liquid in it
	bool stir_rect(int index, Rect &r) const {
		const ItemView &v = view.items[static_cast<size_t>(index)];
		if (v.kind != "vessel" || v.place != "bench" || !(v.liquid_ml > 0.0)) {
			return false;
		}
		const Rect c = foot(v);
		r = Rect{ c.x + c.w - 38, c.y + c.h - 26, 36, 22 }; // at its foot, on the right: clear of its mouth, its burner and its name
		return true;
	}

	Rect speed_rect(int i) const { return Rect{ 380 + static_cast<float>(i) * 84, 14, 76, 36 }; }
	static Rect pause_rect() { return Rect{ 270, 14, 96, 36 }; }
	static Rect check_rect() { return Rect{ 760, 14, 220, 36 }; }
	static Rect reset_rect() { return Rect{ 1000, 14, 96, 36 }; }
	static Rect pour_slider() { return Rect{ PANEL.x + 30, PANEL.y + 84, PANEL.w - 60, 18 }; }
	static Rect pour_button() { return Rect{ PANEL.x + 30, PANEL.y + 118, 180, 36 }; }
	static Rect cancel_button() { return Rect{ PANEL.x + PANEL.w - 126, PANEL.y + PANEL.h - 50, 96, 36 }; }
	// the solids of the vessel being emptied, each with its button
	std::vector<std::pair<std::string, Rect>> tip_buttons() const {
		std::vector<std::pair<std::string, Rect>> out;
		if (!panel || from < 0) {
			return out;
		}
		for (const SubstanceView &s : view.items[static_cast<size_t>(from)].contents) {
			if (s.phase == 's' && !s.wall && out.size() < 3) {
				out.emplace_back(s.species, Rect{ PANEL.x + 30, PANEL.y + 168 + static_cast<float>(out.size()) * 40, 260, 34 });
			}
		}
		return out;
	}

	bool act(const json &command) {
		std::string why;
		const bool ok = lab->command(command.dump(), why);
		message = ok ? std::string() : why;
		refresh();
		return ok;
	}

	const std::string &id(int index) const { return view.items[static_cast<size_t>(index)].id; }

	// ------------------------------------------------------------- the selection (§13.1)

	bool chosen(int index) const { return std::find(group.begin(), group.end(), index) != group.end(); }
	void choose_only(int index) {
		group.clear();
		if (index >= 0) {
			group.push_back(index);
		}
		selected = index;
	}
	void toggle(int index) {
		const auto at = std::find(group.begin(), group.end(), index);
		if (at != group.end()) {
			group.erase(at);
		} else {
			group.push_back(index);
		}
		selected = group.empty() ? -1 : group.back();
	}
	// a selection of several, the item in hand among them, being carried
	bool carrying_group() const { return drag >= 0 && moved && group.size() > 1 && chosen(drag); }
	Rect marquee_rect() const { return Rect{ std::fmin(press_x, px), std::fmin(press_y, py), std::fabs(px - press_x), std::fabs(py - press_y) }; }
	// what a rectangle drawn on the bench takes: everything on the bench it touches
	void take_marquee() {
		const Rect m = marquee_rect();
		if (!(mods & Ui::ModAny)) {
			choose_only(-1);
		}
		for (int i = 0; i < static_cast<int>(view.items.size()); ++i) {
			const ItemView &v = view.items[static_cast<size_t>(i)];
			if (v.place != "bench" || v.kind == "tray" || v.col < 0 || chosen(i)) {
				continue;
			}
			const Rect f = foot(v);
			if (f.x < m.x + m.w && m.x < f.x + f.w && f.y < m.y + m.h && m.y < f.y + f.h) {
				group.push_back(i);
				selected = i;
			}
		}
	}
	// where an item is drawn now: in hand, or carried with the selection
	Rect drawn(int index) const {
		Rect r = rect(index);
		if (index == drag) {
			r.x = px - grab_x;
			r.y = py - grab_y;
		} else if (carrying_group() && chosen(index) && view.items[static_cast<size_t>(index)].place == "bench") {
			r.x += px - press_x;
			r.y += py - press_y;
		}
		return r;
	}
	// letting go of a selection of several, carried by one of them
	void drop_group(int index, float x, float y) {
		if (guided()) {
			not_now(); // a tutorial asks for one item at a time
			return;
		}
		std::vector<int> standing;
		for (const int i : group) {
			if (view.items[static_cast<size_t>(i)].place == "bench") {
				standing.push_back(i);
			}
		}
		if (CABINET.has(x, y)) {
			for (const int i : standing) {
				act({ { "do", "stow" }, { "item", id(i) } });
			}
			return;
		}
		const ItemView &v = view.items[static_cast<size_t>(index)];
		if (!BENCH.has(x, y) || v.place != "bench") {
			message = "A selection moves along the bench together, or back to the cabinet; one item at a time goes anywhere else";
			return;
		}
		const int col = std::clamp(static_cast<int>((x - BENCH.x) / square()) - anchor_c, 0, std::max(0, view.cols - v.w));
		const int row = std::clamp(static_cast<int>((y - BENCH.y) / square()) - anchor_r, 0, std::max(0, view.rows - v.h));
		json names = json::array();
		for (const int i : standing) {
			names.push_back(id(i));
		}
		if (col != v.col || row != v.row) {
			act({ { "do", "move" }, { "items", names }, { "by", { col - v.col, row - v.row } } });
		}
	}

	// what letting go of an item at a point does
	void drop(int index, float x, float y) {
		const ItemView v = view.items[static_cast<size_t>(index)];
		const int target = item_at(x, y, index, true);
		// first it comes off what it is joined to
		const bool was_on = v.place == "on", was_under = v.place == "under";
		if (target >= 0 && (target == v.on || target == v.under)) {
			return; // put back where it was
		}
		if (!guided_drop(index, target, x, y)) {
			return; // back where it was
		}
		if (is_part_kind(v.kind)) {
			// a signal part: along the bench, or back to the drawer
			if (CABINET.has(x, y) || DRAWER.has(x, y)) {
				act({ { "do", "stow" }, { "item", v.id } });
				choose_only(-1);
			} else if (BENCH.has(x, y) && target < 0) {
				const int col = std::clamp(static_cast<int>((x - BENCH.x) / square()) - anchor_c, 0, view.cols - 1);
				const int row = std::clamp(static_cast<int>((y - BENCH.y) / square()) - anchor_r, 0, view.rows - 1);
				act({ { "do", "place" }, { "item", v.id }, { "at", { col, row } } });
			} else if (target >= 0 || OUTBOX.has(x, y)) {
				message = "a part stands on a free square of the bench: wire it from its port, on its right";
			}
			return;
		}
		if (was_on && !act({ { "do", "detach" }, { "item", v.id } })) {
			return;
		}
		if (was_under && !act({ { "do", "unheat" }, { "burner", v.id } })) {
			return;
		}
		if (target >= 0) {
			const ItemView t = view.items[static_cast<size_t>(target)];
			if (v.kind == "burner") {
				if (t.kind == "vessel") {
					act({ { "do", "heat" }, { "burner", v.id }, { "vessel", t.id } });
				}
			} else if (t.kind != "vessel") {
				message = "\"" + t.id + "\" takes nothing";
			} else if (v.kind == "balloon" || v.condenser) {
				act({ { "do", "attach" }, { "item", v.id }, { "to", t.id } });
			} else if (t.condenser) {
				act({ { "do", "under" }, { "item", v.id }, { "of", t.id } });
			} else {
				// a vessel over a vessel: ask what to pour or tip
				panel = true;
				from = index;
				to = target;
				pour_fraction = v.liquid_ml > 50.0 ? 50.0 / v.liquid_ml : 1.0;
			}
			return;
		}
		if (OUTBOX.has(x, y)) {
			act({ { "do", "submit" }, { "item", v.id } });
			return;
		}
		if (CABINET.has(x, y)) {
			if (view.items[static_cast<size_t>(index)].place == "bench") {
				act({ { "do", "stow" }, { "item", v.id } });
			}
			return;
		}
		if (BENCH.has(x, y)) {
			// the square under the pointer takes the square of the footprint it was held by, the whole of it on the bench
			const int col = std::clamp(static_cast<int>((x - BENCH.x) / square()) - anchor_c, 0, std::max(0, view.cols - v.w));
			const int row = std::clamp(static_cast<int>((y - BENCH.y) / square()) - anchor_r, 0, std::max(0, view.rows - v.h));
			if (!act({ { "do", "place" }, { "item", v.id }, { "at", { col, row } } }) && was_on && view.items[static_cast<size_t>(index)].drains_to >= 0) {
				message.clear(); // it came off, and stays beside the vessel: what stands under its outlet holds it there
			}
		}
	}

	void press(float x, float y) {
		if (draft.edit != Draft::None) {
			// the clock stays the player's while a part is set
			if (!pause_rect().has(x, y) && !(speed_rect(0).has(x, y) || speed_rect(1).has(x, y) || speed_rect(2).has(x, y) || speed_rect(3).has(x, y))) {
				edit_press(x, y);
				return;
			}
		}
		if (!panel && draft.edit == Draft::None && (part_press(x, y) || begin_signal_drag(x, y))) {
			return;
		}
		if (panel) {
			if (pour_slider().has(x, y) && view.items[static_cast<size_t>(from)].liquid_ml > 0.0) {
				if (guided() && !asks_click("panel:pour") && !asks_click("panel:slider@")) {
					not_now();
					return;
				}
				slider_drag = true;
				slide(x);
				return;
			}
			if (pour_button().has(x, y) && view.items[static_cast<size_t>(from)].liquid_ml > 0.0) {
				if (!allowed_click("panel:pour")) {
					return;
				}
				const double ml = view.items[static_cast<size_t>(from)].liquid_ml * pour_fraction;
				json command = { { "do", "pour" }, { "from", id(from) }, { "to", id(to) } };
				if (pour_fraction >= 1.0) {
					command["all"] = true;
				} else {
					command["ml"] = ml;
				}
				if (act(command)) {
					panel = false;
				}
				return;
			}
			for (const auto &[species, r] : tip_buttons()) {
				if (r.has(x, y)) {
					if (!allowed_click("panel:tip:" + species)) {
						return;
					}
					if (act({ { "do", "tip" }, { "from", id(from) }, { "to", id(to) }, { "species", species }, { "all", true } })) {
						panel = false;
					}
					return;
				}
			}
			if (cancel_button().has(x, y) || !PANEL.has(x, y)) {
				panel = false;
			}
			return;
		}
		if (pause_rect().has(x, y)) {
			if (paused) {
				play();
			} else {
				paused = true;
			}
			return;
		}
		for (int i = 0; i < 4; ++i) {
			if (speed_rect(i).has(x, y)) {
				speed = SPEEDS[i];
				play();
				return;
			}
		}
		if (check_rect().has(x, y)) {
			check_every_step = !check_every_step;
			return;
		}
		if (reset_rect().has(x, y)) {
			std::string why;
			std::unique_ptr<Lab> again = Lab::open(bench, why);
			if (again) {
				lab = std::move(again);
				owed = 0.0;
				message.clear();
				refresh();
				choose_only(-1);
				draft = Draft{};
				rung.clear();
				chain.clear();
				if (tutorial) {
					tutorial->restart();
				}
			}
			return;
		}
		for (int i = 0; i < static_cast<int>(view.items.size()); ++i) {
			Rect r;
			if (valve_rect(i, r)) {
				Rect wide = r;
				wide.x -= 8;
				wide.w += 16;
				wide.y -= 6;
				wide.h += 12;
				if (wide.has(x, y)) {
					if (!allowed_click("valve:" + id(i) + "@")) {
						return;
					}
					valve_drag = i;
					turn(y);
					return;
				}
			}
			if (stir_rect(i, r) && r.has(x, y)) {
				if (!allowed_click("stir:" + id(i))) {
					return;
				}
				act({ { "do", "stir" }, { "vessel", id(i) }, { "on", !view.items[static_cast<size_t>(i)].stirred } });
				return;
			}
		}
		const int hit = item_at(x, y);
		if (hit < 0 && BENCH.has(x, y)) {
			marquee = true; // a rectangle, or, let go where it began, a click on nothing
			return;
		}
		if (hit < 0 && (CABINET.has(x, y) || OUTBOX.has(x, y)) && !(mods & Ui::ModAny)) {
			choose_only(-1); // a click on nothing lets go of the selection
		}
		if (hit >= 0 && view.items[static_cast<size_t>(hit)].place == "outbox") {
			if (mods & Ui::ModAny) {
				toggle(hit);
			} else {
				choose_only(hit); // handed in: it can be read, and watched, but not moved
			}
		}
		if (hit >= 0 && view.items[static_cast<size_t>(hit)].place != "outbox") {
			// a press takes it in hand; a release without moving it selects it (§13.3),
			// and a drop the tutorial does not ask for is refused when it is let go
			const Rect r = rect(hit);
			drag = hit;
			grab_x = x - r.x;
			grab_y = y - r.y;
			moved = false;
			const ItemView &v = view.items[static_cast<size_t>(hit)];
			if (v.place == "bench") { // held by the square under the pointer
				anchor_c = std::clamp(static_cast<int>((x - BENCH.x) / square()) - v.col, 0, v.w - 1);
				anchor_r = std::clamp(static_cast<int>((y - BENCH.y) / square()) - v.row, 0, v.h - 1);
			} else { // held by its body, which lands under the pointer
				body_square(hit, anchor_c, anchor_r);
			}
		}
	}

	// ------------------------------------------------------------- signals (§13.3)

	bool is_part(int index) const { return index >= 0 && index < static_cast<int>(view.items.size()) && is_part_kind(view.items[static_cast<size_t>(index)].kind); }
	int item_index(const std::string &name) const {
		for (int i = 0; i < static_cast<int>(view.items.size()); ++i) {
			if (view.items[static_cast<size_t>(i)].id == name) {
				return i;
			}
		}
		return -1;
	}
	// "M1" for monitor1: what a part's square has room for
	static std::string short_name(const ItemView &v) {
		std::string digits;
		for (const char ch : v.id) {
			if (ch >= '0' && ch <= '9') {
				digits += ch;
			}
		}
		return std::string(1, static_cast<char>(v.kind[0] - 'a' + 'A')) + digits;
	}

	static Rect drawer_rect(int k) { return Rect{ DRAWER.x + 20 + static_cast<float>(k) * 110, DRAWER.y + 56, 90, 90 }; }
	// a part's output, on its right side; an alarm has none
	bool port_rect(int index, Rect &r) const {
		if (!is_part(index) || view.items[static_cast<size_t>(index)].kind == "alarm") {
			return false;
		}
		const Rect body = drawn(index);
		r = Rect{ body.x + body.w - 5, body.cy() - 6, 12, 12 };
		return true;
	}
	// where a wire comes into a part, and where a rig acts on a control
	void in_point(int index, float &x, float &y) const {
		const Rect body = drawn(index);
		x = body.x;
		y = body.cy();
	}
	void control_point(const std::string &item_id, const std::string &control, float &x, float &y) const {
		const int i = item_index(item_id);
		if (i < 0) {
			x = y = 0;
			return;
		}
		Rect r;
		if ((control == "valve" && valve_rect(i, r)) || (control == "stir" && stir_rect(i, r))) {
			x = r.cx();
			y = r.cy();
			return;
		}
		const Rect body = drawn(i);
		x = body.x + 4;
		y = control == "reset" ? body.y + body.h - 4 : body.y + 4;
	}

	// ---- the part editor: geometry
	Rect edit_cancel() const { return Rect{ EDITOR.x + EDITOR.w - 116, EDITOR.y + 12, 96, 32 }; }
	Rect edit_done() const { return Rect{ EDITOR.x + 20, EDITOR.y + EDITOR.h - 52, 140, 36 }; }
	// a monitor's: what it watches, its quantity, the comparison, the values
	std::vector<int> watchable() const {
		std::vector<int> out;
		for (int i = 0; i < static_cast<int>(view.items.size()); ++i) {
			const ItemView &v = view.items[static_cast<size_t>(i)];
			if (v.kind != "tray" && !is_part_kind(v.kind) && out.size() < 8) {
				out.push_back(i);
			}
		}
		return out;
	}
	Rect edit_item(int k) const { return Rect{ EDITOR.x + 20 + static_cast<float>(k) * 86, EDITOR.y + 60, 82, 30 }; }
	Rect edit_quantity() const { return Rect{ EDITOR.x + 20, EDITOR.y + 108, 330, 30 }; }
	Rect edit_measure(int k) const { return Rect{ EDITOR.x + 360 + static_cast<float>(k) * 70, EDITOR.y + 108, 64, 30 }; }
	Rect edit_compare(int k) const { return Rect{ EDITOR.x + 20 + static_cast<float>(k) * 96, EDITOR.y + 146, 92, 28 }; }
	Rect edit_field(int f) const { return Rect{ EDITOR.x + 20 + static_cast<float>(f) * 180, EDITOR.y + 182, 140, 30 }; }
	Rect edit_nudge(int k) const { // k in -3..-1, 1..3
		const int at = k < 0 ? k + 3 : k + 2;
		return Rect{ EDITOR.x + 356 + static_cast<float>(at) * 58, EDITOR.y + 182, 54, 30 };
	}
	Rect pick_entry(int i) const {
		const Rect q = edit_quantity();
		return Rect{ q.x, q.y + q.h + 2 + static_cast<float>(i) * 28, q.w, 26 };
	}
	std::vector<Quantity> pickable() const {
		const int item = item_index(draft.quantity.item);
		return item >= 0 ? quantities_of(view.items[static_cast<size_t>(item)]) : std::vector<Quantity>{};
	}
	bool species_quantity() const {
		const std::string &w = draft.quantity.what;
		return !(w == "celsius" || w == "gas_ml" || w == "condensed_ml" || w == "liquid_ml" || w == "valve" || w == "fill");
	}
	// a timer's preset
	Rect preset_field() const { return Rect{ EDITOR.x + 20, EDITOR.y + 108, 200, 30 }; }
	Rect preset_nudge(int k) const { // k in -3..-1, 1..3: a minute, ten seconds, a second
		const int at = k < 0 ? k + 3 : k + 2;
		return Rect{ EDITOR.x + 240 + static_cast<float>(at) * 64, EDITOR.y + 108, 60, 30 };
	}
	// a rig's: its form, then a row for each setting
	Rect form_button(int f) const { return Rect{ EDITOR.x + 20 + static_cast<float>(f) * 170, EDITOR.y + 60, 160, 30 }; }
	std::vector<int> sides() const { return draft.change ? std::vector<int>{ 2 } : std::vector<int>{ 0, 1 }; }
	float side_y(int side) const { return EDITOR.y + 108 + static_cast<float>(draft.change ? 0 : side) * 62; }
	std::vector<std::pair<std::string, std::optional<double>>> choices() const {
		if (draft.control == "stir") {
			return { { "stir", 1.0 }, { "stop", 0.0 }, { "leave", std::nullopt } };
		}
		if (draft.control == "reset") {
			return { { "reset", 1.0 }, { "leave", std::nullopt } };
		}
		return { { "leave", std::nullopt } }; // a valve: or a value, typed
	}
	Rect choice_rect(int side, int k) const { return Rect{ EDITOR.x + 150 + static_cast<float>(k) * 100, side_y(side), 92, 30 }; }
	Rect percent_field(int side) const { return Rect{ EDITOR.x + 250, side_y(side), 100, 30 }; }
	Rect percent_nudge(int side, int k) const { // k in -2, -1, 1, 2: ten and one per cent
		const int at = k < 0 ? k + 2 : k + 1;
		return Rect{ EDITOR.x + 380 + static_cast<float>(at) * 60, side_y(side), 56, 30 };
	}
	// a wire dropped on a timer: into its input, or onto its reset
	Rect choose_rect(int k) const { return Rect{ EDITOR.x + 20 + static_cast<float>(k) * 240, EDITOR.y + 80, 230, 36 }; }

	// a value as the editor writes it: plain, to the quantity's last shown digit
	static std::string plain(const Quantity &q, double v) {
		const char *f = q.what == "celsius" || q.what == "valve" || q.what == "fill" ? "%.1f"
				: q.what == "gas_ml" || q.what == "condensed_ml" || q.what == "liquid_ml" ? "%.2f"
				: q.measure == "count"														? "%.0f"
				: q.measure == "size"														? "%.1f"
																							: "%.3f";
		return text_of(f, v);
	}
	double now(const Quantity &q) const {
		const std::optional<double> v = measure(view, q);
		return v ? *v : 0.0;
	}
	void set_quantity(const Quantity &q) {
		draft.quantity = q;
		draft.text = { plain(q, now(q)), plain(q, now(q)) };
	}

	// ---- opening and closing it
	void open_edit(const Draft &d) {
		const bool was_open = draft.edit != Draft::None;
		draft = d;
		picking = false;
		focus = -1;
		message.clear();
		// the clock waits while it is written: at 600x the moment could pass first
		if (!was_open) {
			resume = !paused;
		}
		paused = true;
	}
	void close_edit() {
		draft = Draft{};
		picking = false;
		focus = -1;
		if (resume && rung.empty()) {
			paused = false;
		}
		resume = false;
	}
	void edit_part(int index) {
		const ItemView &v = view.items[static_cast<size_t>(index)];
		Draft d;
		d.part = v.id;
		if (v.kind == "timer") {
			d.edit = Draft::Timer;
			d.preset = clock_text(static_cast<double>(v.part.preset) / 60.0);
			open_edit(d);
			return;
		}
		if (v.kind != "monitor") {
			return;
		}
		d.edit = Draft::Monitor;
		open_edit(d);
		if (v.part.watching) {
			draft.quantity = v.part.quantity;
			draft.op = v.part.comparison.op;
			draft.text = { plain(v.part.quantity, v.part.comparison.a), plain(v.part.quantity, v.part.comparison.b) };
			return;
		}
		const std::vector<int> items = watchable();
		draft.quantity = Quantity{};
		for (const int i : items) {
			if (view.items[static_cast<size_t>(i)].place == "bench") {
				const std::vector<Quantity> qs = quantities_of(view.items[static_cast<size_t>(i)]);
				if (!qs.empty()) {
					set_quantity(qs.front());
				}
				break;
			}
		}
	}
	void edit_rig(const std::string &source, const std::string &item_id, const std::string &control) {
		Draft d;
		d.edit = Draft::Rig;
		d.part = source;
		d.item = item_id;
		d.control = control;
		if (control == "stir") {
			d.setting = { 1.0, 0.0, std::nullopt };
		} else if (control == "reset") {
			d.setting = { 1.0, std::nullopt, std::nullopt };
		}
		d.percent = { "0", "0", "0" };
		open_edit(d);
	}

	// ---- setting it: the editor's lab command
	void commit() {
		if (draft.edit == Draft::Monitor) {
			if (!allowed_click("edit:set")) {
				return;
			}
			json c = { { "do", "set" }, { "part", draft.part }, { "item", draft.quantity.item }, { "what", draft.quantity.what }, { "measure", draft.quantity.measure },
				{ "compare", OP_KEYS[static_cast<int>(draft.op)] } };
			double a = 0.0, b = 0.0;
			char *end = nullptr;
			a = std::strtod(draft.text[0].c_str(), &end);
			bool ok = !draft.text[0].empty() && end != nullptr && *end == '\0';
			if (two_values(draft.op)) {
				b = std::strtod(draft.text[1].c_str(), &end);
				ok = ok && !draft.text[1].empty() && end != nullptr && *end == '\0';
				c["values"] = { a, b };
			} else {
				c["value"] = a;
			}
			if (!ok) {
				message = "a value is not a number";
				return;
			}
			if (guided_commit(c) && act(c)) {
				close_edit();
			}
		} else if (draft.edit == Draft::Timer) {
			if (!allowed_click("edit:set")) {
				return;
			}
			double seconds = 0.0;
			if (!seconds_of(draft.preset, seconds)) {
				message = "the time is not a time: hh:mm:ss.mmm, mm:ss or seconds";
				return;
			}
			const json c = { { "do", "set" }, { "part", draft.part }, { "seconds", seconds } };
			if (guided_commit(c) && act(c)) {
				close_edit();
			}
		} else if (draft.edit == Draft::Rig) {
			if (!allowed_click("edit:set")) {
				return;
			}
			json c = { { "do", "rig" }, { "from", draft.part }, { "item", draft.item }, { "control", draft.control } };
			for (const int side : sides()) {
				const std::optional<double> &v = draft.setting[static_cast<size_t>(side)];
				json value = nullptr;
				if (v) {
					value = draft.control == "stir" ? json(*v > 0.5) : draft.control == "reset" ? json(true) : json(*v);
				}
				c[SIDES[side]] = value;
			}
			if (guided_commit(c) && act(c)) {
				close_edit();
			}
		}
	}

	// a valve's typed per cent, read into its setting
	void read_percent(int side) {
		char *end = nullptr;
		const double v = std::strtod(draft.percent[static_cast<size_t>(side)].c_str(), &end);
		if (!draft.percent[static_cast<size_t>(side)].empty() && end != nullptr && *end == '\0' && v >= 0.0 && v <= 100.0) {
			draft.setting[static_cast<size_t>(side)] = v / 100.0;
		}
	}

	void edit_press(float x, float y) {
		if (draft.edit == Draft::Monitor && picking) {
			const std::vector<Quantity> qs = pickable();
			for (int i = 0; i < static_cast<int>(qs.size()); ++i) {
				if (pick_entry(i).has(x, y) && allowed_click("edit:pick:" + qs[static_cast<size_t>(i)].what)) {
					set_quantity(qs[static_cast<size_t>(i)]);
				}
			}
			picking = false; // a click anywhere closes the list
			return;
		}
		if (edit_cancel().has(x, y)) {
			if (allowed_click("edit:cancel")) {
				close_edit();
			}
			return;
		}
		if (draft.edit != Draft::Choose && edit_done().has(x, y)) {
			commit();
			return;
		}
		if (draft.edit == Draft::Choose) {
			if (choose_rect(0).has(x, y) || choose_rect(1).has(x, y)) {
				const bool into = choose_rect(0).has(x, y);
				if (!allowed_click(into ? "choose:wire" : "choose:reset")) {
					return;
				}
				const std::string source = draft.part, timer = draft.item;
				if (into) {
					close_edit();
					act({ { "do", "wire" }, { "from", source }, { "to", timer } });
				} else {
					edit_rig(source, timer, "reset");
				}
			}
			return;
		}
		if (draft.edit == Draft::Monitor) {
			const std::vector<int> items = watchable();
			for (int k = 0; k < static_cast<int>(items.size()); ++k) {
				if (edit_item(k).has(x, y)) {
					if (!allowed_click("edit:item:" + id(items[static_cast<size_t>(k)]))) {
						return;
					}
					const std::vector<Quantity> qs = quantities_of(view.items[static_cast<size_t>(items[static_cast<size_t>(k)])]);
					if (!qs.empty()) {
						set_quantity(qs.front());
					}
					return;
				}
			}
			if (edit_quantity().has(x, y)) {
				picking = allowed_click("edit:quantity");
				return;
			}
			for (int k = 0; species_quantity() && k < 4; ++k) {
				if (edit_measure(k).has(x, y)) {
					if (!allowed_click(std::string("edit:measure:") + MEASURES[k])) {
						return;
					}
					Quantity q = draft.quantity;
					q.measure = MEASURES[k];
					set_quantity(q);
					return;
				}
			}
			for (int k = 0; k < 7; ++k) {
				if (edit_compare(k).has(x, y)) {
					if (allowed_click(std::string("edit:compare:") + OP_KEYS[k])) {
						draft.op = OPS[k];
					}
					return;
				}
			}
			for (int f = 0; f < (two_values(draft.op) ? 2 : 1); ++f) {
				if (edit_field(f).has(x, y)) {
					if (allowed_click("edit:value:" + std::to_string(f))) {
						focus = f;
						fresh = true;
					}
					return;
				}
			}
			for (int k : { -3, -2, -1, 1, 2, 3 }) {
				if (edit_nudge(k).has(x, y)) {
					if (!allowed_click("edit:nudge:" + std::to_string(k))) {
						return;
					}
					const int f = focus == 1 && two_values(draft.op) ? 1 : 0;
					std::string &field = draft.text[static_cast<size_t>(f)];
					double scale[3];
					scales(draft.quantity, scale);
					char *end = nullptr;
					double v = std::strtod(field.c_str(), &end);
					if (field.empty() || end == nullptr || *end != '\0') {
						v = now(draft.quantity);
					}
					field = plain(draft.quantity, v + (k < 0 ? -1.0 : 1.0) * scale[3 - (k < 0 ? -k : k)]);
					fresh = true;
					return;
				}
			}
		}
		if (draft.edit == Draft::Timer) {
			if (preset_field().has(x, y)) {
				if (allowed_click("edit:value")) {
					focus = 2;
					fresh = true;
				}
				return;
			}
			for (int k : { -3, -2, -1, 1, 2, 3 }) {
				if (preset_nudge(k).has(x, y)) {
					if (!allowed_click("edit:nudge:" + std::to_string(k))) {
						return;
					}
					double t = 0.0;
					if (!seconds_of(draft.preset, t)) {
						t = 300.0;
					}
					const double step = (k == 3 || k == -3) ? 60.0 : (k == 2 || k == -2) ? 10.0 : 1.0;
					draft.preset = clock_text(std::max(0.0, t + (k < 0 ? -step : step)));
					fresh = true;
					return;
				}
			}
		}
		if (draft.edit == Draft::Rig) {
			for (int f = 0; f < 2; ++f) {
				if (form_button(f).has(x, y)) {
					if (allowed_click(f == 1 ? "edit:form:change" : "edit:form:onoff")) {
						draft.change = f == 1;
						focus = -1;
					}
					return;
				}
			}
			const auto options = choices();
			for (const int side : sides()) {
				for (int k = 0; k < static_cast<int>(options.size()); ++k) {
					if (choice_rect(side, k).has(x, y)) {
						if (!allowed_click(std::string("edit:") + SIDES[side] + ":" + options[static_cast<size_t>(k)].first)) {
							return;
						}
						draft.setting[static_cast<size_t>(side)] = options[static_cast<size_t>(k)].second;
						focus = -1;
						return;
					}
				}
				if (draft.control != "valve") {
					continue;
				}
				if (percent_field(side).has(x, y)) {
					if (!allowed_click(std::string("edit:") + SIDES[side] + ":value")) {
						return;
					}
					focus = 3 + side;
					fresh = true;
					read_percent(side);
					return;
				}
				for (int k : { -2, -1, 1, 2 }) {
					if (percent_nudge(side, k).has(x, y)) {
						if (!allowed_click(std::string("edit:") + SIDES[side] + ":nudge:" + std::to_string(k))) {
							return;
						}
						std::string &field = draft.percent[static_cast<size_t>(side)];
						const double v = std::clamp(std::atof(field.c_str()) + (k < 0 ? -1.0 : 1.0) * ((k == 2 || k == -2) ? 10.0 : 1.0), 0.0, 100.0);
						field = text_of("%g", v);
						read_percent(side);
						fresh = true;
						return;
					}
				}
			}
		}
		focus = -1;
	}

	// ---- the drawer, the ports, the readout's buttons
	// the buttons the readout shows for the part selected: (name, where)
	std::vector<std::pair<std::string, Rect>> part_buttons() const {
		std::vector<std::pair<std::string, Rect>> out;
		if (!is_part(selected) || draft.edit != Draft::None) {
			return out;
		}
		const ItemView &v = view.items[static_cast<size_t>(selected)];
		const float top = (tutorial ? TUTOR.y + TUTOR.h + 30 : READOUT.y + 30) + 110;
		if (v.kind != "alarm") {
			out.emplace_back("part:edit", Rect{ READOUT.x + 16, top, 90, 30 });
		}
		if (v.kind == "timer") {
			out.emplace_back("part:reset", Rect{ READOUT.x + 116, top, 90, 30 });
		}
		float y = top + 70;
		for (const Wire &w : view.wires) {
			if (w.from == v.id) {
				out.emplace_back("part:unwire:" + w.to, Rect{ READOUT.x + READOUT.w - 60, y - 20, 44, 26 });
				y += 30;
			}
		}
		for (const Rig &r : view.rigs) {
			if (r.from == v.id) {
				out.emplace_back("part:unrig:" + r.item + ":" + r.control, Rect{ READOUT.x + READOUT.w - 60, y - 20, 44, 26 });
				y += 30;
			}
		}
		return out;
	}
	bool part_press(float x, float y) {
		for (const auto &[name, r] : part_buttons()) {
			if (!r.has(x, y)) {
				continue;
			}
			if (!allowed_click(name)) {
				return true;
			}
			const std::string part_id = id(selected);
			if (name == "part:edit") {
				edit_part(selected);
			} else if (name == "part:reset") {
				act({ { "do", "reset" }, { "timer", part_id } });
			} else if (name.rfind("part:unwire:", 0) == 0) {
				act({ { "do", "unwire" }, { "from", part_id }, { "to", name.substr(12) } });
			} else {
				const std::string rest = name.substr(11);
				const size_t colon = rest.rfind(':');
				act({ { "do", "unrig" }, { "from", part_id }, { "item", rest.substr(0, colon) }, { "control", rest.substr(colon + 1) } });
			}
			return true;
		}
		return false;
	}
	// a drag from the drawer, or from a part's port, begins
	bool begin_signal_drag(float x, float y) {
		for (int k = 0; k < 3; ++k) {
			if (drawer_rect(k).has(x, y)) {
				if (guided() && !asks_drag_from(std::string("drawer:") + PART_KINDS[k])) {
					not_now();
					return true;
				}
				making = k;
				return true;
			}
		}
		for (int i = static_cast<int>(view.items.size()) - 1; i >= 0; --i) {
			Rect r;
			if (port_rect(i, r) && Rect{ r.x - 4, r.y - 4, r.w + 8, r.h + 8 }.has(x, y)) {
				if (guided() && !asks_drag_from("port:" + id(i))) {
					not_now();
					return true;
				}
				wiring = i;
				return true;
			}
		}
		return false;
	}
	bool asks_drag_from(const std::string &what) const {
		for (const TutorialGesture &g : tutorial->gestures()) {
			if (g.drag && g.what == what) {
				return true;
			}
		}
		return false;
	}
	// where the step's drag from `what` goes, if this drop is it
	bool guided_signal_drop(const std::string &what, int target, float x, float y) {
		if (!guided()) {
			return true;
		}
		for (const TutorialGesture &g : tutorial->gestures()) {
			if (g.drag && g.what == what &&
					((g.to.rfind("item:", 0) == 0 && target >= 0 && id(target) == g.to.substr(5)) || (g.to.rfind("square:", 0) == 0 && target < 0 && BENCH.has(x, y)))) {
				return true;
			}
		}
		return not_now();
	}
	// a part let go on the bench, out of the drawer: made there, and its settings opened
	void drop_making(int kind, float x, float y) {
		if (!BENCH.has(x, y)) {
			return;
		}
		const int target = item_at(x, y, -1, true);
		if (!guided_signal_drop(std::string("drawer:") + PART_KINDS[kind], target, x, y)) {
			return;
		}
		const int col = static_cast<int>((x - BENCH.x) / square()), row = static_cast<int>((y - BENCH.y) / square());
		if (act({ { "do", "part" }, { "kind", PART_KINDS[kind] }, { "at", { col, row } } })) {
			const int made = static_cast<int>(view.items.size()) - 1;
			choose_only(made);
			edit_part(made);
		}
	}
	// a wire let go: into a part's input, or a rig onto a control
	void drop_wiring(int source, float x, float y) {
		const int target = item_at(x, y, -1, true);
		if (target < 0) {
			return; // nowhere: nothing is joined
		}
		if (!guided_signal_drop("port:" + id(source), target, x, y)) {
			return;
		}
		const ItemView &t = view.items[static_cast<size_t>(target)];
		if (t.kind == "timer") {
			Draft d;
			d.edit = Draft::Choose;
			d.part = id(source);
			d.item = t.id;
			open_edit(d);
		} else if (t.kind == "monitor" || t.kind == "alarm") {
			act({ { "do", "wire" }, { "from", id(source) }, { "to", t.id } });
		} else if (t.kind == "vessel") {
			edit_rig(id(source), t.id, "stir");
		} else if (t.kind == "burner") {
			edit_rig(id(source), t.id, "valve");
		} else {
			message = "\"" + t.id + "\" has no control to rig: a vessel's stirring, a burner's valve, a timer's reset";
		}
	}

	void type(Ui::Key key, uint32_t codepoint) {
		if (draft.edit == Draft::None) {
			if (key == Ui::KeyEscape && !panel) {
				choose_only(-1);
			}
			return;
		}
		if (key == Ui::KeyEscape) {
			if (picking) {
				picking = false;
			} else if (allowed_click("edit:cancel")) {
				close_edit();
			}
			return;
		}
		if (key == Ui::KeyEnter) {
			commit();
			return;
		}
		std::string *field = focus == 0 || focus == 1 ? &draft.text[static_cast<size_t>(focus)] : focus == 2 ? &draft.preset : focus >= 3 && focus <= 5 ? &draft.percent[static_cast<size_t>(focus - 3)] : nullptr;
		if (field == nullptr) {
			return;
		}
		if (key == Ui::KeyBackspace) {
			if (fresh) {
				field->clear();
			} else if (!field->empty()) {
				field->pop_back();
			}
			fresh = false;
		} else if ((codepoint >= '0' && codepoint <= '9') || codepoint == '.' || (codepoint == '-' && focus < 2) || (codepoint == ':' && focus == 2)) {
			const std::string typed = (fresh ? std::string() : *field) + static_cast<char>(codepoint);
			if (typed.size() <= 16 && allowed_text(typed)) {
				*field = typed;
				fresh = false;
			}
		}
		if (focus >= 3) {
			read_percent(focus - 3);
		}
	}

	// ---- an alarm rang: the clock stops, and what set it off is shown
	void ring() {
		refresh();
		paused = true;
		owed = 0.0;
		rung = view.ringing;
		chain.clear();
		std::vector<std::string> todo = rung;
		while (!todo.empty()) {
			const std::string at = todo.back();
			todo.pop_back();
			std::vector<std::string> inputs, on;
			for (const Wire &w : view.wires) {
				if (w.to == at) {
					inputs.push_back(w.from);
					const int i = item_index(w.from);
					if (i >= 0 && view.items[static_cast<size_t>(i)].part.out) {
						on.push_back(w.from);
					}
				}
			}
			for (const std::string &p : on.empty() ? inputs : on) {
				if (std::find(chain.begin(), chain.end(), p) == chain.end()) {
					chain.push_back(p);
					todo.push_back(p);
				}
			}
		}
		std::string said = rung.front() + " rang";
		for (const std::string &p : chain) {
			const int i = item_index(p);
			const Part &x = view.items[static_cast<size_t>(i)].part;
			if (x.kind == PartKind::Monitor && x.watching) {
				said += ": " + p + ", " + label(view, x.quantity) + " " + op_name(x.comparison.op) + " " + shown(x.quantity, x.comparison.a) +
						(two_values(x.comparison.op) ? " and " + shown(x.quantity, x.comparison.b) : "") + " (now " + shown(x.quantity, now(x.quantity)) + ")";
			} else if (x.kind == PartKind::Timer) {
				said += ": " + p + " at " + clock_text(static_cast<double>(x.left) / 60.0);
			}
		}
		message = said;
		if (!chain.empty()) {
			const int first = item_index(chain.front());
			const Part &x = view.items[static_cast<size_t>(first)].part;
			const int shown_item = x.kind == PartKind::Monitor ? item_index(x.quantity.item) : first;
			if (shown_item >= 0) {
				choose_only(shown_item);
			}
		}
	}

	void slide(float x) {
		const Rect r = pour_slider();
		const double f = (x - r.x) / r.w;
		pour_fraction = f < 0.02 ? 0.02 : f > 1 ? 1 : f;
	}

	void turn(float y) {
		Rect r;
		if (!valve_rect(valve_drag, r)) {
			return;
		}
		double open = 1.0 - (y - r.y) / r.h;
		open = open < 0 ? 0 : open > 1 ? 1 : open;
		act({ { "do", "valve" }, { "burner", id(valve_drag) }, { "to", open } });
	}

	// ------------------------------------------------------------- painting

	void rect_fill(const Rect &r, uint32_t color) {
		list.commands.push_back(DrawCommand{ DrawCommand::Rect, color, 1, 0, 0, r.x, r.y, r.w, r.h });
	}
	void outline(const Rect &r, uint32_t color, float width = 2) {
		list.commands.push_back(DrawCommand{ DrawCommand::Outline, color, width, 0, 0, r.x, r.y, r.w, r.h });
	}
	void line(float x1, float y1, float x2, float y2, uint32_t color, float width = 2) {
		list.commands.push_back(DrawCommand{ DrawCommand::Line, color, width, 0, 0, x1, y1, x2, y2 });
	}
	void circle(float x, float y, float r, uint32_t color) {
		list.commands.push_back(DrawCommand{ DrawCommand::Circle, color, 1, 0, 0, x, y, r, 0 });
	}
	void polygon(std::initializer_list<float> xy, uint32_t color) {
		const auto first = static_cast<int32_t>(list.points.size() / 2);
		list.points.insert(list.points.end(), xy.begin(), xy.end());
		list.commands.push_back(DrawCommand{ DrawCommand::Polygon, color, 1, first, static_cast<int32_t>(xy.size() / 2), 0, 0, 0, 0 });
	}
	void text(float x, float y, const std::string &s, uint32_t color = INK, float size = 18) {
		const auto offset = static_cast<int32_t>(list.text.size());
		list.text += s;
		list.text += '\0';
		list.commands.push_back(DrawCommand{ DrawCommand::Text, color, 1, offset, static_cast<int32_t>(s.size()), x, y, 0, size });
	}
	void button(const Rect &r, const std::string &label, bool on = false, float size = 18) {
		rect_fill(r, on ? BUTTON_ON : BUTTON);
		text(r.x + (size < 18 ? 6 : 10), r.y + r.h / 2 + size / 3, label, INK, size);
	}

	// a signal part (§13.3): its name, its state, a lamp lit while it signals, its port
	void paint_part(int index, const Rect &r) {
		const ItemView &v = view.items[static_cast<size_t>(index)];
		const Part &p = v.part;
		const bool ringing = v.kind == "alarm" && p.ringing;
		rect_fill(r, ringing ? rgba(110, 70, 30) : PANEL_FILL);
		outline(r, p.out ? GOOD : WALL_INK, p.out ? 2.0f : 1.0f);
		text(r.x + 4, r.y + 15, short_name(v), INK, 13);
		if (v.kind == "timer") {
			const auto seconds = static_cast<int64_t>(std::ceil(static_cast<double>(p.left) / 60.0));
			text(r.x + 3, r.y + r.h - 4, text_of("%.0f:", static_cast<double>(seconds / 60)) + text_of("%02.0f", static_cast<double>(seconds % 60)), DIM, 10);
		} else if (v.kind == "monitor") {
			text(r.x + 3, r.y + r.h - 4, p.watching ? op_name(p.comparison.op) : "unset", DIM, 9);
		}
		circle(r.x + r.w - 8, r.y + 8, 4, ringing ? WARN : p.out ? GOOD : BUTTON);
		Rect port;
		if (port_rect(index, port)) {
			circle(port.cx(), port.cy(), 5, p.out ? GOOD : WALL_INK);
		}
	}

	// the wires between parts, and the rigs onto controls: lit while their source signals
	void paint_signals() {
		const double pulse = 0.5 + 0.5 * std::sin(clock * 5.0);
		const uint32_t alarm_colour = (WARN & 0xffffff00u) | static_cast<uint32_t>(140.0 + 115.0 * pulse);
		const auto on = [&](const std::string &part) {
			const int i = item_index(part);
			return i >= 0 && view.items[static_cast<size_t>(i)].part.out;
		};
		const auto in_chain = [&](const std::string &part) { return std::find(chain.begin(), chain.end(), part) != chain.end(); };
		for (const Wire &w : view.wires) {
			const int a = item_index(w.from), b = item_index(w.to);
			Rect port;
			if (a < 0 || b < 0 || !port_rect(a, port)) {
				continue;
			}
			float x1 = 0, y1 = 0;
			in_point(b, x1, y1);
			const bool lit = in_chain(w.from);
			line(port.cx(), port.cy(), x1, y1, lit ? alarm_colour : on(w.from) ? GOOD : WALL_INK, lit ? 3.0f : 2.0f);
		}
		for (const Rig &r : view.rigs) {
			const int a = item_index(r.from);
			Rect port;
			if (a < 0 || !port_rect(a, port)) {
				continue;
			}
			float x1 = 0, y1 = 0;
			control_point(r.item, r.control, x1, y1);
			line(port.cx(), port.cy(), x1, y1, on(r.from) ? rgba(90, 140, 230) : rgba(110, 130, 160, 200), 2);
			circle(x1, y1, 4, rgba(90, 140, 230));
		}
		// a wire in hand, and a part out of the drawer
		Rect port;
		if (wiring >= 0 && port_rect(wiring, port)) {
			line(port.cx(), port.cy(), px, py, GUIDE, 2);
		}
		if (making >= 0) {
			const Rect ghost{ px - 18, py - 18, 36, 36 };
			rect_fill(ghost, PANEL_FILL);
			outline(ghost, GUIDE, 2);
			text(ghost.x + 4, ghost.y + 15, std::string(1, static_cast<char>(PART_KINDS[making][0] - 'a' + 'A')), INK, 13);
		}
		// what set an alarm off, pulsing, until the clock plays
		for (const std::string &part : chain) {
			const int i = item_index(part);
			if (i < 0) {
				continue;
			}
			const Rect r = drawn(i);
			outline(Rect{ r.x - 6, r.y - 6, r.w + 12, r.h + 12 }, alarm_colour, 3);
			const Part &x = view.items[static_cast<size_t>(i)].part;
			const int watched = x.kind == PartKind::Monitor ? item_index(x.quantity.item) : -1;
			if (watched >= 0) {
				const Rect w = drawn(watched);
				outline(Rect{ w.x - 10, w.y - 10, w.w + 20, w.h + 20 }, alarm_colour, 4);
			}
		}
		for (const std::string &alarm : rung) {
			const int i = item_index(alarm);
			if (i >= 0) {
				const Rect r = drawn(i);
				outline(Rect{ r.x - 8, r.y - 8, r.w + 16, r.h + 16 }, alarm_colour, 4);
			}
		}
	}

	void paint_drawer() {
		rect_fill(DRAWER, PANEL_FILL);
		outline(DRAWER, GRID, 1);
		text(DRAWER.x + 14, DRAWER.y + 26, "parts: drag one onto the bench", INK, 17);
		static const char *const SAYS[] = { "counts down", "watches a value", "stops the clock" };
		for (int k = 0; k < 3; ++k) {
			const Rect r = drawer_rect(k);
			rect_fill(r, BUTTON);
			outline(r, GRID, 1);
			text(r.x + 10, r.y + 30, PART_KINDS[k], INK, 17);
			text(r.x + 10, r.y + 56, std::string(1, static_cast<char>(PART_KINDS[k][0] - 'a' + 'A')), DIM, 22);
			text(r.x, r.y + r.h + 22, SAYS[k], DIM, 13);
		}
		text(DRAWER.x + 14, DRAWER.y + 210, "wire a part from its port, on its right:", DIM, 14);
		text(DRAWER.x + 14, DRAWER.y + 232, "into another part, or onto a control to rig it", DIM, 14);
		text(DRAWER.x + 14, DRAWER.y + 254, "(a vessel's stirring, a burner's valve, a timer's reset)", DIM, 14);
	}

	void paint_item(int index, Rect r) {
		const ItemView &v = view.items[static_cast<size_t>(index)];
		if (is_part_kind(v.kind)) {
			paint_part(index, r);
			return;
		}
		if (v.kind == "burner") {
			rect_fill(r, rgba(96, 96, 96));
			if (v.valve > 0.0) {
				const float flame = static_cast<float>(12.0 + 30.0 * v.valve);
				polygon({ r.cx() - 12, r.y, r.cx() + 12, r.y, r.cx(), r.y - flame }, mix(rgba(240, 200, 60), rgba(80, 140, 255), v.valve));
			}
		} else if (v.kind == "balloon") {
			// all of it is the optical pass's: rubber, and what condensed inside
		} else {
			// what is in it is the optical pass's (§12); here, the glass: open at the top
			if (v.liquid_ml > 0.05) {
				// and a liquid's surface, until refraction shows a clear one (Stage 11)
				const float level = static_cast<float>(std::fmin(1.0, v.condensed_ml / (v.volume_ml > 0.0 ? v.volume_ml : 1.0))) * r.h;
				line(r.x + 1, r.y + r.h - level, r.x + r.w - 1, r.y + r.h - level, rgba(74, 86, 98, 110), 1);
			}
			line(r.x, r.y, r.x, r.y + r.h, WALL_GLASS);
			line(r.x, r.y + r.h, r.x + r.w, r.y + r.h, WALL_GLASS);
			line(r.x + r.w, r.y + r.h, r.x + r.w, r.y, WALL_GLASS);
			if (v.condenser) {
				line(r.x + r.w, r.y + r.h - 10, r.x + r.w + 26, r.y + r.h + 14, WALL_GLASS); // its outlet
			}
		}
		const std::string label = v.kind == "burner" ? v.id : v.id + text_of("  %.0f C", v.celsius);
		if (index == drag) {
			return;
		}
		if (v.place == "on") {
			text(r.x, r.y - 8, label, WALL_INK, 15);
		} else if (v.place == "under") {
			// under a vessel it needs no name: its valve is beside it
		} else if (v.place == "bench") {
			const Rect c = foot(v);
			text(c.x + 2, c.y + c.h - 10, label, WALL_INK, 14);
		} else {
			text(r.x, r.y + r.h + 18, label, WALL_INK, 15);
		}
	}

	void paint() {
		list.commands.clear();
		list.points.clear();
		list.text.clear();
		list.above = 0;
		rect_fill(Rect{ 0, 0, 1920, 1080 }, BACKGROUND);
		rect_fill(ROOM, WALL);
		// the bench and its grid
		const float sq = square();
		for (int c = 0; c <= view.cols; ++c) {
			line(BENCH.x + static_cast<float>(c) * sq, BENCH.y, BENCH.x + static_cast<float>(c) * sq, BENCH.y + static_cast<float>(view.rows) * sq, WALL_GRID, 1);
		}
		for (int r = 0; r <= view.rows; ++r) {
			line(BENCH.x, BENCH.y + static_cast<float>(r) * sq, BENCH.x + static_cast<float>(view.cols) * sq, BENCH.y + static_cast<float>(r) * sq, WALL_GRID, 1);
		}
		outline(CABINET, WALL_GRID, 1);
		outline(OUTBOX, WALL_GRID, 1);
		// ---- the optical pass goes here: what is in the vessels and the balloons
		list.above = static_cast<int32_t>(list.commands.size());
		text(CABINET.x + 16, CABINET.y + 24, "cabinet", WALL_INK);
		text(OUTBOX.x + 16, OUTBOX.y + 26, "outbox: drop here to hand in", WALL_INK);
		// the clock
		button(pause_rect(), paused ? "play" : "pause", paused);
		for (int i = 0; i < 4; ++i) {
			button(speed_rect(i), text_of("%.0fx", SPEEDS[i]), !paused && speed == SPEEDS[i]);
		}
		button(check_rect(), check_every_step ? "check every step: on" : "check every step: off", check_every_step);
		button(reset_rect(), "reset");
		const double seconds = static_cast<double>(view.step) / 60.0;
		text(1120, 40, text_of("%02.0f:%04.1f", std::floor(seconds / 60.0), seconds - 60.0 * std::floor(seconds / 60.0)), INK, 22);
		// the items, those in hand last
		for (int i = 0; i < static_cast<int>(view.items.size()); ++i) {
			if (view.items[static_cast<size_t>(i)].kind == "tray" || i == drag) {
				continue;
			}
			paint_item(i, drawn(i));
			Rect r;
			if (valve_rect(i, r)) {
				rect_fill(r, BUTTON);
				const float knob = r.y + r.h * static_cast<float>(1.0 - view.items[static_cast<size_t>(i)].valve);
				rect_fill(Rect{ r.x - 4, knob - 4, r.w + 8, 8 }, INK);
			}
			if (stir_rect(i, r)) {
				button(r, "stir", view.items[static_cast<size_t>(i)].stirred, 14);
			}
			// what stands under a condenser's outlet is joined to it by a drip
			const ItemView &v = view.items[static_cast<size_t>(i)];
			if (v.drains_to >= 0) {
				const Rect a = rect(i), b = rect(v.drains_to);
				line(a.x + a.w + 26, a.y + a.h + 14, b.cx(), b.y, rgba(70, 120, 190, 200), 1);
			}
		}
		if (drag >= 0) {
			paint_item(drag, drawn(drag));
		}
		for (const int i : group) {
			if (i != drag) {
				const Rect r = drawn(i);
				outline(Rect{ r.x - 6, r.y - 6, r.w + 12, r.h + 12 }, WALL_INK, i == selected ? 2.0f : 1.0f);
			}
		}
		if (marquee && down) {
			outline(marquee_rect(), WALL_INK, 1);
		}
		if (tutorial) {
			paint_tutorial();
		}
		paint_signals();
		paint_readout();
		paint_drawer();
		if (panel) {
			paint_panel();
		}
		if (draft.edit != Draft::None) {
			paint_editor();
		}
		if (tutorial) {
			paint_guide();
		}
	}

	bool locate(std::string_view what, float &x, float &y) const;

	// the signals' names on the screen (Ui::locate): the drawer, ports, the editor, the readout's buttons
	bool signal_rect(std::string_view what, Rect &out) const {
		const auto number = [](std::string_view t) { return std::atoi(std::string(t).c_str()); };
		const auto after = [&](std::string_view prefix, std::string_view &rest) {
			if (what.substr(0, prefix.size()) != prefix) {
				return false;
			}
			rest = what.substr(prefix.size());
			return true;
		};
		const auto found = [&](const Rect &r) {
			out = r;
			return true;
		};
		std::string_view rest;
		if (after("drawer:", rest)) {
			for (int k = 0; k < 3; ++k) {
				if (rest == PART_KINDS[k]) {
					return found(drawer_rect(k));
				}
			}
			return false;
		}
		if (after("port:", rest)) {
			Rect r;
			return port_rect(item_index(std::string(rest)), r) && found(r);
		}
		if (what.substr(0, 5) == "part:") {
			for (const auto &[name, r] : part_buttons()) {
				if (name == what) {
					return found(r);
				}
			}
			return false;
		}
		if (after("choose:", rest)) {
			return draft.edit == Draft::Choose && (rest == "wire" || rest == "reset") && found(choose_rect(rest == "wire" ? 0 : 1));
		}
		if (!after("edit:", rest) || draft.edit == Draft::None || draft.edit == Draft::Choose) {
			return false;
		}
		if (rest == "cancel") {
			return found(edit_cancel());
		}
		if (rest == "set") {
			return found(edit_done());
		}
		std::string_view more;
		const auto sub = [&](std::string_view prefix) {
			if (rest.substr(0, prefix.size()) != prefix) {
				return false;
			}
			more = rest.substr(prefix.size());
			return true;
		};
		if (draft.edit == Draft::Monitor) {
			if (picking) {
				const std::vector<Quantity> qs = pickable();
				for (int i = 0; sub("pick:") && i < static_cast<int>(qs.size()); ++i) {
					if (qs[static_cast<size_t>(i)].what == more) {
						return found(pick_entry(i));
					}
				}
				return false;
			}
			if (sub("item:")) {
				const std::vector<int> items = watchable();
				for (int k = 0; k < static_cast<int>(items.size()); ++k) {
					if (id(items[static_cast<size_t>(k)]) == more) {
						return found(edit_item(k));
					}
				}
				return false;
			}
			if (rest == "quantity") {
				return !draft.quantity.item.empty() && found(edit_quantity());
			}
			if (sub("measure:")) {
				for (int k = 0; species_quantity() && k < 4; ++k) {
					if (more == MEASURES[k]) {
						return found(edit_measure(k));
					}
				}
				return false;
			}
			if (sub("compare:")) {
				for (int k = 0; k < 7; ++k) {
					if (more == OP_KEYS[k]) {
						return found(edit_compare(k));
					}
				}
				return false;
			}
			if (sub("value:")) {
				const int f = number(more);
				return (f == 0 || (f == 1 && two_values(draft.op))) && found(edit_field(f));
			}
			if (sub("nudge:")) {
				const int k = number(more);
				return k != 0 && k >= -3 && k <= 3 && found(edit_nudge(k));
			}
			return false;
		}
		if (draft.edit == Draft::Timer) {
			if (rest == "value") {
				return found(preset_field());
			}
			if (sub("nudge:")) {
				const int k = number(more);
				return k != 0 && k >= -3 && k <= 3 && found(preset_nudge(k));
			}
			return false;
		}
		// a rig: edit:form:<onoff|change>, edit:<on|off|change>:<stir|stop|reset|leave|value|nudge:k>
		if (rest == "form:onoff" || rest == "form:change") {
			return found(form_button(rest == "form:change" ? 1 : 0));
		}
		const size_t colon = rest.find(':');
		if (colon == std::string_view::npos) {
			return false;
		}
		int side = -1;
		for (int k = 0; k < 3; ++k) {
			side = rest.substr(0, colon) == SIDES[k] ? k : side;
		}
		const std::vector<int> open_sides = sides();
		if (side < 0 || std::find(open_sides.begin(), open_sides.end(), side) == open_sides.end()) {
			return false;
		}
		const std::string_view choice = rest.substr(colon + 1);
		const auto options = choices();
		for (int k = 0; k < static_cast<int>(options.size()); ++k) {
			if (choice == options[static_cast<size_t>(k)].first) {
				return found(choice_rect(side, k));
			}
		}
		if (draft.control == "valve" && choice == "value") {
			return found(percent_field(side));
		}
		if (draft.control == "valve" && choice.substr(0, 6) == "nudge:") {
			const int k = number(choice.substr(6));
			return (k == -2 || k == -1 || k == 1 || k == 2) && found(percent_nudge(side, k));
		}
		return false;
	}

	// ------------------------------------------------------------- the tutorial (§13.2)

	// the card: the step, what to do and why, its checks; or the result
	void paint_tutorial() {
		const TutorialView &t = tutorial->view();
		rect_fill(TUTOR, PANEL_FILL);
		outline(TUTOR, GRID, 1);
		const float x = TUTOR.x + 18, right = TUTOR.x + TUTOR.w - 18;
		float y = TUTOR.y + 30;
		const auto lines = [&](const std::string &s, size_t width, uint32_t color, float size, float gap) {
			for (const std::string &l : wrapped(s, width)) {
				text(x, y, l, color, size);
				y += size + gap;
			}
		};
		text(x, y, t.part, DIM, 15);
		const int shown_step = t.step < t.steps ? t.step + 1 : t.steps;
		text(right - 110, y, text_of("step %.0f of %.0f", shown_step, t.steps), DIM, 15);
		y += 12;
		const float bar = right - x;
		rect_fill(Rect{ x, y, bar, 6 }, BUTTON);
		rect_fill(Rect{ x, y, bar * static_cast<float>(t.step) / static_cast<float>(t.steps > 0 ? t.steps : 1), 6 }, t.state == TutorialView::Failed ? BAD : GOOD);
		y += 40;
		if (t.state == TutorialView::Failed) {
			text(x, y, "This run cannot be completed", BAD, 22);
			y += 36;
			lines(t.failure, 44, INK, 17, 8);
			return;
		}
		if (t.state == TutorialView::Complete) {
			text(x, y, "Tutorial complete", GOOD, 24);
			y += 36;
			lines("Everything was made and handed in. What was measured:", 50, INK, 16, 7);
			y += 4;
			for (const std::string &r : t.results) {
				lines(r, 54, DIM, 15, 6);
			}
			y += 10;
		} else {
			lines(t.title, 40, INK, 22, 10);
			lines(t.text, 46, INK, 17, 7);
			if (!t.why.empty()) {
				y += 4;
				lines(t.why, 54, DIM, 15, 6);
			}
			y += 4;
			text(x, y + 14, !tutorial->waiting() ? "The clock waits for you." : paused ? "The clock is paused: press play, or a speed." : "The clock runs: 60x or 600x hurries it.", WARN, 15);
			y += 38;
		}
		for (const TutorialCheck &c : t.checks) {
			const Rect tick{ x, y - 13, 14, 14 };
			if (c.done) {
				rect_fill(tick, GOOD);
			} else {
				outline(tick, INK, 1);
			}
			std::string label = c.text;
			if (!c.done && !c.now.empty()) {
				label += "  (now " + c.now + ")";
			}
			for (const std::string &l : wrapped(label, 52)) {
				text(x + 24, y, l, c.done ? GOOD : INK, 16);
				y += 22;
			}
		}
		if (t.state == TutorialView::Complete) {
			y += 8;
			text(x, y, "Reset (at the top) plays it again.", DIM, 15);
		}
	}

	// where a named thing is drawn: an item's body, a cell, a button; a small box around a point otherwise
	bool box(std::string_view what, Rect &r) const {
		float x = 0, y = 0;
		if (!locate(what, x, y)) {
			return false;
		}
		Rect t;
		if (signal_rect(what, t)) {
			r = Rect{ t.x - 4, t.y - 4, t.w + 8, t.h + 8 };
			return true;
		}
		if (what.substr(0, 5) == "item:") {
			for (int i = 0; i < static_cast<int>(view.items.size()); ++i) {
				if (view.items[static_cast<size_t>(i)].id == what.substr(5)) {
					const Rect body = rect(i);
					r = Rect{ body.x - 8, body.y - 8, body.w + 16, body.h + 16 };
					return true;
				}
			}
		}
		if (what == "cabinet") {
			r = CABINET;
			return true;
		}
		if (what == "outbox") {
			r = OUTBOX;
			return true;
		}
		if (what.substr(0, 7) == "square:") {
			r = square_rect(static_cast<int>((x - BENCH.x) / square()), static_cast<int>((y - BENCH.y) / square()));
			return true;
		}
		if (what.substr(0, 6) != "valve:" && what.substr(0, 13) != "panel:slider@") {
			std::vector<Rect> buttons{ pause_rect(), check_rect(), reset_rect() };
			for (int i = 0; i < 4; ++i) {
				buttons.push_back(speed_rect(i));
			}
			if (panel) {
				buttons.push_back(pour_button());
				buttons.push_back(cancel_button());
				for (const auto &[species, b] : tip_buttons()) {
					buttons.push_back(b);
				}
			}
			for (int i = 0; i < static_cast<int>(view.items.size()); ++i) {
				Rect b;
				if (stir_rect(i, b)) {
					buttons.push_back(b);
				}
			}
			for (const Rect &b : buttons) {
				if (b.has(x, y)) {
					r = Rect{ b.x - 4, b.y - 4, b.w + 8, b.h + 8 };
					return true;
				}
			}
		}
		r = Rect{ x - 20, y - 14, 40, 28 };
		return true;
	}

	// the pointer at what the step asks for: the last of its gestures that is on the screen now
	// Whether the part editor already shows what a step's gesture does (§13.2): a
	// choice chosen, a field holding the text the next gesture types. "set" is
	// never done: it is what the step ends with.
	bool done_in_editor(const std::vector<TutorialGesture> &gestures, size_t i) const {
		const std::string &w = gestures[i].what;
		const std::string *typed = i + 1 < gestures.size() && !gestures[i + 1].type.empty() ? &gestures[i + 1].type : nullptr;
		const auto after = [&](const std::string &prefix, std::string &rest) {
			if (w.rfind(prefix, 0) != 0) {
				return false;
			}
			rest = w.substr(prefix.size());
			return true;
		};
		std::string rest;
		if (draft.edit == Draft::Monitor) {
			if (after("edit:item:", rest)) {
				return draft.quantity.item == rest;
			}
			if (w == "edit:quantity") { // done once the list is open, or the quantity picked after it is
				return picking || (i + 1 < gestures.size() && gestures[i + 1].what == "edit:pick:" + draft.quantity.what);
			}
			if (after("edit:pick:", rest)) {
				return draft.quantity.what == rest;
			}
			if (after("edit:measure:", rest)) {
				return draft.quantity.measure == rest;
			}
			if (after("edit:compare:", rest)) {
				for (int k = 0; k < 7; ++k) {
					if (rest == OP_KEYS[k]) {
						return draft.op == OPS[k];
					}
				}
				return false;
			}
			if (after("edit:value:", rest)) {
				return typed != nullptr && draft.text[static_cast<size_t>(rest == "1" ? 1 : 0)] == *typed;
			}
		}
		if (draft.edit == Draft::Timer && w == "edit:value") {
			return typed != nullptr && draft.preset == *typed;
		}
		if (draft.edit == Draft::Rig) {
			if (w == "edit:form:onoff" || w == "edit:form:change") {
				return draft.change == (w == "edit:form:change");
			}
			for (int side = 0; side < 3; ++side) {
				if (!after(std::string("edit:") + SIDES[side] + ":", rest)) {
					continue;
				}
				const std::optional<double> &now_set = draft.setting[static_cast<size_t>(side)];
				if (rest == "value") {
					char *end = nullptr;
					const double v = typed != nullptr ? std::strtod(typed->c_str(), &end) : -1.0;
					return typed != nullptr && draft.percent[static_cast<size_t>(side)] == *typed && now_set && std::fabs(*now_set - v / 100.0) < 1e-12;
				}
				for (const auto &[name, value] : choices()) {
					if (rest == name) {
						return now_set == value;
					}
				}
			}
		}
		return false;
	}

	void paint_guide() {
		const std::vector<TutorialGesture> &gestures = tutorial->gestures();
		const double pulse = 0.5 + 0.5 * std::sin(clock * 5.0);
		const uint32_t colour = (GUIDE & 0xffffff00u) | static_cast<uint32_t>(140.0 + 115.0 * pulse);
		// in the part editor: the first of the step's gestures it does not show done yet
		if (draft.edit != Draft::None) {
			for (size_t i = 0; i < gestures.size(); ++i) {
				Rect a;
				if (gestures[i].type.empty() && !gestures[i].drag && gestures[i].what.rfind("edit:", 0) == 0 && !done_in_editor(gestures, i) && box(gestures[i].what, a)) {
					outline(a, colour, 3);
					return;
				}
			}
		}
		for (auto g = gestures.rbegin(); g != gestures.rend(); ++g) {
			Rect a, b;
			if (!box(g->what, a) || (g->drag && !box(g->to, b))) {
				continue;
			}
			if (drag >= 0 && g->drag) {
				outline(b, colour, 3); // the item is in hand: only where it goes
				return;
			}
			outline(a, colour, 3);
			if (g->drag) {
				outline(b, colour, 3);
				const float x0 = a.cx(), y0 = a.cy(), x1 = b.cx(), y1 = b.cy();
				const float dx = x1 - x0, dy = y1 - y0, length = std::sqrt(dx * dx + dy * dy);
				if (length > 30) {
					const float ux = dx / length, uy = dy / length, tip_x = x1 - ux * 10, tip_y = y1 - uy * 10;
					line(x0, y0, tip_x - ux * 16, tip_y - uy * 16, colour, 4);
					polygon({ tip_x, tip_y, tip_x - ux * 22 - uy * 11, tip_y - uy * 22 + ux * 11, tip_x - ux * 22 + uy * 11, tip_y - uy * 22 - ux * 11 }, colour);
				}
			}
			return;
		}
	}

	// the bodies the optical pass shows, as the screen places them: in painting order
	void compose() {
		std::vector<OpticsBody> bodies;
		const auto add = [&](int i, const Rect &r) {
			const ItemView &v = view.items[static_cast<size_t>(i)];
			if (v.kind == "vessel" || v.kind == "balloon") {
				bodies.push_back(OpticsBody{ &v, r.x, r.y, r.w, r.h });
			}
		};
		for (int i = 0; i < static_cast<int>(view.items.size()); ++i) {
			if (i != drag) {
				add(i, drawn(i));
			}
		}
		if (drag >= 0) {
			add(drag, drawn(drag));
		}
		// what is drawn on the wall behind them (GAME_SPEC §12.3): the draw list's first part, as flat greys
		OpticsRoom room;
		room.x = ROOM.x;
		room.y = ROOM.y;
		room.w = ROOM.w;
		room.h = ROOM.h;
		room.bench = WALL_RADIANCE;
		room.elsewhere = DARK_RADIANCE;
		paint();
		const auto radiance = [](uint32_t c) {
			const auto lin = [](double v) { return v <= 0.04045 ? v / 12.92 : std::pow((v + 0.055) / 1.055, 2.4); };
			return 0.2126 * lin(((c >> 24) & 0xff) / 255.0) + 0.7152 * lin(((c >> 16) & 0xff) / 255.0) + 0.0722 * lin(((c >> 8) & 0xff) / 255.0);
		};
		for (int i = 0; i < list.above; ++i) {
			const DrawCommand &c = list.commands[static_cast<size_t>(i)];
			const double r = radiance(c.color);
			if (c.type == DrawCommand::Rect) {
				room.shapes.push_back(OpticsShape{ c.x, c.y, c.w, c.h, r });
			} else if (c.type == DrawCommand::Outline) {
				room.shapes.push_back(OpticsShape{ c.x, c.y, c.w, c.width, r });
				room.shapes.push_back(OpticsShape{ c.x, c.y + c.h - c.width, c.w, c.width, r });
				room.shapes.push_back(OpticsShape{ c.x, c.y, c.width, c.h, r });
				room.shapes.push_back(OpticsShape{ c.x + c.w - c.width, c.y, c.width, c.h, r });
			} else if (c.type == DrawCommand::Line && (c.x == c.w || c.y == c.h)) { // straight across or up: a thin rectangle
				const float x0 = std::min(c.x, c.w), y0 = std::min(c.y, c.h);
				room.shapes.push_back(OpticsShape{ c.x == c.w ? x0 - c.width / 2 : x0, c.y == c.h ? y0 - c.width / 2 : y0, c.x == c.w ? c.width : std::fabs(c.w - c.x), c.y == c.h ? c.width : std::fabs(c.h - c.y), r });
			}
		}
		optics_compose(scene, room, bodies);
	}

	void paint_readout() {
		float y = tutorial ? TUTOR.y + TUTOR.h + 30 : READOUT.y + 30;
		const auto row = [&](const std::string &s, uint32_t color = INK, float size = 17) {
			text(READOUT.x + 16, y, s, color, size);
			y += size + 8;
		};
		outline(READOUT, GRID, 1);
		row(view.conserved ? "every element, mass and energy: unchanged" : "THE TOTALS MOVED", view.conserved ? rgba(120, 200, 140) : rgba(240, 90, 90));
		row(text_of("world: %.3f g, %.3f kJ", view.mass_g, view.energy_j / 1000.0), DIM, 15);
		y += 10;
		const int showing = drag >= 0 ? drag : selected >= 0 ? selected : hover;
		if (showing >= 0 && is_part(showing)) {
			const ItemView &v = view.items[static_cast<size_t>(showing)];
			const Part &p = v.part;
			row(v.id + (p.out ? ": signalling" : ": quiet"), p.out ? GOOD : INK, 20);
			if (v.kind == "monitor") {
				row(p.watching ? label(view, p.quantity) + " " + op_name(p.comparison.op) + " " + shown(p.quantity, p.comparison.a) +
								(two_values(p.comparison.op) ? " and " + shown(p.quantity, p.comparison.b) : "")
							   : "watches nothing yet: edit it",
						DIM, 15);
				row(p.watching ? "now " + shown(p.quantity, now(p.quantity)) : "", DIM, 15);
			} else if (v.kind == "timer") {
				row(clock_text(static_cast<double>(p.left) / 60.0) + " left of " + clock_text(static_cast<double>(p.preset) / 60.0), DIM, 15);
				row(p.left == 0 ? "at zero: on until it is reset" : "", DIM, 15);
			} else {
				row(p.ringing ? "ringing: play on to answer it" : "rings when its input turns on", p.ringing ? WARN : DIM, 15);
				row("", DIM, 15);
			}
			const auto buttons = showing == selected ? part_buttons() : std::vector<std::pair<std::string, Rect>>{};
			for (const auto &[name, r] : buttons) {
				if (name == "part:edit" || name == "part:reset") {
					button(r, name == "part:edit" ? "edit" : "reset");
				}
			}
			y += 46;
			std::vector<std::string> inputs;
			for (const Wire &w : view.wires) {
				if (w.to == v.id) {
					inputs.push_back(w.from);
				}
			}
			std::string named = "inputs: ";
			for (size_t i = 0; i < inputs.size(); ++i) {
				named += (i > 0 ? ", " : "") + inputs[i];
			}
			row(inputs.empty() ? "no inputs: it signals freely" : named + " (any on lets it signal)", DIM, 15);
			for (const Wire &w : view.wires) {
				if (w.from == v.id) {
					row("wired into " + w.to, INK, 15);
					y += 5;
				}
			}
			for (const Rig &r : view.rigs) {
				if (r.from == v.id) {
					const auto say = [&](const std::optional<double> &x) {
						return !x ? std::string("leave") : r.control == "valve" ? text_of("%.0f%%", 100.0 * *x) : r.control == "stir" ? (*x > 0.5 ? "stir" : "stop") : std::string("reset");
					};
					row("rigged to " + r.item + "'s " + r.control + ": " + (r.change ? "on a change " + say(r.any) : "on " + say(r.on) + ", off " + say(r.off)), INK, 15);
					y += 5;
				}
			}
			for (const auto &[name, r] : buttons) {
				if (name.rfind("part:unwire:", 0) == 0 || name.rfind("part:unrig:", 0) == 0) {
					button(r, " x");
				}
			}
		} else if (showing >= 0) {
			const ItemView &v = view.items[static_cast<size_t>(showing)];
			row(v.id + " (" + v.kind + (v.condenser ? ", condenser" : "") + ", " + v.place + ")", INK, 20);
			for (const ControlSetting &c : view.controls) {
				if (c.item == v.id) {
					row(c.control + (c.control == "valve" ? text_of(" %.0f%%", 100.0 * c.value) : c.value > 0.5 ? " on" : " off") + ", set by " + c.by +
									text_of(" at %.1f s", static_cast<double>(c.step) / 60.0),
							DIM, 14);
				}
			}
			if (v.kind == "burner") {
				row(text_of("flame %.0f C, valve %.0f%%", v.celsius, 100.0 * v.valve));
			} else {
				row(text_of("%.2f C", v.celsius));
				row(v.kind == "vessel" ? text_of("%.1f mL gas, %.2f mL solids and liquids", v.gas_ml, v.condensed_ml)
									   : text_of("%.1f mL of %.0f mL", v.gas_ml + v.condensed_ml, v.capacity_ml));
				for (const SubstanceView &s : v.contents) {
					if (s.wall) {
						continue;
					}
					const std::string phase = s.phase == 'a' ? "aq" : std::string(1, s.phase);
					std::string line = s.formula + "(" + phase + ")" + text_of("  %.4f g", s.g);
					if (s.particles > 0) {
						line += text_of("  %.0f particles, %.0f um", static_cast<double>(s.particles), s.size_um);
					}
					row(line, DIM, 15);
				}
			}
		} else {
			row("point at something to read it", DIM);
		}
		y += 10;
		for (const std::string &e : view.events) {
			row(e, WARN, 15);
		}
		if (!message.empty()) {
			// wrapped at about 52 characters
			for (size_t at = 0; at < message.size(); at += 52) {
				text(READOUT.x + 16, READOUT.y + READOUT.h - 120 + static_cast<float>(at / 52) * 24, message.substr(at, 52), WARN, 16);
			}
		}
	}

	void paint_editor() {
		rect_fill(EDITOR, PANEL_FILL);
		outline(EDITOR, GLASS, 1);
		button(edit_cancel(), "cancel");
		const auto field = [&](const Rect &r, const std::string &value, bool focused) {
			rect_fill(r, focused ? rgba(30, 30, 30) : rgba(24, 24, 24));
			outline(r, focused ? GUIDE : GRID, focused ? 2.0f : 1.0f);
			text(r.x + 8, r.y + r.h / 2 + 6, value + (focused && !fresh ? "_" : ""), INK, 17);
		};
		const auto nudge_label = [](double step) {
			std::string amount = text_of(step >= 1.0 ? "%.0f" : "%g", step);
			return amount.rfind("0.", 0) == 0 ? amount.substr(1) : amount; // ".01"
		};
		if (draft.edit == Draft::Choose) {
			text(EDITOR.x + 20, EDITOR.y + 40, "from " + draft.part + " to the timer " + draft.item + ":", INK, 22);
			button(choose_rect(0), "its input (a wire)");
			button(choose_rect(1), "its reset (a rig)");
			text(EDITOR.x + 20, EDITOR.y + 150, "A wire lets the timer signal only while this part does.", DIM, 15);
			text(EDITOR.x + 20, EDITOR.y + 174, "A rig resets it when this part's signal changes as you say.", DIM, 15);
			return;
		}
		button(edit_done(), draft.edit == Draft::Rig ? "rig" : "set");
		text(edit_done().x + 160, edit_done().y + 24, "click a value to type it; enter sets, escape cancels", DIM, 14);
		if (draft.edit == Draft::Monitor) {
			text(EDITOR.x + 20, EDITOR.y + 40, draft.part + " watches", INK, 22);
			const std::vector<int> items = watchable();
			for (int k = 0; k < static_cast<int>(items.size()); ++k) {
				button(edit_item(k), id(items[static_cast<size_t>(k)]), id(items[static_cast<size_t>(k)]) == draft.quantity.item, 14);
			}
			if (draft.quantity.item.empty()) {
				text(EDITOR.x + 20, EDITOR.y + 128, "choose what it watches", DIM, 15);
				return;
			}
			const std::string full = label(view, draft.quantity);
			button(edit_quantity(), full.substr(full.find(": ") + 2) + "  v");
			for (int k = 0; species_quantity() && k < 4; ++k) {
				button(edit_measure(k), MEASURES[k], draft.quantity.measure == MEASURES[k]);
			}
			for (int k = 0; k < 7; ++k) {
				button(edit_compare(k), op_name(OPS[k]), draft.op == OPS[k]);
			}
			const int fields = two_values(draft.op) ? 2 : 1;
			for (int f = 0; f < fields; ++f) {
				field(edit_field(f), draft.text[static_cast<size_t>(f)], focus == f);
			}
			if (fields == 2) {
				text(edit_field(0).x + 146, edit_field(0).y + 21, "and", DIM, 15);
			}
			double scale[3];
			scales(draft.quantity, scale);
			for (int k : { -3, -2, -1, 1, 2, 3 }) {
				button(edit_nudge(k), (k < 0 ? "-" : "+") + nudge_label(scale[3 - (k < 0 ? -k : k)]));
			}
			text(edit_field(0).x, edit_field(0).y + 50, "now " + shown(draft.quantity, now(draft.quantity)), DIM, 14);
			if (picking) {
				const std::vector<Quantity> qs = pickable();
				for (int i = 0; i < static_cast<int>(qs.size()); ++i) {
					const Rect r = pick_entry(i);
					rect_fill(r, rgba(20, 20, 20));
					outline(r, GRID, 1);
					const std::string name = label(view, qs[static_cast<size_t>(i)]);
					text(r.x + 8, r.y + 19, name.substr(name.find(": ") + 2), INK, 15);
				}
			}
		} else if (draft.edit == Draft::Timer) {
			text(EDITOR.x + 20, EDITOR.y + 40, draft.part + " counts down from", INK, 22);
			field(preset_field(), draft.preset, focus == 2);
			const char *labels[] = { "-1m", "-10s", "-1s", "+1s", "+10s", "+1m" };
			int i = 0;
			for (int k : { -3, -2, -1, 1, 2, 3 }) {
				button(preset_nudge(k), labels[i++]);
			}
			text(EDITOR.x + 20, EDITOR.y + 170, "At 00:00:00.000 it signals, until it is reset: by hand, or by a rig.", DIM, 15);
		} else if (draft.edit == Draft::Rig) {
			text(EDITOR.x + 20, EDITOR.y + 40, draft.part + " rigged to the " + draft.control + " of " + draft.item, INK, 22);
			button(form_button(0), "when on / when off", !draft.change);
			button(form_button(1), "on any change", draft.change);
			static const char *const SAYS[] = { "when on", "when off", "on a change" };
			const auto options = choices();
			for (const int side : sides()) {
				text(EDITOR.x + 20, side_y(side) + 21, SAYS[side], INK, 16);
				const std::optional<double> &now_set = draft.setting[static_cast<size_t>(side)];
				for (int k = 0; k < static_cast<int>(options.size()); ++k) {
					button(choice_rect(side, k), options[static_cast<size_t>(k)].first, options[static_cast<size_t>(k)].second == now_set);
				}
				if (draft.control == "valve") {
					field(percent_field(side), draft.percent[static_cast<size_t>(side)] + " %", focus == 3 + side);
					const char *labels[] = { "-10", "-1", "+1", "+10" };
					int i = 0;
					for (int k : { -2, -1, 1, 2 }) {
						button(percent_nudge(side, k), labels[i++]);
					}
				}
			}
			text(EDITOR.x + 20, EDITOR.y + 236, "The control keeps what the last event set: a rig's, or your own hand's.", DIM, 14);
		}
	}

	void paint_panel() {
		const ItemView &source = view.items[static_cast<size_t>(from)];
		rect_fill(PANEL, PANEL_FILL);
		outline(PANEL, GLASS, 1);
		text(PANEL.x + 30, PANEL.y + 40, source.id + " into " + id(to), INK, 22);
		if (source.liquid_ml > 0.0) {
			const Rect s = pour_slider();
			rect_fill(s, BUTTON);
			rect_fill(Rect{ s.x, s.y, s.w * static_cast<float>(pour_fraction), s.h }, rgba(90, 140, 220));
			text(s.x, s.y - 8, text_of("%.1f mL of %.1f mL", source.liquid_ml * pour_fraction, source.liquid_ml), DIM, 15);
			button(pour_button(), pour_fraction >= 1.0 ? "pour it all" : "pour");
		} else {
			text(PANEL.x + 30, PANEL.y + 100, "no liquid in it", DIM, 15);
		}
		for (const auto &[species, r] : tip_buttons()) {
			button(r, "tip in the " + species);
		}
		button(cancel_button(), "cancel");
	}
};

Ui::Ui(std::unique_ptr<Impl> impl) :
		impl_(std::move(impl)) {}
Ui::~Ui() = default;

std::unique_ptr<Ui> Ui::open(std::string_view bench_json, std::string &why) {
	auto impl = std::make_unique<Impl>();
	impl->lab = Lab::open(bench_json, why);
	if (!impl->lab) {
		return nullptr;
	}
	impl->bench = std::string(bench_json);
	impl->refresh();
	return std::unique_ptr<Ui>(new Ui(std::move(impl)));
}

void Ui::pointer(float x, float y, bool down, int mods) {
	Impl &m = *impl_;
	const bool pressed = down && !m.down, released = !down && m.down;
	m.mods = mods;
	if (pressed) {
		m.press_x = x;
		m.press_y = y;
	}
	if (m.drag >= 0 && (std::fabs(x - m.px) > 0 || std::fabs(y - m.py) > 0)) {
		m.moved = true;
	}
	m.px = x;
	m.py = y;
	m.down = down;
	if (pressed) {
		m.press(x, y);
	} else if (down) {
		if (m.slider_drag) {
			m.slide(x);
		} else if (m.valve_drag >= 0) {
			m.turn(y);
		}
	} else if (released) {
		if (m.making >= 0) {
			const int kind = m.making;
			m.making = -1;
			m.drop_making(kind, x, y);
		} else if (m.wiring >= 0) {
			const int from = m.wiring;
			m.wiring = -1;
			m.drop_wiring(from, x, y);
		} else if (m.drag >= 0 && m.moved) {
			const int index = m.drag;
			const bool several = m.carrying_group();
			m.drag = -1;
			if (several) {
				m.drop_group(index, x, y);
			} else {
				m.drop(index, x, y);
			}
		} else if (m.drag >= 0) {
			// a click on it, without moving it, chooses it (§13.1); with a modifier, adds or takes it away
			if (mods & ModAny) {
				m.toggle(m.drag);
			} else {
				m.choose_only(m.drag);
			}
		} else if (m.marquee) {
			if (std::fabs(x - m.press_x) > 3 || std::fabs(y - m.press_y) > 3) {
				m.take_marquee();
			} else if (!(mods & ModAny)) {
				m.choose_only(-1); // a click on nothing lets go of the selection
			}
		}
		m.marquee = false;
		m.drag = -1;
		m.valve_drag = -1;
		m.slider_drag = false;
	}
	if (!down) {
		const int at = m.item_at(x, y);
		if (at >= 0) {
			m.hover = at;
		}
	}
	m.follow();
}

void Ui::frame(double seconds) {
	Impl &m = *impl_;
	if (!m.paused && !m.lab->failed() && seconds > 0.0) {
		m.owed += seconds * m.speed * 60.0;
		// no more than twenty simulated seconds a frame: beyond it the clock runs slow, never the picture
		auto steps = static_cast<int64_t>(m.owed < 1200.0 ? m.owed : 1200.0);
		m.owed = m.owed < 1200.0 ? m.owed - static_cast<double>(steps) : 0.0;
		std::string why;
		bool ok = true, first = true;
		// one step of every frame against every invariant; all of them when asked; and
		// the lab stops at the end of a step in which an alarm rang (§13.3)
		const auto run = [&](int64_t n) {
			m.lab->set_checking(first || m.check_every_step);
			ok = m.lab->advance(first && !m.check_every_step ? 1 : n, why, true);
			m.lab->set_checking(false);
			if (ok && first && !m.check_every_step && n > 1 && !m.lab->rang()) {
				ok = m.lab->advance(n - 1, why, true);
			}
			first = false;
			if (ok && m.lab->rang()) {
				m.ring();
			}
		};
		if (!m.tutorial) {
			if (steps > 0) {
				run(steps);
			}
		} else {
			// a tutorial's clock stands while its step asks for a gesture (§13.2): the lab
			// advances a second at a time, and stops in the second where a wait ends
			while (ok && steps > 0 && !m.held() && !m.paused) {
				const int64_t n = steps < 60 ? steps : 60;
				run(n);
				steps -= n;
				m.refresh();
				m.follow();
			}
		}
		if (m.held() || m.paused) {
			m.owed = 0.0;
		}
		if (!ok) {
			m.message = why;
		}
	}
	m.clock += seconds > 0.0 ? seconds : 0.0;
	m.refresh();
	m.follow();
}

const DrawList &Ui::draw() {
	impl_->paint();
	return impl_->list;
}

const OpticsScene &Ui::optics() {
	impl_->compose();
	return impl_->scene;
}

Lab &Ui::lab() {
	return *impl_->lab;
}

bool Ui::tutor(std::string_view tutorial_json, std::string &why) {
	Impl &m = *impl_;
	std::unique_ptr<Tutorial> t = Tutorial::open(tutorial_json, m.view, why);
	if (!t) {
		return false;
	}
	m.tutorial = std::move(t);
	m.follow();
	return true;
}

const Tutorial *Ui::tutorial() const {
	return impl_->tutorial.get();
}

std::string Ui::selected() const {
	return impl_->selected >= 0 ? impl_->id(impl_->selected) : std::string();
}

std::vector<std::string> Ui::selection() const {
	std::vector<std::string> out;
	for (const int i : impl_->group) {
		out.push_back(impl_->id(i));
	}
	return out;
}

void Ui::key(Key key, uint32_t codepoint) {
	impl_->type(key, codepoint);
	impl_->follow();
}

const std::string &Ui::message() const {
	return impl_->message;
}

double Ui::speed() const {
	return impl_->speed;
}

bool Ui::paused() const {
	return impl_->paused;
}

bool Ui::locate(std::string_view what, float &x, float &y) const {
	return impl_->locate(what, x, y);
}

bool Ui::Impl::locate(std::string_view what, float &x, float &y) const {
	const Impl &m = *this;
	const auto centre = [&](const Rect &r) {
		x = r.cx();
		y = r.cy();
		return true;
	};
	const auto index_of = [&](std::string_view name) {
		for (int i = 0; i < static_cast<int>(m.view.items.size()); ++i) {
			if (m.view.items[static_cast<size_t>(i)].id == name) {
				return i;
			}
		}
		return -1;
	};
	const auto after = [&](std::string_view prefix, std::string_view &rest) {
		if (what.substr(0, prefix.size()) != prefix) {
			return false;
		}
		rest = what.substr(prefix.size());
		return true;
	};
	std::string_view rest;
	if (what == "cabinet") {
		x = CABINET.cx();
		y = CABINET.y + CABINET.h - 60;
		return true;
	}
	if (what == "outbox") {
		x = OUTBOX.x + OUTBOX.w - 80;
		y = OUTBOX.cy();
		return true;
	}
	if (what == "pause") {
		return centre(Impl::pause_rect());
	}
	if (what == "check") {
		return centre(Impl::check_rect());
	}
	if (what == "reset") {
		return centre(Impl::reset_rect());
	}
	if (after("speed:", rest)) {
		for (int i = 0; i < 4; ++i) {
			if (rest == text_of("%.0f", SPEEDS[i])) {
				return centre(m.speed_rect(i));
			}
		}
		return false;
	}
	if (after("item:", rest)) {
		const int i = index_of(rest);
		return i >= 0 && m.view.items[static_cast<size_t>(i)].kind != "tray" && centre(m.rect(i));
	}
	if (after("square:", rest)) {
		const size_t comma = rest.find(',');
		if (comma == std::string_view::npos) {
			return false;
		}
		const int col = std::atoi(std::string(rest.substr(0, comma)).c_str()), row = std::atoi(std::string(rest.substr(comma + 1)).c_str());
		if (col < 0 || col >= m.view.cols || row < 0 || row >= m.view.rows) {
			return false;
		}
		const Rect c = m.square_rect(col, row);
		x = c.cx();
		y = c.cy();
		return true;
	}
	if (after("valve:", rest)) {
		const size_t at = rest.find('@');
		const int i = index_of(rest.substr(0, at));
		Rect r;
		if (i < 0 || at == std::string_view::npos || !m.valve_rect(i, r)) {
			return false;
		}
		const double open = std::atof(std::string(rest.substr(at + 1)).c_str());
		x = r.cx();
		y = r.y + r.h * static_cast<float>(1.0 - open);
		return true;
	}
	if (after("stir:", rest)) {
		const int i = index_of(rest);
		Rect r;
		return i >= 0 && m.stir_rect(i, r) && centre(r);
	}
	Rect signal;
	if (m.signal_rect(what, signal)) {
		return centre(signal);
	}
	if (!m.panel) {
		return false;
	}
	if (what == "panel:pour") {
		return centre(Impl::pour_button());
	}
	if (what == "panel:cancel") {
		return centre(Impl::cancel_button());
	}
	if (after("panel:slider@", rest)) {
		const Rect r = Impl::pour_slider();
		x = r.x + r.w * static_cast<float>(std::atof(std::string(rest).c_str()));
		y = r.cy();
		return true;
	}
	if (after("panel:tip:", rest)) {
		for (const auto &[species, r] : m.tip_buttons()) {
			if (species == rest) {
				return centre(r);
			}
		}
	}
	return false;
}

} // namespace lab
