#pragma once

#include "lab/lab.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

// The tutorial (GAME_SPEC §13.2): the demo as steps, each with what to do, why,
// and checks read from the lab's state. A step is passed when all its checks
// hold; the tutorial is complete when every step is passed and its goal holds,
// and it fails, until a reset, when one of its failures happens. It only reads
// the lab: it never commands it. Data: data/tutorials/*.json ("chem-tutorial/1").
namespace lab {

struct TutorialCheck {
	std::string text;
	bool done = false;
	bool wait = false; // the lab is waited on for it, not a gesture
	std::string now; // the measured value it waits on: "3.214 g", "184.2 C"; empty when it has none
};

// A gesture a step asks for, named as Ui::locate names things.
struct TutorialGesture {
	bool drag = false; // a drag from `what` to `to`; otherwise a click on `what`
	std::string what, to;
};

struct TutorialView {
	enum State : int32_t { Running = 0, Complete = 1, Failed = 2 };
	State state = Running;
	int step = 0; // the current step, from 0; equal to `steps` once every step is passed
	int steps = 0;
	std::string part; // "Stage 1: gas from malachite"
	std::string title, text, why;
	std::vector<TutorialCheck> checks;
	std::string failure; // why it failed
	std::vector<std::string> results; // what was made, measured, once complete
};

class Tutorial {
public:
	// null, with the reason in `why`, when the text is not a tutorial, or names
	// an item the bench does not have or a species the tables do not.
	static std::unique_ptr<Tutorial> open(std::string_view tutorial_json, const LabView &bench, std::string &why);
	~Tutorial();

	// Reads the lab after a frame or a gesture: passes the steps whose checks
	// hold, in order, and fails or completes. `speed` is the screen's clock;
	// `broken` is the lab having stopped on an invariant.
	void follow(const LabView &view, double speed, bool broken);
	// From the first step again (the screen's reset).
	void restart();

	const TutorialView &view() const;
	// The current step's gestures, in order, while a check that is not waited
	// on is still open: the screen points at the last one it can find, a test
	// plays them all.
	const std::vector<TutorialGesture> &gestures() const;
	const std::string &title() const;

private:
	struct Impl;
	explicit Tutorial(std::unique_ptr<Impl> impl);
	std::unique_ptr<Impl> impl_;
};

} // namespace lab
