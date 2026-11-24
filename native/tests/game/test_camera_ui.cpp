// A thermal camera on the screen (GAME_SPEC §13.1, Stage 48), played as a person plays it: taken from the cabinet, it
// sees through its own screen; its frame, taken up by its handle, reads wherever it is carried, the hot mark following
// what is hot; let go on the room it stays there while the camera moves; on its own screen it goes home; Escape puts it
// back. Every control in its readout does what it says. And the cabinet scrolls, each item in a box of one size.
#include "lab/thermal.hpp"
#include "lab/ui.hpp"

#include <doctest/doctest.h>

#include <cmath>
#include <fstream>
#include <set>
#include <sstream>
#include <string>

namespace {

std::string read(const std::string &relative) {
	std::ifstream in(std::string(CHEM_REPO_ROOT) + "/" + relative);
	REQUIRE_MESSAGE(static_cast<bool>(in), relative);
	std::ostringstream out;
	out << in.rdbuf();
	return out.str();
}

// A person at the screen: a frame at a time, the pointer moved as a hand moves it, the scene made every frame.
struct Hand {
	std::unique_ptr<lab::Ui> ui;
	float x = 960, y = 20;
	bool down = false;

	explicit Hand(const std::string &bench = read("data/benches/lab.json")) {
		std::string why;
		ui = lab::Ui::open(bench, why);
		REQUIRE_MESSAGE(ui != nullptr, why);
		ui->set_budget(3.0);
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
	void move_to(float tx, float ty, int frames = 30) {
		const float sx = x, sy = y;
		for (int f = 1; f <= frames; ++f) {
			const float t = static_cast<float>(f) / static_cast<float>(frames), e = t * t * (3.0f - 2.0f * t);
			x = sx + (tx - sx) * e;
			y = sy + (ty - sy) * e;
			frame();
		}
	}
	void move_to(const std::string &what, int frames = 30) {
		float tx = 0, ty = 0;
		point(what, tx, ty);
		move_to(tx, ty, frames);
	}
	void press() {
		down = true;
		frame();
	}
	void release() {
		down = false;
		frame();
	}
	void click() {
		press();
		pause(0.1);
		release();
	}
	void click(const std::string &what) {
		move_to(what, 20);
		pause(0.3);
		click();
		pause(0.3);
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
	// frames until the camera has a reading
	lab::Ui::ThermalShown reading(int frames = 600) {
		lab::Ui::ThermalShown t;
		for (int f = 0; f < frames; ++f) {
			if (ui->thermal("camera", t)) {
				return t;
			}
			frame();
		}
		FAIL("the camera read nothing");
		return t;
	}
	// a fresh reading: the search started again where the frame is now, run to its end
	lab::Ui::ThermalShown fresh() {
		pause(0.4); // (the reading scene is made again at most every 100 ms, and a search takes some frames)
		lab::Ui::ThermalShown t = reading();
		for (int f = 0; f < 120; ++f) {
			frame();
		}
		ui->thermal("camera", t);
		return t;
	}
};

// the camera on the bench, a burner lit beside it
void set_up(Hand &h) {
	h.must(R"({"do":"place","item":"camera","at":[17,1]})");
	h.must(R"({"do":"place","item":"burner","at":[8,10]})");
	h.must(R"({"do":"valve","burner":"burner","to":1.0})");
	h.pause(0.5);
}

} // namespace

TEST_CASE("camera: it sees through its screen; its frame carried slowly over a flame reads it, the hot mark on it (Stage 48)") {
	Hand h;
	set_up(h);
	// over its own screen: the room behind it, one temperature
	lab::Ui::ThermalShown t = h.fresh();
	CHECK(t.hottest_c - t.coldest_c < 0.5);
	CHECK(std::fabs(t.centre_c - 20.0) < 1.0);
	// taken up by its handle and carried, slowly, over the flame
	h.move_to("frame:camera", 40);
	h.pause(0.5);
	h.press();
	float fx = 0, fy = 0, hx = 0, hy = 0;
	h.point("item:burner", fx, fy);
	h.point("frame:camera", hx, hy);
	float lx = 0, ly = 0;
	h.point("lens:camera", lx, ly);
	// the frame's middle to the flame, over two seconds
	h.move_to(h.x + (fx - lx), h.y + (fy - 40.0f - ly), 120);
	t = h.fresh();
	MESSAGE("the frame over the flame: hottest " << t.hottest_c << " C at (" << t.hot_x << ", " << t.hot_y << "), coldest " << t.coldest_c << " C");
	CHECK(t.hottest_c > 60.0); // the flame
	const int was_x = t.hot_x;
	// carried on, slowly, 30 px to the right: the flame is 30 px further left in the frame
	h.move_to(h.x + 30.0f, h.y, 60);
	t = h.fresh();
	CHECK(std::abs((was_x - t.hot_x) - 30) <= 3);
	// let go on the room: it stays there, and stays there when the camera moves
	h.release();
	h.pause(0.3);
	CHECK(h.item("camera").thermal.frame_room);
	const double at_x = h.item("camera").thermal.frame_x;
	h.must(R"({"do":"move","items":["camera"],"by":[0,6]})");
	h.pause(0.2);
	CHECK(h.item("camera").thermal.frame_x == at_x);
	CHECK(h.fresh().hottest_c > 60.0);
}

TEST_CASE("camera: its frame let go over its own screen goes home; in hand, Escape puts it back (Stage 48)") {
	Hand h;
	set_up(h);
	h.must(R"({"do":"frame","item":"camera","room":[3,3]})");
	h.pause(0.2);
	// in hand by a click (pressed and let go where it was), then Escape: back on the room where it was
	h.click("frame:camera");
	h.move_to(h.x + 200.0f, h.y + 100.0f, 40);
	h.ui->key(lab::Ui::KeyEscape, 0);
	h.pause(0.2);
	CHECK(h.item("camera").thermal.frame_room);
	CHECK(h.item("camera").thermal.frame_x == 3.0);
	// dragged by its handle onto its own screen, more than half over it: home
	h.move_to("frame:camera", 30);
	h.press();
	float sx = 0, sy = 0, lx = 0, ly = 0;
	h.point("screen:camera", sx, sy);
	h.point("lens:camera", lx, ly);
	h.move_to(h.x + (sx - lx) + 20.0f, h.y + (sy - ly) + 10.0f, 90);
	h.release();
	h.pause(0.2);
	CHECK(!h.item("camera").thermal.frame_room);
}

TEST_CASE("camera: every control in its readout does what it says (Stage 48)") {
	Hand h;
	set_up(h);
	h.click("item:camera");
	for (const std::string &p : lab::thermal_palette_names()) {
		h.click("readout:camera:palette:" + p);
		CHECK(h.item("camera").thermal.palette == p);
	}
	h.click("readout:camera:markers");
	CHECK(!h.item("camera").thermal.markers);
	h.click("readout:camera:markers");
	CHECK(h.item("camera").thermal.markers);
	h.click("readout:camera:gain");
	CHECK(h.item("camera").thermal.gain == "high");
	h.click("readout:camera:gain");
	CHECK(h.item("camera").thermal.gain == "low");
	h.click("readout:camera:gain");
	CHECK(h.item("camera").thermal.gain == "auto");
	h.click("readout:camera:emissivity:-");
	CHECK(h.item("camera").thermal.emissivity == doctest::Approx(0.94));
	h.click("readout:camera:emissivity:+");
	h.click("readout:camera:emissivity:+");
	CHECK(h.item("camera").thermal.emissivity == doctest::Approx(0.96));
	h.click("readout:camera:reflected:+");
	CHECK(h.item("camera").thermal.reflected_c.value_or(0.0) == doctest::Approx(21.0));
	h.click("readout:camera:reflected:-");
	h.click("readout:camera:reflected:-");
	CHECK(h.item("camera").thermal.reflected_c.value_or(0.0) == doctest::Approx(19.0));
	// the range locked where it shows now; its level and span then its own
	h.fresh();
	h.click("readout:camera:range");
	CHECK(h.item("camera").thermal.locked);
	const double level = h.item("camera").thermal.level_c, span = h.item("camera").thermal.span_c;
	h.click("readout:camera:level:+");
	CHECK(h.item("camera").thermal.level_c == doctest::Approx(level + 5.0));
	h.click("readout:camera:span:+");
	CHECK(h.item("camera").thermal.span_c == doctest::Approx(span + 5.0));
	h.pause(0.3);
	lab::Ui::ThermalShown t;
	REQUIRE(h.ui->thermal("camera", t));
	CHECK(t.low_c == doctest::Approx(level + 5.0 - 0.5 * (span + 5.0)));
	h.click("readout:camera:range");
	CHECK(!h.item("camera").thermal.locked);
	// "frame home" shows while its frame is on the room, and sends it home
	float x = 0, y = 0;
	CHECK(!h.ui->locate("readout:camera:home", x, y));
	h.must(R"({"do":"frame","item":"camera","room":[3,3]})");
	h.pause(0.2);
	h.click("readout:camera:home");
	CHECK(!h.item("camera").thermal.frame_room);
}

TEST_CASE("camera: the cabinet scrolls, a row a notch or by its bar, and every item is in a box of one size (Stage 48)") {
	// the lab's bench with six stoppers more: fourteen rows' worth of items is more than the cabinet shows
	std::string bench = read("data/benches/lab.json");
	const std::string camera = "\t\t{\n\t\t\t\"id\": \"camera\",\n\t\t\t\"kind\": \"thermal\"\n\t\t}";
	const size_t at = bench.find(camera);
	REQUIRE(at != std::string::npos);
	std::string more;
	for (int k = 1; k <= 6; ++k) {
		more += ",\n\t\t{\"id\": \"plug" + std::to_string(k) + "\", \"kind\": \"stopper\", \"wall\": \"latex\", \"wall_g\": 8, \"hold_n\": 30, \"loss\": 0.1}";
	}
	bench.insert(at + camera.size(), more);
	Hand h(bench);
	h.pause(0.3);
	// every item on the screen is in a box 100 px square (as large as fits it); the last are below the fold
	float x = 0, y = 0;
	CHECK(h.ui->locate("item:flask", x, y));
	CHECK(!h.ui->locate("item:plug6", x, y));
	// the wheel over the cabinet: a row a notch
	h.move_to(120.0f, 500.0f, 20);
	for (int k = 0; k < 4; ++k) {
		h.ui->zoom(h.x, h.y, 0.8f);
		h.pause(0.2);
	}
	CHECK(h.ui->locate("item:plug6", x, y));
	CHECK(!h.ui->locate("item:flask", x, y));
	// and back by the bar, dragged to its top, slowly
	h.move_to(234.0f, 700.0f, 30);
	h.press();
	h.move_to(234.0f, 100.0f, 60);
	h.release();
	CHECK(h.ui->locate("item:flask", x, y));
	// an item taken from the cabinet's lowest row after scrolling down to it
	h.move_to(120.0f, 500.0f, 20);
	for (int k = 0; k < 4; ++k) {
		h.ui->zoom(h.x, h.y, 0.8f);
		h.pause(0.2);
	}
	h.click("item:plug6");
	h.click("square:5,5");
	CHECK(h.item("plug6").place == "bench");
}
