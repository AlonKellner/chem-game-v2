// The view of the room (GAME_SPEC §13.1, Stage 42): a camera on the lit room, from 1x to 16x, the screen's own
// parts as they are. Zoomed, the room is laid out anew, larger (O18: the same room); the pointer's points over it
// are the view's, so a person's gestures are the same commands at any view.
#include "lab/colour.hpp"
#include "lab/optics_scene.hpp"
#include "lab/ui.hpp"
#include "lab/view.hpp"

#include "chem/optics.hpp"

#include "sparse.hpp"

#include <doctest/doctest.h>

#include <cmath>
#include <array>
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
	double clock_ms = 0.0; // a 60th of a second a frame (the view rests VIEW_QUIET_MS after it last moved)
	Hand() {
		std::string why;
		ui = lab::Ui::open(read("data/benches/lab.json"), why);
		REQUIRE_MESSAGE(ui != nullptr, why);
		ui->set_clock([this] { return clock_ms; });
	}
	void must(const std::string &command) {
		std::string why;
		REQUIRE_MESSAGE(ui->lab().command(command, why), command << ": " << why);
	}
	void frame() {
		clock_ms += 1000.0 / 60.0;
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
	h.pause(0.2); // at rest: the scene made at the view
	const lab::View v = h.ui->view();
	REQUIRE(v.scale == 4.0f);
	REQUIRE(h.ui->scene_view() == v);
	const lab::OpticsScene zoomed = h.ui->optics();
	// through the acid and its glass, its rim, beside it on the wall's ruling: the same light, four times larger
	float lx = 0, ly = 0;
	h.ui->reset_view();
	h.frame();
	h.point("item:acid", lx, ly);
	h.ui->zoom(ax, ay, 4.0f);
	h.pause(0.2);
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

namespace {

// a view as the passes take it: a point l of the layout at l * scale + (x, y) on the screen
void affine_of(const lab::View &v, double out[3]) {
	out[0] = v.scale;
	out[1] = ROOM_X - v.x * v.scale;
	out[2] = ROOM_Y - v.y * v.scale;
}

double eye_rgb(const double a[3], const double b[3]) {
	const auto seen = [](const double c[3]) {
		return lab::shown(chem::optics::Xyz{ 0.4124564 * c[0] + 0.3575761 * c[1] + 0.1804375 * c[2], 0.2126729 * c[0] + 0.7151522 * c[1] + 0.0721750 * c[2],
				0.0193339 * c[0] + 0.1191920 * c[1] + 0.9503041 * c[2] });
	};
	return lab::eye_difference(seen(a), seen(b));
}

// frames until no pixel asks for more (at most `most`): how many, and the points they spent
int settle(lab::OpticsFrames &f, const lab::OpticsScene &scene, double &points, int most = 80) {
	points = 0.0;
	int n = 0;
	for (; n < most; ++n) {
		f.frame(scene);
		points += f.points();
		if (std::getenv("LAB_O20_PRINT") != nullptr) {
			MESSAGE("frame " << n << ": " << f.points() << " points, " << f.asked() << " asked, " << f.read() << " read");
		}
		if (n >= 4 && f.asked() <= 0.0) {
			return n + 1;
		}
	}
	return n;
}

} // namespace

TEST_CASE("view: O20, a zoom resamples, then settles once: a tenth closer, most pixels keep their history, those nothing bends read little more than their estimate, and the picture is a fresh start's (Stage 42)") {
	Hand h;
	double now_ms = 0.0;
	h.ui->set_clock([&] { return now_ms; });
	set_up(h);
	h.must(R"({"do":"part","kind":"timer","id":"clock","at":[5,2]})");
	h.must(R"({"do":"part","kind":"alarm","id":"bell","at":[4,3]})");
	h.must(R"({"do":"wire","from":"clock","to":"bell"})");
	h.frame();
	float ax = 0, ay = 0;
	h.point("item:clock", ax, ay);
	h.ui->zoom(ax, ay, 1.6f);
	CHECK(h.ui->view_moving());
	now_ms += 200.0; // at rest
	CHECK(!h.ui->view_moving());
	const lab::OpticsScene a = h.ui->optics();
	const lab::View va = h.ui->scene_view();
	REQUIRE(va == h.ui->view());
	// (every pixel of the signal parts at the view, their faces, lamps and ports, a wire, the monitor, and the wall's
	// ruling about them, the anchor; of the view after it, a sample, each pixel of which has its anchor's about it)
	h.point("item:clock", ax, ay);
	const int ax0 = static_cast<int>(ax) - 230, ay0 = static_cast<int>(ay) - 150;
	lab::OpticsFrames anchor(ax0, ay0, 460, 300, 16.0, 1);
	const int step = sparse(), x0 = ax0 + 40, y0 = ay0 + 30, w = 380 / step, hh = 240 / step;
	double spent = 0.0;
	settle(anchor, a, spent, 40);
	// a tenth closer: while it moves, the scene stands
	h.ui->zoom(ax, ay, 1.1f);
	const uint64_t standing = h.ui->optics().revision;
	CHECK(h.ui->view_moving());
	CHECK(h.ui->scene_view() == va);
	now_ms += 50.0;
	CHECK(h.ui->optics().revision == standing);
	now_ms += 200.0;
	const lab::OpticsScene b = h.ui->optics();
	const lab::View vb = h.ui->scene_view();
	REQUIRE(vb == h.ui->view());
	double from[3], to[3];
	affine_of(va, from);
	affine_of(vb, to);
	// the picture it comes to: a fresh start's, settled
	lab::OpticsFrames fresh(x0, y0, w, hh, 16.0, step), carried(x0, y0, w, hh, 16.0, step), keyed(x0, y0, w, hh, 16.0, step), early(x0, y0, w, hh, 16.0, step);
	settle(fresh, b, spent, 40);
	std::vector<std::array<int, 2>> shown;
	std::vector<std::array<double, 3>> settled;
	std::vector<bool> bent;
	for (int r = 0; r < hh; ++r) {
		for (int c = 0; c < w; ++c) {
			const int x = x0 + c * step, y = y0 + r * step;
			if (lab::optics_cover(b, x, y) >= 0.02) {
				std::array<double, 3> q{};
				fresh.colour(b, x, y, q.data());
				shown.push_back({ x, y });
				settled.push_back(q);
				bent.push_back(lab::optics_ray(b, x + 0.5, y + 0.5, 550.0).bent > 0 || lab::optics_ray(b, x + 0.5, y + 0.5, 550.0).x != x + 0.5);
			}
		}
	}
	const auto off = [&](const lab::OpticsFrames &f) {
		int n = 0;
		for (size_t i = 0; i < shown.size(); ++i) {
			double p[3];
			f.colour(b, shown[i][0], shown[i][1], p);
			n += eye_rgb(p, settled[i].data()) > lab::EYE_VISIBLE ? 1 : 0;
		}
		return n;
	};
	// carried from the anchor: its first frame keeps most pixels' histories; the pixels nothing bends read little
	// more than their estimate where a fresh start's read their first batches
	carried.resample(anchor, b, to, from);
	keyed.resample(anchor, b, to, from, true);
	int kept = 0, bodies = 0, flat = 0;
	double flat_carried = 0.0, flat_fresh = 0.0;
	for (int k = 0; k < 3; ++k) {
		carried.frame(b);
		early.frame(b);
		for (size_t i = 0; i < shown.size(); ++i) {
			if (k == 0 && lab::optics_cover(b, shown[i][0], shown[i][1]) >= 0.999) {
				++bodies;
				kept += carried.restarted(shown[i][0], shown[i][1]) ? 0 : 1;
			}
			if (!bent[i]) {
				flat += k == 0 ? 1 : 0;
				flat_carried += carried.spent(shown[i][0], shown[i][1]);
				flat_fresh += early.spent(shown[i][0], shown[i][1]);
			}
		}
	}
	const int off_carried = off(carried), off_early = off(early);
	// and settled it is the fresh start's picture; given the whole key (its estimate not read again), it is not
	double rest = 0.0;
	settle(carried, b, rest, 40);
	settle(keyed, b, rest, 40);
	const int apart = off(carried), apart_keyed = off(keyed), compared = static_cast<int>(shown.size());
	MESSAGE("a tenth closer: " << kept << " of " << bodies << " pixels of bodies kept their history; the " << flat << " nothing bends read " << flat_carried << " points in three frames (a fresh start: " << flat_fresh
							   << "); visibly off the settled picture after three frames " << off_carried << " (fresh: " << off_early << ") of " << compared << "; settled, " << apart << " apart (the whole key: " << apart_keyed << ")");
	REQUIRE(bodies > 4000 / (step * step));
	CHECK(kept >= 0.9 * bodies);
	CHECK(flat_carried <= 0.25 * flat_fresh);
	CHECK(off_carried <= std::max(off_early, compared / 200));
	CHECK(apart <= compared / 200);
	CHECK(apart_keyed > compared / 200); // (O20's self-test)
}
