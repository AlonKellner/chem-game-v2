#pragma once

#include "lab/lab.hpp"
#include "lab/optics_scene.hpp"
#include "lab/timers.hpp"
#include "lab/tutorial.hpp"

#include <cstdint>
#include <memory>
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
	enum Type : int32_t { Rect = 0, Outline = 1, Line = 2, Polygon = 3, Circle = 4, Text = 5 };
	int32_t type = Rect;
	uint32_t color = 0xffffffff; // 0xRRGGBBAA
	float width = 1.0f; // Outline, Line
	int32_t first = 0; // Polygon: its first point (a pair of floats). Text: the offset of its string
	int32_t count = 0; // Polygon: how many points
	// Rect, Outline: x, y, w, h. Line: x1, y1, x2, y2. Circle: centre and w = radius.
	// Text: x, y of its baseline's start and h = its size.
	float x = 0.0f, y = 0.0f, w = 0.0f, h = 0.0f;
};

struct DrawList {
	std::vector<DrawCommand> commands; // in painting order
	int32_t above = 0; // the first command painted over the optical pass (§12); those before it are under it
	std::vector<float> points; // x, y pairs
	std::string text; // the strings, each ended by a NUL
};

class Ui {
public:
	static std::unique_ptr<Ui> open(std::string_view bench_json, std::string &why);
	~Ui();

	// The pointer, in screen pixels, and whether its button is down.
	void pointer(float x, float y, bool down);
	// A key for the focused field (GAME_SPEC §13.3): text (a character, by its
	// code point), backspace, enter, escape.
	enum Key : int32_t { KeyText = 0, KeyBackspace = 1, KeyEnter = 2, KeyEscape = 3 };
	void key(Key key, uint32_t codepoint = 0);
	// A frame of real time: the lab advances by it times the speed. One step
	// of each frame is checked against every invariant; all of them when the
	// screen's "check every step" is on.
	void frame(double seconds);
	// What to paint now: the commands under the optical pass, the pass itself
	// (what is in the vessels and the balloons, from optics()), the commands over it.
	const DrawList &draw();
	// What the optical pass shows now (GAME_SPEC §12).
	const OpticsScene &optics();

	Lab &lab();
	// A tutorial over the lab (GAME_SPEC §13.2): its card above the readout,
	// and a pointer at what its step asks for. false, with the reason, for a
	// tutorial this bench cannot follow. Reset starts it again.
	bool tutor(std::string_view tutorial_json, std::string &why);
	// null without one
	const Tutorial *tutorial() const;
	// The player's timers (GAME_SPEC §13.3), and the item selected ("" for none).
	const Timers &timers() const;
	std::string selected() const;
	// The last notice: a refusal's reason, a failed invariant.
	const std::string &message() const;
	double speed() const;
	bool paused() const;

	// The centre of a named thing on the screen, for tests and scripted hosts:
	//   item:<id>   square:<col>,<row> (a drop there puts the square the item is
	//   held by on it)   cabinet   outbox
	//   valve:<burner>@<0..1>   stir:<vessel>
	//   panel:pour  panel:slider@<0..1>  panel:tip:<species>  panel:cancel
	//   pause   speed:<1|10|60|600>   check   reset
	//   timer:add   timer:cancel   timer:arm   timer:any   timer:all   timer:condition:add
	//   timer:quantity:<c>   timer:pick:<what>   timer:measure:<c>:<g|count|size>
	//   timer:compare:<c>:<above|below|at_least|at_most|reaches|between|outside>
	//   timer:value:<c>:<0|1>   timer:nudge:<c>:<-3..3>   timer:remove:<c>
	//   timer:limit:toggle   timer:limit:value   timer:limit:nudge:<-2..2>
	//   timer:row:<n>:rearm   timer:row:<n>:remove
	// false when it is not on the screen now.
	bool locate(std::string_view what, float &x, float &y) const;

private:
	struct Impl;
	explicit Ui(std::unique_ptr<Impl> impl);
	std::unique_ptr<Impl> impl_;
};

} // namespace lab
