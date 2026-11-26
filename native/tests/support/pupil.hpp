#pragma once
// A pupil (GAME_SPEC §13.2): a tutorial played through the screen with the pointer and nothing but its own
// gestures, instantly or as a person plays; shared by the tutorial's tests and the experiments' (Stage 41).
#include "lab/conditions.hpp"
#include "lab/glassware.hpp"
#include "lab/tutorial.hpp"
#include "lab/ui.hpp"

#include <doctest/doctest.h>

#include <cstdlib>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

namespace pupil {

using lab::ItemView;
using lab::LabView;
using lab::TutorialView;

inline std::string read(const std::string &relative) {
	std::ifstream in(std::string(CHEM_REPO_ROOT) + "/" + relative);
	REQUIRE_MESSAGE(static_cast<bool>(in), relative);
	std::ostringstream out;
	out << in.rdbuf();
	return out.str();
}

struct Pupil {
	std::unique_ptr<lab::Ui> ui;

	explicit Pupil(const std::string &bench = "data/benches/lab.json", const std::string &tutorial = "data/tutorials/demo.json", bool strict = true) {
		std::string why;
		ui = lab::Ui::open(read(bench), why);
		REQUIRE_MESSAGE(ui != nullptr, why);
		ui->set_strict(strict);
		REQUIRE_MESSAGE(ui->tutor(read(tutorial), why), why);
	}

	const TutorialView &tutorial() const { return ui->tutorial()->view(); }

	void at(const std::string &what, float &x, float &y) {
		REQUIRE_MESSAGE(ui->locate(what, x, y), "not on the screen: " << what << " (step " << tutorial().step + 1 << ": " << tutorial().title << ")");
	}
	void click(const std::string &what) {
		float x = 0, y = 0;
		if ((what.rfind("valve:", 0) == 0 || what.rfind("stir:", 0) == 0) && !ui->locate(what, x, y)) {
			// an item's controls are shown for the item chosen (Stage 22): it is chosen first
			const std::string item = what.substr(what.find(':') + 1);
			click("item:" + item.substr(0, item.find('@')));
		}
		at(what, x, y);
		const int mods = what.rfind("item:", 0) == 0 ? lab::Ui::ModRight : 0; // an item is chosen by the right button (Stage 27)
		ui->pointer(x, y, false, mods);
		ui->pointer(x, y, true, mods);
		ui->pointer(x, y, false, mods);
	}
	// what a drag names: an item is carried, a click to pick it up and a click to put it down (Stage 27), a vessel
	// onto a vessel with the right button (the menu: what to pour or tip); from a port, the drawer or a slider, a drag
	void drag(const std::string &what, const std::string &to) {
		float x0 = 0, y0 = 0, x1 = 0, y1 = 0;
		at(what, x0, y0);
		if (what.rfind("item:", 0) == 0) {
			ui->pointer(x0, y0, false);
			ui->pointer(x0, y0, true);
			ui->pointer(x0, y0, false);
			at(to, x1, y1);
			const int mods = carried_onto_vessel(what.substr(5), to) ? lab::Ui::ModRight : 0;
			ui->pointer(x1, y1, false, mods);
			ui->pointer(x1, y1, true, mods);
			ui->pointer(x1, y1, false, mods);
			return;
		}
		at(to, x1, y1);
		ui->pointer(x0, y0, false);
		ui->pointer(x0, y0, true);
		ui->pointer((x0 + x1) / 2, (y0 + y1) / 2, true);
		ui->pointer(x1, y1, true);
		ui->pointer(x1, y1, false);
	}
	bool carried_onto_vessel(const std::string &id, const std::string &to) {
		if (to.rfind("item:", 0) != 0) {
			return false;
		}
		const lab::LabView v = ui->lab().view();
		const lab::ItemView *a = nullptr, *b = nullptr;
		for (const lab::ItemView &x : v.items) {
			a = x.id == id ? &x : a;
			b = x.id == to.substr(5) ? &x : b;
		}
		const lab::glass::Vessel *g = a != nullptr && !a->glassware.empty() ? lab::glass::find(a->glassware, a->nominal_ml) : nullptr;
		const bool residue = g != nullptr && g->paper_herzberg_s > 0.0 && a->solid_ml > 0.0 && !(a->liquid_ml > 0.1); // (Stage 59: a funnel's residue, scraped)
		return a != nullptr && b != nullptr && a->kind == "vessel" && g != nullptr && (!g->outlet || residue) && b->kind == "vessel" && !b->condenser && b->place == "bench"; // (an outlet's: a drop, Stage 52)
	}
	void type(const std::string &text) {
		for (const char c : text) {
			ui->key(lab::Ui::KeyText, static_cast<uint32_t>(c));
		}
	}
	// a paused clock (a timer fired) is played on, as the card says
	void resume() {
		if (ui->paused()) {
			click("pause");
		}
	}
	void play(const lab::TutorialGesture &g) {
		if (!g.type.empty()) {
			type(g.type);
		} else if (g.drag) {
			drag(g.what, g.to);
		} else {
			click(g.what);
		}
	}
	ItemView item(const std::string &id) {
		for (const ItemView &v : ui->lab().view().items) {
			if (v.id == id) {
				return v;
			}
		}
		FAIL("no item " << id);
		return ItemView{};
	}
	double grams(const std::string &id, const std::string &species) {
		return lab::grams(item(id), species); // (Stage 63: a retired salt as its ions make it)
	}
	// the step's checks not yet holding, with what they wait on
	std::string open_checks() const {
		std::string out;
		for (const lab::TutorialCheck &c : tutorial().checks) {
			if (!c.done) {
				out += " [open: " + c.text + (c.now.empty() ? "" : ", now " + c.now) + "]";
			}
		}
		return out;
	}
	// frames of 1/60 s until `done`, at most `seconds` of the lab's time
	template <typename Done>
	void watch(Done done, double seconds) {
		const int64_t until = ui->lab().step() + static_cast<int64_t>(seconds * 60.0);
		while (!done()) {
			resume();
			REQUIRE_MESSAGE(ui->lab().step() < until, "waited too long on step " << tutorial().step + 1 << ": " << tutorial().title << open_checks());
			REQUIRE_MESSAGE((!ui->strict() || ui->tutorial()->waiting() || tutorial().state != TutorialView::Running),
					"step " << tutorial().step + 1 << " (" << tutorial().title << "): the clock waits for a gesture its gestures did not make; " << ui->message() << open_checks());
			ui->frame(1.0 / 60.0);
			REQUIRE_MESSAGE(!ui->lab().failed(), ui->message());
		}
	}
	// ---- as a person plays: a second and a half to read and reach before each
	// gesture, the pointer moved across the screen frame by frame, the clock running
	void frames(int n) {
		for (int i = 0; i < n; ++i) {
			ui->frame(1.0 / 60.0);
		}
	}
	// where a person lets go: on an item, its middle; in the outbox, on whatever was handed in last
	void target(const std::string &what, float &x, float &y) {
		if (what == "outbox") {
			for (const ItemView &v : ui->lab().view().items) {
				if (v.place == "outbox" && ui->locate("item:" + v.id, x, y)) {
					return; // over it
				}
			}
		}
		at(what, x, y);
	}
	void move(float x0, float y0, float x1, float y1, bool down) {
		for (int i = 1; i <= 20; ++i) {
			const float f = static_cast<float>(i) / 20.0f;
			ui->pointer(x0 + (x1 - x0) * f, y0 + (y1 - y0) * f, down);
			ui->frame(1.0 / 60.0);
		}
	}
	void person(const lab::TutorialGesture &g) {
		frames(90); // reading the card, finding the thing
		if (!g.type.empty()) {
			for (const char c : g.type) {
				ui->key(lab::Ui::KeyText, static_cast<uint32_t>(c));
				frames(12);
			}
			return;
		}
		float x0 = 0, y0 = 0, x1 = 0, y1 = 0;
		if (!g.drag && g.what.rfind("valve:", 0) == 0) {
			// a slider: taken at its knob, where it is now, and dragged
			const std::string burner = g.what.substr(6, g.what.find('@') - 6);
			at("valve:" + burner + "@" + std::to_string(item(burner).valve), x0, y0);
			at(g.what, x1, y1);
		} else if (g.drag) {
			at(g.what, x0, y0);
			target(g.to, x1, y1);
		} else {
			at(g.what, x0, y0);
			x1 = x0;
			y1 = y0;
		}
		move(x0 - 200, y0 - 100, x0, y0, false);
		if (g.what.rfind("item:", 0) == 0) {
			// an item is carried (Stage 27): a click picks it up, it follows the pointer, a click puts it down (onto a
			// vessel it pours into, with the right button: the menu); a click on it alone, put back where it was
			ui->pointer(x0, y0, true);
			ui->frame(1.0 / 60.0);
			ui->pointer(x0, y0, false);
			ui->frame(1.0 / 60.0);
			move(x0, y0, x1, y1, false);
			const int mods = g.drag && carried_onto_vessel(g.what.substr(5), g.to) ? lab::Ui::ModRight : 0;
			ui->pointer(x1, y1, true, mods);
			ui->frame(1.0 / 60.0);
			ui->pointer(x1, y1, false, mods);
			ui->frame(1.0 / 60.0);
			return;
		}
		ui->pointer(x0, y0, true);
		ui->frame(1.0 / 60.0);
		move(x0, y0, x1, y1, true);
		ui->pointer(x1, y1, false);
		ui->frame(1.0 / 60.0);
	}

	bool drawn(const std::string &s) {
		const lab::DrawList &list = ui->draw();
		return list.text.find(s) != std::string::npos;
	}
};


// The whole run, each step's gestures played at once and then the lab waited on (at most `wait_s` of the
// lab's time a step); returns the gestures played. Strict or free, as the screen is.
inline int follow_through(Pupil &p, double wait_s) {
	const int steps = p.tutorial().steps;
	int played = 0;
	while (p.tutorial().state == TutorialView::Running && p.tutorial().step < steps) {
		const int step = p.tutorial().step;
		const std::vector<lab::TutorialGesture> gestures = p.ui->tutorial()->gestures();
		for (const lab::TutorialGesture &g : gestures) {
			p.play(g);
			CHECK_MESSAGE(p.ui->message().empty(), "step " << step + 1 << ": " << p.ui->message());
		}
		played += static_cast<int>(gestures.size());
		p.watch([&] { return p.tutorial().step != step || p.tutorial().state != TutorialView::Running; }, wait_s);
		if (p.tutorial().state == TutorialView::Failed) {
			FAIL("step " << step + 1 << " (" << p.tutorial().title << "): " << p.tutorial().failure);
		}
	}
	return played;
}

// The whole run as a person plays it: a second and a half before each gesture, the pointer moved across the
// screen, then the card read every half second while the lab is waited on.
inline void play_as_person(Pupil &p, double wait_s) {
	const int steps = p.tutorial().steps;
	while (p.tutorial().state == TutorialView::Running && p.tutorial().step < steps) {
		const int step = p.tutorial().step;
		const std::string title = p.tutorial().title;
		for (const lab::TutorialGesture &g : std::vector<lab::TutorialGesture>(p.ui->tutorial()->gestures())) {
			p.person(g);
			INFO("step " << step + 1 << ": " << title);
			REQUIRE_MESSAGE(!p.ui->lab().failed(), "step " << step + 1 << " (" << title << "): " << p.ui->message());
			CHECK_MESSAGE(p.ui->message().empty(), p.ui->message());
			if (p.tutorial().state != TutorialView::Running) {
				break;
			}
		}
		const int64_t limit = p.ui->lab().step() + static_cast<int64_t>(wait_s * 60.0);
		while (p.tutorial().step == step && p.tutorial().state == TutorialView::Running) {
			REQUIRE_MESSAGE(p.ui->lab().step() < limit, "waited too long on step " << step + 1 << ": " << title);
			p.frames(45);
			p.resume();
			const int64_t before = p.ui->lab().step();
			p.frames(30);
			REQUIRE_MESSAGE(!p.ui->lab().failed(), "step " << step + 1 << " (" << title << "): " << p.ui->message());
			if (p.ui->lab().step() == before && p.tutorial().step == step) {
				FAIL("step " << step + 1 << " (" << title << "): the clock stands, waiting for a gesture its gestures did not make; " << p.ui->message());
			}
		}
		if (p.tutorial().state == TutorialView::Failed) {
			FAIL("step " << step + 1 << " (" << title << "): " << p.tutorial().failure);
		}
	}
}

} // namespace pupil
