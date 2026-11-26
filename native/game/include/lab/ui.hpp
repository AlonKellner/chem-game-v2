#pragma once

#include "lab/lab.hpp"
#include "lab/optics_scene.hpp"
#include "lab/spectrometer.hpp"
#include "lab/tutorial.hpp"
#include "lab/view.hpp"

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// The screen, engine-free (GAME_SPEC §13): a lab, a pointer, and a draw list.
// A host forwards the mouse, calls frame() and paints the list; everything a
// player does on the screen becomes a lab command here, so it is the same on
// every host and is tested without one.
//
// The screen is 1920 x 1080: the cabinet on the left, the bench in the middle,
// the outbox under it, the readouts on the right, the clock on top.
namespace lab {

struct DrawCommand {
	enum Type : int32_t { Rect = 0, Outline = 1, Line = 2, Polygon = 3, Circle = 4, Text = 5, Image = 6 };
	int32_t type = Rect;
	uint32_t color = 0xffffffff; // 0xRRGGBBAA
	float width = 1.0f; // Outline, Line
	int32_t first = 0; // Polygon: its first point (a pair of floats). Text: the offset of its string
	int32_t count = 0; // Polygon: how many points
	// Rect, Outline: x, y, w, h. Line: x1, y1, x2, y2. Circle: centre and w = radius.
	// Text: x, y of its baseline's start and h = its size. Image (Stage 61): x, y, w, h, the picture `first` of the
	// list's images stretched over it.
	float x = 0.0f, y = 0.0f, w = 0.0f, h = 0.0f;
};

// A picture the screen paints (Stage 61: a supply's, captured ahead): its pixels across and down, 8-bit sRGB red,
// green and blue row by row from the top, and a hash that moves only when a pixel does (a host keeps its texture by it)
struct DrawImage {
	int32_t w = 0, h = 0;
	const uint8_t *rgb = nullptr;
	uint64_t hash = 0;
};

struct DrawList {
	std::vector<DrawCommand> commands; // in painting order
	int32_t above = 0; // the first command painted over the optical pass (§12); those before it are under it
	std::vector<float> points; // x, y pairs
	std::string text; // the strings, each ended by a NUL
	std::vector<DrawImage> images; // (Stage 61) the pictures painted, borrowed until the next frame
};

class Ui {
public:
	static std::unique_ptr<Ui> open(std::string_view bench_json, std::string &why);
	~Ui();

	// The pointer, in screen pixels, whether its button is down, and the keys
	// held with it: a click with one adds an item to the selection or takes it
	// away (§13.1).
	// keys held with the pointer, and its right button (Stage 27: the full menu over a vessel)
	enum Mod : int32_t { ModShift = 1, ModCtrl = 2, ModMeta = 4, ModAny = 7, ModRight = 8 };
	void pointer(float x, float y, bool down, int mods = 0);
	// A key for the part editor's focused field (GAME_SPEC §13.3): text (a
	// character, by its code point), backspace, enter (set), escape (cancel; with
	// no editor open, let go of the selection).
	// (KeyView, Stage 42: the room seen whole again, at 1x)
	enum Key : int32_t { KeyText = 0, KeyBackspace = 1, KeyEnter = 2, KeyEscape = 3, KeyUndo = 4, KeyView = 5 };
	void key(Key key, uint32_t codepoint = 0);
	// The view of the room (GAME_SPEC §13.1, Stage 42): a camera on the lit room (the cabinet, the bench, the outbox),
	// from 1x (all of it) to 16x; the top bar, the readout's column and the panels stay as they are. Zoomed by
	// `factor` about a point of the screen (what is under it stays under it), moved by a hand's drag (pixels of the
	// screen), and back to 1x. The room is laid out anew at each view: its bodies, the wall and the depths, so a
	// closer view shows what is finer (a screen's subpixels). The pointer's points over the room are the view's.
	void zoom(float x, float y, float factor);
	void pan(float dx, float dy);
	void reset_view();
	View view() const;
	// Whether the view is moving (Stage 42; GAME_SPEC §12.3): it changed within the last VIEW_QUIET_MS of the clock.
	// While it moves the scene (optics()) is not made again: it stands at the view it was made at (scene_view()),
	// and a host shows its pixels carried to the view now, tracing nothing; at rest it is made at the view once.
	static constexpr double VIEW_QUIET_MS = 120.0;
	bool view_moving() const;
	View scene_view() const;
	// the room's part of the screen, which the view is of
	static ViewRoom view_room();
	// A frame of real time: the lab advances by it times the speed. One step
	// of each frame is checked against every invariant; all of them when the
	// screen's "check every step" is on.
	void frame(double seconds);
	// A frame's time for the lab's steps (Stage 36; GAME_SPEC §13.1), in milliseconds of the CPU: a frame runs
	// whole steps of 1/60 s until it has spent it, and what it could not run is run in the next frames (no more
	// than one frame's worth is carried), so a frame costs the same at any speed of the clock and a heavy moment
	// runs the clock slower, never the picture. 0 (as opened): no budget, every step a frame asks is run (up to
	// twenty seconds' worth). The steps are the same steps however they fall into frames (D1).
	void set_budget(double ms);
	// The debug infrared view (GAME_SPEC §12.4, Stage 46): the whole picture as an 8-14 um thermal camera sees it, a
	// temperature a colour (the debug panel's "infrared"); off as opened.
	void set_infrared(bool on);
	bool infrared() const;
	// The supplies' pictures (GAME_SPEC §13.1, Stage 61): each supply's box is a picture captured once, by the
	// renderer's own source on the CPU, of it alone in its box as the room lights it, kept by what is drawn of it.
	// A host gives back what it kept (`add_pictures`: those built ahead for the benches of data/, then its own), and
	// keeps what is new (`pictures`, a "chem-pictures/1" text of every one there is; `pictures_changed` while one
	// was captured since it was last taken). A box with no picture yet is drawn by the pass until its own is
	// captured, apart, a supply at a time. `capture_pictures` captures every one this bench lacks, waiting (a build's
	// tool, a test).
	bool add_pictures(std::string_view text, std::string &why);
	std::string pictures();
	bool pictures_changed() const;
	void capture_pictures();
	// whether a supply with no picture has its own captured apart, a supply at a time (a host's choice: off as opened)
	void set_capture_ahead(bool on);
	// What a thermal camera on the bench last read (Stage 48): its hottest, coldest and centre (C) and where its hottest
	// and coldest are (its sensor pixels from its frame's top-left), the span it shows and at what gain; false until
	// its first reading.
	struct ThermalShown {
		double hottest_c = 0.0, coldest_c = 0.0, centre_c = 0.0;
		int hot_x = 0, hot_y = 0, cold_x = 0, cold_y = 0;
		double low_c = 0.0, high_c = 0.0;
		std::string gain;
		bool over = false;
	};
	bool thermal(std::string_view camera, ThermalShown &out) const;
	// what the budget is measured by: milliseconds, from any start (a test gives its own; by default the
	// steady clock)
	void set_clock(std::function<double()> ms_now);
	// the clock's speed the lab has kept to over the last frames (the speed chosen, or less where the budget
	// held it back); 0 while paused
	double achieved() const;
	// What to paint now: the commands under the optical pass, the pass itself
	// (what is in the vessels and the balloons, from optics()), the commands over it.
	const DrawList &draw();
	// What the optical pass shows now (GAME_SPEC §12).
	const OpticsScene &optics();
	// A spectral viewer's probe, as it has read so far, and the picture on its screen (GAME_SPEC §13.1, Stage 42);
	// null for none such, or one not yet on the screen. Read when the scene is made (optics()).
	const Spectrometer *probe(std::string_view viewer) const;
	const Picture *screen(std::string_view viewer) const;

	Lab &lab();
	// A tutorial over the lab (GAME_SPEC §13.2): its card above the readout,
	// and a pointer at what its step asks for. false, with the reason, for a
	// tutorial this bench cannot follow. Reset starts it again.
	bool tutor(std::string_view tutorial_json, std::string &why);
	// null without one
	const Tutorial *tutorial() const;
	// Strict (as opened, and the card's switch, tutorial:strict; GAME_SPEC §13.2, Stage 41): a running
	// tutorial refuses every gesture its step does not ask for, and its clock waits while the step asks for
	// one. Free: it only points at what the step asks for; every gesture is the player's, and so is the
	// clock. Steps pass by the state either way, and a failure fails the run either way. The mode is the
	// screen's: it stays through a reset and another experiment.
	void set_strict(bool strict);
	bool strict() const;
	// The experiments (GAME_SPEC §13.7, Stage 41; `chem-experiments/1`): each an id, a title, a summary, its
	// bench and its tutorial (null for none) inlined, all of them opened here and refused if one cannot be
	// (with the reason). The top bar's "experiments" opens a list of them; one chosen opens its bench and its
	// tutorial in place of what is open, and reset starts it again. The entry that is what is open now is
	// marked as shown. A bench or a tutorial given as a path (the catalogue file's own, data/experiments.json)
	// is read by `read` (none: refused); it gives the file's text, or nothing for one it cannot read.
	bool experiments(std::string_view catalogue_json, std::string &why, const std::function<std::optional<std::string>(const std::string &path)> &read = {});
	// opens the experiment of this id, as a click on it does; false, with the reason, for none such
	bool choose(std::string_view id, std::string &why);
	// the id of the experiment shown, "" for none of the catalogue's
	std::string experiment() const;
	// Progress through the experiments (GAME_SPEC §13.7, Stage 55): the ids of those completed, in a
	// `chem-progress/1` document {"format", "done": [ids]}; the host keeps it between runs and gives it back. The
	// list marks each done, and the next in the catalogue's order. set_progress refuses a document that is not one.
	std::string progress() const;
	bool set_progress(std::string_view progress_json, std::string &why);
	// The item selected ("" for none): the last one chosen.
	std::string selected() const;
	// Everything selected, in the order it was chosen (§13.1).
	std::vector<std::string> selection() const;
	// The last notice: a refusal's reason, a failed invariant.
	const std::string &message() const;
	// The lab's save, once the debug panel's "export" was pressed (GAME_SPEC
	// §13.6), for the host to write; "" when it was not since the last call.
	std::string take_export();
	double speed() const;
	bool paused() const;

	// The centre of a named thing on the screen, for tests and scripted hosts:
	//   probe:<viewer> (its probe's disc: in its holder, in hand, or where it is fixed)   holder:<viewer>
	//   screen:<viewer>[@u,v] (a place of its screen, 0 to 1)   item:<id>@u,v (a place of an item in its own frame)
	//   readout:probe:holder (the button that sends the chosen viewer's probe home)   (Stage 42)
	//   item:<id>   square:<col>,<row> (a drop there puts the square the item is
	//   held by on it)   cabinet   outbox
	//   valve:<burner>@<0..1>   stir:<vessel>
	//   panel:pour  panel:slider@<0..1>  panel:tip:<species>  panel:cancel
	//   pause   speed:<1|10|60|600>   check   reset
	//   tutorial:strict (the card's switch)   menu:experiments   experiment:<id> (while the list is open)
	//   drawer:<timer|monitor|alarm>   port:<part>   choose:<wire|reset>
	//   part:edit   part:reset   part:unwire:<to>   part:unrig:<item>:<control>
	//   circuit:create (the drawer's, for the parts chosen)   part:open   part:give_back
	//   circuit:close   circuit:up   inner:<col>,<row> (a square of the open circuit)
	//   notices   notices:<detail|event|warning|danger> (its switch)   notices:close
	//   edit:level:<detail|event|warning|danger>   edit:item:any (a notice part's)
	//   debug   debug:<step|export|up|down|close> (the debug panel, §13.6)
	//   slot:<1..6>   goal:<1..6> (a slot's goal button)   edit:name   edit:term:<r>:<what|measure|compare|value|remove>
	//   edit:term:add   edit:remove (the goal editor, §13.5)
	//   edit:set   edit:cancel   (the part editor, while it is open)
	//     a monitor's: edit:item:<id>   edit:quantity   edit:pick:<what>   edit:measure:<g|count|size|purity>
	//       edit:compare:<above|below|at_least|at_most|reaches|between|outside>   edit:value:<0|1>   edit:nudge:<-3..3>
	//     a timer's: edit:value   edit:nudge:<-3..3>
	//     a rig's: edit:form:<onoff|change>   edit:<on|off|change>:<stir|stop|reset|leave|value|nudge:<-2..2>>
	//   notebook   notebook:<close|erase|x|tx|y|ty|fit|log:1|log:5|log:30|unlog|write:label|write:value|write>
	//   record:<item>:<what>   report:<k>   report:check (Stage 54)
	//   nm:<spectrophotometer>@<nm> (its slider)   nm:<spectrophotometer>:<+|->   blank:<spectrophotometer>
	//   predict:<n> (the n-th prediction's field)   predict:make (Stage 55)
	//   cell:<spectrophotometer> (its cell holder's switch)   cell:<spectrophotometer>:<+|-> (Stage 56)
	// false when it is not on the screen now.
	bool locate(std::string_view what, float &x, float &y) const;

private:
	struct Impl;
	explicit Ui(std::unique_ptr<Impl> impl);
	std::unique_ptr<Impl> impl_;
};

} // namespace lab
