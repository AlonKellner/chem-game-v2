#include "lab/ui.hpp"
#include "lab/contents.hpp"
#include "lab/glassware.hpp"
#include "lab/instruments.hpp"
#include "lab/notebook.hpp"
#include "lab/spectro.hpp"

#include "chem/builtin_ids.hpp"
#include "lab/calculator.hpp"
#include "lab/planner.hpp"
#include "lab/search.hpp"
#include "lab/conditions.hpp"
#include "lab/prism_demo.hpp"
#include "lab/renderer_key.hpp"
#include "lab/spectrometer.hpp"
#include "lab/thermal.hpp"
#include "lab/view.hpp"

#include "chem/optics.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <cstring>
#include <future>
#include <map>
#include <set>
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
constexpr Rect TRASH{ 10, 934, 220, 136 }; // at the cabinet's foot (Stage 34): what was put away last, to be taken up again
constexpr Rect BENCH{ 260, 120, 1120, 600 };
constexpr Rect OUTBOX{ 260, 760, 640, 300 };
constexpr Rect DRAWER{ 920, 760, 460, 300 }; // the drawer of signal parts (§13.3), beside the outbox
constexpr Rect EDITOR{ 440, 76, 720, 340 }; // a part's settings, or a rig's, being written
constexpr Rect READOUT{ 1400, 64, 520, 1016 };
constexpr Rect PANEL{ 600, 250, 440, 340 }; // the pour menu (Stage 27: and upside down, with a cap)
constexpr Rect TUTOR{ 1400, 64, 520, 502 }; // a tutorial's card, above the readout (§13.2)
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
constexpr Rect NOTEBOOK{ 860, 64, 530, 1010 }; // the notebook (Stage 54), over the bench's right: beside the readout and a report
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
	// the frame's budget for the lab's steps (Stage 36): 0, none
	double budget_ms = 0.0;
	std::function<double()> clock_ms = [] { return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now().time_since_epoch()).count(); };
	double achieved = 0.0; // the clock's speed kept to, over the last frames
	// The spectral viewers (GAME_SPEC §13.1, Stage 42): whose probe is in hand (-1: none), and whether a click took
	// it (the next press lets it go) or a press holds it (its release does); each viewer's probe and the picture on
	// its screen; the scene the probes read (the pass's own, without their heads and cables), and when it was made
	int probing = -1;
	bool probe_in_hand = false;
	// the view of the room (Stage 42), and how what is painted now is placed: 0 the screen's own (the top bar, the
	// readout, the panels), 1 the room's paint (the wall, the grid: placed and sized by the view, as the room is),
	// 2 over the room (labels and highlights: placed by the view, drawn at their own size)
	View vw = view_whole(VIEW_ROOM);
	View composed_at = view_whole(VIEW_ROOM); // the view the scene was last made at
	double view_changed_ms = -1.0e30;
	bool view_moving() const { return clock_ms() - view_changed_ms < Ui::VIEW_QUIET_MS; }
	void set_view(const View &v) {
		if (v != vw) {
			vw = v;
			view_changed_ms = clock_ms();
		}
	}
	int room_paint = 0;
	static constexpr ViewRoom VIEW_ROOM{ ROOM.x, ROOM.y, ROOM.w, ROOM.h };
	bool zoomed() const { return vw != view_whole(VIEW_ROOM); }
	float sx(float x) const { return screen_x(vw, VIEW_ROOM, x); }
	float sy(float y) const { return screen_y(vw, VIEW_ROOM, y); }
	Rect on_screen(const Rect &r) const { return Rect{ sx(r.x), sy(r.y), r.w * vw.scale, r.h * vw.scale }; }
	// whether a point of the screen is the room's (laid out by the view) or a panel's over it
	bool room_at(float x, float y) const {
		if (!ROOM.has(x, y) || draft.edit != Draft::None || panel || DRAWER.has(x, y) || (!opened.empty() && circuit_panel().has(x, y))) {
			return false;
		}
		return !(notices_open && NOTICES.has(x, y)) && !(debug_open && DEBUG.has(x, y)) && !(notebook_open && NOTEBOOK.has(x, y)) && !menu;
	}
	// whether a name of the screen (Ui::locate) is of the room: what stands in it and its places, not a panel's
	bool room_name(std::string_view what) const {
		static const char *const ROOMS[] = { "item:", "square:", "cabinet", "trash", "outbox", "slot:", "goal:", "port:", "sense:", "outlet:", "probe:", "holder:", "screen:", "frame:", "lens:", "plunger:" };
		for (const char *prefix : ROOMS) {
			const std::string_view p(prefix);
			if (what.substr(0, p.size()) != p) {
				continue;
			}
			// (a part in an open circuit is the circuit's panel's)
			const size_t colon = what.find(':');
			if (colon != std::string_view::npos && p.back() == ':') {
				std::string_view name = what.substr(colon + 1);
				name = name.substr(0, name.find('@'));
				const int i = item_index(std::string(name));
				if (i >= 0 && view.items[static_cast<size_t>(i)].place == "circuit") {
					return false;
				}
			}
			return true;
		}
		return false;
	}
	struct ViewerState {
		Spectrometer probe;
		Picture picture;
		bool plotted = false;
	};
	std::map<std::string, ViewerState> viewers;
	std::map<std::string, Picture> balances; // each balance's display as it last showed (Stage 50)
	std::map<std::string, Picture> spectro_shows; // each spectrophotometer's two displays, "<id>:a" and "<id>:nm" (Stage 55)
	// a thermal camera's (Stage 48): its search, what it last read, the span it shows, what its screen is given
	struct CameraState {
		ThermalSearch search;
		ThermalFrame searched;
		bool searching = false;
		ThermalReading last;
		double hottest_c = 0.0, coldest_c = 0.0, centre_c = 0.0;
		ThermalSpan span;
		ThermalOverlay overlay;
		OpticsRelay relay;
	};
	std::map<std::string, CameraState> cameras;
	mutable std::deque<OpticsRelay> relays_at_view; // (its relay at the view: the scene points at these)
	static constexpr int CAMERA_READS = 600; // a frame's reads of the scene for a camera's search
	// a camera's frame in hand (Stage 48): which camera's, and where on it the pointer took it
	int framing = -1;
	bool frame_in_hand = false;
	float frame_dx = 0.0f, frame_dy = 0.0f;
	OpticsScene reading;
	double reading_ms = -1.0e30;
	static constexpr double PROBE_BUDGET_MS = 2.0; // a frame's time for the probes' rays, all of them
	static constexpr int64_t PROBE_RAYS_MOST = 16384; // and at most these (a frame's on a clock that does not move)
	static constexpr double READING_EVERY_MS = 100.0; // the scene they read, made again at most this often while they rest
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
	// A press over a vessel: a pour by hand (Stage 27; Stage 31). What is in hand goes from the hand to where its
	// lip is over the other's mouth, and turns about its lip while the button is held; let go, it turns back
	// upright and comes back to the hand. `tilt`: its share of the full tilt; `eased`: how far from the hand
	// (0) to the pouring place (1); `pour_side`: the lip it pours from, the one nearer the other's axis.
	bool pouring = false;
	int pour_over = -1, pour_side = 1;
	double tilt = 0.0, eased = 0.0;
	static constexpr double TILT_SECONDS = 2.5; // from upright to the full tilt, while the button is held
	static constexpr double UNTILT_SECONDS = 0.6; // and back upright, let go
	static constexpr double EASE_SECONDS = 0.25; // from the hand to the pouring place, and back
	static constexpr float LIP_GAP = 10; // a pouring lip above the mouth it pours into
	static constexpr int STREAM_LENGTHS = 8; // a stream is drawn in at most so many lengths, each its own width
	static constexpr double STREAM_PI = 3.14159265358979323846;
	int valve_drag = -1; // the burner whose valve is being turned
	// (Stage 79) the syringe whose plunger's handle is held (a physical control on the bench: dragged along its barrel,
	// the hand pulls the plunger toward the pointer), and how far the handle's middle stood from the pointer when held
	int plunging = -1;
	float plunge_dy = 0.0f;
	bool slider_drag = false;
	// the cabinet (Stage 48): its first row on the screen, and whether its scroll bar is held
	int cabinet_from = 0;
	// (Stage 61) Supplies (the cabinet's place on the screen) and Storage (the outbox): where the supplies begin among
	// the screen's items (after the lab's own: each one the cabinet shows, as a copy of it is when taken out); Storage's
	// first slot on the screen and whether its bar is held; the two searches, and which of them is typed into (0 none,
	// 1 Supplies, 2 Storage)
	int supply_base = 0;
	// (Stage 61) a copy just taken out of Supplies by a press, in hand: the lab before it was taken, and whether it was
	// put somewhere it stays (a square, equipment, Storage)
	std::string fresh_copy, fresh_undo;
	// (Stage 61) the supplies' pictures, by what is drawn of each (picture_key); the one being captured apart (its
	// key, and its picture as it comes); whether one was captured since the host last took them; one to capture,
	// alone in the cabinet's first box (capture_one)
	std::map<std::string, Picture> pictures;
	std::string capturing_key;
	std::future<Picture> capturing;
	bool pictures_new = false;
	std::string capture_only;
	int storage_from = 0;
	bool storage_scrolling = false;
	std::string supplies_query, storage_query;
	int search_focus = 0;
	bool cabinet_scrolling = false;
	int hover = -1;
	// the pour panel
	bool panel = false;
	int from = -1, to = -1;
	double pour_fraction = 1.0;
	std::string amount; // the pour menu's amount as typed (Stage 50): mL of its liquid, or g of its solid when it has none
	// a tutorial (§13.2), and real time for its pointer's pulse
	std::unique_ptr<Tutorial> tutorial;
	std::string tutorial_json; // to open it again with another experiment's bench, and to know the experiment shown
	bool strict = true; // the card's switch (Stage 41): strict refuses what the step does not ask for; free only points
	// the experiments (§13.7, Stage 41): the catalogue, the one shown (-1: none of it), the list open
	struct Experiment {
		std::string id, title, summary, bench, tutorial; // bench and tutorial as JSON ("" for no tutorial)
		std::string scene; // or a scene of the optics alone (Stage 47: "prism", the prism and the grating), no bench
		std::vector<std::string> words; // (Stage 78) what the list's search matches (lab/search.hpp)
	};
	std::unique_ptr<PrismDemo> shown_scene; // the scene shown in place of the lab, if one is
	std::vector<Experiment> catalogue;
	int shown_k = -1; // (the experiment shown)
	bool menu = false;
	// (Stage 78) the list's search: its query, the catalogue's entries it leaves (in order), the first of them on the
	// screen, whether the list's bar is held, and a trackpad's pan not yet a whole row
	std::string menu_query;
	std::vector<int> menu_shown;
	int menu_from = 0;
	bool menu_scrolling = false;
	float menu_pan = 0.0f;
	double clock = 0.0;
	int selected = -1; // the last item chosen
	// signals (§13.3): the part editor, a part being made or wired, what rang
	Draft draft;
	int focus = -1; // the field typed into: 0, 1 a monitor's values; 2 a timer's preset; 3, 4, 5 a valve's setting on, off, on a change; 6 a writer's words, a goal's name; 10 + r a goal's term's value
	int pick_row = -1; // the goal's term whose quantity is being picked
	bool fresh = false; // the next key replaces the focused field
	bool resume = false; // the clock was playing when the editor opened
	int making = -1; // a part's kind, taken out of the drawer
	bool making_carried = false; // and in hand, no button held (Stage 34): the next press puts it down
	int wiring = -1; // the part whose port a wire is drawn from
	int sensing = -1; // the part whose sensing port a sensing line is drawn from (§13.3, Stage 26)
	int draining = -1; // the condenser whose outlet a line is drawn from, to the vessel it runs into (Stage 34)
	std::vector<std::string> rung, chain; // the alarms that rang, and the parts that set them off, until the clock plays
	std::string opened; // the circuit whose inside is shown (§13.3), "" for none
	// notices (§13.4): the levels shown (the others silenced, still kept), and the panel
	std::array<bool, 4> hear{ false, true, true, true };
	bool notices_open = false;
	// the notebook's panel (Stage 54): open; what its plot shows (x: a label, or "" for the time; y: a label), each axis's
	// transform, the fitted line
	bool notebook_open = false;
	// the report (Stage 54): what the student typed for each item, the field typed into, and each item as checked
	struct ReportCheck {
		bool done = false, within = false;
		double truth = 0.0, yours = 0.0;
		std::string why;
	};
	std::vector<std::string> answers;
	// (Stage 55) the report's predictions: what is typed for each before the run, and what was made (kept as typed)
	std::vector<std::string> guesses;
	std::set<std::string> done; // (Stage 55) the experiments completed: progress, the host's to keep
	std::vector<std::optional<double>> predictions;
	int guess_focus = -1;
	int report_focus = -1;
	std::vector<ReportCheck> report_checks;
	std::string plot_x, plot_y;
	AxisTransform plot_tx = AxisTransform::Plain, plot_ty = AxisTransform::Plain;
	bool plot_fit = true;
	// (Stage 55) the notebook's write row: a label and a number the student writes, and which field is typed in
	std::string note_label, note_value;
	int note_focus = 0; // 0 none, 1 the label, 2 the value
	bool note_fresh = false; // a field just clicked: what is typed next replaces it (Stage 56)
	// (Stage 58) the panel's tabs: the notebook, the calculator, the reference
	int notebook_tab = 0;
	// the calculator: what it works out, the reaction chosen, what is typed in each field, which field is typed in
	int calc_kind = 0, calc_reaction = 0, calc_focus = -1;
	bool calc_fresh = false;
	std::array<std::string, 8> calc_fields{};
	// the reference: its page; the model's solubility, worked out once it is shown
	int reference_page = 0;
	std::vector<std::pair<double, double>> solubility_rows; // C, mol/kg
	// (Stage 60) the planner: its target's fields, its route (each step's reaction and its yield's field), what is typed in
	std::string plan_target, plan_grams, plan_purity = "99";
	std::vector<int> plan_reactions;
	std::vector<std::string> plan_yields;
	std::string plan_focus;
	bool plan_fresh = false;
	// the debug panel (§13.6): open, its first line shown, and a save for the host to write
	bool debug_open = false;
	bool infrared = false; // the debug infrared view (Stage 46): the whole pass in the long-wave range
	int debug_from = 0;
	std::string exported;

	uint64_t viewed = 0; // the lab's stamp its view was taken at (Stage 68)
	LabView lab_view; // the lab's own view at that stamp
	void refresh() {
		// (Stage 68) the lab's view taken again only once the lab has changed (a tutorial's frame took it twice a second
		// of the clock); what the screen adds to it, as it now stands
		if (lab->stamp() != viewed) {
			lab_view = lab->view();
			viewed = lab->stamp();
		}
		view = lab_view;
		supply_base = static_cast<int>(view.items.size());
		for (const ItemView &s : view.supplies) { // (Stage 61) those the search leaves, in the cabinet's boxes
			if (capture_only.empty() ? lab::search::matches(supplies_query, lab::search::keywords(s)) : s.id == capture_only) {
				view.items.push_back(s);
				view.items.back().place = "cabinet";
			}
		}
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
			const std::vector<std::string> before = ids_now();
			if (act({ { "do", "circuit" }, { "parts", names } })) {
				choose_only(newest(before));
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

	// ---- the notebook (GAME_SPEC §13.1, Stage 54): its button, its panel's table, plot and logger
	static Rect notebook_button() { return Rect{ 1006, 14, 104, 36 }; } // after reset, before the clock (Stage 55: not over it)
	static constexpr const char *TABS[] = { "tab:notebook", "tab:calculator", "tab:reference", "tab:planner" };
	static constexpr const char *PAGES[] = { "elements", "species", "reactions", "solubility" };
	static constexpr const char *KINDS[] = { "mass", "moles", "solution", "dilution", "reaction" };
	static Rect notebook_rect(const std::string &what) {
		// close, erase; x, its transform, y, its transform, fit; log every 1, 5, 30 s, stop
		static const char *const NAMES[] = { "close", "erase", "x", "tx", "y", "ty", "fit", "log:1", "log:5", "log:30", "unlog" };
		// (Stage 58) the panel's tabs; the reference's pages; the calculator's kinds, its reaction's arrows, its note
		for (int k = 0; k < 4; ++k) {
			if (what == TABS[k]) {
				return Rect{ NOTEBOOK.x + 20 + static_cast<float>(k) * 84, NOTEBOOK.y + 12, 80, 30 }; // (Stage 60: four)
			}
		}
		if (what == "close" || what == "erase") {
			return Rect{ NOTEBOOK.x + NOTEBOOK.w - 100, NOTEBOOK.y + (what == "close" ? 12.0f : 52.0f), 80, 30 };
		}
		// (Stage 60) the planner: its target's fields, each step's arrows and yield, adding and taking off a step, the goal
		if (what == "plan:target" || what == "plan:grams" || what == "plan:purity") {
			const float x = what == "plan:target" ? 90.0f : what == "plan:grams" ? 300.0f : 430.0f;
			return Rect{ NOTEBOOK.x + x, NOTEBOOK.y + 100, what == "plan:target" ? 160.0f : what == "plan:grams" ? 80.0f : 60.0f, 30 };
		}
		if (what.rfind("plan:", 0) == 0 && (what.rfind("plan:prev:", 0) == 0 || what.rfind("plan:next:", 0) == 0 || what.rfind("plan:yield:", 0) == 0)) {
			const int k = std::atoi(what.c_str() + what.rfind(':') + 1);
			const float row = NOTEBOOK.y + 150 + static_cast<float>(k) * 66;
			if (what.rfind("plan:yield:", 0) == 0) {
				return Rect{ NOTEBOOK.x + 430, row + 20, 60, 30 };
			}
			return Rect{ NOTEBOOK.x + (what.rfind("plan:prev:", 0) == 0 ? 20.0f : 56.0f), row + 22, 32, 28 };
		}
		if (what == "plan:add" || what == "plan:remove") {
			return Rect{ NOTEBOOK.x + (what == "plan:add" ? 20.0f : 140.0f), NOTEBOOK.y + 420, 110, 30 };
		}
		if (what == "plan:goal") {
			return Rect{ NOTEBOOK.x + 20, NOTEBOOK.y + 960, 240, 30 };
		}
		for (int k = 0; k < 4; ++k) {
			if (what == std::string("ref:") + PAGES[k]) {
				return Rect{ NOTEBOOK.x + 20 + static_cast<float>(k) * 122, NOTEBOOK.y + 60, 116, 30 };
			}
		}
		for (int k = 0; k < 5; ++k) {
			if (what == std::string("calc:") + KINDS[k]) {
				return Rect{ NOTEBOOK.x + 20 + static_cast<float>(k) * 98, NOTEBOOK.y + 60, 94, 30 };
			}
		}
		if (what == "calc:prev" || what == "calc:next") {
			return Rect{ NOTEBOOK.x + (what == "calc:prev" ? 20.0f : 70.0f), NOTEBOOK.y + 104, 44, 30 };
		}
		if (what == "calc:note") {
			return Rect{ NOTEBOOK.x + 20, NOTEBOOK.y + 960, 220, 30 };
		}
		if (what.rfind("calc:field:", 0) == 0) {
			const int k = std::atoi(what.c_str() + 11);
			return Rect{ NOTEBOOK.x + 280, NOTEBOOK.y + 150 + static_cast<float>(k) * 40, 220, 30 };
		}
		// (Stage 55) the write row under the plot: its label's field, its value's, and "write"
		if (what == "write:label" || what == "write:value" || what == "write") {
			static const float AT[] = { 80, 236, 400 }, W[] = { 130, 130, 90 };
			const int k = what == "write:label" ? 0 : what == "write:value" ? 1 : 2;
			return Rect{ NOTEBOOK.x + AT[k], NOTEBOOK.y + 790, W[k], 30 };
		}
		for (int k = 0; k < 11; ++k) {
			if (what == NAMES[k]) {
				if (k < 2) {
					return Rect{ NOTEBOOK.x + NOTEBOOK.w - 210 + static_cast<float>(k) * 100, NOTEBOOK.y + 12, 90, 30 };
				}
				if (k < 7) {
					static const float AT[] = { 20, 170, 240, 390, 460 }, W[] = { 145, 64, 145, 64, 64 }; // (Stage 55: within the panel)
					return Rect{ NOTEBOOK.x + AT[k - 2], NOTEBOOK.y + 316, W[k - 2], 30 };
				}
				return Rect{ NOTEBOOK.x + 150 + static_cast<float>(k - 7) * 84, NOTEBOOK.y + 980, 78, 28 };
			}
		}
		return Rect{};
	}
	static Rect plot_area() { return Rect{ NOTEBOOK.x + 60, NOTEBOOK.y + 370, NOTEBOOK.w - 90, 330 }; }
	// the notebook's columns: its labels, in the order first written
	std::vector<std::string> note_labels() const {
		std::vector<std::string> out;
		for (const NoteEntry &e : view.notebook) {
			if (std::find(out.begin(), out.end(), e.label) == out.end()) {
				out.push_back(e.label);
			}
		}
		return out;
	}
	bool note_celsius(const std::string &label) const {
		for (const NoteEntry &e : view.notebook) {
			if (e.label == label) {
				return e.quantity.what == "celsius";
			}
		}
		return false;
	}
	// the plot's points: y's entries against the time, or against x's entry of the same moment, each transformed
	std::vector<std::pair<double, double>> plot_points() const {
		// (Stage 55) a y pairs with the x written at the same moment, or else the last x written before it: a logged pair,
		// or a number written and then a reading recorded (a standard's concentration, then its absorbance). Entries are
		// written in the order of their moments: one pass, a moment at a time.
		std::vector<std::pair<double, double>> out;
		std::optional<double> last; // the last x of the moments before
		for (size_t g = 0; g < view.notebook.size();) {
			size_t end = g;
			std::optional<double> here; // the x written at this moment
			while (end < view.notebook.size() && view.notebook[end].step == view.notebook[g].step) {
				if (!plot_x.empty() && view.notebook[end].label == plot_x) {
					here = view.notebook[end].value;
				}
				++end;
			}
			for (size_t i = g; i < end; ++i) {
				const NoteEntry &e = view.notebook[i];
				if (e.label != plot_y) {
					continue;
				}
				const std::optional<double> x = plot_x.empty() ? std::optional<double>(static_cast<double>(e.step) * STEP_SECONDS) : here ? here : last;
				if (!x) {
					continue;
				}
				const std::optional<double> tx = transformed(*x, plot_tx, !plot_x.empty() && note_celsius(plot_x)), ty = transformed(e.value, plot_ty, note_celsius(plot_y));
				if (tx && ty) {
					out.emplace_back(*tx, *ty);
				}
			}
			last = here ? here : last;
			g = end;
		}
		return out;
	}
	bool notebook_press(float x, float y) {
		if (notebook_button().has(x, y)) {
			notebook_open = !notebook_open;
			notices_open = notices_open && !notebook_open;
			debug_open = debug_open && !notebook_open;
			return true;
		}
		if (!notebook_open) {
			return false;
		}
		for (int k = 0; k < 4; ++k) {
			if (notebook_rect(TABS[k]).has(x, y)) {
				notebook_tab = k; // (Stage 58)
				note_focus = 0;
				calc_focus = -1;
				return true;
			}
		}
		if (notebook_rect("close").has(x, y)) {
			notebook_open = false;
			return true;
		}
		if (notebook_tab == 1) {
			calc_press(x, y);
			return NOTEBOOK.has(x, y);
		}
		if (notebook_tab == 3) {
			plan_press(x, y);
			return NOTEBOOK.has(x, y);
		}
		if (notebook_tab == 2) {
			for (int k = 0; k < 4; ++k) {
				if (notebook_rect(std::string("ref:") + PAGES[k]).has(x, y)) {
					reference_page = k;
				}
			}
			return NOTEBOOK.has(x, y);
		}
		const std::vector<std::string> labels = note_labels();
		const auto next = [&](const std::string &now, bool time) {
			// the next column after `now`: the time first when it may be one
			std::vector<std::string> all;
			if (time) {
				all.emplace_back();
			}
			all.insert(all.end(), labels.begin(), labels.end());
			if (all.empty()) {
				return now;
			}
			const auto at = std::find(all.begin(), all.end(), now);
			return at == all.end() || at + 1 == all.end() ? all.front() : *(at + 1);
		};
		const auto cycle = [](AxisTransform t) { return static_cast<AxisTransform>((static_cast<int>(t) + 1) % 3); };
		note_focus = notebook_rect("write:label").has(x, y) ? 1 : notebook_rect("write:value").has(x, y) ? 2 : 0;
		note_fresh = note_focus != 0;
		if (notebook_rect("close").has(x, y)) {
			notebook_open = false;
		} else if (notebook_rect("write").has(x, y)) {
			write_note();
		} else if (notebook_rect("erase").has(x, y)) {
			act({ { "do", "erase" } });
		} else if (notebook_rect("x").has(x, y)) {
			plot_x = next(plot_x, true);
		} else if (notebook_rect("tx").has(x, y)) {
			plot_tx = cycle(plot_tx);
		} else if (notebook_rect("y").has(x, y)) {
			plot_y = next(plot_y, false);
		} else if (notebook_rect("ty").has(x, y)) {
			plot_ty = cycle(plot_ty);
		} else if (notebook_rect("fit").has(x, y)) {
			plot_fit = !plot_fit;
		} else if (const auto read = std::find_if(view.notebook.rbegin(), view.notebook.rend(), [](const NoteEntry &e) { return !e.quantity.item.empty(); });
				read != view.notebook.rend()) {
			// a logger for the last quantity recorded (not a number written)
			const NoteEntry &last = *read;
			const json q = { { "item", last.quantity.item }, { "what", last.quantity.what }, { "measure", last.quantity.measure } };
			for (const double every : { 1.0, 5.0, 30.0 }) {
				if (notebook_rect("log:" + text_of("%.0f", every)).has(x, y)) {
					json c = q;
					c["do"] = "log";
					c["every"] = every;
					c["label"] = last.label;
					act(c);
				}
			}
			if (notebook_rect("unlog").has(x, y)) {
				json c = q;
				c["do"] = "unlog";
				act(c);
			}
		}
		return NOTEBOOK.has(x, y); // the panel takes what falls on it
	}
	// (Stage 55) the write row's number into the notebook under its label; the label stays for the next
	void write_note() {
		char *end = nullptr;
		const double v = std::strtod(note_value.c_str(), &end);
		if (note_label.empty() || note_value.empty() || end == nullptr || *end != '\0') {
			message = "write a label and a number, then \"write\"";
			return;
		}
		if (act({ { "do", "note" }, { "label", note_label }, { "value", v } })) {
			note_value.clear();
			note_focus = 2;
		}
	}
	// ---- the supplies' pictures (Stage 61): what is drawn of a supply, as a key (FNV-1a: the same on every build of
	// the same source, so a picture built ahead is found again), and what it is rendered by (Stage 86: the renderer's
	// files, RENDERER_KEY; what bodies a view composes here is "pictures", the key's version); a box's pixels read from
	// a scene; one captured alone
	OpticsRoom room_was, room_drawn; // (the wall behind glass, and as it is painted)
	bool capture_ahead = false; // (a host's: supplies with no picture captured apart, one at a time)
	static std::string picture_key(const ItemView &s) {
		json j = { { "pictures", 3 }, { "renderer", RENDERER_KEY }, { "box", CAB_BOX }, { "kind", s.kind }, { "glassware", s.glassware }, { "ml", s.nominal_ml }, { "volume", s.volume_ml }, { "w", s.w },
			{ "h", s.h }, { "label", s.label }, { "wall", s.wall_mm }, { "floor", s.floor_mm }, { "condenser", s.condenser } };
		json held = json::array();
		for (const SubstanceView &c : s.contents) {
			held.push_back({ c.species, std::string(1, c.phase), std::round(c.mol * 1e9) / 1e9, c.particles, std::round(c.size_um) });
		}
		j["holds"] = held;
		const std::string text = j.dump();
		uint64_t h = 1469598103934665603ull;
		for (const char c : text) {
			h = (h ^ static_cast<uint8_t>(c)) * 1099511628211ull;
		}
		char out[17];
		std::snprintf(out, sizeof out, "%016llx", static_cast<unsigned long long>(h));
		return out;
	}
	// the wall's grey where no body is (as the room's shapes draw it)
	static double wall_at(const OpticsRoom &room, double x, double y) {
		double r = x >= room.x && x < room.x + room.w && y >= room.y && y < room.y + room.h ? room.bench : room.elsewhere;
		for (const OpticsShape &s : room.shapes) {
			if (x >= s.x && x < s.x + s.w && y >= s.y && y < s.y + s.h) {
				r = s.radiance;
			}
		}
		return r;
	}
	static Picture capture_box(const OpticsScene &scene, const OpticsRoom &room, Rect box) {
		Picture p;
		p.w = static_cast<int>(box.w);
		p.h = static_cast<int>(box.h);
		p.rgb.assign(static_cast<size_t>(p.w) * static_cast<size_t>(p.h) * 3, 0);
		const auto encode = [](double v) {
			v = std::clamp(v, 0.0, 1.0);
			return v <= 0.0031308 ? 12.92 * v : 1.055 * std::pow(v, 1.0 / 2.4) - 0.055;
		};
		uint64_t h = 1469598103934665603ull;
		for (int y = 0; y < p.h; ++y) {
			for (int x = 0; x < p.w; ++x) {
				const int sx = static_cast<int>(box.x) + x, sy = static_cast<int>(box.y) + y;
				double rgba[4];
				optics_pixel(scene, sx, sy, rgba);
				const double wall = encode(wall_at(room, sx + 0.5, sy + 0.5));
				for (int c = 0; c < 3; ++c) {
					const double v = rgba[c] * rgba[3] + wall * (1.0 - rgba[3]);
					const auto byte = static_cast<uint8_t>(std::lround(std::clamp(v, 0.0, 1.0) * 255.0));
					p.rgb[(static_cast<size_t>(y) * static_cast<size_t>(p.w) + static_cast<size_t>(x)) * 3 + static_cast<size_t>(c)] = byte;
					h = (h ^ byte) * 1099511628211ull;
				}
			}
		}
		p.hash = h;
		return p;
	}
	// a supply alone in the cabinet's first box, composed; its picture captured now (wait), or apart
	void capture_one(const std::string &supply, bool wait) {
		const ItemView *s = nullptr;
		for (const ItemView &v : view.supplies) {
			s = v.id == supply ? &v : s;
		}
		if (s == nullptr) {
			return;
		}
		const std::string key = picture_key(*s);
		const int scrolled = cabinet_from;
		const View seen = vw;
		vw = view_whole(VIEW_ROOM);
		capture_only = supply;
		cabinet_from = 0;
		refresh();
		compose();
		const std::optional<Rect> box = cabinet_box(0);
		if (box) {
			if (wait) {
				pictures[key] = capture_box(scene, room_drawn, *box);
				pictures_new = true;
			} else {
				capturing_key = key;
				capturing = std::async(std::launch::async, [frozen = scene, room = room_drawn, at = *box]() { return capture_box(frozen, room, at); });
			}
		}
		capture_only.clear();
		cabinet_from = scrolled;
		vw = seen;
		refresh();
	}
	// at a frame: a picture captured apart taken in; the next supply with none, begun
	void follow_pictures() {
		if (capturing.valid() && capturing.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
			pictures[capturing_key] = capturing.get();
			pictures_new = true;
		}
		if (!capture_ahead || capturing.valid()) {
			return;
		}
		for (const ItemView &s : view.supplies) {
			if (pictures.find(picture_key(s)) == pictures.end()) {
				capture_one(s.id, false);
				return;
			}
		}
	}
	// ---- the searches (Stage 61): a field over Supplies and one over Storage
	static Rect search_rect(int which) { return which == 1 ? Rect{ CABINET.x + 104, CABINET.y + 6, 126, 26 } : Rect{ OUTBOX.x + OUTBOX.w - 230, OUTBOX.y + 5, 222, 26 }; }
	bool search_press(float x, float y) {
		for (const int which : { 1, 2 }) {
			if (search_rect(which).has(x, y)) {
				search_focus = which;
				return true;
			}
		}
		search_focus = 0;
		return false;
	}
	bool type_search(Ui::Key key, uint32_t codepoint) {
		if (search_focus == 0) {
			return false;
		}
		std::string &q = search_focus == 1 ? supplies_query : storage_query;
		if (key == Ui::KeyText && codepoint >= 32 && codepoint < 127) {
			if (q.size() < 40) {
				q += static_cast<char>(codepoint);
			}
		} else if (key == Ui::KeyBackspace) {
			if (!q.empty()) {
				q.pop_back();
			}
		} else if (key == Ui::KeyEscape) {
			q.clear();
			search_focus = 0;
		} else if (key == Ui::KeyEnter) {
			search_focus = 0;
		}
		cabinet_from = 0;
		storage_from = 0;
		refresh();
		return true;
	}
	void paint_search(int which) {
		const Rect r = search_rect(which);
		const std::string &q = which == 1 ? supplies_query : storage_query;
		rect_fill(r, PANEL_FILL);
		outline(r, search_focus == which ? GUIDE : GRID, search_focus == which ? 2.0f : 1.0f);
		text(r.x + 8, r.y + 18, q.empty() && search_focus != which ? "search" : q + (search_focus == which ? "_" : ""), q.empty() ? DIM : INK, 14);
	}
	bool type_note(Ui::Key key, uint32_t codepoint) {
		if (!notebook_open || note_focus == 0) {
			return false;
		}
		std::string &f = note_focus == 1 ? note_label : note_value;
		const bool letter = (codepoint >= 'a' && codepoint <= 'z') || (codepoint >= 'A' && codepoint <= 'Z') || codepoint == ' ' || codepoint == '_';
		const bool digit = (codepoint >= '0' && codepoint <= '9') || codepoint == '.' || codepoint == '-' || codepoint == 'e';
		const bool replacing = note_fresh;
		note_fresh = false;
		if (key == Ui::KeyText && (digit || (note_focus == 1 && letter))) {
			if (replacing) {
				f.clear(); // a field just clicked: what is typed replaces it (Stage 56)
			}
			if (f.size() < 16) {
				f += static_cast<char>(codepoint);
			}
		} else if (key == Ui::KeyBackspace && !f.empty()) {
			f.pop_back();
		} else if (key == Ui::KeyEnter) {
			write_note();
		} else if (key == Ui::KeyEscape) {
			note_focus = 0;
		}
		return true;
	}
	// ---- the calculator (GAME_SPEC §13.1, Stage 58): what it works out, its fields, what follows
	struct CalcField {
		std::string label;
		bool formula = false;
	};
	std::vector<CalcField> calc_fields_of() const {
		switch (calc_kind) {
		case 0:
			return { { "formula", true } };
		case 1:
			return { { "formula", true }, { "mass (g)", false } };
		case 2:
			return { { "the solid's formula", true }, { "concentration (mol/L)", false }, { "volume (mL)", false } };
		case 3:
			return { { "the stock (mol/L)", false }, { "wanted (mol/L)", false }, { "made up to (mL)", false } };
		default: {
			std::vector<CalcField> out;
			const chem::Reaction &r = calc_reaction_def();
			const chem::SpeciesTable &table = chem::builtin_species();
			std::string product;
			for (const chem::Term &t : calc::written_terms(r)) {
				out.push_back({ chem::written(table.species(t.species).formula) + (t.nu < 0 ? " in (g)" : " made (g)"), false });
				product = product.empty() && t.nu > 0 ? chem::written(table.species(t.species).formula) : product;
			}
			out.push_back({ "got of " + product + " (g)", false });
			return out;
		}
		}
	}
	const chem::Reaction &calc_reaction_def() const {
		const auto all = chem::builtin_reactions();
		return all[static_cast<size_t>(std::clamp(calc_reaction, 0, static_cast<int>(all.size()) - 1))];
	}
	std::optional<double> calc_number(int k) const {
		const std::string &f = calc_fields[static_cast<size_t>(k)];
		char *end = nullptr;
		const double v = std::strtod(f.c_str(), &end);
		return !f.empty() && end != nullptr && *end == '\0' && std::isfinite(v) ? std::optional<double>(v) : std::nullopt;
	}
	struct CalcResult {
		bool ok = false;
		std::vector<std::string> lines;
		double main = 0.0;
		std::string label, unit;
	};
	CalcResult calc_result() const {
		CalcResult out;
		const chem::SpeciesTable &table = chem::builtin_species();
		const auto formula = [&](int k, calc::MolarMass &mm) {
			mm = calc::molar_mass(table, calc_fields[static_cast<size_t>(k)]);
			if (!mm.ok) {
				out.lines.push_back(calc_fields[static_cast<size_t>(k)].empty() ? "type a formula, such as CuSO4.5H2O" : mm.why);
				return false;
			}
			out.lines.push_back(text_of("M = %.3f g/mol", mm.g_per_mol));
			return true;
		};
		calc::MolarMass mm;
		switch (calc_kind) {
		case 0:
			if (formula(0, mm)) {
				for (size_t i = 0; i < mm.shares.size(); ++i) {
					out.lines.push_back(mm.shares[i].first + text_of(": %.2f %% by mass", mm.shares[i].second) + text_of(" (%.0f atoms)", static_cast<double>(mm.atoms[i].second)));
				}
				out = CalcResult{ true, out.lines, mm.g_per_mol, "M", "g/mol" };
			}
			break;
		case 1:
			if (formula(0, mm)) {
				if (const std::optional<double> m = calc_number(1)) {
					const double n = calc::moles(*m, mm.g_per_mol);
					out.lines.push_back(text_of("n = m / M = %.6g", *m) + text_of(" / %.3f", mm.g_per_mol) + text_of(" = %.5g mol", n));
					out = CalcResult{ true, out.lines, n, "n", "mol" };
				} else {
					out.lines.push_back("type its mass in grams");
				}
			}
			break;
		case 2:
			if (formula(0, mm)) {
				const std::optional<double> c = calc_number(1), v = calc_number(2);
				if (c && v) {
					const double m = calc::solution_grams(*c, *v, mm.g_per_mol);
					out.lines.push_back(text_of("m = c V M = %.5g mol/L", *c) + text_of(" x %.5g L", *v / 1000.0) + text_of(" x %.3f g/mol", mm.g_per_mol));
					out.lines.push_back(text_of("  = %.4f g, made up to the volume", m));
					out = CalcResult{ true, out.lines, m, "m", "g" };
				} else {
					out.lines.push_back("type the concentration and the volume");
				}
			}
			break;
		case 3: {
			const std::optional<double> c1 = calc_number(0), c2 = calc_number(1), v2 = calc_number(2);
			if (c1 && c2 && v2 && *c1 > 0.0) {
				const double v1 = calc::dilution_ml(*c1, *c2, *v2);
				out.lines.push_back(text_of("V1 = c2 V2 / c1 = %.5g", *c2) + text_of(" x %.5g", *v2) + text_of(" / %.5g", *c1) + text_of(" = %.3f mL of the stock", v1));
				out.lines.push_back(text_of("made up to %.5g mL", *v2));
				out = CalcResult{ true, out.lines, v1, "V1", "mL" };
			} else {
				out.lines.push_back("type the three: c1 V1 = c2 V2");
			}
			break;
		}
		default: {
			const chem::Reaction &r = calc_reaction_def();
			const auto terms = calc::written_terms(r);
			std::vector<std::optional<double>> given;
			for (size_t k = 0; k < terms.size(); ++k) {
				given.push_back(calc_number(static_cast<int>(k)));
			}
			const calc::Stoichiometry st = calc::stoichiometry(table, r, given);
			if (!st.ok) {
				out.lines.push_back(st.why);
				break;
			}
			for (const calc::Stoichiometry::Term &t : st.terms) {
				if (t.allows) {
					out.lines.push_back(t.formula + text_of(": %.5g g", *t.given_g) + text_of(" = %.5g mol, enough for %.5g mol of the reaction", *t.given_g / t.g_per_mol, *t.allows));
				}
			}
			out.lines.push_back("limiting: " + st.terms[static_cast<size_t>(st.limiting)].formula + text_of(", %.5g mol of the reaction", st.extent));
			int first = -1;
			for (size_t k = 0; k < st.terms.size(); ++k) {
				const calc::Stoichiometry::Term &t = st.terms[k];
				out.lines.push_back(text_of("  %.4g g of ", t.g) + t.formula + (t.nu < 0 ? " used" : " made"));
				first = first < 0 && t.nu > 0 ? static_cast<int>(k) : first;
			}
			out = CalcResult{ true, out.lines, first >= 0 ? st.terms[static_cast<size_t>(first)].g : st.extent, "theoretical", "g" };
			if (const std::optional<double> got = calc_number(static_cast<int>(terms.size())); got && first >= 0) {
				const double y = calc::percent_yield(*got, st.terms[static_cast<size_t>(first)].g);
				out.lines.push_back(text_of("yield = %.5g g", *got) + text_of(" / %.4g g", st.terms[static_cast<size_t>(first)].g) + text_of(" = %.1f %%", y));
				out.main = y;
				out.label = "yield";
				out.unit = "%";
			}
			break;
		}
		}
		return out;
	}
	void calc_press(float x, float y) {
		for (int k = 0; k < 5; ++k) {
			if (notebook_rect(std::string("calc:") + KINDS[k]).has(x, y)) {
				if (calc_kind != k) {
					calc_kind = k;
					calc_fields = {};
				}
				calc_focus = -1;
				return;
			}
		}
		const int reactions = static_cast<int>(chem::builtin_reactions().size());
		if (calc_kind == 4 && (notebook_rect("calc:prev").has(x, y) || notebook_rect("calc:next").has(x, y))) {
			calc_reaction = (calc_reaction + (notebook_rect("calc:next").has(x, y) ? 1 : reactions - 1)) % reactions;
			calc_fields = {};
			calc_focus = -1;
			return;
		}
		if (notebook_rect("calc:note").has(x, y)) {
			const CalcResult r = calc_result();
			if (r.ok) {
				act({ { "do", "note" }, { "label", r.label }, { "value", r.main }, { "unit", r.unit } });
			}
			return;
		}
		calc_focus = -1;
		for (int k = 0; k < static_cast<int>(calc_fields_of().size()) && k < 8; ++k) {
			if (notebook_rect("calc:field:" + std::to_string(k)).has(x, y)) {
				calc_focus = k;
				calc_fresh = true;
			}
		}
	}
	bool type_calc(Ui::Key key, uint32_t codepoint) {
		if (!notebook_open || notebook_tab != 1 || calc_focus < 0) {
			return false;
		}
		const std::vector<CalcField> fields = calc_fields_of();
		if (calc_focus >= static_cast<int>(fields.size())) {
			return false;
		}
		std::string &f = calc_fields[static_cast<size_t>(calc_focus)];
		const bool formula = fields[static_cast<size_t>(calc_focus)].formula;
		const bool ok = formula ? (std::isalnum(static_cast<int>(codepoint)) != 0 || codepoint == '(' || codepoint == ')' || codepoint == '.')
								: ((codepoint >= '0' && codepoint <= '9') || codepoint == '.' || codepoint == '-' || codepoint == 'e');
		const bool replacing = calc_fresh;
		calc_fresh = false;
		if (key == Ui::KeyText && ok && codepoint < 128) {
			if (replacing) {
				f.clear();
			}
			if (f.size() < 24) {
				f += static_cast<char>(codepoint);
			}
		} else if (key == Ui::KeyBackspace && !f.empty()) {
			f.pop_back();
		} else if (key == Ui::KeyEnter || key == Ui::KeyEscape) {
			calc_focus = -1;
		}
		return true;
	}
	void paint_calculator() {
		static const char *const NAMES[] = { "molar mass", "moles", "solution", "dilution", "reaction" };
		for (int k = 0; k < 5; ++k) {
			button(notebook_rect(std::string("calc:") + KINDS[k]), NAMES[k], calc_kind == k, 13);
		}
		const chem::SpeciesTable &table = chem::builtin_species();
		if (calc_kind == 4) {
			button(notebook_rect("calc:prev"), "<", false, 15);
			button(notebook_rect("calc:next"), ">", false, 15);
			text(NOTEBOOK.x + 126, NOTEBOOK.y + 125, calc::equation(table, calc_reaction_def()), INK, 13);
		}
		const std::vector<CalcField> fields = calc_fields_of();
		for (size_t k = 0; k < fields.size() && k < 8; ++k) {
			const Rect f = notebook_rect("calc:field:" + std::to_string(k));
			text(NOTEBOOK.x + 20, f.y + 21, fields[k].label, INK, 14);
			rect_fill(f, rgba(24, 24, 24));
			const bool typing = calc_focus == static_cast<int>(k);
			outline(f, typing ? GUIDE : GRID, typing ? 2.0f : 1.0f);
			text(f.x + 8, f.y + 21, calc_fields[k] + (typing ? "_" : ""), INK, 15);
		}
		const CalcResult r = calc_result();
		float y = NOTEBOOK.y + 150 + static_cast<float>(std::min<size_t>(fields.size(), 8)) * 40 + 30;
		for (const std::string &line : r.lines) {
			for (const std::string &l : wrapped(line, 58)) {
				text(NOTEBOOK.x + 20, y, l, r.ok ? INK : DIM, 15);
				y += 21;
			}
		}
		button(notebook_rect("calc:note"), r.ok ? "write " + r.label + " in the notebook" : std::string("nothing to write yet"), false, 13);
	}
	// ---- the reference (GAME_SPEC §13.1, Stage 58): the simulation's own tables
	void paint_reference() {
		static const char *const NAMES[] = { "elements", "species", "reactions", "solubility" };
		for (int k = 0; k < 4; ++k) {
			button(notebook_rect(std::string("ref:") + PAGES[k]), NAMES[k], reference_page == k, 13);
		}
		const chem::SpeciesTable &table = chem::builtin_species();
		const float x0 = NOTEBOOK.x + 20, y0 = NOTEBOOK.y + 110;
		if (reference_page == 0) {
			// the periodic table's first four periods; the elements the tables hold, with their weights
			static const char *const SYMBOLS[] = { "H", "He", "Li", "Be", "B", "C", "N", "O", "F", "Ne", "Na", "Mg", "Al", "Si", "P", "S", "Cl", "Ar", "K", "Ca", "Sc", "Ti", "V", "Cr", "Mn", "Fe", "Co", "Ni", "Cu", "Zn", "Ga",
				"Ge", "As", "Se", "Br", "Kr" };
			for (int z = 1; z <= 36; ++z) {
				const int period = z <= 2 ? 1 : z <= 10 ? 2 : z <= 18 ? 3 : 4;
				const int first = period == 1 ? 1 : period == 2 ? 3 : period == 3 ? 11 : 19, i = z - first;
				const int column = period == 1 ? (z == 1 ? 1 : 18) : period == 4 ? i + 1 : (i < 2 ? i + 1 : i + 11);
				const Rect cell{ x0 + static_cast<float>(column - 1) * 27, y0 + 10 + static_cast<float>(period - 1) * 36, 25, 33 };
				bool held = false;
				for (int e = 0; e < table.element_count(); ++e) {
					held = held || table.element(e).symbol == SYMBOLS[z - 1];
				}
				rect_fill(cell, held ? BUTTON_ON : BUTTON);
				text(cell.x + 3, cell.y + 14, SYMBOLS[z - 1], held ? INK : DIM, 13);
				text(cell.x + 3, cell.y + 29, std::to_string(z), DIM, 10);
			}
			float y = y0 + 170;
			text(x0, y, "in the simulation's tables (standard atomic weights):", DIM, 14);
			for (int e = 0; e < table.element_count(); ++e) {
				y += 21;
				text(x0, y, std::string(table.element(e).symbol) + text_of("  %.3f g/mol", static_cast<double>(table.element(e).mass_ug_per_mol) * 1e-6), INK, 14);
			}
		} else if (reference_page == 1) {
			float y = y0 + 20;
			text(x0, y, "formula", DIM, 13);
			text(x0 + 170, y, "phase", DIM, 13);
			text(x0 + 220, y, "M g/mol", DIM, 13);
			text(x0 + 300, y, "Hf kJ/mol", DIM, 13);
			text(x0 + 385, y, "S J/mol K", DIM, 13);
			text(x0 + 455, y, "Cp", DIM, 13);
			static const char *const PHASES[] = { "s", "l", "g", "aq" };
			for (int s = 0; s < table.species_count(); ++s) {
				const chem::SpeciesDef &d = table.species(chem::SpeciesId{ s });
				y += 21;
				text(x0, y, chem::written(d.formula) + (d.wall ? " (wall)" : ""), INK, 13);
				text(x0 + 170, y, PHASES[static_cast<int>(d.phase) & 3], DIM, 13);
				text(x0 + 220, y, text_of("%.2f", static_cast<double>(table.molar_mass_ug(chem::SpeciesId{ s })) * 1e-6), INK, 13);
				if (d.has_thermo) {
					text(x0 + 300, y, text_of("%.2f", d.hf / 1000.0), INK, 13);
					text(x0 + 385, y, text_of("%.2f", d.s0), INK, 13);
					text(x0 + 455, y, text_of("%.1f", d.cp), INK, 13);
				}
			}
		} else if (reference_page == 2) {
			float y = y0;
			for (const chem::Reaction &r : chem::builtin_reactions()) {
				y += 22;
				text(x0, y, calc::equation(table, r), INK, 13);
				y += 18;
				const chem::Kinetics &k = r.kinetics();
				const std::string law = k.law == chem::Kinetics::Law::FirstOrder ? text_of("first order, Ea %.1f kJ/mol", k.activation / 1000.0)
						: k.law == chem::Kinetics::Law::Surface											   ? text_of("at a surface, Ea %.1f kJ/mol", k.activation / 1000.0) + (table.has(k.driver) ? text_of(", order %.2g in ", k.driver_order) + chem::written(table.species(k.driver).formula) : std::string())
						: k.law == chem::Kinetics::Law::Equilibrium										   ? std::string("fast: always at its equilibrium")
																										   : std::string("runs when it is brought about");
				text(x0 + 16, y, law, DIM, 12);
			}
		} else {
			// the model's own solubility: copper sulfate under its pentahydrate, at each ten degrees
			if (solubility_rows.empty()) {
				const chem::Reaction &r = chem::builtin_reactions()[static_cast<size_t>(chem::builtin::reaction::pentahydrate_crystallization)];
				for (int c = 0; c <= 90; c += 10) {
					solubility_rows.emplace_back(c, calc::solubility(table, r, chem::builtin::copper_sulfate_aq, c + 273.15));
				}
			}
			float y = y0 + 20;
			text(x0, y, "copper sulfate in water, under its blue crystals: the simulation's own", DIM, 13);
			y += 30;
			text(x0, y, "C", DIM, 13);
			text(x0 + 80, y, "mol/kg", DIM, 13);
			text(x0 + 200, y, "g CuSO4 per 100 g of water", DIM, 13);
			for (const auto &[c, m] : solubility_rows) {
				y += 22;
				text(x0, y, text_of("%.0f", c), INK, 14);
				text(x0 + 80, y, text_of("%.3f", m), INK, 14);
				text(x0 + 200, y, text_of("%.1f", m * 159.61 / 10.0), INK, 14);
			}
		}
	}
	// ---- the planner (GAME_SPEC §13.1, Stage 60): a target, the route back to it, what each step needs, the goal
	chem::SpeciesId plan_species() const {
		const chem::SpeciesTable &table = chem::builtin_species();
		chem::SpeciesId best{};
		int rank = 9;
		for (int s = 0; s < table.species_count(); ++s) {
			const chem::SpeciesDef &d = table.species(chem::SpeciesId{ s });
			if (d.wall || std::string(d.formula) != plan_target) {
				continue;
			}
			const int r = d.phase == chem::Phase::Solid ? 0 : d.phase == chem::Phase::Aqueous ? 1 : d.phase == chem::Phase::Liquid ? 2 : 3;
			if (r < rank) {
				rank = r;
				best = chem::SpeciesId{ s };
			}
		}
		return best;
	}
	// what step k is asked to make: the target, or the first reactant of the step before that some reaction makes
	chem::SpeciesId plan_asked(size_t k) const {
		if (k == 0) {
			return plan_species();
		}
		const auto all = chem::builtin_reactions();
		const chem::SpeciesTable &table = chem::builtin_species();
		if (k > plan_reactions.size() || plan_reactions[k - 1] < 0) {
			return {};
		}
		for (const chem::Term &t : calc::written_terms(all[static_cast<size_t>(plan_reactions[k - 1])])) {
			const chem::SpeciesDef &d = table.species(t.species);
			if (t.nu < 0 && std::string(d.formula) != "H2O" && !plan::makers(t.species).empty()) {
				return t.species;
			}
		}
		return {};
	}
	std::vector<plan::Step> plan_route() const {
		std::vector<plan::Step> route;
		for (size_t k = 0; k < plan_reactions.size(); ++k) {
			char *end = nullptr;
			const double pct = std::strtod(plan_yields[k].c_str(), &end);
			route.push_back({ plan_reactions[k], end != nullptr && *end == '\0' && !plan_yields[k].empty() ? pct / 100.0 : -1.0 });
		}
		return route;
	}
	plan::Plan plan_now() const {
		const chem::SpeciesId target = plan_species();
		if (target.index < 0) {
			plan::Plan p;
			p.why = plan_target.empty() ? "type a target's formula, such as CuSO4.5H2O" : "the tables hold no " + plan_target;
			return p;
		}
		char *end = nullptr;
		const double g = std::strtod(plan_grams.c_str(), &end);
		if (plan_grams.empty() || end == nullptr || *end != '\0') {
			plan::Plan p;
			p.why = "type how many grams of it";
			return p;
		}
		return plan::work_back(chem::builtin_species(), target, g, plan_route());
	}
	void plan_press(float x, float y) {
		plan_focus.clear();
		for (const char *f : { "plan:target", "plan:grams", "plan:purity" }) {
			if (notebook_rect(f).has(x, y)) {
				plan_focus = f;
				plan_fresh = true;
				return;
			}
		}
		for (size_t k = 0; k < plan_reactions.size(); ++k) {
			const std::string n = std::to_string(k);
			if (notebook_rect("plan:yield:" + n).has(x, y)) {
				plan_focus = "plan:yield:" + n;
				plan_fresh = true;
				return;
			}
			const bool prev = notebook_rect("plan:prev:" + n).has(x, y), next = notebook_rect("plan:next:" + n).has(x, y);
			if (prev || next) {
				// another reaction making what this step is asked for; the steps after it follow from it, so they go
				const std::vector<int> choices = plan::makers(plan_asked(k));
				if (!choices.empty()) {
					const auto at = std::find(choices.begin(), choices.end(), plan_reactions[k]);
					const int i = at == choices.end() ? 0 : static_cast<int>(at - choices.begin());
					const int count = static_cast<int>(choices.size());
					plan_reactions[k] = choices[static_cast<size_t>((i + (next ? 1 : count - 1)) % count)];
					plan_reactions.resize(k + 1);
					plan_yields.resize(k + 1);
				}
				return;
			}
		}
		if (notebook_rect("plan:add").has(x, y) && plan_reactions.size() < 4) {
			const std::vector<int> choices = plan::makers(plan_asked(plan_reactions.size()));
			if (!choices.empty()) {
				plan_reactions.push_back(choices.front());
				plan_yields.emplace_back("100");
			}
			return;
		}
		if (notebook_rect("plan:remove").has(x, y) && !plan_reactions.empty()) {
			plan_reactions.pop_back();
			plan_yields.pop_back();
			return;
		}
		if (notebook_rect("plan:goal").has(x, y)) {
			const plan::Plan p = plan_now();
			char *end = nullptr;
			const double purity = std::strtod(plan_purity.c_str(), &end);
			if (!p.ok) {
				message = p.why;
				return;
			}
			int slot = 0;
			for (int k = 1; k <= 6 && slot == 0; ++k) {
				slot = std::none_of(view.goals.begin(), view.goals.end(), [k](const Goal &g) { return g.slot == k; }) ? k : 0;
			}
			if (slot == 0) {
				message = "every slot of the outbox has a goal: take one off first";
				return;
			}
			const std::string id(chem::builtin_species().species(plan_species()).id);
			json terms = json::array({ { { "quantity", { { "what", id }, { "measure", "g" } } }, { "compare", "at_least" }, { "value", std::strtod(plan_grams.c_str(), nullptr) } } });
			if (end != nullptr && *end == '\0' && purity > 0.0) {
				terms.push_back({ { "quantity", { { "what", id }, { "measure", "purity" } } }, { "compare", "at_least" }, { "value", purity } });
			}
			act({ { "do", "goal" }, { "slot", slot }, { "name", plan_grams + " g of " + plan_target }, { "terms", terms } });
		}
	}
	bool type_plan(Ui::Key key, uint32_t codepoint) {
		if (!notebook_open || notebook_tab != 3 || plan_focus.empty()) {
			return false;
		}
		std::string *f = plan_focus == "plan:target" ? &plan_target : plan_focus == "plan:grams" ? &plan_grams : plan_focus == "plan:purity" ? &plan_purity : nullptr;
		if (f == nullptr) {
			const size_t k = static_cast<size_t>(std::atoi(plan_focus.c_str() + 11));
			if (k >= plan_yields.size()) {
				return false;
			}
			f = &plan_yields[k];
		}
		const bool formula = plan_focus == "plan:target";
		const bool ok = formula ? (std::isalnum(static_cast<int>(codepoint)) != 0 || codepoint == '(' || codepoint == ')' || codepoint == '.') : ((codepoint >= '0' && codepoint <= '9') || codepoint == '.');
		const bool replacing = plan_fresh;
		plan_fresh = false;
		if (key == Ui::KeyText && ok && codepoint < 128) {
			if (replacing) {
				f->clear();
			}
			if (f->size() < 24) {
				*f += static_cast<char>(codepoint);
			}
			if (formula && plan_reactions.empty()) {
				const std::vector<int> choices = plan::makers(plan_species());
				if (!choices.empty()) { // its first step offered at once
					plan_reactions.push_back(choices.front());
					plan_yields.emplace_back("100");
				}
			} else if (formula) {
				plan_reactions.clear(); // another target: its route anew
				plan_yields.clear();
				const std::vector<int> choices = plan::makers(plan_species());
				if (!choices.empty()) {
					plan_reactions.push_back(choices.front());
					plan_yields.emplace_back("100");
				}
			}
		} else if (key == Ui::KeyBackspace && !f->empty()) {
			f->pop_back();
		} else if (key == Ui::KeyEnter || key == Ui::KeyEscape) {
			plan_focus.clear();
		}
		return true;
	}
	void paint_planner() {
		const chem::SpeciesTable &table = chem::builtin_species();
		const auto field = [&](const std::string &name, const std::string &value) {
			const Rect f = notebook_rect(name);
			rect_fill(f, rgba(24, 24, 24));
			outline(f, plan_focus == name ? GUIDE : GRID, plan_focus == name ? 2.0f : 1.0f);
			text(f.x + 6, f.y + 21, value + (plan_focus == name ? "_" : ""), INK, 15);
		};
		text(NOTEBOOK.x + 20, NOTEBOOK.y + 80, "a target, and the route to it, from the target back", DIM, 13);
		text(NOTEBOOK.x + 20, NOTEBOOK.y + 121, "target", INK, 15);
		field("plan:target", plan_target);
		field("plan:grams", plan_grams);
		text(NOTEBOOK.x + 384, NOTEBOOK.y + 121, "g", DIM, 14);
		field("plan:purity", plan_purity);
		text(NOTEBOOK.x + 494, NOTEBOOK.y + 121, "%", DIM, 14);
		const auto all = chem::builtin_reactions();
		for (size_t k = 0; k < plan_reactions.size(); ++k) {
			const std::string n = std::to_string(k);
			const float row = NOTEBOOK.y + 150 + static_cast<float>(k) * 66;
			const chem::SpeciesId asked = plan_asked(k);
			text(NOTEBOOK.x + 20, row + 14, text_of("step %.0f makes ", static_cast<double>(k + 1)) + (asked.index >= 0 ? std::string(table.species(asked).formula) : std::string("?")), INK, 14);
			button(notebook_rect("plan:prev:" + n), "<", false, 14);
			button(notebook_rect("plan:next:" + n), ">", false, 14);
			text(NOTEBOOK.x + 96, row + 42, calc::equation(table, all[static_cast<size_t>(plan_reactions[k])]), INK, 12);
			text(NOTEBOOK.x + 380, row + 14, "yield %", DIM, 13);
			field("plan:yield:" + n, plan_yields[k]);
		}
		button(notebook_rect("plan:add"), "+ step", false, 14);
		button(notebook_rect("plan:remove"), "- step", false, 14);
		const plan::Plan p = plan_now();
		float y = NOTEBOOK.y + 480;
		if (!p.ok) {
			text(NOTEBOOK.x + 20, y, p.why, DIM, 15);
		} else {
			for (size_t k = 0; k < p.extents.size(); ++k) {
				text(NOTEBOOK.x + 20, y, text_of("step %.0f: ", static_cast<double>(k + 1)) + text_of("%.5g mol of its reaction, making ", p.extents[k]) + std::string(table.species(p.made[k]).formula), INK, 14);
				y += 21;
				for (const plan::Need &need : p.needs) {
					if (need.step == static_cast<int>(k)) {
						text(NOTEBOOK.x + 40, y, text_of("%.4g g", need.g) + text_of(" (%.5g mol) of ", need.mol) + need.formula + (need.carried ? "  <- made by the next step" : ""), need.carried ? GOOD : DIM, 14);
						y += 20;
					}
				}
				y += 6;
			}
		}
		button(notebook_rect("plan:goal"), p.ok ? "set it as a goal in the outbox" : "nothing to set yet", false, 13);
	}
	void paint_notebook() {
		button(notebook_button(), text_of("notebook %.0f", static_cast<double>(view.notebook.size())), notebook_open, 15);
		if (!notebook_open) {
			return;
		}
		rect_fill(NOTEBOOK, PANEL_FILL);
		outline(NOTEBOOK, GLASS, 1);
		static const char *const TAB_NAMES[] = { "notebook", "calculator", "reference", "planner" };
		for (int k = 0; k < 4; ++k) {
			button(notebook_rect(TABS[k]), TAB_NAMES[k], notebook_tab == k, 14); // (Stage 58)
		}
		button(notebook_rect("close"), "close");
		if (notebook_tab == 1) {
			paint_calculator();
			return;
		}
		if (notebook_tab == 2) {
			paint_reference();
			return;
		}
		if (notebook_tab == 3) {
			paint_planner();
			return;
		}
		button(notebook_rect("erase"), "erase");
		if (view.instruments) {
			text(NOTEBOOK.x + 20, NOTEBOOK.y + 64, "by instruments: what they read", DIM, 13);
		}
		// the table: the newest at the foot, as written
		float y = NOTEBOOK.y + 80;
		const size_t shown_rows = 10, first = view.notebook.size() > shown_rows ? view.notebook.size() - shown_rows : 0;
		for (size_t i = first; i < view.notebook.size(); ++i) {
			const NoteEntry &e = view.notebook[i];
			text(NOTEBOOK.x + 20, y, when(e.step), DIM, 14);
			text(NOTEBOOK.x + 110, y, e.label, INK, 14);
			text(NOTEBOOK.x + 360, y, e.quantity.item.empty() ? text_of("%.6g", e.value) + (e.unit.empty() ? "" : " " + e.unit) + " (written)" : shown(e.quantity, e.value), INK, 14);
			y += 21;
		}
		if (view.notebook.empty()) {
			text(NOTEBOOK.x + 20, y, "nothing yet: choose an item, then a \"+\" under its readings", DIM, 15);
		}
		// the plot
		const std::string xs = plot_x.empty() ? "time (s)" : plot_x, ys = plot_y.empty() ? "(choose y)" : plot_y;
		button(notebook_rect("x"), "x: " + xs, false, 13);
		button(notebook_rect("tx"), std::string(transform_name(plot_tx, note_celsius(plot_x))).empty() ? "as is" : transform_name(plot_tx, note_celsius(plot_x)), plot_tx != AxisTransform::Plain, 13);
		button(notebook_rect("y"), "y: " + ys, false, 13);
		button(notebook_rect("ty"), std::string(transform_name(plot_ty, note_celsius(plot_y))).empty() ? "as is" : transform_name(plot_ty, note_celsius(plot_y)), plot_ty != AxisTransform::Plain, 13);
		button(notebook_rect("fit"), "fit", plot_fit, 13);
		// the write row (Stage 55): a number the student writes, under a label
		{
			text(NOTEBOOK.x + 20, NOTEBOOK.y + 811, "write", DIM, 15);
			const Rect fl = notebook_rect("write:label"), fv = notebook_rect("write:value");
			for (int k = 1; k <= 2; ++k) {
				const Rect &f = k == 1 ? fl : fv;
				rect_fill(f, rgba(24, 24, 24));
				outline(f, note_focus == k ? GUIDE : GRID, note_focus == k ? 2.0f : 1.0f);
				const std::string &t = k == 1 ? note_label : note_value;
				text(f.x + 8, f.y + 21, t.empty() && note_focus != k ? (k == 1 ? "label" : "number") : t + (note_focus == k ? "_" : ""), t.empty() ? DIM : INK, 15);
			}
			text(fl.x + fl.w + 10, fl.y + 21, "=", DIM, 16);
			button(notebook_rect("write"), "write", false, 14);
		}
		const Rect area = plot_area();
		rect_fill(area, rgba(24, 24, 24));
		outline(area, GRID, 1);
		const std::vector<std::pair<double, double>> pts = plot_points();
		if (!pts.empty()) {
			double x0 = pts[0].first, x1 = x0, y0 = pts[0].second, y1 = y0;
			for (const auto &[qx, qy] : pts) {
				x0 = std::min(x0, qx);
				x1 = std::max(x1, qx);
				y0 = std::min(y0, qy);
				y1 = std::max(y1, qy);
			}
			if (x1 - x0 < 1e-12) {
				x1 = x0 + 1.0;
			}
			if (y1 - y0 < 1e-12) {
				y1 = y0 + 1.0;
			}
			const auto sx = [&](double v) { return area.x + 10 + static_cast<float>((v - x0) / (x1 - x0)) * (area.w - 20); };
			const auto sy = [&](double v) { return area.y + area.h - 10 - static_cast<float>((v - y0) / (y1 - y0)) * (area.h - 20); };
			for (const auto &[qx, qy] : pts) {
				rect_fill(Rect{ sx(qx) - 3, sy(qy) - 3, 6, 6 }, GUIDE);
			}
			text(area.x, area.y + area.h + 22, text_of("%.4g", x0), DIM, 13);
			text(area.x + area.w - 80, area.y + area.h + 22, text_of("%.4g", x1), DIM, 13);
			text(area.x - 56, area.y + area.h - 6, text_of("%.4g", y0), DIM, 13);
			text(area.x - 56, area.y + 14, text_of("%.4g", y1), DIM, 13);
			if (plot_fit) {
				std::vector<double> xv, yv;
				for (const auto &[qx, qy] : pts) {
					xv.push_back(qx);
					yv.push_back(qy);
				}
				const LineFit f = fit_line(xv, yv);
				if (f.ok) {
					line(sx(x0), sy(f.slope * x0 + f.intercept), sx(x1), sy(f.slope * x1 + f.intercept), WARN, 2);
					text(NOTEBOOK.x + 20, area.y + area.h + 54, text_of("slope %.6g", f.slope) + text_of(", intercept %.6g", f.intercept) + text_of(", R2 %.5f", f.r2) + text_of(" (%.0f points)", f.n), INK, 15);
				}
			}
		}
		if (const auto read = std::find_if(view.notebook.rbegin(), view.notebook.rend(), [](const NoteEntry &e) { return !e.quantity.item.empty(); });
				read != view.notebook.rend()) {
			// the last quantity recorded (Stage 55: not a number written), short of the buttons
			const std::string named = read->label.size() > 14 ? read->label.substr(0, 13) + "." : read->label;
			text(NOTEBOOK.x + 20, NOTEBOOK.y + 1000, "log " + named + ":", DIM, 14);
			button(notebook_rect("log:1"), "1 s", false, 13);
			button(notebook_rect("log:5"), "5 s", false, 13);
			button(notebook_rect("log:30"), "30 s", false, 13);
			button(notebook_rect("unlog"), "stop", false, 13);
		}
		float ly = NOTEBOOK.y + 940;
		for (const NoteLogger &l : view.loggers) {
			text(NOTEBOOK.x + 20, ly, "logging " + l.label + text_of(" every %.0f s", l.every_s), WARN, 14);
			ly -= 20;
		}
	}
	// ---- the report (GAME_SPEC §13.2, Stage 54): once a tutorial is complete, under its card in place of the readout
	bool report_active() const { return tutorial && tutorial->view().state == TutorialView::Complete && !tutorial->report().empty(); }
	void clear_report() {
		answers.clear();
		report_checks.clear();
		report_focus = -1;
		guesses.clear();
		predictions.clear();
		guess_focus = -1;
	}
	// ---- predictions (GAME_SPEC §13.2, Stage 55): the report's items marked `predict`, typed and made before the run's
	// end, in place of the readout while any is still to make; kept as made, shown beside the result
	std::vector<int> predicted_items() const {
		std::vector<int> out;
		if (tutorial) {
			for (size_t k = 0; k < tutorial->report().size(); ++k) {
				if (tutorial->report()[k].predict) {
					out.push_back(static_cast<int>(k));
				}
			}
		}
		return out;
	}
	int predictions_made() const {
		int n = 0;
		for (const std::optional<double> &p : predictions) {
			n += p ? 1 : 0;
		}
		return n;
	}
	bool predicting() const { return running() && predictions_made() < static_cast<int>(predicted_items().size()); }
	// shown in place of the readout while nothing is selected: an item chosen shows its own readout and controls
	bool predict_shown() const { return predicting() && selected < 0; }
	static Rect guess_field(int n) { return Rect{ READOUT.x + 300, TUTOR.y + TUTOR.h + 100 + static_cast<float>(n) * 70, 150, 30 }; }
	Rect guess_make() const { return Rect{ READOUT.x + 16, TUTOR.y + TUTOR.h + 100 + static_cast<float>(predicted_items().size()) * 70, 120, 34 }; }
	void make_predictions() {
		const size_t n = tutorial->report().size();
		guesses.resize(n);
		predictions.resize(n);
		for (const int k : predicted_items()) {
			char *end = nullptr;
			const std::string &g = guesses[static_cast<size_t>(k)];
			const double v = std::strtod(g.c_str(), &end);
			if (!predictions[static_cast<size_t>(k)] && !g.empty() && end != nullptr && *end == '\0') {
				predictions[static_cast<size_t>(k)] = v;
			}
		}
		message = predicting() ? "a prediction is a number: type one in each field" : std::string();
	}
	bool predict_press(float x, float y) {
		if (!predict_shown()) {
			return false;
		}
		const std::vector<int> items = predicted_items();
		for (size_t n = 0; n < items.size(); ++n) {
			if (guess_field(static_cast<int>(n)).has(x, y)) {
				guess_focus = items[n];
				return true;
			}
		}
		if (guess_make().has(x, y)) {
			make_predictions();
			return true;
		}
		return false;
	}
	bool type_predict(Ui::Key key, uint32_t codepoint) {
		if (!predict_shown() || guess_focus < 0) {
			return false;
		}
		guesses.resize(tutorial->report().size());
		predictions.resize(tutorial->report().size());
		if (predictions[static_cast<size_t>(guess_focus)]) {
			return true; // made: kept as it was
		}
		std::string &g = guesses[static_cast<size_t>(guess_focus)];
		if (key == Ui::KeyText && ((codepoint >= '0' && codepoint <= '9') || codepoint == '.' || codepoint == '-' || codepoint == 'e') && g.size() < 16) {
			g += static_cast<char>(codepoint);
		} else if (key == Ui::KeyBackspace && !g.empty()) {
			g.pop_back();
		} else if (key == Ui::KeyEnter) {
			make_predictions();
		} else if (key == Ui::KeyEscape) {
			guess_focus = -1;
		}
		return true;
	}
	void paint_predict() {
		const std::vector<TutorialReport> &items = tutorial->report();
		float y = TUTOR.y + TUTOR.h + 46;
		text(READOUT.x + 16, y, "Before you start: predict", INK, 18);
		text(READOUT.x + 16, y + 26, "what you expect, kept as you make it, beside what you find", DIM, 14);
		const std::vector<int> ks = predicted_items();
		for (size_t n = 0; n < ks.size(); ++n) {
			const size_t k = static_cast<size_t>(ks[n]);
			const Rect f = guess_field(static_cast<int>(n));
			text(READOUT.x + 16, f.y + 21, items[k].label, INK, 15);
			rect_fill(f, rgba(24, 24, 24));
			const bool made = k < predictions.size() && predictions[k];
			outline(f, guess_focus == static_cast<int>(k) && !made ? GUIDE : GRID, guess_focus == static_cast<int>(k) ? 2.0f : 1.0f);
			const std::string typed = made ? text_of("%.6g", *predictions[k]) : k < guesses.size() ? guesses[k] : std::string();
			text(f.x + 8, f.y + 21, typed + (guess_focus == static_cast<int>(k) && !made ? "_" : ""), made ? GOOD : INK, 16);
			text(f.x + f.w + 10, f.y + 21, items[k].unit, DIM, 15);
		}
		button(guess_make(), "predict", false, 15);
	}
	static Rect report_field(int k) { return Rect{ READOUT.x + 300, TUTOR.y + TUTOR.h + 70 + static_cast<float>(k) * 96, 150, 30 }; }
	Rect report_check() const { return Rect{ READOUT.x + 16, TUTOR.y + TUTOR.h + 70 + static_cast<float>(tutorial->report().size()) * 96, 120, 34 }; }
	void check_report() {
		const std::vector<TutorialReport> &items = tutorial->report();
		answers.resize(items.size());
		report_checks.assign(items.size(), ReportCheck{});
		for (size_t k = 0; k < items.size(); ++k) {
			ReportCheck &c = report_checks[k];
			c.done = true;
			if (!evaluate(items[k].truth, view, c.truth, c.why)) {
				c.why = "its truth cannot be worked out: " + c.why;
				continue;
			}
			char *end = nullptr;
			c.yours = std::strtod(answers[k].c_str(), &end);
			const bool number = !answers[k].empty() && end != nullptr && *end == '\0';
			const double allowed = items[k].relative ? items[k].tolerance * std::fabs(c.truth) : items[k].tolerance;
			c.within = number && std::fabs(c.yours - c.truth) <= allowed;
			if (!number) {
				c.why = "nothing to check: type a number";
			}
		}
	}
	bool report_press(float x, float y) {
		if (!report_active()) {
			return false;
		}
		answers.resize(tutorial->report().size());
		for (int k = 0; k < static_cast<int>(answers.size()); ++k) {
			if (report_field(k).has(x, y)) {
				report_focus = k;
				return true;
			}
		}
		if (report_check().has(x, y)) {
			check_report();
			return true;
		}
		return false;
	}
	bool type_report(Ui::Key key, uint32_t codepoint) {
		if (!report_active() || report_focus < 0) {
			return false;
		}
		answers.resize(tutorial->report().size());
		std::string &a = answers[static_cast<size_t>(report_focus)];
		if (key == Ui::KeyText && ((codepoint >= '0' && codepoint <= '9') || codepoint == '.' || codepoint == '-' || codepoint == 'e') && a.size() < 16) {
			a += static_cast<char>(codepoint);
		} else if (key == Ui::KeyBackspace && !a.empty()) {
			a.pop_back();
		} else if (key == Ui::KeyEnter) {
			check_report();
		} else if (key == Ui::KeyEscape) {
			report_focus = -1;
		}
		return true;
	}
	void paint_report() {
		const std::vector<TutorialReport> &items = tutorial->report();
		float y = TUTOR.y + TUTOR.h + 46;
		text(READOUT.x + 16, y, "Your report: type each result, then check it", INK, 18);
		for (size_t k = 0; k < items.size(); ++k) {
			const Rect f = report_field(static_cast<int>(k));
			text(READOUT.x + 16, f.y + 21, items[k].label, INK, 15);
			rect_fill(f, rgba(24, 24, 24));
			outline(f, report_focus == static_cast<int>(k) ? GUIDE : GRID, report_focus == static_cast<int>(k) ? 2.0f : 1.0f);
			const std::string typed = k < answers.size() ? answers[k] : std::string();
			text(f.x + 8, f.y + 21, typed + (report_focus == static_cast<int>(k) ? "_" : ""), INK, 16);
			text(f.x + f.w + 10, f.y + 21, items[k].unit, DIM, 15);
			if (k < report_checks.size() && report_checks[k].done) {
				const ReportCheck &c = report_checks[k];
				std::string says = c.why.empty() ? text_of("the truth %.4g", c.truth) + (c.within ? ": yours is within it" : text_of(": yours is off by %.3g", c.yours - c.truth)) : c.why;
				if (items[k].ideal) {
					says += text_of("; the textbook's %.4g", *items[k].ideal);
				}
				if (k < predictions.size() && predictions[k]) {
					says += text_of("; you predicted %.4g", *predictions[k]); // (Stage 55)
				}
				text(READOUT.x + 16, f.y + 50, says, c.within ? GOOD : WARN, 14);
				if (!items[k].why.empty()) {
					const std::vector<std::string> w = wrapped(items[k].why, 64);
					text(READOUT.x + 16, f.y + 70, w.empty() ? std::string() : w.front() + (w.size() > 1 ? " .." : ""), DIM, 13);
				}
			}
		}
		button(report_check(), "check", false, 15);
		y = report_check().y + 60;
		for (size_t k = 0; k < items.size() && k < report_checks.size() && report_checks[k].done; ++k) {
			for (const std::string &l : wrapped(items[k].why, 60)) {
				if (y > READOUT.y + READOUT.h - 30) {
					break;
				}
				text(READOUT.x + 16, y, l, DIM, 13);
				y += 18;
			}
		}
	}

	// the readout's "+": a reading of the chosen item into the notebook (Stage 54)
	std::vector<std::pair<std::string, Rect>> record_buttons() const {
		std::vector<std::pair<std::string, Rect>> out;
		if (selected < 0 || selected >= static_cast<int>(view.items.size()) || draft.edit != Draft::None) {
			return out;
		}
		const ItemView &v = view.items[static_cast<size_t>(selected)];
		std::vector<std::string> whats;
		if (v.kind == "balance") {
			whats = { "reads_g" };
		} else if (v.kind == "spectrophotometer") {
			whats = { "absorbance" }; // (Stage 55)
		} else if (v.kind == "vessel") {
			whats.emplace_back("celsius");
			if (v.ph) {
				whats.emplace_back("ph");
			}
			if (v.us_cm) {
				whats.emplace_back("us_cm"); // (Stage 65)
			}
			if (v.syringe) {
				whats.emplace_back("plunger_ml"); // (Stage 79)
			} else if (v.scale_ml) {
				whats.emplace_back("scale_ml");
			} else if (!view.instruments || v.read_ml) {
				whats.emplace_back("liquid_ml");
			}
			whats.emplace_back("kpa");
		}
		for (size_t k = 0; k < whats.size(); ++k) {
			out.emplace_back(whats[k], Rect{ READOUT.x + 16 + static_cast<float>(k) * 96, READOUT.y + READOUT.h - 168, 90, 28 });
		}
		return out;
	}
	static std::string record_name(const std::string &what) {
		return what == "celsius" ? "+ T" : what == "ph" ? "+ pH" : what == "us_cm" ? "+ uS/cm" : what == "reads_g" ? "+ mass" : what == "kpa" ? "+ kPa" : what == "absorbance" ? "+ A" : "+ mL";
	}

	// ---- the debug panel (§13.6): the selected item from the inside
	static Rect debug_button() { return Rect{ 1670, 14, 110, 36 }; }
	// step, export, up, down, close; and (Stage 46) the infrared view's switch, beside the panel's title
	static Rect debug_rect(int k) { return k == 5 ? Rect{ DEBUG.x + 390, DEBUG.y + 12, 110, 30 } : Rect{ DEBUG.x + 20 + static_cast<float>(k) * 98, DEBUG.y + 56, 90, 30 }; }
	static constexpr const char *DEBUG_BUTTONS[] = { "step", "export", "up", "down", "close", "infrared" };
	static constexpr int DEBUG_BUTTON_COUNT = 6;
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
		for (int k = 0; k < DEBUG_BUTTON_COUNT; ++k) {
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
			} else if (k == 5) {
				infrared = !infrared;
				message = infrared ? "infrared: the room as an 8-14 um camera sees it (5 K under the room to 95 K over it)" : "infrared: off";
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
		for (int k = 0; k < DEBUG_BUTTON_COUNT; ++k) {
			button(debug_rect(k), DEBUG_BUTTONS[k], k == 5 && infrared, 15);
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
		screen.predicted = predictions_made(); // (Stage 55)
		if (notebook_open && notebook_tab == 1 && calc_result().ok) {
			screen.calculated = KINDS[calc_kind]; // (Stage 58)
		}
		if (notebook_open && notebook_tab == 3 && plan_now().ok) {
			screen.calculated = "plan"; // (Stage 60)
		}
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
		settle_fresh();
		if (tutorial) {
			tutorial->follow(view, screen_now());
			if (tutorial->view().state == TutorialView::Complete && shown_k >= 0) {
				done.insert(catalogue[static_cast<size_t>(shown_k)].id); // (Stage 55) progress
			}
		}
	}
	// (Stage 55) the next lesson: the first in the catalogue's order with a tutorial, not yet done
	int next_lesson() const {
		for (size_t k = 0; k < catalogue.size(); ++k) {
			if (!catalogue[k].tutorial.empty() && done.count(catalogue[k].id) == 0) {
				return static_cast<int>(k);
			}
		}
		return -1;
	}
	void play() {
		paused = false;
		if (!rung.empty()) {
			act({ { "do", "ack" } }); // the alarms are answered
			rung.clear();
			chain.clear();
		}
	}

	// a lab opened in place of the one open (reset, another experiment): the screen as it opens
	void begin(std::unique_ptr<Lab> again) {
		lab = std::move(again);
		owed = 0.0;
		message.clear();
		refresh();
		choose_only(-1);
		draft = Draft{};
		rung.clear();
		chain.clear();
		opened.clear();
	}
	// ---- the experiments (§13.7, Stage 41): a button over the cabinet, the list over the bench
	static Rect menu_button() { return Rect{ 10, 14, 240, 36 }; }
	// (Stage 78) one column of rows under a search field, as many on the screen as fit, scrolled a whole row at a
	// time (the wheel or a trackpad's two fingers anywhere while it is open, or its bar held), as Supplies' cabinet is;
	// a row the search leaves, or out of sight, is not drawn and takes no press
	static constexpr float MENU_ROW = 84, MENU_TOP = 120; // (the rows' first, under the panel's top)
	static constexpr int MENU_ROWS = 10; // on the screen at once
	static Rect menu_rect() { return Rect{ 460, 64, 1000, MENU_TOP + MENU_ROW * MENU_ROWS + 8 }; }
	static Rect menu_field() { return Rect{ menu_rect().x + 16, menu_rect().y + 72, menu_rect().w - 32, 36 }; }
	// the entries the query leaves, in the catalogue's order; the first shown again
	void menu_filter() {
		menu_shown.clear();
		for (int k = 0; k < static_cast<int>(catalogue.size()); ++k) {
			if (lab::search::matches(menu_query, catalogue[static_cast<size_t>(k)].words)) {
				menu_shown.push_back(k);
			}
		}
		menu_from = 0;
	}
	int menu_most_from() const { return std::max(static_cast<int>(menu_shown.size()) - MENU_ROWS, 0); }
	void menu_scroll(int rows) { menu_from = std::clamp(menu_from + rows, 0, menu_most_from()); }
	// opened, the list is the whole catalogue with what is shown in sight; closed, its query is let go
	void set_menu(bool open) {
		menu = open;
		menu_query.clear();
		menu_scrolling = false;
		menu_pan = 0.0f;
		menu_filter();
		if (open) {
			search_focus = 0; // (the keys are the list's field's while it is open)
			menu_from = std::clamp(shown_k - MENU_ROWS / 2, 0, menu_most_from());
		}
	}
	// the j-th row the search leaves (none where it is scrolled out of sight)
	std::optional<Rect> menu_row(int j) const {
		const int row = j - std::clamp(menu_from, 0, menu_most_from());
		if (j < 0 || j >= static_cast<int>(menu_shown.size()) || row < 0 || row >= MENU_ROWS) {
			return std::nullopt;
		}
		const Rect r = menu_rect();
		return Rect{ r.x + 16, r.y + MENU_TOP + MENU_ROW * static_cast<float>(row), r.w - 16 - 28, MENU_ROW - 8 };
	}
	// its scroll bar (none while every row left is on the screen): the track, and the thumb on it
	static Rect menu_track() { return Rect{ menu_rect().x + menu_rect().w - 20, menu_rect().y + MENU_TOP, 8, MENU_ROW * MENU_ROWS - 8 }; }
	Rect menu_thumb() const {
		const Rect t = menu_track();
		const float share = static_cast<float>(MENU_ROWS) / static_cast<float>(std::max(static_cast<int>(menu_shown.size()), 1));
		const float h = std::fmax(t.h * std::fmin(share, 1.0f), 24.0f);
		const float at = menu_most_from() > 0 ? static_cast<float>(std::clamp(menu_from, 0, menu_most_from())) / static_cast<float>(menu_most_from()) : 0.0f;
		return Rect{ t.x, t.y + (t.h - h) * at, t.w, h };
	}
	// the bar held at y: the row whose share of the track y is at
	void menu_scroll_to(float y) {
		const Rect t = menu_track();
		menu_from = static_cast<int>(std::lround(std::clamp((y - t.y) / std::fmax(t.h, 1.0f), 0.0f, 1.0f) * static_cast<float>(menu_most_from())));
	}
	// a trackpad's two fingers (a pan of dy pixels): the rows move with them, a whole row at a time
	void menu_panned(float dy) {
		menu_pan += dy;
		const int rows = static_cast<int>(menu_pan / MENU_ROW);
		menu_pan -= static_cast<float>(rows) * MENU_ROW;
		menu_scroll(-rows);
	}
	// a key while the list is open: its field's (text, backspace), enter opens the first row left, escape clears the
	// query or, with none, closes the list
	bool type_menu(Ui::Key key, uint32_t codepoint) {
		if (!menu) {
			return false;
		}
		if (key == Ui::KeyText && codepoint >= 32 && codepoint < 127) {
			if (menu_query.size() < 40) {
				menu_query += static_cast<char>(codepoint);
				menu_filter();
			}
		} else if (key == Ui::KeyBackspace) {
			if (!menu_query.empty()) {
				menu_query.pop_back();
				menu_filter();
			}
		} else if (key == Ui::KeyEscape) {
			if (menu_query.empty()) {
				set_menu(false);
			} else {
				menu_query.clear();
				menu_filter();
			}
		} else if (key == Ui::KeyEnter) {
			if (!menu_shown.empty()) {
				std::string why;
				if (!open_experiment(menu_shown.front(), why)) {
					message = why;
				}
			}
		} else {
			return false;
		}
		return true;
	}
	// opens the k-th experiment: its bench, its tutorial (or none); the screen's mode stays
	bool open_experiment(int k, std::string &why) {
		const Experiment &e = catalogue[static_cast<size_t>(k)];
		if (!e.scene.empty()) {
			// a scene of the optics alone (Stage 47): shown in place of the lab, which waits where it was
			shown_scene = std::make_unique<PrismDemo>();
			shown_k = k;
			set_menu(false);
			return true;
		}
		std::unique_ptr<Lab> again = Lab::open(e.bench, why);
		if (!again) {
			return false;
		}
		std::unique_ptr<Tutorial> t;
		if (!e.tutorial.empty()) {
			t = Tutorial::open(e.tutorial, again->view(), why);
			if (!t) {
				return false;
			}
		}
		bench = e.bench;
		begin(std::move(again));
		panel = false;
		pouring = false;
		valve_drag = -1;
		plunging = -1;
		slider_drag = false;
		hover = -1;
		tutorial = std::move(t);
		clear_report(); // (Stage 54: a new tutorial's report)
		tutorial_json = e.tutorial;
		shown_k = k;
		set_menu(false);
		shown_scene.reset();
		follow();
		return true;
	}
	// the entry that is the bench and the tutorial open now, -1 for none
	int shown_now() const {
		if (shown_scene) {
			return shown_k;
		}
		const auto same = [](const std::string &a, const std::string &b) {
			if (a.empty() || b.empty()) {
				return a.empty() && b.empty();
			}
			const json x = json::parse(a, nullptr, false), y = json::parse(b, nullptr, false);
			return !x.is_discarded() && x == y;
		};
		for (int k = 0; k < static_cast<int>(catalogue.size()); ++k) {
			if (same(catalogue[static_cast<size_t>(k)].bench, bench) && same(catalogue[static_cast<size_t>(k)].tutorial, tutorial ? tutorial_json : std::string())) {
				return k;
			}
		}
		return -1;
	}
	// a press on the experiments: the button, the list (which takes every press while it is open)
	bool menu_press(float x, float y) {
		if (catalogue.empty() || carrying || drag >= 0 || making >= 0) {
			return false; // with something in hand, a press is where it goes
		}
		if (menu_button().has(x, y)) {
			set_menu(!menu);
			return true;
		}
		if (!menu) {
			return false;
		}
		const Rect t = menu_track();
		if (menu_most_from() > 0 && Rect{ t.x - 6, t.y, t.w + 12, t.h }.has(x, y)) {
			menu_scrolling = true; // its bar, held (Stage 78)
			menu_scroll_to(y);
			return true;
		}
		for (int j = 0; j < static_cast<int>(menu_shown.size()); ++j) {
			const std::optional<Rect> row = menu_row(j);
			if (row && row->has(x, y)) {
				std::string why;
				if (!open_experiment(menu_shown[static_cast<size_t>(j)], why)) {
					message = why;
				}
				return true;
			}
		}
		if (!menu_rect().has(x, y)) {
			set_menu(false); // a press elsewhere closes it, and does nothing else
		}
		return true; // (on the panel, its field or between its rows: nothing)
	}
	void paint_menu() {
		if (catalogue.empty()) {
			return;
		}
		button(menu_button(), shown_k >= 0 ? "experiments: " + catalogue[static_cast<size_t>(shown_k)].id : std::string("experiments"), menu, 15);
		if (!menu) {
			return;
		}
		const Rect r = menu_rect();
		rect_fill(r, PANEL_FILL);
		outline(r, GRID, 1);
		text(r.x + 20, r.y + 34, "Experiments: one chosen opens its bench and its steps (or its scene)", INK, 20);
		// (Stage 55) progress: each done, and the next; (Stage 78) the next named over the list, wherever it is scrolled
		int lessons = 0, finished = 0;
		for (const Experiment &e : catalogue) {
			lessons += e.tutorial.empty() ? 0 : 1;
			finished += !e.tutorial.empty() && done.count(e.id) != 0 ? 1 : 0;
		}
		const int next = next_lesson();
		text(r.x + 20, r.y + 60,
			text_of("%.0f of %.0f with steps done", finished, lessons) + (next >= 0 ? "; next: " + catalogue[static_cast<size_t>(next)].title : std::string()), DIM, 15);
		// the search field (Stage 78), typed into while the list is open
		const Rect f = menu_field();
		rect_fill(f, BACKGROUND);
		outline(f, GUIDE, 2);
		text(f.x + 10, f.y + 24, menu_query.empty() ? "search: type a title's, a summary's or an id's words" : menu_query + "_", menu_query.empty() ? DIM : INK, 16);
		const std::string count = text_of("%.0f of %.0f", static_cast<double>(menu_shown.size()), static_cast<double>(catalogue.size()));
		text(f.x + f.w - 90, f.y + 24, count, DIM, 15);
		if (menu_shown.empty()) {
			text(r.x + 24, r.y + MENU_TOP + 34, "no experiments match \"" + menu_query + "\" (Escape clears the search)", DIM, 18);
		}
		for (int j = 0; j < static_cast<int>(menu_shown.size()); ++j) {
			const std::optional<Rect> at = menu_row(j);
			if (!at) {
				continue; // (scrolled out of sight)
			}
			const int k = menu_shown[static_cast<size_t>(j)];
			const Experiment &e = catalogue[static_cast<size_t>(k)];
			const Rect row = *at;
			rect_fill(row, k == shown_k ? BUTTON_ON : BUTTON);
			text(row.x + 12, row.y + 26, e.title, INK, 19);
			if (done.count(e.id) != 0) {
				text(row.x + row.w - 64, row.y + 26, "done", GOOD, 16);
			} else if (k == next) {
				outline(row, GUIDE, 2);
				text(row.x + row.w - 64, row.y + 26, "next", GUIDE, 16);
			}
			float y = row.y + 48;
			for (const std::string &l : wrapped(e.summary, 112)) {
				text(row.x + 12, y, l, DIM, 14);
				y += 18;
				if (y > row.y + row.h - 4) {
					break;
				}
			}
		}
		if (menu_most_from() > 0) {
			rect_fill(menu_track(), GRID);
			rect_fill(menu_thumb(), menu_scrolling ? INK : DIM);
		}
	}
	// ---- the tutorial's switch (Stage 41): on its card, while there is one
	bool strict_shown() const { return tutorial != nullptr && !notices_open && !debug_open; }
	static Rect strict_rect() { return Rect{ TUTOR.x + TUTOR.w - 168, TUTOR.y + 10, 150, 28 }; }

	// ---- a running tutorial allows only its step's gestures (§13.2), while it is strict (Stage 41)
	bool running() const { return tutorial && tutorial->view().state == TutorialView::Running; }
	bool guided() const { return running() && strict; }
	// its clock stands while the step asks for a gesture; free, the clock is the player's
	// (Stage 79: not while a hand works a physical control, a plunger's handle: what it does takes the lab's time)
	bool held() const { return guided() && !tutorial->waiting() && plunging < 0; }
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
	static constexpr float TALL_MOST = 400; // the tallest glassware is drawn (Stage 52: a burette over a flask, under the top bar)
	// (Stage 71) glassware's one scale, pixels a centimetre: the 250 mL beaker as wide as it was drawn before (90 px)
	static constexpr float GLASS_PX_PER_CM = 11.75f;
	// a balance (Stage 50): its case, the pan on it, and its display in the case's front; a vessel on it stands on the pan
	static constexpr float BALANCE_W = 150, BALANCE_H = 44, BALANCE_PAN = 8;
	static Rect balance_case(const Rect &r) { return Rect{ r.x, r.y + BALANCE_PAN, r.w, r.h - BALANCE_PAN }; }
	static Rect balance_pan(const Rect &r) { return Rect{ r.x + r.w * 0.12f, r.y, r.w * 0.76f, BALANCE_PAN }; }
	static Rect balance_display(const Rect &r) { return Rect{ r.x + r.w * 0.42f, r.y + BALANCE_PAN + 8, r.w * 0.52f, (r.h - BALANCE_PAN) * 0.5f }; }
	// a spectrophotometer (Stage 55): its case, the well its cuvette stands in, its absorbance's display and its
	// wavelength's on either side of the well
	static constexpr float SPECTRO_W = 190, SPECTRO_H = 64, SPECTRO_WELL = 10;
	static Rect spectro_case(const Rect &r) { return Rect{ r.x, r.y + SPECTRO_WELL, r.w, r.h - SPECTRO_WELL }; }
	static Rect spectro_well(const Rect &r) { return Rect{ r.cx() - 0.09f * r.w, r.y, 0.18f * r.w, SPECTRO_WELL * r.w / SPECTRO_W }; }
	static Rect spectro_display(const Rect &r) { return Rect{ r.x + 0.06f * r.w, r.y + 0.36f * r.h, 0.34f * r.w, 0.34f * r.h }; }
	static Rect spectro_nm_display(const Rect &r) { return Rect{ r.x + 0.64f * r.w, r.y + 0.36f * r.h, 0.28f * r.w, 0.34f * r.h }; }
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
		if (v.kind == "thermal") { // its case and its screen (Stage 48)
			w = CAMERA_W;
			h = CAMERA_H;
		} else if (v.kind == "balance") { // its case, its pan and its display (Stage 50)
			w = BALANCE_W;
			h = BALANCE_H;
		} else if (v.kind == "spectrophotometer") { // its case, its well and its displays (Stage 55)
			w = SPECTRO_W;
			h = SPECTRO_H;
		} else if (v.kind == "viewer") { // its case and screen, and its probe's holder beside them (Stage 42)
			w = VIEWER_W;
			h = VIEWER_H;
		} else if (v.kind == "burner") {
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
			// glassware (GAME_SPEC §12.3, Stage 24): its standard's shape; (Stage 71, asked for by the user: "vessel sizes
			// should never change once rendered, they are orthogonally projected to the screen") all of it at one scale,
			// its real size: a tube stands inside a beaker as it is
			w = static_cast<float>(2.0 * made->widest()) * GLASS_PX_PER_CM;
			h = static_cast<float>(made->height) * GLASS_PX_PER_CM;
			if (h > TALL_MOST) { // tall glassware (Stage 52): no taller than fits over a flask, in its own proportions
				w *= TALL_MOST / h;
				h = TALL_MOST;
			}
		} else {
			w = static_cast<float>(40.0 + 8.0 * std::cbrt(v.volume_ml));
			h = w * 1.25f;
		}
	}

	// ---- the spectral viewer (GAME_SPEC §13.1, Stage 42)
	// its parts within its drawn rectangle (smaller in the cabinet, as all of it is): its case, the screen in it, the
	// probe's holder beside it; where the cable leaves the case, and where the probe's disc rests in the holder
	static constexpr float VIEWER_W = 272, VIEWER_H = 150;
	static Rect viewer_case(const Rect &r) {
		const float s = r.w / VIEWER_W;
		return Rect{ r.x, r.y, 232 * s, r.h };
	}
	static Rect viewer_screen(const Rect &r) {
		const float s = r.w / VIEWER_W;
		return Rect{ r.x + 12 * s, r.y + 12 * s, 208 * s, 116 * s };
	}
	static Rect viewer_holder(const Rect &r) {
		const float s = r.w / VIEWER_W;
		return Rect{ r.x + 240 * s, r.y + 26 * s, 26 * s, 96 * s };
	}
	static void viewer_port(const Rect &r, float &x, float &y) {
		const float s = r.w / VIEWER_W;
		x = r.x + 232 * s;
		y = r.y + 136 * s;
	}
	static void viewer_rest(const Rect &r, float &x, float &y) {
		const float s = r.w / VIEWER_W;
		x = r.x + 253 * s;
		y = r.y + 44 * s;
	}
	// A place of an item in its own frame (0 to 1 across and down its rectangle, upright) on the screen, as it is
	// drawn now: upside down with it, turned with it; and the way back
	void place_on(int index, double u, double v, float &x, float &y) const {
		const Rect r = drawn(index);
		const ItemView &it = view.items[static_cast<size_t>(index)];
		double ax = r.x + u * r.w, ay = r.y + v * r.h;
		if (it.inverted && it.pours_into >= 0) {
			ax = 2.0 * r.cx() - ax;
			ay = 2.0 * r.cy() - ay;
		}
		float angle = 0, pvx = 0, pvy = 0;
		if (turned(index, r, angle, pvx, pvy)) {
			const double c = std::cos(angle), sn = std::sin(angle), dx = ax - pvx, dy = ay - pvy;
			ax = pvx + dx * c - dy * sn;
			ay = pvy + dx * sn + dy * c;
		}
		x = static_cast<float>(ax);
		y = static_cast<float>(ay);
	}
	void frame_of(int index, float x, float y, double &u, double &v) const {
		const Rect r = drawn(index);
		const ItemView &it = view.items[static_cast<size_t>(index)];
		double ax = x, ay = y;
		float angle = 0, pvx = 0, pvy = 0;
		if (turned(index, r, angle, pvx, pvy)) {
			const double c = std::cos(angle), sn = std::sin(angle), dx = ax - pvx, dy = ay - pvy;
			ax = pvx + dx * c + dy * sn;
			ay = pvy - dx * sn + dy * c;
		}
		if (it.inverted && it.pours_into >= 0) {
			ax = 2.0 * r.cx() - ax;
			ay = 2.0 * r.cy() - ay;
		}
		u = r.w > 0 ? (ax - r.x) / r.w : 0.5;
		v = r.h > 0 ? (ay - r.y) / r.h : 0.5;
	}
	// Where a viewer's probe reads (the middle of its disc): in hand, where the pointer is; fixed on an item or a
	// part that is on the screen, at its place of it; on the room, there; else resting in its holder (false)
	bool probe_point(int index, float &x, float &y) const {
		const ItemView &v = view.items[static_cast<size_t>(index)];
		if (probing == index) {
			x = px;
			y = py;
			return true;
		}
		if (!v.probe_on.empty()) {
			const int t = item_index(v.probe_on);
			if (t >= 0 && visible(t) && !hidden(t)) {
				place_on(t, v.probe_u, v.probe_v, x, y);
				return true;
			}
		} else if (v.probe_room) {
			x = BENCH.x + static_cast<float>(v.probe_u) * square();
			y = BENCH.y + static_cast<float>(v.probe_v) * square();
			return true;
		}
		viewer_rest(drawn(index), x, y);
		return false;
	}
	// what takes the probe up: its disc and its handle
	bool probe_grab(int index, float x, float y) const {
		const ItemView &v = view.items[static_cast<size_t>(index)];
		if (v.kind != "viewer" || v.place != "bench" || hidden(index)) {
			return false;
		}
		float hx = 0, hy = 0;
		probe_point(index, hx, hy);
		return std::hypot(x - hx, y - hy) < 10.0f || std::hypot(x - (hx + 13.0f), y - (hy - 13.0f)) < 8.0f;
	}
	// the readout's button that puts the probe back in its holder, while its viewer is selected on the bench
	bool probe_home_rect(int index, Rect &r) const {
		const ItemView &v = view.items[static_cast<size_t>(index)];
		if (index != selected || v.kind != "viewer" || v.place != "bench" || draft.edit != Draft::None || (v.probe_on.empty() && !v.probe_room)) {
			return false;
		}
		r = Rect{ READOUT.x + 16, readout_top() + 90, 150, 30 };
		return true;
	}
	// letting go of a probe: on its own screen it reads that (a frame late); on the rest of its viewer it goes home;
	// on an item or a part it is fixed there, at that place of it; on the room, there; anywhere else, home
	void drop_probing(int index, float x, float y) {
		probing = -1;
		probe_in_hand = false;
		const ItemView &v = view.items[static_cast<size_t>(index)];
		const Rect r = drawn(index);
		json c = { { "do", "probe" }, { "viewer", v.id } };
		if (viewer_screen(r).has(x, y)) {
			double u = 0, w = 0;
			frame_of(index, x, y, u, w);
			c["on"] = v.id;
			c["at"] = { u, w };
		} else if (Rect{ r.x - 6, r.y - 6, r.w + 12, r.h + 12 }.has(x, y)) {
			// home
		} else if (const int t = item_at(x, y, index); t >= 0) {
			double u = 0, w = 0;
			frame_of(t, x, y, u, w);
			c["on"] = id(t);
			c["at"] = { u, w };
		} else if (ROOM.has(x, y)) {
			c["at"] = { (x - BENCH.x) / square(), (y - BENCH.y) / square() };
		}
		act(c);
	}

	// ---- a thermal camera (GAME_SPEC §13.1, Stage 48)
	// its case, and its screen in it (smaller in the cabinet, as all of it is); its sensor's frame is the screen's size
	static constexpr float CAMERA_W = 190, CAMERA_H = 150;
	static Rect camera_screen(const Rect &r) {
		const float s = r.w / CAMERA_W;
		return Rect{ r.x + 19 * s, r.y + 10 * s, 152 * s, 114 * s };
	}
	// the bit its bodies are hidden from its own rays by: one a camera
	uint32_t camera_bit(int index) const {
		int k = 0;
		for (int i = 0; i < index; ++i) {
			k += view.items[static_cast<size_t>(i)].kind == "thermal" ? 1 : 0;
		}
		return 1u << (k % 23); // (Stage 61: the 24th the panels')
	}
	// its frame: in hand, where the pointer has it; fixed on the room, there; else over its own screen
	Rect camera_frame(int index) const {
		const ItemView &v = view.items[static_cast<size_t>(index)];
		const Rect screen = camera_screen(drawn(index));
		if (framing == index) {
			return Rect{ px - frame_dx, py - frame_dy, screen.w, screen.h };
		}
		if (v.thermal.frame_room) {
			return Rect{ BENCH.x + static_cast<float>(v.thermal.frame_x) * square(), BENCH.y + static_cast<float>(v.thermal.frame_y) * square(), screen.w, screen.h };
		}
		return screen;
	}
	// what takes the frame up: its handle, a tab over its top right corner
	Rect frame_handle(int index) const {
		const Rect f = camera_frame(index);
		return Rect{ f.x + f.w - 18, f.y - 12, 18, 12 };
	}
	// (Stage 79) a syringe's plunger on the screen: its face's height with `ml` on its scale, and its handle there (a
	// thin bar over the plunger's rod, across the barrel); none for one not upright in the room, or in hand
	float plunger_face(const Rect &r, const glass::Vessel &g, double ml) const {
		const float px_cm = r.h / static_cast<float>(g.height);
		return r.y + r.h - static_cast<float>(g.plunger_least + ml / g.plunger_area()) * px_cm;
	}
	Rect handle_of(const Rect &r, const glass::Vessel &g, double ml) const {
		const float px_cm = r.h / static_cast<float>(g.height), w = static_cast<float>(g.handle) * px_cm;
		const float top = plunger_face(r, g, ml) - static_cast<float>(g.plunger_length) * px_cm;
		return Rect{ r.cx() - w / 2, top - HANDLE_H, w, HANDLE_H };
	}
	static constexpr float HANDLE_H = 7.0f;
	bool plunger_handle(int index, double ml, Rect &h) const {
		const ItemView &v = view.items[static_cast<size_t>(index)];
		const glass::Vessel *g = glass_of(v);
		if (g == nullptr || !g->syringe || v.inverted || v.pours_into >= 0 || hidden(index) || (v.place != "bench" && v.place != "on" && v.place != "in") ||
				(index == drag && (carrying || moved))) {
			return false;
		}
		h = handle_of(drawn(index), *g, ml);
		return true;
	}
	// what its scale reads with its handle's middle at height y of the screen
	double plunger_ml_at(int index, float y) const {
		const ItemView &v = view.items[static_cast<size_t>(index)];
		const glass::Vessel *g = glass_of(v);
		const Rect r = drawn(index);
		const float px_cm = r.h / static_cast<float>(g->height);
		const float face = y + HANDLE_H / 2 + static_cast<float>(g->plunger_length) * px_cm;
		const double cm = (r.y + r.h - face) / px_cm;
		return std::clamp((cm - g->plunger_least) * g->plunger_area(), 0.0, g->nominal_ml);
	}
	bool plunger_grab(int index, float x, float y) const {
		Rect h;
		return plunger_handle(index, view.items[static_cast<size_t>(index)].plunger_ml, h) && Rect{ h.x - 4, h.y - 6, h.w + 8, h.h + 12 }.has(x, y);
	}
	void plunge(float y) {
		if (plunging < 0 || plunging >= static_cast<int>(view.items.size())) {
			return;
		}
		const double ml = plunger_ml_at(plunging, y + plunge_dy);
		act({ { "do", "plunger" }, { "item", id(plunging) }, { "ml", ml } });
	}
	bool frame_grab(int index, float x, float y) const {
		const ItemView &v = view.items[static_cast<size_t>(index)];
		if (v.kind != "thermal" || v.place != "bench" || hidden(index)) {
			return false;
		}
		const Rect h = frame_handle(index);
		return Rect{ h.x - 4, h.y - 4, h.w + 8, h.h + 8 }.has(x, y);
	}
	// letting go of a frame: more than half over its own screen, home (see-through); else fixed on the room there
	void drop_frame(int index, float x, float y) {
		framing = -1;
		frame_in_hand = false;
		const ItemView &v = view.items[static_cast<size_t>(index)];
		const Rect screen = camera_screen(drawn(index)), f{ x - frame_dx, y - frame_dy, screen.w, screen.h };
		const float over = std::fmax(0.0f, std::fmin(f.x + f.w, screen.x + screen.w) - std::fmax(f.x, screen.x)) * std::fmax(0.0f, std::fmin(f.y + f.h, screen.y + screen.h) - std::fmax(f.y, screen.y));
		if (over > 0.5f * screen.w * screen.h) {
			act({ { "do", "frame" }, { "item", v.id }, { "home", true } });
		} else {
			act({ { "do", "frame" }, { "item", v.id }, { "room", { (f.x - BENCH.x) / square(), (f.y - BENCH.y) / square() } } });
		}
	}
	// Its controls in the readout, while it is selected on the bench (Stage 48): each a button, its name (locate's
	// "readout:camera:<name>"), what it says, the command it sends, whether it is on
	struct CameraButton {
		Rect r;
		std::string name, label;
		json command;
		bool on = false;
	};
	std::vector<CameraButton> camera_buttons(int index) const {
		std::vector<CameraButton> out;
		const ItemView &v = view.items[static_cast<size_t>(index)];
		if (index != selected || v.kind != "thermal" || v.place != "bench" || draft.edit != Draft::None) {
			return out;
		}
		const ThermalSettings &t = v.thermal;
		const float x0 = READOUT.x + 16, y0 = readout_top() + 40;
		const auto set = [&](json fields) {
			fields["do"] = "thermal";
			fields["item"] = v.id;
			return fields;
		};
		static const char *const SHORT[] = { "iron", "white", "black", "rainbow", "arctic" };
		const std::vector<std::string> &names = thermal_palette_names();
		for (size_t k = 0; k < names.size() && k < 5; ++k) {
			out.push_back({ Rect{ x0 + static_cast<float>(k) * 92, y0, 86, 28 }, "palette:" + names[k], SHORT[k], set({ { "palette", names[k] } }), t.palette == names[k] });
		}
		const auto round2 = [](double x) { return std::round(x * 100.0) / 100.0; };
		const CameraState *st = cameras.count(v.id) ? &cameras.at(v.id) : nullptr;
		const double reflected = t.reflected_c.value_or(view.room_celsius);
		const double lo = st ? st->span.low_c : t.level_c - 0.5 * t.span_c, hi = st ? st->span.high_c : t.level_c + 0.5 * t.span_c;
		const float y1 = y0 + 36, y2 = y0 + 72, y3 = y0 + 108;
		out.push_back({ Rect{ x0, y1, 110, 28 }, "markers", t.markers ? "marks on" : "marks off", set({ { "markers", !t.markers } }), t.markers });
		out.push_back({ Rect{ x0 + 118, y1, 130, 28 }, "gain", "gain " + t.gain, set({ { "gain", t.gain == "auto" ? "high" : t.gain == "high" ? "low" : "auto" } }), t.gain != "auto" });
		if (t.frame_room) {
			out.push_back({ Rect{ x0 + 256, y1, 120, 28 }, "home", "frame home", json{ { "do", "frame" }, { "item", v.id }, { "home", true } }, false });
		}
		out.push_back({ Rect{ x0, y2, 34, 28 }, "emissivity:-", "-", set({ { "emissivity", std::max(0.1, round2(t.emissivity - 0.01)) } }), false });
		out.push_back({ Rect{ x0 + 130, y2, 34, 28 }, "emissivity:+", "+", set({ { "emissivity", std::min(1.0, round2(t.emissivity + 0.01)) } }), false });
		out.push_back({ Rect{ x0 + 190, y2, 34, 28 }, "reflected:-", "-", set({ { "reflected_c", std::round(reflected) - 1.0 } }), false });
		out.push_back({ Rect{ x0 + 330, y2, 34, 28 }, "reflected:+", "+", set({ { "reflected_c", std::round(reflected) + 1.0 } }), false });
		// its range: auto, or locked where it shows now; locked, its level and span
		out.push_back({ Rect{ x0, y3, 70, 28 }, "range", t.locked ? "LOCK" : "AUTO",
			t.locked ? set({ { "range", "auto" } }) : set({ { "range", "lock" }, { "level_c", std::round((lo + hi) * 5.0) / 10.0 }, { "span_c", std::max(1.0, std::round((hi - lo) * 10.0) / 10.0) } }), t.locked });
		if (t.locked) {
			out.push_back({ Rect{ x0 + 80, y3, 34, 28 }, "level:-", "-", set({ { "level_c", t.level_c - 5.0 } }), false });
			out.push_back({ Rect{ x0 + 200, y3, 34, 28 }, "level:+", "+", set({ { "level_c", t.level_c + 5.0 } }), false });
			out.push_back({ Rect{ x0 + 244, y3, 34, 28 }, "span:-", "-", set({ { "span_c", std::max(1.0, t.span_c - 5.0) } }), false });
			out.push_back({ Rect{ x0 + 360, y3, 34, 28 }, "span:+", "+", set({ { "span_c", t.span_c + 5.0 } }), false });
		}
		return out;
	}

	// what its screen is given: its frame, how it reads, the span it shows, the palette, what it shows over its picture
	void camera_relay(int index) {
		const ItemView &v = view.items[static_cast<size_t>(index)];
		CameraState &st = cameras[v.id];
		const Rect f = camera_frame(index), screen = camera_screen(drawn(index));
		OpticsRelay &r = st.relay;
		r.frame_x = f.x;
		r.frame_y = f.y;
		r.emissivity = v.thermal.emissivity;
		r.reflected_kelvin = v.thermal.reflected_c ? *v.thermal.reflected_c + 273.15 : 0.0;
		r.low_kelvin = st.span.low_c + 273.15;
		r.high_kelvin = st.span.high_c + 273.15;
		r.palette = thermal_palette(v.thermal.palette);
		r.bit = camera_bit(index);
		thermal_overlay(v.thermal, st.span, st.last, st.hottest_c, st.coldest_c, st.centre_c, static_cast<int>(std::lround(screen.w)), static_cast<int>(std::lround(screen.h)), st.overlay);
		r.lcd_w = st.overlay.w;
		r.lcd_h = st.overlay.h;
		r.overlay = st.overlay.rgb;
		r.overlay_mask = st.overlay.mask;
		r.overlay_hash = st.overlay.hash;
		uint64_t h = std::hash<std::string>{}(v.thermal.palette);
		for (const double x : { v.thermal.emissivity, r.reflected_kelvin, r.low_kelvin, r.high_kelvin }) {
			h = (h ^ std::hash<double>{}(x)) * 1099511628211ull;
		}
		r.hash = h;
	}
	// a frame's step of its search over the reading scene (the room laid out), and what it found: its temperatures and the
	// span it shows them over
	void camera_read(int index) {
		const ItemView &v = view.items[static_cast<size_t>(index)];
		CameraState &st = cameras[v.id];
		const Rect f = camera_frame(index);
		const ThermalFrame frame{ f.x, f.y, static_cast<int>(std::lround(f.w)), static_cast<int>(std::lround(f.h)), camera_bit(index) };
		if (!st.searching || frame.x != st.searched.x || frame.y != st.searched.y || frame.w != st.searched.w || frame.h != st.searched.h) {
			st.search.start(frame);
			st.searched = frame;
			st.searching = true;
		}
		if (!st.search.step(reading, CAMERA_READS)) {
			return;
		}
		st.last = st.search.reading();
		const double room_k = view.room_celsius + 273.15, reflected = v.thermal.reflected_c ? *v.thermal.reflected_c + 273.15 : room_k;
		st.hottest_c = thermal_celsius(st.last.hottest.signal, room_k, v.thermal.emissivity, reflected);
		st.coldest_c = thermal_celsius(st.last.coldest.signal, room_k, v.thermal.emissivity, reflected);
		st.centre_c = thermal_celsius(st.last.centre.signal, room_k, v.thermal.emissivity, reflected);
		st.span = thermal_span(v.thermal, st.coldest_c, st.hottest_c, st.span);
	}

	// ---- the cabinet (Stage 48): every item in a square box of one size, two to a row, its name under it; as many rows
	// as fit over the trash on the screen, scrolled a whole row at a time (by the wheel over it, or its bar)
	static constexpr float CAB_BOX = 100, CAB_ROW = 124, CAB_TOP = 96, CAB_BOTTOM = TRASH.y - 8, CAB_BAR_X = CABINET.x + CABINET.w - 12;
	static int cabinet_shown_rows() { return static_cast<int>((CAB_BOTTOM - CAB_TOP) / CAB_ROW); }
	int cabinet_rows() const {
		int n = 0;
		for (const ItemView &v : view.items) {
			n += v.place == "cabinet" && v.kind != "tray" ? 1 : 0;
		}
		return (n + 1) / 2;
	}
	int cabinet_most_from() const { return std::max(cabinet_rows() - cabinet_shown_rows(), 0); }
	void cabinet_scroll(int rows) { cabinet_from = std::clamp(cabinet_from + rows, 0, cabinet_most_from()); }
	// the box of the k-th item in the cabinet (none where its row is scrolled out of sight)
	std::optional<Rect> cabinet_box(int k) const {
		const int row = k / 2 - std::clamp(cabinet_from, 0, cabinet_most_from());
		if (row < 0 || row >= cabinet_shown_rows()) {
			return std::nullopt;
		}
		return Rect{ CABINET.x + 10 + static_cast<float>(k % 2) * (CAB_BOX + 8), CAB_TOP + static_cast<float>(row) * CAB_ROW, CAB_BOX, CAB_BOX };
	}
	// its scroll bar (none while every row is on the screen): the track, and the thumb on it
	Rect cabinet_track() const { return Rect{ CAB_BAR_X, CAB_TOP, 8, static_cast<float>(cabinet_shown_rows()) * CAB_ROW - 24 }; }
	Rect cabinet_thumb() const {
		const Rect t = cabinet_track();
		const float share = static_cast<float>(cabinet_shown_rows()) / static_cast<float>(std::max(cabinet_rows(), 1));
		const float h = std::fmax(t.h * std::fmin(share, 1.0f), 24.0f);
		const float at = cabinet_most_from() > 0 ? static_cast<float>(std::clamp(cabinet_from, 0, cabinet_most_from())) / static_cast<float>(cabinet_most_from()) : 0.0f;
		return Rect{ t.x, t.y + (t.h - h) * at, t.w, h };
	}
	// the bar held at y: the row whose share of the track y is at
	void cabinet_scroll_to(float y) {
		const Rect t = cabinet_track();
		cabinet_from = static_cast<int>(std::lround(std::clamp((y - t.y) / std::fmax(t.h, 1.0f), 0.0f, 1.0f) * static_cast<float>(cabinet_most_from())));
	}
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
		if (v.place == "trash") {
			// in the trash's box, the last put there (the others are not on the screen), as small as fits it
			if (index != trash_top()) {
				return Rect{ -10000, -10000, 0, 0 };
			}
			if (is_part_kind(v.kind)) {
				return Rect{ TRASH.cx() - 20, TRASH.y + TRASH.h - 56, 40, 40 };
			}
			const float scale = std::fmin(1.0f, std::fmin(200.0f / w, (TRASH.h - 44.0f) / h));
			return Rect{ TRASH.cx() - w * scale / 2, TRASH.y + TRASH.h - 12 - h * scale, w * scale, h * scale };
		}
		if (v.place == "cabinet") {
			// in its box, as large as fits it, standing on the box's floor (Stage 48)
			const std::optional<Rect> box = cabinet_box(slot(index, "cabinet"));
			if (!box) {
				return Rect{ -10000, -10000, 0, 0 };
			}
			const float scale = std::fmin(1.0f, std::fmin((CAB_BOX - 8.0f) / w, (CAB_BOX - 8.0f) / h));
			return Rect{ box->cx() - w * scale / 2, box->y + box->h - 4 - h * scale, w * scale, h * scale };
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
		if (v.place == "in" && !v.stands_in.empty()) {
			// (Stage 71) standing in another vessel, upright on its inside floor: the first a little left of its middle,
			// the second right of it, a third at it (its middle free to drop another on while two stand there)
			int before = 0;
			for (int k = 0; k < index; ++k) {
				before += view.items[static_cast<size_t>(k)].stands_in == v.stands_in ? 1 : 0;
			}
			for (size_t k = 0; k < view.items.size(); ++k) {
				if (view.items[k].id == v.stands_in) {
					const Rect outer = rect(static_cast<int>(k));
					const ItemView &o = view.items[k];
					const glass::Vessel *g = o.glassware.empty() ? nullptr : glass::find(o.glassware, o.nominal_ml);
					const float floor_px = g != nullptr ? static_cast<float>(g->floor / g->height) * outer.h : 0.0f;
					const float share = before == 0 ? 0.28f : before == 1 ? 0.72f : 0.5f;
					return Rect{ outer.x + outer.w * share - w / 2, outer.y + outer.h - floor_px - h, w, h };
				}
			}
		}
		if (v.inverted && v.pours_into >= 0) {
			// upside down over what it pours into, its mouth a little above that one's (Stage 27), held by a stand
			const Rect below = rect(v.pours_into);
			return Rect{ below.cx() - w / 2, below.y - INVERTED_GAP - h, w, h };
		}
		if (v.place == "under" && v.under >= 0) {
			const Rect vessel = rect(v.under);
			if (v.kind == "balance" || v.kind == "spectrophotometer") { // the vessel stands on its pan (Stage 50), in its well (Stage 55)
				return Rect{ vessel.cx() - w / 2, vessel.y + vessel.h, w, h };
			}
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
			if (other.kind == "balance" && other.under == index) {
				lift = STAND + BALANCE_H; // on a balance's pan (Stage 50)
			}
			if (other.kind == "spectrophotometer" && other.under == index) {
				lift = STAND + SPECTRO_H; // in a spectrophotometer's well (Stage 55)
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
		// (Stage 79) what stands in another vessel first: it is drawn in front of it, whatever their order
		for (const bool in_front : { true, false }) {
			const int at = item_at(x, y, except, on_bench, in_front);
			if (at >= 0) {
				return at;
			}
		}
		return -1;
	}
	int item_at(float x, float y, int except, bool on_bench, bool in_front) const {
		for (int i = static_cast<int>(view.items.size()) - 1; i >= 0; --i) {
			const std::string &place = view.items[static_cast<size_t>(i)].place;
			if (i == except || view.items[static_cast<size_t>(i)].kind == "tray" || (on_bench && place != "bench" && place != "on" && place != "under" && place != "in" && place != "circuit") ||
					!visible(i) || (place == "in") != in_front) {
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
	// a vessel with an outlet at its foot (Stage 52: a burette, a pipette), and one with a mark it is filled to
	static const glass::Vessel *glass_of(const ItemView &v) { return v.kind == "vessel" && !v.glassware.empty() ? glass::find(v.glassware, v.nominal_ml) : nullptr; }
	static bool has_outlet(const ItemView &v) { return glass_of(v) != nullptr && glass_of(v)->outlet; }
	static bool has_mark(const ItemView &v) { return glass_of(v) != nullptr && glass_of(v)->mark; }
	// a filter funnel drained, its residue on its paper (Stage 59): carried onto a vessel, it is scraped into it, not
	// stood on it
	static bool residue_only(const ItemView &v) { return glass_of(v) != nullptr && glass_of(v)->paper_herzberg_s > 0.0 && v.solid_ml > 0.0 && !(v.liquid_ml > 0.1); } // (a drained paper holds a drop or two)
	// a burner's valve, or a vessel's cap set upside down over another (Stage 27): one slider, shut to open; and a
	// burette's stopcock (Stage 52)
	bool valve_rect(int index, Rect &r) const {
		const ItemView &v = view.items[static_cast<size_t>(index)];
		const bool cap = v.kind == "vessel" && ((v.inverted && v.pours_into >= 0) || (has_outlet(v) && v.glassware == "burette"));
		if (index != selected || (v.kind != "burner" && v.kind != "spectrophotometer" && !cap) || (v.place != "bench" && v.place != "under" && !cap) || draft.edit != Draft::None) {
			return false;
		}
		r = Rect{ READOUT.x + 90, readout_top() + 96, 300, 18 };
		return true;
	}
	// a spectrophotometer's wavelength on its slider (Stage 55): 0 its range's first nanometre, 1 its last
	static double nm_share(double nm) { return (nm - spectro::model().first_nm) / (spectro::model().last_nm - spectro::model().first_nm); }
	static double nm_at(double share) { return std::round(spectro::model().first_nm + share * (spectro::model().last_nm - spectro::model().first_nm)); }
	// its buttons under the slider: blank, and a nanometre down or up
	std::vector<std::pair<std::string, Rect>> spectro_buttons(int index) const {
		std::vector<std::pair<std::string, Rect>> out;
		Rect slider;
		if (view.items[static_cast<size_t>(index)].kind != "spectrophotometer" || !valve_rect(index, slider)) {
			return out;
		}
		const float y0 = slider.y + 32;
		out.emplace_back("blank", Rect{ READOUT.x + 16, y0, 90, 30 });
		out.emplace_back("-", Rect{ READOUT.x + 116, y0, 44, 30 });
		out.emplace_back("+", Rect{ READOUT.x + 166, y0, 44, 30 });
		// (Stage 56) its cell holder: on or off, 5 C down or up
		out.emplace_back("holder", Rect{ READOUT.x + 230, y0, 110, 30 });
		out.emplace_back("h-", Rect{ READOUT.x + 350, y0, 44, 30 });
		out.emplace_back("h+", Rect{ READOUT.x + 400, y0, 44, 30 });
		return out;
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

	// a balance's tare (Stage 50), while it is selected on the bench or under a vessel
	bool tare_rect(int index, Rect &r) const {
		const ItemView &v = view.items[static_cast<size_t>(index)];
		if (index != selected || v.kind != "balance" || (v.place != "bench" && v.place != "under") || draft.edit != Draft::None) {
			return false;
		}
		r = Rect{ READOUT.x + 16, readout_top() + 90, 90, 30 };
		return true;
	}

	// an upright bottle's screw cap, on or off, while it is selected on the bench (Stage 28)
	bool screw_rect(int index, Rect &r) const {
		const ItemView &v = view.items[static_cast<size_t>(index)];
		const glass::Vessel *g = v.kind == "vessel" && !v.glassware.empty() ? glass::find(v.glassware, v.nominal_ml) : nullptr;
		const bool nozzle = g != nullptr && g->syringe && v.on < 0 && v.tube < 0 && (v.place == "bench" || v.place == "in"); // (Stage 79) a syringe's nozzle cap
		if (index != selected || g == nullptr || !(g->screw_cap || nozzle) || v.burst || v.pours_into >= 0 || v.holds >= 0 || (v.place != "bench" && !nozzle) || draft.edit != Draft::None) {
			return false;
		}
		r = Rect{ READOUT.x + 116, readout_top() + 90, 140, 30 };
		return true;
	}
	// (Stage 79) a syringe's plunger lock, while it is in the room
	bool lock_rect(int index, Rect &r) const {
		const ItemView &v = view.items[static_cast<size_t>(index)];
		if (index != selected || !v.syringe || (v.place != "bench" && v.place != "on" && v.place != "in") || draft.edit != Draft::None) {
			return false;
		}
		r = Rect{ READOUT.x + 262, readout_top() + 90, 130, 30 };
		return true;
	}

	Rect speed_rect(int i) const { return Rect{ 380 + static_cast<float>(i) * 80, 14, 72, 36 }; }
	static Rect pause_rect() { return Rect{ 270, 14, 96, 36 }; }
	static Rect check_rect() { return Rect{ 704, 14, 200, 36 }; }
	static Rect reset_rect() { return Rect{ 916, 14, 80, 36 }; } // (Stage 55: narrower, for the notebook's button)
	static Rect view_rect() { return Rect{ 1440, 14, 150, 36 }; } // while zoomed: back to all of the room (Stage 42)
	static Rect empty_rect() { return Rect{ TRASH.x + TRASH.w - 70, TRASH.y + 4, 64, 24 }; } // the trash's "empty" (Stage 38)
	static Rect pour_slider() { return Rect{ PANEL.x + 30, PANEL.y + 84, PANEL.w - 60, 18 }; }
	static Rect pour_button() { return Rect{ PANEL.x + 30, PANEL.y + 118, 180, 36 }; }
	// the same amount as a trickle (Stage 47): laid on the liquid there as a layer of its own, not stirred in
	static Rect gentle_button() { return Rect{ PANEL.x + 230, PANEL.y + 118, 180, 36 }; }
	static Rect cancel_button() { return Rect{ PANEL.x + PANEL.w - 126, PANEL.y + PANEL.h - 50, 96, 36 }; }
	// upside down over it, with a cap: a placed pour (Stage 27)
	static Rect invert_button() { return Rect{ PANEL.x + 30, PANEL.y + PANEL.h - 50, 240, 36 }; }
	// into a volumetric flask: up to its mark (Stage 52), beside the pours
	static Rect mark_button() { return Rect{ PANEL.x + PANEL.w - 150, PANEL.y + 168, 120, 34 }; }
	// (Stage 71) stood in it, upright on its floor: a tube in a bath, under the mark's place
	static Rect stand_button() { return Rect{ PANEL.x + PANEL.w - 150, PANEL.y + 210, 120, 34 }; }
	// (Stage 79) a syringe over a vessel: its nozzle on the vessel's mouth, or tubed to another syringe's nozzle
	static Rect join_button() { return Rect{ PANEL.x + PANEL.w - 150, PANEL.y + 250, 120, 34 }; }
	bool could_join() const {
		if (!panel || from < 0 || to < 0) {
			return false;
		}
		const ItemView &a = view.items[static_cast<size_t>(from)], &b = view.items[static_cast<size_t>(to)];
		return a.syringe && b.kind == "vessel" && !a.screwed && a.tube < 0 && a.on < 0 && (b.syringe ? b.tube < 0 && b.on < 0 && !b.screwed : b.holds < 0);
	}
	// a vessel that could stand in the other: glassware both, the other the larger
	bool could_stand() const {
		if (!panel || from < 0 || to < 0) {
			return false;
		}
		const ItemView &a = view.items[static_cast<size_t>(from)], &b = view.items[static_cast<size_t>(to)];
		return a.kind == "vessel" && b.kind == "vessel" && !a.glassware.empty() && !b.glassware.empty() && b.nominal_ml > a.nominal_ml;
	}
	// the solids of the vessel being emptied, each with its button; by instruments (Stage 50) one button for them all,
	// "solid", which names none of them
	std::vector<std::pair<std::string, Rect>> tip_buttons() const {
		std::vector<std::pair<std::string, Rect>> out;
		if (!panel || from < 0) {
			return out;
		}
		for (const SubstanceView &s : view.items[static_cast<size_t>(from)].contents) {
			if (s.phase == 's' && !s.wall && out.size() < 3) {
				out.emplace_back(view.instruments ? "solid" : s.species, Rect{ PANEL.x + 30, PANEL.y + 168 + static_cast<float>(out.size()) * 40, 260, 34 });
				if (view.instruments) {
					break;
				}
			}
		}
		return out;
	}
	// the amount typed (Stage 50): its field, top right of the menu; what it is of (mL of the liquid, else g of the solid)
	static Rect amount_field() { return Rect{ PANEL.x + PANEL.w - 160, PANEL.y + 14, 130, 34 }; }
	bool amount_in_ml() const { return from >= 0 && view.items[static_cast<size_t>(from)].liquid_ml > 0.0; }
	std::optional<double> typed() const {
		char *end = nullptr;
		const double v = std::strtod(amount.c_str(), &end);
		return !amount.empty() && end != nullptr && *end == '\0' && v > 0.0 && std::isfinite(v) ? std::optional<double>(v) : std::nullopt;
	}
	// a key while the pour menu is open: digits and a point type its amount, Backspace takes one back, Enter pours what
	// is typed (or tips it, from a vessel with no liquid), Escape closes it
	void type_amount(Ui::Key key, uint32_t codepoint) {
		if (key == Ui::KeyText && ((codepoint >= '0' && codepoint <= '9') || (codepoint == '.' && amount.find('.') == std::string::npos)) && amount.size() < 9 &&
				allowed_text(amount + static_cast<char>(codepoint))) {
			amount += static_cast<char>(codepoint);
			if (const std::optional<double> v = typed(); v && amount_in_ml()) {
				pour_fraction = std::clamp(*v / view.items[static_cast<size_t>(from)].liquid_ml, 0.02, 1.0);
			}
		} else if (key == Ui::KeyBackspace && !amount.empty()) {
			amount.pop_back();
		} else if (key == Ui::KeyEscape) {
			panel = false; // still in hand
		} else if (key == Ui::KeyEnter && typed()) {
			if (amount_in_ml()) {
				press(pour_button().cx(), pour_button().cy());
			} else if (const auto tips = tip_buttons(); !tips.empty()) {
				press(tips.front().second.cx(), tips.front().second.cy());
			}
		}
	}

	bool act(const json &command) {
		std::string why;
		const bool ok = lab->command(command.dump(), why);
		message = ok ? std::string() : why;
		refresh();
		return ok;
	}

	const std::string &id(int index) const { return view.items[static_cast<size_t>(index)].id; }
	// (Stage 61) a fresh copy's carry over: put where it stays (on the bench, on or under equipment, handed in to
	// Storage's slot it was dropped on), it is kept; still in Storage otherwise (put nowhere, back over Supplies, a
	// drop refused), the lab is loaded back as it was before it was taken
	bool fresh_dropped_in_storage = false;
	void settle_fresh() {
		if (fresh_copy.empty()) {
			return;
		}
		const int at = item_index(fresh_copy);
		if (at >= 0 && at == drag) {
			return; // still in hand
		}
		const std::string &place = at >= 0 ? view.items[static_cast<size_t>(at)].place : std::string();
		const bool stays = place == "bench" || place == "on" || place == "under" || place == "in" || (place == "outbox" && fresh_dropped_in_storage);
		if (!stays) {
			const std::string refused = message;
			std::string no;
			if (std::unique_ptr<Lab> was = Lab::load(fresh_undo, no)) {
				lab = std::move(was);
				drag = -1;
				carrying = false;
				refresh();
				choose_only(-1);
			}
			message = refused;
		}
		fresh_copy.clear();
		fresh_undo.clear();
		fresh_dropped_in_storage = false;
	}
	// (Stage 61) the name a supply's box is known by in the pass, apart from its first copy's
	std::map<std::string, std::string> supply_names;
	const char *supply_name(const std::string &id) {
		std::string &name = supply_names[id];
		if (name.empty()) {
			name = "supply:" + id;
		}
		return name.c_str();
	}
	// the lab's items and parts by id, and the one made since (Stage 61: what the screen lists after them, Supplies,
	// is not the lab's: the newest is found by its id, not as the last)
	std::vector<std::string> ids_now() const {
		std::vector<std::string> out;
		for (int i = 0; i < supply_base; ++i) {
			out.push_back(view.items[static_cast<size_t>(i)].id);
		}
		return out;
	}
	int newest(const std::vector<std::string> &before) const {
		for (int i = 0; i < supply_base; ++i) {
			if (std::find(before.begin(), before.end(), view.items[static_cast<size_t>(i)].id) == before.end()) {
				return i;
			}
		}
		return supply_base - 1;
	}

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
	// what was last put in the trash (Stage 34): the one thing of the trash that is on the screen; -1: none
	int trash_top() const { return view.trash.empty() ? -1 : view.trash.back(); }
	bool hidden(int index) const {
		const ItemView &v = view.items[static_cast<size_t>(index)];
		return (v.place == "trash" && index != trash_top()) || (v.place == "cabinet" && !cabinet_box(slot(index, "cabinet"))); // (scrolled out of sight: Stage 48)
	}
	bool visible(int index) const {
		const ItemView &v = view.items[static_cast<size_t>(index)];
		if (v.place == "trash") {
			return index == trash_top();
		}
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
	// a vessel's pouring lip and tip in its own rectangle, upright: from its left edge and from its top, pixels
	bool lip_in(int index, int side, float &lip_x, float &lip_y, float &tip_x, float &tip_y) const {
		const ItemView &v = view.items[static_cast<size_t>(index)];
		const glass::Vessel *g = v.glassware.empty() ? nullptr : glass::find(v.glassware, v.nominal_ml);
		if (g == nullptr) {
			return false;
		}
		float w = 0, h = 0;
		size_of(v, w, h);
		const double px_per_cm = w / (2.0 * g->widest());
		const contents::Lip lip = contents::lip_of(*g, side);
		lip_x = static_cast<float>(w / 2.0 + lip.lip_x * px_per_cm);
		lip_y = static_cast<float>(h - lip.lip_y * px_per_cm);
		tip_x = static_cast<float>(w / 2.0 + lip.tip_x * px_per_cm);
		tip_y = static_cast<float>(h - lip.tip_y * px_per_cm);
		return true;
	}
	// the lip a vessel in a pouring position pours from: the one nearer the axis of what it pours into
	int side_of(int index) const {
		if (index == drag && pouring) {
			return pour_side;
		}
		const ItemView &v = view.items[static_cast<size_t>(index)];
		return v.pours_into >= 0 && rect(v.pours_into).cx() < rect(index).cx() ? -1 : 1;
	}
	// How a vessel pouring by hand is turned (Stage 31): about its pouring tip, a place of the screen, by an angle
	// clockwise as seen. False for what is not turned.
	bool turned(int index, const Rect &r, float &angle, float &pivot_x, float &pivot_y) const {
		const ItemView &v = view.items[static_cast<size_t>(index)];
		float lx = 0, ly = 0, tx = 0, ty = 0;
		if (v.pours_into < 0 || v.inverted || !(v.turn > 0.0) || !lip_in(index, side_of(index), lx, ly, tx, ty)) {
			return false;
		}
		angle = static_cast<float>(side_of(index) * v.turn);
		pivot_x = r.x + tx;
		pivot_y = r.y + ty;
		return true;
	}
	Rect drawn(int index) const {
		Rect r = rect(index);
		if (index == drag) {
			const ItemView &v = view.items[static_cast<size_t>(index)];
			if (v.place == "cabinet" || v.place == "outbox" || v.place == "trash") {
				// (Stage 61) taken up from off the bench: in hand at its full size, held at the same share of it
				float w = 0, h = 0;
				size_of(v, w, h);
				const float sx = r.w > 0.0f ? w / r.w : 1.0f, sy = r.h > 0.0f ? h / r.h : 1.0f;
				r = Rect{ px - grab_x * sx, py - grab_y * sy, w, h };
				return r;
			}
			r.x = px - grab_x;
			r.y = py - grab_y;
			float lx = 0, ly = 0, tx = 0, ty = 0;
			if (pouring && pour_over >= 0 && eased > 0.0 && lip_in(index, pour_side, lx, ly, tx, ty)) {
				// to where its tip is over the middle of the other's mouth, a little above it
				const Rect over = rect(pour_over);
				const float e = static_cast<float>(eased * eased * (3.0 - 2.0 * eased));
				r.x += (over.cx() - tx - r.x) * e;
				r.y += (over.y - LIP_GAP - ty - r.y) * e;
			}
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
		if (TRASH.has(x, y)) {
			for (const int i : std::vector<int>(standing)) {
				act({ { "do", "trash" }, { "item", id(i) } });
			}
			choose_only(-1);
			return;
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
		// (a burette or a pipette delivers by its tip: Stage 52; a drained funnel's residue is scraped out: Stage 59)
		return a.kind == "vessel" && !a.glassware.empty() && (!has_outlet(a) || residue_only(a)) && b.kind == "vessel" && !b.condenser && b.place == "bench";
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
		pour_over = -1;
		tilt = eased = 0.0;
		act({ { "do", "unpour" }, { "item", id(drag) } });
	}
	void drop(int index, float x, float y) {
		const ItemView v = view.items[static_cast<size_t>(index)];
		const int target = item_at(x, y, index, true);
		// first it comes off what it is joined to
		const bool was_on = v.place == "on", was_under = v.place == "under";
		if (target >= 0 && (target == v.on || target == v.under || (!v.stands_in.empty() && view.items[static_cast<size_t>(target)].id == v.stands_in))) {
			return; // put back where it was (Stage 71: in the vessel it stands in)
		}
		if (!guided_drop(index, target, x, y)) {
			return; // back where it was
		}
		if (index >= supply_base) {
			// (Stage 61) a supply: a copy of it is taken out, and it is that copy that lands; back over Supplies or into the
			// trash, nothing is taken
			if (CABINET.has(x, y) || TRASH.has(x, y)) {
				return;
			}
			if (BENCH.has(x, y) && target < 0) {
				const int col = std::clamp(static_cast<int>((x - BENCH.x) / square()) - anchor_c, 0, std::max(0, view.cols - v.w));
				const int row = std::clamp(static_cast<int>((y - BENCH.y) / square()) - anchor_r, 0, std::max(0, view.rows - v.h));
				if (act({ { "do", "take" }, { "supply", v.id }, { "at", { col, row } } })) {
					choose_only(item_index(lab->last_taken()));
				}
				return;
			}
			// onto equipment: its copy taken out and dropped there; refused there, the lab as it was before, that copy too
			const std::string before = lab->save();
			if (!act({ { "do", "take" }, { "supply", v.id } })) {
				return;
			}
			const std::string copy = lab->last_taken();
			const int made = item_index(copy);
			message.clear();
			if (made >= 0 && made < supply_base) {
				drop(made, x, y);
			}
			const int now = item_index(copy);
			if (!message.empty() && (now < 0 || view.items[static_cast<size_t>(now)].place == "outbox")) {
				const std::string refused = message;
				std::string no;
				if (std::unique_ptr<Lab> was = Lab::load(before, no)) {
					lab = std::move(was);
					refresh();
				}
				message = refused;
			}
			return;
		}
		if (is_part_kind(v.kind)) {
			// a signal part: along the bench or within its open circuit, or back to the drawer
			int inner_col = 0, inner_row = 0;
			if (TRASH.has(x, y)) {
				if (v.place != "trash") {
					act({ { "do", "trash" }, { "item", v.id } }); // (Stage 34: kept, with its settings, to be taken up again)
				}
				choose_only(-1);
			} else if (CABINET.has(x, y) || DRAWER.has(x, y)) {
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
		if (was_under && !act(v.kind == "balance"		   ? json{ { "do", "unweigh" }, { "balance", v.id } }
							  : v.kind == "spectrophotometer" ? json{ { "do", "remove" }, { "spectrophotometer", v.id } }
															  : json{ { "do", "unheat" }, { "burner", v.id } })) {
			return;
		}
		if (target >= 0) {
			const ItemView t = view.items[static_cast<size_t>(target)];
			if (v.kind == "burner") {
				if (t.kind == "vessel") {
					act({ { "do", "heat" }, { "burner", v.id }, { "vessel", t.id } });
				}
			} else if (v.kind == "balance") {
				if (t.kind == "vessel") { // under the vessel, where it stands (Stage 50)
					act({ { "do", "weigh" }, { "balance", v.id }, { "vessel", t.id } });
				}
			} else if (v.kind == "spectrophotometer") {
				if (t.kind == "vessel") { // under a cuvette, where it stands (Stage 55)
					act({ { "do", "insert" }, { "spectrophotometer", v.id }, { "cuvette", t.id } });
				}
			} else if (has_outlet(v) && t.kind == "vessel" && !residue_only(v)) {
				// (Stage 52) a pipette, empty, draws to its mark from what it is dipped into; full, or a burette, stands on
				// the vessel, its tip in the mouth, and drains into it
				if (v.glassware == "pipette" && !(v.condensed_ml > 1e-6)) {
					act({ { "do", "fill" }, { "item", v.id }, { "from", t.id } });
				} else {
					act({ { "do", "attach" }, { "item", v.id }, { "to", t.id } });
				}
			} else if (t.kind == "balance" && v.kind == "vessel" && t.place == "bench") {
				act({ { "do", "weigh" }, { "balance", t.id }, { "vessel", v.id }, { "onto", true } }); // set on its pan (Stage 50)
			} else if (t.kind == "spectrophotometer" && v.kind == "vessel" && t.place == "bench") {
				act({ { "do", "insert" }, { "spectrophotometer", t.id }, { "cuvette", v.id }, { "onto", true } }); // into its well (Stage 55)
			} else if (t.kind != "vessel") {
				message = "\"" + t.id + "\" takes nothing";
			} else if (v.kind == "balloon" || v.kind == "stopper" || v.condenser) {
				act({ { "do", "attach" }, { "item", v.id }, { "to", t.id } });
			} else if (t.condenser) {
				// (Stage 34: an outlet is led to a vessel by a line drawn from it, as a part's port is wired)
				message = "drag a line from \"" + t.id + "\"'s outlet to the vessel it runs into";
			} else {
				// a vessel over a vessel: ask what to pour or tip
				panel = true;
				from = index;
				to = target;
				pour_fraction = v.liquid_ml > 50.0 ? 50.0 / v.liquid_ml : 1.0;
				amount.clear();
			}
			return;
		}
		if (OUTBOX.has(x, y)) {
			json c = { { "do", "submit" }, { "item", v.id } };
			if (slot_at(x, y) > 0) {
				c["slot"] = slot_at(x, y);
			}
			fresh_dropped_in_storage = act(c) && v.id == fresh_copy; // (Stage 61: a fresh copy set in Storage stays there)
			return;
		}
		if (TRASH.has(x, y)) {
			if (view.items[static_cast<size_t>(index)].place != "trash") {
				act({ { "do", "trash" }, { "item", v.id } });
				choose_only(-1);
			}
			return;
		}
		if (CABINET.has(x, y)) {
			const std::string &stood = view.items[static_cast<size_t>(index)].place;
			if (stood == "bench" || stood == "trash") {
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
		if (framing >= 0 && frame_in_hand) {
			drop_frame(framing, x, y); // a frame in hand (Stage 48): this press is where it goes
			return;
		}
		if (search_press(x, y)) {
			return; // a search's field, to type into (Stage 61)
		}
		if (cabinet_most_from() > 0 && Rect{ CAB_BAR_X - 6, CAB_TOP, 20, cabinet_track().h }.has(x, y) && drag < 0) {
			cabinet_scrolling = true; // the cabinet's bar, held (Stage 48)
			cabinet_scroll_to(y);
			return;
		}
		if (storage_most_from() > 0 && Rect{ storage_track().x, storage_track().y - 6, storage_track().w, 18 }.has(x, y) && drag < 0) {
			storage_scrolling = true; // Storage's bar, held (Stage 61)
			storage_scroll_to(x);
			return;
		}
		if (probing >= 0 && probe_in_hand) {
			drop_probing(probing, x, y); // a probe in hand (Stage 42): this press is where it goes
			return;
		}
		if (making >= 0 && making_carried) {
			return; // a part in hand (Stage 34): this press is where it goes, at its release
		}
		if (menu_press(x, y)) {
			return; // the experiments are the player's, like the clock (Stage 41)
		}
		if (draft.edit != Draft::None) {
			// the clock stays the player's while a part is set, and so does the tutorial's switch
			if (!pause_rect().has(x, y) && !(speed_rect(0).has(x, y) || speed_rect(1).has(x, y) || speed_rect(2).has(x, y) || speed_rect(3).has(x, y)) &&
					!(strict_shown() && strict_rect().has(x, y))) {
				edit_press(x, y);
				return;
			}
		}
		if (notices_press(x, y) || debug_press(x, y) || notebook_press(x, y) || predict_press(x, y) || report_press(x, y)) {
			return; // the notices and the debug panel are the player's, like the clock, under a tutorial too
		}
		if (strict_shown() && strict_rect().has(x, y)) {
			strict = !strict; // the player's at any time (Stage 41): strict again holds the player to this step at once
			message.clear();
			return;
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
			const bool gently = gentle_button().has(x, y);
			if ((pour_button().has(x, y) || gently) && view.items[static_cast<size_t>(from)].liquid_ml > 0.0) {
				if (!allowed_click(gently ? "panel:gentle" : "panel:pour")) {
					return;
				}
				const std::optional<double> asked = typed();
				const double ml = asked ? *asked : view.items[static_cast<size_t>(from)].liquid_ml * pour_fraction;
				json command = { { "do", "pour" }, { "from", id(from) }, { "to", id(to) } };
				if (pour_fraction >= 1.0 && !asked) {
					command["all"] = true;
				} else {
					command["ml"] = ml;
				}
				if (view.instruments) {
					command["about"] = true; // a hand's pour (Stage 50)
				}
				if (gently) {
					command["gentle"] = true;
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
					// all of it, or the grams typed (Stage 50; from a vessel with no liquid); by instruments the solids, by hand
					json command = { { "do", "tip" }, { "from", id(from) }, { "to", id(to) } };
					if (species != "solid" || !view.instruments) {
						command["species"] = species;
					}
					if (const std::optional<double> g = typed(); g && !amount_in_ml()) {
						command["g"] = *g;
					} else {
						command["all"] = true;
					}
					if (view.instruments) {
						command["about"] = true;
					}
					if (act(command)) {
						panel = false;
						put_back();
					}
					return;
				}
			}
			if (mark_button().has(x, y) && has_mark(view.items[static_cast<size_t>(to)]) && !has_mark(view.items[static_cast<size_t>(from)])) {
				// (Stage 52) a volumetric flask made up to its mark from what is over it
				if (allowed_click("panel:mark") && act({ { "do", "fill" }, { "item", id(to) }, { "from", id(from) } })) {
					panel = false;
					put_back();
				}
				return;
			}
			if (could_stand() && stand_button().has(x, y)) {
				if (allowed_click("panel:stand") && act({ { "do", "stand" }, { "item", id(from) }, { "in", id(to) } })) {
					panel = false;
					put_back(); // it stands in the other now
				}
				return;
			}
			if (could_join() && join_button().has(x, y)) {
				const bool tube = view.items[static_cast<size_t>(to)].syringe;
				if (allowed_click(tube ? "panel:tube" : "panel:attach") &&
						act(tube ? json{ { "do", "tube" }, { "item", id(from) }, { "to", id(to) } } : json{ { "do", "attach" }, { "item", id(from) }, { "to", id(to) } })) {
					panel = false;
					put_back(); // on the other's mouth, or back where it was with its tube
				}
				return;
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
		if (!view.trash.empty() && empty_rect().has(x, y) && !guided()) {
			act({ { "do", "empty_trash" } });
			choose_only(-1);
			return;
		}
		if (zoomed() && view_rect().has(x, y)) {
			set_view(view_whole(VIEW_ROOM)); // the player's at any time, like the clock (Stage 42)
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
				begin(std::move(again));
				if (tutorial) {
					tutorial->restart();
					clear_report(); // (Stage 54: a report anew)
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
					const std::string &kind = view.items[static_cast<size_t>(i)].kind;
					if (!allowed_click((kind == "burner" ? "valve:" : kind == "spectrophotometer" ? "nm:" : "cap:") + id(i) + "@")) {
						return;
					}
					valve_drag = i;
					turn(x);
					return;
				}
			}
			for (const CameraButton &b : camera_buttons(i)) {
				if (b.r.has(x, y)) {
					if (allowed_click("readout:camera:" + b.name)) {
						act(b.command); // a thermal camera's control (Stage 48)
					}
					return;
				}
			}
			if (probe_home_rect(i, r) && r.has(x, y)) {
				if (!allowed_click("readout:probe:holder")) {
					return;
				}
				act({ { "do", "probe" }, { "viewer", id(i) } });
				return;
			}
			if (stir_rect(i, r) && r.has(x, y)) {
				if (!allowed_click("stir:" + id(i))) {
					return;
				}
				act({ { "do", "stir" }, { "vessel", id(i) }, { "on", !view.items[static_cast<size_t>(i)].stirred } });
				return;
			}
			for (const auto &[name, b] : spectro_buttons(i)) {
				if (b.has(x, y)) {
					// (Stage 55) its blank, or a nanometre down or up; (Stage 56) its holder on or off, 5 C down or up
					const ItemView &sv = view.items[static_cast<size_t>(i)];
					const bool holding = name == "holder" || name == "h-" || name == "h+";
					const std::string named = name == "blank" ? "blank:" + id(i) : holding ? "cell:" + id(i) + (name == "holder" ? "" : name == "h-" ? ":-" : ":+") : "nm:" + id(i) + ":" + name;
					if (allowed_click(named)) {
						if (holding) {
							const double now = sv.holder_c ? *sv.holder_c : 25.0;
							const json c = name == "holder" ? (sv.holder_c ? json(nullptr) : json(25.0)) : json(std::clamp(now + (name == "h+" ? 5.0 : -5.0), spectro::model().holder_least_c, spectro::model().holder_most_c));
							act({ { "do", "holder" }, { "spectrophotometer", id(i) }, { "celsius", c } });
						} else {
							const double nm = sv.nm + (name == "+" ? 1.0 : -1.0);
							act(name == "blank" ? json{ { "do", "blank" }, { "spectrophotometer", id(i) } } : json{ { "do", "tune" }, { "spectrophotometer", id(i) }, { "nm", nm } });
						}
					}
					return;
				}
			}
			if (tare_rect(i, r) && r.has(x, y)) {
				if (allowed_click("tare:" + id(i))) {
					act({ { "do", "tare" }, { "balance", id(i) } }); // (Stage 50)
				}
				return;
			}
			if (screw_rect(i, r) && r.has(x, y)) {
				if (!allowed_click("screw:" + id(i))) {
					return;
				}
				act({ { "do", "cap" }, { "item", id(i) }, { "to", view.items[static_cast<size_t>(i)].screwed ? 1.0 : 0.0 } });
				return;
			}
			if (lock_rect(i, r) && r.has(x, y)) {
				if (!allowed_click("lock:" + id(i))) {
					return;
				}
				act({ { "do", "plunger" }, { "item", id(i) }, { "lock", !view.items[static_cast<size_t>(i)].plunger_locked } });
				return;
			}
		}
		for (const auto &[what, r] : record_buttons()) {
			if (r.has(x, y) && !carrying) {
				if (allowed_click("record:" + id(selected) + ":" + what)) {
					act({ { "do", "record" }, { "item", id(selected) }, { "what", what } }); // into the notebook (Stage 54)
				}
				return;
			}
		}
		if (carrying && drag >= 0) {
			// a press while carrying (Stage 27): over a vessel it pours into, a held pour, or with the right button
			// the full menu; anywhere else its release puts it down
			if (pouring) {
				return; // let go and pressed again before it was back in the hand: it tilts again
			}
			const int target = item_at(x, y, drag, true);
			// (Stage 71) over its own place in the vessel it stands in: put down there, not poured into it
			const ItemView &held = view.items[static_cast<size_t>(drag)];
			const bool own = target >= 0 && !held.stands_in.empty() && view.items[static_cast<size_t>(target)].id == held.stands_in && own_place(drag, x, y);
			if (!own && pours_into(drag, target)) {
				if (mods & Ui::ModRight) {
					if (guided() && !guided_drop(drag, target, x, y)) {
						return;
					}
					panel = true;
					from = drag;
					to = target;
					pour_fraction = view.items[static_cast<size_t>(drag)].liquid_ml > 50.0 ? 50.0 / view.items[static_cast<size_t>(drag)].liquid_ml : 1.0;
					amount.clear();
					return;
				}
				if (guided()) {
					not_now(); // a tutorial pours by the menu, an amount
					return;
				}
				const float mine = drawn(drag).cx(), its = rect(target).cx();
				if (act({ { "do", "hold" }, { "item", id(drag) }, { "over", id(target) } })) {
					pouring = true;
					pour_over = target;
					pour_side = its < mine ? -1 : 1;
					tilt = eased = 0.0;
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
		int hit = item_at(x, y);
		if (hit < 0 && !opened.empty() && circuit_panel().has(x, y)) {
			return; // the open circuit's panel: no rectangle across it
		}
		std::optional<Rect> held_by; // (Stage 61) what was pressed, when it is not what is held: a supply's box
		if (hit >= supply_base && !(mods & Ui::ModAny)) {
			// (Stage 61) a supply pressed: its copy taken out at once, a real item in hand at its full size, simulated and
			// drawn by the pass; put nowhere, back over Supplies or refused where it is dropped, the lab is as it was
			held_by = rect(hit);
			const std::string before = lab->save();
			if (!act({ { "do", "take" }, { "supply", id(hit) } })) {
				return;
			}
			fresh_copy = lab->last_taken();
			fresh_undo = before;
			hit = item_index(fresh_copy);
			if (hit < 0) {
				return;
			}
		}
		if (hit < 0 && BENCH.has(x, y)) {
			marquee = true; // a rectangle, or, let go where it began, a click on nothing
			return;
		}
		if (hit < 0 && (CABINET.has(x, y) || OUTBOX.has(x, y)) && !(mods & Ui::ModAny)) {
			choose_only(-1); // a click on nothing lets go of the selection
		}
		if (hit >= 0) { // (Stage 61: what is in Storage is taken up as anything off the bench is)
			// a press takes it in hand; a release without moving it selects it (§13.3),
			// and a drop the tutorial does not ask for is refused when it is let go
			const Rect r = held_by ? *held_by : rect(hit); // (held where it was pressed: a supply's box, its copy's size)
			drag = hit;
			grab_x = x - r.x;
			grab_y = y - r.y;
			if (held_by) {
				const Rect own = rect(hit); // (drawn() scales the hold from the copy's rectangle to its full size)
				grab_x *= own.w / std::fmax(r.w, 1.0f);
				grab_y *= own.h / std::fmax(r.h, 1.0f);
			}
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
	// A condenser's outlet (Stage 34): its end, where the line to the vessel it runs into is drawn from and where
	// it is taken hold of, as a part's port is.
	bool outlet_rect(int index, Rect &r) const {
		if (index < 0 || index >= static_cast<int>(view.items.size()) || !view.items[static_cast<size_t>(index)].condenser || index == drag) {
			return false;
		}
		const std::string &place = view.items[static_cast<size_t>(index)].place;
		if (place != "bench" && place != "on") {
			return false; // (in the cabinet its outlet leads nowhere)
		}
		const Rect body = rect(index);
		r = Rect{ body.x + body.w + 26 - 7, body.y + body.h + 14 - 7, 14, 14 };
		return true;
	}
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
	// ---- Storage (Stage 61: the outbox, as many slots as it needs): at least six, always one free after the last that
	// holds anything or has a goal; those its search leaves, six on the screen at a time, scrolled a slot at a time
	static constexpr int SHOWN_SLOTS = 6;
	static constexpr uint32_t PANEL_BIT = 1u << 23; // (Stage 61: the pixel source's PANEL_BIT)
	int storage_count() const {
		int last = OUTBOX_SLOTS - 1;
		for (const ItemView &v : view.items) {
			last = v.place == "outbox" ? std::max(last, v.slot) : last;
		}
		for (const Goal &g : view.goals) {
			last = std::max(last, g.slot);
		}
		return last + 1;
	}
	std::vector<int> shown_slots() const {
		std::vector<int> out;
		for (int k = 1; k <= storage_count(); ++k) {
			const Goal *g = goal_of(k);
			bool shown = storage_query.empty();
			for (const ItemView &v : view.items) {
				if (!shown && v.place == "outbox" && v.slot == k) {
					shown = lab::search::matches(storage_query, lab::search::keywords(v, g != nullptr ? std::string_view(g->name) : std::string_view{}));
				}
			}
			if (!shown && g != nullptr) {
				std::vector<std::string> words{ "goal", std::to_string(k) };
				words.push_back(g->name);
				std::string w;
				for (const char c : g->name + " ") {
					if (c == ' ') {
						if (!w.empty()) {
							words.push_back(w);
						}
						w.clear();
					} else {
						w += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
					}
				}
				shown = lab::search::matches(storage_query, words);
			}
			if (shown) {
				out.push_back(k);
			}
		}
		return out;
	}
	int storage_most_from() const { return std::max(static_cast<int>(shown_slots().size()) - SHOWN_SLOTS, 0); }
	void storage_scroll(int slots) { storage_from = std::clamp(storage_from + slots, 0, storage_most_from()); }
	// a slot's box, where it is shown (off the screen while scrolled out of sight, or left by the search)
	Rect slot_rect(int k) const {
		const std::vector<int> shown = shown_slots();
		const auto at = std::find(shown.begin(), shown.end(), k);
		const int pos = at == shown.end() ? -1 : static_cast<int>(at - shown.begin()) - std::clamp(storage_from, 0, storage_most_from());
		if (pos < 0 || pos >= SHOWN_SLOTS) {
			return Rect{ -10000, -10000, 0, 0 };
		}
		return Rect{ OUTBOX.x + 8 + static_cast<float>(pos) * 104, OUTBOX.y + 36, 100, 250 };
	}
	// its scroll bar (none while every slot shown is on the screen), under the slots
	Rect storage_track() const { return Rect{ OUTBOX.x + 8, OUTBOX.y + OUTBOX.h - 10, SHOWN_SLOTS * 104.0f - 4, 6 }; }
	Rect storage_thumb() const {
		const Rect t = storage_track();
		const float share = static_cast<float>(SHOWN_SLOTS) / static_cast<float>(std::max<size_t>(shown_slots().size(), 1));
		const float w = std::fmax(t.w * std::fmin(share, 1.0f), 24.0f);
		const float at = storage_most_from() > 0 ? static_cast<float>(std::clamp(storage_from, 0, storage_most_from())) / static_cast<float>(storage_most_from()) : 0.0f;
		return Rect{ t.x + (t.w - w) * at, t.y, w, t.h };
	}
	void storage_scroll_to(float x) {
		const Rect t = storage_track();
		storage_from = static_cast<int>(std::lround(std::clamp((x - t.x) / std::fmax(t.w, 1.0f), 0.0f, 1.0f) * static_cast<float>(storage_most_from())));
	}
	int slot_at(float x, float y) const {
		for (const int k : shown_slots()) {
			if (slot_rect(k).has(x, y)) {
				return k;
			}
		}
		return 0;
	}
	Rect goal_button(int k) const { return Rect{ slot_rect(k).x + 20, slot_rect(k).y + 4, 58, 22 }; }
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
			for (int k = 1; k <= storage_count(); ++k) {
				bool held = false;
				for (const ItemView &v : view.items) {
					held = held || (v.place == "outbox" && v.slot == k);
				}
				if (!held && (goal_ok || goal_of(k) == nullptr)) {
					return k;
				}
			}
		}
		return storage_count();
	}
	bool goal_press(float x, float y) {
		for (const int k : shown_slots()) {
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
		return !(w == "celsius" || w == "liquid_ml" || w == "gas_ml" || w == "ph" || w == "us_cm");
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
	static std::string controller_state(const Part &p) { return !p.watching ? "unset" : !p.tuned ? (p.trying ? "trying" : "untuned") : text_of("%.0f%%", 100.0 * p.level); }

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
		for (int i = static_cast<int>(view.items.size()) - 1; i >= 0; --i) {
			if (plunger_grab(i, x, y)) { // (Stage 79) a syringe's plunger, by its handle: the hand holds it where it is
				if (guided() && !asks_drag_from("plunger:" + id(i))) {
					not_now();
					return true;
				}
				Rect h;
				plunger_handle(i, view.items[static_cast<size_t>(i)].plunger_ml, h);
				plunging = i;
				plunge_dy = h.cy() - y;
				act({ { "do", "plunger" }, { "item", id(i) }, { "ml", view.items[static_cast<size_t>(i)].plunger_ml } });
				return true;
			}
		}
		for (int i = static_cast<int>(view.items.size()) - 1; i >= 0; --i) {
			if (frame_grab(i, x, y)) { // a thermal camera's frame, by its handle (Stage 48)
				if (guided() && !asks_drag_from("frame:" + id(i))) {
					not_now();
					return true;
				}
				const Rect f = camera_frame(i);
				framing = i;
				frame_in_hand = false;
				frame_dx = x - f.x;
				frame_dy = y - f.y;
				return true;
			}
			if (probe_grab(i, x, y)) { // a spectral viewer's probe (Stage 42)
				if (guided() && !asks_drag_from("probe:" + id(i))) {
					not_now();
					return true;
				}
				probing = i;
				probe_in_hand = false;
				return true;
			}
		}
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
			if (outlet_rect(i, r) && Rect{ r.x - 4, r.y - 4, r.w + 8, r.h + 8 }.has(x, y)) {
				if (guided() && !asks_drag_from("outlet:" + id(i))) {
					not_now();
					return true;
				}
				draining = i;
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
		const std::vector<std::string> before = ids_now();
		if (!opened.empty() && circuit_panel().has(x, y)) {
			if (inner_square(x, y, inner_col, inner_row) &&
					act({ { "do", "part" }, { "kind", DRAWN[kind] }, { "at", { inner_col, inner_row } }, { "in", opened } })) {
				const int made = newest(before);
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
			const int made = newest(before);
			choose_only(made);
			edit_part(made);
			if (running() && senses(view.items[static_cast<size_t>(made)].part.kind)) {
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
	// An outlet's line let go (Stage 34): on a vessel, the outlet runs into it (taken first from what it ran into);
	// on nothing, the line comes off.
	void drop_draining(int source, float x, float y) {
		const int target = item_at(x, y, source, true), now = view.items[static_cast<size_t>(source)].drains_to;
		const std::string outlet_of = id(source);
		if (target < 0) {
			if (now >= 0 && !guided()) {
				act({ { "do", "undrain" }, { "of", outlet_of } });
			}
			return;
		}
		if (!guided_signal_drop("outlet:" + outlet_of, target, x, y)) {
			return;
		}
		const ItemView t = view.items[static_cast<size_t>(target)];
		if (t.kind != "vessel" || t.condenser) {
			message = "an outlet runs into a vessel";
			return;
		}
		if (now == target || (now >= 0 && !act({ { "do", "undrain" }, { "of", outlet_of } }))) {
			return;
		}
		act({ { "do", "under" }, { "item", t.id }, { "of", outlet_of } });
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
		if (type_menu(key, codepoint)) {
			return; // the experiments' search, while their list is open (Stage 78)
		}
		if (type_search(key, codepoint)) {
			return; // a search's field (Stage 61)
		}
		if (type_predict(key, codepoint) || type_report(key, codepoint)) {
			return; // a prediction's field (Stage 55), the report's (Stage 54)
		}
		if (type_plan(key, codepoint)) {
			return; // the planner's fields (Stage 60)
		}
		if (type_calc(key, codepoint)) {
			return; // the calculator's fields (Stage 58)
		}
		if (type_note(key, codepoint)) {
			return; // the notebook's write row (Stage 55)
		}
		if (panel && draft.edit == Draft::None) {
			type_amount(key, codepoint); // the pour menu's amount (Stage 50)
			return;
		}
		if (draft.edit == Draft::None && (key == Ui::KeyBackspace || key == Ui::KeyUndo)) {
			// Nothing being typed (Stage 34): Backspace puts what is in hand, or what is chosen, in the trash; the
			// undo key brings back what was last put there. A tutorial's step asks for neither.
			if (guided() || panel || making >= 0) {
				if (guided()) {
					not_now();
				}
				return;
			}
			if (key == Ui::KeyUndo) {
				const int top = trash_top();
				if (top < 0) {
					message = "the trash is empty";
					return;
				}
				const std::string back = id(top);
				if (act({ { "do", "restore" } })) {
					choose_only(item_index(back));
				}
				return;
			}
			std::vector<std::string> away;
			if (carrying && drag >= 0) {
				away.push_back(id(drag));
				stop_pouring();
				carrying = false;
				drag = -1;
			} else {
				for (const int i : group) {
					away.push_back(id(i));
				}
			}
			for (const std::string &one : away) {
				const int i = item_index(one);
				if (i >= 0 && view.items[static_cast<size_t>(i)].place != "trash") {
					act({ { "do", "trash" }, { "item", one } });
				}
			}
			if (!away.empty()) {
				choose_only(-1);
			}
			return;
		}
		if (draft.edit == Draft::None) {
			if (key == Ui::KeyEscape && probing >= 0) {
				probing = -1; // a probe in hand goes back where it was (Stage 42)
				probe_in_hand = false;
				return;
			}
			if (key == Ui::KeyEscape && framing >= 0) {
				framing = -1; // a camera's frame in hand goes back where it was (Stage 48)
				frame_in_hand = false;
				return;
			}
			if (key == Ui::KeyEscape && making >= 0) {
				making = -1; // a part in hand goes back in the drawer
				making_carried = false;
				return;
			}
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
		amount.clear(); // (the slider's amount, not what was typed: Stage 50)
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
		} else if (view.items[static_cast<size_t>(valve_drag)].kind == "spectrophotometer") {
			act({ { "do", "tune" }, { "spectrophotometer", id(valve_drag) }, { "nm", nm_at(open) } }); // (Stage 55)
		} else {
			act({ { "do", "cap" }, { "item", id(valve_drag) }, { "to", open } });
		}
	}

	// ------------------------------------------------------------- painting

	// (each placed by the view where it is the room's: room_paint; Stage 42)
	float pw(float width) const { return room_paint == 1 ? width * vw.scale : width; }
	void rect_fill(const Rect &r, uint32_t color) {
		const Rect s = room_paint != 0 ? on_screen(r) : r;
		list.commands.push_back(DrawCommand{ DrawCommand::Rect, color, 1, 0, 0, s.x, s.y, s.w, s.h });
	}
	// (Stage 61) a picture over a rectangle, as the screen paints it: no part of the room (the pass never sees it)
	void image(const Rect &r, const Picture &p) {
		const Rect s = room_paint != 0 ? on_screen(r) : r;
		DrawCommand c{ DrawCommand::Image, 0xffffffff, 1, static_cast<int32_t>(list.images.size()), 0, s.x, s.y, s.w, s.h };
		list.commands.push_back(c);
		list.images.push_back(DrawImage{ p.w, p.h, p.rgb.data(), p.hash });
	}
	void outline(const Rect &r, uint32_t color, float width = 2) {
		const Rect s = room_paint != 0 ? on_screen(r) : r;
		list.commands.push_back(DrawCommand{ DrawCommand::Outline, color, pw(width), 0, 0, s.x, s.y, s.w, s.h });
	}
	void line(float x1, float y1, float x2, float y2, uint32_t color, float width = 2) {
		if (room_paint != 0) {
			x1 = sx(x1);
			y1 = sy(y1);
			x2 = sx(x2);
			y2 = sy(y2);
		}
		list.commands.push_back(DrawCommand{ DrawCommand::Line, color, pw(width), 0, 0, x1, y1, x2, y2 });
	}
	void circle(float x, float y, float r, uint32_t color) {
		if (room_paint != 0) {
			x = sx(x);
			y = sy(y);
			r = room_paint == 1 ? r * vw.scale : r;
		}
		list.commands.push_back(DrawCommand{ DrawCommand::Circle, color, 1, 0, 0, x, y, r, 0 });
	}
	void polygon(std::initializer_list<float> xy, uint32_t color) {
		const auto first = static_cast<int32_t>(list.points.size() / 2);
		list.points.insert(list.points.end(), xy.begin(), xy.end());
		if (room_paint != 0) {
			for (size_t k = static_cast<size_t>(first) * 2; k < list.points.size(); k += 2) {
				list.points[k] = sx(list.points[k]);
				list.points[k + 1] = sy(list.points[k + 1]);
			}
		}
		list.commands.push_back(DrawCommand{ DrawCommand::Polygon, color, 1, first, static_cast<int32_t>(xy.size() / 2), 0, 0, 0, 0 });
	}
	void text(float x, float y, const std::string &s, uint32_t color = INK, float size = 18) {
		const auto offset = static_cast<int32_t>(list.text.size());
		list.text += s;
		list.text += '\0';
		if (room_paint != 0) {
			x = sx(x);
			y = sy(y);
		}
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
		if (draining >= 0 && outlet_rect(draining, port)) {
			line(port.cx(), port.cy(), px, py, rgba(70, 120, 190, 230), 2);
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
		for (const int k : shown_slots()) {
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
		text(DRAWER.x + 14, DRAWER.y + 26, "parts: press one, then where it goes", INK, 17);
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
		const std::string label = v.kind == "burner" || v.kind == "viewer" || v.kind == "thermal" ? v.id : v.id + text_of("  %.0f C", v.celsius);
		if (index == drag) {
			return;
		}
		// on the bench a name is a label the screen writes over the pass: only for what is chosen (Stage 22). The
		// cabinet and the outbox are the screen's own panels, and name what they hold
		if ((v.place == "on" || v.place == "under" || v.place == "bench" || v.place == "in") && !chosen(index)) {
			return;
		}
		if (v.place == "in") { // (Stage 71) standing in a vessel: beside its top
			text(r.x + r.w + 6, r.y + 16, label, WALL_INK, 14);
		} else if (v.place == "on") {
			text(r.x, r.y - 8, label, WALL_INK, 15);
		} else if (v.place == "under") {
			const Rect c = rect(index);
			text(c.x, c.y + c.h + 18, label, WALL_INK, 14);
		} else if (v.place == "bench" && v.inverted && v.pours_into >= 0) {
			text(r.x + r.w + 6, r.y + 16, label, WALL_INK, 14); // upside down over another: beside it, where it is (Stage 27)
		} else if (v.place == "bench") {
			const Rect c = foot(v);
			text(c.x + 2, c.y + c.h - 10, label, WALL_INK, 14);
		} else if (v.place == "cabinet") {
			const std::optional<Rect> box = cabinet_box(slot(index, "cabinet"));
			if (box) {
				text(box->x + 2, box->y + box->h + 16, label, WALL_INK, 13); // under its box (Stage 48)
			}
		} else {
			text(r.x, r.y + r.h + 18, label, WALL_INK, 15);
		}
	}

	void paint() {
		list.commands.clear();
		list.points.clear();
		list.text.clear();
		list.images.clear();
		list.above = 0;
		room_paint = 0;
		rect_fill(Rect{ 0, 0, 1920, 1080 }, BACKGROUND);
		room_paint = 1; // the room's paint, as the view lays it out (Stage 42)
		rect_fill(ROOM, WALL);
		// the bench and its grid
		// (each line a pixel's strip, on the pixel that a line a pixel wide on its whole coordinate is painted on: so
		// the room seen closer is the same ruling larger, not one moved by half a pixel; Stage 42)
		const float sq = square();
		const auto ruled = [&](float x0, float y0, float x1, float y1) { rect_fill(Rect{ x0 - 1, y0 - 1, std::max(x1 - x0, 0.0f) + 1, std::max(y1 - y0, 0.0f) + 1 }, WALL_GRID); };
		for (int c = 0; c <= view.cols; ++c) {
			ruled(BENCH.x + static_cast<float>(c) * sq, BENCH.y, BENCH.x + static_cast<float>(c) * sq, BENCH.y + static_cast<float>(view.rows) * sq);
		}
		for (int r = 0; r <= view.rows; ++r) {
			ruled(BENCH.x, BENCH.y + static_cast<float>(r) * sq, BENCH.x + static_cast<float>(view.cols) * sq, BENCH.y + static_cast<float>(r) * sq);
		}
		// (Stage 61) the supplies' pictures, under the pass: the screen's own, never in the way of a ray (what is held
		// over them is drawn by the pass, over them, seeing the room's wall)
		for (int i = supply_base; i < static_cast<int>(view.items.size()); ++i) {
			const auto pic = pictures.find(picture_key(view.items[static_cast<size_t>(i)]));
			const std::optional<Rect> box = pic != pictures.end() && capture_only.empty() ? cabinet_box(slot(i, "cabinet")) : std::nullopt;
			if (box) {
				image(*box, pic->second);
			}
		}
		// ---- the optical pass goes here: what is in the vessels and the balloons
		list.above = static_cast<int32_t>(list.commands.size());
		room_paint = 2; // over the room: its names and highlights, placed by the view
		// (Stage 61) the panels' outlines over it: the panels are the screen's, no part of the wall seen through glass
		for (const Rect &box : { CABINET, OUTBOX }) {
			ruled(box.x, box.y, box.x + box.w, box.y);
			ruled(box.x, box.y + box.h, box.x + box.w, box.y + box.h);
			ruled(box.x, box.y, box.x, box.y + box.h);
			ruled(box.x + box.w, box.y, box.x + box.w, box.y + box.h);
		}
		text(CABINET.x + 16, CABINET.y + 24, "supplies", WALL_INK); // (Stage 61: never runs out)
		paint_search(1);
		if (cabinet_most_from() > 0) {
			// its scroll bar (Stage 48): where its rows on the screen are among all of them
			rect_fill(cabinet_track(), WALL_GRID);
			rect_fill(cabinet_thumb(), cabinet_scrolling ? INK : WALL_INK);
		}
		// the trash (Stage 34): what was put away last stands in it, and may be taken up again
		outline(TRASH, WALL_GRID, 1);
		text(TRASH.x + 8, TRASH.y + 20, view.trash.empty() ? std::string("trash") : text_of("trash (%.0f)", static_cast<double>(view.trash.size())), WALL_INK);
		if (!view.trash.empty()) {
			button(empty_rect(), "empty", false, 13);
		}
		if (view.trash.empty()) {
			text(TRASH.x + 8, TRASH.y + 44, "Backspace puts what is chosen here;", WALL_INK, 12);
			text(TRASH.x + 8, TRASH.y + 60, "Ctrl+Z brings the last back", WALL_INK, 12);
		}
		text(OUTBOX.x + 16, OUTBOX.y + 26, "storage: set aside, or handed in to a goal", WALL_INK); // (Stage 61: the outbox)
		paint_search(2);
		paint_slots();
		if (storage_most_from() > 0) {
			rect_fill(storage_track(), WALL_GRID);
			rect_fill(storage_thumb(), storage_scrolling ? INK : WALL_INK);
		}
		paint_room_overlays();
		room_paint = 0;
		if (zoomed()) {
			// the room seen closer: what of it lies past its own part of the screen is not shown (Stage 42)
			rect_fill(Rect{ 0, 0, 1920, ROOM.y }, BACKGROUND);
			rect_fill(Rect{ ROOM.x + ROOM.w, ROOM.y, 1920 - ROOM.x - ROOM.w, 1080 - ROOM.y }, BACKGROUND);
			rect_fill(DRAWER, BACKGROUND);
		}
		paint_screen_own();
	}

	// what is painted over the room (its names, highlights, the signals' lines), placed by the view
	void paint_room_overlays() {
		// the items, those in hand last
		for (int i = 0; i < static_cast<int>(view.items.size()); ++i) {
			if (view.items[static_cast<size_t>(i)].kind == "tray" || i == drag || view.items[static_cast<size_t>(i)].place == "circuit" || hidden(i)) {
				continue;
			}
			paint_item(i, drawn(i));
			// what stands under a condenser's outlet is joined to it by a drip (Stage 59: a condenser's alone; a burette, a
			// pipette or a funnel delivers from its tip, in the mouth under it)
			const ItemView &v = view.items[static_cast<size_t>(i)];
			if (v.drains_to >= 0 && v.condenser) {
				const Rect a = rect(i), b = rect(v.drains_to);
				line(a.x + a.w + 26, a.y + a.h + 14, b.cx(), b.y, rgba(70, 120, 190, 200), 1);
			}
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
		paint_signals();
	}

	// the screen's own: the clock, an open circuit, the tutorial, the readout, the drawer, the panels
	void paint_screen_own() {
		button(pause_rect(), paused ? "play" : "pause", paused);
		for (int i = 0; i < 4; ++i) {
			button(speed_rect(i), text_of("%.0fx", SPEEDS[i]), !paused && speed == SPEEDS[i]);
		}
		button(check_rect(), check_every_step ? "check every step: on" : "check every step: off", check_every_step);
		button(reset_rect(), "reset");
		if (zoomed()) {
			button(view_rect(), text_of("%.1fx: whole", vw.scale), false, 15); // back to all of the room (Stage 42)
		}
		const double seconds = static_cast<double>(view.step) / 60.0;
		text(1120, 40, text_of("%02.0f:%04.1f", std::floor(seconds / 60.0), seconds - 60.0 * std::floor(seconds / 60.0)), INK, 22);
		// where the frame's budget holds the clock back (Stage 36): the speed it keeps
		if (budget_ms > 0.0 && !paused && achieved < 0.9 * speed) {
			text(1220, 40, text_of("keeping %.0fx", achieved), WALL_INK, 14);
		}
		if (!opened.empty()) {
			paint_circuit();
		}
		if (tutorial) {
			paint_tutorial();
		}
		paint_readout();
		paint_drawer();
		paint_notices();
		paint_notebook();
		paint_debug();
		if (panel) {
			paint_panel();
		}
		if (draft.edit != Draft::None) {
			paint_editor();
		}
		paint_menu();
		if (tutorial && !menu) {
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
		if (after("outlet:", rest)) {
			Rect r;
			return outlet_rect(item_index(std::string(rest)), r) && found(r);
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
			return k >= 1 && k <= storage_count() && found(slot_rect(k));
		}
		if (after("goal:", rest)) {
			const int k = number(rest);
			const Goal *g = k >= 1 && k <= storage_count() ? goal_of(k) : nullptr;
			return k >= 1 && k <= storage_count() && draft.edit == Draft::None && (g == nullptr || !g->game) && found(goal_button(k));
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
		button(strict_rect(), strict ? "strict: on" : "strict: off (free)", strict, 15);
		float y = TUTOR.y + 56;
		const auto lines = [&](const std::string &s, size_t width, uint32_t color, float size, float gap) {
			for (const std::string &l : wrapped(s, width)) {
				text(x, y, l, color, size);
				y += size + gap;
			}
		};
		text(x, y, t.part, DIM, 15);
		const int shown_step = t.step < t.steps ? t.step + 1 : t.steps;
		text(right - 110, y, text_of("step %.0f of %.0f", shown_step, t.steps), DIM, 15);
		text(x, TUTOR.y + 30, strict ? "Only what the step asks is taken" : "Free: anything is taken; the outline points", DIM, 13);
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
			text(x, y + 14,
					!strict ? (paused ? "Free: the clock is yours, and paused." : "Free: the clock is yours.")
					: !tutorial->waiting() ? "The clock waits for you."
					: paused ? "The clock is paused: press play, or a speed."
					: "The clock runs: 60x or 600x hurries it.",
					WARN, 15);
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
			r = Rect{ CABINET.x, CABINET.y, CABINET.w, TRASH.y - CABINET.y - 8 }; // (over the trash)
			return true;
		}
		if (what == "search:supplies" || what == "search:storage") { // (Stage 61)
			r = search_rect(what == "search:supplies" ? 1 : 2);
			return true;
		}
		if (what == "trash") {
			r = TRASH;
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
		if (what.substr(0, 6) != "valve:" && what.substr(0, 13) != "panel:slider@" && what.find('@') == std::string::npos) {
			std::vector<Rect> buttons{ pause_rect(), check_rect(), reset_rect(), notebook_button() };
			// (Stage 54, 55) the notebook's, the readout's "+", a spectrophotometer's, a prediction's and the report's
			if (notebook_open) {
				for (const char *n : { "close", "erase", "x", "tx", "y", "ty", "fit", "log:1", "log:5", "log:30", "unlog", "write:label", "write:value", "write" }) {
					buttons.push_back(notebook_rect(n));
				}
				// (Stage 58) its tabs, the calculator's kinds, arrows, note and fields, the reference's pages
				for (const char *n : TABS) {
					buttons.push_back(notebook_rect(n));
				}
				for (const char *n : KINDS) {
					buttons.push_back(notebook_rect(std::string("calc:") + n));
				}
				for (const char *n : PAGES) {
					buttons.push_back(notebook_rect(std::string("ref:") + n));
				}
				for (const char *n : { "plan:target", "plan:grams", "plan:purity", "plan:add", "plan:remove", "plan:goal" }) {
					buttons.push_back(notebook_rect(n));
				}
				for (size_t k = 0; k < plan_reactions.size(); ++k) {
					for (const char *n : { "plan:prev:", "plan:next:", "plan:yield:" }) {
						buttons.push_back(notebook_rect(n + std::to_string(k)));
					}
				}
				for (const char *n : { "calc:prev", "calc:next", "calc:note" }) {
					buttons.push_back(notebook_rect(n));
				}
				for (int k = 0; k < 8; ++k) {
					buttons.push_back(notebook_rect("calc:field:" + std::to_string(k)));
				}
			}
			for (const auto &[w, b] : record_buttons()) {
				buttons.push_back(b);
			}
			if (predict_shown()) {
				for (size_t n = 0; n < predicted_items().size(); ++n) {
					buttons.push_back(guess_field(static_cast<int>(n)));
				}
				buttons.push_back(guess_make());
			}
			if (report_active()) {
				for (size_t k = 0; k < tutorial->report().size(); ++k) {
					buttons.push_back(report_field(static_cast<int>(k)));
				}
				buttons.push_back(report_check());
			}
			for (int i = 0; i < 4; ++i) {
				buttons.push_back(speed_rect(i));
			}
			if (panel) {
				buttons.push_back(pour_button());
				buttons.push_back(gentle_button());
				buttons.push_back(cancel_button());
				if (could_stand()) {
					buttons.push_back(stand_button()); // (Stage 71)
				}
				if (could_join()) {
					buttons.push_back(join_button()); // (Stage 79)
				}
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
				if (lock_rect(i, b)) {
					buttons.push_back(b);
				}
				for (const auto &[n, sb] : spectro_buttons(i)) {
					buttons.push_back(sb);
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
			// where the view shows them (Stage 42)
			a = room_name(g->what) ? on_screen(a) : a;
			b = g->drag && room_name(g->to) ? on_screen(b) : b;
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
			const std::string who = i >= supply_base ? std::string(supply_name(v.id)) : v.id; // (Stage 61: a supply's box apart from its first copy)
			if (v.kind == "stopper") { // rubber (Stage 28), opaque, lit by the room
				thing(who, "body", OpticsBody::Slab, r, 4.0f, "rubber", nullptr, in_hand);
			} else if (v.kind == "vessel" || v.kind == "balloon") {
				OpticsBody body{ &v, r.x, r.y, r.w, r.h };
				body.layer = in_hand || v.place == "in" ? 1 : 0; // (Stage 71: standing in a vessel, drawn before it: Open)
				body.named = i >= supply_base ? supply_name(v.id) : nullptr; // (Stage 61: a supply's box is not its first copy)
				body.flipped = v.inverted && v.pours_into >= 0; // upside down (Stage 27)
				if (turned(i, r, body.angle, body.pivot_x, body.pivot_y)) { // pouring by hand: about its lip (Stage 31)
					body.side = side_of(i);
				}
				body.vortex = static_cast<float>(v.vortex); // a stirred liquid's (Stage 32)
				bodies.push_back(body);
				const glass::Vessel *made = glass_of(v);
				if (v.screwed && made != nullptr && made->syringe) { // (Stage 79) a syringe's nozzle capped, at its foot
					thing(who, "cap", OpticsBody::Slab, Rect{ r.cx() - 6, r.y + r.h - 4, 12, 9 }, 3.0f, "cap_plastic", nullptr, in_hand);
				} else if (v.screwed) { // its screw cap on (Stage 28)
					const float mouth = mouth_px(i) + 8;
					thing(who, "cap", OpticsBody::Slab, Rect{ r.cx() - mouth / 2, r.y - 8, mouth, 12 }, 3.0f, "cap_plastic", nullptr, in_hand);
				}
				if (made != nullptr && made->syringe && !body.flipped && body.angle == 0.0f) {
					// (Stage 79) its plunger: a rod of ground glass in its bore from the gas's top up out of the barrel, its
					// handle across its top
					const float px_cm = r.h / static_cast<float>(made->height);
					const float face = plunger_face(r, *made, v.plunger_ml), bore = static_cast<float>(0.5 * made->diameter - made->wall) * 2.0f * px_cm;
					const Rect handle = handle_of(r, *made, v.plunger_ml);
					thing(who, "plunger", OpticsBody::Slab, Rect{ r.cx() - bore / 2 + 0.5f, handle.y + handle.h, bore - 1.0f, face - handle.y - handle.h }, 3.0f, "ground_glass", nullptr, in_hand);
					thing(who, "handle", OpticsBody::Slab, handle, 3.0f, "ground_glass", nullptr, in_hand);
				}
				if (v.condenser) {
					bar(who, "outlet", r.x + r.w, r.y + r.h - 10, r.x + r.w + 26, r.y + r.h + 14, 2.0f, "outlet_glass", in_hand);
				}
				if (const glass::Vessel *g = glass_of(v); g != nullptr && (g->outlet || g->mark) && !body.flipped && body.angle == 0.0f) {
					// (Stage 52) a burette's stopcock, a key across it; a mark etched round its neck, at its true volume
					const float px_cm = r.h / static_cast<float>(g->height);
					if (v.glassware == "burette") {
						const float y = r.y + r.h - static_cast<float>(0.1 + 0.075 * g->height) * px_cm;
						thing(who, "stopcock", OpticsBody::Slab, Rect{ r.cx() - 2.5f * r.w, y - 3.0f, 5.0f * r.w, 6.0f }, 2.0f, "cap_plastic", nullptr, in_hand);
					}
					if (g->mark && v.mark_ml) {
						const float y = r.y + r.h - static_cast<float>(g->level(*v.mark_ml)) * px_cm, half = static_cast<float>(g->radius_inside(g->level(*v.mark_ml))) * px_cm + 2.0f;
						bar(who, "mark", r.cx() - half, y, r.cx() + half, y, 1.0f, "lcd_black", in_hand);
					}
				}
			} else if (v.kind == "thermal") {
				// a thermal camera (Stage 48): its case and its screen, hidden from its own rays; on the bench (switched on)
				// its screen shows what its frame sees, and its frame, away from it, is four thin bars; its handle's tab
				const float s = r.w / CAMERA_W;
				const uint32_t bit = camera_bit(i);
				const bool on = v.place == "bench";
				bodies[thing(who, "case", OpticsBody::Slab, r, 6.0f * s, "viewer_case", nullptr, in_hand)].hidden = bit;
				const size_t k = thing(who, "screen", on ? OpticsBody::Sensor : OpticsBody::Slab, camera_screen(r), 1.0f * s, "lcd_black", nullptr, in_hand);
				bodies[k].hidden = bit;
				if (on) {
					camera_relay(i);
					bodies[k].relay = &cameras[v.id].relay;
					const Rect f = camera_frame(i), tab = frame_handle(i);
					const bool away = v.thermal.frame_room || framing == i;
					if (away) {
						const float x0 = f.x - 1, y0 = f.y - 1, x1 = f.x + f.w + 1, y1 = f.y + f.h + 1;
						for (const auto &[a, b, c, d] : { std::array<float, 4>{ x0, y0, x1, y0 }, { x1, y0, x1, y1 }, { x1, y1, x0, y1 }, { x0, y1, x0, y0 } }) {
							bar(who, "frame", a, b, c, d, 2.0f, "rig", framing == i);
							bodies.back().hidden = bit;
						}
					}
					bodies[thing(who, "tab", OpticsBody::Slab, tab, 2.0f, "rig", nullptr, framing == i)].hidden = bit;
				}
			} else if (v.kind == "viewer") {
				// the spectral viewer (Stage 42): its case, its screen showing its picture, its probe's holder
				const float s = r.w / VIEWER_W;
				thing(who, "case", OpticsBody::Slab, viewer_case(r), 6.0f * s, "viewer_case", nullptr, in_hand);
				ViewerState &st = viewers[v.id];
				if (!st.plotted) {
					plot_spectrum({}, 0, st.picture);
					st.plotted = true;
				}
				// (off the bench it is switched off: its screen dark, its probe reading nothing)
				static const Picture OFF{};
				const Picture &shows = v.place == "bench" ? st.picture : OFF;
				const size_t k = thing(who, "screen", OpticsBody::Screen, viewer_screen(r), 1.0f * s, "lcd_black", nullptr, in_hand);
				bodies[k].image = shows.rgb.data();
				bodies[k].image_w = shows.w;
				bodies[k].image_h = shows.h;
				bodies[k].image_hash = shows.hash;
				thing(who, "holder", OpticsBody::Slab, viewer_holder(r), 4.0f * s, "burner_metal", nullptr, in_hand);
			} else if (v.kind == "balance") {
				// a balance (Stage 50): its case, its pan of steel, its display showing its reading (dark off the bench)
				const float s = r.w / BALANCE_W;
				thing(who, "case", OpticsBody::Slab, balance_case(r), 6.0f * s, "viewer_case", nullptr, in_hand);
				thing(who, "pan", OpticsBody::Slab, balance_pan(r), 2.0f * s, "burner_metal", nullptr, in_hand);
				const instruments::Balance *model = instruments::balance(v.model);
				Picture &shows = balances[v.id];
				const std::string says = model != nullptr ? instruments::display_text(*model, v.display_g, v.overload) : std::string();
				instruments::plot_display(says, shows);
				const size_t k = thing(who, "display", OpticsBody::Screen, balance_display(r), 1.0f * s, "lcd_black", nullptr, in_hand);
				bodies[k].image = shows.rgb.data();
				bodies[k].image_w = shows.w;
				bodies[k].image_h = shows.h;
				bodies[k].image_hash = shows.hash;
			} else if (v.kind == "spectrophotometer") {
				// a spectrophotometer (Stage 55): its case, its well, its two displays (dark off the bench)
				const float s = r.w / SPECTRO_W;
				thing(who, "case", OpticsBody::Slab, spectro_case(r), 8.0f * s, "viewer_case", nullptr, in_hand);
				thing(who, "well", OpticsBody::Slab, spectro_well(r), 3.0f * s, "lcd_black", nullptr, in_hand);
				const bool on = v.place == "bench" || v.place == "under";
				for (int d = 0; d < 2; ++d) {
					Picture &shows = spectro_shows[v.id + (d == 0 ? ":a" : ":nm")];
					instruments::plot_display(!on ? std::string() : d == 0 ? spectro::display_text(v.absorbance, v.blanked, v.overload) : spectro::wavelength_text(v.nm), shows);
					const size_t k = thing(who, d == 0 ? "display" : "nm", OpticsBody::Screen, d == 0 ? spectro_display(r) : spectro_nm_display(r), 1.0f * s, "lcd_black", nullptr, in_hand);
					bodies[k].image = shows.rgb.data();
					bodies[k].image_w = shows.w;
					bodies[k].image_h = shows.h;
					bodies[k].image_hash = shows.hash;
				}
			} else if (v.kind == "burner") {
				thing(who, "body", OpticsBody::Slab, r, 3.0f, "burner_metal", nullptr, in_hand);
				if (v.valve > 0.0) {
					// its flame: from a sooty yellow, barely open, to a blue, wide open
					const float flame = static_cast<float>(12.0 + 30.0 * v.valve);
					const size_t k = thing(who, "flame", OpticsBody::Cone, Rect{ r.cx() - 12, r.y - flame, 24, flame }, 0.0f, nullptr, "flame_sooty", in_hand);
					bodies[k].light_to = "flame_blue";
					bodies[k].toward = v.valve;
					bodies[k].kelvin = v.celsius + 273.15; // its flame's temperature (Stage 46)
				}
			} else if (is_part_kind(v.kind) && (v.place == "bench" || (v.place == "trash" && i == trash_top()))) {
				const bool circuit = v.kind == "circuit";
				bool ringing = v.kind == "alarm" && v.part.ringing, lit = v.part.out;
				if (circuit) {
					lit = circuit_out(v.id);
					for (const ItemView &o : view.items) {
						ringing = ringing || (o.kind == "alarm" && o.part.ringing && holds(v.id, o.id));
					}
				}
				thing(who, "face", OpticsBody::Slab, r, 4.0f, face_of(v.kind), nullptr, in_hand);
				if (v.kind == "timer") {
					// its time left: a strip of light along its foot, as long as the share of its time that is left
					const Rect strip{ r.x + 5, r.y + r.h - 9, r.w - 10, 4 };
					thing(who, "strip", OpticsBody::Slab, strip, 2.0f, "lamp_glass", nullptr, in_hand);
					const double left = v.part.preset > 0 ? static_cast<double>(v.part.left) / static_cast<double>(v.part.preset) : 0.0;
					if (left > 0.0) {
						thing(who, "time", OpticsBody::Slab, Rect{ strip.x, strip.y, strip.w * static_cast<float>(std::min(left, 1.0)), strip.h }, 2.0f, "lamp_glass", "lamp_green", in_hand);
					}
				}
				thing(who, "lamp", OpticsBody::Slab, Rect{ r.x + r.w - 12, r.y + 4, 8, 8 }, 4.0f, "lamp_glass", ringing ? "lamp_amber" : lit ? "lamp_green" : nullptr, in_hand);
				Rect port;
				if (port_rect(i, port)) {
					thing(who, "port", OpticsBody::Slab, Rect{ port.cx() - 5, port.cy() - 5, 10, 10 }, 5.0f, "wire", nullptr, in_hand);
				}
				if (sense_rect(i, port)) {
					thing(who, "sense port", OpticsBody::Slab, Rect{ port.cx() - 4, port.cy() - 4, 8, 8 }, 4.0f, "sense", nullptr, in_hand);
				}
			}
		};
		for (int i = 0; i < static_cast<int>(view.items.size()); ++i) {
			if (i != drag && !hidden(i)) {
				// (Stage 61) a supply's box: its picture, captured ahead, shown by a screen of the box's size; drawn by the
				// pass only until it has one (and while its own is being captured)
				// (Stage 61) a supply's box with its picture: painted by the screen under the pass (paint), no body of it
				if (i >= supply_base && capture_only.empty() && pictures.find(picture_key(view.items[static_cast<size_t>(i)])) != pictures.end()) {
					continue;
				}
				const size_t first = bodies.size();
				add(i, drawn(i), carrying_group() && chosen(i) && view.items[static_cast<size_t>(i)].place == "bench");
				const std::string &place = view.items[static_cast<size_t>(i)].place;
				for (size_t k = first; (place == "cabinet" || place == "outbox" || place == "trash") && k < bodies.size(); ++k) {
					bodies[k].hidden |= PANEL_BIT; // (Stage 61: the panels' own: what is in hand over them does not see them)
				}
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
		// The viewers' probes (Stage 42): each a ring about the disc it reads, a handle, and a cable sagging from the
		// handle to where it leaves its viewer; all of the pass, and none of them in the probes' own reading
		const size_t before_probes = bodies.size();
		for (int i = 0; i < static_cast<int>(view.items.size()); ++i) {
			const ItemView &v = view.items[static_cast<size_t>(i)];
			if (v.kind != "viewer" || hidden(i) || !visible(i)) {
				continue;
			}
			float hx = 0, hy = 0, ox = 0, oy = 0;
			probe_point(i, hx, hy);
			viewer_port(drawn(i), ox, oy);
			int on = v.probe_on.empty() ? -1 : item_index(v.probe_on);
			const bool held = probing == i || (on >= 0 && (on == drag ? (moved || carrying) : carrying_group() && chosen(on))) || (i == drag && (moved || carrying));
			const float ring = static_cast<float>(PROBE_RADIUS_PX) + 1.5f;
			for (int k = 0; k < 6; ++k) {
				const float a0 = 6.2831853f * static_cast<float>(k) / 6.0f, a1 = 6.2831853f * static_cast<float>(k + 1) / 6.0f;
				bar(v.id, k == 0 ? "probe ring 0" : k == 1 ? "probe ring 1" : k == 2 ? "probe ring 2" : k == 3 ? "probe ring 3" : k == 4 ? "probe ring 4" : "probe ring 5", hx + ring * std::cos(a0), hy + ring * std::sin(a0),
						hx + ring * std::cos(a1), hy + ring * std::sin(a1), 1.5f, "probe_head", held);
			}
			const float ex = hx + ring + 12.0f, ey = hy - ring - 12.0f;
			bar(v.id, "probe handle", hx + ring * 0.7f, hy - ring * 0.7f, ex, ey, 4.0f, "probe_head", held);
			const float sag = 10.0f + 0.25f * std::hypot(ox - ex, oy - ey);
			static const char *const CABLE[] = { "probe cable 0", "probe cable 1", "probe cable 2", "probe cable 3", "probe cable 4", "probe cable 5" };
			for (int k = 0; k < 6; ++k) {
				const float t0 = static_cast<float>(k) / 6.0f, t1 = static_cast<float>(k + 1) / 6.0f;
				bar(v.id, CABLE[k], ex + (ox - ex) * t0, ey + (oy - ey) * t0 + sag * 4.0f * t0 * (1.0f - t0), ex + (ox - ex) * t1, ey + (oy - ey) * t1 + sag * 4.0f * t1 * (1.0f - t1), 3.0f,
						"probe_cable", held);
			}
		}
		const size_t after_probes = bodies.size();
		// A pour's stream (Stage 27; Stage 31): a column of what pours, from the lip or the mouth it leaves straight
		// down to where it lands, thinning as it falls faster: what passes a second is the same at every height,
		// so its radius there is sqrt(Q / (pi sqrt(v0^2 + 2 g y))). In short lengths, each as wide as it is there.
		streams.clear();
		streams.reserve(view.items.size() * static_cast<size_t>(STREAM_LENGTHS));
		for (int i = 0; i < static_cast<int>(view.items.size()); ++i) {
			const ItemView &v = view.items[static_cast<size_t>(i)];
			const double bulk = v.pours_solid_ml_s / contents::PACKING, runs = v.pours_ml_s + bulk;
			const glass::Vessel *made = v.glassware.empty() ? nullptr : glass::find(v.glassware, v.nominal_ml);
			if (v.pours_into < 0 || !(runs > 0.0) || made == nullptr) {
				continue;
			}
			const ItemView &t = view.items[static_cast<size_t>(v.pours_into)];
			const Rect from_r = drawn(i), to_r = drawn(v.pours_into);
			float land = to_r.y + to_r.h;
			if (const glass::Vessel *g = t.glassware.empty() ? nullptr : glass::find(t.glassware, t.nominal_ml); g != nullptr) {
				land -= static_cast<float>(g->level(t.condensed_ml) * to_r.w / (2.0 * g->widest()));
			}
			// where it leaves: an upside-down vessel's mouth; by hand, the lip, turned with the vessel
			const double px_per_cm = from_r.w / (2.0 * made->widest());
			float x = from_r.cx(), top = from_r.y + from_r.h;
			double leaves = 0.0; // its speed as it leaves, cm/s
			if (v.inverted) {
				const double bore = 0.5 * made->cap_bore * std::sqrt(std::max(v.cap, 1e-6) * 0.61);
				leaves = runs / (STREAM_PI * bore * bore);
			} else {
				float lx = 0, ly = 0, tx = 0, ty = 0, angle = 0, pivot_x = 0, pivot_y = 0;
				if (!lip_in(i, side_of(i), lx, ly, tx, ty) || !turned(i, from_r, angle, pivot_x, pivot_y)) {
					continue;
				}
				const float dx = lx - tx, dy = ly - ty;
				x = pivot_x + dx * std::cos(angle) - dy * std::sin(angle);
				top = pivot_y + dx * std::sin(angle) + dy * std::cos(angle);
				leaves = std::sqrt(2.0 * contents::GRAVITY * std::max({ v.head_cm, 0.05 }));
			}
			if (land <= top + 2.0f) {
				continue;
			}
			const bool powder = !(v.pours_ml_s > 0.0); // sediment alone: a column of it
			const int lengths = std::max(1, std::min(STREAM_LENGTHS, static_cast<int>((land - top) / 8.0f)));
			for (int k = 0; k < lengths; ++k) {
				const float y0 = top + (land - top) * static_cast<float>(k) / static_cast<float>(lengths), y1 = top + (land - top) * static_cast<float>(k + 1) / static_cast<float>(lengths);
				const double fallen = 0.5 * (y0 + y1 - 2.0f * top) / px_per_cm;
				const double radius = std::sqrt(runs / (STREAM_PI * std::sqrt(leaves * leaves + 2.0 * contents::GRAVITY * fallen)));
				const float width = static_cast<float>(std::max(2.0 * radius * px_per_cm, 1.0));
				ItemView stream;
				stream.id = v.id + "~stream" + (k > 0 ? std::to_string(k) : std::string());
				stream.kind = "vessel";
				stream.place = "bench";
				stream.celsius = v.celsius;
				// a column full of it, as thick as it is wide; what it holds as its vessel does (a solution's colour is
				// its concentration's, a suspension's haze its particles' in that much liquid)
				const double across = width / px_per_cm;
				stream.volume_ml = stream.condensed_ml = STREAM_PI * across * across / 4.0 * (y1 - y0) / px_per_cm;
				stream.liquid_ml = powder ? 0.0 : v.liquid_ml;
				// (with the liquid goes the share of the solids it holds up, a haze in it; sediment running with it too)
				const bool hazy = !powder && (v.pours_solid_ml_s > 0.0 || v.suspended > 0.0);
				stream.solid_ml = powder ? stream.volume_ml * 1000.0 : hazy ? v.solid_ml : 0.0;
				stream.suspended = hazy ? (v.pours_solid_ml_s > 0.0 ? 1.0 : v.suspended) : 0.0;
				for (const SubstanceView &c : v.contents) {
					if (!c.wall && (powder ? c.phase == 's' : c.phase == 'l' || c.phase == 'a' || (c.phase == 's' && hazy))) {
						stream.contents.push_back(c);
					}
				}
				streams.push_back(stream);
				OpticsBody body{ &streams.back(), x - width / 2.0f, y0, width, y1 - y0 };
				body.layer = i == drag && (moved || carrying) ? 1 : 0;
				bodies.push_back(body);
			}
		}
		// (Stage 79) a tube between two syringes' nozzles, hanging between them
		for (size_t i = 0; i < view.items.size(); ++i) {
			const ItemView &v = view.items[i];
			if (v.tube <= static_cast<int>(i) || v.tube >= static_cast<int>(view.items.size()) || hidden(static_cast<int>(i)) || hidden(v.tube)) {
				continue;
			}
			const Rect a = drawn(static_cast<int>(i)), b = drawn(v.tube);
			const float x0 = a.cx(), y0 = a.y + a.h, x1 = b.cx(), y1 = b.y + b.h;
			const float sag = 18.0f + 0.1f * std::fabs(x1 - x0);
			constexpr int PIECES = 8;
			for (int k = 0; k < PIECES; ++k) {
				const auto at = [&](int n, float &x, float &y) {
					const float t = static_cast<float>(n) / PIECES;
					x = x0 + (x1 - x0) * t;
					y = y0 + (y1 - y0) * t + 4.0f * sag * t * (1.0f - t);
				};
				float xa = 0, ya = 0, xb = 0, yb = 0;
				at(k, xa, ya);
				at(k + 1, xb, yb);
				bar(v.id + "~" + view.items[static_cast<size_t>(v.tube)].id, k == 0 ? "tube" : "tube+", xa, ya, xb, yb, 4.0f, "rubber", false);
				bodies.back().id ^= static_cast<size_t>(k) * 0x9e3779b97f4a7c15ull;
			}
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
		// (at the view: the room laid out anew, its depths with it, so the view is the same room larger: Stage 42)
		const auto room_now = [&]() {
		OpticsRoom room;
		const float scale = vw.scale;
		room.x = sx(ROOM.x);
		room.y = sy(ROOM.y);
		room.w = ROOM.w * scale;
		room.h = ROOM.h * scale;
		room.wall_px *= scale;
		room.gap_px *= scale;
		room.cube_x = sx(960.0f);
		room.cube_y = sy(540.0f);
		room.pixel = scale;
		room.pixel_x0 = sx(0.0f);
		room.pixel_y0 = sy(0.0f);
		room.bench = WALL_RADIANCE;
		room.elsewhere = DARK_RADIANCE;
		room.kelvin = view.room_celsius + 273.15; // Stage 46: what the long-wave range sees the room by
		room.range = infrared ? 1 : 0;
		// The wall behind and the room in front are endless white planes ruled as the bench is (the user's word,
		// Stage 33). Beyond the screen and in front they are seen only far off or turned about, so as one
		// radiance: the wall's, less what its ruling takes (a line a pixel wide each way in every square).
		{
			const auto lin = [](double v) { return v <= 0.04045 ? v / 12.92 : std::pow((v + 0.055) / 1.055, 2.4); };
			const double ruled = lin(((WALL_GRID >> 24) & 0xff) / 255.0), sq = std::max(static_cast<double>(square()), 2.0); // (the layout's: its ruling is as much larger as its squares)
			const double share = 2.0 / sq - 1.0 / (sq * sq);
			room.beyond = WALL_RADIANCE - share * (WALL_RADIANCE - ruled);
			// (Stage 78) and the faces ruled so, now that a mirror shows them: a line each way every square, its mean the
			// same `beyond`
			room.face_white = WALL_RADIANCE;
			room.face_ruled = ruled;
			room.face_pitch = sq * scale;
			room.face_line = scale;
			// The room is a cube twice the screen's width on a side, each face ruled in sixteen cells across, the
			// cells along its twelve edges black (the user's word: endless white planes left a vessel all but
			// unseen; rays bent far, as at a rim, come to the black cells). What glass reflects is the room as a
			// whole: its faces' mean.
			room.cube = 3840.0 * scale;
			room.cube_cell = room.cube / 16.0;
			room.edge = 0.0;
			const double black = 1.0 - (14.0 / 16.0) * (14.0 / 16.0);
			room.front = (1.0 - black) * room.beyond + black * room.edge;
		}
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
		room_drawn = room; // (Stage 61: as painted, for a supply's picture where no body is)
		{
			int k = 0;
			for (const Rect &box : { CABINET, OUTBOX }) { // (where the pass leaves the wall to the screen's drawing)
				const Rect s = on_screen(box);
				room.panels[k][0] = s.x;
				room.panels[k][1] = s.y;
				room.panels[k][2] = s.w;
				room.panels[k][3] = s.h;
				++k;
			}
		}
		// (Stage 61) behind glass the panels are the wall as the bench rules it: its grid continued over them
		{
			const double ruled_r = radiance(WALL_GRID);
			// (drawn behind glass only: bare, the panels are as the screen paints them)
			const auto glass = [&](double x0, double y0, double x1, double y1, double r) {
				const double left = std::ceil(x0 - 0.5), right = std::ceil(x1 - 0.5), top = std::ceil(y0 - 0.5), bottom = std::ceil(y1 - 0.5);
				if (right > left && bottom > top) {
					room.glass_shapes.push_back(OpticsShape{ static_cast<float>(left), static_cast<float>(top), static_cast<float>(right - left), static_cast<float>(bottom - top), r });
				}
			};
			const float sq = square();
			for (const Rect &box : { CABINET, OUTBOX }) {
				const float c0 = std::floor((box.x - BENCH.x) / sq), c1 = std::ceil((box.x + box.w - BENCH.x) / sq);
				for (float c = c0; c <= c1; c += 1.0f) {
					const float x = BENCH.x + c * sq;
					if (x >= box.x && x <= box.x + box.w) {
						glass(sx(x - 1.0f), sy(box.y), sx(x), sy(box.y + box.h), ruled_r);
					}
				}
				const float r0 = std::floor((box.y - BENCH.y) / sq), r1 = std::ceil((box.y + box.h - BENCH.y) / sq);
				for (float r = r0; r <= r1; r += 1.0f) {
					const float y = BENCH.y + r * sq;
					if (y >= box.y && y <= box.y + box.h) {
						glass(sx(box.x), sy(y - 1.0f), sx(box.x + box.w), sy(y), ruled_r);
					}
				}
			}
		}
		return room;
		};
		// The probes read the room as it is laid out, whatever the view (Stage 42); the screen shows it at the view
		OpticsRoom whole;
		if (zoomed()) {
			const View seen = vw;
			vw = view_whole(VIEW_ROOM);
			whole = room_now();
			vw = seen;
		}
		OpticsRoom room = room_now(); // (and the draw list, at the view)
		if (!zoomed()) {
			whole = room;
		}
		room_was = room; // (Stage 61: the wall a supply's picture is captured against)
		// while the view moves, the scene stands as it was made: the host carries its pixels to the view now
		if (!view_moving() || scene.table.empty()) {
			if (!zoomed()) {
				optics_compose(scene, room, bodies);
			} else {
				optics_compose(scene, room, at_view(bodies));
				clip_rectangles();
			}
			composed_at = vw;
		}
		read_probes(bodies, before_probes, after_probes, whole);
	}

	// The bodies at the view (Stage 42): placed and sized by it, their depths with them; every one of them, those past
	// the room's part of the screen too (Stage 46: the scene is the whole room at any view)
	std::vector<OpticsBody> at_view(const std::vector<OpticsBody> &bodies) const {
		relays_at_view.clear();
		std::vector<OpticsBody> out;
		out.reserve(bodies.size());
		const float scale = vw.scale;
		for (OpticsBody b : bodies) {
			b.x = sx(b.x);
			b.y = sy(b.y);
			b.w *= scale;
			b.h *= scale;
			b.x0 = sx(b.x0);
			b.y0 = sy(b.y0);
			b.x1 = sx(b.x1);
			b.y1 = sy(b.y1);
			b.pivot_x = sx(b.pivot_x);
			b.pivot_y = sy(b.pivot_y);
			if (b.shape != OpticsBody::Grating) {
				b.size *= scale; // (a grating's is its ruling's pitch, in nanometres)
			}
			b.dash *= scale;
			if (b.relay != nullptr) {
				// a sensor's display (Stage 48): its frame where the view puts it
				relays_at_view.push_back(*b.relay);
				relays_at_view.back().frame_x = sx(b.relay->frame_x);
				relays_at_view.back().frame_y = sy(b.relay->frame_y);
				b.relay = &relays_at_view.back();
			}
			out.push_back(b); // (Stage 46: every body, on the screen or off it: a sensor's frame may be anywhere in the room)
		}
		return out;
	}
	// the passes that trace rays run over the room's part of the screen alone
	void clip_rectangles() {
		std::vector<float> kept;
		std::vector<int> bodies_kept;
		for (size_t k = 0; k + 3 < scene.rectangles.size(); k += 4) {
			const float l = std::max(scene.rectangles[k], std::floor(ROOM.x)), t = std::max(scene.rectangles[k + 1], std::floor(ROOM.y));
			const float r = std::min(scene.rectangles[k + 2], std::ceil(ROOM.x + ROOM.w)), b = std::min(scene.rectangles[k + 3], std::ceil(ROOM.y + ROOM.h));
			if (r > l && b > t) {
				kept.insert(kept.end(), { l, t, r, b });
				bodies_kept.push_back(scene.rectangle_bodies[k / 4]);
			}
		}
		scene.rectangles = kept;
		scene.rectangle_bodies = bodies_kept;
	}


	// The viewers' probes read (GAME_SPEC §12: the spectrum probe; Stage 42): the scene as the pass has it, but for
	// the probes' own heads and cables, made again whenever a probe has moved and at most every READING_EVERY_MS
	// while they rest; each probe's disc where it is now, read for a frame's share of PROBE_BUDGET_MS; and each
	// screen's picture plotted from what its probe has read (shown at the next frame: on its own screen, a probe
	// reads the frame before)
	void read_probes(const std::vector<OpticsBody> &bodies, size_t probes_from, size_t probes_to, const OpticsRoom &room) {
		std::vector<int> showing, cams;
		for (int i = 0; i < static_cast<int>(view.items.size()); ++i) {
			if (view.items[static_cast<size_t>(i)].kind == "viewer" && view.items[static_cast<size_t>(i)].place == "bench") {
				showing.push_back(i); // (switched on: on the bench)
			}
			if (view.items[static_cast<size_t>(i)].kind == "thermal" && view.items[static_cast<size_t>(i)].place == "bench") {
				cams.push_back(i); // a thermal camera (Stage 48), switched on
			}
		}
		if (showing.empty() && cams.empty()) {
			return;
		}
		const double now = clock_ms();
		bool moved_any = false;
		std::vector<std::pair<float, float>> at;
		for (const int i : showing) {
			float x = 0, y = 0;
			probe_point(i, x, y);
			at.emplace_back(x, y);
			const Spectrometer &p = viewers[id(i)].probe;
			moved_any = moved_any || x != p.x() || y != p.y();
		}
		if (moved_any || now - reading_ms >= READING_EVERY_MS || reading.table.empty()) {
			std::vector<OpticsBody> seen;
			seen.reserve(bodies.size());
			for (size_t k = 0; k < bodies.size(); ++k) {
				if (k < probes_from || k >= probes_to) {
					seen.push_back(bodies[k]);
				}
			}
			optics_compose(reading, room, seen);
			reading_ms = now;
		}
		// (the time is the rays' own, from when the scene they read is made: a sweep of every nanometre first, whatever
		// the time, so a probe carried shows the whole spectrum at every frame)
		const double start = clock_ms();
		for (size_t n = 0; n < showing.size(); ++n) {
			ViewerState &st = viewers[id(showing[n])];
			st.probe.aim(reading, at[n].first, at[n].second, PROBE_RADIUS_PX);
			const double share = start + PROBE_BUDGET_MS * static_cast<double>(n + 1) / static_cast<double>(showing.size());
			int read = st.probe.read(reading, SPECTRUM_NM);
			read += st.probe.read(reading, PROBE_RAYS_MOST / static_cast<int64_t>(showing.size()) - SPECTRUM_NM, [&] { return clock_ms() < share; });
			if (read > 0 || !st.plotted) {
				plot_spectrum(st.probe.curve(), st.probe.read_count(), st.picture);
				st.plotted = true;
			}
		}
		for (const int i : cams) {
			camera_read(i); // a step of each camera's search (Stage 48)
		}
	}

	void paint_readout() {
		if (report_active()) {
			paint_report(); // (Stage 54) in place of the readout, once a tutorial with a report is complete
			return;
		}
		if (predict_shown()) {
			paint_predict(); // (Stage 55) in place of the readout, until the predictions are made
			return;
		}
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
						: p.trying ? "now " + shown(p.quantity, now(p.quantity)) + "; trying what it controls, to tune itself"
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
				const bool tuned = v.kind == "spectrophotometer";
				const double open = v.kind == "burner" ? v.valve : tuned ? nm_share(v.nm) : v.cap;
				text(READOUT.x + 16, control.y + 15, v.kind == "burner" ? "valve" : tuned ? "nm" : has_outlet(v) ? "tap" : "cap", INK, 16);
				rect_fill(control, BUTTON);
				const float knob = control.x + control.w * static_cast<float>(open);
				rect_fill(Rect{ control.x, control.y, knob - control.x, control.h }, BUTTON_ON);
				rect_fill(Rect{ knob - 4, control.y - 4, 8, control.h + 8 }, INK);
				text(control.x + control.w + 12, control.y + 15, tuned ? text_of("%.0f nm", v.nm) : text_of("%.0f%%", 100.0 * open), DIM, 15);
				y += 42;
				for (const auto &[name, b] : spectro_buttons(showing)) {
					const bool on = v.holder_c.has_value();
					button(b, name == "blank" ? "blank" : name == "-" ? "-1" : name == "+" ? "+1" : name == "holder" ? (on ? text_of("holder %.0f C", *v.holder_c) : std::string("holder off")) : name == "h-" ? "-5" : "+5",
						name == "holder" && on, 15);
				}
				y += spectro_buttons(showing).empty() ? 0 : 52;
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
			if (Rect lock; lock_rect(showing, lock)) { // (Stage 79) in the row of its stirring and its cap
				button(lock, v.plunger_locked ? "plunger locked" : "lock plunger", v.plunger_locked, 15);
				Rect other;
				if (!stir_rect(showing, other) && !screw_rect(showing, other)) {
					y += 42;
				}
			}
			for (const ControlSetting &c : view.controls) {
				if (c.item == v.id) {
					row(c.control + (c.control == "valve" ? text_of(" %.0f%%", 100.0 * c.value) : c.control == "cap" ? text_of(" %.0f%% open", 100.0 * c.value) : c.value > 0.5 ? " on" : " off") +
									", set by " + c.by +
									text_of(" at %.1f s", static_cast<double>(c.step) / 60.0),
							DIM, 14);
				}
			}
			if (v.kind == "thermal") {
				// a thermal camera (Stage 48): its controls, where its frame is, what it reads
				const std::vector<CameraButton> buttons = camera_buttons(showing);
				for (const CameraButton &b : buttons) {
					button(b.r, b.label, b.on, 14);
				}
				const ThermalSettings &t = v.thermal;
				if (!buttons.empty()) {
					const float y2 = readout_top() + 112, y3 = readout_top() + 148;
					text(READOUT.x + 16 + 40, y2 + 19, text_of("e %.2f", t.emissivity), INK, 15);
					text(READOUT.x + 16 + 230, y2 + 19, text_of("refl %.0f C", t.reflected_c.value_or(view.room_celsius)), INK, 15);
					if (t.locked) {
						text(READOUT.x + 16 + 118, y3 + 19, text_of("%.0f C", t.level_c), INK, 15);
						text(READOUT.x + 16 + 282, y3 + 19, text_of("+-%.0f", 0.5 * t.span_c), INK, 15);
					}
					y = std::max(y, y3 + 50);
				}
				row(framing == showing ? "its frame in hand: it reads where it is"
						: t.frame_room ? "its frame on the room" + text_of(", %.1f squares across, ", t.frame_x) + text_of("%.1f down", t.frame_y)
									   : "its frame over its screen: it sees what is behind it",
						DIM, 15);
				if (v.place != "bench") {
					row("off: put it on the bench to read", DIM, 15);
				} else if (const auto found = cameras.find(v.id); found != cameras.end() && found->second.last.done) {
					const CameraState &st = found->second;
					row(text_of("hottest %.1f C", st.hottest_c) + text_of(" at (%.0f, ", st.last.hottest.x) + text_of("%.0f)", st.last.hottest.y), rgba(255, 110, 90), 16);
					row(text_of("coldest %.1f C", st.coldest_c) + text_of(" at (%.0f, ", st.last.coldest.x) + text_of("%.0f)", st.last.coldest.y), rgba(110, 160, 255), 16);
					row(text_of("centre %.1f C", st.centre_c), INK, 16);
					row(text_of("shows %.1f", st.span.low_c) + text_of(" to %.1f C", st.span.high_c) + ", gain " + st.span.gain + (st.span.over ? (st.span.gain == "low" ? ", past 650 C" : ", past 150 C") : ""),
							st.span.over ? WARN : DIM, 15);
				} else {
					row("reading...", DIM, 15);
				}
			} else if (v.kind == "viewer") {
				// the spectral viewer (Stage 42): where its probe is, how much it has read, and the peak
				Rect home;
				if (probe_home_rect(showing, home)) {
					button(home, "probe home", false, 15);
					y += 42;
				}
				row(probing == showing ? "its probe in hand: reading where it is"
						: !v.probe_on.empty() ? "its probe on " + v.probe_on + text_of(", at %.2f across, ", v.probe_u) + text_of("%.2f down", v.probe_v)
						: v.probe_room		  ? "its probe on the room" + text_of(", %.2f squares across, ", v.probe_u) + text_of("%.2f down", v.probe_v)
											  : "its probe in its holder: take it up to read anything",
						DIM, 15);
				const auto found = viewers.find(v.id);
				if (found != viewers.end()) {
					const Spectrometer &p = found->second.probe;
					int least = PROBE_SAMPLES_MOST;
					for (const int c : p.counts()) {
						least = std::min(least, c);
					}
					const std::array<double, SPECTRUM_NM> c = p.curve();
					int peak = 0;
					for (int k = 1; k < SPECTRUM_NM; ++k) {
						peak = c[static_cast<size_t>(k)] > c[static_cast<size_t>(peak)] ? k : peak;
					}
					row(p.settled() ? "settled: every nanometre the mean of its rays over the disc"
									: text_of("read %.0f of 401 nm", p.read_count()) + text_of(", at least %.0f rays each", least),
							DIM, 15);
					if (p.read_count() > 0) {
						row(text_of("peak %.0f nm: ", SPECTRUM_FIRST_NM + peak) + text_of("%.3g mW/(sr m2 nm)", 1000.0 * radiance_of(c[static_cast<size_t>(peak)], peak)), DIM, 15);
					}
				}
			} else if (v.kind == "balance") {
				// a balance (Stage 50): its tare, what it reads, what is on it, what it states of itself
				Rect tare;
				if (tare_rect(showing, tare)) {
					button(tare, "tare", false, 15);
					y += 42;
				}
				const instruments::Balance *model = instruments::balance(v.model);
				const std::string says = model != nullptr ? instruments::display_text(*model, v.display_g, v.overload) : std::string();
				row(says.empty() ? "off: put it on the bench to weigh" : v.overload ? text_of("OL: past its %.0f g", v.capacity_g) : "reads " + says, v.overload ? WARN : INK, 18);
				row(v.under >= 0 ? "on its pan: " + view.items[static_cast<size_t>(v.under)].id : "set a vessel on its pan to weigh it", DIM, 15);
				std::string name = v.model;
				std::replace(name.begin(), name.end(), '_', '-');
				row(name + text_of(", to %g g", v.readability_g) + text_of(" up to %.0f g,", v.capacity_g) + text_of(" within %g g", v.linearity_g), DIM, 14);
			} else if (v.kind == "spectrophotometer") {
				// (Stage 55) what it reads, against what blank, what is in its well
				const bool on = v.place == "bench" || v.place == "under";
				row(!on ? "off: put it on the bench to read" : !v.blanked ? "no blank yet: a cuvette of the solvent in its well, then blank" : v.overload ? "OL: past its range of absorbance" : text_of("absorbance %.3f", *v.absorbance),
					on && v.blanked && !v.overload ? INK : DIM, 18);
				if (v.blanked && v.blank_nm != v.nm) {
					row(text_of("blanked at %.0f nm, set to ", v.blank_nm) + text_of("%.0f nm: blank again", v.nm), WARN, 15);
				}
				if (v.under >= 0) {
					// (Stage 56) and how warm it is, by its thermometer (by instruments) or as it is
					const ItemView &in = view.items[static_cast<size_t>(v.under)];
					const std::optional<double> warm = view.instruments ? in.read_c : std::optional<double>(in.celsius);
					row("in its well: " + in.id + (warm ? text_of(", %.1f C", *warm) : std::string()), DIM, 15);
				} else {
					row("carry a cuvette onto it to read", DIM, 15);
				}
				const spectro::Model &model = spectro::model();
				row(text_of("%.0f to ", model.first_nm) + text_of("%.0f nm,", model.last_nm) + text_of(" band %.0f nm,", model.band_nm) + text_of(" stray light %g%%", 100.0 * model.stray), DIM, 14);
			} else if (v.kind == "burner") {
				row(text_of("flame %.0f C, valve %.0f%%", v.celsius, 100.0 * v.valve));
			} else if (v.kind == "stopper") {
				row(v.on >= 0 ? "in the neck of " + view.items[static_cast<size_t>(v.on)].id + ": pull it out by picking it up" : "put it on a narrow neck to seal it", DIM, 15);
				row(text_of("holds %.0f N", v.hold_n), DIM, 15);
			} else {
				// by instruments (Stage 50): its label and its instruments' readings, not what it holds
				const bool measured = view.instruments && v.kind == "vessel";
				if (!v.label.empty()) {
					row("\"" + v.label + "\"", INK, 16);
				}
				if (measured) {
					row((v.read_c ? text_of("thermometer %.1f C", *v.read_c) : std::string()) + (v.read_ph ? text_of(", pH meter %.2f", *v.read_ph) : std::string()) +
							(v.read_us_cm ? ", " + shown(Quantity{ v.id, "us_cm", "g" }, *v.read_us_cm) : std::string())); // (Stage 65: its conductivity meter's)
				} else {
					row(text_of("%.2f C", v.celsius) + (v.kind == "vessel" ? text_of(", %.1f kPa", v.pressure_kpa) : std::string()) + (v.ph ? text_of(", pH %.2f", *v.ph) : std::string()) +
							(v.us_cm ? ", " + shown(Quantity{ v.id, "us_cm", "g" }, *v.us_cm) : std::string()));
				}
				if (v.layers.size() > 1 && measured) {
					row(text_of("layered: %d layers", static_cast<int>(v.layers.size())), DIM, 15);
				} else if (v.layers.size() > 1) { // its column (Stage 47)
					row(text_of("layered: %d layers, ", static_cast<int>(v.layers.size())) + text_of("%.1f C at the top, ", v.layers.front().celsius) + text_of("%.1f C at the bottom", v.layers.back().celsius), DIM, 15);
				}
				if (v.boiling) { // its bubbles (Stage 45)
					row(v.bubbles_surface ? text_of("boiling: bubbles of %.1f mm reach the surface", v.bubble_mm) : text_of("bubbles from the floor, gone %.1f mm up", v.bubbles_end_cm * 10.0), DIM, 15);
				} else if (v.gas_extinction > 0.0) {
					row("bubbles of gas rise through it", DIM, 15);
				}
				if (v.burst && v.kind == "vessel") {
					row("broken: its glass burst", WARN, 15);
				} else if (v.sealed) {
					row(text_of("sealed: it gives way past %.0f kPa", v.gives_way_kpa), v.pressure_kpa > 0.8 * v.gives_way_kpa ? WARN : DIM, 15);
				}
				if (v.syringe) { // (Stage 79) its plunger: where it stands, held or free
					const std::string at = measured && v.read_ml ? text_of("plunger at %.1f mL", *v.read_ml) : text_of("plunger at %.2f mL", v.plunger_ml);
					row(at + (v.plunger_held ? text_of(", held: the hand %.0f N", std::fabs(v.plunger_force_n)) : std::string(", free")) + (v.tube >= 0 ? ", tubed" : ""), INK, 16);
				}
				// volumetric glass (Stage 52): a burette's reading from its zero line; a mark, and how the liquid stands to it
				if (v.scale_ml) {
					row(measured && v.read_ml ? text_of("reads %.2f mL from its zero line", *v.read_ml) + text_of(" (class A, +-%.2f)", v.tolerance_ml)
											  : text_of("%.3f mL from its zero line", *v.scale_ml),
							INK, 16);
				}
				if (v.mark_ml) {
					const double off = v.condensed_ml - *v.mark_ml;
					const double near = 0.002; // (as close as an eye sets a meniscus on a mark)
					std::string says = std::fabs(off) <= near ? (measured ? text_of("at its mark: %.2f mL", v.nominal_ml) + text_of(" (class A, +-%.2f)", v.tolerance_ml) : text_of("at its mark: %.4f mL", *v.mark_ml))
							: off < 0.0 ? (measured ? std::string("under its mark") : text_of("%.3f mL under its mark", -off))
										: (measured ? std::string("over its mark") : text_of("%.3f mL over its mark", off));
					if (!(v.condensed_ml > 0.0)) {
						says = "empty";
					}
					row(says, INK, 16);
				}
				if (measured) {
					// what its marks read of its liquid; what it holds is for the student to find out
					if (v.read_ml && v.graduation_ml > 0.0 && !v.scale_ml) {
						const int places = v.graduation_ml >= 1.0 ? 1 : 2;
						row(text_of("reads %.*f mL", places, *v.read_ml) + text_of(" (divisions of %g mL)", v.graduation_ml), DIM, 15);
					} else if (v.read_ml && !v.scale_ml && !v.mark_ml) {
						row(!(v.liquid_ml > 0.0) ? "no liquid"
								: *v.read_ml > 0.0 ? text_of("liquid to about %.0f mL by its marks", *v.read_ml) + text_of(" (every %.0f mL)", v.marks_ml)
												   : text_of("liquid under its first mark (%.0f mL)", v.marks_ml),
								DIM, 15);
					}
					if (v.solid_ml > 0.0) {
						row("a solid in it", DIM, 15);
					}
				} else {
				row(v.kind == "vessel" ? text_of("%.1f mL gas, %.2f mL solids and liquids", v.gas_ml, v.condensed_ml)
									   : text_of("%.1f mL of %.0f mL", v.gas_ml + v.condensed_ml, v.capacity_ml));
				for (const SubstanceView &s : v.contents) {
					if (s.wall || (s.phase == 'a' && s.g < 0.00005)) {
						continue; // (Stage 63) a dissolved trace that reads nothing here: water's own ions, a hydrolysis's
					}
					const std::string phase = s.phase == 'a' ? "aq" : std::string(1, s.phase);
					std::string line = s.formula + "(" + phase + ")" + text_of("  %.4f g", s.g);
					if (s.particles > 0) {
						line += text_of("  %.0f particles, %.0f um", static_cast<double>(s.particles), s.size_um);
					}
					row(line, DIM, 15);
				}
				}
			}
		} else {
			row("point at something to read it", DIM);
		}
		y += 10;
		for (const std::string &e : view.events) {
			row(e, WARN, 15);
		}
		for (const auto &[what, r] : record_buttons()) {
			button(r, record_name(what), false, 14); // (Stage 54) a reading into the notebook
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
		// the amount typed (Stage 50): what a pour or a tip aims at; by instruments, a hand's
		const Rect field = amount_field();
		const std::optional<double> asked = typed();
		rect_fill(field, rgba(24, 24, 24));
		outline(field, amount.empty() ? GRID : GUIDE, amount.empty() ? 1.0f : 2.0f);
		text(field.x + 8, field.y + 23, amount.empty() ? (amount_in_ml() ? "type mL" : "type g") : amount + (amount_in_ml() ? " mL" : " g") + "_", amount.empty() ? DIM : INK, 16);
		if (source.liquid_ml > 0.0) {
			const Rect s = pour_slider();
			const double shown = asked ? std::min(*asked, source.liquid_ml) / source.liquid_ml : pour_fraction;
			rect_fill(s, BUTTON);
			rect_fill(Rect{ s.x, s.y, s.w * static_cast<float>(shown), s.h }, rgba(90, 140, 220));
			if (view.instruments) {
				// (by instruments the vessel's liquid is what its marks say)
				const double whole = source.read_ml.value_or(0.0);
				text(s.x, s.y - 8, asked ? text_of("about %.1f mL, by hand", *asked) : whole > 0.0 ? text_of("about %.0f mL", whole * pour_fraction) + text_of(" of about %.0f mL", whole) : text_of("%.0f%% of its liquid", 100.0 * pour_fraction), DIM, 15);
			} else {
				text(s.x, s.y - 8, asked ? text_of("%.1f mL", *asked) + text_of(" of %.1f mL", source.liquid_ml) : text_of("%.1f mL of %.1f mL", source.liquid_ml * pour_fraction, source.liquid_ml), DIM, 15);
			}
			button(pour_button(), pour_fraction >= 1.0 && !asked ? "pour it all" : "pour");
			button(gentle_button(), "pour gently");
		} else {
			text(PANEL.x + 30, PANEL.y + 100, "no liquid in it", DIM, 15);
		}
		for (const auto &[species, r] : tip_buttons()) {
			const std::optional<double> g = amount_in_ml() ? std::nullopt : typed();
			const std::string what = species == "solid" && view.instruments ? "the solid" : "the " + species;
			button(r, g ? text_of("tip %g g of ", *g) + what : "tip in " + what);
		}
		if (!guided() || asks_click("panel:invert")) {
			button(invert_button(), "upside down, with a cap");
		}
		if (has_mark(view.items[static_cast<size_t>(to)]) && !has_mark(source) && source.liquid_ml > 0.0) {
			button(mark_button(), "to the mark");
		}
		if (could_stand()) {
			button(stand_button(), "stand it in");
		}
		if (could_join()) {
			button(join_button(), view.items[static_cast<size_t>(to)].syringe ? "tube to it" : "on its mouth");
		}
		button(cancel_button(), "cancel");
	}
};

Ui::Ui(std::unique_ptr<Impl> impl) :
		impl_(std::move(impl)) {}
Ui::~Ui() = default;

std::unique_ptr<Ui> Ui::open(std::string_view bench_json, std::string &why) {
	auto impl = std::make_unique<Impl>();
	// (Stage 71) a lab's save (the debug panel's export) opens as it was saved; a bench, at its start
	const nlohmann::json head = nlohmann::json::parse(bench_json, nullptr, false);
	const bool saved = head.is_object() && head.value("format", "") == "chem-save/1";
	impl->lab = saved ? Lab::load(std::string(bench_json), why) : Lab::open(bench_json, why);
	if (!impl->lab) {
		return nullptr;
	}
	impl->bench = std::string(bench_json);
	impl->refresh();
	return std::unique_ptr<Ui>(new Ui(std::move(impl)));
}

void Ui::pointer(float x, float y, bool down, int mods) {
	Impl &m = *impl_;
	// the experiments' list (Stage 78), the screen's own and never the room's view: while it is open it takes every
	// press, and its bar held follows the pointer until it is let go
	if (m.menu_scrolling) {
		m.down = down;
		m.px = x;
		m.py = y;
		if (down) {
			m.menu_scroll_to(y);
		} else {
			m.menu_scrolling = false;
		}
		return;
	}
	if (m.menu && down && !m.down && m.menu_press(x, y)) {
		m.down = down;
		m.px = x;
		m.py = y;
		return;
	}
	if (m.shown_scene) {
		// a scene of the optics alone: a press on the experiments is theirs, every other its own
		const bool pressed = down && !m.down;
		m.down = down;
		m.px = x;
		m.py = y;
		if (pressed && m.menu_press(x, y)) {
			return;
		}
		if (m.shown_scene) {
			m.shown_scene->pointer(x, y, down);
		}
		return;
	}
	// over the room, a point of the view: where it is in the room's layout (Stage 42; at 1x, itself)
	if (m.zoomed() && m.room_at(x, y)) {
		x = layout_x(m.vw, Impl::VIEW_ROOM, x);
		y = layout_y(m.vw, Impl::VIEW_ROOM, y);
	}
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
		if (m.storage_scrolling) {
			m.storage_scroll_to(x);
			return;
		}
		if (m.cabinet_scrolling) {
			m.cabinet_scroll_to(y);
		} else if (m.slider_drag) {
			m.slide(x);
		} else if (m.valve_drag >= 0) {
			m.turn(x);
		} else if (m.plunging >= 0) {
			m.plunge(y);
		}
	} else if (released) {
		if (m.storage_scrolling) {
			m.storage_scrolling = false;
			return;
		}
		if (m.cabinet_scrolling) {
			m.cabinet_scrolling = false;
		} else if (m.plunging >= 0) {
			// (Stage 79) the handle let go: the plunger free
			const int at = m.plunging;
			m.plunging = -1;
			m.act({ { "do", "plunger" }, { "item", m.id(at) }, { "free", true } });
		} else if (m.framing >= 0 && !m.frame_in_hand) {
			// a camera's frame (Stage 48): pressed and let go where it was, it is in hand, and the next press puts it
			// down; pressed and moved, it goes where the button is let go
			if (std::fabs(x - m.press_x) > 4 || std::fabs(y - m.press_y) > 4) {
				m.drop_frame(m.framing, x, y);
			} else {
				m.frame_in_hand = true;
			}
		} else if (m.probing >= 0 && !m.probe_in_hand) {
			// a probe (Stage 42): pressed and let go where it was, it is in hand, and the next press puts it down;
			// pressed and moved, it goes where the button is let go
			if (std::fabs(x - m.press_x) > 4 || std::fabs(y - m.press_y) > 4) {
				m.drop_probing(m.probing, x, y);
			} else {
				m.probe_in_hand = true;
			}
		} else if (m.making >= 0 && !m.making_carried && DRAWER.has(x, y)) {
			// pressed and let go on the drawer (Stage 34): the part is in hand, as an item taken from the cabinet
			// is, and the next press puts it down. (Pressed and dragged out, it goes where the button is let go.)
			m.making_carried = true;
		} else if (m.making >= 0) {
			const int kind = m.making;
			m.making = -1;
			m.making_carried = false;
			m.drop_making(kind, x, y);
		} else if (m.wiring >= 0) {
			const int from = m.wiring;
			m.wiring = -1;
			m.drop_wiring(from, x, y);
		} else if (m.sensing >= 0) {
			const int from = m.sensing;
			m.sensing = -1;
			m.drop_sensing(from, x, y);
		} else if (m.draining >= 0) {
			const int from = m.draining;
			m.draining = -1;
			m.drop_draining(from, x, y);
		} else if (m.drag >= 0 && m.carrying) {
			// carried (Stage 27): let go, a pour by hand turns back upright (Stage 31: the frames bring it back, and
			// it ends in the hand); a click puts it down, on its own place too
			if (m.pouring) {
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

namespace {

// (Stage 61) a picture's bytes as text, and back
const char *const BASE64 = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
std::string to_base64(const std::vector<uint8_t> &in) {
	std::string out;
	out.reserve((in.size() + 2) / 3 * 4);
	for (size_t i = 0; i < in.size(); i += 3) {
		const uint32_t n = (static_cast<uint32_t>(in[i]) << 16) | (i + 1 < in.size() ? static_cast<uint32_t>(in[i + 1]) << 8 : 0u) | (i + 2 < in.size() ? in[i + 2] : 0u);
		out += BASE64[(n >> 18) & 63];
		out += BASE64[(n >> 12) & 63];
		out += i + 1 < in.size() ? BASE64[(n >> 6) & 63] : '=';
		out += i + 2 < in.size() ? BASE64[n & 63] : '=';
	}
	return out;
}
bool from_base64(const std::string &in, std::vector<uint8_t> &out) {
	out.clear();
	uint32_t n = 0;
	int bits = 0;
	for (const char c : in) {
		if (c == '=') {
			break;
		}
		const char *at = std::strchr(BASE64, c);
		if (at == nullptr || c == 0) {
			return false;
		}
		n = (n << 6) | static_cast<uint32_t>(at - BASE64);
		bits += 6;
		if (bits >= 8) {
			bits -= 8;
			out.push_back(static_cast<uint8_t>((n >> bits) & 0xff));
		}
	}
	return true;
}

} // namespace

bool Ui::add_pictures(std::string_view text, std::string &why) {
	json j;
	try {
		j = json::parse(text);
		if (!j.is_object() || j.value("format", "") != "chem-pictures/1") {
			why = "the pictures are not \"chem-pictures/1\"";
			return false;
		}
		if (j.value("renderer", "") != RENDERER_KEY) {
			return true; // (Stage 86) another renderer's pictures (or a file older than the key): none is this one's
		}
		for (const json &p : j.at("pictures")) {
			Picture pic;
			pic.w = p.at("w").get<int>();
			pic.h = p.at("h").get<int>();
			if (!from_base64(p.at("rgb").get<std::string>(), pic.rgb) || pic.w <= 0 || pic.h <= 0 || pic.rgb.size() != static_cast<size_t>(pic.w) * static_cast<size_t>(pic.h) * 3) {
				why = "a picture's pixels are not its size";
				return false;
			}
			uint64_t h = 1469598103934665603ull;
			for (const uint8_t b : pic.rgb) {
				h = (h ^ b) * 1099511628211ull;
			}
			pic.hash = h;
			impl_->pictures[p.at("key").get<std::string>()] = std::move(pic);
		}
	} catch (const std::exception &e) {
		why = std::string("the pictures are malformed: ") + e.what();
		return false;
	}
	return true;
}

std::string Ui::pictures() {
	json list = json::array();
	for (const auto &[key, p] : impl_->pictures) {
		list.push_back({ { "key", key }, { "w", p.w }, { "h", p.h }, { "rgb", to_base64(p.rgb) } });
	}
	impl_->pictures_new = false;
	return json{ { "format", "chem-pictures/1" }, { "renderer", RENDERER_KEY }, { "pictures", list } }.dump();
}

bool Ui::pictures_changed() const {
	return impl_->pictures_new;
}

void Ui::capture_pictures() {
	Impl &m = *impl_;
	if (m.capturing.valid()) {
		m.pictures[m.capturing_key] = m.capturing.get();
		m.pictures_new = true;
	}
	for (const ItemView &s : std::vector<ItemView>(m.view.supplies)) {
		if (m.pictures.find(Impl::picture_key(s)) == m.pictures.end()) {
			m.capture_one(s.id, true);
		}
	}
}

void Ui::set_capture_ahead(bool on) {
	impl_->capture_ahead = on;
}

void Ui::frame(double seconds) {
	Impl &m = *impl_;
	if (m.shown_scene) { // a scene of the optics alone: no clock, no lab
		m.clock += seconds > 0.0 ? seconds : 0.0;
		return;
	}
	m.follow_pictures(); // (Stage 61)
	if (m.pouring && m.drag >= 0 && seconds > 0.0) {
		// A pour by hand (Stage 27; Stage 31): to the pouring place, then tilting further while the button is held;
		// let go, upright again, then back to the hand, where it ends
		const double before = m.tilt;
		if (m.down) {
			m.eased = std::min(1.0, m.eased + seconds / Impl::EASE_SECONDS);
			if (m.eased >= 1.0) {
				m.tilt = std::min(1.0, m.tilt + seconds / Impl::TILT_SECONDS);
			}
		} else if (m.tilt > 0.0) {
			m.tilt = std::max(0.0, m.tilt - seconds / Impl::UNTILT_SECONDS);
		} else {
			m.eased = std::max(0.0, m.eased - seconds / Impl::EASE_SECONDS);
		}
		if (m.tilt != before) {
			m.act({ { "do", "pour" }, { "from", m.id(m.drag) }, { "rate", m.tilt } });
		}
		if (!m.down && !(m.tilt > 0.0) && !(m.eased > 0.0)) {
			m.stop_pouring();
		}
	}
	if (!m.paused && !m.lab->failed() && seconds > 0.0) {
		const double asked = seconds * m.speed * 60.0;
		m.owed += asked;
		// no more than twenty simulated seconds a frame: beyond it the clock runs slow, never the picture
		const auto steps = static_cast<int64_t>(m.owed < 1200.0 ? m.owed : 1200.0);
		std::string why;
		bool ok = true, first = true, stopped = false;
		int64_t done = 0;
		// one step of every frame against every invariant; all of them when asked; and
		// the lab stops at the end of a step in which an alarm rang (§13.3)
		const auto run = [&](int64_t n) {
			const int64_t before = m.lab->step();
			m.lab->set_checking(first || m.check_every_step);
			ok = m.lab->advance(first && !m.check_every_step ? 1 : n, why, true);
			m.lab->set_checking(false);
			if (ok && first && !m.check_every_step && n > 1 && !m.lab->rang()) {
				ok = m.lab->advance(n - 1, why, true);
			}
			first = false;
			done += m.lab->step() - before;
			if (ok && m.lab->rang()) {
				stopped = true;
				m.ring();
			}
		};
		// Under a budget (Stage 36) the steps are run in pieces, each as many as the time left is likely to hold
		// at what the steps so far have cost, until it is spent: at least one step a frame.
		const double start = m.budget_ms > 0.0 ? m.clock_ms() : 0.0;
		const auto spent = [&] { return m.budget_ms > 0.0 && m.clock_ms() - start >= m.budget_ms; };
		const auto piece = [&](int64_t left, int64_t most) {
			if (!(m.budget_ms > 0.0)) {
				return std::min(left, most);
			}
			if (done == 0) {
				return std::min<int64_t>(left, 1); // (the first is checked)
			}
			// as many as the time left holds at what the steps so far cost; eight at most until nine have run (the
			// first, checked, says too much of a step's cost, or, where a step is dear, a controller's trial, too
			// little: Stage 39)
			const double each = std::max((m.clock_ms() - start) / static_cast<double>(done), 1e-6);
			const auto fits = static_cast<int64_t>((m.budget_ms - (m.clock_ms() - start)) / each);
			return std::clamp<int64_t>(fits, 1, std::min({ left, most, done < 9 ? int64_t{ 8 } : most }));
		};
		if (!m.tutorial) {
			while (ok && done < steps && !stopped && !(done > 0 && spent())) {
				run(piece(steps - done, steps));
			}
		} else {
			// a tutorial's clock stands while its step asks for a gesture (§13.2): the lab
			// advances a second at a time, and stops in the second where a wait ends
			while (ok && done < steps && !m.held() && !m.paused && !stopped && !(done > 0 && spent())) {
				run(piece(steps - done, 60));
				m.refresh();
				m.follow();
			}
		}
		// what was not run is carried, no more than one frame's worth: beyond it the clock runs slow
		m.owed = m.owed < 1200.0 ? std::min(m.owed - static_cast<double>(done), std::max(asked, 1.0)) : 0.0;
		if (stopped) {
			m.owed = std::min(m.owed, std::max(m.owed - std::floor(m.owed), 0.0)); // (stopped by an alarm: the frame's other steps are not owed)
		}
		m.achieved = 0.9 * m.achieved + 0.1 * (static_cast<double>(done) / 60.0 / seconds);
		if (m.held() || m.paused) {
			m.owed = 0.0;
		}
		if (!ok) {
			m.message = why;
		}
	} else if (m.paused) {
		m.achieved = 0.0;
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
	Impl &m = *impl_;
	if (m.shown_scene) {
		// the scene's own picture, and over it the experiments' button and list
		m.list = m.shown_scene->draw();
		m.paint_menu();
		return m.list;
	}
	m.paint();
	return m.list;
}

const OpticsScene &Ui::optics() {
	if (impl_->shown_scene) {
		return impl_->shown_scene->optics();
	}
	impl_->compose();
	return impl_->scene;
}

Lab &Ui::lab() {
	return *impl_->lab;
}

const Spectrometer *Ui::probe(std::string_view viewer) const {
	const auto found = impl_->viewers.find(std::string(viewer));
	return found == impl_->viewers.end() ? nullptr : &found->second.probe;
}

const Picture *Ui::screen(std::string_view viewer) const {
	const auto found = impl_->viewers.find(std::string(viewer));
	return found == impl_->viewers.end() || !found->second.plotted ? nullptr : &found->second.picture;
}

bool Ui::tutor(std::string_view tutorial_json, std::string &why) {
	Impl &m = *impl_;
	std::unique_ptr<Tutorial> t = Tutorial::open(tutorial_json, m.view, why);
	if (!t) {
		return false;
	}
	m.tutorial = std::move(t);
	m.clear_report(); // (Stage 54)
	m.tutorial_json = std::string(tutorial_json);
	m.shown_k = m.shown_now();
	m.follow();
	return true;
}

void Ui::set_strict(bool strict) {
	impl_->strict = strict;
}

bool Ui::strict() const {
	return impl_->strict;
}

bool Ui::experiments(std::string_view catalogue_json, std::string &why, const std::function<std::optional<std::string>(const std::string &path)> &read) {
	Impl &m = *impl_;
	const json doc = json::parse(catalogue_json, nullptr, false);
	if (doc.is_discarded() || !doc.is_object() || doc.value("format", "") != "chem-experiments/1" || !doc.contains("experiments") || !doc["experiments"].is_array() ||
			doc["experiments"].empty()) {
		why = "experiments: not a chem-experiments/1 catalogue with a list of experiments";
		return false;
	}
	std::vector<Impl::Experiment> out;
	for (const json &e : doc["experiments"]) {
		if (!e.is_object() || !e.contains("id") || !e["id"].is_string() || !e.contains("title") || !e["title"].is_string() || (!e.contains("bench") && !e.contains("scene"))) {
			why = "experiments: each needs an id, a title and its bench (or its scene)";
			return false;
		}
		Impl::Experiment x;
		x.id = e["id"].get<std::string>();
		x.title = e["title"].get<std::string>();
		x.summary = e.value("summary", "");
		// (Stage 78) what the list's search matches: its id, title and summary, and its keywords if it has any
		for (const std::string &t : { x.id, x.title, x.summary }) {
			for (std::string &w : lab::search::words(t)) {
				x.words.push_back(std::move(w));
			}
		}
		if (e.contains("keywords")) {
			if (!e["keywords"].is_array()) {
				why = "experiment " + x.id + ": its keywords are a list of words";
				return false;
			}
			for (const json &k : e["keywords"]) {
				if (!k.is_string()) {
					why = "experiment " + x.id + ": its keywords are a list of words";
					return false;
				}
				for (std::string &w : lab::search::words(k.get<std::string>())) {
					x.words.push_back(std::move(w));
				}
			}
		}
		// a bench or a tutorial inlined, or its path, read by the host's `read`
		const auto text_of_entry = [&](const json &v, std::string &out) {
			if (v.is_object()) {
				out = v.dump();
				return true;
			}
			if (!v.is_string() || !read) {
				why = "experiment " + x.id + ": a bench or a tutorial is inlined, or a path a reader is given for";
				return false;
			}
			const std::optional<std::string> text = read(v.get<std::string>());
			if (!text) {
				why = "experiment " + x.id + ": cannot read " + v.get<std::string>();
				return false;
			}
			out = *text;
			return true;
		};
		if (e.contains("scene")) {
			// a scene of the optics alone (Stage 47): the prism and the grating
			if (!e["scene"].is_string() || e["scene"].get<std::string>() != "prism" || e.contains("bench") || e.contains("tutorial")) {
				why = "experiment " + x.id + ": a scene is \"prism\", with no bench and no tutorial";
				return false;
			}
			x.scene = e["scene"].get<std::string>();
		} else if (!text_of_entry(e["bench"], x.bench) || (e.contains("tutorial") && !e["tutorial"].is_null() && !text_of_entry(e["tutorial"], x.tutorial))) {
			return false;
		}
		for (const Impl::Experiment &y : out) {
			if (y.id == x.id) {
				why = "experiments: two are named " + x.id;
				return false;
			}
		}
		// each is opened now: a catalogue with one that cannot be is refused whole
		if (!x.scene.empty()) {
			out.push_back(std::move(x));
			continue;
		}
		std::unique_ptr<Lab> bench = Lab::open(x.bench, why);
		if (!bench) {
			why = "experiment " + x.id + ": " + why;
			return false;
		}
		if (!x.tutorial.empty() && !Tutorial::open(x.tutorial, bench->view(), why)) {
			why = "experiment " + x.id + ": " + why;
			return false;
		}
		out.push_back(std::move(x));
	}
	m.catalogue = std::move(out);
	m.set_menu(false);
	m.shown_k = m.shown_now();
	return true;
}

bool Ui::choose(std::string_view id, std::string &why) {
	Impl &m = *impl_;
	for (int k = 0; k < static_cast<int>(m.catalogue.size()); ++k) {
		if (m.catalogue[static_cast<size_t>(k)].id == id) {
			return m.open_experiment(k, why);
		}
	}
	why = "no experiment is named " + std::string(id);
	return false;
}

std::string Ui::experiment() const {
	return impl_->shown_k >= 0 ? impl_->catalogue[static_cast<size_t>(impl_->shown_k)].id : std::string();
}

std::string Ui::progress() const {
	nlohmann::json done = nlohmann::json::array();
	for (const std::string &id : impl_->done) {
		done.push_back(id);
	}
	return nlohmann::json{ { "format", "chem-progress/1" }, { "done", done } }.dump();
}

bool Ui::set_progress(std::string_view progress_json, std::string &why) {
	const nlohmann::json j = nlohmann::json::parse(progress_json, nullptr, false);
	if (j.is_discarded() || !j.is_object() || j.value("format", "") != "chem-progress/1" || !j.contains("done") || !j.at("done").is_array()) {
		why = "progress is {\"format\": \"chem-progress/1\", \"done\": [ids]}";
		return false;
	}
	std::set<std::string> done;
	for (const nlohmann::json &id : j.at("done")) {
		if (!id.is_string()) {
			why = "progress's \"done\" holds experiments' ids";
			return false;
		}
		done.insert(id.get<std::string>()); // (an id the catalogue has not is kept: another catalogue may)
	}
	impl_->done = std::move(done);
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
	if (key == KeyView) {
		reset_view();
		return;
	}
	impl_->type(key, codepoint);
	impl_->follow();
}

void Ui::zoom(float x, float y, float factor) {
	if (impl_->menu) { // the experiments' list, while it is open (Stage 78): the wheel scrolls it, a row a notch
		if (factor > 0.0f && factor != 1.0f && std::isfinite(factor)) {
			impl_->menu_scroll(factor > 1.0f ? -1 : 1);
		}
		return;
	}
	if (impl_->shown_scene) { // in a scene of the optics alone the wheel turns what it shows
		impl_->shown_scene->turn(factor > 1.0f ? 0.05 : factor < 1.0f ? -0.05 : 0.0);
		return;
	}
	Impl &m = *impl_;
	if (CABINET.has(x, y) && factor > 0.0f && factor != 1.0f && std::isfinite(factor)) {
		m.cabinet_scroll(factor > 1.0f ? -1 : 1); // the wheel over the cabinet scrolls it, a row a notch (Stage 48)
		return;
	}
	if (OUTBOX.has(x, y) && factor > 0.0f && factor != 1.0f && std::isfinite(factor)) {
		m.storage_scroll(factor > 1.0f ? -1 : 1); // and over Storage, a slot a notch (Stage 61)
		return;
	}
	if (!(factor > 0.0f) || !std::isfinite(factor) || !m.room_at(x, y)) {
		return; // (over a panel, the screen's own: not the room's)
	}
	m.set_view(view_zoomed(m.vw, Impl::VIEW_ROOM, x, y, factor));
}

void Ui::pan(float dx, float dy) {
	Impl &m = *impl_;
	if (m.menu) { // and a trackpad's two fingers scroll it (Stage 78)
		if (std::isfinite(dy)) {
			m.menu_panned(dy);
		}
		return;
	}
	if (std::isfinite(dx) && std::isfinite(dy)) {
		m.set_view(view_panned(m.vw, Impl::VIEW_ROOM, dx, dy));
	}
}

void Ui::reset_view() {
	impl_->set_view(view_whole(Impl::VIEW_ROOM));
}

View Ui::view() const {
	return impl_->vw;
}

bool Ui::view_moving() const {
	return impl_->view_moving();
}

View Ui::scene_view() const {
	return impl_->composed_at;
}

ViewRoom Ui::view_room() {
	return Impl::VIEW_ROOM;
}

const std::string &Ui::message() const {
	return impl_->message;
}

bool Ui::thermal(std::string_view camera, ThermalShown &out) const {
	const auto found = impl_->cameras.find(std::string(camera));
	if (found == impl_->cameras.end() || !found->second.last.done) {
		return false;
	}
	const Impl::CameraState &st = found->second;
	out = ThermalShown{ st.hottest_c, st.coldest_c, st.centre_c, st.last.hottest.x, st.last.hottest.y, st.last.coldest.x, st.last.coldest.y, st.span.low_c, st.span.high_c, st.span.gain, st.span.over };
	return true;
}

void Ui::set_infrared(bool on) {
	impl_->infrared = on;
}

bool Ui::infrared() const {
	return impl_->infrared;
}

void Ui::set_budget(double ms) {
	impl_->budget_ms = ms > 0.0 ? ms : 0.0;
}

void Ui::set_clock(std::function<double()> ms_now) {
	if (ms_now) {
		impl_->clock_ms = std::move(ms_now);
	}
}

double Ui::achieved() const {
	return impl_->achieved;
}

double Ui::speed() const {
	return impl_->speed;
}

bool Ui::paused() const {
	return impl_->paused;
}

bool Ui::locate(std::string_view what, float &x, float &y) const {
	if (!impl_->locate(what, x, y)) {
		return false;
	}
	if (impl_->room_name(what)) {
		x = impl_->sx(x); // where the view shows it (Stage 42)
		y = impl_->sy(y);
	}
	return true;
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
		y = TRASH.y - 30; // (over the trash, Stage 34)
		return true;
	}
	if (what == "search:supplies" || what == "search:storage") { // (Stage 61)
		const Rect r = Impl::search_rect(what == "search:supplies" ? 1 : 2);
		x = r.cx();
		y = r.cy();
		return true;
	}
	if (what == "trash") {
		x = TRASH.cx();
		y = TRASH.y + 30;
		return true;
	}
	if (what == "trash:empty") { // its button, while something is in it (Stage 38)
		if (view.trash.empty()) {
			return false;
		}
		x = empty_rect().cx();
		y = empty_rect().cy();
		return true;
	}
	if (what == "outbox") { // where a hand-in with no slot goes: the first free one, without a goal first
		const Rect r = m.slot_rect(m.free_slot());
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
	if (what == "tutorial:strict") {
		return m.strict_shown() && centre(Impl::strict_rect());
	}
	if (what == "menu:experiments") {
		return !m.catalogue.empty() && centre(Impl::menu_button());
	}
	if (after("experiment:", rest)) { // (Stage 78: a row the search leaves and in sight)
		for (int j = 0; m.menu && j < static_cast<int>(m.menu_shown.size()); ++j) {
			if (m.catalogue[static_cast<size_t>(m.menu_shown[static_cast<size_t>(j)])].id == rest) {
				const std::optional<Rect> row = m.menu_row(j);
				return row && centre(*row);
			}
		}
		return false;
	}
	// the experiments' list (Stage 78), while it is open: its search field, where the wheel scrolls it, its bar's thumb
	// (while not every row left is in sight) and a place on its track
	if (what == "experiments:search") {
		return m.menu && centre(Impl::menu_field());
	}
	if (what == "experiments:scroll") {
		const Rect r = Impl::menu_rect();
		return m.menu && centre(Rect{ r.x + 16, r.y + Impl::MENU_TOP, r.w - 44, Impl::MENU_ROW * Impl::MENU_ROWS });
	}
	if (what == "experiments:bar") {
		return m.menu && m.menu_most_from() > 0 && centre(m.menu_thumb());
	}
	if (after("experiments:bar@", rest)) {
		const double u = std::atof(std::string(rest).c_str());
		if (!m.menu || m.menu_most_from() <= 0 || !(u >= 0.0 && u <= 1.0)) {
			return false;
		}
		const Rect t = Impl::menu_track();
		x = t.cx();
		y = t.y + t.h * static_cast<float>(u);
		return true;
	}
	if (after("speed:", rest)) {
		for (int i = 0; i < 4; ++i) {
			if (rest == text_of("%.0f", SPEEDS[i])) {
				return centre(m.speed_rect(i));
			}
		}
		return false;
	}
	// the spectral viewer (Stage 42): its probe's disc, its holder, a place of its screen; a place of an item in its
	// own frame (item:<id>@u,v); the readout's button that sends the probe home
	const auto at_uv = [&](std::string_view s, std::string &name, double &u, double &v) {
		const size_t a = s.find('@');
		if (a == std::string_view::npos) {
			name = std::string(s);
			return false;
		}
		name = std::string(s.substr(0, a));
		const std::string rest_uv(s.substr(a + 1));
		return std::sscanf(rest_uv.c_str(), "%lf,%lf", &u, &v) == 2;
	};
	if (after("frame:", rest) || after("lens:", rest)) {
		// a thermal camera's frame's handle (Stage 48); or the middle of its frame
		const bool handle = after("frame:", rest);
		const int i = index_of(rest);
		if (i < 0 || m.view.items[static_cast<size_t>(i)].kind != "thermal" || m.view.items[static_cast<size_t>(i)].place != "bench" || !m.visible(i)) {
			return false;
		}
		const Rect r = handle ? m.frame_handle(i) : m.camera_frame(i);
		x = r.cx();
		y = r.cy();
		return true;
	}
	if (after("probe:", rest)) {
		const int i = index_of(rest);
		if (i < 0 || m.view.items[static_cast<size_t>(i)].kind != "viewer" || !m.visible(i)) {
			return false;
		}
		m.probe_point(i, x, y);
		return true;
	}
	if (after("holder:", rest)) {
		const int i = index_of(rest);
		if (i < 0 || m.view.items[static_cast<size_t>(i)].kind != "viewer" || !m.visible(i)) {
			return false;
		}
		Impl::viewer_rest(m.drawn(i), x, y);
		return true;
	}
	if (after("screen:", rest)) {
		std::string name;
		double u = 0.5, v = 0.5;
		at_uv(rest, name, u, v);
		const int i = index_of(name);
		if (i < 0 || (m.view.items[static_cast<size_t>(i)].kind != "viewer" && m.view.items[static_cast<size_t>(i)].kind != "thermal") || !m.visible(i)) {
			return false;
		}
		// (a viewer's screen, or a thermal camera's: Stage 48)
		const Rect sc = m.view.items[static_cast<size_t>(i)].kind == "thermal" ? Impl::camera_screen(m.drawn(i)) : Impl::viewer_screen(m.drawn(i)), r = m.drawn(i);
		m.place_on(i, (sc.x + u * sc.w - r.x) / r.w, (sc.y + v * sc.h - r.y) / r.h, x, y);
		return true;
	}
	if (after("readout:camera:", rest)) {
		// a thermal camera's control (Stage 48), while it is selected on the bench
		if (m.selected < 0) {
			return false;
		}
		for (const Impl::CameraButton &b : m.camera_buttons(m.selected)) {
			if (b.name == rest) {
				return centre(b.r);
			}
		}
		return false;
	}
	if (what == "readout:probe:holder") {
		Rect r;
		return m.selected >= 0 && m.probe_home_rect(m.selected, r) && centre(r);
	}
	if (after("item:", rest) && rest.find('@') != std::string_view::npos) {
		std::string name;
		double u = 0.5, v = 0.5;
		if (!at_uv(rest, name, u, v)) {
			return false;
		}
		const int i = index_of(name);
		if (i < 0 || !m.visible(i)) {
			return false;
		}
		m.place_on(i, u, v, x, y);
		return true;
	}
	if (after("item:", rest)) {
		const int i = index_of(rest);
		return i >= 0 && m.view.items[static_cast<size_t>(i)].kind != "tray" && m.visible(i) && !m.hidden(i) && centre(m.rect(i)); // (not scrolled out of the cabinet's sight: Stage 48)
	}
	if (what == "notices") {
		return centre(Impl::notices_button());
	}
	if (what == "debug") {
		return centre(Impl::debug_button());
	}
	if (after("debug:", rest)) {
		for (int k = 0; k < Impl::DEBUG_BUTTON_COUNT; ++k) {
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
	if (after("plunger:", rest)) {
		// (Stage 79) a syringe's plunger's handle: "plunger:<id>" where it is, "plunger:<id>@<ml>" where it would be at ml
		const size_t at = rest.find('@');
		const int i = index_of(rest.substr(0, at));
		if (i < 0) {
			return false;
		}
		const double ml = at == std::string_view::npos ? m.view.items[static_cast<size_t>(i)].plunger_ml : std::atof(std::string(rest.substr(at + 1)).c_str());
		Rect h;
		if (!m.plunger_handle(i, ml, h)) {
			return false;
		}
		x = h.cx();
		y = h.cy();
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
	if (after("nm:", rest)) {
		// (Stage 55) a spectrophotometer's wavelength: "nm:<id>@<nm>" on its slider, "nm:<id>:+" or ":-" its buttons
		const size_t at = rest.find('@'), by = rest.find(':');
		const int i = index_of(rest.substr(0, std::min(at, by)));
		Rect r;
		if (i < 0 || !m.valve_rect(i, r)) {
			return false;
		}
		if (at != std::string_view::npos) {
			x = r.x + r.w * static_cast<float>(m.nm_share(std::atof(std::string(rest.substr(at + 1)).c_str())));
			y = r.cy();
			return true;
		}
		for (const auto &[name, b] : m.spectro_buttons(i)) {
			if (by != std::string_view::npos && rest.substr(by + 1) == name) {
				return centre(b);
			}
		}
		return false;
	}
	if (after("cell:", rest)) {
		// (Stage 56) a spectrophotometer's cell holder: "cell:<id>" its switch, ":-" or ":+" 5 C down or up
		const size_t by = rest.find(':');
		const int i = index_of(rest.substr(0, by));
		if (i < 0) {
			return false;
		}
		const std::string want = by == std::string_view::npos ? "holder" : rest.substr(by + 1) == "-" ? "h-" : "h+";
		for (const auto &[name, b] : m.spectro_buttons(i)) {
			if (name == want) {
				return centre(b);
			}
		}
		return false;
	}
	if (after("blank:", rest)) {
		const int i = index_of(rest);
		if (i >= 0) {
			for (const auto &[name, b] : m.spectro_buttons(i)) {
				if (name == "blank") {
					return centre(b);
				}
			}
		}
		return false;
	}
	if (after("stir:", rest)) {
		const int i = index_of(rest);
		Rect r;
		return i >= 0 && m.stir_rect(i, r) && centre(r);
	}
	if (after("lock:", rest)) {
		const int i = index_of(rest);
		Rect r;
		return i >= 0 && m.lock_rect(i, r) && centre(r);
	}
	if (after("screw:", rest)) {
		const int i = index_of(rest);
		Rect r;
		return i >= 0 && m.screw_rect(i, r) && centre(r);
	}
	if (after("tare:", rest)) { // (Stage 50)
		const int i = index_of(rest);
		Rect r;
		return i >= 0 && m.tare_rect(i, r) && centre(r);
	}
	if (after("report:", rest)) { // (Stage 54)
		if (!m.report_active()) {
			return false;
		}
		if (rest == "check") {
			return centre(m.report_check());
		}
		const int k = std::atoi(std::string(rest).c_str());
		return k >= 0 && k < static_cast<int>(m.tutorial->report().size()) && centre(Impl::report_field(k));
	}
	if (after("predict:", rest)) { // (Stage 55) a prediction's field, by its order among them; "make"
		if (!m.predict_shown()) {
			return false;
		}
		if (rest == "make") {
			return centre(m.guess_make());
		}
		const int n = std::atoi(std::string(rest).c_str());
		return n >= 0 && n < static_cast<int>(m.predicted_items().size()) && centre(Impl::guess_field(n));
	}
	if (what == "notebook") { // (Stage 54)
		return centre(Impl::notebook_button());
	}
	if (after("notebook:", rest)) {
		// (Stage 58) on the tab that shows it: the calculator's, the reference's, the notebook's own
		const std::string name(rest);
		const int tab = name.rfind("calc:", 0) == 0 ? 1 : name.rfind("ref:", 0) == 0 ? 2 : name.rfind("plan:", 0) == 0 ? 3 : name.rfind("tab:", 0) == 0 || name == "close" ? m.notebook_tab : 0;
		if (name.rfind("plan:", 0) == 0 && name.find_first_of("0123456789") != std::string::npos && std::atoi(name.c_str() + name.rfind(':') + 1) >= static_cast<int>(m.plan_reactions.size())) {
			return false;
		}
		if (name.rfind("calc:field:", 0) == 0 && std::atoi(name.c_str() + 11) >= static_cast<int>(m.calc_fields_of().size())) {
			return false;
		}
		return m.notebook_open && m.notebook_tab == tab && centre(Impl::notebook_rect(name));
	}
	if (after("record:", rest)) {
		const size_t colon = rest.find(':');
		if (colon == std::string_view::npos || m.selected < 0 || m.id(m.selected) != rest.substr(0, colon)) {
			return false;
		}
		for (const auto &[w, r] : m.record_buttons()) {
			if (w == rest.substr(colon + 1)) {
				return centre(r);
			}
		}
		return false;
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
	if (what == "panel:gentle") {
		return centre(Impl::gentle_button());
	}
	if (what == "panel:invert") {
		x = Impl::invert_button().cx();
		y = Impl::invert_button().cy();
		return m.panel;
	}
	if (what == "panel:cancel") {
		return centre(Impl::cancel_button());
	}
	if (what == "panel:mark") { // (Stage 52)
		return m.panel && centre(Impl::mark_button());
	}
	if (what == "panel:tube" || what == "panel:attach") { // (Stage 79)
		return m.could_join() && (m.view.items[static_cast<size_t>(m.to)].syringe == (what == "panel:tube")) && centre(Impl::join_button());
	}
	if (what == "panel:stand") { // (Stage 71)
		return m.could_stand() && centre(Impl::stand_button());
	}
	if (what == "panel:amount") { // (Stage 50)
		return m.panel && centre(Impl::amount_field());
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
