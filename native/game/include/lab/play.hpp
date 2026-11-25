#pragma once

#include "lab/ui.hpp"

#include <string>
#include <string_view>
#include <vector>

// A play (Stage 33): what a hand does on the screen, as data, a frame at a time. The same scenes are timed on
// a host's GPU and counted in the tests (a frame's rays, GAME_SPEC §18: O13), so what is measured is what is
// held. Nothing of the game is decided here: a play gives the screen its pointer, as a host's mouse does.
//
// "chem-play/1": {"format": "chem-play/1", "steps": [step, ...]}, a step:
//   "to":     where the pointer goes, a named thing (Ui::locate: "item:acid", "square:4,8", "stir:flask") or
//             [x, y]; without it the pointer stays
//   "frames": how many frames the step takes (1 without it); the pointer glides there evenly over them
//   "button": what the button does on the step's last frame: "click", "right" (a click of the right button),
//             "down" (pressed, and held through the steps after), "up" (let go); without it, as it was
//   "mark":   a name for the step's frames (what a host times, what a test counts)
//   "do":     (Stage 42) a lab command, given on the step's first frame (a bench set out as a test sets it)
//   "zoom":   (Stage 42) the room's view zoomed by this factor about the pointer on each of the step's frames (a
//             wheel turned: a notch a frame)
//   "shot":   (Stage 42) a name for a picture of the screen a host takes after the step's last frame
//   "type":   (Stage 58) text typed on the step's last frame, after its button, a key a character
namespace lab {

class Play {
public:
	static bool read(std::string_view json, Play &out, std::string &why);
	// The next frame's gestures, given to the screen before its frame(). false once the play is over.
	bool next(Ui &ui);
	bool over() const { return at_ >= steps_.size(); }
	// the mark of the frame last given ("" for none, or when the play is over)
	const std::string &mark() const { return mark_; }
	// a thing a step named that was not on the screen ("" when every one was)
	const std::string &missed() const { return missed_; }
	// the picture to take after the frame last given ("" for none), and a command that was refused ("" for none)
	const std::string &shot() const { return shot_; }
	const std::string &refused() const { return refused_; }

private:
	enum Button : int { Keep = 0, Click = 1, Right = 2, Down = 3, Up = 4 };
	struct Step {
		std::string to; // a named thing, or "" with `placed` for a point
		bool placed = false;
		float x = 0.0f, y = 0.0f;
		int frames = 1;
		int button = Keep;
		std::string mark;
		std::string command, shot, typed;
		float zoom = 0.0f;
	};
	std::vector<Step> steps_;
	size_t at_ = 0;
	int frame_ = 0; // of the step
	float x_ = 960.0f, y_ = 20.0f; // the pointer
	float from_x_ = 960.0f, from_y_ = 20.0f, to_x_ = 960.0f, to_y_ = 20.0f; // the step's glide
	bool down_ = false;
	std::string mark_, missed_, shot_, refused_;
};

} // namespace lab
