// The screen (GAME_SPEC §13): a pointer in, lab commands and a draw list out.
// The demo is played here with nothing but the pointer.
#include "lab/optics_scene.hpp"
#include "lab/ui.hpp"

#include <doctest/doctest.h>

#include <cmath>
#include <fstream>
#include <sstream>
#include <string>

using lab::ItemView;
using lab::LabView;

namespace {

// the chain's pass runs only over vessels made as glassware standing on the bench (Stage 24: their curved rows
// are read along their outlines), not over anything held
bool only_glassware(const lab::OpticsScene &scene) {
	namespace L = lab::optics_layout;
	for (const int b : scene.chain_bodies) {
		const float *map = &scene.table[static_cast<size_t>(L::HEADER_VECS + b * L::BODY_VECS + L::MAP) * 4];
		const float *refract = &scene.table[static_cast<size_t>(L::HEADER_VECS + b * L::BODY_VECS + L::REFRACT) * 4];
		if (map[3] >= 0.0f || refract[1] != 0.0f) {
			return false;
		}
	}
	return true;
}

} // namespace

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
		ui->pointer(x, y, false);
		ui->pointer(x, y, true);
		ui->pointer(x, y, false);
	}
	void drag(const std::string &what, const std::string &to) {
		float x0 = 0, y0 = 0, x1 = 0, y1 = 0;
		at(what, x0, y0);
		at(to, x1, y1);
		ui->pointer(x0, y0, false);
		ui->pointer(x0, y0, true);
		ui->pointer((x0 + x1) / 2, (y0 + y1) / 2, true);
		ui->pointer(x1, y1, true);
		ui->pointer(x1, y1, false);
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
	p.drag("item:beaker", "item:flask"); // onto the flask itself: the pour panel, with nothing to pour
	p.click("panel:cancel");
	float x = 0, y = 0;
	CHECK_FALSE(p.ui->locate("panel:cancel", x, y)); // closed
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
	// while it is in hand it is a layer nearer the eye, the pixels it covers are the chain's, and the flask's
	// liquid is seen through it. Set down, it stands again.
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
	// everything stands: one layer, and the chain's pass runs over the glassware alone (its curved rows read
	// along their outlines, Stage 24)
	CHECK(p.ui->optics().table[3 * 4 + 1] == 0.0f);
	CHECK(only_glassware(p.ui->optics()));
	float jx = 0, jy = 0, fx = 0, fy = 0;
	p.at("item:jar", jx, jy);
	p.at("item:flask", fx, fy);
	// pressed, not yet moved: it still stands
	p.ui->pointer(jx, jy, false);
	p.ui->pointer(jx, jy, true);
	p.ui->frame(1.0 / 60.0);
	CHECK(p.ui->optics().table[3 * 4 + 1] == 0.0f);
	// carried, a person's pace: about four pixels a frame, toward the flask
	const int frames = 50;
	for (int k = 1; k <= frames; ++k) {
		p.ui->pointer(jx + (fx - jx) * static_cast<float>(k) / frames, jy + (fy - jy) * static_cast<float>(k) / frames, true);
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
	// its rectangle is where the chain's pass runs
	size_t at = held.chain_bodies.size();
	for (size_t k = 0; k < held.chain_bodies.size(); ++k) {
		at = held.chain_bodies[k] == jar ? k : at;
	}
	REQUIRE(at < held.chain_bodies.size());
	const float *rect = &held.table[static_cast<size_t>(L::HEADER_VECS + jar * L::BODY_VECS + L::RECT) * 4];
	CHECK(held.chain[at * 4] <= rect[0]);
	CHECK(held.chain[at * 4 + 2] >= rect[0] + rect[2]);
	// through the jar's glass, low in it, the flask's blue liquid behind: less of the red than beside the flask
	const int px = static_cast<int>(rect[0] + rect[2] / 2), py = static_cast<int>(rect[1] + rect[3]) - 12;
	const double through_both = lab::optics_through(held, px, py, 650.0);
	CHECK(lab::optics_through(held, px, py, 650.0) == doctest::Approx(lab::optics_area_through(held, px, py, 650.0, 8)).epsilon(0.05));
	// let go beside the flask: it stands again, on the bench's layer, and the chain's pass is the glassware's alone
	p.ui->pointer(fx, fy, false);
	p.ui->frame(1.0 / 60.0);
	CHECK(p.ui->optics().table[3 * 4 + 1] == 0.0f);
	CHECK(only_glassware(p.ui->optics()));
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
	CHECK(slabs == 1 + 2 * 2 + 1 + 1); // the burner; two faces and two lamps; the monitor's port; the rig's knob
	CHECK(bars == 3); // the wire and the rig; the condenser's outlet, in the cabinet
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
	p.drag("item:beaker", "item:condenser");
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
	// carried together, a frame at a time: three squares up
	float bx = 0, by = 0;
	p.at("item:beaker", bx, by);
	p.sweep(bx, by, bx, by - 120);
	CHECK(p.item("beaker").col == 18);
	CHECK(p.item("beaker").row == 5);
	CHECK(p.item("flask").col == 10);
	CHECK(p.item("flask").row == 5);
	CHECK(p.ui->message().empty());
	// the flask would land on the jar: neither moves, and the reason is shown
	p.at("item:beaker", bx, by);
	p.sweep(bx, by, bx - 280, by + 120);
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
	// a monitor: its editor opens; the flask's temperature, between 30 and 40.5 C, typed and nudged
	p.drag("drawer:monitor", "square:1,1");
	CHECK(kinds() == V{ "monitor1" });
	CHECK(p.ui->selected() == "monitor1");
	p.click("edit:item:flask");
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
	p.click("edit:item:flask");
	p.click("edit:quantity");
	p.click("edit:pick:water");
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
	p.click("edit:cancel");
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
