#include "lab/ui.hpp"

#include <nlohmann/json.hpp>

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
constexpr Rect TIMERS{ 920, 760, 460, 300 }; // the player's timers (§13.3), beside the outbox
constexpr Rect EDITOR{ 440, 76, 720, 680 }; // a timer being written
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
	// the player's timers (§13.3): the item selected, the editor, what fired
	Timers timers;
	int selected = -1;
	bool editor = false;
	TimerDraft draft;
	int picking = -1; // the condition whose quantity list is open
	int focus_c = -1, focus_f = -1; // the focused field: a condition's value, or (-1, 2) the time limit
	bool fresh = false; // the next key replaces the focused field
	int fired = -1; // the timer that paused the clock, until play
	bool resume = false; // the clock was playing when the editor opened

	void refresh() { view = lab->view(); }
	void follow() {
		if (tutorial) {
			TutorialScreen screen;
			screen.speed = speed;
			screen.broken = lab->failed();
			screen.paused = paused;
			screen.selected = selected >= 0 ? id(selected) : std::string();
			screen.draft = editor ? &draft : nullptr;
			screen.timers = &timers.list();
			tutorial->follow(view, screen);
		}
	}
	void play() {
		paused = false;
		fired = -1;
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
		r = Rect{ c.x + 4, c.y + 6, 44, 24 }; // the footprint's corner, clear of what stands on the vessel's mouth
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
		if (editor) {
			// the clock stays the player's while a timer is written
			if (!pause_rect().has(x, y) && !(speed_rect(0).has(x, y) || speed_rect(1).has(x, y) || speed_rect(2).has(x, y) || speed_rect(3).has(x, y))) {
				editor_press(x, y);
				return;
			}
		}
		if (!panel && !editor && timers_press(x, y)) {
			return;
		}
		Rect add;
		if (!panel && add_rect(add) && add.has(x, y)) {
			open_editor(selected);
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
				timers = Timers{};
				selected = -1;
				editor = false;
				fired = -1;
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
		if (hit < 0 && (BENCH.has(x, y) || CABINET.has(x, y) || OUTBOX.has(x, y))) {
			selected = -1; // a click on nothing lets go of the selection
		}
		if (hit >= 0 && view.items[static_cast<size_t>(hit)].place == "outbox") {
			selected = hit; // handed in: it can be read, and watched, but not moved
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

	// ------------------------------------------------------------- timers (§13.3)

	// the readout's "add timer", while an item is selected
	bool add_rect(Rect &r) const {
		if (selected < 0 || selected >= static_cast<int>(view.items.size()) || view.items[static_cast<size_t>(selected)].kind == "tray") {
			return false;
		}
		const float top = tutorial ? TUTOR.y + TUTOR.h + 30 : READOUT.y + 30;
		r = Rect{ READOUT.x + READOUT.w - 146, top + 58 - 24, 130, 32 };
		return true;
	}

	float cond_y(int c) const { return EDITOR.y + 104 + static_cast<float>(c) * 130; }
	Rect cond_quantity(int c) const { return Rect{ EDITOR.x + 20, cond_y(c), 330, 30 }; }
	Rect cond_measure(int c, int k) const { return Rect{ EDITOR.x + 360 + static_cast<float>(k) * 70, cond_y(c), 64, 30 }; }
	Rect cond_remove(int c) const { return Rect{ EDITOR.x + EDITOR.w - 56, cond_y(c), 36, 30 }; }
	Rect cond_compare(int c, int k) const { return Rect{ EDITOR.x + 20 + static_cast<float>(k) * 96, cond_y(c) + 38, 92, 28 }; }
	Rect cond_field(int c, int f) const { return Rect{ EDITOR.x + 20 + static_cast<float>(f) * 180, cond_y(c) + 74, 140, 30 }; }
	Rect cond_nudge(int c, int k) const { // k in -3..-1, 1..3
		const int at = k < 0 ? k + 3 : k + 2;
		return Rect{ EDITOR.x + 356 + static_cast<float>(at) * 58, cond_y(c) + 74, 54, 30 };
	}
	Rect add_condition() const { return Rect{ EDITOR.x + 20, cond_y(static_cast<int>(draft.conditions.size())), 200, 30 }; }
	float limit_y() const { return cond_y(static_cast<int>(draft.conditions.size())) + 50; }
	Rect limit_toggle() const { return Rect{ EDITOR.x + 20, limit_y(), 170, 30 }; }
	Rect limit_field() const { return Rect{ EDITOR.x + 200, limit_y(), 110, 30 }; }
	Rect limit_nudge(int k) const { // k in -2..-1, 1..2
		const int at = k < 0 ? k + 2 : k + 1;
		return Rect{ EDITOR.x + 320 + static_cast<float>(at) * 64, limit_y(), 60, 30 };
	}
	Rect any_button() const { return Rect{ EDITOR.x + 140, EDITOR.y + 56, 70, 30 }; }
	Rect all_button() const { return Rect{ EDITOR.x + 216, EDITOR.y + 56, 70, 30 }; }
	Rect arm_button() const { return Rect{ EDITOR.x + 20, EDITOR.y + EDITOR.h - 52, 140, 36 }; }
	Rect editor_cancel() const { return Rect{ EDITOR.x + EDITOR.w - 116, EDITOR.y + 12, 96, 32 }; }
	std::vector<Quantity> pickable() const {
		const int item = item_index(draft.item);
		return item >= 0 ? quantities_of(view.items[static_cast<size_t>(item)]) : std::vector<Quantity>{};
	}
	Rect pick_entry(int i) const {
		const Rect q = cond_quantity(picking < 0 ? 0 : picking);
		return Rect{ q.x, q.y + q.h + 2 + static_cast<float>(i) * 28, q.w, 26 };
	}
	static constexpr Comparison::Op OPS[] = { Comparison::Above, Comparison::Below, Comparison::AtLeast, Comparison::AtMost, Comparison::Reaches, Comparison::Between, Comparison::Outside };
	static constexpr const char *OP_KEYS[] = { "above", "below", "at_least", "at_most", "reaches", "between", "outside" };
	static constexpr const char *MEASURES[] = { "g", "count", "size" };

	Rect row_rect(int n) const { return Rect{ TIMERS.x + 10, TIMERS.y + 40 + static_cast<float>(n) * 52, TIMERS.w - 20, 48 }; }
	Rect row_rearm(int n) const { const Rect r = row_rect(n); return Rect{ r.x + r.w - 126, r.y + 4, 70, 26 }; }
	Rect row_remove(int n) const { const Rect r = row_rect(n); return Rect{ r.x + r.w - 50, r.y + 4, 46, 26 }; }

	int item_index(const std::string &name) const {
		for (int i = 0; i < static_cast<int>(view.items.size()); ++i) {
			if (view.items[static_cast<size_t>(i)].id == name) {
				return i;
			}
		}
		return -1;
	}

	// a value as the editor writes it: plain, to the quantity's last shown digit
	static std::string plain(const Quantity &q, double v) {
		const char *f = q.what == "celsius" || q.what == "valve" ? "%.1f" : q.what == "gas_ml" || q.what == "condensed_ml" || q.what == "liquid_ml" ? "%.2f" : q.measure == "count" ? "%.0f" : q.measure == "size" ? "%.1f" : "%.3f";
		return text_of(f, v);
	}
	double now(const Quantity &q) const {
		const std::optional<double> v = measure(view, q);
		return v ? *v : 0.0;
	}
	void set_quantity(int c, const Quantity &q) {
		TimerCondition &cond = draft.conditions[static_cast<size_t>(c)];
		cond.quantity = q;
		draft.text[static_cast<size_t>(c)] = { plain(q, now(q)), plain(q, now(q)) };
	}

	void open_editor(int item) {
		const std::vector<Quantity> qs = quantities_of(view.items[static_cast<size_t>(item)]);
		draft = TimerDraft{};
		draft.item = id(item);
		if (!qs.empty()) {
			draft.conditions.push_back(TimerCondition{ qs.front(), Comparison{} });
			draft.text.push_back({ "", "" });
			set_quantity(0, qs.front());
		}
		editor = true;
		picking = -1;
		focus_c = focus_f = -1;
		message.clear();
		// the clock waits while a timer is written: at 600x the moment could pass first
		resume = !paused;
		paused = true;
	}
	void close_editor() {
		editor = false;
		picking = -1;
		if (resume && fired < 0) {
			paused = false;
		}
		resume = false;
	}
	void focus(int c, int f) {
		focus_c = c;
		focus_f = f;
		fresh = true;
	}
	std::string *focused() {
		if (focus_c == -1 && focus_f == 2) {
			return &draft.limit_text;
		}
		if (focus_c >= 0 && focus_c < static_cast<int>(draft.text.size()) && focus_f >= 0 && focus_f < 2) {
			return &draft.text[static_cast<size_t>(focus_c)][static_cast<size_t>(focus_f)];
		}
		return nullptr;
	}
	void nudge(int c, int k) {
		TimerCondition &cond = draft.conditions[static_cast<size_t>(c)];
		const int f = focus_c == c && focus_f >= 0 && focus_f < 2 ? focus_f : 0;
		std::string &field = draft.text[static_cast<size_t>(c)][static_cast<size_t>(f)];
		double scale[3];
		scales(cond.quantity, scale);
		char *end = nullptr;
		double v = std::strtod(field.c_str(), &end);
		if (field.empty() || end == nullptr || *end != '\0') {
			v = now(cond.quantity);
		}
		v += (k < 0 ? -1.0 : 1.0) * scale[3 - (k < 0 ? -k : k)];
		field = plain(cond.quantity, v);
		fresh = true;
	}
	void nudge_limit(int k) {
		double t = 0.0;
		if (!Timers::seconds_of(draft.limit_text, t)) {
			t = 300.0;
		}
		t += (k < 0 ? -1.0 : 1.0) * ((k == 2 || k == -2) ? 60.0 : 10.0);
		draft.limit_text = Timers::clock_of(t < 10.0 ? 10.0 : t);
	}
	void arm() {
		std::string why;
		if (timers.arm(draft, view, why)) {
			close_editor();
			message.clear();
		} else {
			message = why;
		}
	}

	void editor_press(float x, float y) {
		if (picking >= 0) {
			const std::vector<Quantity> qs = pickable();
			for (int i = 0; i < static_cast<int>(qs.size()); ++i) {
				if (pick_entry(i).has(x, y)) {
					set_quantity(picking, qs[static_cast<size_t>(i)]);
				}
			}
			picking = -1; // a click anywhere closes the list
			return;
		}
		if (editor_cancel().has(x, y)) {
			close_editor();
			return;
		}
		if (arm_button().has(x, y)) {
			arm();
			return;
		}
		if (any_button().has(x, y) || all_button().has(x, y)) {
			draft.all = all_button().has(x, y);
			return;
		}
		const int n = static_cast<int>(draft.conditions.size());
		for (int c = 0; c < n; ++c) {
			TimerCondition &cond = draft.conditions[static_cast<size_t>(c)];
			if (cond_quantity(c).has(x, y)) {
				picking = c;
				return;
			}
			if (cond_remove(c).has(x, y)) {
				draft.conditions.erase(draft.conditions.begin() + c);
				draft.text.erase(draft.text.begin() + c);
				focus_c = focus_f = -1;
				return;
			}
			const bool species = cond.quantity.what != "celsius" && cond.quantity.what != "gas_ml" && cond.quantity.what != "condensed_ml" && cond.quantity.what != "liquid_ml" &&
					cond.quantity.what != "valve";
			for (int k = 0; species && k < 3; ++k) {
				if (cond_measure(c, k).has(x, y)) {
					Quantity q = cond.quantity;
					q.measure = MEASURES[k];
					set_quantity(c, q);
					return;
				}
			}
			for (int k = 0; k < 7; ++k) {
				if (cond_compare(c, k).has(x, y)) {
					cond.comparison.op = OPS[k];
					return;
				}
			}
			for (int f = 0; f < (two_values(cond.comparison.op) ? 2 : 1); ++f) {
				if (cond_field(c, f).has(x, y)) {
					focus(c, f);
					return;
				}
			}
			for (int k : { -3, -2, -1, 1, 2, 3 }) {
				if (cond_nudge(c, k).has(x, y)) {
					nudge(c, k);
					return;
				}
			}
		}
		if (n < Timers::MAX_CONDITIONS && add_condition().has(x, y)) {
			const std::vector<Quantity> qs = pickable();
			if (!qs.empty()) {
				draft.conditions.push_back(TimerCondition{ qs.front(), Comparison{} });
				draft.text.push_back({ "", "" });
				set_quantity(n, qs.front());
			}
			return;
		}
		if (limit_toggle().has(x, y)) {
			draft.limited = !draft.limited;
			return;
		}
		if (draft.limited && limit_field().has(x, y)) {
			focus(-1, 2);
			return;
		}
		for (int k : { -2, -1, 1, 2 }) {
			if (draft.limited && limit_nudge(k).has(x, y)) {
				nudge_limit(k);
				return;
			}
		}
		focus_c = focus_f = -1;
	}

	bool timers_press(float x, float y) {
		for (int n = 0; n < static_cast<int>(timers.list().size()); ++n) {
			if (row_rearm(n).has(x, y)) {
				timers.rearm(n, view);
				fired = fired == n ? -1 : fired;
				return true;
			}
			if (row_remove(n).has(x, y)) {
				timers.remove(n);
				fired = fired == n ? -1 : fired > n ? fired - 1 : fired;
				return true;
			}
		}
		return TIMERS.has(x, y);
	}

	void type(Ui::Key key, uint32_t codepoint) {
		if (!editor) {
			return;
		}
		if (key == Ui::KeyEscape) {
			if (picking >= 0) {
				picking = -1;
			} else {
				close_editor();
			}
			return;
		}
		if (key == Ui::KeyEnter) {
			arm();
			return;
		}
		std::string *field = focused();
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
			return;
		}
		const bool limit = focus_c == -1;
		if ((codepoint >= '0' && codepoint <= '9') || codepoint == '.' || (codepoint == '-' && !limit) || (codepoint == ':' && limit)) {
			if (fresh) {
				field->clear();
			}
			fresh = false;
			if (field->size() < 16) {
				field->push_back(static_cast<char>(codepoint));
			}
		}
	}

	// a timer fired: the clock stops, and the item it watched is shown
	void fire(int n) {
		paused = true;
		owed = 0.0;
		fired = n;
		const Timer &t = timers.list()[static_cast<size_t>(n)];
		const int item = item_index(t.item_fired);
		if (item >= 0) {
			selected = item;
		}
		message = text_of("timer %.0f fired: ", n + 1) + t.fired_by;
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
	void button(const Rect &r, const std::string &label, bool on = false) {
		rect_fill(r, on ? BUTTON_ON : BUTTON);
		text(r.x + 10, r.y + r.h / 2 + 6, label);
	}

	void paint_item(int index, Rect r) {
		const ItemView &v = view.items[static_cast<size_t>(index)];
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
			text(c.x + 4, c.y + c.h - 10, label, WALL_INK, 15);
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
			paint_item(i, rect(i));
			Rect r;
			if (valve_rect(i, r)) {
				rect_fill(r, BUTTON);
				const float knob = r.y + r.h * static_cast<float>(1.0 - view.items[static_cast<size_t>(i)].valve);
				rect_fill(Rect{ r.x - 4, knob - 4, r.w + 8, 8 }, INK);
			}
			if (stir_rect(i, r)) {
				button(r, "stir", view.items[static_cast<size_t>(i)].stirred);
			}
			// what stands under a condenser's outlet is joined to it by a drip
			const ItemView &v = view.items[static_cast<size_t>(i)];
			if (v.drains_to >= 0) {
				const Rect a = rect(i), b = rect(v.drains_to);
				line(a.x + a.w + 26, a.y + a.h + 14, b.cx(), b.y, rgba(70, 120, 190, 200), 1);
			}
		}
		if (drag >= 0) {
			Rect r = rect(drag);
			r.x = px - grab_x;
			r.y = py - grab_y;
			paint_item(drag, r);
		}
		if (selected >= 0 && selected != drag) {
			const Rect r = rect(selected);
			outline(Rect{ r.x - 6, r.y - 6, r.w + 12, r.h + 12 }, WALL_INK, 1);
		}
		if (tutorial) {
			paint_tutorial();
		}
		paint_readout();
		paint_timers();
		if (panel) {
			paint_panel();
		}
		if (editor) {
			paint_editor();
		}
		paint_fired();
		if (tutorial) {
			paint_guide();
		}
	}

	bool locate(std::string_view what, float &x, float &y) const;

	// the timers' names on the screen (Ui::locate, after "timer:"): where each is drawn
	bool locate_timer(std::string_view what, float &x, float &y) const {
		Rect r;
		if (!timer_rect(what, r)) {
			return false;
		}
		x = r.cx();
		y = r.cy();
		return true;
	}
	bool timer_rect(std::string_view what, Rect &out) const {
		const auto centre = [&](const Rect &r) {
			out = r;
			return true;
		};
		const auto number = [](std::string_view s) { return std::atoi(std::string(s).c_str()); };
		const auto after = [&](std::string_view prefix, std::string_view &rest) {
			if (what.substr(0, prefix.size()) != prefix) {
				return false;
			}
			rest = what.substr(prefix.size());
			return true;
		};
		std::string_view rest;
		if (what == "add") {
			Rect r;
			return !editor && add_rect(r) && centre(r);
		}
		if (after("row:", rest)) {
			const size_t colon = rest.find(':');
			const int n = number(rest.substr(0, colon));
			if (colon == std::string_view::npos || n < 0 || n >= static_cast<int>(timers.list().size())) {
				return false;
			}
			return rest.substr(colon + 1) == "rearm" ? centre(row_rearm(n)) : rest.substr(colon + 1) == "remove" && centre(row_remove(n));
		}
		if (!editor) {
			return false;
		}
		const int n = static_cast<int>(draft.conditions.size());
		if (what == "cancel") {
			return centre(editor_cancel());
		}
		if (what == "arm") {
			return centre(arm_button());
		}
		if (what == "any" || what == "all") {
			return centre(what == "any" ? any_button() : all_button());
		}
		if (what == "condition:add") {
			return n < Timers::MAX_CONDITIONS && centre(add_condition());
		}
		if (what == "limit:toggle") {
			return centre(limit_toggle());
		}
		if (what == "limit:value") {
			return draft.limited && centre(limit_field());
		}
		if (after("limit:nudge:", rest)) {
			const int k = number(rest);
			return draft.limited && (k == -2 || k == -1 || k == 1 || k == 2) && centre(limit_nudge(k));
		}
		if (after("pick:", rest)) {
			const std::vector<Quantity> qs = pickable();
			for (int i = 0; picking >= 0 && i < static_cast<int>(qs.size()); ++i) {
				if (qs[static_cast<size_t>(i)].what == rest) {
					return centre(pick_entry(i));
				}
			}
			return false;
		}
		// the rest name a condition: <kind>:<c>[:<more>]
		const size_t colon = what.find(':');
		if (colon == std::string_view::npos) {
			return false;
		}
		const std::string_view kind = what.substr(0, colon);
		rest = what.substr(colon + 1);
		const size_t second = rest.find(':');
		const int c = number(rest.substr(0, second));
		const std::string_view more = second == std::string_view::npos ? std::string_view() : rest.substr(second + 1);
		if (c < 0 || c >= n || picking >= 0) {
			return false;
		}
		if (kind == "quantity") {
			return centre(cond_quantity(c));
		}
		if (kind == "remove") {
			return centre(cond_remove(c));
		}
		if (kind == "measure") {
			for (int k = 0; k < 3; ++k) {
				if (more == MEASURES[k]) {
					return centre(cond_measure(c, k));
				}
			}
			return false;
		}
		if (kind == "compare") {
			for (int k = 0; k < 7; ++k) {
				if (more == OP_KEYS[k]) {
					return centre(cond_compare(c, k));
				}
			}
			return false;
		}
		if (kind == "value") {
			const int f = number(more);
			return (f == 0 || (f == 1 && two_values(draft.conditions[static_cast<size_t>(c)].comparison.op))) && centre(cond_field(c, f));
		}
		if (kind == "nudge") {
			const int k = number(more);
			return k != 0 && k >= -3 && k <= 3 && centre(cond_nudge(c, k));
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
		if (what.substr(0, 6) == "timer:") {
			Rect t;
			if (timer_rect(what.substr(6), t)) {
				r = Rect{ t.x - 4, t.y - 4, t.w + 8, t.h + 8 };
				return true;
			}
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
	void paint_guide() {
		const std::vector<TutorialGesture> &gestures = tutorial->gestures();
		const double pulse = 0.5 + 0.5 * std::sin(clock * 5.0);
		const uint32_t colour = (GUIDE & 0xffffff00u) | static_cast<uint32_t>(140.0 + 115.0 * pulse);
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
				add(i, rect(i));
			}
		}
		if (drag >= 0) {
			Rect r = rect(drag);
			r.x = px - grab_x;
			r.y = py - grab_y;
			add(drag, r);
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
		const int shown = drag >= 0 ? drag : selected >= 0 ? selected : hover;
		if (shown >= 0) {
			const ItemView &v = view.items[static_cast<size_t>(shown)];
			row(v.id + " (" + v.kind + (v.condenser ? ", condenser" : "") + ", " + v.place + ")", INK, 20);
			Rect add;
			if (shown == selected && add_rect(add)) {
				button(add, "add timer");
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

	void paint_timers() {
		rect_fill(TIMERS, PANEL_FILL);
		outline(TIMERS, GRID, 1);
		const std::vector<Timer> &all = timers.list();
		text(TIMERS.x + 14, TIMERS.y + 26, text_of("timers  %.0f of %.0f", static_cast<double>(all.size()), Timers::MAX_TIMERS), INK, 17);
		if (all.empty()) {
			text(TIMERS.x + 14, TIMERS.y + 60, "Select an item, then \"add timer\":", DIM, 15);
			text(TIMERS.x + 14, TIMERS.y + 82, "the clock stops when it fires.", DIM, 15);
			return;
		}
		const double t = static_cast<double>(view.step) / 60.0;
		for (int n = 0; n < static_cast<int>(all.size()); ++n) {
			const Timer &tm = all[static_cast<size_t>(n)];
			const Rect r = row_rect(n);
			rect_fill(r, n == fired ? rgba(90, 70, 30) : GRID);
			std::string what;
			for (size_t c = 0; c < tm.conditions.size(); ++c) {
				what += (c > 0 ? (tm.all ? " and " : " or ") : "") + Timers::describe(view, tm.conditions[c]);
			}
			if (what.empty()) {
				what = "time limit";
			}
			if (what.size() > 40) {
				what = what.substr(0, 38) + "..";
			}
			text(r.x + 8, r.y + 19, text_of("%.0f  ", n + 1) + what, INK, 15);
			std::string state;
			if (tm.fired) {
				state = "fired: " + tm.fired_by;
			} else {
				if (!tm.conditions.empty()) {
					const TimerCondition &c = tm.conditions.front();
					state = "now " + shown(c.quantity, now(c.quantity));
				}
				if (tm.limited) {
					state += (state.empty() ? "" : ",  ") + Timers::clock_of(tm.limit_s - (t - tm.armed_at_s)) + " left";
				}
			}
			if (state.size() > 52) {
				state = state.substr(0, 50) + "..";
			}
			text(r.x + 8, r.y + 40, state, tm.fired ? WARN : DIM, 14);
			button(row_rearm(n), "again");
			button(row_remove(n), " x");
		}
	}

	void paint_editor() {
		rect_fill(EDITOR, PANEL_FILL);
		outline(EDITOR, GLASS, 1);
		text(EDITOR.x + 20, EDITOR.y + 38, "a timer on " + draft.item, INK, 22);
		button(editor_cancel(), "cancel");
		text(EDITOR.x + 20, EDITOR.y + 77, "fires when", DIM, 15);
		button(any_button(), "any", !draft.all);
		button(all_button(), "all", draft.all);
		text(EDITOR.x + 300, EDITOR.y + 77, "of its conditions holds, or its time limit passes", DIM, 15);
		const auto field = [&](const Rect &r, const std::string &value, bool focused) {
			rect_fill(r, focused ? rgba(30, 30, 30) : rgba(24, 24, 24));
			outline(r, focused ? GUIDE : GRID, focused ? 2.0f : 1.0f);
			text(r.x + 8, r.y + r.h / 2 + 6, value + (focused && !fresh ? "_" : ""), INK, 17);
		};
		for (int c = 0; c < static_cast<int>(draft.conditions.size()); ++c) {
			const TimerCondition &cond = draft.conditions[static_cast<size_t>(c)];
			const std::string full = label(view, cond.quantity);
			button(cond_quantity(c), full.substr(full.find(": ") + 2) + "  v");
			const bool species = !(cond.quantity.what == "celsius" || cond.quantity.what == "gas_ml" || cond.quantity.what == "condensed_ml" || cond.quantity.what == "liquid_ml" ||
					cond.quantity.what == "valve");
			for (int k = 0; species && k < 3; ++k) {
				button(cond_measure(c, k), MEASURES[k], cond.quantity.measure == MEASURES[k]);
			}
			button(cond_remove(c), " x");
			for (int k = 0; k < 7; ++k) {
				button(cond_compare(c, k), op_name(OPS[k]), cond.comparison.op == OPS[k]);
			}
			const int fields = two_values(cond.comparison.op) ? 2 : 1;
			for (int f = 0; f < fields; ++f) {
				field(cond_field(c, f), draft.text[static_cast<size_t>(c)][static_cast<size_t>(f)], focus_c == c && focus_f == f);
			}
			if (fields == 2) {
				text(cond_field(c, 0).x + 146, cond_field(c, 0).y + 21, "and", DIM, 15);
			}
			double scale[3];
			scales(cond.quantity, scale);
			for (int k : { -3, -2, -1, 1, 2, 3 }) {
				const double step = scale[3 - (k < 0 ? -k : k)];
				std::string amount = text_of(step >= 1.0 ? "%.0f" : "%g", step);
				if (amount.rfind("0.", 0) == 0) {
					amount = amount.substr(1); // ".01"
				}
				button(cond_nudge(c, k), (k < 0 ? "-" : "+") + amount);
			}
			text(cond_field(c, 0).x, cond_field(c, 0).y + 48, "now " + shown(cond.quantity, now(cond.quantity)), DIM, 14);
		}
		if (static_cast<int>(draft.conditions.size()) < Timers::MAX_CONDITIONS) {
			button(add_condition(), "+ a condition");
		}
		button(limit_toggle(), draft.limited ? "time limit: on" : "time limit: off", draft.limited);
		if (draft.limited) {
			field(limit_field(), draft.limit_text, focus_c == -1 && focus_f == 2);
			const char *labels[] = { "-1m", "-10s", "+10s", "+1m" };
			int i = 0;
			for (int k : { -2, -1, 1, 2 }) {
				button(limit_nudge(k), labels[i++]);
			}
		}
		button(arm_button(), "arm");
		text(arm_button().x + 160, arm_button().y + 24, "click a value to type it; enter arms, escape cancels", DIM, 14);
		if (picking >= 0) {
			const std::vector<Quantity> qs = pickable();
			for (int i = 0; i < static_cast<int>(qs.size()); ++i) {
				const Rect r = pick_entry(i);
				rect_fill(r, rgba(20, 20, 20));
				outline(r, GRID, 1);
				const std::string full = label(view, qs[static_cast<size_t>(i)]);
				text(r.x + 8, r.y + 19, full.substr(full.find(": ") + 2), INK, 15);
			}
		}
	}

	// what a timer fired on, pulsing, until the clock plays again
	void paint_fired() {
		if (fired < 0 || fired >= static_cast<int>(timers.list().size())) {
			return;
		}
		const double pulse = 0.5 + 0.5 * std::sin(clock * 5.0);
		const uint32_t colour = (WARN & 0xffffff00u) | static_cast<uint32_t>(140.0 + 115.0 * pulse);
		const int item = item_index(timers.list()[static_cast<size_t>(fired)].item_fired);
		if (item >= 0) {
			const Rect r = rect(item);
			outline(Rect{ r.x - 10, r.y - 10, r.w + 20, r.h + 20 }, colour, 4);
		}
		outline(row_rect(fired), colour, 3);
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

void Ui::pointer(float x, float y, bool down) {
	Impl &m = *impl_;
	const bool pressed = down && !m.down, released = !down && m.down;
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
		if (m.drag >= 0 && m.moved) {
			const int index = m.drag;
			m.drag = -1;
			m.drop(index, x, y);
		} else if (m.drag >= 0) {
			m.selected = m.drag; // a click on it, without moving it (§13.3)
		}
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
		// one step of every frame against every invariant; all of them when asked
		const auto run = [&](int64_t n) {
			m.lab->set_checking(first || m.check_every_step);
			ok = m.lab->advance(first && !m.check_every_step ? 1 : n, why);
			m.lab->set_checking(false);
			if (ok && first && !m.check_every_step && n > 1) {
				ok = m.lab->advance(n - 1, why);
			}
			first = false;
		};
		if (!m.tutorial && !m.timers.armed()) {
			if (steps > 0) {
				run(steps);
			}
		} else {
			// a tutorial's clock stands while its step asks for a gesture (§13.2), and an armed
			// timer is read after every quarter of a lab-second (§13.3): the lab advances in
			// pieces, and stops in the piece where a wait ends or a timer fires
			while (ok && steps > 0 && !m.held() && !m.paused) {
				const int64_t piece = m.timers.armed() ? Timers::READ_STEPS : 60;
				const int64_t n = steps < piece ? steps : piece;
				run(n);
				steps -= n;
				m.refresh();
				if (m.timers.armed()) {
					const int fired = m.timers.poll(m.view);
					if (fired >= 0) {
						m.fire(fired);
					}
				}
				m.follow();
			}
			if (m.held() || m.paused) {
				m.owed = 0.0;
			}
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

const Timers &Ui::timers() const {
	return impl_->timers;
}

std::string Ui::selected() const {
	return impl_->selected >= 0 ? impl_->id(impl_->selected) : std::string();
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
	if (after("timer:", rest)) {
		return m.locate_timer(rest, x, y);
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
