// A thermal camera in the lab (GAME_SPEC §13, Stage 48): equipment of the cabinet that holds no matter, so it changes
// nothing in the world; where its frame is and how it reads (its settings) are the lab's, saved, and not the world's.
#include "lab/lab.hpp"
#include "lab/thermal.hpp"

#include <doctest/doctest.h>

#include <fstream>
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

std::unique_ptr<lab::Lab> open_lab() {
	std::string why;
	auto made = lab::Lab::open(read("data/benches/lab.json"), why);
	REQUIRE_MESSAGE(made != nullptr, why);
	return made;
}

void must(lab::Lab &l, const std::string &command) {
	std::string why;
	REQUIRE_MESSAGE(l.command(command, why), command << ": " << why);
}

std::string refused(lab::Lab &l, const std::string &command) {
	std::string why;
	const std::string before = l.digest(), saved = l.save();
	CHECK_MESSAGE(!l.command(command, why), command);
	CHECK(l.digest() == before);
	CHECK(l.save() == saved); // nothing at all changed
	return why;
}

lab::ItemView item(const lab::LabView &v, const std::string &id) {
	for (const lab::ItemView &i : v.items) {
		if (i.id == id) {
			return i;
		}
	}
	FAIL("no item " << id);
	return lab::ItemView{};
}

// the interactive bench, its camera taken out
std::string without_camera() {
	std::string text = read("data/benches/lab.json");
	const std::string entry = ",\n\t\t{\n\t\t\t\"id\": \"camera\",\n\t\t\t\"kind\": \"thermal\"\n\t\t}";
	const size_t at = text.find(entry);
	REQUIRE(at != std::string::npos);
	return text.erase(at, entry.size());
}

} // namespace

TEST_CASE("lab: a thermal camera is equipment of the cabinet that changes nothing in the world (Stage 48)") {
	auto with = open_lab();
	std::string why;
	auto without = lab::Lab::open(without_camera(), why);
	REQUIRE_MESSAGE(without != nullptr, why);
	const lab::ItemView v = item(with->view(), "camera");
	CHECK(v.kind == "thermal");
	CHECK(v.place == "cabinet");
	CHECK(v.w == 5);
	CHECK(v.h == 4);
	CHECK(v.contents.empty());
	CHECK(!v.thermal.frame_room);
	CHECK(v.thermal.palette == "ironbow");
	CHECK(v.thermal.emissivity == 0.95);
	CHECK(with->digest() == without->digest());
	// placed, its frame on the room and its settings changed, through steps with a flame under a flask: the same world
	for (lab::Lab *l : { with.get(), without.get() }) {
		must(*l, R"({"do":"place","item":"flask","at":[8,4]})");
		must(*l, R"({"do":"place","item":"burner","at":[8,11]})");
		must(*l, R"({"do":"valve","burner":"burner","to":1})");
	}
	must(*with, R"({"do":"place","item":"camera","at":[16,2]})");
	must(*with, R"({"do":"frame","item":"camera","room":[7.5,3.25]})");
	must(*with, R"({"do":"thermal","item":"camera","palette":"white_hot","emissivity":0.7,"reflected_c":35,"markers":false,"range":"lock","level_c":90,"span_c":40,"gain":"low"})");
	for (int s = 0; s < 3; ++s) {
		must(*with, R"({"do":"wait","seconds":5})");
		must(*without, R"({"do":"wait","seconds":5})");
		CHECK(with->digest() == without->digest());
	}
	const lab::ItemView c = item(with->view(), "camera");
	CHECK(c.thermal.frame_room);
	CHECK(c.thermal.frame_x == 7.5);
	CHECK(c.thermal.frame_y == 3.25);
	CHECK(c.thermal.palette == "white_hot");
	CHECK(c.thermal.emissivity == 0.7);
	CHECK(c.thermal.reflected_c.value_or(-1.0) == 35.0);
	CHECK(!c.thermal.markers);
	CHECK(c.thermal.locked);
	CHECK(c.thermal.level_c == 90.0);
	CHECK(c.thermal.span_c == 40.0);
	CHECK(c.thermal.gain == "low");
}

TEST_CASE("lab: a camera's frame is on the room or home over its screen; put away, it goes home (Stage 48)") {
	auto l = open_lab();
	CHECK(refused(*l, R"({"do":"frame","item":"camera","room":[3,3]})").find("cabinet") != std::string::npos);
	must(*l, R"({"do":"place","item":"camera","at":[16,2]})");
	must(*l, R"({"do":"frame","item":"camera","room":[3,4]})");
	CHECK(item(l->view(), "camera").thermal.frame_room);
	// it stays where it is when the camera moves
	must(*l, R"({"do":"move","items":["camera"],"by":[-2,6]})");
	CHECK(item(l->view(), "camera").thermal.frame_room);
	CHECK(item(l->view(), "camera").thermal.frame_x == 3.0);
	must(*l, R"({"do":"frame","item":"camera","home":true})");
	CHECK(!item(l->view(), "camera").thermal.frame_room);
	must(*l, R"({"do":"frame","item":"camera","room":[3,4]})");
	must(*l, R"({"do":"stow","item":"camera"})");
	CHECK(!item(l->view(), "camera").thermal.frame_room);
	must(*l, R"({"do":"place","item":"camera","at":[16,2]})");
	CHECK(!item(l->view(), "camera").thermal.frame_room);
	// refused, each with its reason
	CHECK(refused(*l, R"({"do":"frame","item":"camera"})").find("room") != std::string::npos);
	CHECK(refused(*l, R"({"do":"frame","item":"camera","room":[1]})").find("two numbers") != std::string::npos);
	CHECK(refused(*l, R"({"do":"frame","item":"flask","room":[1,1]})").find("thermal camera") != std::string::npos);
}

TEST_CASE("lab: a camera's settings are checked, each refused with its reason, and only the camera's (Stage 48)") {
	auto l = open_lab();
	CHECK(refused(*l, R"({"do":"thermal","item":"camera","palette":"sepia"})").find("ironbow") != std::string::npos);
	CHECK(refused(*l, R"({"do":"thermal","item":"camera","emissivity":0.05})").find("emissivity") != std::string::npos);
	CHECK(refused(*l, R"({"do":"thermal","item":"camera","emissivity":1.2})").find("emissivity") != std::string::npos);
	CHECK(refused(*l, R"({"do":"thermal","item":"camera","gain":"medium"})").find("gain") != std::string::npos);
	CHECK(refused(*l, R"({"do":"thermal","item":"camera","range":"fixed"})").find("range") != std::string::npos);
	CHECK(refused(*l, R"({"do":"thermal","item":"camera","markers":1})").find("markers") != std::string::npos);
	CHECK(refused(*l, R"({"do":"thermal","item":"camera","span_c":0})").find("span_c") != std::string::npos);
	// a refused command changes none of the settings it named, even those before the one it refused
	refused(*l, R"({"do":"thermal","item":"camera","palette":"arctic","gain":"medium"})");
	CHECK(item(l->view(), "camera").thermal.palette == "ironbow");
	// every palette of data/thermal.json is a camera's; the reflected room back to the room's own
	for (const std::string &p : lab::thermal_palette_names()) {
		must(*l, R"({"do":"thermal","item":"camera","palette":")" + p + R"("})");
	}
	CHECK(lab::thermal_palette_names().size() == 5);
	must(*l, R"({"do":"thermal","item":"camera","reflected_c":40})");
	CHECK(item(l->view(), "camera").thermal.reflected_c.has_value());
	must(*l, R"({"do":"thermal","item":"camera","reflected_c":"room"})");
	CHECK(!item(l->view(), "camera").thermal.reflected_c.has_value());
}

TEST_CASE("lab: a camera's frame and settings are saved and loaded (Stage 48)") {
	auto l = open_lab();
	must(*l, R"({"do":"place","item":"camera","at":[16,2]})");
	must(*l, R"({"do":"frame","item":"camera","room":[0.1234567890123,3.5]})");
	must(*l, R"({"do":"thermal","item":"camera","palette":"arctic","emissivity":0.8125,"reflected_c":31.5,"range":"lock","level_c":42.25,"span_c":17,"gain":"high","markers":false})");
	std::string why;
	auto again = lab::Lab::load(l->save(), why);
	REQUIRE_MESSAGE(again != nullptr, why);
	const lab::ItemView v = item(again->view(), "camera"), was = item(l->view(), "camera");
	CHECK(v.kind == "thermal");
	CHECK(v.thermal.frame_room);
	CHECK(v.thermal.frame_x == 0.1234567890123);
	CHECK(v.thermal.palette == "arctic");
	CHECK(v.thermal.emissivity == 0.8125);
	CHECK(v.thermal.reflected_c.value_or(0.0) == 31.5);
	CHECK(v.thermal.locked);
	CHECK(v.thermal.level_c == 42.25);
	CHECK(v.thermal.span_c == 17.0);
	CHECK(!v.thermal.markers);
	CHECK(again->digest() == l->digest());
	CHECK(again->save() == l->save());
}

TEST_CASE("lab: every command aimed at a camera is refused or leaves the world as it was (Stage 48)") {
	auto l = open_lab();
	must(*l, R"({"do":"place","item":"camera","at":[16,2]})");
	must(*l, R"({"do":"place","item":"flask","at":[8,4]})");
	must(*l, R"({"do":"place","item":"burner","at":[2,4]})");
	const char *const commands[] = { "place", "move", "heat", "unheat", "valve", "stir", "pour", "tip", "attach", "detach", "under", "undrain", "submit", "hold", "invert", "unpour", "cap", "stow", "trash", "restore", "sense", "unsense", "rig", "unrig", "probe" };
	const char *const keys[] = { "item", "vessel", "burner", "to", "of", "from", "into", "species", "viewer" };
	for (const char *command : commands) {
		for (const char *key : keys) {
			std::string why;
			const std::string text = std::string(R"({"do":")") + command + R"(",")" + key + R"(":"camera","item":"camera","to":"flask","at":[16,2],"rate":0.5,"slot":1,"on":true,"control":"stir","part":"none"})";
			const std::string before_world = l->digest();
			const bool ran = l->command(text, why);
			CAPTURE(text);
			CAPTURE(why);
			CHECK(!l->failed());
			if (ran) {
				l->command(R"({"do":"restore"})", why);
				l->command(R"({"do":"place","item":"camera","at":[16,2]})", why);
			}
			CHECK(l->digest() == before_world);
		}
	}
	CHECK(item(l->view(), "camera").kind == "thermal");
	CHECK(item(l->view(), "camera").contents.empty());
}

TEST_CASE("thermal: the palettes, the gains, and a span that moves only past its hysteresis (Stage 48)") {
	for (const std::string &name : lab::thermal_palette_names()) {
		CAPTURE(name);
		const auto &p = lab::thermal_palette(name);
		for (const auto &c : p) {
			for (const float v : c) {
				CHECK(v >= 0.0f);
				CHECK(v <= 1.0f);
			}
		}
		// each runs from dark to light, or light to dark, by its luminance at its ends
		const auto lum = [](const std::array<float, 3> &c) { return 0.2126 * c[0] + 0.7152 * c[1] + 0.0722 * c[2]; };
		CHECK(std::abs(lum(p.front()) - lum(p.back())) > 0.4);
	}
	CHECK(lab::thermal_palette("ironbow")[0][0] == 0.0f); // black
	CHECK(lab::thermal_palette("ironbow")[255][1] == 1.0f); // to white
	double lo = 0.0, hi = 0.0;
	lab::thermal_gain_range("high", lo, hi);
	CHECK(lo == -20.0);
	CHECK(hi == 150.0);
	lab::thermal_gain_range("low", lo, hi);
	CHECK(hi == 650.0);
	lab::ThermalSettings auto_gain;
	lab::ThermalSpan s = lab::thermal_span(auto_gain, 20.0, 60.0, lab::ThermalSpan{});
	CHECK(s.gain == "high");
	CHECK(s.low_c == 20.0);
	CHECK(s.high_c == 60.0);
	// a reading a little off keeps the span; one well off moves it
	CHECK(lab::thermal_span(auto_gain, 20.3, 60.4, s).high_c == 60.0);
	CHECK(lab::thermal_span(auto_gain, 20.0, 70.0, s).high_c == 70.0);
	// auto gain: low above 140 C, high again only under 120
	s = lab::thermal_span(auto_gain, 20.0, 400.0, s);
	CHECK(s.gain == "low");
	CHECK(s.high_c == 400.0);
	CHECK(lab::thermal_span(auto_gain, 20.0, 130.0, s).gain == "low");
	CHECK(lab::thermal_span(auto_gain, 20.0, 110.0, s).gain == "high");
	// past the high gain's range: over, the span clipped to it
	lab::ThermalSettings high = auto_gain;
	high.gain = "high";
	const lab::ThermalSpan over = lab::thermal_span(high, 20.0, 900.0, lab::ThermalSpan{});
	CHECK(over.over);
	CHECK(over.high_c == 150.0);
	// locked: its level and span
	lab::ThermalSettings locked = auto_gain;
	locked.locked = true;
	locked.level_c = 50.0;
	locked.span_c = 30.0;
	const lab::ThermalSpan fixed = lab::thermal_span(locked, 0.0, 600.0, lab::ThermalSpan{});
	CHECK(fixed.low_c == 35.0);
	CHECK(fixed.high_c == 65.0);
}
