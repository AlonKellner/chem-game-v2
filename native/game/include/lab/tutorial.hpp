#pragma once

#include "lab/lab.hpp"
#include "lab/quantity.hpp"

#include <optional>

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
	std::string type; // text typed into the focused field (then `what` is empty)
};

// What the screen's part editor holds while it is open (§13.3), as a
// tutorial's checks read it ("editor").
struct TutorialEdit {
	std::string editing; // "", "monitor", "timer", "rig", "choose"
	std::string part; // the part being set, or the rig's source
	Quantity quantity; // a monitor's
	Comparison::Op op = Comparison::Below;
	std::string value; // a monitor's first value, as typed
	std::string item, control; // a rig's
};

// What the screen holds that a tutorial's checks may read besides the lab.
struct TutorialScreen {
	double speed = 1.0;
	bool broken = false; // the lab stopped on an invariant
	bool paused = false;
	std::string selected; // the item selected, "" for none
	TutorialEdit edit;
	int predicted = 0; // how many of the report's predictions are made (Stage 55)
	std::string calculated; // what the calculator has worked out, shown: "mass", "moles", "solution", "dilution", "reaction" (Stage 58)
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

// A result the student reports at the end (GAME_SPEC §13.2, Stage 54): what it is and its unit; its truth, an expression
// over the lab's quantities worked out when it is checked (lab/notebook.hpp's `evaluate`); the textbook's ideal, if
// any; how close counts (absolute, or a share of the truth when `relative`); and why the three may differ.
// (Stage 85) one of a report item's choices: its label, and its truth, an expression that is above zero where it holds
// (an ion present, a shift's direction)
struct TutorialChoice {
	std::string label, truth;
};

struct TutorialReport {
	std::string label, unit, truth;
	// (Stage 85) an answer that is no number: the choices that hold, picked from these (one only where `single`)
	std::vector<TutorialChoice> choices;
	bool single = false;
	std::optional<double> ideal;
	double tolerance = 0.0;
	bool relative = false;
	std::string why;
	bool predict = false; // (Stage 55) asked for as a prediction before the run, then shown beside the result
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
	void follow(const LabView &view, const TutorialScreen &screen);
	// From the first step again (the screen's reset).
	void restart();

	const TutorialView &view() const;
	// what the student reports at the end (Stage 54)
	const std::vector<TutorialReport> &report() const;
	// The current step's gestures, in order, while a check that is not waited
	// on is still open: the screen points at the last one it can find, a test
	// plays them all.
	const std::vector<TutorialGesture> &gestures() const;
	// Running, and the current step waits on the lab alone: every check still
	// open is waited on. While a tutorial runs and is not waiting, the screen
	// holds its clock: the lab's time passes only while there is nothing to do.
	bool waiting() const;
	// Whether the current step's checks that ask for a gesture would all hold
	// in this view and screen; the first that would not, in `missing`. It
	// changes nothing: the screen asks it before a command the step's last
	// gesture makes, and refuses one that would leave the step undone (§13.2).
	bool would_pass(const LabView &view, const TutorialScreen &screen, std::string &missing) const;
	const std::string &title() const;

private:
	struct Impl;
	explicit Tutorial(std::unique_ptr<Impl> impl);
	std::unique_ptr<Impl> impl_;
};

} // namespace lab
