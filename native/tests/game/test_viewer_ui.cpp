// The spectral viewer on the screen (GAME_SPEC §13.1, Stage 42), played as a person plays it: the probe taken up
// from its holder reads wherever it is carried, frame by frame; let go, it stays where it was let go, on an item at
// that place of it (and goes with the item), on the room, on its own screen; the reading is the light there.
#include "lab/optics_scene.hpp"
#include "lab/spectrometer.hpp"
#include "lab/ui.hpp"

#include "chem/optics.hpp"

#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
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

// A person at the screen: a frame at a time, the pointer moved as a hand moves it, the scene made every frame
// (as the host makes it), a second's pause before each gesture.
struct Hand {
	std::unique_ptr<lab::Ui> ui;
	float x = 960, y = 20;
	bool down = false;

	Hand() {
		std::string why;
		ui = lab::Ui::open(read("data/benches/lab.json"), why);
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
	// the pointer to a place over `frames` frames, as a hand moves it (eased), the button as it is
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
	lab::ItemView item(const std::string &id) {
		for (const lab::ItemView &v : ui->lab().view().items) {
			if (v.id == id) {
				return v;
			}
		}
		FAIL("no item " << id);
		return lab::ItemView{};
	}
	const lab::Spectrometer &probe() {
		const lab::Spectrometer *p = ui->probe("viewer");
		REQUIRE(p != nullptr);
		return *p;
	}
	// frames until the probe has read every nanometre `rays` times
	void read_for(int rays) {
		for (int f = 0; f < 4000; ++f) {
			int least = 1 << 30;
			for (const int c : probe().counts()) {
				least = std::min(least, c);
			}
			if (least >= rays) {
				return;
			}
			frame();
		}
		FAIL("the probe read too slowly");
	}
};

// a bench with the viewer out and a flask of the blue solution on it, the probe in its holder
void set_up(Hand &h) {
	h.must(R"({"do":"place","item":"viewer","at":[17,1]})");
	h.must(R"({"do":"place","item":"flask","at":[6,6]})");
	h.must(R"({"do":"place","item":"acid","at":[11,6]})");
	h.pause(0.5);
}

int peak_of(const std::array<double, lab::SPECTRUM_NM> &c) {
	int peak = 0;
	for (int k = 1; k < lab::SPECTRUM_NM; ++k) {
		peak = c[static_cast<size_t>(k)] > c[static_cast<size_t>(peak)] ? k : peak;
	}
	return peak;
}

} // namespace

TEST_CASE("viewer: the probe carried reads where it is, frame by frame, and stays where it is let go (Stage 42)") {
	Hand h;
	set_up(h);
	float rx = 0, ry = 0;
	h.point("holder:viewer", rx, ry);
	float px = 0, py = 0;
	h.point("probe:viewer", px, py);
	CHECK(px == rx);
	CHECK(py == ry);
	// it reads even in its holder
	CHECK(h.probe().read_count() > 0);
	CHECK(h.probe().x() == doctest::Approx(rx));
	// taken up with a click: in hand, it reads where the pointer is, frame by frame, as the hand sweeps over the acid
	h.move_to("probe:viewer");
	h.pause(1.0);
	h.click();
	float ax = 0, ay = 0;
	h.point("item:acid@0.5,0.85", ax, ay);
	const float sx = h.x, sy = h.y;
	for (int f = 1; f <= 60; ++f) {
		const float t = static_cast<float>(f) / 60.0f;
		h.x = sx + (ax - sx) * t;
		h.y = sy + (ay - sy) * t;
		h.frame();
		CHECK(h.probe().x() == doctest::Approx(h.x));
		CHECK(h.probe().y() == doctest::Approx(h.y));
		CHECK(h.probe().read_count() > 26); // the whole spectrum, coarse at least, every frame
		h.point("probe:viewer", px, py);
		CHECK(px == doctest::Approx(h.x));
	}
	// let go over the acid: fixed there, at that place of it
	h.pause(1.0);
	h.click();
	lab::ItemView v = h.item("viewer");
	CHECK(v.probe_on == "acid");
	CHECK(v.probe_u == doctest::Approx(0.5).epsilon(0.02));
	CHECK(v.probe_v == doctest::Approx(0.85).epsilon(0.02));
	// it goes with the acid: carried along the bench, the probe is where the acid's place is
	h.must(R"({"do":"move","items":["acid"],"by":[3,0]})");
	h.frame();
	h.point("item:acid@0.5,0.85", ax, ay);
	h.point("probe:viewer", px, py);
	CHECK(px == doctest::Approx(ax));
	CHECK(py == doctest::Approx(ay));
	// dragged (pressed, carried, let go) onto the bare bench: fixed on the room
	h.move_to("probe:viewer");
	h.pause(1.0);
	h.press();
	float bx = 0, by = 0;
	h.point("square:3,12", bx, by);
	h.move_to(bx, by, 40);
	h.release();
	v = h.item("viewer");
	CHECK(v.probe_on.empty());
	CHECK(v.probe_room);
	// onto its own screen: it reads that
	h.move_to("probe:viewer");
	h.pause(1.0);
	h.click();
	h.move_to("screen:viewer@0.5,0.9", 40);
	h.pause(1.0);
	h.click();
	CHECK(h.item("viewer").probe_on == "viewer");
	// onto the rest of the viewer: home
	h.move_to("probe:viewer");
	h.pause(1.0);
	h.click();
	float cx = 0, cy = 0;
	h.point("item:viewer@0.3,0.95", cx, cy);
	h.move_to(cx, cy, 40);
	h.pause(1.0);
	h.click();
	v = h.item("viewer");
	CHECK(v.probe_on.empty());
	CHECK(!v.probe_room);
	// taken up and put back with Escape: where it was
	h.must(R"({"do":"probe","viewer":"viewer","on":"flask","at":[0.5,0.5]})");
	h.frame();
	h.move_to("probe:viewer");
	h.pause(1.0);
	h.click();
	h.move_to(bx, by, 30);
	h.ui->key(lab::Ui::KeyEscape);
	h.frame();
	CHECK(h.item("viewer").probe_on == "flask");
	// chosen, the readout's button sends it home
	h.move_to("item:viewer@0.3,0.5");
	h.pause(1.0);
	h.down = false;
	h.ui->pointer(h.x, h.y, true, lab::Ui::ModRight);
	h.ui->pointer(h.x, h.y, false, lab::Ui::ModRight);
	h.frame();
	h.move_to("readout:probe:holder");
	h.pause(1.0);
	h.click();
	CHECK(h.item("viewer").probe_on.empty());
}

TEST_CASE("viewer: the reading is the light there: the wall's light, and a blue flame's band at 431 nm over it (Stage 42)") {
	Hand h;
	set_up(h);
	// on the bare wall, the probe's own ring and cable all about its disc: what it reads is the wall's light alone
	h.must(R"({"do":"probe","viewer":"viewer","at":[3.5,12.5]})");
	h.frame();
	h.read_for(64);
	const lab::OpticsScene &scene = h.ui->optics();
	float px = 0, py = 0;
	h.point("probe:viewer", px, py);
	const double wall = lab::optics_through(scene, static_cast<int>(px) + 30, static_cast<int>(py), 550.0);
	for (const int k : { 20, 170, 320 }) {
		CHECK(h.probe().mean()[static_cast<size_t>(k)] == doctest::Approx(wall).epsilon(0.03));
	}
	// over a burner's flame, wide open: the wall's light and the flame's own, its peak the CH band's
	h.must(R"({"do":"place","item":"burner","at":[3,10]})");
	h.must(R"({"do":"valve","burner":"burner","to":1.0})");
	h.frame();
	float fx = 0, fy = 0;
	h.point("item:burner", fx, fy);
	// (the flame stands on the burner's top, 42 px tall wide open: the probe low in it, where it is widest)
	h.must(std::string(R"({"do":"probe","viewer":"viewer","at":[)") + std::to_string((fx - 260.0f) / 40.0f) + "," + std::to_string((fy - 13.0f - 8.0f - 120.0f) / 40.0f) + "]}");
	h.frame();
	h.read_for(64);
	const std::array<double, lab::SPECTRUM_NM> flame = h.probe().curve();
	const int peak = peak_of(flame);
	MESSAGE("over the blue flame the probe's peak is at " << lab::SPECTRUM_FIRST_NM + peak << " nm: " << flame[static_cast<size_t>(peak)] << " of the room's light; the wall " << wall);
	CHECK(lab::SPECTRUM_FIRST_NM + peak == doctest::Approx(431).epsilon(0.02));
	CHECK(flame[static_cast<size_t>(peak)] > 1.2 * wall);
	// and the screen shows it: a white curve at the peak's column, its colour under it
	const lab::Picture *shown = h.ui->screen("viewer");
	REQUIRE(shown != nullptr);
	CHECK(shown->at(lab::PLOT_X + peak, lab::PLOT_TOP)[0] > 200);
}

TEST_CASE("viewer: through a vessel of liquid the probe reads no more than the wall's light: glass and liquid take, they do not give (Stage 42)") {
	Hand h;
	set_up(h);
	h.must(R"({"do":"probe","viewer":"viewer","on":"acid","at":[0.5,0.85]})");
	h.frame();
	h.read_for(32);
	const std::array<double, lab::SPECTRUM_NM> acid = h.probe().curve();
	h.must(R"({"do":"probe","viewer":"viewer","at":[3.5,12.5]})");
	h.frame();
	h.read_for(32);
	const std::array<double, lab::SPECTRUM_NM> wall = h.probe().curve();
	for (int k = 0; k < lab::SPECTRUM_NM; k += 10) {
		CAPTURE(k);
		CHECK(acid[static_cast<size_t>(k)] <= wall[static_cast<size_t>(k)] * 1.05);
		CHECK(acid[static_cast<size_t>(k)] > 0.2 * wall[static_cast<size_t>(k)]);
	}
}

TEST_CASE("viewer: the viewer is bodies of the pass: its case, its screen, its holder, the probe's ring and cable; nothing flat over it (Stage 42)") {
	Hand h;
	set_up(h);
	h.ui->key(lab::Ui::KeyEscape);
	h.frame();
	const lab::OpticsScene &scene = h.ui->optics();
	namespace L = lab::optics_layout;
	int screens = 0;
	for (int b = 0; b < static_cast<int>(scene.table[0]); ++b) {
		screens += scene.table[static_cast<size_t>(L::HEADER_VECS + b * L::BODY_VECS + L::MORE) * 4 + 1] > 0.5f && scene.table[static_cast<size_t>(L::HEADER_VECS + b * L::BODY_VECS + L::FORM) * 4] == 3.0f ? 1 : 0;
	}
	CHECK(screens == 3); // its own, and the balance's display in the cabinet (Stage 50); (Stage 61) and the viewer's own box in Supplies, which never runs out
	CHECK(lab::check_O1(scene) == "");
	CHECK(lab::check_O17(scene, 50) == "");
	// nothing filled is drawn over it while it is not chosen
	const lab::ItemView v = h.item("viewer");
	float x0 = 0, y0 = 0;
	h.point("square:" + std::to_string(v.col) + "," + std::to_string(v.row), x0, y0);
	const lab::DrawList &list = h.ui->draw();
	for (size_t k = static_cast<size_t>(list.above); k < list.commands.size(); ++k) {
		const lab::DrawCommand &c = list.commands[k];
		if (c.type != lab::DrawCommand::Rect && c.type != lab::DrawCommand::Polygon && c.type != lab::DrawCommand::Circle) {
			continue;
		}
		const bool over = c.x < x0 - 20 + 7 * 40 && c.x + c.w > x0 - 20 && c.y < y0 - 20 + 4 * 40 && c.y + c.h > y0 - 20;
		CAPTURE(c.x);
		CAPTURE(c.y);
		CHECK(!over);
	}
	// its screen is lit where its picture is: the rainbow strip's pixels at 530 nm are green
	float sx = 0, sy = 0;
	h.point("screen:viewer@" + std::to_string((lab::PLOT_X + 150 + 0.5) / lab::SCREEN_W) + "," + std::to_string((lab::STRIP_TOP + 4.0) / lab::SCREEN_H), sx, sy);
	double rgb[3];
	lab::optics_truth(scene, static_cast<int>(sx), static_cast<int>(sy), rgb);
	CHECK(rgb[1] > rgb[0]);
	CHECK(rgb[1] > rgb[2]);
}
