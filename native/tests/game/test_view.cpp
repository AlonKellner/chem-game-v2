// The view of the room (GAME_SPEC §13.1, Stage 42): a camera on the lit room, from 1x to 16x, the screen's own
// parts as they are. Zoomed, the room is laid out anew, larger (O18: the same room); the pointer's points over it
// are the view's, so a person's gestures are the same commands at any view.
#include "lab/optics_scene.hpp"
#include "lab/ui.hpp"
#include "lab/view.hpp"

#include <doctest/doctest.h>

#include <cmath>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace {

std::string read(const std::string &relative) {
	std::ifstream in(std::string(CHEM_REPO_ROOT) + "/" + relative);
	REQUIRE_MESSAGE(static_cast<bool>(in), relative);
	std::ostringstream out;
	out << in.rdbuf();
	return out.str();
}

constexpr float ROOM_X = 0, ROOM_Y = 64;

struct Hand {
	std::unique_ptr<lab::Ui> ui;
	float x = 960, y = 20;
	bool down = false;
	Hand() {
		std::string why;
		ui = lab::Ui::open(read("data/benches/lab.json"), why);
		REQUIRE_MESSAGE(ui != nullptr, why);
	}
	void must(const std::string &command) {
		std::string why;
		REQUIRE_MESSAGE(ui->lab().command(command, why), command << ": " << why);
	}
	void frame() {
		ui->pointer(x, y, down);
		ui->frame(1.0 / 60.0);
		ui->optics();
		ui->draw();
		REQUIRE_MESSAGE(!ui->lab().failed(), ui->message());
	}
	void pause(double seconds) {
		for (int f = 0; f < static_cast<int>(seconds * 60.0); ++f) {
			frame();
		}
	}
	void point(const std::string &what, float &px, float &py) { REQUIRE_MESSAGE(ui->locate(what, px, py), "not on the screen: " << what); }
	void move_to(const std::string &what, int frames = 40) {
		float tx = 0, ty = 0;
		point(what, tx, ty);
		const float sx = x, sy = y;
		for (int f = 1; f <= frames; ++f) {
			const float t = static_cast<float>(f) / static_cast<float>(frames), e = t * t * (3.0f - 2.0f * t);
			x = sx + (tx - sx) * e;
			y = sy + (ty - sy) * e;
			frame();
		}
	}
	void click() {
		down = true;
		frame();
		pause(0.1);
		down = false;
		frame();
	}
	lab::ItemView item(const std::string &id) {
		for (const lab::ItemView &v : ui->lab().view().items) {
			if (v.id == id) {
				return v;
			}
		}
		FAIL("no item " << id);
		return lab::ItemView{};
	}
	bool text_at(const std::string &label, float &tx, float &ty) {
		const lab::DrawList &list = ui->draw();
		for (const lab::DrawCommand &c : list.commands) {
			if (c.type == lab::DrawCommand::Text && std::string(&list.text[static_cast<size_t>(c.first)]) == label) {
				tx = c.x;
				ty = c.y;
				return true;
			}
		}
		return false;
	}
};

void set_up(Hand &h) {
	h.must(R"({"do":"place","item":"acid","at":[8,5]})");
	h.must(R"({"do":"place","item":"flask","at":[13,5]})");
	h.must(R"({"do":"place","item":"burner","at":[18,9]})");
	h.must(R"({"do":"valve","burner":"burner","to":0.7})");
	h.must(R"({"do":"part","kind":"monitor","id":"mon","at":[3,2]})");
	h.frame();
}

} // namespace

TEST_CASE("view: a zoom is about the cursor, from 1x to 16x, and back to all of the room (Stage 42)") {
	const lab::ViewRoom room{ ROOM_X, ROOM_Y, 1390, 1016 };
	lab::View v = lab::view_whole(room);
	CHECK(v.scale == 1.0f);
	CHECK(v.x == ROOM_X);
	CHECK(v.y == ROOM_Y);
	// what is under the cursor stays under it
	const float cx = 700, cy = 400;
	const float lx = lab::layout_x(v, room, cx), ly = lab::layout_y(v, room, cy);
	for (int k = 0; k < 20; ++k) {
		v = lab::view_zoomed(v, room, cx, cy, 1.25f);
	}
	CHECK(v.scale == 16.0f); // (no further)
	CHECK(lab::screen_x(v, room, lx) == doctest::Approx(cx).epsilon(1e-4));
	CHECK(lab::screen_y(v, room, ly) == doctest::Approx(cy).epsilon(1e-4));
	// zoomed out past 1x it is the whole room: nothing beyond it is shown
	for (int k = 0; k < 40; ++k) {
		v = lab::view_zoomed(v, room, 100, 900, 0.8f);
	}
	CHECK(v == lab::view_whole(room));
	// moved by a hand, it stays within the room
	v = lab::view_zoomed(v, room, cx, cy, 4.0f);
	const lab::View panned = lab::view_panned(v, room, 100000.0f, 100000.0f);
	CHECK(panned.x == ROOM_X);
	CHECK(panned.y == ROOM_Y);
	// on the screen: the wheel's zoom, over a panel nothing; the button and the key back to 1x
	Hand h;
	set_up(h);
	h.ui->zoom(1600, 400, 2.0f); // over the readout: the screen's own
	CHECK(h.ui->view().scale == 1.0f);
	float ax = 0, ay = 0;
	h.point("item:acid", ax, ay);
	h.ui->zoom(ax, ay, 3.0f);
	CHECK(h.ui->view().scale == 3.0f);
	float bx = 0, by = 0;
	h.point("item:acid", bx, by);
	CHECK(bx == doctest::Approx(ax).epsilon(1e-3));
	CHECK(by == doctest::Approx(ay).epsilon(1e-3));
	h.ui->key(lab::Ui::KeyView);
	CHECK(h.ui->view().scale == 1.0f);
	h.ui->zoom(ax, ay, 3.0f);
	h.frame();
	h.x = 1515;
	h.y = 32; // the top bar's button
	h.click();
	CHECK(h.ui->view().scale == 1.0f);
}

TEST_CASE("view: zoomed, the room is the same room larger, laid out anew (O18), and the screen's own parts are as they were (Stage 42)") {
	Hand h;
	set_up(h);
	const lab::OpticsScene whole = h.ui->optics();
	float rx = 0, ry = 0, wx = 0, wy = 0;
	REQUIRE(h.text_at("reset", rx, ry));
	REQUIRE(h.text_at("cabinet", wx, wy));
	float ax = 0, ay = 0;
	h.point("item:acid", ax, ay);
	h.ui->zoom(ax, ay, 4.0f);
	h.frame();
	const lab::View v = h.ui->view();
	REQUIRE(v.scale == 4.0f);
	const lab::OpticsScene zoomed = h.ui->optics();
	// through the acid and its glass, its rim, beside it on the wall's ruling: the same light, four times larger
	float lx = 0, ly = 0;
	h.ui->reset_view();
	h.frame();
	h.point("item:acid", lx, ly);
	h.ui->zoom(ax, ay, 4.0f);
	h.frame();
	CHECK(lab::check_O18(whole, zoomed, v.scale, v.x, v.y, ROOM_X, ROOM_Y, lx - 70, ly - 90, lx + 70, ly + 90, 3.0) == "");
	if (std::getenv("LAB_O18_PRINT") != nullptr) {
		int shown = 0;
		for (double y = std::floor(ly - 90) + 0.47; y < ly + 90 && shown < 25; y += 3.0) {
			for (double x = std::floor(lx - 70) + 0.47; x < lx + 70 && shown < 25; x += 3.0) {
				const double zx = ROOM_X + (x - v.x) * 4.0, zy = ROOM_Y + (y - v.y) * 4.0;
				const double a = lab::optics_point(whole, x, y, 553.0), b = lab::optics_point(zoomed, zx, zy, 553.0);
				const lab::OpticsRay rz = lab::optics_ray(zoomed, zx, zy, 553.0);
				if (rz.bent > 0 && (!rz.lands || rz.x < 0.0 || rz.y < 0.0 || rz.x >= 1920.0 || rz.y >= 1080.0)) {
					continue;
				}
				if (std::fabs(a - b) > 2e-3) {
					const lab::OpticsRay ra = lab::optics_ray(whole, x, y, 553.0), rb = lab::optics_ray(zoomed, zx, zy, 553.0);
					MESSAGE("(" << x << ", " << y << "): " << a << " against " << b << "; lands " << ra.x << "," << ra.y << " bent " << ra.bent << " | at view " << (rb.x - ROOM_X) / 4.0 + v.x << "," << (rb.y - ROOM_Y) / 4.0 + v.y << " bent " << rb.bent);
					++shown;
				}
			}
		}
	}
	// it fires on a view whose wall stands as far behind as at 1x: what bends sees another wall
	lab::OpticsScene shallow = zoomed;
	shallow.table[2 * 4] /= 4.0f;
	CHECK(lab::check_O18(whole, shallow, v.scale, v.x, v.y, ROOM_X, ROOM_Y, lx - 70, ly - 90, lx + 70, ly + 90, 3.0) != "");
	// the top bar's button is where it was; the cabinet's name, out of view, is not on the room's part of the screen
	float zx = 0, zy = 0;
	REQUIRE(h.text_at("reset", zx, zy));
	CHECK(zx == rx);
	CHECK(zy == ry);
	REQUIRE(h.text_at("cabinet", zx, zy));
	CHECK(zx == doctest::Approx(ROOM_X + (wx - v.x) * 4.0f));
	CHECK(lab::check_O1(zoomed) == "");
}

TEST_CASE("view: zoomed, a person's gestures are the same commands: the flask carried to a square, the probe let go on the acid (Stage 42)") {
	// the same gestures, by the same names, at 1x and at a view: the lab the same after both
	std::vector<std::string> saves;
	for (const float zoom : { 1.0f, 3.0f }) {
		CAPTURE(zoom);
		Hand h;
		set_up(h);
		h.must(R"({"do":"place","item":"viewer","at":[19,1]})");
		h.frame();
		float ax = 0, ay = 0;
		h.point("item:flask", ax, ay);
		h.ui->zoom(ax, ay, zoom);
		h.frame();
		// the flask, picked up and put down at a square to its right
		h.move_to("item:flask");
		h.pause(1.0);
		h.click();
		h.move_to("square:16,10");
		h.pause(1.0);
		h.click();
		CHECK(h.item("flask").col != 13); // it moved
		// the probe taken up from its holder at 1x, carried to the acid at the view, let go at its place
		h.ui->reset_view();
		h.frame();
		h.move_to("probe:viewer");
		h.pause(1.0);
		h.click();
		h.point("item:acid", ax, ay);
		h.ui->zoom(ax, ay, zoom);
		h.frame();
		h.move_to("item:acid@0.4,0.7");
		h.pause(1.0);
		h.click();
		const lab::ItemView v = h.item("viewer");
		CHECK(v.probe_on == "acid");
		CHECK(v.probe_u == doctest::Approx(0.4).epsilon(0.02));
		CHECK(v.probe_v == doctest::Approx(0.7).epsilon(0.02));
		const lab::ItemView f = h.item("flask");
		saves.push_back(std::to_string(f.col) + "," + std::to_string(f.row) + " " + v.probe_on);
	}
	CHECK(saves[0] == saves[1]);
}

TEST_CASE("view: what the probe reads does not change with the view (Stage 42)") {
	Hand h;
	set_up(h);
	h.must(R"({"do":"place","item":"viewer","at":[19,1]})");
	h.must(R"({"do":"probe","viewer":"viewer","on":"acid","at":[0.5,0.8]})");
	h.pause(0.5);
	const lab::Spectrometer *p = h.ui->probe("viewer");
	REQUIRE(p != nullptr);
	const int restarts = p->restarts();
	const double x = p->x(), y = p->y();
	float ax = 0, ay = 0;
	h.point("item:acid", ax, ay);
	for (const float factor : { 1.5f, 2.0f, 2.0f, 0.5f }) {
		h.ui->zoom(ax + 40.0f, ay - 30.0f, factor);
		h.pause(0.1);
		CHECK(p->restarts() == restarts); // the same disc, the same scene: it goes on reading
		CHECK(p->x() == x);
		CHECK(p->y() == y);
	}
}
