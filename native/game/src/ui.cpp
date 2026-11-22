#include "lab/ui.hpp"
#include "lab/glassware.hpp"

#include "lab/conditions.hpp"

#include "chem/optics.hpp"

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
constexpr Rect EDITOR{ 440, 76, 720, 340 }; // a part's settings, or a rig's, being written
constexpr Rect READOUT{ 1400, 64, 520, 1016 };
constexpr Rect PANEL{ 600, 250, 440, 340 }; // the pour menu (Stage 27: and upside down, with a cap)
constexpr Rect TUTOR{ 1400, 64, 520, 476 }; // a tutorial's card, above the readout (§13.2)
constexpr Rect ROOM{ 0, 64, 1390, 1016 }; // the lit wall behind the cabinet, the bench and the outbox
constexpr float SPEEDS[] = { 1, 10, 60, 600 };

constexpr uint32_t BACKGROUND = 0x2a2a2aff, GRID = 0x444444ff, GLASS = 0xffffffcc, INK = 0xe6e6e6ff, DIM = 0x9a9a9aff, PANEL_FILL = 0x383838ff, BUTTON = 0x505050ff,
				   BUTTON_ON = 0x3c7a4eff, WARN = 0xe0a040ff, GUIDE = 0xffc83cff, GOOD = 0x78c88cff, BAD = 0xf05a5aff,
				   SENSE = 0x8fd6a4ff; // a sensing line in hand, and its highlight while the part reads (§13.3, Stage 26)
constexpr float SENSE_DASH = 10.0f; // a sensing line's dash and gap, together: it is dashed, unlike a wire or a rig
// on the lit wall: what is in the glass is seen by the light that comes through it (§12)
constexpr uint32_t WALL = 0xdededeff, WALL_GRID = 0xc4c4c4ff, WALL_INK = 0x505050ff;
constexpr double WALL_RADIANCE = 0.7305, DARK_RADIANCE = 0.0232; // the linear values of WALL and BACKGROUND

uint32_t rgba(int r, int g, int b, int a = 255) {
	return static_cast<uint32_t>(r) << 24 | static_cast<uint32_t>(g) << 16 | static_cast<uint32_t>(b) << 8 | static_cast<uint32_t>(a);
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
	enum Edit { None, Monitor, Timer, Rig, Choose, Notice, Writer, Goal, Controller } edit = None;
	int level = 2; // a notice part's, or a writer's
	std::string says; // a writer's words, as typed; a notice part's text it hears for (Stage 28)
	int writes = 0; // a writer's: on turning on, off, or on either change (Stage 28)
	std::string part; // the part being set, or a rig's source
	Quantity quantity; // a monitor's: the item it senses, and what it reads (what and measure "" until chosen)
	int kind = -1; // a monitor's: the kind of quantity chosen (lab::QuantityKind), -1 none yet (§13.3, Stage 26)
	int speed = 1; // a controller's: gentle, normal, fast; its target is text[0]
	Comparison::Op op = Comparison::Below;
	std::array<std::string, 2> text; // a monitor's values, as typed
	std::string preset; // a timer's, as typed
	std::string item, control; // a rig's: what it acts on; Choose: the timer a wire was dropped on
	bool change = false;
	std::array<std::optional<double>, 3> setting; // on, off, change; none: leave as it is
	std::array<std::string, 3> percent; // a valve's settings, as typed
	// a goal's (§13.5): its slot, its name (in `says`), its terms and their values as typed
	int slot = 0;
	bool existing = false; // the player's goal there, being changed
	std::vector<GoalTerm> terms;
	std::array<std::string, 3> values;
};
constexpr Comparison::Op GOAL_OPS[] = { Comparison::AtLeast, Comparison::AtMost, Comparison::Above, Comparison::Below };
constexpr const char *SPECIES_MEASURES[] = { "g", "purity", "count", "size" };

constexpr const char *DRAWN[] = { "timer", "monitor", "alarm", "notice", "writer", "controller" };

// what a part's face is made of, by its kind (data/optics/media.json: surfaces; Stage 22): each kind its own
// colour, so a part is known by how it looks, not by text written over the pass
const char *face_of(const std::string &kind) {
	if (kind == "timer" || kind == "monitor" || kind == "alarm" || kind == "notice" || kind == "writer" || kind == "controller") {
		static const std::string names[] = { "timer_face", "monitor_face", "alarm_face", "notice_face", "writer_face", "controller_face" };
		const int k = kind == "timer" ? 0 : kind == "monitor" ? 1 : kind == "alarm" ? 2 : kind == "notice" ? 3 : kind == "writer" ? 4 : 5;
		return names[k].c_str();
	}
	return kind == "circuit" ? "circuit_face" : "part_face";
}

// a surface's colour in the room's light, for the drawer's swatches (the screen's own drawing of what the pass shows)
uint32_t surface_colour(const char *name) {
	const chem::optics::Spectrum *r = chem::optics::surface(name);
	if (r == nullptr) {
		return 0x808080ff;
	}
	double x = 0.0, y = 0.0, z = 0.0, white = 0.0;
	for (size_t i = 0; i < r->size(); ++i) {
		const double light = chem::optics::d65()[i];
		x += chem::optics::xbar()[i] * light * (*r)[i];
		y += chem::optics::ybar()[i] * light * (*r)[i];
		z += chem::optics::zbar()[i] * light * (*r)[i];
		white += chem::optics::ybar()[i] * light;
	}
	x /= white;
	y /= white;
	z /= white;
	const double lin[3] = { 3.2404542 * x - 1.5371385 * y - 0.4985314 * z, -0.9692660 * x + 1.8760108 * y + 0.0415560 * z, 0.0556434 * x - 0.2040259 * y + 1.0572252 * z };
	uint32_t out = 0xff;
	for (int c = 0; c < 3; ++c) {
		const double v = std::min(std::max(lin[c], 0.0), 1.0), e = v <= 0.0031308 ? 12.92 * v : 1.055 * std::pow(v, 1.0 / 2.4) - 0.055;
		out |= static_cast<uint32_t>(std::lround(e * 255.0)) << (24 - 8 * c);
	}
	return out;
}
constexpr int DRAWN_KINDS = 6; // what the drawer holds
constexpr const char *SPEEDS_NAMED[] = { "gentle", "normal", "fast" }; // a controller's (§13.3, Stage 26)
constexpr Rect NOTICES{ 1400, 64, 520, 700 }; // the notices, over the readout (§13.4)
constexpr Rect DEBUG{ 1400, 64, 520, 1016 }; // the debug panel, over the readout (§13.6)
constexpr Comparison::Op OPS[] = { Comparison::Above, Comparison::Below, Comparison::AtLeast, Comparison::AtMost, Comparison::Reaches, Comparison::Between, Comparison::Outside };
constexpr const char *OP_KEYS[] = { "above", "below", "at_least", "at_most", "reaches", "between", "outside" };
constexpr const char *SIDES[] = { "on", "off", "change" };

bool is_part_kind(const std::string &kind) {
	PartKind k = PartKind::Timer;
	return part_kind_of(kind, k);
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
	std::vector<ItemView> streams; // the pours' streams, this frame's (Stage 27): bodies of the pass, each a column of its liquid
	float grab_x = 0, grab_y = 0; // where in it it was taken
	int anchor_c = 0, anchor_r = 0; // the square of its footprint under the pointer
	float press_x = 0, press_y = 0; // where the button went down
	int mods = 0; // Ui::Mod: the keys held with the pointer
	// the selection (§13.1): what was chosen, in order; `selected` is the last of it
	std::vector<int> group;
	bool marquee = false; // a rectangle being drawn from (press_x, press_y)
	bool moved = false;
	// carried (§13.1, Stage 27; asked for by the user, 2026-11-22): a click picks an item up, and it follows the
	// pointer, no button held; the next click puts it down, or, over a vessel it can pour into, pours while held
	bool carrying = false;
	bool putting_down = false; // a press while carrying: its release puts it down
	bool pouring = false; // a press over a vessel: a held pour, tilting while the button is held
	double tilt = 0.0;
	static constexpr double TILT_SECONDS = 1.5; // from upright to the full tilt, while the button is held
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
	int focus = -1; // the field typed into: 0, 1 a monitor's values; 2 a timer's preset; 3, 4, 5 a valve's setting on, off, on a change; 6 a writer's words, a goal's name; 10 + r a goal's term's value
	int pick_row = -1; // the goal's term whose quantity is being picked
	bool fresh = false; // the next key replaces the focused field
	bool resume = false; // the clock was playing when the editor opened
	int making = -1; // a part's kind, dragged out of the drawer
	int wiring = -1; // the part whose port a wire is drawn from
	int sensing = -1; // the part whose sensing port a sensing line is drawn from (§13.3, Stage 26)
	std::vector<std::string> rung, chain; // the alarms that rang, and the parts that set them off, until the clock plays
	std::string opened; // the circuit whose inside is shown (§13.3), "" for none
	// notices (§13.4): the levels shown (the others silenced, still kept), and the panel
	std::array<bool, 4> hear{ false, true, true, true };
	bool notices_open = false;
	// the debug panel (§13.6): open, its first line shown, and a save for the host to write
	bool debug_open = false;
	int debug_from = 0;
	std::string exported;

	void refresh() {
		view = lab->view();
		if (!opened.empty() && item_index(opened) < 0) {
			opened.clear(); // the open circuit was given back, or scrapped
		}
	}
	// the open circuit's buttons, and "create circuit" in the drawer
	bool circuit_press(float x, float y) {
		if (!opened.empty() && circuit_close().has(x, y)) {
			opened.clear();
			return true;
		}
		if (!opened.empty() && circuit_up().has(x, y)) {
			opened = view.items[static_cast<size_t>(opened_index())].part.in; // the circuit around it, or none
			return true;
		}
		if (create_circuit_rect().has(x, y) && !circuit_parts().empty()) {
			if (!allowed_click("circuit:create")) {
				return true;
			}
			json names = json::array();
			for (const std::string &n : circuit_parts()) {
				names.push_back(n);
			}
			if (act({ { "do", "circuit" }, { "parts", names } })) {
				choose_only(static_cast<int>(view.items.size()) - 1);
			}
			return true;
		}
		return false;
	}

	// ---- notices (§13.4): the newest heard in the top bar; the panel, with a switch for each level
	static Rect notices_button() { return Rect{ 1790, 14, 120, 36 }; }
	static Rect notices_close() { return Rect{ NOTICES.x + NOTICES.w - 106, NOTICES.y + 12, 90, 30 }; }
	static Rect hear_rect(int k) { return Rect{ NOTICES.x + 20 + static_cast<float>(k) * 122, NOTICES.y + 56, 114, 30 }; }
	static uint32_t level_colour(lab::Notice::Level level) {
		return level == lab::Notice::Danger ? BAD : level == lab::Notice::Warning ? WARN : level == lab::Notice::Event ? INK : DIM;
	}
	// a press on the notices: the button, the panel's switches; true when it took the press
	bool notices_press(float x, float y) {
		if (notices_button().has(x, y)) {
			notices_open = !notices_open;
			debug_open = debug_open && !notices_open;
			return true;
		}
		if (!notices_open) {
			return false;
		}
		if (notices_close().has(x, y)) {
			notices_open = false;
			return true;
		}
		for (int k = 0; k < 4; ++k) {
			if (hear_rect(k).has(x, y)) {
				hear[static_cast<size_t>(k)] = !hear[static_cast<size_t>(k)];
				return true;
			}
		}
		return NOTICES.has(x, y); // the panel takes what falls on it
	}
	std::string when(int64_t step) const {
		const double seconds = static_cast<double>(step) / 60.0;
		return text_of("%02.0f:", std::floor(seconds / 60.0)) + text_of("%04.1f", seconds - 60.0 * std::floor(seconds / 60.0));
	}
	void paint_notices() {
		// the newest heard, beside the clock
		for (auto n = view.notices.rbegin(); n != view.notices.rend(); ++n) {
			if (hear[static_cast<size_t>(n->level)]) {
				std::string line = when(n->step) + "  " + n->says;
				if (line.size() > 56) {
					line = line.substr(0, 54) + ".."; // short of the debug button
				}
				text(1230, 40, line, level_colour(n->level), 15);
				break;
			}
		}
		button(notices_button(), text_of("notices %.0f", static_cast<double>(view.notices.size())), notices_open, 15);
		if (!notices_open) {
			return;
		}
		rect_fill(NOTICES, PANEL_FILL);
		outline(NOTICES, GLASS, 1);
		text(NOTICES.x + 20, NOTICES.y + 36, "what happened, newest first", INK, 20);
		button(notices_close(), "close");
		for (int k = 0; k < 4; ++k) {
			button(hear_rect(k), std::string(level_name(static_cast<lab::Notice::Level>(k))) + (hear[static_cast<size_t>(k)] ? "" : " (off)"), hear[static_cast<size_t>(k)], 15);
		}
		float y = NOTICES.y + 120;
		for (auto n = view.notices.rbegin(); n != view.notices.rend() && y < NOTICES.y + NOTICES.h - 20; ++n) {
			if (!hear[static_cast<size_t>(n->level)]) {
				continue; // silenced, and kept
			}
			text(NOTICES.x + 20, y, when(n->step) + "  " + level_name(n->level), level_colour(n->level), 14);
			for (const std::string &l : wrapped(n->says, 54)) {
				text(NOTICES.x + 150, y, l, level_colour(n->level), 14);
				y += 20;
			}
			y += 6;
		}
	}

	// ---- the debug panel (§13.6): the selected item from the inside
	static Rect debug_button() { return Rect{ 1670, 14, 110, 36 }; }
	static Rect debug_rect(int k) { return Rect{ DEBUG.x + 20 + static_cast<float>(k) * 98, DEBUG.y + 56, 90, 30 }; } // step, export, up, down, close
	static constexpr const char *DEBUG_BUTTONS[] = { "step", "export", "up", "down", "close" };
	bool debug_press(float x, float y) {
		if (debug_button().has(x, y)) {
			debug_open = !debug_open;
			notices_open = notices_open && !debug_open;
			debug_from = 0;
			return true;
		}
		if (!debug_open) {
			return false;
		}
		for (int k = 0; k < 5; ++k) {
			if (!debug_rect(k).has(x, y)) {
				continue;
			}
			if (k == 0) {
				step_once();
			} else if (k == 1) {
				exported = lab->save();
				message = "exported: the host keeps the lab's save (chem-save/1)";
			} else if (k == 2) {
				debug_from = std::max(0, debug_from - 12);
			} else if (k == 3) {
				debug_from += 12;
			} else {
				debug_open = false;
			}
			return true;
		}
		return DEBUG.has(x, y); // the panel takes what falls on it
	}
	// exactly one step, the clock paused; under a tutorial only while it lets the clock run
	void step_once() {
		if (held()) {
			message = "The clock waits for you: " + tutorial->view().text;
			return;
		}
		paused = true;
		owed = 0.0;
		std::string why;
		lab->set_checking(true);
		const bool ok = lab->advance(1, why, true);
		lab->set_checking(false);
		if (!ok) {
			message = why;
		} else if (lab->rang()) {
			ring();
		}
		refresh();
		follow();
	}
	// the panel's lines: text, colour, size
	struct Line {
		std::string text;
		uint32_t colour;
		float size;
		float indent;
	};
	std::vector<Line> debug_lines() const {
		std::vector<Line> out;
		const auto add = [&](const std::string &t, uint32_t c, float size = 14, float indent = 0) {
			for (const std::string &l : wrapped(t, static_cast<size_t>((DEBUG.w - 40 - indent) / (size * 0.52f)))) {
				out.push_back(Line{ l, c, size, indent });
			}
		};
		if (selected < 0) {
			add("select an item to look inside it", DIM);
			return out;
		}
		const ItemDebug d = lab->debug(id(selected));
		if (!d.found) {
			add(id(selected) + " holds nothing: a burner's flame", DIM);
			return out;
		}
		if (d.part) { // a signal part: what it is doing (Stage 28)
			add(d.item, INK, 18);
			for (const std::string &f : d.facts) {
				add(f, INK, 13, 10);
			}
			return out;
		}
		add(d.item + (d.has_temperature ? text_of(": %.2f C", d.celsius) + text_of(", heat capacity %.1f J/K", d.heat_capacity) : ": no temperature"), INK, 18);
		for (const std::string &f : d.facts) { // its pressure, its pour (Stage 28)
			add(f, INK, 13, 10);
		}
		add("what it holds: mol, g, mole fraction in its mixture, molality, activity", DIM, 13);
		for (const SpeciesDebug &s : d.species) {
			std::string row = s.formula + text_of("  %.4g mol", s.mol) + text_of("  %.4g g", s.g) + text_of("  x %.4g", s.fraction);
			if (s.phase == 'a') {
				row += text_of("  %.4g mol/kg", s.molality);
			}
			add(row + text_of("  a %.4g", s.activity), INK, 13, 10);
		}
		add("its flows in the last step, into it", DIM, 13);
		if (d.flows.empty()) {
			add("none", DIM, 13, 10);
		}
		for (const FlowDebug &f : d.flows) {
			std::string row = f.process + " with " + f.with + ":";
			if (f.watts != 0.0) {
				row += text_of(" %+.4g W", f.watts);
			}
			if (f.grams_per_s != 0.0) {
				row += text_of(" %+.4g g/s", f.grams_per_s);
			}
			add(row, INK, 13, 10);
		}
		add("its reactions", DIM, 13);
		for (const ReactionDebug &r : d.reactions) {
			add(r.equation, INK, 14, 10);
			add(r.explain, DIM, 12, 20);
			add(text_of("rate %.3g mol/s", r.rate_mol_s) + (r.weighed ? text_of(", affinity %.3g kJ/mol", r.affinity_kj) : std::string()), INK, 12, 20);
			// running: green; at rest by its nature: plain; held back: amber
			const bool running = r.limit.rfind("fast:", 0) == 0 || r.limit.rfind("supersaturated", 0) == 0 || r.limit.rfind("limited by its surface", 0) == 0;
			const bool resting = r.limit == "at equilibrium" || r.limit.rfind("fast: held", 0) == 0 || r.limit.rfind("it does not run on its own", 0) == 0;
			add("limit: " + r.limit, resting ? INK : running ? GOOD : WARN, 13, 20);
		}
		return out;
	}
	void paint_debug() {
		button(debug_button(), "debug", debug_open, 15);
		if (!debug_open) {
			return;
		}
		rect_fill(DEBUG, PANEL_FILL);
		outline(DEBUG, GLASS, 1);
		text(DEBUG.x + 20, DEBUG.y + 36, "inside the selected item", INK, 20);
		for (int k = 0; k < 5; ++k) {
			button(debug_rect(k), DEBUG_BUTTONS[k], false, 15);
		}
		const std::vector<Line> lines = debug_lines();
		float y = DEBUG.y + 116;
		for (size_t k = static_cast<size_t>(std::min<int>(debug_from, static_cast<int>(lines.size()) - 1 < 0 ? 0 : static_cast<int>(lines.size()) - 1)); k < lines.size() && y < DEBUG.y + DEBUG.h - 12; ++k) {
			text(DEBUG.x + 20 + lines[k].indent, y, lines[k].text, lines[k].colour, lines[k].size);
			y += lines[k].size + 6;
		}
	}

	// what a tutorial's checks read of the screen
	TutorialScreen screen_now() const {
		TutorialScreen screen;
		screen.speed = speed;
		screen.broken = lab->failed();
		screen.paused = paused;
		screen.selected = selected >= 0 ? id(selected) : std::string();
		static const char *const EDITING[] = { "", "monitor", "timer", "rig", "choose", "notice", "writer", "goal", "controller" };
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
					(where == "outbox" && OUTBOX.has(x, y)) || (where.rfind("slot:", 0) == 0 && slot_at(x, y) == std::atoi(where.c_str() + 5)) || (where == "cabinet" && CABINET.has(x, y))) {
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
	static constexpr float INVERTED_GAP = 24; // an upside-down vessel's mouth above the mouth it pours into
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
		} else if (v.kind == "stopper") { // a rubber bung (Stage 28)
			w = 24;
			h = 16;
		} else if (v.kind == "balloon") {
			const double r = v.burst ? 10.0 : std::fmin(90.0, 14.0 + 6.0 * std::cbrt(v.gas_ml + v.condensed_ml));
			w = h = static_cast<float>(2.0 * r);
		} else if (v.condenser) {
			w = 44;
			h = 120;
		} else if (const glass::Vessel *made = v.glassware.empty() ? nullptr : glass::find(v.glassware, v.nominal_ml)) {
			// glassware (GAME_SPEC §12.3, Stage 24): as wide as its nominal capacity draws it, in its standard's proportions
			w = static_cast<float>(40.0 + 8.0 * std::cbrt(v.nominal_ml));
			h = static_cast<float>(w * made->height / (2.0 * made->widest()));
		} else {
			w = static_cast<float>(40.0 + 8.0 * std::cbrt(v.volume_ml));
			h = w * 1.25f;
		}
	}

	// how much smaller an item is drawn to fit the cabinet's shelf, and an outbox slot (its name under it)
	// a shelf of the cabinet, top to top: 118 px, or less to fit all a bench can put in it on the screen (Stage 28)
	float shelf() const {
		int n = 0;
		for (const ItemView &v : view.items) {
			n += v.kind != "tray" && !is_part_kind(v.kind) ? 1 : 0;
		}
		return std::fmin(118.0f, 970.0f / static_cast<float>(std::max(n, 1)));
	}
	float cabinet_scale(float w, float h) const { return std::fmin(1.0f, std::fmin(200.0f / w, (shelf() - 8.0f - 24.0f) / h)); }
	static float outbox_scale(float w, float h) { return std::fmin(1.0f, std::fmin((100.0f - 12.0f) / w, 106.0f / h)); }

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
			const Rect box{ 20, 96 + static_cast<float>(slot(index, "cabinet")) * shelf(), 200, shelf() - 8.0f };
			const float scale = cabinet_scale(w, h);
			return Rect{ box.cx() - w * scale / 2, box.y + box.h - 22 - h * scale, w * scale, h * scale };
		}
		if (v.place == "outbox") { // in its slot, below the goal (§13.5)
			const Rect box = slot_rect(std::max(1, v.slot));
			const Rect room{ box.x + 6, box.y + box.h - 128, box.w - 12, 106 }; // its name under it, in the slot
			const float scale = outbox_scale(w, h);
			return Rect{ room.cx() - w * scale / 2, room.y + room.h - h * scale, w * scale, h * scale };
		}
		if (v.place == "on" && v.on >= 0 && v.kind == "stopper") {
			// in the neck (Stage 28): as wide as the mouth and a little more, half in it
			const Rect holder = rect(v.on);
			const float mouth = mouth_px(v.on) + 4;
			return Rect{ holder.cx() - mouth / 2, holder.y - h * 0.5f, mouth, h };
		}
		if (v.place == "on" && v.on >= 0) {
			const Rect holder = rect(v.on);
			return Rect{ holder.cx() - w / 2, holder.y - h - 4, w, h };
		}
		if (v.inverted && v.pours_into >= 0) {
			// upside down over what it pours into, its mouth a little above that one's (Stage 27), held by a stand
			const Rect below = rect(v.pours_into);
			return Rect{ below.cx() - w / 2, below.y - INVERTED_GAP - h, w, h };
		}
		if (v.place == "under" && v.under >= 0) {
			const Rect vessel = rect(v.under);
			return Rect{ vessel.cx() - w / 2, vessel.y + vessel.h + 64, w, h };
		}
		if (v.place == "circuit") { // in the open circuit's panel, or not on the screen
			if (v.part.in != opened || opened.empty()) {
				return Rect{ -10000, -10000, 0, 0 };
			}
			const Rect g = circuit_grid();
			return Rect{ g.x + static_cast<float>(v.col) * 40 + 3, g.y + static_cast<float>(v.row) * 40 + 3, 34, 34 };
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

	// a vessel's mouth across, in pixels as it is drawn (its glassware's inside at the mouth, or a third of it)
	float mouth_px(int index) const {
		const ItemView &v = view.items[static_cast<size_t>(index)];
		float w = 0, h = 0;
		size_of(v, w, h);
		const glass::Vessel *g = v.glassware.empty() ? nullptr : glass::find(v.glassware, v.nominal_ml);
		return g != nullptr ? static_cast<float>(w * g->mouth() / g->widest()) : w / 3.0f;
	}

	// the item under a point, the topmost; not the tray, not `except`; with
	// `on_bench`, only what stands on the bench (what can be joined or poured into)
	int item_at(float x, float y, int except = -1, bool on_bench = false) const {
		for (int i = static_cast<int>(view.items.size()) - 1; i >= 0; --i) {
			const std::string &place = view.items[static_cast<size_t>(i)].place;
			if (i == except || view.items[static_cast<size_t>(i)].kind == "tray" || (on_bench && place != "bench" && place != "on" && place != "under" && place != "circuit") ||
					!visible(i)) {
				continue;
			}
			if (!opened.empty() && view.items[static_cast<size_t>(i)].place != "circuit" && circuit_panel().has(x, y)) {
				continue; // behind the open circuit's panel
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

	// An item's controls are not on the bench (GAME_SPEC §13.1, Stage 22): they are in the readout's column, under
	// the selected item's name, for the item that is selected. Where the readout's rows begin:
	float readout_top() const { return tutorial ? TUTOR.y + TUTOR.h + 30 : READOUT.y + 30; }
	// a burner's valve: a slider, shut at its left and open at its right, while it is on the bench or under a vessel
	// a burner's valve, or a vessel's cap set upside down over another (Stage 27): one slider, shut to open
	bool valve_rect(int index, Rect &r) const {
		const ItemView &v = view.items[static_cast<size_t>(index)];
		const bool cap = v.kind == "vessel" && v.inverted && v.pours_into >= 0;
		if (index != selected || (v.kind != "burner" && !cap) || (v.place != "bench" && v.place != "under" && !cap) || draft.edit != Draft::None) {
			return false;
		}
		r = Rect{ READOUT.x + 90, readout_top() + 96, 300, 18 };
		return true;
	}

	// a vessel's stirring button, while it stands on the bench with liquid in it
	bool stir_rect(int index, Rect &r) const {
		const ItemView &v = view.items[static_cast<size_t>(index)];
		if (index != selected || v.kind != "vessel" || v.place != "bench" || !(v.liquid_ml > 0.0) || draft.edit != Draft::None) {
			return false;
		}
		r = Rect{ READOUT.x + 16, readout_top() + 90, 90, 30 };
		return true;
	}

	// an upright bottle's screw cap, on or off, while it is selected on the bench (Stage 28)
	bool screw_rect(int index, Rect &r) const {
		const ItemView &v = view.items[static_cast<size_t>(index)];
		const glass::Vessel *g = v.kind == "vessel" && !v.glassware.empty() ? glass::find(v.glassware, v.nominal_ml) : nullptr;
		if (index != selected || g == nullptr || !g->screw_cap || v.burst || v.pours_into >= 0 || v.holds >= 0 || v.place != "bench" || draft.edit != Draft::None) {
			return false;
		}
		r = Rect{ READOUT.x + 116, readout_top() + 90, 140, 30 };
		return true;
	}

	Rect speed_rect(int i) const { return Rect{ 380 + static_cast<float>(i) * 84, 14, 76, 36 }; }
	static Rect pause_rect() { return Rect{ 270, 14, 96, 36 }; }
	static Rect check_rect() { return Rect{ 760, 14, 220, 36 }; }
	static Rect reset_rect() { return Rect{ 1000, 14, 96, 36 }; }
	static Rect pour_slider() { return Rect{ PANEL.x + 30, PANEL.y + 84, PANEL.w - 60, 18 }; }
	static Rect pour_button() { return Rect{ PANEL.x + 30, PANEL.y + 118, 180, 36 }; }
	static Rect cancel_button() { return Rect{ PANEL.x + PANEL.w - 126, PANEL.y + PANEL.h - 50, 96, 36 }; }
	// upside down over it, with a cap: a placed pour (Stage 27)
	static Rect invert_button() { return Rect{ PANEL.x + 30, PANEL.y + PANEL.h - 50, 240, 36 }; }
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
	bool carrying_group() const { return drag >= 0 && (moved || carrying) && group.size() > 1 && chosen(drag); }
	bool carrying_group_of(int index) const { return group.size() > 1 && chosen(index); }
	// whether a carried item is put down on its own place: there it is left as it was, chosen (Stage 27)
	bool own_place(int index, float x, float y) const {
		const ItemView &v = view.items[static_cast<size_t>(index)];
		if (v.place != "bench") {
			return rect(index).has(x, y);
		}
		const int col = std::clamp(static_cast<int>((x - BENCH.x) / square()) - anchor_c, 0, std::max(0, view.cols - v.w));
		const int row = std::clamp(static_cast<int>((y - BENCH.y) / square()) - anchor_r, 0, std::max(0, view.rows - v.h));
		return BENCH.has(x, y) && col == v.col && row == v.row;
	}
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
	// ---- circuits (§13.3): the open one's panel, and where a part in a closed one is shown
	int opened_index() const { return opened.empty() ? -1 : item_index(opened); }
	Rect circuit_panel() const {
		const int k = opened_index();
		const Part &c = view.items[static_cast<size_t>(k < 0 ? 0 : k)].part;
		const float w = static_cast<float>(std::max(c.inner_w, 8)) * 40 + 40, h = static_cast<float>(std::max(c.inner_h, 4)) * 40 + 100;
		if (w <= OUTBOX.w && h <= OUTBOX.h) {
			return Rect{ OUTBOX.x, OUTBOX.y, w, h }; // over the outbox: the bench stays in sight, to wire out to
		}
		return Rect{ std::clamp(BENCH.cx() - w / 2, BENCH.x, BENCH.x + BENCH.w - w), BENCH.y + BENCH.h - h, w, h };
	}
	Rect circuit_grid() const {
		const Rect p = circuit_panel();
		const int k = opened_index();
		const Part &c = view.items[static_cast<size_t>(k < 0 ? 0 : k)].part;
		return Rect{ p.x + 20, p.y + 70, static_cast<float>(c.inner_w) * 40, static_cast<float>(c.inner_h) * 40 };
	}
	Rect circuit_close() const { const Rect p = circuit_panel(); return Rect{ p.x + p.w - 106, p.y + 12, 90, 30 }; }
	Rect circuit_up() const { const Rect p = circuit_panel(); return Rect{ p.x + p.w - 206, p.y + 12, 90, 30 }; }
	bool visible(int index) const {
		const ItemView &v = view.items[static_cast<size_t>(index)];
		return v.place != "circuit" || (!opened.empty() && v.part.in == opened);
	}
	// what shows a part: itself, or the circuit around it that is on the screen; -1 for none
	int shown_as(int index) const {
		while (index >= 0 && !visible(index)) {
			index = item_index(view.items[static_cast<size_t>(index)].part.in);
		}
		return index;
	}
	// whether a circuit holds a part, at any depth
	bool holds(const std::string &circuit, const std::string &part_id) const {
		for (int i = item_index(part_id); i >= 0;) {
			const std::string &in = view.items[static_cast<size_t>(i)].part.in;
			if (in == circuit) {
				return true;
			}
			i = in.empty() ? -1 : item_index(in);
		}
		return false;
	}
	// a circuit's lamp: something in it signals out of it
	bool circuit_out(const std::string &circuit) const {
		const auto lit = [&](const std::string &source) {
			const int i = item_index(source);
			return i >= 0 && holds(circuit, source) && view.items[static_cast<size_t>(i)].part.out;
		};
		for (const Wire &w : view.wires) {
			if (lit(w.from) && !holds(circuit, w.to)) {
				return true;
			}
		}
		for (const Rig &r : view.rigs) {
			if (lit(r.from) && !holds(circuit, r.item)) {
				return true;
			}
		}
		return false;
	}
	// the signal parts of the selection that stand on the bench: what "create circuit" takes
	std::vector<std::string> circuit_parts() const {
		std::vector<std::string> out;
		for (const int i : group) {
			const ItemView &v = view.items[static_cast<size_t>(i)];
			if (is_part_kind(v.kind) && v.place == "bench") {
				out.push_back(v.id);
			}
		}
		return out;
	}
	static Rect create_circuit_rect() { return Rect{ DRAWER.x + 20, DRAWER.y + 180, 300, 30 }; } // under the parts
	// the square of the open circuit's grid under a point; false outside it
	bool inner_square(float x, float y, int &col, int &row) const {
		const Rect g = circuit_grid();
		if (opened.empty() || !g.has(x, y)) {
			return false;
		}
		col = static_cast<int>((x - g.x) / 40);
		row = static_cast<int>((y - g.y) / 40);
		return true;
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
	// a vessel a carried one pours into: a vessel made as glassware, not a condenser, not itself
	bool pours_into(int carried, int target) const {
		if (carried < 0 || target < 0 || carried == target) {
			return false;
		}
		const ItemView &a = view.items[static_cast<size_t>(carried)], &b = view.items[static_cast<size_t>(target)];
		return a.kind == "vessel" && !a.glassware.empty() && b.kind == "vessel" && !b.condenser && b.place == "bench";
	}
	// what is in hand is set back on its place, as it was
	void put_back() {
		stop_pouring();
		carrying = false;
		putting_down = false;
		drag = -1;
	}
	void stop_pouring() {
		if (!pouring) {
			return;
		}
		pouring = false;
		tilt = 0.0;
		act({ { "do", "unpour" }, { "item", id(drag) } });
	}
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
			// a signal part: along the bench or within its open circuit, or back to the drawer
			int inner_col = 0, inner_row = 0;
			if (CABINET.has(x, y) || DRAWER.has(x, y)) {
				act({ { "do", "stow" }, { "item", v.id } });
				choose_only(-1);
			} else if (v.place == "circuit") {
				if (inner_square(x, y, inner_col, inner_row)) {
					act({ { "do", "place" }, { "item", v.id }, { "at", { inner_col, inner_row } } });
				} else {
					message = "a part in a circuit stays in it: \"give back\" the circuit to take its parts out";
				}
			} else if (!opened.empty() && circuit_panel().has(x, y)) {
				message = "a part goes into a circuit when the circuit is made: select it with the others, and \"create circuit\"";
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
			} else if (v.kind == "balloon" || v.kind == "stopper" || v.condenser) {
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
			json c = { { "do", "submit" }, { "item", v.id } };
			if (slot_at(x, y) > 0) {
				c["slot"] = slot_at(x, y);
			}
			act(c);
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
			act({ { "do", "place" }, { "item", v.id }, { "at", { col, row } } });
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
		if (notices_press(x, y) || debug_press(x, y)) {
			return; // the notices and the debug panel are the player's, like the clock, under a tutorial too
		}
		if (!panel && draft.edit == Draft::None && circuit_press(x, y)) {
			return;
		}
		if (!panel && draft.edit == Draft::None && goal_press(x, y)) {
			return;
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
					put_back(); // the menu acted for the vessel in hand: it is set back on its place (Stage 27)
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
						put_back();
					}
					return;
				}
			}
			if (invert_button().has(x, y) && (!guided() || asks_click("panel:invert"))) {
				if (allowed_click("panel:invert") && act({ { "do", "invert" }, { "item", id(from) }, { "over", id(to) } })) {
					panel = false;
					put_back(); // it stands upside down over the other now: its cap is in the readout, shut
					choose_only(from);
				}
				return;
			}
			if (cancel_button().has(x, y) || !PANEL.has(x, y)) {
				panel = false; // still in hand
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
				opened.clear();
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
					if (!allowed_click((view.items[static_cast<size_t>(i)].kind == "burner" ? "valve:" : "cap:") + id(i) + "@")) {
						return;
					}
					valve_drag = i;
					turn(x);
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
			if (screw_rect(i, r) && r.has(x, y)) {
				if (!allowed_click("screw:" + id(i))) {
					return;
				}
				act({ { "do", "cap" }, { "item", id(i) }, { "to", view.items[static_cast<size_t>(i)].screwed ? 1.0 : 0.0 } });
				return;
			}
		}
		if (carrying && drag >= 0) {
			// a press while carrying (Stage 27): over a vessel it pours into, a held pour, or with the right button
			// the full menu; anywhere else its release puts it down
			const int target = item_at(x, y, drag, true);
			if (pours_into(drag, target)) {
				if (mods & Ui::ModRight) {
					if (guided() && !guided_drop(drag, target, x, y)) {
						return;
					}
					panel = true;
					from = drag;
					to = target;
					pour_fraction = view.items[static_cast<size_t>(drag)].liquid_ml > 50.0 ? 50.0 / view.items[static_cast<size_t>(drag)].liquid_ml : 1.0;
					return;
				}
				if (guided()) {
					not_now(); // a tutorial pours by the menu, an amount
					return;
				}
				if (act({ { "do", "hold" }, { "item", id(drag) }, { "over", id(target) } })) {
					pouring = true;
					tilt = 0.0;
				}
				return;
			}
			if (!(mods & Ui::ModRight)) {
				putting_down = true;
			}
			return;
		}
		if (mods & Ui::ModRight) {
			// the right button on an item chooses it, not picking it up: its controls in the readout (Stage 27; asked
			// for by the user, 2026-11-22)
			const int chose = item_at(x, y);
			if (chose >= 0) {
				choose_only(chose); // as a click chose it before: it changes nothing in the lab, under a tutorial too
			}
			return;
		}
		const int hit = item_at(x, y);
		if (hit < 0 && !opened.empty() && circuit_panel().has(x, y)) {
			return; // the open circuit's panel: no rectangle across it
		}
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

	static Rect drawer_rect(int k) { return Rect{ DRAWER.x + 14 + static_cast<float>(k) * 72, DRAWER.y + 46, 68, 110 }; }
	// a part's output, on its right side; an alarm has none
	bool port_rect(int index, Rect &r) const {
		if (!is_part(index) || !sends(view.items[static_cast<size_t>(index)].part.kind) || !visible(index)) {
			return false;
		}
		const Rect body = drawn(index);
		r = Rect{ body.x + body.w - 5, body.cy() - 6, 12, 12 };
		return true;
	}
	// a monitor's sensing port, at its foot (§13.3, Stage 26)
	bool sense_rect(int index, Rect &r) const {
		if (!is_part(index) || !senses(view.items[static_cast<size_t>(index)].part.kind) || !visible(index)) {
			return false;
		}
		const Rect body = drawn(index);
		r = Rect{ body.cx() - 6, body.y + body.h - 5, 12, 12 };
		return true;
	}
	// where a sensing line meets what it senses: the side of its body nearer the part, low, where a vessel is
	// widest
	void sensed_point(int part, int item, float &x, float &y) const {
		Rect port;
		sense_rect(part, port);
		const Rect r = drawn(item);
		x = port.cx() < r.cx() ? r.x : r.x + r.w;
		y = r.y + r.h * (view.items[static_cast<size_t>(item)].kind == "vessel" ? 0.8f : 0.5f);
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
		// on the item itself (its controls are not on the bench: Stage 22): a burner's valve at its right side, a
		// vessel's stirring at its foot's right corner, a part's input at its corner
		const Rect body = drawn(i);
		if (control == "valve") {
			x = body.x + body.w;
			y = body.cy();
			return;
		}
		if (control == "stir") {
			x = body.x + body.w - 6;
			y = body.y + body.h - 8;
			return;
		}
		x = body.x + 4;
		y = control == "reset" ? body.y + body.h - 4 : body.y + 4;
	}

	// ---- the outbox's slots and their goals (§13.5)
	static Rect slot_rect(int k) { return Rect{ OUTBOX.x + 8 + static_cast<float>(k - 1) * 104, OUTBOX.y + 36, 100, 258 }; }
	static int slot_at(float x, float y) {
		for (int k = 1; k <= OUTBOX_SLOTS; ++k) {
			if (slot_rect(k).has(x, y)) {
				return k;
			}
		}
		return 0;
	}
	static Rect goal_button(int k) { return Rect{ slot_rect(k).x + 20, slot_rect(k).y + 4, 58, 22 }; }
	const Goal *goal_of(int k) const {
		for (const Goal &g : view.goals) {
			if (g.slot == k) {
				return &g;
			}
		}
		return nullptr;
	}
	// the first free slot, without a goal first: where a drop on "outbox" goes, as a submit with none
	int free_slot() const {
		for (const bool goal_ok : { false, true }) {
			for (int k = 1; k <= OUTBOX_SLOTS; ++k) {
				bool held = false;
				for (const ItemView &v : view.items) {
					held = held || (v.place == "outbox" && v.slot == k);
				}
				if (!held && (goal_ok || goal_of(k) == nullptr)) {
					return k;
				}
			}
		}
		return OUTBOX_SLOTS;
	}
	bool goal_press(float x, float y) {
		for (int k = 1; k <= OUTBOX_SLOTS; ++k) {
			const Goal *g = goal_of(k);
			if (goal_button(k).has(x, y) && (g == nullptr || !g->game)) {
				if (allowed_click("goal:" + std::to_string(k))) {
					edit_goal(k);
				}
				return true;
			}
		}
		return false;
	}
	static GoalTerm default_term() { return GoalTerm{ Quantity{ "", "water", "g" }, Comparison{ Comparison::AtLeast, 1.0, 0.0 } }; }
	// a quantity's name alone, without its unit: "CuSO4(aq)", "temperature"
	static std::string what_name(const std::string &what) {
		const std::string full = label(LabView{}, Quantity{ "", what, "g" });
		const std::string name = full.substr(full.find(": ") + 2);
		const size_t unit_at = name.rfind(" (");
		return unit_at == std::string::npos || name.back() != ')' ? name : name.substr(0, unit_at);
	}
	static std::string term_name(const GoalTerm &t) {
		const std::string full = label(LabView{}, t.quantity);
		return full.substr(full.find(": ") + 2);
	}
	void edit_goal(int k) {
		Draft d;
		d.edit = Draft::Goal;
		d.slot = k;
		const Goal *g = goal_of(k);
		if (g != nullptr) {
			d.existing = true;
			d.says = g->name;
			d.terms = g->terms;
		} else {
			d.terms = { default_term() };
		}
		for (size_t r = 0; r < d.terms.size() && r < 3; ++r) {
			d.values[r] = plain(d.terms[r].quantity, d.terms[r].comparison.a);
		}
		open_edit(d);
		pick_row = -1;
	}
	Rect goal_name_field() const { return Rect{ EDITOR.x + 20, EDITOR.y + 60, 680, 30 }; }
	float term_y(int r) const { return EDITOR.y + 104 + static_cast<float>(r) * 38; }
	Rect term_what(int r) const { return Rect{ EDITOR.x + 20, term_y(r), 250, 30 }; }
	Rect term_measure(int r) const { return Rect{ EDITOR.x + 280, term_y(r), 90, 30 }; }
	Rect term_compare(int r) const { return Rect{ EDITOR.x + 380, term_y(r), 110, 30 }; }
	Rect term_value(int r) const { return Rect{ EDITOR.x + 500, term_y(r), 130, 30 }; }
	Rect term_remove(int r) const { return Rect{ EDITOR.x + 640, term_y(r), 60, 30 }; }
	Rect term_add() const { return Rect{ EDITOR.x + 20, term_y(static_cast<int>(draft.terms.size())), 140, 30 }; }
	Rect goal_remove() const { return Rect{ EDITOR.x + EDITOR.w - 250, EDITOR.y + 12, 124, 32 }; }
	Rect goal_pick(int i) const { return Rect{ EDITOR.x + 20, term_y(pick_row) + 32 + static_cast<float>(i) * 26, 250, 24 }; }
	bool goal_species(int r) const {
		const std::string &w = draft.terms[static_cast<size_t>(r)].quantity.what;
		return !(w == "celsius" || w == "liquid_ml" || w == "gas_ml");
	}
	void goal_edit_press(float x, float y) {
		if (pick_row >= 0) {
			const std::vector<std::string> all = goal_quantities();
			for (int i = 0; i < static_cast<int>(all.size()); ++i) {
				if (goal_pick(i).has(x, y) && allowed_click("edit:pick:" + all[static_cast<size_t>(i)])) {
					GoalTerm &t = draft.terms[static_cast<size_t>(pick_row)];
					t.quantity = Quantity{ "", all[static_cast<size_t>(i)], "g" };
				}
			}
			pick_row = -1; // a click anywhere closes the list
			return;
		}
		if (draft.existing && goal_remove().has(x, y)) {
			if (allowed_click("edit:remove") && act({ { "do", "ungoal" }, { "slot", draft.slot } })) {
				close_edit();
			}
			return;
		}
		if (goal_name_field().has(x, y)) {
			if (allowed_click("edit:name")) {
				focus = 6;
				fresh = false;
			}
			return;
		}
		const std::string row = "edit:term:";
		for (int r = 0; r < static_cast<int>(draft.terms.size()); ++r) {
			GoalTerm &t = draft.terms[static_cast<size_t>(r)];
			const std::string at = row + std::to_string(r) + ":";
			if (term_what(r).has(x, y)) {
				if (allowed_click(at + "what")) {
					pick_row = r;
				}
				return;
			}
			if (goal_species(r) && term_measure(r).has(x, y)) {
				if (allowed_click(at + "measure")) {
					int k = 0;
					while (k < 4 && t.quantity.measure != SPECIES_MEASURES[k]) {
						++k;
					}
					t.quantity.measure = SPECIES_MEASURES[(k + 1) % 4];
				}
				return;
			}
			if (term_compare(r).has(x, y)) {
				if (allowed_click(at + "compare")) {
					int k = 0;
					while (k < 4 && t.comparison.op != GOAL_OPS[k]) {
						++k;
					}
					t.comparison.op = GOAL_OPS[(k + 1) % 4];
				}
				return;
			}
			if (term_value(r).has(x, y)) {
				if (allowed_click(at + "value")) {
					focus = 10 + r;
					fresh = true;
				}
				return;
			}
			if (draft.terms.size() > 1 && term_remove(r).has(x, y)) {
				if (allowed_click(at + "remove")) {
					draft.terms.erase(draft.terms.begin() + r);
					for (size_t k = static_cast<size_t>(r); k + 1 < draft.values.size(); ++k) {
						draft.values[k] = draft.values[k + 1];
					}
					focus = -1;
				}
				return;
			}
		}
		if (draft.terms.size() < 3 && term_add().has(x, y)) {
			if (allowed_click("edit:term:add")) {
				draft.values[draft.terms.size()] = "1";
				draft.terms.push_back(default_term());
			}
		}
	}
	// the goal's command, from the draft; false with the reason when a value is not a number
	bool goal_command(json &c) {
		json terms = json::array();
		for (size_t r = 0; r < draft.terms.size(); ++r) {
			char *end = nullptr;
			const double v = std::strtod(draft.values[r].c_str(), &end);
			if (draft.values[r].empty() || end == nullptr || *end != '\0') {
				message = "a value is not a number";
				return false;
			}
			const GoalTerm &t = draft.terms[r];
			terms.push_back({ { "quantity", { { "what", t.quantity.what }, { "measure", t.quantity.measure } } }, { "compare", OP_KEYS[static_cast<int>(t.comparison.op)] }, { "value", v } });
		}
		c = { { "do", "goal" }, { "slot", draft.slot }, { "name", draft.says }, { "terms", terms } };
		return true;
	}

	// a controller's state, in a word: unset, untuned, or its level
	static std::string controller_state(const Part &p) { return !p.watching ? "unset" : !p.tuned ? "untuned" : text_of("%.0f%%", 100.0 * p.level); }

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
	Rect edit_item(int k) const { return Rect{ EDITOR.x + 20 + static_cast<float>(k % 8) * 86, EDITOR.y + 60 + static_cast<float>(k / 8) * 34, 82, 30 }; } // eight a row
	// what a notice part can hear about, or a writer write about: the equipment, then the writers (§13.4)
	// what a notice part can hear about, or a writer write about: the equipment, the writers, the goals (§13.4, §13.5)
	std::vector<std::string> about_list() const {
		std::vector<std::string> out;
		for (const int i : watchable()) {
			out.push_back(id(i));
		}
		for (int i = 0; i < static_cast<int>(view.items.size()); ++i) {
			if (view.items[static_cast<size_t>(i)].kind == "writer" && (draft.edit != Draft::Writer || id(i) != draft.part)) {
				out.push_back(id(i));
			}
		}
		for (const Goal &g : view.goals) {
			out.push_back(g.id);
		}
		out.resize(std::min<size_t>(out.size(), 15)); // after "any", or "itself": two rows
		return out;
	}
	Rect says_field() const { return Rect{ EDITOR.x + 20, EDITOR.y + 174, 680, 30 }; }
	// a notice part's seconds; a writer's choice of what of its input writes (Stage 28)
	Rect hold_field() const { return Rect{ EDITOR.x + 140, EDITOR.y + 214, 110, 30 }; }
	Rect writes_rect(int k) const { return Rect{ EDITOR.x + 20 + static_cast<float>(k) * 150, EDITOR.y + 214, 144, 30 }; }
	// a monitor's, guided (§13.3, Stage 26): the kind, then a choice of that kind (a material, or the vessel's
	// quantity), a material's measure, then the comparison and its values
	const ItemView &sensed() const {
		static const ItemView none;
		const int i = item_index(draft.quantity.item);
		return i >= 0 ? view.items[static_cast<size_t>(i)] : none;
	}
	std::vector<QuantityKind> kinds_now() const { return kinds_of(sensed()); }
	Rect kind_rect(int k) const { return Rect{ EDITOR.x + 20 + static_cast<float>(k) * 156, EDITOR.y + 56, 150, 30 }; }
	std::vector<std::string> choices_now() const {
		return draft.kind < 0 ? std::vector<std::string>{} : quantities_of_kind(sensed(), static_cast<QuantityKind>(draft.kind));
	}
	int choice_rows() const {
		const int n = static_cast<int>(choices_now().size());
		return draft.kind < 0 ? 0 : std::max(1, (n + 5) / 6);
	}
	Rect choice_at(int i) const { return Rect{ EDITOR.x + 20 + static_cast<float>(i % 6) * 116, EDITOR.y + 94 + static_cast<float>(i / 6) * 34, 112, 30 }; }
	bool material() const { return draft.kind == static_cast<int>(QuantityKind::Material); }
	std::vector<std::string> measures_now() const {
		return material() && !draft.quantity.what.empty() ? measures_of(sensed(), draft.quantity.what) : std::vector<std::string>{};
	}
	Rect measure_at(int k) const { return Rect{ EDITOR.x + 20 + static_cast<float>(k) * 116, EDITOR.y + 94 + static_cast<float>(choice_rows()) * 34, 112, 30 }; }
	bool quantity_chosen() const { return !draft.quantity.what.empty() && !draft.quantity.measure.empty(); }
	float lower_y() const { return EDITOR.y + 100 + static_cast<float>(choice_rows()) * 34 + (measures_now().empty() ? 0.0f : 34.0f); }
	Rect edit_compare(int k) const { return Rect{ EDITOR.x + 20 + static_cast<float>(k) * 96, lower_y(), 92, 28 }; }
	Rect pace_rect(int k) const { return Rect{ EDITOR.x + 90 + static_cast<float>(k) * 120, lower_y(), 114, 28 }; } // a controller's speed
	bool sensing_edit() const { return draft.edit == Draft::Monitor || draft.edit == Draft::Controller; }
	int value_fields() const { return draft.edit == Draft::Monitor && two_values(draft.op) ? 2 : 1; }
	Rect edit_field(int f) const { return Rect{ EDITOR.x + 20 + static_cast<float>(f) * 180, lower_y() + 36, 140, 30 }; }
	Rect edit_nudge(int k) const { // k in -3..-1, 1..3
		const int at = k < 0 ? k + 3 : k + 2;
		return Rect{ EDITOR.x + 356 + static_cast<float>(at) * 58, lower_y() + 36, 54, 30 };
	}
	static std::string measure_name(const std::string &m) {
		return m == "g" ? "mass" : m == "mol" ? "amount" : m == "count" ? "particles" : m == "size" ? "size" : "purity";
	}
	void choose_kind(QuantityKind k) {
		draft.kind = static_cast<int>(k);
		draft.quantity.what.clear();
		draft.quantity.measure.clear();
		const std::vector<std::string> c = choices_now();
		if (c.size() == 1) {
			choose_quantity(c.front());
		}
	}
	void choose_quantity(const std::string &what) {
		Quantity q = draft.quantity;
		q.what = what;
		q.measure = material() ? std::string() : std::string("g");
		draft.quantity = q;
		if (!q.measure.empty()) {
			set_quantity(q);
		}
	}
	// a timer's preset
	Rect preset_field() const { return Rect{ EDITOR.x + 20, EDITOR.y + 108, 200, 30 }; }
	Rect preset_nudge(int k) const { // k in -3..-1, 1..3: a minute, ten seconds, a second
		const int at = k < 0 ? k + 3 : k + 2;
		return Rect{ EDITOR.x + 240 + static_cast<float>(at) * 64, EDITOR.y + 108, 60, 30 };
	}
	// a notice part's: the level it hears from, and what about ("any" first)
	Rect level_rect(int k) const { return Rect{ EDITOR.x + 20 + static_cast<float>(k) * 110, EDITOR.y + 134, 104, 30 }; }
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
		if (v.kind == "notice" || v.kind == "writer") {
			d.edit = v.kind == "notice" ? Draft::Notice : Draft::Writer;
			d.level = v.part.notice_level;
			d.quantity.item = v.part.quantity.item;
			d.says = v.kind == "notice" ? v.part.contains : v.part.says;
			d.writes = v.part.writes;
			d.preset = text_of("%g", static_cast<double>(v.part.hold) / 60.0); // a notice part's seconds
			open_edit(d);
			return;
		}
		if (v.kind != "monitor" && v.kind != "controller") {
			return;
		}
		// it reads what it senses: one that senses nothing says how (§13.3, Stage 26)
		if (v.part.quantity.item.empty() || item_index(v.part.quantity.item) < 0) {
			message = v.id + " senses nothing yet: drag from its sensing port, at its foot, onto what it should watch";
			return;
		}
		d.edit = v.kind == "controller" ? Draft::Controller : Draft::Monitor;
		d.speed = v.part.speed;
		open_edit(d);
		draft.quantity = v.part.quantity;
		if (v.part.watching) {
			draft.kind = static_cast<int>(kind_of(v.part.quantity));
			draft.op = v.part.comparison.op;
			draft.text = { plain(v.part.quantity, v.kind == "controller" ? v.part.target : v.part.comparison.a), plain(v.part.quantity, v.part.comparison.b) };
			return;
		}
		draft.quantity.what.clear();
		draft.quantity.measure.clear();
		// a step with one choice is taken
		const std::vector<QuantityKind> kinds = kinds_of(sensed());
		if (kinds.size() == 1) {
			choose_kind(kinds.front());
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
		if (draft.edit == Draft::Controller) {
			if (!allowed_click("edit:set")) {
				return;
			}
			char *end = nullptr;
			const double target = std::strtod(draft.text[0].c_str(), &end);
			if (draft.text[0].empty() || end == nullptr || *end != '\0') {
				message = "the target is not a number";
				return;
			}
			const json c = { { "do", "set" }, { "part", draft.part }, { "item", draft.quantity.item }, { "what", draft.quantity.what }, { "measure", draft.quantity.measure },
				{ "target", target }, { "speed", SPEEDS_NAMED[std::clamp(draft.speed, 0, 2)] } };
			if (guided_commit(c) && act(c)) {
				close_edit();
			}
		} else if (draft.edit == Draft::Monitor) {
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
		} else if (draft.edit == Draft::Notice) {
			if (!allowed_click("edit:set")) {
				return;
			}
			const double seconds = std::atof(draft.preset.c_str());
			if (!(seconds > 0.0)) {
				message = "it signals for some seconds after a notice: more than none";
				return;
			}
			const json c = { { "do", "set" }, { "part", draft.part }, { "level", level_name(static_cast<lab::Notice::Level>(draft.level)) }, { "item", draft.quantity.item },
				{ "contains", draft.says }, { "seconds", seconds } };
			if (guided_commit(c) && act(c)) {
				close_edit();
			}
		} else if (draft.edit == Draft::Goal) {
			json c;
			if (!allowed_click("edit:set") || !goal_command(c)) {
				return;
			}
			if (guided_commit(c) && act(c)) {
				close_edit();
			}
		} else if (draft.edit == Draft::Writer) {
			if (!allowed_click("edit:set")) {
				return;
			}
			const json c = { { "do", "set" }, { "part", draft.part }, { "level", level_name(static_cast<lab::Notice::Level>(draft.level)) }, { "item", draft.quantity.item },
				{ "says", draft.says }, { "writes", lab::writes_name(draft.writes) } };
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
		if (draft.edit == Draft::Goal && pick_row >= 0) {
			goal_edit_press(x, y);
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
		if (sensing_edit()) {
			// guided (§13.3, Stage 26): the kind, a choice of it, a material's measure; then the comparison, or a
			// controller's speed and target
			const std::vector<QuantityKind> kinds = kinds_now();
			for (int k = 0; k < static_cast<int>(kinds.size()); ++k) {
				if (kind_rect(k).has(x, y)) {
					if (allowed_click(std::string("edit:kind:") + kind_name(kinds[static_cast<size_t>(k)]))) {
						choose_kind(kinds[static_cast<size_t>(k)]);
					}
					return;
				}
			}
			const std::vector<std::string> choices = choices_now();
			for (int i = 0; i < static_cast<int>(choices.size()); ++i) {
				if (choice_at(i).has(x, y)) {
					if (allowed_click(std::string(material() ? "edit:material:" : "edit:quantity:") + choices[static_cast<size_t>(i)])) {
						choose_quantity(choices[static_cast<size_t>(i)]);
					}
					return;
				}
			}
			const std::vector<std::string> measures = measures_now();
			for (int k = 0; k < static_cast<int>(measures.size()); ++k) {
				if (measure_at(k).has(x, y)) {
					if (allowed_click("edit:measure:" + measures[static_cast<size_t>(k)])) {
						Quantity q = draft.quantity;
						q.measure = measures[static_cast<size_t>(k)];
						set_quantity(q);
					}
					return;
				}
			}
			if (!quantity_chosen()) {
				return;
			}
			for (int k = 0; draft.edit == Draft::Controller && k < 3; ++k) {
				if (pace_rect(k).has(x, y)) {
					if (allowed_click(std::string("edit:speed:") + SPEEDS_NAMED[k])) {
						draft.speed = k;
					}
					return;
				}
			}
			for (int k = 0; draft.edit == Draft::Monitor && k < 7; ++k) {
				if (edit_compare(k).has(x, y)) {
					if (allowed_click(std::string("edit:compare:") + OP_KEYS[k])) {
						draft.op = OPS[k];
					}
					return;
				}
			}
			for (int f = 0; f < value_fields(); ++f) {
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
					const int f = focus == 1 && value_fields() == 2 ? 1 : 0;
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
		if (draft.edit == Draft::Goal) {
			goal_edit_press(x, y);
			return;
		}
		if (draft.edit == Draft::Notice || draft.edit == Draft::Writer) {
			for (int k = 0; k < 4; ++k) {
				if (level_rect(k).has(x, y)) {
					if (allowed_click(std::string("edit:level:") + level_name(static_cast<lab::Notice::Level>(k)))) {
						draft.level = k;
					}
					return;
				}
			}
			const std::vector<std::string> items = about_list();
			for (int k = -1; k < static_cast<int>(items.size()); ++k) {
				if (edit_item(k + 1).has(x, y)) {
					const std::string about = k < 0 ? std::string() : items[static_cast<size_t>(k)];
					if (allowed_click("edit:item:" + (about.empty() ? std::string(draft.edit == Draft::Writer ? "itself" : "any") : about))) {
						draft.quantity.item = about;
					}
					return;
				}
			}
			if (says_field().has(x, y)) { // a writer's words; a notice part's text (Stage 28)
				if (allowed_click(draft.edit == Draft::Writer ? "edit:says" : "edit:contains")) {
					focus = 6;
					fresh = false; // words are added to, not replaced
				}
				return;
			}
			if (draft.edit == Draft::Notice && hold_field().has(x, y)) {
				if (allowed_click("edit:seconds")) {
					focus = 2;
					fresh = true;
				}
				return;
			}
			for (int k = 0; draft.edit == Draft::Writer && k < 3; ++k) {
				if (writes_rect(k).has(x, y)) {
					if (allowed_click(std::string("edit:writes:") + lab::writes_name(k))) {
						draft.writes = k;
					}
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
		if (v.kind == "circuit") {
			out.emplace_back("part:open", Rect{ READOUT.x + 16, top, 90, 30 });
			if (v.place == "bench") {
				out.emplace_back("part:give_back", Rect{ READOUT.x + 116, top, 120, 30 });
			}
			return out;
		}
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
			if (name == "part:open") {
				opened = part_id;
			} else if (name == "part:give_back") {
				if (act({ { "do", "uncircuit" }, { "circuit", part_id } })) {
					opened.clear();
					choose_only(-1);
				}
			} else if (name == "part:edit") {
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
		for (int k = 0; k < DRAWN_KINDS; ++k) {
			if (drawer_rect(k).has(x, y)) {
				if (guided() && !asks_drag_from(std::string("drawer:") + DRAWN[k])) {
					not_now();
					return true;
				}
				making = k;
				return true;
			}
		}
		for (int i = static_cast<int>(view.items.size()) - 1; i >= 0; --i) {
			Rect r;
			if (sense_rect(i, r) && Rect{ r.x - 4, r.y - 4, r.w + 8, r.h + 8 }.has(x, y)) {
				if (guided() && !asks_drag_from("sense:" + id(i))) {
					not_now();
					return true;
				}
				sensing = i;
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
		int inner_col = 0, inner_row = 0;
		if (!opened.empty() && circuit_panel().has(x, y)) {
			if (inner_square(x, y, inner_col, inner_row) &&
					act({ { "do", "part" }, { "kind", DRAWN[kind] }, { "at", { inner_col, inner_row } }, { "in", opened } })) {
				const int made = static_cast<int>(view.items.size()) - 1;
				choose_only(made);
				edit_part(made);
			}
			return;
		}
		if (!BENCH.has(x, y)) {
			return;
		}
		const int target = item_at(x, y, -1, true);
		if (!guided_signal_drop(std::string("drawer:") + DRAWN[kind], target, x, y)) {
			return;
		}
		const int col = static_cast<int>((x - BENCH.x) / square()), row = static_cast<int>((y - BENCH.y) / square());
		if (act({ { "do", "part" }, { "kind", DRAWN[kind] }, { "at", { col, row } } })) {
			const int made = static_cast<int>(view.items.size()) - 1;
			choose_only(made);
			edit_part(made);
			if (guided() && senses(view.items[static_cast<size_t>(made)].part.kind)) {
				message.clear(); // the step says how it senses
			}
		}
	}
	// a sensing line let go: onto a vessel, a balloon or a burner, which the part then senses, and its settings open
	void drop_sensing(int source, float x, float y) {
		const int target = item_at(x, y, -1, true);
		if (target < 0) {
			return;
		}
		if (!guided_signal_drop("sense:" + id(source), target, x, y)) {
			return;
		}
		const ItemView &t = view.items[static_cast<size_t>(target)];
		if (t.kind != "vessel" && t.kind != "balloon" && t.kind != "burner") {
			message = id(source) + " senses a vessel, a balloon or a burner";
			return;
		}
		if (act({ { "do", "sense" }, { "part", id(source) }, { "item", t.id } })) {
			edit_part(item_index(id(source)));
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
		if (view.items[static_cast<size_t>(source)].kind == "controller" && !is_part(target)) {
			// a controller's rig has no settings: the valve, or the stirring, follows its level (§13.3, Stage 26)
			if (t.kind == "vessel" || t.kind == "burner") {
				act({ { "do", "rig" }, { "from", id(source) }, { "item", t.id }, { "control", t.kind == "burner" ? "valve" : "stir" } });
			} else {
				message = id(source) + " is rigged to a burner's valve or a vessel's stirring";
			}
			return;
		}
		if (t.kind == "timer" && view.items[static_cast<size_t>(source)].kind == "controller") {
			act({ { "do", "wire" }, { "from", id(source) }, { "to", t.id } }); // its level cannot reset a timer
			return;
		}
		if (t.kind == "timer") {
			Draft d;
			d.edit = Draft::Choose;
			d.part = id(source);
			d.item = t.id;
			open_edit(d);
		} else if (t.kind == "monitor" || t.kind == "alarm" || t.kind == "notice" || t.kind == "writer" || t.kind == "controller") {
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
			if (key == Ui::KeyEscape && carrying) {
				stop_pouring();
				carrying = false; // put back where it was (Stage 27)
				drag = -1;
				return;
			}
			if (key == Ui::KeyEscape && !panel) {
				choose_only(-1);
			}
			return;
		}
		if (key == Ui::KeyEscape) {
			if (allowed_click("edit:cancel")) {
				close_edit();
			}
			return;
		}
		if (key == Ui::KeyEnter) {
			commit();
			return;
		}
		if (focus == 6) {
			type_words(key, codepoint); // a writer's words, a goal's name
			return;
		}
		std::string *field = focus == 0 || focus == 1 ? &draft.text[static_cast<size_t>(focus)]
				: focus == 2											 ? &draft.preset
				: focus >= 3 && focus <= 5								 ? &draft.percent[static_cast<size_t>(focus - 3)]
				: focus >= 10 && focus < 13								 ? &draft.values[static_cast<size_t>(focus - 10)]
																		 : nullptr;
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
		} else if ((codepoint >= '0' && codepoint <= '9') || codepoint == '.' || (codepoint == '-' && (focus < 2 || focus >= 10)) || (codepoint == ':' && focus == 2)) {
			const std::string typed = (fresh ? std::string() : *field) + static_cast<char>(codepoint);
			if (typed.size() <= 16 && allowed_text(typed)) {
				*field = typed;
				fresh = false;
			}
		}
		if (focus >= 3 && focus <= 5) { // a valve's per cent (a goal's value, 10 and on, is no per cent: read past the three, once)
			read_percent(focus - 3);
		}
	}

	// a writer's words: any character, in UTF-8, up to MAX_SAYS bytes
	void type_words(Ui::Key key, uint32_t codepoint) {
		std::string &words = draft.says;
		if (key == Ui::KeyBackspace) {
			while (!words.empty() && (static_cast<unsigned char>(words.back()) & 0xC0) == 0x80) {
				words.pop_back(); // a character's continuing bytes
			}
			if (!words.empty()) {
				words.pop_back();
			}
			return;
		}
		if (key != Ui::KeyText || codepoint < 32 || codepoint == 127 || codepoint > 0x10FFFF || (codepoint >= 0xD800 && codepoint <= 0xDFFF)) {
			return;
		}
		std::string ch;
		if (codepoint < 0x80) {
			ch = std::string(1, static_cast<char>(codepoint));
		} else if (codepoint < 0x800) {
			ch = { static_cast<char>(0xC0 | (codepoint >> 6)), static_cast<char>(0x80 | (codepoint & 0x3F)) };
		} else if (codepoint < 0x10000) {
			ch = { static_cast<char>(0xE0 | (codepoint >> 12)), static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)), static_cast<char>(0x80 | (codepoint & 0x3F)) };
		} else {
			ch = { static_cast<char>(0xF0 | (codepoint >> 18)), static_cast<char>(0x80 | ((codepoint >> 12) & 0x3F)), static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)),
				static_cast<char>(0x80 | (codepoint & 0x3F)) };
		}
		const std::string typed = words + ch;
		if (typed.size() <= (draft.edit == Draft::Notice ? lab::MAX_CONTAINS : MAX_SAYS) && allowed_text(typed)) {
			words = typed;
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
			} else if (x.kind == PartKind::Notice && !view.notices.empty()) {
				for (auto n = view.notices.rbegin(); n != view.notices.rend(); ++n) {
					if (static_cast<int>(n->level) >= x.notice_level && (x.quantity.item.empty() || n->item == x.quantity.item)) {
						said += ": " + p + " heard \"" + n->says + "\"";
						break;
					}
				}
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

	void turn(float x) {
		Rect r;
		if (!valve_rect(valve_drag, r)) {
			return;
		}
		double open = (x - r.x) / r.w;
		open = open < 0 ? 0 : open > 1 ? 1 : open;
		if (view.items[static_cast<size_t>(valve_drag)].kind == "burner") {
			act({ { "do", "valve" }, { "burner", id(valve_drag) }, { "to", open } });
		} else {
			act({ { "do", "cap" }, { "item", id(valve_drag) }, { "to", open } });
		}
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
		bool ringing = v.kind == "alarm" && p.ringing;
		if (v.place == "bench") {
			// its face (its kind's colour), its lamp, its port, a timer's strip are bodies of the pass (Stage 22):
			// nothing is written on it. While it is chosen its name and what it is set to are a label above it;
			// a highlight round it while it (or an alarm in it) rings
			if (v.kind == "circuit") {
				for (const ItemView &o : view.items) {
					ringing = ringing || (o.kind == "alarm" && o.part.ringing && holds(v.id, o.id));
				}
			}
			if (chosen(index) && index != drag) {
				std::string says = v.id;
				if (v.kind == "timer") {
					const auto seconds = static_cast<int64_t>(std::ceil(static_cast<double>(p.left) / 60.0));
					says += "  " + text_of("%.0f:", static_cast<double>(seconds / 60)) + text_of("%02.0f", static_cast<double>(seconds % 60));
				} else if (v.kind == "monitor") {
					says += p.watching ? "  " + std::string(op_name(p.comparison.op)) : "  unset";
				} else if (v.kind == "controller") {
					says += "  " + controller_state(p);
				} else if (v.kind == "notice" || v.kind == "writer") {
					says += "  " + std::string(level_name(static_cast<lab::Notice::Level>(p.notice_level)));
				}
				text(r.x, r.y - 8, says, WALL_INK, 14);
			}
			if (ringing) {
				outline(Rect{ r.x - 3, r.y - 3, r.w + 6, r.h + 6 }, WARN, 2.0f);
			}
			return;
		}
		if (v.kind == "circuit") {
			int held = 0;
			for (const ItemView &o : view.items) {
				held += o.part.in == v.id ? 1 : 0;
				ringing = ringing || (o.kind == "alarm" && o.part.ringing && holds(v.id, o.id));
			}
			const bool lit = circuit_out(v.id);
			rect_fill(r, ringing ? rgba(110, 70, 30) : rgba(40, 60, 80));
			outline(r, lit ? GOOD : WALL_INK, lit ? 2.0f : 1.0f);
			outline(Rect{ r.x + 2, r.y + 2, r.w - 4, r.h - 4 }, rgba(90, 110, 140), 1);
			text(r.x + 4, r.y + 15, short_name(v), INK, 13);
			text(r.x + 3, r.y + r.h - 4, text_of("%.0f in", held), DIM, 10);
			circle(r.x + r.w - 8, r.y + 8, 4, ringing ? WARN : lit ? GOOD : BUTTON);
			return;
		}
		rect_fill(r, ringing ? rgba(110, 70, 30) : PANEL_FILL);
		outline(r, p.out ? GOOD : WALL_INK, p.out ? 2.0f : 1.0f);
		text(r.x + 4, r.y + 15, short_name(v), INK, 13);
		if (v.kind == "timer") {
			const auto seconds = static_cast<int64_t>(std::ceil(static_cast<double>(p.left) / 60.0));
			text(r.x + 3, r.y + r.h - 4, text_of("%.0f:", static_cast<double>(seconds / 60)) + text_of("%02.0f", static_cast<double>(seconds % 60)), DIM, 10);
		} else if (v.kind == "monitor") {
			text(r.x + 3, r.y + r.h - 4, p.watching ? op_name(p.comparison.op) : "unset", DIM, 9);
		} else if (v.kind == "controller") {
			text(r.x + 3, r.y + r.h - 4, controller_state(p), DIM, 9);
		} else if (v.kind == "notice" || v.kind == "writer") {
			text(r.x + 3, r.y + r.h - 4, level_name(static_cast<lab::Notice::Level>(p.notice_level)), DIM, 9);
		}
		circle(r.x + r.w - 8, r.y + 8, 4, ringing ? WARN : p.out ? GOOD : BUTTON);
		Rect port;
		if (port_rect(index, port)) {
			circle(port.cx(), port.cy(), 5, p.out ? GOOD : WALL_INK);
		}
		if (sense_rect(index, port)) {
			circle(port.cx(), port.cy(), 4, p.watching ? SENSE : WALL_INK);
		}
	}

	// the open circuit: its grid, and what is in it
	void paint_circuit() {
		const Rect p = circuit_panel(), g = circuit_grid();
		rect_fill(p, rgba(52, 56, 62));
		outline(p, GLASS, 1);
		const ItemView &k = view.items[static_cast<size_t>(opened_index())];
		text(p.x + 20, p.y + 36, "inside " + k.id + (k.part.in.empty() ? "" : ", in " + k.part.in), INK, 20);
		text(p.x + 20, p.y + 58, "move parts in it; wire them in and out", DIM, 13);
		button(circuit_close(), "close");
		if (!k.part.in.empty()) {
			button(circuit_up(), "up");
		}
		for (int c = 0; c <= k.part.inner_w; ++c) {
			line(g.x + static_cast<float>(c) * 40, g.y, g.x + static_cast<float>(c) * 40, g.y + g.h, GRID, 1);
		}
		for (int r = 0; r <= k.part.inner_h; ++r) {
			line(g.x, g.y + static_cast<float>(r) * 40, g.x + g.w, g.y + static_cast<float>(r) * 40, GRID, 1);
		}
		for (int i = 0; i < static_cast<int>(view.items.size()); ++i) {
			if (i != drag && view.items[static_cast<size_t>(i)].place == "circuit" && visible(i)) {
				paint_item(i, drawn(i));
			}
		}
	}

	// where a link leaves a part: its port; a part in a closed circuit, the circuit's right side
	bool out_point(int index, float &x, float &y) const {
		const int shown = shown_as(index);
		if (shown < 0) {
			return false;
		}
		Rect r;
		if (shown == index && port_rect(index, r)) {
			x = r.cx();
			y = r.cy();
			return true;
		}
		r = drawn(shown);
		x = r.x + r.w;
		y = r.cy();
		return true;
	}

	// a sensing line's two ends: from the part's sensing port to what it senses, both on the bench
	bool sensing_ends(int part, float &x0, float &y0, float &x1, float &y1) const {
		Rect port;
		if (!is_part(part) || !on_bench(part) || !sense_rect(part, port)) {
			return false;
		}
		const int item = item_index(view.items[static_cast<size_t>(part)].part.quantity.item);
		if (item < 0 || !on_bench(item) || is_part(item)) {
			return false;
		}
		x0 = port.cx();
		y0 = port.cy();
		sensed_point(part, item, x1, y1);
		return true;
	}
	// whether an item is on the bench (not in the cabinet, a circuit or the outbox)
	bool on_bench(int index) const { return index >= 0 && view.items[static_cast<size_t>(index)].place == "bench"; }
	// a rig's two ends on the screen: from its part's port to the control it works
	bool rig_ends(const Rig &r, float &x0, float &y0, float &x1, float &y1) const {
		const int a = item_index(r.from);
		if (a < 0 || !out_point(a, x0, y0)) {
			return false;
		}
		const int target = item_index(r.item);
		if (target >= 0 && is_part(target) && shown_as(target) != target) { // a timer's reset, in a closed circuit
			if (shown_as(target) == shown_as(a)) {
				return false;
			}
			in_point(shown_as(target), x1, y1);
		} else {
			control_point(r.item, r.control, x1, y1);
		}
		return true;
	}

	// The wires between parts, and the rigs onto controls. On the bench they are bodies of the pass, and what is
	// drawn here is a highlight along one while its source signals (Stage 22); in an open circuit's panel, the
	// panel's drawing.
	void paint_signals() {
		const double pulse = 0.5 + 0.5 * std::sin(clock * 5.0);
		const uint32_t alarm_colour = (WARN & 0xffffff00u) | static_cast<uint32_t>(140.0 + 115.0 * pulse);
		const auto on = [&](const std::string &part) {
			const int i = item_index(part);
			return i >= 0 && view.items[static_cast<size_t>(i)].part.out;
		};
		const auto in_chain = [&](const std::string &part) { return std::find(chain.begin(), chain.end(), part) != chain.end(); };
		for (const Wire &w : view.wires) {
			const int a = item_index(w.from), b = shown_as(item_index(w.to));
			float x0 = 0, y0 = 0, x1 = 0, y1 = 0;
			if (a < 0 || b < 0 || shown_as(a) == b || !out_point(a, x0, y0)) {
				continue; // inside one closed circuit: not on the screen
			}
			in_point(b, x1, y1);
			const bool lit = in_chain(w.from), standing = on_bench(shown_as(a)) && on_bench(b);
			if (!standing || lit || on(w.from)) {
				line(x0, y0, x1, y1, lit ? alarm_colour : on(w.from) ? GOOD : WALL_INK, lit ? 3.0f : standing ? 1.0f : 2.0f);
			}
		}
		for (const Rig &r : view.rigs) {
			float x0 = 0, y0 = 0, x1 = 0, y1 = 0;
			if (!rig_ends(r, x0, y0, x1, y1)) {
				continue;
			}
			const bool standing = on_bench(shown_as(item_index(r.from)));
			if (!standing) {
				line(x0, y0, x1, y1, on(r.from) ? rgba(90, 140, 230) : rgba(110, 130, 160, 200), 2);
				circle(x1, y1, 4, rgba(90, 140, 230));
			} else if (on(r.from)) {
				line(x0, y0, x1, y1, rgba(90, 140, 230), 1); // a highlight: it is working its control
			}
		}
		// a wire in hand, and a part out of the drawer
		Rect port;
		if (wiring >= 0 && port_rect(wiring, port)) {
			line(port.cx(), port.cy(), px, py, GUIDE, 2);
		}
		if (sensing >= 0 && sense_rect(sensing, port)) {
			line(port.cx(), port.cy(), px, py, SENSE, 2);
		}
		for (int i = 0; i < static_cast<int>(view.items.size()); ++i) {
			float x0 = 0, y0 = 0, x1 = 0, y1 = 0;
			if (view.items[static_cast<size_t>(i)].part.watching && sensing_ends(i, x0, y0, x1, y1)) {
				line(x0, y0, x1, y1, (SENSE & 0xffffff00u) | 0x70u, 1); // a highlight: it is reading
			}
		}
		if (making >= 0) {
			const Rect ghost{ px - 18, py - 18, 36, 36 };
			rect_fill(ghost, PANEL_FILL);
			outline(ghost, GUIDE, 2);
			text(ghost.x + 4, ghost.y + 15, std::string(1, static_cast<char>(DRAWN[making][0] - 'a' + 'A')), INK, 13);
		}
		// what set an alarm off, pulsing, until the clock plays
		for (const std::string &part : chain) {
			const int i = item_index(part);
			if (i < 0 || shown_as(i) < 0) {
				continue;
			}
			const Rect r = drawn(shown_as(i));
			outline(Rect{ r.x - 6, r.y - 6, r.w + 12, r.h + 12 }, alarm_colour, 3);
			const Part &x = view.items[static_cast<size_t>(i)].part;
			const int watched = x.kind == PartKind::Monitor ? item_index(x.quantity.item) : -1;
			if (watched >= 0) {
				const Rect w = drawn(watched);
				outline(Rect{ w.x - 10, w.y - 10, w.w + 20, w.h + 20 }, alarm_colour, 4);
			}
		}
		for (const std::string &alarm : rung) {
			const int i = shown_as(item_index(alarm));
			if (i >= 0) {
				const Rect r = drawn(i);
				outline(Rect{ r.x - 8, r.y - 8, r.w + 16, r.h + 16 }, alarm_colour, 4);
			}
		}
	}

	// the outbox's slots: each one's goal, its terms now, a lamp lit while it is met (§13.5)
	void paint_slots() {
		for (int k = 1; k <= OUTBOX_SLOTS; ++k) {
			const Rect r = slot_rect(k);
			const Goal *g = goal_of(k);
			std::string held; // what is handed in there
			for (const ItemView &v : view.items) {
				held = v.place == "outbox" && v.slot == k ? v.id : held;
			}
			// its state at a glance: a green band and border while met; amber while what is there falls short
			const bool met = g != nullptr && g->met, short_of = g != nullptr && !g->met && !held.empty();
			if (met || short_of) {
				rect_fill(Rect{ r.x + 1, r.y + 1, r.w - 2, 26 }, met ? rgba(46, 125, 70) : rgba(150, 105, 30));
			}
			outline(r, met ? GOOD : short_of ? WARN : WALL_GRID, met || short_of ? 3.0f : 1.0f);
			text(r.x + 6, r.y + 19, std::to_string(k), met || short_of ? INK : WALL_INK, 15);
			if (g == nullptr || !g->game) {
				button(goal_button(k), g == nullptr ? "+ goal" : "edit", false, 12);
			} else {
				text(r.x + 22, r.y + 19, "game", met || short_of ? INK : WALL_INK, 11);
			}
			if (g == nullptr) {
				continue;
			}
			circle(r.x + r.w - 10, r.y + 14, 5, g->met ? GOOD : BUTTON);
			text(r.x + 6, r.y + 44, met ? "met" : short_of ? "not met" : "open", met ? rgba(30, 110, 55) : short_of ? rgba(150, 90, 10) : WALL_INK, 13); // dark: on the lit wall
			float y = r.y + 60;
			for (const std::string &line : wrapped(g->name, 15)) {
				text(r.x + 6, y, line, WALL_INK, 12);
				y += 14;
			}
			for (const GoalTerm &t : g->terms) {
				y += 4;
				for (const std::string &line : wrapped(term_name(t), 17)) {
					text(r.x + 6, y, line, WALL_INK, 10);
					y += 12;
				}
				std::string says = std::string(op_name(t.comparison.op)) + " " + shown(t.quantity, t.comparison.a);
				text(r.x + 6, y, says, WALL_INK, 10);
				y += 12;
				if (!held.empty()) {
					const std::optional<double> now = measure(view, Quantity{ held, t.quantity.what, t.quantity.measure });
					const bool ok = now && meets(t.comparison, *now, std::nullopt);
					text(r.x + 6, y, "now " + (now ? shown(t.quantity, *now) : std::string("-")), ok ? GOOD : WARN, 10);
					y += 12;
				}
			}
		}
	}

	void paint_drawer() {
		rect_fill(DRAWER, PANEL_FILL);
		outline(DRAWER, GRID, 1);
		text(DRAWER.x + 14, DRAWER.y + 26, "parts: drag one onto the bench", INK, 17);
		static const char *const SAYS[][2] = { { "counts", "down" }, { "watches", "a value" }, { "stops", "the clock" }, { "hears", "notices" }, { "writes", "a notice" },
			{ "holds", "a value" } };
		for (int k = 0; k < DRAWN_KINDS; ++k) {
			const Rect r = drawer_rect(k);
			rect_fill(r, BUTTON);
			outline(r, GRID, 1);
			rect_fill(Rect{ r.x + r.w - 20, r.y + 32, 14, 14 }, surface_colour(face_of(DRAWN[k]))); // how one looks on the bench
			text(r.x + 6, r.y + 22, DRAWN[k], INK, 13);
			text(r.x + 6, r.y + 54, std::string(1, static_cast<char>(DRAWN[k][0] - 'a' + 'A')), DIM, 22);
			text(r.x + 6, r.y + 80, SAYS[k][0], DIM, 12);
			text(r.x + 6, r.y + 96, SAYS[k][1], DIM, 12);
		}
		if (!circuit_parts().empty()) {
			const Rect c = create_circuit_rect();
			rect_fill(c, rgba(40, 60, 80));
			outline(c, GRID, 1);
			const size_t n = circuit_parts().size();
			text(c.x + 8, c.y + 21, text_of(n == 1 ? "create circuit of %.0f part" : "create circuit of %.0f parts", static_cast<double>(n)), INK, 16);
		}
		text(DRAWER.x + 14, DRAWER.y + 234, "wire a part from its port, on its right: into another", DIM, 13);
		text(DRAWER.x + 14, DRAWER.y + 254, "part, or onto a vessel, a burner or a timer to rig its", DIM, 13);
		text(DRAWER.x + 14, DRAWER.y + 274, "stirring, its valve or its reset", DIM, 13);
	}

	void paint_item(int index, Rect r) {
		const ItemView &v = view.items[static_cast<size_t>(index)];
		if (is_part_kind(v.kind)) {
			paint_part(index, r);
			return;
		}
		if (v.kind == "burner") {
			// its body and its flame are the pass's (Stage 22)
		} else if (v.kind == "balloon") {
			// all of it is the optical pass's: rubber, and what condensed inside
		} else {
			// All of it is the optical pass's (§12): its glass, what is in it, its liquid's surface, a condenser's
			// outlet. No outline is drawn over the pass (Stage 21): a line the screen draws is not bent by what is
			// held before it.
		}
		const std::string label = v.kind == "burner" ? v.id : v.id + text_of("  %.0f C", v.celsius);
		if (index == drag) {
			return;
		}
		// on the bench a name is a label the screen writes over the pass: only for what is chosen (Stage 22). The
		// cabinet and the outbox are the screen's own panels, and name what they hold
		if ((v.place == "on" || v.place == "under" || v.place == "bench") && !chosen(index)) {
			return;
		}
		if (v.place == "on") {
			text(r.x, r.y - 8, label, WALL_INK, 15);
		} else if (v.place == "under") {
			const Rect c = rect(index);
			text(c.x, c.y + c.h + 18, label, WALL_INK, 14);
		} else if (v.place == "bench" && v.inverted && v.pours_into >= 0) {
			text(r.x + r.w + 6, r.y + 16, label, WALL_INK, 14); // upside down over another: beside it, where it is (Stage 27)
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
		text(OUTBOX.x + 16, OUTBOX.y + 26, "outbox: drop onto a slot to hand in", WALL_INK);
		paint_slots();
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
			if (view.items[static_cast<size_t>(i)].kind == "tray" || i == drag || view.items[static_cast<size_t>(i)].place == "circuit") {
				continue;
			}
			paint_item(i, drawn(i));
			// what stands under a condenser's outlet is joined to it by a drip
			const ItemView &v = view.items[static_cast<size_t>(i)];
			if (v.drains_to >= 0) {
				const Rect a = rect(i), b = rect(v.drains_to);
				line(a.x + a.w + 26, a.y + a.h + 14, b.cx(), b.y, rgba(70, 120, 190, 200), 1);
			}
		}
		if (!opened.empty()) {
			paint_circuit();
		}
		if (drag >= 0) {
			paint_item(drag, drawn(drag));
		}
		for (const int i : group) {
			if (i != drag && visible(i)) {
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
		paint_notices();
		paint_debug();
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
			for (int k = 0; k < DRAWN_KINDS; ++k) {
				if (rest == DRAWN[k]) {
					return found(drawer_rect(k));
				}
			}
			return false;
		}
		if (after("port:", rest)) {
			Rect r;
			return port_rect(item_index(std::string(rest)), r) && found(r);
		}
		if (after("sense:", rest)) {
			Rect r;
			return sense_rect(item_index(std::string(rest)), r) && found(r);
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
		if (after("slot:", rest)) {
			const int k = number(rest);
			return k >= 1 && k <= OUTBOX_SLOTS && found(slot_rect(k));
		}
		if (after("goal:", rest)) {
			const int k = number(rest);
			const Goal *g = k >= 1 && k <= OUTBOX_SLOTS ? goal_of(k) : nullptr;
			return k >= 1 && k <= OUTBOX_SLOTS && draft.edit == Draft::None && (g == nullptr || !g->game) && found(goal_button(k));
		}
		if (draft.edit == Draft::Goal && after("edit:", rest)) {
			if (pick_row >= 0) {
				const std::vector<std::string> all = goal_quantities();
				for (int i = 0; rest.substr(0, 5) == "pick:" && i < static_cast<int>(all.size()); ++i) {
					if (rest.substr(5) == all[static_cast<size_t>(i)]) {
						return found(goal_pick(i));
					}
				}
				return false;
			}
			if (rest == "name") {
				return found(goal_name_field());
			}
			if (rest == "remove") {
				return draft.existing && found(goal_remove());
			}
			if (rest == "set") {
				return found(edit_done());
			}
			if (rest == "cancel") {
				return found(edit_cancel());
			}
			if (rest == "term:add") {
				return draft.terms.size() < 3 && found(term_add());
			}
			if (rest.substr(0, 5) == "term:") {
				const std::string_view more = rest.substr(5);
				const int r = number(more);
				const size_t colon = more.find(':');
				if (r < 0 || r >= static_cast<int>(draft.terms.size()) || colon == std::string_view::npos) {
					return false;
				}
				const std::string_view field_name = more.substr(colon + 1);
				return field_name == "what"		? found(term_what(r))
						: field_name == "measure" ? goal_species(r) && found(term_measure(r))
						: field_name == "compare" ? found(term_compare(r))
						: field_name == "value"	? found(term_value(r))
						: field_name == "remove"	? draft.terms.size() > 1 && found(term_remove(r))
												: false;
			}
			return false;
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
		if (draft.edit == Draft::Notice || draft.edit == Draft::Writer) {
			if ((draft.edit == Draft::Writer && rest == "says") || (draft.edit == Draft::Notice && rest == "contains")) {
				return found(says_field());
			}
			if (draft.edit == Draft::Notice && rest == "seconds") {
				return found(hold_field());
			}
			for (int k = 0; draft.edit == Draft::Writer && sub("writes:") && k < 3; ++k) {
				if (more == lab::writes_name(k)) {
					return found(writes_rect(k));
				}
			}
			for (int k = 0; sub("level:") && k < 4; ++k) {
				if (more == level_name(static_cast<lab::Notice::Level>(k))) {
					return found(level_rect(k));
				}
			}
			if (sub("item:")) {
				if (more == (draft.edit == Draft::Writer ? "itself" : "any")) {
					return found(edit_item(0));
				}
				const std::vector<std::string> items = about_list();
				for (int k = 0; k < static_cast<int>(items.size()); ++k) {
					if (items[static_cast<size_t>(k)] == more) {
						return found(edit_item(k + 1));
					}
				}
			}
			return false;
		}
		if (sensing_edit()) {
			if (sub("kind:")) {
				const std::vector<QuantityKind> kinds = kinds_now();
				for (int k = 0; k < static_cast<int>(kinds.size()); ++k) {
					if (more == kind_name(kinds[static_cast<size_t>(k)])) {
						return found(kind_rect(k));
					}
				}
				return false;
			}
			if (sub(material() ? "material:" : "quantity:")) {
				const std::vector<std::string> choices = choices_now();
				for (int i = 0; i < static_cast<int>(choices.size()); ++i) {
					if (more == choices[static_cast<size_t>(i)]) {
						return found(choice_at(i));
					}
				}
				return false;
			}
			if (sub("measure:")) {
				const std::vector<std::string> measures = measures_now();
				for (int k = 0; k < static_cast<int>(measures.size()); ++k) {
					if (more == measures[static_cast<size_t>(k)]) {
						return found(measure_at(k));
					}
				}
				return false;
			}
			if (!quantity_chosen()) {
				return false;
			}
			if (sub("speed:")) {
				for (int k = 0; draft.edit == Draft::Controller && k < 3; ++k) {
					if (more == SPEEDS_NAMED[k]) {
						return found(pace_rect(k));
					}
				}
				return false;
			}
			if (sub("compare:")) {
				for (int k = 0; draft.edit == Draft::Monitor && k < 7; ++k) {
					if (more == OP_KEYS[k]) {
						return found(edit_compare(k));
					}
				}
				return false;
			}
			if (sub("value:")) {
				const int f = number(more);
				return f >= 0 && f < value_fields() && found(edit_field(f));
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
				if (screw_rect(i, b)) {
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
		if (sensing_edit()) {
			if (after("edit:speed:", rest)) {
				return draft.speed >= 0 && draft.speed < 3 && rest == SPEEDS_NAMED[draft.speed];
			}
			if (after("edit:kind:", rest)) {
				return draft.kind >= 0 && rest == kind_name(static_cast<QuantityKind>(draft.kind));
			}
			if (after("edit:material:", rest) || after("edit:quantity:", rest)) {
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
		// What is in hand is a layer of its own, nearer the eye (GAME_SPEC §12.3): the item being carried, and
		// those of a selection carried with it. What stands behind it is seen through it. An item pressed and
		// not yet moved still stands.
		//
		// Everything that takes space is a body of the pass (Stage 22): a vessel and a balloon by what they hold; a
		// burner, its flame, a condenser's outlet, a signal part on the bench (its face, its lamp, its port), a
		// wire and a rig as things of the lab, each a shape of a surface and a light. A part in an open circuit
		// is the circuit panel's drawing, not the bench's.
		std::vector<OpticsBody> bodies;
		const auto thing = [&](const std::string &who, const char *role, OpticsBody::Shape shape, Rect r, float size, const char *surface, const char *light, bool in_hand) {
			OpticsBody b;
			b.shape = shape;
			b.x = r.x;
			b.y = r.y;
			b.w = r.w;
			b.h = r.h;
			b.size = size;
			b.surface = surface;
			b.light = light;
			b.layer = in_hand ? 1 : 0;
			b.id = std::hash<std::string>{}(who + "/" + role);
			bodies.push_back(b);
			return bodies.size() - 1;
		};
		const auto bar = [&](const std::string &who, const char *role, float x0, float y0, float x1, float y1, float width, const char *surface, bool in_hand) {
			const size_t k = thing(who, role, OpticsBody::Bar, Rect{}, width, surface, nullptr, in_hand);
			bodies[k].x0 = x0;
			bodies[k].y0 = y0;
			bodies[k].x1 = x1;
			bodies[k].y1 = y1;
		};
		const auto add = [&](int i, const Rect &r, bool in_hand) {
			const ItemView &v = view.items[static_cast<size_t>(i)];
			if (v.kind == "stopper") { // rubber (Stage 28), opaque, lit by the room
				thing(v.id, "body", OpticsBody::Slab, r, 4.0f, "rubber", nullptr, in_hand);
			} else if (v.kind == "vessel" || v.kind == "balloon") {
				OpticsBody body{ &v, r.x, r.y, r.w, r.h };
				body.layer = in_hand ? 1 : 0;
				body.flipped = v.inverted && v.pours_into >= 0; // upside down (Stage 27)
				bodies.push_back(body);
				if (v.screwed) { // its screw cap on (Stage 28)
					const float mouth = mouth_px(i) + 8;
					thing(v.id, "cap", OpticsBody::Slab, Rect{ r.cx() - mouth / 2, r.y - 8, mouth, 12 }, 3.0f, "cap_plastic", nullptr, in_hand);
				}
				if (v.condenser) {
					bar(v.id, "outlet", r.x + r.w, r.y + r.h - 10, r.x + r.w + 26, r.y + r.h + 14, 2.0f, "outlet_glass", in_hand);
				}
			} else if (v.kind == "burner") {
				thing(v.id, "body", OpticsBody::Slab, r, 3.0f, "burner_metal", nullptr, in_hand);
				if (v.valve > 0.0) {
					// its flame: from a sooty yellow, barely open, to a blue, wide open
					const float flame = static_cast<float>(12.0 + 30.0 * v.valve);
					const size_t k = thing(v.id, "flame", OpticsBody::Cone, Rect{ r.cx() - 12, r.y - flame, 24, flame }, 0.0f, nullptr, "flame_sooty", in_hand);
					bodies[k].light_to = "flame_blue";
					bodies[k].toward = v.valve;
				}
			} else if (is_part_kind(v.kind) && v.place == "bench") {
				const bool circuit = v.kind == "circuit";
				bool ringing = v.kind == "alarm" && v.part.ringing, lit = v.part.out;
				if (circuit) {
					lit = circuit_out(v.id);
					for (const ItemView &o : view.items) {
						ringing = ringing || (o.kind == "alarm" && o.part.ringing && holds(v.id, o.id));
					}
				}
				thing(v.id, "face", OpticsBody::Slab, r, 4.0f, face_of(v.kind), nullptr, in_hand);
				if (v.kind == "timer") {
					// its time left: a strip of light along its foot, as long as the share of its time that is left
					const Rect strip{ r.x + 5, r.y + r.h - 9, r.w - 10, 4 };
					thing(v.id, "strip", OpticsBody::Slab, strip, 2.0f, "lamp_glass", nullptr, in_hand);
					const double left = v.part.preset > 0 ? static_cast<double>(v.part.left) / static_cast<double>(v.part.preset) : 0.0;
					if (left > 0.0) {
						thing(v.id, "time", OpticsBody::Slab, Rect{ strip.x, strip.y, strip.w * static_cast<float>(std::min(left, 1.0)), strip.h }, 2.0f, "lamp_glass", "lamp_green", in_hand);
					}
				}
				thing(v.id, "lamp", OpticsBody::Slab, Rect{ r.x + r.w - 12, r.y + 4, 8, 8 }, 4.0f, "lamp_glass", ringing ? "lamp_amber" : lit ? "lamp_green" : nullptr, in_hand);
				Rect port;
				if (port_rect(i, port)) {
					thing(v.id, "port", OpticsBody::Slab, Rect{ port.cx() - 5, port.cy() - 5, 10, 10 }, 5.0f, "wire", nullptr, in_hand);
				}
				if (sense_rect(i, port)) {
					thing(v.id, "sense port", OpticsBody::Slab, Rect{ port.cx() - 4, port.cy() - 4, 8, 8 }, 4.0f, "sense", nullptr, in_hand);
				}
			}
		};
		for (int i = 0; i < static_cast<int>(view.items.size()); ++i) {
			if (i != drag) {
				add(i, drawn(i), carrying_group() && chosen(i) && view.items[static_cast<size_t>(i)].place == "bench");
			}
		}
		if (drag >= 0) {
			add(drag, drawn(drag), moved || carrying);
		}
		// supports that come with the glassware (§13, Stage 27): metal, bodies of the pass like everything that takes
		// space; they hold, and change nothing else
		const auto floor_of = [&](int i) {
			const ItemView &v = view.items[static_cast<size_t>(i)];
			const Rect f = v.place == "bench" ? foot(v) : rect(i);
			return f.y + f.h;
		};
		for (int i = 0; i < static_cast<int>(view.items.size()); ++i) {
			const ItemView &v = view.items[static_cast<size_t>(i)];
			if (v.kind != "burner" || v.under < 0 || i == drag || !on_bench(v.under)) {
				continue;
			}
			// a vessel over a burner stands on a wire gauze over a tripod
			const Rect pot = drawn(v.under), flame = drawn(i);
			const float gauze = pot.y + pot.h, ground = flame.y + flame.h;
			thing(v.id, "gauze", OpticsBody::Slab, Rect{ pot.x - 8, gauze, pot.w + 16, 3 }, 1.0f, "burner_metal", nullptr, false);
			bar(v.id, "leg left", pot.x - 4, gauze + 2, pot.x - 14, ground, 3.0f, "burner_metal", false);
			bar(v.id, "leg right", pot.x + pot.w + 4, gauze + 2, pot.x + pot.w + 14, ground, 3.0f, "burner_metal", false);
		}
		for (int i = 0; i < static_cast<int>(view.items.size()); ++i) {
			const ItemView &v = view.items[static_cast<size_t>(i)];
			const int under = v.place == "on" ? v.on : v.inverted ? v.pours_into : -1;
			if (under < 0 || i == drag || !on_bench(under) || v.kind == "stopper") { // a stopper holds by its friction
				continue;
			}
			// held on a vessel's mouth, or upside down over it: a retort stand beside that vessel, a clamp across
			const Rect held = drawn(i), base = drawn(under);
			const float rod = base.x - 18, ground = floor_of(under), grip = held.y + held.h * (v.inverted ? 0.5f : 0.35f);
			thing(v.id, "stand base", OpticsBody::Slab, Rect{ rod - 22, ground - 6, 44, 6 }, 2.0f, "burner_metal", nullptr, false);
			bar(v.id, "stand rod", rod, ground - 6, rod, grip - 10, 3.0f, "burner_metal", false);
			bar(v.id, "clamp", rod, grip, held.cx() - held.w * 0.3f, grip, 2.5f, "burner_metal", false);
		}
		// a pour's stream (Stage 27): a thin column of the liquid itself, from the mouth it leaves to where it lands
		streams.clear();
		streams.reserve(view.items.size());
		for (int i = 0; i < static_cast<int>(view.items.size()); ++i) {
			const ItemView &v = view.items[static_cast<size_t>(i)];
			const bool flows = v.pours_into >= 0 && v.liquid_ml > 0.0 && (v.inverted ? v.cap > 0.0 : v.tilt > 0.0);
			if (!flows) {
				continue;
			}
			const ItemView &t = view.items[static_cast<size_t>(v.pours_into)];
			const Rect from_r = drawn(i), to_r = drawn(v.pours_into);
			float land = to_r.y + to_r.h;
			if (const glass::Vessel *g = t.glassware.empty() ? nullptr : glass::find(t.glassware, t.nominal_ml); g != nullptr) {
				land -= static_cast<float>(g->level(t.condensed_ml) * to_r.w / (2.0 * g->widest()));
			}
			const float x = v.inverted ? from_r.cx() : from_r.x + from_r.w - 3.0f, top = v.inverted ? from_r.y + from_r.h : from_r.y;
			const float width = static_cast<float>(2.0 + 3.0 * (v.inverted ? v.cap : v.tilt));
			if (land <= top + 2.0f) {
				continue;
			}
			ItemView stream;
			stream.id = v.id + "~stream";
			stream.kind = "vessel";
			stream.place = "bench";
			stream.celsius = v.celsius;
			stream.volume_ml = stream.condensed_ml = stream.liquid_ml = 1.0; // a column full of it
			for (const SubstanceView &c : v.contents) {
				if (!c.wall && (c.phase == 'l' || c.phase == 'a')) {
					stream.contents.push_back(c);
				}
			}
			streams.push_back(stream);
			OpticsBody body{ &streams.back(), x - width / 2.0f, top, width, land - top };
			body.layer = i == drag && (moved || carrying) ? 1 : 0;
			bodies.push_back(body);
		}
		// the wires and the rigs between things on the bench
		for (const Wire &w : view.wires) {
			const int a = item_index(w.from), b = shown_as(item_index(w.to));
			float x0 = 0, y0 = 0, x1 = 0, y1 = 0;
			if (a < 0 || b < 0 || shown_as(a) == b || !on_bench(shown_as(a)) || !on_bench(b) || !out_point(a, x0, y0)) {
				continue;
			}
			in_point(b, x1, y1);
			bar(w.from + ">" + w.to, "wire", x0, y0, x1, y1, 2.0f, "wire", false);
		}
		for (const Rig &r : view.rigs) {
			const int a = item_index(r.from);
			float x0 = 0, y0 = 0, x1 = 0, y1 = 0;
			if (a < 0 || !on_bench(shown_as(a)) || !rig_ends(r, x0, y0, x1, y1)) {
				continue;
			}
			bar(r.from + ">" + r.item + "." + r.control, "rig", x0, y0, x1, y1, 2.0f, "rig", false);
			thing(r.from + ">" + r.item + "." + r.control, "knob", OpticsBody::Slab, Rect{ x1 - 4, y1 - 4, 8, 8 }, 4.0f, "rig", nullptr, false);
		}
		// what each monitor senses (§13.3, Stage 26): a dashed line, unlike a wire or a rig
		for (int i = 0; i < static_cast<int>(view.items.size()); ++i) {
			float x0 = 0, y0 = 0, x1 = 0, y1 = 0;
			if (!sensing_ends(i, x0, y0, x1, y1)) {
				continue;
			}
			bar(id(i) + ">sense", "sense", x0, y0, x1, y1, 2.0f, "sense", false);
			bodies.back().dash = SENSE_DASH; // one body, dashed by the pass, not a body a dash
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
		// each shape on the pixels the host paints it on: those whose centres it covers (a GPU's rule, without
		// antialiasing), an outline's stroke centred on its edges; so the wall seen through a vessel is the wall
		// drawn beside it, pixel for pixel (a line one pixel wide on a whole coordinate is one row, not two halves)
		const auto shape = [&](double x0, double y0, double x1, double y1, double r) {
			const double left = std::ceil(x0 - 0.5), right = std::ceil(x1 - 0.5), top = std::ceil(y0 - 0.5), bottom = std::ceil(y1 - 0.5);
			if (right > left && bottom > top) {
				room.shapes.push_back(OpticsShape{ static_cast<float>(left), static_cast<float>(top), static_cast<float>(right - left), static_cast<float>(bottom - top), r });
			}
		};
		for (int i = 0; i < list.above; ++i) {
			const DrawCommand &c = list.commands[static_cast<size_t>(i)];
			const double r = radiance(c.color);
			const double x = c.x, y = c.y, half = c.width / 2.0;
			if (c.type == DrawCommand::Rect) {
				shape(x, y, x + c.w, y + c.h, r);
			} else if (c.type == DrawCommand::Outline) {
				const double right = x + c.w, bottom = y + c.h;
				shape(x - half, y - half, right + half, y + half, r);
				shape(x - half, bottom - half, right + half, bottom + half, r);
				shape(x - half, y - half, x + half, bottom + half, r);
				shape(right - half, y - half, right + half, bottom + half, r);
			} else if (c.type == DrawCommand::Line && c.y == c.h) { // straight across or up: a thin rectangle
				shape(std::min(x, static_cast<double>(c.w)), y - half, std::max(x, static_cast<double>(c.w)), y + half, r);
			} else if (c.type == DrawCommand::Line && c.x == c.w) {
				shape(x - half, std::min(y, static_cast<double>(c.h)), x + half, std::max(y, static_cast<double>(c.h)), r);
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
			row(v.kind == "circuit" ? v.id + ": a circuit" : v.id + (p.out ? ": signalling" : ": quiet"), p.out ? GOOD : INK, 20);
			if (v.kind == "monitor") {
				row(p.watching ? label(view, p.quantity) + " " + op_name(p.comparison.op) + " " + shown(p.quantity, p.comparison.a) +
								(two_values(p.comparison.op) ? " and " + shown(p.quantity, p.comparison.b) : "")
							   : "watches nothing yet: edit it",
						DIM, 15);
				row(p.watching ? "now " + shown(p.quantity, now(p.quantity)) : "", DIM, 15);
			} else if (v.kind == "controller") {
				// what it holds, at what, how far it has it; and what its trial found (§13.3, Stage 26)
				row(p.watching ? label(view, p.quantity) + " held at " + shown(p.quantity, p.target) + ", " + SPEEDS_NAMED[std::clamp(p.speed, 0, 2)]
							   : "holds nothing yet: sense an item, then edit it",
						DIM, 15);
				row(!p.watching ? ""
						: p.tuned ? "now " + shown(p.quantity, now(p.quantity)) + text_of(", level %.0f%%; ", 100.0 * p.level) + text_of("gain %.3g, ", p.kc) + text_of("integral %.0f s", p.ti)
								  : "now " + shown(p.quantity, now(p.quantity)) + "; untuned: rig it to a valve or stirring",
						DIM, 15);
			} else if (v.kind == "circuit") {
				int held = 0;
				for (const ItemView &o : view.items) {
					held += o.part.in == v.id ? 1 : 0;
				}
				row(text_of(held == 1 ? "holds %.0f part" : "holds %.0f parts", held) + (v.part.in.empty() ? "" : ", in " + v.part.in), DIM, 15);
				row(circuit_out(v.id) ? "something in it signals out of it" : "nothing in it signals out of it", DIM, 15);
			} else if (v.kind == "timer") {
				row(clock_text(static_cast<double>(p.left) / 60.0) + " left of " + clock_text(static_cast<double>(p.preset) / 60.0), DIM, 15);
				row(p.left == 0 ? "at zero: on until it is reset" : "", DIM, 15);
			} else if (v.kind == "notice" || v.kind == "writer") {
				const std::string level = level_name(static_cast<lab::Notice::Level>(p.notice_level));
				const std::string about = p.quantity.item.empty() ? (v.kind == "notice" ? "anything" : "itself") : p.quantity.item;
				const std::string a = level == "event" ? "an " : "a ";
				row(v.kind == "notice" ? "hears " + level + " notices and above, about " + about : "writes " + a + level + " notice about " + about + " when its input turns on", DIM, 15);
				row(v.kind == "writer" ? "\"" + (p.says.empty() ? v.id + " was signalled" : p.says) + "\"" : "", DIM, 15);
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
			if (v.kind != "circuit") {
				const bool takes = !sends(v.part.kind); // an alarm, a writer: its input turning on is what it acts on
				row(inputs.empty() ? (takes ? "no inputs: wire one in, or it never acts" : "no inputs: it signals freely")
								   : named + (takes ? " (any turning on sets it off)" : " (any on lets it signal)"),
						DIM, 15);
			}
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
					row(r.follow ? "rigged to " + r.item + "'s " + r.control + (r.control == "valve" ? ": it follows the level" : ": by PWM, on for the level's share")
								 : "rigged to " + r.item + "'s " + r.control + ": " + (r.change ? "on a change " + say(r.any) : "on " + say(r.on) + ", off " + say(r.off)),
							INK, 15);
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
			// its controls, while it is selected (GAME_SPEC §13.1: not on the bench)
			Rect control;
			if (valve_rect(showing, control)) {
				const double open = v.kind == "burner" ? v.valve : v.cap;
				text(READOUT.x + 16, control.y + 15, v.kind == "burner" ? "valve" : "cap", INK, 16);
				rect_fill(control, BUTTON);
				const float knob = control.x + control.w * static_cast<float>(open);
				rect_fill(Rect{ control.x, control.y, knob - control.x, control.h }, BUTTON_ON);
				rect_fill(Rect{ knob - 4, control.y - 4, 8, control.h + 8 }, INK);
				text(control.x + control.w + 12, control.y + 15, text_of("%.0f%%", 100.0 * open), DIM, 15);
				y += 42;
			} else if (stir_rect(showing, control)) {
				button(control, v.stirred ? "stirring" : "stir", v.stirred, 15);
				Rect screw;
				if (screw_rect(showing, screw)) {
					button(screw, v.screwed ? "cap on" : "cap off", v.screwed, 15);
				}
				y += 42;
			} else if (screw_rect(showing, control)) {
				button(control, v.screwed ? "cap on" : "cap off", v.screwed, 15);
				y += 42;
			}
			for (const ControlSetting &c : view.controls) {
				if (c.item == v.id) {
					row(c.control + (c.control == "valve" ? text_of(" %.0f%%", 100.0 * c.value) : c.control == "cap" ? text_of(" %.0f%% open", 100.0 * c.value) : c.value > 0.5 ? " on" : " off") +
									", set by " + c.by +
									text_of(" at %.1f s", static_cast<double>(c.step) / 60.0),
							DIM, 14);
				}
			}
			if (v.kind == "burner") {
				row(text_of("flame %.0f C, valve %.0f%%", v.celsius, 100.0 * v.valve));
			} else if (v.kind == "stopper") {
				row(v.on >= 0 ? "in the neck of " + view.items[static_cast<size_t>(v.on)].id + ": pull it out by picking it up" : "put it on a narrow neck to seal it", DIM, 15);
				row(text_of("holds %.0f N", v.hold_n), DIM, 15);
			} else {
				row(text_of("%.2f C", v.celsius) + (v.kind == "vessel" ? text_of(", %.1f kPa", v.pressure_kpa) : std::string()));
				if (v.burst && v.kind == "vessel") {
					row("broken: its glass burst", WARN, 15);
				} else if (v.sealed) {
					row(text_of("sealed: it gives way past %.0f kPa", v.gives_way_kpa), v.pressure_kpa > 0.8 * v.gives_way_kpa ? WARN : DIM, 15);
				}
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
		if (sensing_edit()) {
			// guided (§13.3, Stage 26): each step a short list of what the item has now, the next once it is chosen
			text(EDITOR.x + 20, EDITOR.y + 40, draft.part + (draft.edit == Draft::Controller ? " holds " : " watches ") + draft.quantity.item, INK, 22);
			if (quantity_chosen()) {
				text(EDITOR.x + 340, EDITOR.y + 40, "now " + shown(draft.quantity, now(draft.quantity)), DIM, 15);
			}
			const std::vector<QuantityKind> kinds = kinds_now();
			for (int k = 0; k < static_cast<int>(kinds.size()); ++k) {
				const QuantityKind kind = kinds[static_cast<size_t>(k)];
				button(kind_rect(k), kind == QuantityKind::Material ? "a material" : kind == QuantityKind::Vessel ? "the " + sensed().kind : "other", draft.kind == static_cast<int>(kind), 15);
			}
			if (draft.kind < 0) {
				text(EDITOR.x + 20, EDITOR.y + 114, "what kind of quantity should it watch?", DIM, 15);
				return;
			}
			const std::vector<std::string> choices = choices_now();
			for (int i = 0; i < static_cast<int>(choices.size()); ++i) {
				const std::string &c = choices[static_cast<size_t>(i)];
				button(choice_at(i), what_name(c), draft.quantity.what == c, 13);
			}
			const std::vector<std::string> measures = measures_now();
			for (int k = 0; k < static_cast<int>(measures.size()); ++k) {
				button(measure_at(k), measure_name(measures[static_cast<size_t>(k)]), draft.quantity.measure == measures[static_cast<size_t>(k)], 14);
			}
			if (!quantity_chosen()) {
				text(EDITOR.x + 20, lower_y() + 20, material() && !draft.quantity.what.empty() ? "measured how?" : material() ? "which material?" : "which quantity?", DIM, 15);
				return;
			}
			if (draft.edit == Draft::Controller) {
				// its speed and its target: the game tunes it, by trying what it controls (§13.3, Stage 26)
				text(EDITOR.x + 20, pace_rect(0).y + 20, "speed", DIM, 15);
				for (int k = 0; k < 3; ++k) {
					button(pace_rect(k), SPEEDS_NAMED[k], draft.speed == k);
				}
				text(edit_field(0).x + 146, edit_field(0).y + 21, "the target", DIM, 14);
			}
			for (int k = 0; draft.edit == Draft::Monitor && k < 7; ++k) {
				button(edit_compare(k), op_name(OPS[k]), draft.op == OPS[k]);
			}
			const int fields = value_fields();
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
		} else if (draft.edit == Draft::Goal) {
			text(EDITOR.x + 20, EDITOR.y + 40, text_of("a goal for slot %.0f", draft.slot), INK, 22);
			if (draft.existing) {
				button(goal_remove(), "take it off", false, 15);
			}
			field(goal_name_field(), draft.says.empty() && focus != 6 ? std::string("(its name: click to type)") : draft.says, focus == 6);
			for (int r = 0; r < static_cast<int>(draft.terms.size()); ++r) {
				const GoalTerm &t = draft.terms[static_cast<size_t>(r)];
				button(term_what(r), what_name(t.quantity.what) + "  v", false, 15);
				if (goal_species(r)) {
					button(term_measure(r), t.quantity.measure, false, 15);
				}
				button(term_compare(r), op_name(t.comparison.op), false, 15);
				field(term_value(r), draft.values[static_cast<size_t>(r)], focus == 10 + r);
				if (draft.terms.size() > 1) {
					button(term_remove(r), "x", false, 15);
				}
			}
			if (draft.terms.size() < 3) {
				button(term_add(), "+ term", false, 15);
			}
			text(EDITOR.x + 20, EDITOR.y + 232, "Met while what is handed in to the slot meets every term.", DIM, 14);
			if (pick_row >= 0) {
				const std::vector<std::string> all = goal_quantities();
				for (int i = 0; i < static_cast<int>(all.size()); ++i) {
					const Rect e = goal_pick(i);
					rect_fill(e, rgba(20, 20, 20));
					outline(e, GRID, 1);
					text(e.x + 8, e.y + 17, what_name(all[static_cast<size_t>(i)]), INK, 14);
				}
			}
		} else if (draft.edit == Draft::Writer) {
			text(EDITOR.x + 20, EDITOR.y + 40, draft.part + " writes a notice about", INK, 22);
			const std::vector<std::string> items = about_list();
			button(edit_item(0), "itself", draft.quantity.item.empty(), 14);
			for (int k = 0; k < static_cast<int>(items.size()); ++k) {
				button(edit_item(k + 1), items[static_cast<size_t>(k)], items[static_cast<size_t>(k)] == draft.quantity.item, 14);
			}
			for (int k = 0; k < 4; ++k) {
				button(level_rect(k), level_name(static_cast<lab::Notice::Level>(k)), draft.level == k);
			}
			field(says_field(), draft.says.empty() && focus != 6 ? std::string("(its words: click to type)") : draft.says, focus == 6);
			static const char *const WRITES[] = { "when it turns on", "when it turns off", "on either change" };
			for (int k = 0; k < 3; ++k) {
				button(writes_rect(k), WRITES[k], draft.writes == k, 14);
			}
			text(EDITOR.x + 20, EDITOR.y + 270, "When its input changes so, it writes these words at that level; notice parts hear it.", DIM, 14);
		} else if (draft.edit == Draft::Notice) {
			text(EDITOR.x + 20, EDITOR.y + 40, draft.part + " hears notices about", INK, 22);
			const std::vector<std::string> items = about_list();
			button(edit_item(0), "any", draft.quantity.item.empty(), 14);
			for (int k = 0; k < static_cast<int>(items.size()); ++k) {
				button(edit_item(k + 1), items[static_cast<size_t>(k)], items[static_cast<size_t>(k)] == draft.quantity.item, 14);
			}
			for (int k = 0; k < 4; ++k) {
				button(level_rect(k), std::string(level_name(static_cast<lab::Notice::Level>(k))) + (k < 3 ? " up" : ""), draft.level == k);
			}
			field(says_field(), draft.says.empty() && focus != 6 ? std::string("(containing: click to type, or any words)") : draft.says, focus == 6);
			text(EDITOR.x + 20, EDITOR.y + 235, "signals for", INK, 16);
			field(hold_field(), draft.preset, focus == 2);
			text(EDITOR.x + 260, EDITOR.y + 235, "seconds", INK, 16);
			text(EDITOR.x + 20, EDITOR.y + 270, "It signals when a notice at that level or above is written, with those words in it.", DIM, 15);
			text(EDITOR.x + 20, EDITOR.y + 292, "Wire it to an alarm to stop the clock on, say, every warning.", DIM, 15);
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
		if (!guided() || asks_click("panel:invert")) {
			button(invert_button(), "upside down, with a cap");
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
	if (m.drag >= 0 && !m.carrying && down && (std::fabs(x - m.press_x) > 4 || std::fabs(y - m.press_y) > 4)) {
		m.moved = true; // a drag, not a click: a hand shakes a little
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
			m.turn(x);
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
		} else if (m.sensing >= 0) {
			const int from = m.sensing;
			m.sensing = -1;
			m.drop_sensing(from, x, y);
		} else if (m.drag >= 0 && m.carrying) {
			// carried (Stage 27): a held pour ends; a click puts it down, on its own place too
			if (m.pouring) {
				m.stop_pouring();
			} else if (m.putting_down) {
				m.putting_down = false;
				const int index = m.drag;
				const bool several = m.carrying_group();
				m.carrying = false;
				m.drag = -1;
				if (several) {
					m.drop_group(index, x, y);
				} else if (!m.own_place(index, x, y)) {
					m.drop(index, x, y);
				}
			}
		} else if (m.drag >= 0 && m.moved) {
			m.drag = -1; // a drag over an item moves nothing (Stage 27: dragging is for wires, sensing, sliders, rectangles)
		} else if (m.drag >= 0) {
			// a click on it picks it up, and chooses it (§13.1, Stage 27); with a modifier, adds it to the
			// selection, or takes it away
			if (mods & ModAny) {
				m.toggle(m.drag);
				m.drag = -1;
			} else {
				if (!m.chosen(m.drag) || !m.carrying_group_of(m.drag)) {
					m.choose_only(m.drag);
				}
				m.carrying = true;
			}
		} else if (m.marquee) {
			if (std::fabs(x - m.press_x) > 3 || std::fabs(y - m.press_y) > 3) {
				m.take_marquee();
			} else if (!(mods & ModAny)) {
				m.choose_only(-1); // a click on nothing lets go of the selection
			}
		}
		m.marquee = false;
		if (!m.carrying) {
			m.drag = -1;
		}
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
	if (m.pouring && m.down && m.drag >= 0 && seconds > 0.0) {
		// a held pour tilts further while the button is held (Stage 27): its rate is its tilt's share
		m.tilt = std::min(1.0, m.tilt + seconds / Impl::TILT_SECONDS);
		m.act({ { "do", "pour" }, { "from", m.id(m.drag) }, { "rate", m.tilt } });
	}
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

std::string Ui::take_export() {
	std::string out;
	out.swap(impl_->exported);
	return out;
}

const DrawList &Ui::draw() {
	impl_->paint();
	return impl_->list;
}

const OpticsScene &Ui::optics() {
	impl_->compose();
	return impl_->scene;
}

int Ui::prepare_optics() {
	Impl &m = *impl_;
	const LabView view = m.lab->view();
	int tables = 0;
	for (const ItemView &v : view.items) {
		if (v.kind != "vessel" || v.glassware.empty()) {
			continue;
		}
		float w = 0, h = 0;
		m.size_of(v, w, h);
		// at every size the screen draws it: on the bench, on the cabinet's shelf, in an outbox slot; standing or
		// in hand (a vessel carried from the shelf keeps the shelf's size)
		std::vector<float> scales{ 1.0f };
		for (const float scale : { m.cabinet_scale(w, h), Impl::outbox_scale(w, h) }) {
			if (std::find(scales.begin(), scales.end(), scale) == scales.end()) {
				scales.push_back(scale);
			}
		}
		for (const float scale : scales) {
			// alone, its axis on a whole pixel, as the screen draws it: its maps are its anywhere
			for (const int layer : { 0, 1 }) {
				OpticsRoom room;
				OpticsBody body{ &v, 100.0f - w * scale / 2.0f, 100.0f, w * scale, h * scale };
				body.layer = layer;
				OpticsScene alone;
				optics_compose(alone, room, { body });
				++tables;
			}
		}
	}
	return tables;
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
	if (what == "outbox") { // where a hand-in with no slot goes: the first free one, without a goal first
		const Rect r = Impl::slot_rect(m.free_slot());
		x = r.cx();
		y = r.y + r.h - 60;
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
		return i >= 0 && m.view.items[static_cast<size_t>(i)].kind != "tray" && m.visible(i) && centre(m.rect(i));
	}
	if (what == "notices") {
		return centre(Impl::notices_button());
	}
	if (what == "debug") {
		return centre(Impl::debug_button());
	}
	if (after("debug:", rest)) {
		for (int k = 0; k < 5; ++k) {
			if (rest == Impl::DEBUG_BUTTONS[k]) {
				return m.debug_open && centre(Impl::debug_rect(k));
			}
		}
		return false;
	}
	if (what == "notices:close") {
		return m.notices_open && centre(Impl::notices_close());
	}
	if (after("notices:", rest)) {
		lab::Notice::Level level = lab::Notice::Detail;
		return m.notices_open && level_of(rest, level) && centre(Impl::hear_rect(static_cast<int>(level)));
	}
	if (what == "circuit:create") {
		return !m.circuit_parts().empty() && centre(Impl::create_circuit_rect());
	}
	if (what == "circuit:close" || what == "circuit:up") {
		return !m.opened.empty() && (what == "circuit:close" || !m.view.items[static_cast<size_t>(m.opened_index())].part.in.empty()) &&
				centre(what == "circuit:close" ? m.circuit_close() : m.circuit_up());
	}
	if (after("inner:", rest)) {
		const size_t comma = rest.find(',');
		if (m.opened.empty() || comma == std::string_view::npos) {
			return false;
		}
		const int col = std::atoi(std::string(rest.substr(0, comma)).c_str()), row = std::atoi(std::string(rest.substr(comma + 1)).c_str());
		const Part &k = m.view.items[static_cast<size_t>(m.opened_index())].part;
		if (col < 0 || row < 0 || col >= k.inner_w || row >= k.inner_h) {
			return false;
		}
		const Rect g = m.circuit_grid();
		x = g.x + static_cast<float>(col) * 40 + 20;
		y = g.y + static_cast<float>(row) * 40 + 20;
		return true;
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
	if (after("valve:", rest) || after("cap:", rest)) {
		const size_t at = rest.find('@');
		const int i = index_of(rest.substr(0, at));
		Rect r;
		if (i < 0 || at == std::string_view::npos || !m.valve_rect(i, r)) {
			return false;
		}
		const double open = std::atof(std::string(rest.substr(at + 1)).c_str());
		x = r.x + r.w * static_cast<float>(open);
		y = r.cy();
		return true;
	}
	if (after("stir:", rest)) {
		const int i = index_of(rest);
		Rect r;
		return i >= 0 && m.stir_rect(i, r) && centre(r);
	}
	if (after("screw:", rest)) {
		const int i = index_of(rest);
		Rect r;
		return i >= 0 && m.screw_rect(i, r) && centre(r);
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
	if (what == "panel:invert") {
		x = Impl::invert_button().cx();
		y = Impl::invert_button().cy();
		return m.panel;
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
