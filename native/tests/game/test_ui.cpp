// The screen (GAME_SPEC §13): a pointer in, lab commands and a draw list out.
// The demo is played here with nothing but the pointer.
#include "lab/optics_scene.hpp"
#include "lab/ui.hpp"

#include <doctest/doctest.h>

#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

using lab::ItemView;
using lab::LabView;

namespace {

std::string read(const std::string &relative) {
	std::ifstream in(std::string(CHEM_REPO_ROOT) + "/" + relative);
	REQUIRE_MESSAGE(static_cast<bool>(in), relative);
	std::ostringstream out;
	out << in.rdbuf();
	return out.str();
}

struct Player {
	std::unique_ptr<lab::Ui> ui;

	Player() {
		std::string why;
		ui = lab::Ui::open(read("data/benches/lab.json"), why);
		REQUIRE_MESSAGE(ui != nullptr, why);
	}

	void at(const std::string &what, float &x, float &y) {
		REQUIRE_MESSAGE(ui->locate(what, x, y), "not on the screen: " << what);
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
		return a != nullptr && b != nullptr && a->kind == "vessel" && !a->glassware.empty() && b->kind == "vessel" && !b->condenser && b->place == "bench";
	}
	// a click with keys held (lab::Ui::Mod)
	void click_with(const std::string &what, int mods) {
		float x = 0, y = 0;
		at(what, x, y);
		ui->pointer(x, y, false, mods);
		ui->pointer(x, y, true, mods);
		ui->pointer(x, y, false, mods);
	}
	// the pointer pressed at one point and moved to another as a hand does, a frame at a time
	void sweep(float x0, float y0, float x1, float y1) {
		ui->pointer(x0, y0, false);
		ui->pointer(x0, y0, true);
		for (int i = 1; i <= 20; ++i) {
			const float t = static_cast<float>(i) / 20.0f;
			ui->pointer(x0 + (x1 - x0) * t, y0 + (y1 - y0) * t, true);
			ui->frame(1.0 / 60.0);
		}
		ui->pointer(x1, y1, false);
	}
	ItemView item(const std::string &id) {
		const LabView view = ui->lab().view();
		for (const ItemView &v : view.items) {
			if (v.id == id) {
				return v;
			}
		}
		FAIL("no item " << id);
		return ItemView{};
	}
	double grams(const std::string &id, const std::string &species) {
		for (const lab::SubstanceView &s : item(id).contents) {
			if (s.species == species) {
				return s.g;
			}
		}
		return 0.0;
	}
	// frames of 1/60 s at the speed chosen, until `done` (a player watching)
	template <typename Done>
	void watch(Done done, double timeout_seconds) {
		const double frames = timeout_seconds * 60.0 / ui->speed();
		for (int frame = 0; !done(); ++frame) {
			REQUIRE_MESSAGE(frame < frames, "the player waited too long");
			ui->frame(1.0 / 60.0);
			REQUIRE_MESSAGE(!ui->lab().failed(), ui->message());
		}
	}
	void wait(double seconds) {
		const int frames = static_cast<int>(seconds * 60.0 / ui->speed() + 0.5);
		for (int frame = 0; frame < frames; ++frame) {
			ui->frame(1.0 / 60.0);
		}
		REQUIRE_MESSAGE(!ui->lab().failed(), ui->message());
	}
};

// every index of the draw list is inside it
void well_formed(const lab::DrawList &list) {
	REQUIRE(list.commands.size() > 20);
	for (const lab::DrawCommand &c : list.commands) {
		REQUIRE(c.type >= lab::DrawCommand::Rect);
		REQUIRE(c.type <= lab::DrawCommand::Text);
		if (c.type == lab::DrawCommand::Polygon) {
			REQUIRE(c.first >= 0);
			REQUIRE(c.count >= 3);
			REQUIRE(static_cast<size_t>(2 * (c.first + c.count)) <= list.points.size());
		}
		if (c.type == lab::DrawCommand::Text) {
			REQUIRE(c.first >= 0);
			REQUIRE(static_cast<size_t>(c.first + c.count) < list.text.size());
			REQUIRE(list.text[static_cast<size_t>(c.first + c.count)] == '\0');
		}
	}
}

} // namespace

TEST_CASE("ui: equipment is dragged from the cabinet onto the bench, along it, and back (Stage 9)") {
	Player p;
	CHECK(p.item("flask").place == "cabinet");
	p.drag("item:flask", "square:11,12"); // its body lands under the pointer: a 3 x 7 footprint
	CHECK(p.item("flask").place == "bench");
	CHECK(p.item("flask").col == 10);
	CHECK(p.item("flask").row == 8);
	CHECK(p.item("flask").w == 3);
	CHECK(p.item("flask").h == 7);
	p.drag("item:flask", "square:19,4"); // held by its body's square, (1, 4) of its footprint
	CHECK(p.item("flask").col == 18);
	CHECK(p.item("flask").row == 0);
	p.drag("item:beaker", "square:17,1"); // a footprint over the flask's
	CHECK(p.item("beaker").place == "cabinet");
	CHECK(p.item("beaker").place == "cabinet");
	CHECK(p.ui->message().find("stands there") != std::string::npos);
	p.drag("item:beaker", "item:flask"); // onto the flask itself, the right button: the menu, with nothing to pour
	p.click("panel:cancel");
	float x = 0, y = 0;
	CHECK_FALSE(p.ui->locate("panel:cancel", x, y)); // closed; the beaker still in hand
	p.ui->key(lab::Ui::KeyEscape); // put back where it was (Stage 27)
	p.drag("item:beaker", "square:23,4");
	CHECK(p.item("beaker").place == "bench");
	p.drag("item:beaker", "cabinet");
	CHECK(p.item("beaker").place == "cabinet");
	CHECK(p.ui->message().empty());
	// a click without a drag moves nothing
	p.click("item:flask");
	CHECK(p.item("flask").col == 18);
	well_formed(p.ui->draw());
}

TEST_CASE("ui: an item carried by a person is the hand's layer, and what stands behind it is seen through it (Stage 21)") {
	// A person picks the jar up and carries it slowly over the flask, a few pixels a frame, and holds it there:
	// while it is in hand it is a layer nearer the eye, the pixels it covers are among those the pass over what
	// bends runs on, and the flask's liquid is seen through it. Set down, it stands again.
	namespace L = lab::optics_layout;
	Player p;
	p.drag("item:flask", "square:11,12");
	p.drag("item:acid", "square:5,12");
	std::string why;
	REQUIRE_MESSAGE(p.ui->lab().command(R"({"do": "pour", "from": "acid", "to": "flask", "ml": 120})", why), why);
	p.drag("item:jar", "square:16,12");
	const auto body_of = [&](const lab::OpticsScene &scene, float layer_wanted) {
		// the scene's bodies carry no names: the jar is the narrowest rectangle that refracts on its layer
		int found = -1;
		for (int b = 0; b < static_cast<int>(scene.table[0]); ++b) {
			const float *rect = &scene.table[static_cast<size_t>(L::HEADER_VECS + b * L::BODY_VECS + L::RECT) * 4];
			const float *refract = &scene.table[static_cast<size_t>(L::HEADER_VECS + b * L::BODY_VECS + L::REFRACT) * 4];
			if (refract[3] > 0.5f && refract[1] == layer_wanted && (found < 0 || rect[2] < scene.table[static_cast<size_t>(L::HEADER_VECS + found * L::BODY_VECS + L::RECT) * 4 + 2])) {
				found = b;
			}
		}
		return found;
	};
	// everything stands: one layer
	CHECK(p.ui->optics().table[3 * 4 + 1] == 0.0f);
	float jx = 0, jy = 0, fx = 0, fy = 0;
	p.at("item:jar", jx, jy);
	p.at("item:flask", fx, fy);
	// pressed, not yet let go: it still stands
	p.ui->pointer(jx, jy, false);
	p.ui->pointer(jx, jy, true);
	p.ui->frame(1.0 / 60.0);
	CHECK(p.ui->optics().table[3 * 4 + 1] == 0.0f);
	p.ui->pointer(jx, jy, false); // let go over it: picked up (Stage 27)
	// carried, a person's pace: about four pixels a frame, toward the flask, no button held
	const int frames = 50;
	for (int k = 1; k <= frames; ++k) {
		p.ui->pointer(jx + (fx - jx) * static_cast<float>(k) / frames, jy + (fy - jy) * static_cast<float>(k) / frames, false);
		p.ui->frame(1.0 / 60.0);
		REQUIRE(p.ui->optics().table[3 * 4 + 1] == 1.0f); // in hand all the way
	}
	// held still over the flask
	for (int k = 0; k < 10; ++k) {
		p.ui->frame(1.0 / 60.0);
	}
	const lab::OpticsScene held = p.ui->optics();
	REQUIRE(lab::check_O1(held) == "");
	const int jar = body_of(held, 1.0f);
	REQUIRE(jar >= 0);
	CHECK(jar == static_cast<int>(held.table[0]) - 1); // painted over everything that stands
	// its rectangle is within one of those the pass over what bends runs on
	const float *rect = &held.table[static_cast<size_t>(L::HEADER_VECS + jar * L::BODY_VECS + L::RECT) * 4];
	bool within = false;
	for (size_t k = 0; k + 3 < held.chain.size(); k += 4) {
		within = within || (held.chain[k] <= rect[0] && held.chain[k + 1] <= rect[1] && held.chain[k + 2] >= rect[0] + rect[2] && held.chain[k + 3] >= rect[1] + rect[3]);
	}
	CHECK(within);
	// through the jar's glass, low in it, the flask's blue liquid behind: less of the red than beside the flask
	const int px = static_cast<int>(rect[0] + rect[2] / 2), py = static_cast<int>(rect[1] + rect[3]) - 12;
	const double through_both = lab::optics_through(held, px, py, 650.0);
	CHECK(lab::optics_through(held, px, py, 650.0) == doctest::Approx(lab::optics_area_through(held, px, py, 650.0, 8)).epsilon(0.05));
	// put back (Escape, Stage 27): it stands again, on the bench's layer
	p.ui->key(lab::Ui::KeyEscape);
	p.ui->frame(1.0 / 60.0);
	CHECK(p.ui->optics().table[3 * 4 + 1] == 0.0f);
	MESSAGE("carried over the flask: through the jar and the flask's liquid at 650 nm, " << through_both << " of the room's light");
	well_formed(p.ui->draw());
}

TEST_CASE("ui: everything on the bench that takes space is a body of the optical pass: a burner and its flame, parts, their lamps and ports, wires, rigs (Stage 22)") {
	namespace L = lab::optics_layout;
	Player p;
	p.drag("item:flask", "square:11,12");
	p.drag("item:burner", "item:flask");
	std::string why;
	for (const char *c : { R"({"do": "valve", "burner": "burner", "to": 0.7})", R"({"do": "part", "kind": "monitor", "at": [1, 1]})", R"({"do": "part", "kind": "alarm", "at": [4, 1]})",
				 R"({"do": "wire", "from": "monitor1", "to": "alarm1"})", R"({"do": "rig", "from": "monitor1", "item": "burner", "control": "valve", "on": 0})" }) {
		REQUIRE_MESSAGE(p.ui->lab().command(c, why), why);
	}
	p.ui->frame(1.0 / 60.0);
	const lab::OpticsScene &scene = p.ui->optics();
	REQUIRE(lab::check_O1(scene) == "");
	int slabs = 0, bars = 0, cones = 0, vessels = 0;
	for (int b = 0; b < static_cast<int>(scene.table[0]); ++b) {
		const float kind = scene.table[static_cast<size_t>(L::HEADER_VECS + b * L::BODY_VECS + L::FORM) * 4];
		slabs += kind == 3.0f ? 1 : 0;
		bars += kind == 4.0f ? 1 : 0;
		cones += kind == 5.0f ? 1 : 0;
		vessels += kind == 0.0f ? 1 : 0;
	}
	CHECK(vessels == 6); // the flask, and the vessels in the cabinet, as they always were
	CHECK(cones == 1); // the burner's flame
	CHECK(slabs == 1 + 2 * 2 + 1 + 1 + 1 + 1 + 1); // the burner; two faces and two lamps; the monitor's port and sensing port; the rig's knob; the gauze (Stage 27); the stopper, in the cabinet (Stage 28)
	CHECK(bars == 3 + 2); // the wire and the rig; the condenser's outlet, in the cabinet; the tripod's two legs (Stage 27)
	// the flame's light is the burner's valve's: half way to blue at 0.7, between the sooty and the blue
	MESSAGE("bodies: " << vessels << " vessel, " << slabs << " slabs, " << bars << " bars, " << cones << " cone");
	// and none of it is drawn over the pass: no polygon (a flame) anywhere, no circle (a lamp, a port, a knob) on
	// a part or the burner
	float mx = 0, my = 0, ax = 0, ay = 0, bx = 0, by = 0;
	p.at("item:monitor1", mx, my);
	p.at("item:alarm1", ax, ay);
	p.at("item:burner", bx, by);
	const lab::DrawList &list = p.ui->draw();
	for (size_t k = static_cast<size_t>(list.above); k < list.commands.size(); ++k) {
		const lab::DrawCommand &c = list.commands[k];
		CHECK(c.type != lab::DrawCommand::Polygon);
		if (c.type == lab::DrawCommand::Circle) {
			for (const auto &[x, y] : { std::pair{ mx, my }, std::pair{ ax, ay }, std::pair{ bx, by } }) {
				CHECK((std::fabs(c.x - x) > 45.0f || std::fabs(c.y - y) > 45.0f));
			}
		}
	}
}

TEST_CASE("ui: nothing of the lab is drawn over the optical pass: on the bench only names, outlines and highlights cross what stands there (Stage 22)") {
	// A busy bench: a lit burner under the flask, a condenser on it and a beaker under its outlet, a monitor rigged
	// to the burner and wired to an alarm. Nothing chosen. What the screen draws over the pass that falls on what
	// stands on the bench is text, an outline or a line (a highlight), never a fill, a polygon or a circle.
	Player p;
	p.drag("item:flask", "square:11,12");
	p.drag("item:burner", "item:flask");
	p.drag("item:condenser", "item:flask");
	p.drag("item:beaker", "square:16,12");
	std::string why;
	for (const char *c : { R"({"do": "valve", "burner": "burner", "to": 0.5})", R"({"do": "part", "kind": "monitor", "at": [1, 1]})", R"({"do": "part", "kind": "alarm", "at": [4, 1]})",
				 R"({"do": "wire", "from": "monitor1", "to": "alarm1"})", R"({"do": "rig", "from": "monitor1", "item": "burner", "control": "valve", "on": 0})" }) {
		REQUIRE_MESSAGE(p.ui->lab().command(c, why), why);
	}
	p.ui->key(lab::Ui::KeyEscape); // nothing chosen
	p.ui->frame(1.0 / 60.0);
	float x0 = 0, y0 = 0, x1 = 0, y1 = 0;
	p.at("square:0,0", x0, y0);
	p.at("square:1,0", x1, y1);
	const float square = x1 - x0, left = x0 - square / 2, top = y0 - square / 2;
	std::vector<lab::ItemView> standing;
	for (const lab::ItemView &v : p.ui->lab().view().items) {
		if (v.place == "bench" || v.place == "under" || v.place == "on") {
			standing.push_back(v);
		}
	}
	REQUIRE(standing.size() >= 6);
	const lab::DrawList &list = p.ui->draw();
	int crossing = 0;
	for (size_t k = static_cast<size_t>(list.above); k < list.commands.size(); ++k) {
		const lab::DrawCommand &c = list.commands[k];
		if (c.type == lab::DrawCommand::Outline || c.type == lab::DrawCommand::Line) {
			continue; // a highlight
		}
		// nothing is written on the bench while nothing is chosen (a text's box: its baseline, its size above it)
		const bool text = c.type == lab::DrawCommand::Text;
		const float cx0 = c.type == lab::DrawCommand::Circle ? c.x - c.w : c.x, cy0 = c.type == lab::DrawCommand::Circle ? c.y - c.w : text ? c.y - c.h : c.y;
		const float cx1 = c.type == lab::DrawCommand::Circle ? c.x + c.w : text ? c.x + 4.0f : c.x + c.w, cy1 = c.type == lab::DrawCommand::Circle ? c.y + c.w : text ? c.y : c.y + c.h;
		for (const lab::ItemView &v : standing) {
			if (v.col < 0) {
				continue;
			}
			const float fx = left + static_cast<float>(v.col) * square, fy = top + static_cast<float>(v.row) * square;
			if (cx0 < fx + static_cast<float>(v.w) * square && cx1 > fx && cy0 < fy + static_cast<float>(v.h) * square && cy1 > fy) {
				CAPTURE(v.id);
				CAPTURE(c.type);
				CAPTURE(c.x);
				CAPTURE(c.y);
				CHECK(false);
				++crossing;
			}
		}
	}
	CHECK(crossing == 0);
	// chosen, a part says its name and what it is set to, above it; and each kind of part is its own colour
	p.click("item:monitor1");
	p.ui->frame(1.0 / 60.0);
	const lab::DrawList &chosen = p.ui->draw();
	bool named = false;
	for (size_t k = static_cast<size_t>(chosen.above); k < chosen.commands.size(); ++k) {
		const lab::DrawCommand &c = chosen.commands[k];
		named = named || (c.type == lab::DrawCommand::Text && std::string(&chosen.text[static_cast<size_t>(c.first)]).rfind("monitor1", 0) == 0);
	}
	CHECK(named);
	namespace L = lab::optics_layout;
	const lab::OpticsScene &scene = p.ui->optics();
	std::vector<float> faces; // the sets of the faces of a monitor and an alarm
	for (int b = 0; b < static_cast<int>(scene.table[0]); ++b) {
		const float *rect = &scene.table[static_cast<size_t>(L::HEADER_VECS + b * L::BODY_VECS + L::RECT) * 4];
		if (scene.table[static_cast<size_t>(L::HEADER_VECS + b * L::BODY_VECS + L::FORM) * 4] == 3.0f && rect[2] > 30.0f && rect[3] > 30.0f && rect[3] < 40.0f) {
			faces.push_back(scene.table[static_cast<size_t>(L::HEADER_VECS + b * L::BODY_VECS + L::SET) * 4]);
		}
	}
	REQUIRE(faces.size() == 2);
	CHECK(faces[0] != faces[1]);
}

TEST_CASE("ui: a timer's time left is a strip of light on it that shrinks as it counts down (Stage 22)") {
	namespace L = lab::optics_layout;
	Player p;
	std::string why;
	for (const char *c : { R"({"do": "part", "kind": "timer", "at": [1, 1]})", R"({"do": "set", "part": "timer1", "seconds": 10})" }) {
		REQUIRE_MESSAGE(p.ui->lab().command(c, why), why);
	}
	const auto lit = [&] {
		p.ui->frame(1.0 / 60.0);
		const lab::OpticsScene &scene = p.ui->optics();
		float widest = 0.0f;
		for (int b = 0; b < static_cast<int>(scene.table[0]); ++b) {
			const float *rect = &scene.table[static_cast<size_t>(L::HEADER_VECS + b * L::BODY_VECS + L::RECT) * 4];
			const int set = static_cast<int>(scene.table[static_cast<size_t>(L::HEADER_VECS + b * L::BODY_VECS + L::SET) * 4]);
			const float glow = scene.rows[static_cast<size_t>((set * L::ROWS_PER_SET + L::GLOW_ROW) * L::ROW + 150)]; // 530 nm
			if (rect[3] == 4.0f && glow > 0.5f) {
				widest = std::max(widest, rect[2]);
			}
		}
		return widest;
	};
	const float full = lit();
	CHECK(full > 20.0f);
	REQUIRE_MESSAGE(p.ui->lab().command(R"({"do": "wait", "seconds": 5})", why), why);
	CHECK(lit() == doctest::Approx(full / 2.0f).epsilon(0.05));
	REQUIRE_MESSAGE(p.ui->lab().command(R"({"do": "wait", "seconds": 6})", why), why);
	CHECK(lit() == 0.0f);
}

TEST_CASE("ui: an item's controls are in the column on the right, for the item chosen; a person opens the valve and stirs at their pace (Stage 22)") {
	Player p;
	p.drag("item:flask", "square:11,12");
	p.drag("item:burner", "item:flask");
	std::string why;
	REQUIRE_MESSAGE(p.ui->lab().command(R"({"do": "pour", "from": "acid", "to": "flask", "ml": 60})", why), why);
	float x = 0, y = 0;
	// nothing chosen: no control is on the screen
	p.ui->key(lab::Ui::KeyEscape);
	CHECK_FALSE(p.ui->locate("valve:burner@0.5", x, y));
	CHECK_FALSE(p.ui->locate("stir:flask", x, y));
	// the burner chosen: its valve, in the readout's column, not on the bench
	p.click("item:burner");
	REQUIRE(p.ui->locate("valve:burner@0", x, y));
	float bx = 0, by = 0;
	p.at("square:27,0", bx, by);
	CHECK(x > bx + 20.0f);
	CHECK_FALSE(p.ui->locate("stir:flask", x, y));
	// a person drags it open, a few pixels a frame
	float sx = 0, sy = 0, ex = 0, ey = 0;
	p.at("valve:burner@0", sx, sy);
	p.at("valve:burner@0.8", ex, ey);
	p.ui->pointer(sx, sy, false);
	p.ui->pointer(sx, sy, true);
	for (int k = 1; k <= 40; ++k) {
		p.ui->pointer(sx + (ex - sx) * static_cast<float>(k) / 40.0f, sy, true);
		p.ui->frame(1.0 / 60.0);
	}
	p.ui->pointer(ex, ey, false);
	CHECK(p.item("burner").valve == doctest::Approx(0.8).epsilon(0.02));
	// the flask chosen: its stirring, and no valve
	p.click("item:flask");
	CHECK_FALSE(p.ui->locate("valve:burner@0.5", x, y));
	REQUIRE(p.ui->locate("stir:flask", x, y));
	p.click("stir:flask");
	CHECK(p.item("flask").stirred);
	p.ui->frame(1.0 / 60.0);
	p.click("stir:flask");
	CHECK_FALSE(p.item("flask").stirred);
	well_formed(p.ui->draw());
}

TEST_CASE("ui: a refusal is shown, and nothing changes (Stage 9)") {
	Player p;
	p.drag("item:flask", "square:11,12");
	p.drag("item:balloon", "item:flask");
	CHECK(p.item("balloon").place == "on");
	const std::string before = p.ui->lab().digest();
	p.drag("item:condenser", "item:flask"); // the mouth is taken
	CHECK(p.ui->message().find("taken") != std::string::npos);
	CHECK(p.item("condenser").place == "cabinet");
	p.drag("item:flask", "outbox"); // still joined
	CHECK(p.ui->message().find("joined") != std::string::npos);
	CHECK(p.item("flask").place == "bench");
	CHECK(p.ui->lab().digest() == before);
	p.drag("item:balloon", "square:3,4"); // off again, set down where it was dropped, held by its body
	CHECK(p.item("balloon").place == "bench");
	CHECK(p.item("balloon").col == 1);
	CHECK(p.ui->message().empty());
}

TEST_CASE("ui: the clock: pause, speeds, and one checked step in every frame (Stage 9)") {
	Player p;
	CHECK(p.ui->speed() == 1.0);
	p.ui->frame(0.5);
	CHECK(p.ui->lab().step() == 30);
	p.click("pause");
	CHECK(p.ui->paused());
	p.ui->frame(0.5);
	CHECK(p.ui->lab().step() == 30);
	p.click("speed:600"); // choosing a speed also resumes
	CHECK_FALSE(p.ui->paused());
	p.ui->frame(1.0 / 60.0);
	CHECK(p.ui->lab().step() == 630);
	p.ui->frame(10.0); // a long frame is capped at twenty simulated seconds
	CHECK(p.ui->lab().step() == 1830);
	p.click("check");
	p.ui->frame(1.0 / 60.0); // every step checked
	CHECK(p.ui->lab().step() == 2430);
	CHECK_FALSE(p.ui->lab().failed());
	p.click("reset");
	CHECK(p.ui->lab().step() == 0);
	CHECK(p.item("flask").place == "cabinet");
}

TEST_CASE("ui: the three stages of the demo, played with the pointer alone (Stage 9)") {
	Player p;
	// ---- stage 1: gas from malachite
	p.drag("item:flask", "square:11,12");
	p.drag("item:burner", "item:flask");
	CHECK(p.item("burner").place == "under");
	p.drag("item:jar", "square:4,13");
	p.drag("item:jar", "item:flask");
	p.click("panel:tip:malachite");
	CHECK(p.grams("flask", "malachite") == doctest::Approx(5.0).epsilon(1e-9));
	p.drag("item:balloon", "item:flask");
	p.click("valve:burner@1");
	CHECK(p.item("burner").valve == doctest::Approx(1.0).epsilon(0.02));
	p.click("speed:600");
	p.watch([&] { return p.grams("flask", "malachite") < 0.005; }, 1800);
	p.drag("item:balloon", "outbox"); // off the flask, sealed, and handed in
	CHECK(p.item("balloon").submitted);
	CHECK(p.item("flask").holds == -1);
	p.click("valve:burner@0");
	p.watch([&] { return p.item("flask").celsius < 30.0; }, 1800);

	// ---- stage 2: copper sulfate solution
	p.drag("item:acid", "square:7,12");
	p.drag("item:acid", "item:flask");
	p.click("panel:pour"); // the panel opens at 50 mL
	CHECK(p.item("flask").liquid_ml == doctest::Approx(50.0).epsilon(0.01));
	p.click("stir:flask");
	CHECK(p.item("flask").stirred);
	p.click("valve:burner@0.2");
	p.watch([&] { return p.grams("flask", "copper_oxide") < 0.001; }, 3600);
	p.click("stir:flask");
	CHECK_FALSE(p.item("flask").stirred);
	CHECK(p.grams("flask", "copper_sulfate_aq") > 7.0); // blue

	// ---- stage 3: water and crystals
	p.drag("item:condenser", "item:flask");
	p.drag("item:beaker", "square:19,12");
	// (Stage 34) a vessel carried onto the condenser is not led under its outlet: the line is drawn from the outlet
	p.drag("item:beaker", "item:condenser");
	CHECK(p.item("condenser").drains_to < 0);
	CHECK(p.ui->message().find("outlet") != std::string::npos);
	p.drag("outlet:condenser", "item:beaker");
	CHECK(p.item("condenser").drains_to >= 0);
	p.click("valve:burner@1");
	p.watch([&] { return p.grams("flask", "water") < 10.0; }, 1800);
	p.click("valve:burner@0");
	p.watch([&] { return p.item("flask").celsius < 21.0; }, 3600);
	p.wait(7200); // the crystals grow for two hours (Stage 23: measured nucleation and transport)
	p.drag("item:condenser", "square:23,3");
	p.drag("item:waste", "square:23,12");
	p.drag("item:flask", "item:waste");
	p.click("panel:slider@1");
	p.click("panel:pour");
	CHECK(p.item("flask").liquid_ml == 0.0);
	p.drag("item:beaker", "outbox");
	p.drag("item:flask", "outbox");
	CHECK(p.ui->message().empty());

	// ---- GAME_SPEC §17: done when
	const LabView view = p.ui->lab().view();
	CHECK(view.conserved);
	CHECK(view.events.empty());
	CHECK(p.item("balloon").submitted);
	CHECK(p.item("beaker").submitted);
	CHECK(p.item("flask").submitted);
	CHECK(p.grams("balloon", "carbon_dioxide") > 0.8);
	CHECK(p.grams("beaker", "water") > 35.0);
	for (const lab::SubstanceView &s : p.item("beaker").contents) {
		CHECK((s.species == "water" || s.species == "steam" || s.species == "nitrogen" || s.species == "oxygen" || s.species == "glass"));
	}
	const double crystals = p.grams("flask", "copper_sulfate_pentahydrate");
	CHECK(crystals > 8.0);
	CHECK(p.grams("flask", "copper_sulfate") == 0.0);
	CHECK(p.grams("flask", "sulfuric_acid_aq") == 0.0);
	CHECK(crystals / (crystals + p.grams("flask", "malachite") + p.grams("flask", "copper_oxide")) > 0.999);
	well_formed(p.ui->draw());
	MESSAGE("played with the pointer: " << p.grams("balloon", "carbon_dioxide") << " g of CO2, " << p.grams("beaker", "water") << " g of water, " << crystals
										<< " g of crystals, in " << view.step / 60 << " s");
}

TEST_CASE("ui: items are chosen by a click, a click with a key held, or a rectangle, and the chosen move together (Stage 13)") {
	Player p;
	p.click("pause");
	p.drag("item:flask", "square:11,12"); // [10, 8]
	p.drag("item:beaker", "square:19,12"); // [18, 8]
	p.drag("item:jar", "square:4,13"); // [3, 9]
	using V = std::vector<std::string>;
	p.click("item:flask");
	CHECK(p.ui->selection() == V{ "flask" });
	p.click_with("item:beaker", lab::Ui::ModShift);
	CHECK(p.ui->selection() == V{ "flask", "beaker" });
	CHECK(p.ui->selected() == "beaker");
	p.click_with("item:flask", lab::Ui::ModCtrl); // taken away again
	CHECK(p.ui->selection() == V{ "beaker" });
	p.click_with("item:jar", lab::Ui::ModMeta);
	CHECK(p.ui->selection() == V{ "beaker", "jar" });
	p.ui->key(lab::Ui::KeyEscape);
	CHECK(p.ui->selection().empty());
	// a rectangle from an empty square, across the flask's footprint and the beaker's, not the jar's
	float x0 = 0, y0 = 0, x1 = 0, y1 = 0;
	p.at("square:9,1", x0, y0);
	p.at("square:19,9", x1, y1);
	p.sweep(x0, y0, x1, y1);
	CHECK(p.ui->selection() == V{ "flask", "beaker" });
	p.click("square:1,1"); // a click on nothing lets go
	CHECK(p.ui->selection().empty());
	p.sweep(x0, y0, x1, y1);
	// carried together, a frame at a time: three squares up (picked up by a click on one of them, put down by
	// another: Stage 27)
	float bx = 0, by = 0;
	p.at("item:beaker", bx, by);
	const auto carry = [&](float dx, float dy) {
		p.ui->pointer(bx, by, true);
		p.ui->pointer(bx, by, false);
		for (int i = 1; i <= 20; ++i) {
			p.ui->pointer(bx + dx * static_cast<float>(i) / 20.0f, by + dy * static_cast<float>(i) / 20.0f, false);
			p.ui->frame(1.0 / 60.0);
		}
		p.ui->pointer(bx + dx, by + dy, true);
		p.ui->pointer(bx + dx, by + dy, false);
	};
	carry(0, -120);
	CHECK(p.item("beaker").col == 18);
	CHECK(p.item("beaker").row == 5);
	CHECK(p.item("flask").col == 10);
	CHECK(p.item("flask").row == 5);
	CHECK(p.ui->message().empty());
	// the flask would land on the jar: neither moves, and the reason is shown
	p.at("item:beaker", bx, by);
	carry(-280, 120);
	CHECK(p.item("beaker").col == 18);
	CHECK(p.item("flask").col == 10);
	CHECK(p.ui->message().find("stands where \"flask\" would go") != std::string::npos);
	CHECK(p.ui->selection() == V{ "flask", "beaker" }); // still chosen
	// back to the cabinet together
	p.drag("item:flask", "cabinet");
	CHECK(p.item("flask").place == "cabinet");
	CHECK(p.item("beaker").place == "cabinet");
	CHECK(p.item("jar").place == "bench");
	well_formed(p.ui->draw());
}

TEST_CASE("ui: what is not wanted goes in the trash, carried there or by Backspace; the last put there stands in it and is taken up again, and the undo key brings it back where it stood (Stage 34)") {
	Player p;
	p.click("pause");
	p.drag("item:flask", "square:11,12");
	p.drag("item:beaker", "square:19,12");
	const auto in_trash = [&] { return p.ui->lab().view().trash.size(); };
	float tx = 0, ty = 0, x = 0, y = 0;
	p.at("trash", tx, ty);
	// carried to the trash and put down there
	p.drag("item:beaker", "trash");
	CHECK(p.item("beaker").place == "trash");
	CHECK(in_trash() == 1);
	REQUIRE(p.ui->locate("item:beaker", x, y)); // it stands in the trash's box
	CHECK(std::fabs(x - tx) < 110.0f);
	CHECK(y > ty - 30.0f);
	well_formed(p.ui->draw());
	p.ui->optics();
	// chosen (a click with Shift held) and Backspace: the flask too; only the last put there is on the screen
	const int flask_col = p.item("flask").col, flask_row = p.item("flask").row;
	p.click_with("item:flask", lab::Ui::ModShift);
	REQUIRE(p.ui->selection() == std::vector<std::string>{ "flask" });
	p.ui->key(lab::Ui::KeyBackspace);
	CHECK(p.item("flask").place == "trash");
	CHECK(in_trash() == 2);
	CHECK(p.ui->selection().empty());
	CHECK(p.ui->locate("item:flask", x, y));
	CHECK_FALSE(p.ui->locate("item:beaker", x, y));
	// the undo key: the flask stands where it stood, chosen; the beaker is the trash's again
	p.ui->key(lab::Ui::KeyUndo);
	CHECK(p.item("flask").place == "bench");
	CHECK(p.item("flask").col == flask_col);
	CHECK(p.item("flask").row == flask_row);
	CHECK(p.ui->selected() == "flask");
	CHECK(in_trash() == 1);
	CHECK(p.ui->locate("item:beaker", x, y));
	// taken up out of the trash as out of the cabinet, and put down on a free square
	p.ui->key(lab::Ui::KeyEscape);
	p.drag("item:beaker", "square:16,12");
	CHECK(p.item("beaker").place == "bench");
	CHECK(in_trash() == 0);
	// in hand, and Backspace: what is carried goes
	p.at("item:beaker", x, y);
	p.ui->pointer(x, y, true);
	p.ui->pointer(x, y, false);
	p.ui->pointer(x + 40.0f, y - 30.0f, false);
	p.ui->key(lab::Ui::KeyBackspace);
	CHECK(p.item("beaker").place == "trash");
	// out of the trash into the cabinet
	p.drag("item:beaker", "cabinet");
	CHECK(p.item("beaker").place == "cabinet");
	// a part: made, its editor closed, chosen, and Backspace: in the trash with its settings; the undo key brings it back
	p.drag("drawer:timer", "square:2,2");
	p.ui->key(lab::Ui::KeyBackspace); // (its editor is open: this is typed into it, and trashes nothing)
	CHECK(p.item("timer1").place == "bench");
	p.ui->key(lab::Ui::KeyEscape);
	p.click_with("item:timer1", lab::Ui::ModShift);
	if (p.ui->selection().empty()) {
		p.click_with("item:timer1", lab::Ui::ModShift); // (chosen already when made: the click took it out)
	}
	REQUIRE(p.ui->selection() == std::vector<std::string>{ "timer1" });
	p.ui->key(lab::Ui::KeyBackspace);
	CHECK(p.item("timer1").place == "trash");
	CHECK(p.ui->locate("item:timer1", x, y));
	well_formed(p.ui->draw());
	p.ui->optics();
	p.ui->key(lab::Ui::KeyUndo);
	CHECK(p.item("timer1").place == "bench");
	CHECK(p.item("timer1").col == 2);
	// nothing in the trash: the undo key says so
	p.ui->key(lab::Ui::KeyUndo);
	CHECK(p.ui->message().find("trash is empty") != std::string::npos);
}

TEST_CASE("ui: a condenser's outlet is led to a vessel by a line drawn from it, to another by another, and let go on nothing the line comes off (Stage 34)") {
	Player p;
	p.click("pause");
	p.drag("item:flask", "square:11,12");
	p.drag("item:condenser", "item:flask");
	p.drag("item:beaker", "square:19,12");
	p.drag("item:acid", "square:16,12");
	REQUIRE(p.item("condenser").on >= 0);
	float ox = 0, oy = 0, bx = 0, by = 0;
	p.at("outlet:condenser", ox, oy);
	p.at("item:beaker", bx, by);
	// taken by its outlet and drawn to the beaker: a line follows the pointer, and the outlet runs into the beaker
	p.ui->pointer(ox, oy, true);
	p.ui->pointer(0.5f * (ox + bx), 0.5f * (oy + by), true);
	well_formed(p.ui->draw());
	p.ui->pointer(bx, by, true);
	p.ui->pointer(bx, by, false);
	const auto into = [&] {
		const int to = p.item("condenser").drains_to;
		return to >= 0 ? p.ui->lab().view().items[static_cast<size_t>(to)].id : std::string();
	};
	CHECK(into() == "beaker");
	CHECK(p.item("beaker").place == "bench"); // (the beaker stays where it stands)
	// drawn again to the bottle: taken from the beaker, led to the bottle
	p.drag("outlet:condenser", "item:acid");
	CHECK(into() == "acid");
	// to a thing that is no vessel: said, and as it was
	p.drag("item:burner", "square:4,12");
	p.drag("outlet:condenser", "item:burner");
	CHECK(into() == "acid");
	CHECK(p.ui->message().find("vessel") != std::string::npos);
	// let go on nothing: the line comes off
	p.drag("outlet:condenser", "square:2,3");
	CHECK(into().empty());
	// a session's command takes it off too, and says when there is nothing to take
	p.drag("outlet:condenser", "item:beaker");
	std::string why;
	CHECK(p.ui->lab().command(R"({"do": "undrain", "of": "condenser"})", why));
	CHECK(into().empty());
	CHECK_FALSE(p.ui->lab().command(R"({"do": "undrain", "of": "condenser"})", why));
	CHECK(why.find("nothing under its outlet") != std::string::npos);
}

TEST_CASE("ui: a part is taken from the drawer with a press and put down with another, as an item from the cabinet is; Escape puts it back (Stage 34)") {
	Player p;
	p.click("pause");
	const auto parts = [&] {
		int n = 0;
		for (const ItemView &v : p.ui->lab().view().items) {
			n += v.kind == "timer" || v.kind == "alarm" ? 1 : 0;
		}
		return n;
	};
	float dx = 0, dy = 0, sx = 0, sy = 0;
	p.at("drawer:timer", dx, dy);
	p.at("square:2,2", sx, sy);
	// a press and its release on the drawer: nothing is made yet, the part is in hand and follows the pointer
	p.ui->pointer(dx, dy, true);
	p.ui->pointer(dx, dy, false);
	CHECK(parts() == 0);
	p.ui->pointer(0.5f * (dx + sx), 0.5f * (dy + sy), false);
	p.ui->pointer(sx, sy, false);
	CHECK(parts() == 0);
	// the next press and release, on a square: there it is, chosen, its editor open
	p.ui->pointer(sx, sy, true);
	CHECK(parts() == 0);
	p.ui->pointer(sx, sy, false);
	CHECK(parts() == 1);
	CHECK(p.ui->selected() == "timer1");
	p.ui->key(lab::Ui::KeyEscape);
	// another, and Escape with it in hand: back in the drawer, and the next press makes nothing
	p.at("drawer:alarm", dx, dy);
	p.ui->pointer(dx, dy, true);
	p.ui->pointer(dx, dy, false);
	p.ui->key(lab::Ui::KeyEscape);
	p.at("square:5,2", sx, sy);
	p.ui->pointer(sx, sy, true);
	p.ui->pointer(sx, sy, false);
	CHECK(parts() == 1);
	// in hand, and put down off the bench: it goes back in the drawer
	p.ui->pointer(dx, dy, true);
	p.ui->pointer(dx, dy, false);
	p.ui->pointer(dx, dy, true);
	p.ui->pointer(dx, dy, false);
	CHECK(parts() == 1);
	// and dragged out as before: where the button is let go
	p.drag("drawer:alarm", "square:5,2");
	CHECK(parts() == 2);
	well_formed(p.ui->draw());
}

TEST_CASE("ui: parts come out of the drawer, are set, wired and rigged with the pointer and the keys, and go back (Stage 13)") {
	Player p;
	p.click("pause");
	p.drag("item:flask", "square:11,12");
	p.drag("item:burner", "item:flask");
	p.drag("item:jar", "square:4,13");
	using V = std::vector<std::string>;
	const auto kinds = [&] {
		V out;
		for (const ItemView &v : p.ui->lab().view().items) {
			if (v.kind == "timer" || v.kind == "monitor" || v.kind == "alarm") {
				out.push_back(v.id);
			}
		}
		return out;
	};
	float x = 0, y = 0;
	// a monitor: it senses the flask, and its editor opens; the flask's temperature, between 30 and 40.5 C, typed and nudged
	p.drag("drawer:monitor", "square:1,1");
	CHECK(kinds() == V{ "monitor1" });
	CHECK(p.ui->selected() == "monitor1");
	CHECK(p.ui->message().find("sensing port") != std::string::npos); // it senses nothing yet: how to make it
	p.drag("sense:monitor1", "item:flask");
	p.click("edit:kind:vessel");
	p.click("edit:quantity:celsius");
	p.click("edit:compare:between");
	p.click("edit:value:0");
	p.ui->key(lab::Ui::KeyText, '3');
	p.ui->key(lab::Ui::KeyText, '0');
	p.click("edit:value:1");
	for (const char c : std::string("40")) {
		p.ui->key(lab::Ui::KeyText, static_cast<uint32_t>(c));
	}
	p.click("edit:nudge:-1"); // the second value, focused: 40 + 0.1 * 5
	p.click("edit:nudge:1");
	p.click("edit:nudge:1");
	p.click("edit:nudge:1");
	p.click("edit:nudge:1");
	p.click("edit:nudge:1");
	p.click("edit:set");
	CHECK(p.ui->message().empty());
	ItemView m = p.item("monitor1");
	CHECK(m.part.watching);
	CHECK(m.part.quantity == lab::Quantity{ "flask", "celsius", "g" });
	CHECK(m.part.comparison.op == lab::Comparison::Between);
	CHECK(m.part.comparison.a == doctest::Approx(30.0));
	CHECK(m.part.comparison.b == doctest::Approx(40.4));
	CHECK_FALSE(p.ui->locate("edit:set", x, y)); // closed
	// edited again: a value that is not a number is refused, and the editor stays
	p.click("item:monitor1");
	p.click("part:edit");
	p.click("edit:value:0");
	p.ui->key(lab::Ui::KeyText, '-');
	p.ui->key(lab::Ui::KeyText, '.');
	p.ui->key(lab::Ui::KeyEnter);
	CHECK(p.ui->message().find("not a number") != std::string::npos);
	CHECK(p.ui->locate("edit:set", x, y));
	p.ui->key(lab::Ui::KeyEscape);
	CHECK_FALSE(p.ui->locate("edit:set", x, y));
	CHECK(p.item("monitor1").part.comparison.a == doctest::Approx(30.0)); // unchanged
	// a timer: typed, as hh:mm:ss.mmm or seconds
	p.drag("drawer:timer", "square:2,1");
	p.click("edit:value");
	for (const char c : std::string("0:01:30")) {
		p.ui->key(lab::Ui::KeyText, static_cast<uint32_t>(c));
	}
	p.click("edit:nudge:-1");
	p.click("edit:set");
	CHECK(p.item("timer1").part.preset == 89 * 60);
	// an alarm, and a wire into it; a wire onto a timer asks which of its inputs
	p.drag("drawer:alarm", "square:3,1");
	CHECK(kinds() == V{ "monitor1", "timer1", "alarm1" });
	CHECK_FALSE(p.ui->locate("edit:set", x, y)); // an alarm has nothing to set
	p.drag("port:monitor1", "item:alarm1");
	p.drag("port:monitor1", "item:timer1");
	p.click("choose:wire");
	CHECK(p.ui->lab().view().wires == std::vector<lab::Wire>{ { "monitor1", "alarm1" }, { "monitor1", "timer1" } });
	p.drag("port:timer1", "item:timer1"); // onto its own reset
	p.click("choose:reset");
	CHECK(p.ui->locate("edit:on:reset", x, y));
	p.click("edit:set");
	// a rig onto the burner's valve: on any change, 25 %
	p.drag("port:monitor1", "item:burner");
	p.click("edit:form:change");
	p.click("edit:change:value");
	p.ui->key(lab::Ui::KeyText, '2');
	p.ui->key(lab::Ui::KeyText, '0');
	p.click("edit:change:nudge:1");
	p.click("edit:change:nudge:1");
	p.click("edit:change:nudge:1");
	p.click("edit:change:nudge:1");
	p.click("edit:change:nudge:1");
	p.click("edit:set");
	// and onto the flask's stirring: stir when on, leave when off
	p.drag("port:monitor1", "item:flask");
	p.click("edit:off:leave");
	p.click("edit:set");
	const std::vector<lab::Rig> rigs = p.ui->lab().view().rigs;
	REQUIRE(rigs.size() == 3);
	CHECK(rigs[0].item == "timer1");
	CHECK(rigs[0].control == "reset");
	CHECK(rigs[1].change);
	CHECK(*rigs[1].any == doctest::Approx(0.25));
	CHECK(*rigs[2].on == 1.0);
	CHECK_FALSE(rigs[2].off.has_value());
	// a wire onto what has no control is refused with the reason
	p.drag("port:monitor1", "item:jar");
	CHECK(p.ui->locate("edit:set", x, y)); // a vessel: its stirring
	p.click("edit:cancel");
	// the readout takes wires and rigs off again
	p.click("item:monitor1");
	p.click("part:unrig:flask:stir");
	p.click("part:unwire:timer1");
	CHECK(p.ui->lab().view().rigs.size() == 2);
	CHECK(p.ui->lab().view().wires.size() == 1);
	// a part dropped on equipment is refused; dragged along the bench, it moves; to the drawer, it goes
	p.drag("item:alarm1", "item:jar");
	CHECK(p.ui->message().find("free square") != std::string::npos);
	p.drag("item:alarm1", "square:5,1");
	CHECK(p.item("alarm1").col == 5);
	p.drag("item:timer1", "cabinet");
	CHECK(kinds() == V{ "monitor1", "alarm1" });
	CHECK(p.ui->lab().view().rigs.size() == 1); // its reset rig went with it
	well_formed(p.ui->draw());
}

TEST_CASE("ui: a person boiling down at 600x: a monitor closes the valve at 10 g, and an alarm stops the clock and shows why (Stage 13)") {
	Player p;
	p.drag("item:flask", "square:11,12");
	p.drag("item:burner", "item:flask");
	p.drag("item:acid", "square:7,12");
	p.drag("item:acid", "item:flask");
	p.click("panel:pour");
	p.drag("drawer:monitor", "square:1,1");
	p.drag("sense:monitor1", "item:flask");
	p.click("edit:kind:material");
	p.click("edit:material:water");
	p.click("edit:measure:g");
	p.click("edit:compare:below");
	p.click("edit:value:0");
	p.ui->key(lab::Ui::KeyText, '1');
	p.ui->key(lab::Ui::KeyText, '0');
	p.click("edit:set");
	p.drag("port:monitor1", "item:burner");
	p.click("edit:on:value");
	p.ui->key(lab::Ui::KeyText, '0');
	p.click("edit:set");
	p.drag("drawer:alarm", "square:3,1");
	p.drag("port:monitor1", "item:alarm1");
	CHECK(p.ui->message().empty());
	p.click("valve:burner@1");
	p.click("speed:600");
	// a person: watching the screen, doing nothing
	for (int frame = 0; !p.ui->paused(); ++frame) {
		REQUIRE_MESSAGE(frame < 60 * 60, "the alarm never rang");
		p.ui->frame(1.0 / 60.0);
	}
	CHECK(p.grams("flask", "water") < 10.0);
	CHECK(p.grams("flask", "water") > 9.9);
	CHECK(p.item("burner").valve == 0.0);
	CHECK(p.ui->message().find("alarm1 rang: monitor1, flask: H2O(l) (g) below 10.000 g") != std::string::npos);
	CHECK(p.ui->selected() == "flask"); // what the monitor watched is shown
	const std::string at_ring = p.ui->lab().digest();
	p.ui->frame(1.0); // paused: nothing moves
	CHECK(p.ui->lab().digest() == at_ring);
	p.click("speed:600"); // playing on answers it
	CHECK(p.ui->lab().view().ringing.empty());
	CHECK_FALSE(p.ui->paused());
	p.ui->frame(1.0);
	CHECK(p.item("burner").valve == 0.0); // and the valve stays as the rig left it
	MESSAGE("the alarm rang at " << p.grams("flask", "water") << " g of water");
}

TEST_CASE("ui: chosen parts become a circuit, opened to work inside, nested, and given back (Stage 14)") {
	Player p;
	p.click("pause");
	float x = 0, y = 0, x1 = 0, y1 = 0;
	const auto make = [&](const char *kind, const char *square) {
		p.drag(std::string("drawer:") + kind, square);
		if (p.ui->locate("edit:cancel", x, y)) {
			p.click("edit:cancel");
		}
	};
	make("timer", "square:2,1");
	make("alarm", "square:4,2");
	make("monitor", "square:9,1");
	p.drag("port:timer1", "item:alarm1");
	CHECK(p.ui->locate("circuit:create", x, y)); // the part just made is chosen
	p.ui->key(lab::Ui::KeyEscape);
	CHECK_FALSE(p.ui->locate("circuit:create", x, y)); // nothing chosen
	// a rectangle around the timer and the alarm
	p.at("square:1,0", x, y);
	p.at("square:5,3", x1, y1);
	p.sweep(x, y, x1, y1);
	CHECK(p.ui->selection() == std::vector<std::string>{ "timer1", "alarm1" });
	p.click("circuit:create");
	CHECK(p.ui->message().empty());
	CHECK(p.ui->selected() == "circuit1");
	CHECK(p.item("circuit1").col == 2);
	CHECK_FALSE(p.ui->locate("item:timer1", x, y)); // inside it, not on the bench
	CHECK(p.ui->locate("item:circuit1", x, y));
	// opened: what is in it is on the screen, and worked on there
	p.click("part:open");
	REQUIRE(p.ui->locate("item:timer1", x, y));
	p.drag("item:timer1", "inner:4,3");
	CHECK(p.item("timer1").col == 4);
	CHECK(p.item("timer1").row == 3);
	p.drag("drawer:monitor", "inner:0,3"); // out of the drawer, into it
	CHECK(p.item("monitor2").part.in == "circuit1");
	CHECK_FALSE(p.ui->locate("edit:cancel", x, y)); // it senses nothing: its settings wait for that (Stage 26)
	p.drag("port:timer1", "item:monitor1"); // a wire out of it, to the bench
	CHECK(p.ui->lab().view().wires.size() == 2);
	p.drag("item:alarm1", "square:12,1"); // out of the panel: it stays in
	CHECK(p.ui->message().find("stays in it") != std::string::npos);
	CHECK(p.item("alarm1").part.in == "circuit1");
	p.click("circuit:close");
	CHECK_FALSE(p.ui->locate("item:alarm1", x, y));
	// the timer at zero rings the alarm in the circuit: the circuit is what is shown
	p.click("speed:60");
	for (int frame = 0; !p.ui->paused(); ++frame) {
		REQUIRE_MESSAGE(frame < 60 * 30, "the alarm in the circuit never rang");
		p.ui->frame(1.0 / 60.0);
	}
	CHECK(p.ui->message().find("alarm1 rang: timer1 at 00:00:00.000") != std::string::npos);
	p.click("pause"); // answered
	// nested: the circuit and the monitor into another
	p.click("item:circuit1");
	p.click_with("item:monitor1", lab::Ui::ModShift);
	p.click("circuit:create");
	CHECK(p.item("circuit1").part.in == "circuit2");
	p.click("part:open");
	p.click("item:circuit1"); // inside the outer one
	p.click("part:open");
	CHECK(p.ui->locate("item:timer1", x, y));
	CHECK(p.ui->locate("circuit:up", x, y));
	p.click("circuit:up");
	CHECK(p.ui->locate("item:circuit1", x, y));
	CHECK_FALSE(p.ui->locate("item:timer1", x, y));
	p.click("circuit:close");
	// given back, from the readout
	p.click("item:circuit2");
	p.click("part:give_back");
	CHECK(p.item("circuit1").place == "bench");
	CHECK(p.item("monitor1").place == "bench");
	p.click("item:circuit1");
	p.click("part:give_back");
	CHECK(p.item("timer1").place == "bench");
	CHECK(p.item("alarm1").place == "bench");
	CHECK(p.item("monitor2").place == "bench");
	well_formed(p.ui->draw());
}

TEST_CASE("ui: a person makes a controller, lets it sense the flask, holds it at 60 C and rigs it to the burner; at 600x the flask is held there (Stage 26)") {
	Player p;
	p.drag("item:flask", "square:11,12");
	p.drag("item:burner", "item:flask");
	p.drag("item:acid", "square:7,12");
	p.drag("item:acid", "item:flask");
	p.click("panel:pour");
	p.drag("drawer:controller", "square:1,1");
	CHECK(p.ui->message().find("sensing port") != std::string::npos);
	p.drag("sense:controller1", "item:flask");
	p.click("edit:kind:vessel");
	p.click("edit:quantity:celsius");
	p.click("edit:speed:fast");
	p.click("edit:value:0");
	p.ui->key(lab::Ui::KeyText, '6');
	p.ui->key(lab::Ui::KeyText, '0');
	p.click("edit:set");
	CHECK(p.ui->message().empty());
	CHECK(p.item("controller1").part.watching);
	CHECK(p.item("controller1").part.target == 60.0);
	CHECK(p.item("controller1").part.speed == 2);
	CHECK_FALSE(p.item("controller1").part.tuned); // nothing rigged yet
	p.drag("port:controller1", "item:burner"); // no settings: the valve follows its level
	CHECK(p.ui->message().empty());
	CHECK(p.item("controller1").part.tuned);
	p.click("speed:600");
	for (int frame = 0; frame < 60 * 10; ++frame) { // ten seconds of a person's watching: 100 minutes of the lab
		p.ui->frame(1.0 / 60.0);
	}
	const double t = p.item("flask").celsius;
	MESSAGE("the flask held at " << t << " C; the controller's level " << p.item("controller1").part.level);
	CHECK(t == doctest::Approx(60.0).epsilon(1.0 / 60.0));
	well_formed(p.ui->draw());
}

// a pixel's own rays, traced, while a test runs: the reading that knows a vessel's turn (Stage 30)
struct Traced {
	int before;
	Traced() :
			before(lab::optics_reading(5)) {}
	~Traced() { lab::optics_reading(before); }
	Traced(const Traced &) = delete;
	Traced &operator=(const Traced &) = delete;
};

int body_of(const lab::OpticsScene &scene, const std::string &id) {
	return lab::optics_body(scene, id);
}

TEST_CASE("ui: a vessel carried over another is poured by hand: held, it goes to where its lip is over the other's mouth and turns about its lip, and pours once its liquid reaches the lip; let go, it turns back and is in the hand again; the right button's menu sets it upside down (Stage 27, Stage 31)") {
	const Traced traced;
	Player p;
	p.drag("item:flask", "square:11,8");
	p.drag("item:acid", "square:4,8");
	float ax = 0, ay = 0, fx = 0, fy = 0;
	p.at("item:acid", ax, ay);
	p.at("item:flask", fx, fy);
	p.ui->pointer(ax, ay, true); // picked up
	p.ui->pointer(ax, ay, false);
	for (int k = 1; k <= 20; ++k) {
		p.ui->pointer(ax + (fx - ax) * static_cast<float>(k) / 20.0f, ay + (fy - ay) * static_cast<float>(k) / 20.0f, false);
		p.ui->frame(1.0 / 60.0);
	}
	CHECK(p.item("flask").liquid_ml == 0.0);
	p.ui->pointer(fx, fy, true); // held over the flask: to the pouring place, then tilting further each frame
	for (int k = 0; k < 60; ++k) {
		p.ui->frame(1.0 / 60.0);
	}
	// a second in: turned, its liquid not yet at its lip (the bottle is half full), nothing poured
	const lab::ItemView early = p.item("acid");
	CHECK(early.pours_into >= 0);
	CHECK(early.tilt > 0.2);
	CHECK(early.tilt < 0.4);
	CHECK(early.turn > 0.3);
	CHECK(early.head_cm <= 0.0);
	CHECK(p.item("flask").liquid_ml == 0.0);
	{
		// drawn turned about its lip, which is over the middle of the flask's mouth, a little above it
		const lab::OpticsScene &scene = p.ui->optics();
		const int body = body_of(scene, "acid");
		REQUIRE(body >= 0);
		const double *lie = &scene.lies[static_cast<size_t>(body) * lab::optics_layout::SHAPE_BLOCK * 4];
		CHECK(lie[3] == doctest::Approx(early.turn).epsilon(1e-6)); // (it came from the left: it pours from its right lip, clockwise)
		float flask_x = 0, flask_y = 0;
		p.at("item:flask", flask_x, flask_y);
		CHECK(lie[4] == doctest::Approx(flask_x).epsilon(0.01));
		CHECK(lie[5] < flask_y);
		CHECK(lab::check_O1(scene) == "");
	}
	for (int k = 0; k < 120; ++k) {
		p.ui->frame(1.0 / 60.0);
	}
	CHECK(p.item("acid").tilt == 1.0); // three seconds held: at its full tilt
	const double running = p.item("flask").liquid_ml;
	CHECK(running > 20.0);
	p.ui->pointer(fx, fy, false); // let go: it turns back upright, and the pour ends in the hand
	for (int k = 0; k < 20; ++k) {
		p.ui->frame(1.0 / 60.0);
	}
	CHECK(p.item("acid").tilt < 0.5);
	CHECK(p.item("acid").pours_into >= 0); // (on its way back)
	for (int k = 0; k < 50; ++k) {
		p.ui->frame(1.0 / 60.0);
	}
	CHECK(p.item("acid").pours_into < 0);
	const double poured = p.item("flask").liquid_ml;
	CHECK(poured >= running);
	for (int k = 0; k < 30; ++k) {
		p.ui->frame(1.0 / 60.0);
	}
	CHECK(p.item("flask").liquid_ml == doctest::Approx(poured).epsilon(1e-3));
	// the right button: the full menu; upside down, with its cap shut
	p.ui->pointer(fx, fy, true, lab::Ui::ModRight);
	p.ui->pointer(fx, fy, false, lab::Ui::ModRight);
	p.click("panel:invert");
	CHECK(p.item("acid").inverted);
	CHECK(p.item("acid").cap == 0.0);
	p.click("cap:acid@0.5"); // its cap, in the readout: half open
	CHECK(p.item("acid").cap == doctest::Approx(0.5).epsilon(0.02));
	const double before = p.item("flask").liquid_ml;
	for (int k = 0; k < 120; ++k) {
		p.ui->frame(1.0 / 60.0);
	}
	CHECK(p.item("flask").liquid_ml > before + 2.0);
	well_formed(p.ui->draw());
}

TEST_CASE("ui: a person pours by hand at their own pace: carries the bottle over slowly, holds, lets go as the acid runs, presses again before it is back upright, and lets go for good; the stream is the acid's, from the lip into the flask, while it runs and not after (Stage 31)") {
	const Traced traced;
	Player p;
	p.drag("item:flask", "square:11,8");
	p.drag("item:acid", "square:4,8");
	float ax = 0, ay = 0, fx = 0, fy = 0;
	p.at("item:acid", ax, ay);
	p.at("item:flask", fx, fy);
	const auto frames = [&](int n, float x, float y, bool held) {
		for (int k = 0; k < n; ++k) {
			// a hand is never still: the pointer wanders a pixel or two
			p.ui->pointer(x + static_cast<float>((k * 7) % 3 - 1), y + static_cast<float>((k * 5) % 3 - 1), held);
			p.ui->frame(1.0 / 60.0);
			REQUIRE(lab::check_O1(p.ui->optics()) == "");
			REQUIRE_FALSE(p.ui->lab().failed());
		}
	};
	const auto stream = [&]() { return body_of(p.ui->optics(), "acid~stream"); };
	p.ui->pointer(ax, ay, true);
	p.ui->pointer(ax, ay, false);
	for (int k = 1; k <= 90; ++k) { // a second and a half on the way
		const float t = static_cast<float>(k) / 90.0f;
		p.ui->pointer(ax + (fx - ax) * t, ay + (fy - ay) * t, false);
		p.ui->frame(1.0 / 60.0);
	}
	frames(20, fx, fy, false); // over it, a moment
	CHECK(stream() < 0);
	p.ui->pointer(fx, fy, true);
	// held until the acid runs: the first of it comes when the lip is down to it
	int waited = 0;
	while (waited < 400 && !(p.item("acid").pours_ml_s > 0.0)) {
		frames(1, fx, fy, true);
		++waited;
	}
	REQUIRE(waited < 400);
	MESSAGE("held " << waited / 60.0 << " s before the acid ran: the bottle turned " << p.item("acid").turn * 180.0 / M_PI << " degrees");
	CHECK(p.item("acid").turn > 1.2);
	frames(12, fx, fy, true); // and a moment more
	{
		// the stream: a column from the bottle's lip down into the flask, of the acid itself
		const lab::OpticsScene &scene = p.ui->optics();
		const int s = stream();
		REQUIRE(s >= 0);
		namespace L = lab::optics_layout;
		const float *rect = &scene.table[static_cast<size_t>(L::HEADER_VECS + s * L::BODY_VECS + L::RECT) * 4];
		float flask_x = 0, flask_y = 0;
		p.at("item:flask", flask_x, flask_y);
		CHECK(std::abs(rect[0] + rect[2] / 2.0f - flask_x) < 8.0f); // over the flask's mouth
		CHECK(rect[2] >= 1.0f);
		CHECK(rect[2] < 24.0f); // (well narrower than the bottle's mouth, 40 pixels)
		// it thins as it falls: its last length is no wider than its first
		int last = s;
		for (int k = 1; k < 8; ++k) {
			const int next = body_of(scene, "acid~stream" + std::to_string(k));
			last = next >= 0 ? next : last;
		}
		CHECK(last != s);
		CHECK(scene.table[static_cast<size_t>(L::HEADER_VECS + last * L::BODY_VECS + L::RECT) * 4 + 2] <= rect[2]);
	}
	const double first = p.item("flask").liquid_ml;
	CHECK(first > 0.0);
	// let go: it turns back, and before it is upright the button is pressed again: it tilts again, and pours on
	frames(8, fx, fy, false);
	CHECK(p.item("acid").pours_into >= 0);
	p.ui->pointer(fx, fy, true);
	frames(40, fx, fy, true);
	CHECK(p.item("acid").pours_into >= 0);
	CHECK(p.item("acid").pours_ml_s > 0.0);
	CHECK(p.item("flask").liquid_ml > first);
	// let go for good: upright, in the hand, no stream; and put down where it stood
	frames(80, fx, fy, false);
	CHECK(p.item("acid").pours_into < 0);
	CHECK(stream() < 0);
	const double poured = p.item("flask").liquid_ml;
	frames(30, fx, fy, false);
	CHECK(p.item("flask").liquid_ml == doctest::Approx(poured).epsilon(1e-3));
	float home_x = 0, home_y = 0;
	p.at("square:4,8", home_x, home_y);
	frames(30, home_x, home_y, false);
	p.ui->pointer(home_x, home_y, true);
	p.ui->pointer(home_x, home_y, false);
	CHECK(p.item("acid").place == "bench");
	well_formed(p.ui->draw());
}

TEST_CASE("ui: a picture of a pour by hand, by the truth's own rays (written where LAB_PICTURE names a file, as a PPM) (Stage 31)") {
	const char *path = std::getenv("LAB_PICTURE");
	if (path == nullptr) {
		return;
	}
	const Traced traced;
	Player p;
	p.drag("item:flask", "square:11,8");
	p.drag("item:acid", "square:4,8");
	float ax = 0, ay = 0, fx = 0, fy = 0;
	p.at("item:acid", ax, ay);
	p.at("item:flask", fx, fy);
	p.ui->pointer(ax, ay, true);
	p.ui->pointer(ax, ay, false);
	p.ui->pointer(fx, fy, false);
	p.ui->frame(1.0 / 60.0);
	p.ui->pointer(fx, fy, true);
	const int held = std::getenv("LAB_PICTURE_FRAMES") != nullptr ? std::atoi(std::getenv("LAB_PICTURE_FRAMES")) : 150;
	for (int k = 0; k < held; ++k) {
		p.ui->frame(1.0 / 60.0);
	}
	const lab::OpticsScene scene = p.ui->optics();
	const int x0 = static_cast<int>(fx) - 260, y0 = static_cast<int>(fy) - 330, w = 420, h = 480;
	std::vector<double> rgb(static_cast<size_t>(w) * h * 3, 0.0);
	std::vector<std::thread> threads;
	std::atomic<int> next{ 0 };
	for (unsigned t = 0; t < std::max(1u, std::thread::hardware_concurrency()); ++t) {
		threads.emplace_back([&] {
			for (int y = next.fetch_add(1); y < h; y = next.fetch_add(1)) {
				for (int x = 0; x < w; ++x) {
					lab::optics_area_truth(scene, x0 + x, y0 + y, 1, &rgb[(static_cast<size_t>(y) * w + x) * 3]);
				}
			}
		});
	}
	for (std::thread &t : threads) {
		t.join();
	}
	std::FILE *out = std::fopen(path, "wb");
	REQUIRE(out != nullptr);
	std::fprintf(out, "P6\n%d %d\n255\n", w, h);
	for (size_t i = 0; i < rgb.size(); ++i) {
		const double v = std::min(std::max(rgb[i], 0.0), 1.0), encoded = v <= 0.0031308 ? 12.92 * v : 1.055 * std::pow(v, 1.0 / 2.4) - 0.055;
		std::fputc(static_cast<int>(encoded * 255.0 + 0.5), out);
	}
	std::fclose(out);
	MESSAGE("the bottle turned " << p.item("acid").turn * 180.0 / M_PI << " degrees, pouring " << p.item("acid").pours_ml_s << " mL/s");
}

TEST_CASE("ui: a person seals the flask with the stopper, carried onto its neck, and caps the acid bottle from its readout; the stopper is rubber at the flask's mouth, the cap on the bottle's, bodies of the pass (Stage 28)") {
	Player p;
	p.drag("item:flask", "square:11,12");
	p.drag("item:acid", "square:20,12");
	p.drag("item:stopper", "item:flask"); // put down on the flask: into its neck
	REQUIRE(p.item("stopper").place == "on");
	CHECK(p.item("flask").sealed);
	for (int k = 0; k < 10; ++k) { // the bench opens out of equilibrium: a moment for the acid's gas to settle at the room's pressure
		p.ui->frame(1.0 / 60.0);
	}
	p.click("item:acid");
	p.click("screw:acid");
	INFO(p.ui->message());
	CHECK(p.item("acid").screwed);
	CHECK(p.item("acid").sealed);
	p.ui->frame(0.0);
	namespace L = lab::optics_layout;
	const lab::OpticsScene &scene = p.ui->optics();
	float fx = 0, fy = 0;
	p.at("item:flask", fx, fy);
	int at_mouth = 0;
	for (int b = 0; b < static_cast<int>(scene.table[0]); ++b) {
		const float *rect = &scene.table[static_cast<size_t>(L::HEADER_VECS + b * L::BODY_VECS + L::RECT) * 4];
		const float kind = scene.table[static_cast<size_t>(L::HEADER_VECS + b * L::BODY_VECS + L::FORM) * 4];
		at_mouth += kind == 3.0f && std::fabs(rect[0] + rect[2] / 2 - fx) < 2.0f && rect[2] < 50.0f ? 1 : 0; // a slab across the flask's axis, narrow: the stopper
	}
	CHECK(at_mouth == 1);
	p.click("screw:acid"); // off again
	CHECK_FALSE(p.item("acid").sealed);
	// pulled out: picked up off the neck and put down on the bench
	p.drag("item:stopper", "square:2,14");
	CHECK(p.item("stopper").place == "bench");
	CHECK_FALSE(p.item("flask").sealed);
}

TEST_CASE("ui: the room is a cube whose edge cells are black: a ray comes to the wall, to a black cell along an edge or to a white one, by where it goes; the wall's table has the room's black and white far off; the pass's rays and the truth's go to the same places (Stage 33)") {
	const Traced traced;
	Player p;
	p.click("pause");
	p.drag("item:flask", "square:11,12");
	p.ui->frame(0.0);
	const lab::OpticsScene &scene = p.ui->optics();
	namespace L = lab::optics_layout;
	REQUIRE(scene.table[3 * 4 + 2] > 0.0f); // the room's side
	CHECK(scene.table[3 * 4 + 3] == doctest::Approx(scene.table[3 * 4 + 2] / 16.0f));
	CHECK(scene.far_left == 0.0); // a black cell
	CHECK(scene.far_right > 0.6); // any other cell off the wall: the room's white
	CHECK(scene.wall_beyond == doctest::Approx(scene.far_right));
	// the room itself: a cube about the screen's middle, its back face the wall
	const double side = scene.table[3 * 4 + 2], cell = scene.table[3 * 4 + 3], wall = scene.table[2 * 4], lo = 960.0 - 0.5 * side;
	const auto comes = [&](double px, double py, double pz, double tx, double ty, double tz, double landing[2]) {
		const double d = std::sqrt((tx - px) * (tx - px) + (ty - py) * (ty - py) + (tz - pz) * (tz - pz));
		const double from[3] = { px, py, pz }, along[3] = { (tx - px) / d, (ty - py) / d, (tz - pz) / d };
		return lab::optics_room_exit(scene, from, along, landing);
	};
	double at[2] = { 0.0, 0.0 };
	// straight on from the screen's middle: the wall, where it looks
	CHECK(comes(960, 540, 0, 960, 540, wall, at) == 0);
	CHECK(at[0] == doctest::Approx(960.0));
	CHECK(at[1] == doctest::Approx(540.0));
	// to the wall beyond the screen, short of its edge cells: the wall, there; into a cell along its edge: black
	CHECK(comes(960, 540, 0, lo + 1.5 * cell, 540, wall, at) == 0);
	CHECK(at[0] == doctest::Approx(lo + 1.5 * cell));
	CHECK(comes(960, 540, 0, lo + 0.5 * cell, 540, wall, at) == 2);
	CHECK(comes(960, 540, 0, 960, 540 + 0.5 * side - 0.5 * cell, wall, at) == 2);
	// sideways to the left face: white at its middle, black along its edge with the wall and its edge with the front
	CHECK(comes(960, 540, wall - 0.5 * side, lo, 540, wall - 0.5 * side, at) == 1);
	CHECK(comes(960, 540, wall - 0.5 * cell, lo, 540, wall - 0.5 * cell, at) == 2);
	CHECK(comes(960, 540, wall - side + 0.5 * cell, lo, 540, wall - side + 0.5 * cell, at) == 2);
	// up to the ceiling, and back to the front face: white at their middles, black in the front's corner
	CHECK(comes(960, 540, wall - 0.5 * side, 960, 540 - 0.5 * side, wall - 0.5 * side, at) == 1);
	CHECK(comes(960, 540, 0, 960, 540, wall - side, at) == 1);
	CHECK(comes(960, 540, 0, lo + 0.5 * cell, 540 - 0.5 * side + 0.5 * cell, wall - side, at) == 2);
	// and through a flask the pass's rays and the truth's go to the same places
	float fx = 0, fy = 0;
	p.at("item:flask", fx, fy);
	int rays = 0, apart = 0;
	for (int y = static_cast<int>(fy) - 70; y <= static_cast<int>(fy) + 70; y += 2) {
		for (int x = static_cast<int>(fx) - 60; x <= static_cast<int>(fx) + 60; ++x) {
			if (lab::optics_cover(scene, x, y) <= 0.0) {
				continue;
			}
			const lab::OpticsRay mine = lab::optics_ray(scene, x + 0.5, y + 0.5, 550.0), exact = lab::optics_exact_ray(scene, x + 0.5, y + 0.5, 550.0);
			++rays;
			apart += (mine.x < -5.0e4) != (exact.x < -5.0e4) || (mine.x > 5.0e4) != (exact.x > 5.0e4) || (std::fabs(mine.x) < 5.0e4 && std::hypot(mine.x - exact.x, mine.y - exact.y) > 1.0) ? 1 : 0;
		}
	}
	REQUIRE(rays > 2000);
	CHECK(apart * 100 <= rays); // (a ray in a hundred at most: at a rim the two tracers part by a hair)
}
