// A conductivity meter in the lab (GAME_SPEC §13, Readings; §10; Stage 65): a vessel's liquid reads its conductivity,
// the truth or, by instruments, its meter's reading within its stated accuracy to its range's resolution (I1);
// recorded and logged in the notebook; a quantity a sensing part and a tutorial can name.
#include "lab/conditions.hpp"
#include "lab/instruments.hpp"
#include "lab/lab.hpp"

#include <doctest/doctest.h>

#include <cmath>
#include <string>

namespace {

// a 25 C room (its air's carbon dioxide `co2_ppm`; 0 none, the default 420): a beaker of 0.01 mol/kg KCl, one of water
// open to the room's air, one dry; `extra` the bench's fields
std::string bench(const std::string &extra = "", double co2_ppm = 0.0) {
	return R"({"format": "chem-bench/2", "grid": [28, 15], "room": {"celsius": 25, "air_mol": 1000, "co2_ppm": )" + std::to_string(co2_ppm) + R"(},)" + extra + R"(
		"items": [
			{"id": "salt", "kind": "vessel", "glassware": "beaker", "volume_ml": 250, "wall": "glass", "wall_g": 80, "at": [4, 7],
				"label": "potassium chloride, 0.01 M", "contents": {"water": {"g": 100}, "potassium_chloride_aq": {"mol": 0.001}}},
			{"id": "water", "kind": "vessel", "glassware": "beaker", "volume_ml": 250, "wall": "glass", "wall_g": 80, "at": [10, 7],
				"label": "water", "contents": {"water": {"g": 100}}},
			{"id": "dry", "kind": "vessel", "glassware": "beaker", "volume_ml": 250, "wall": "glass", "wall_g": 80, "at": [16, 7]}
		]})";
}

std::unique_ptr<lab::Lab> open(const std::string &text) {
	std::string why;
	auto made = lab::Lab::open(text, why);
	REQUIRE_MESSAGE(made != nullptr, why);
	return made;
}

void must(lab::Lab &l, const std::string &command) {
	std::string why;
	REQUIRE_MESSAGE(l.command(command, why), command << ": " << why);
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

} // namespace

TEST_CASE("lab: a vessel's liquid reads its conductivity, by instruments its meter's within its accuracy (I1); water under a room's CO2 takes it up (Stage 65)") {
	auto truth = open(bench());
	lab::LabView v = truth->view();
	const lab::ItemView salt = item(v, "salt"), water = item(v, "water"), dry = item(v, "dry");
	REQUIRE(salt.us_cm);
	REQUIRE(water.us_cm);
	CHECK_FALSE(dry.us_cm);
	MESSAGE("0.01 mol/kg KCl at 25 C: " << *salt.us_cm << " uS/cm (Jones and Bradshaw's 0.01 demal: 1408.8); water open to the room: " << *water.us_cm);
	CHECK(*salt.us_cm == doctest::Approx(1410.0).epsilon(0.01));
	// pure water is 0.055 uS/cm (ASTM D1193's Type I): a room with no carbon dioxide
	CHECK(*water.us_cm == doctest::Approx(0.0549).epsilon(0.02));
	// open to air of 420 ppm, its carbonic acid adds most of a microsiemens (distilled water reads about 1 uS/cm)
	auto aired = open(bench("", 420.0));
	// a bench that names none has today's air
	const std::string unsaid = std::string(R"({"format": "chem-bench/2", "grid": [28, 15], "room": {"celsius": 25, "air_mol": 1000},)") + R"("items": [
			{"id": "water", "kind": "vessel", "glassware": "beaker", "volume_ml": 250, "wall": "glass", "wall_g": 80, "at": [10, 7], "contents": {"water": {"g": 100}}}]})";
	CHECK(*item(open(unsaid)->view(), "water").us_cm == doctest::Approx(*item(aired->view(), "water").us_cm).epsilon(1e-6));
	const double distilled = *item(aired->view(), "water").us_cm;
	MESSAGE("water under 420 ppm of CO2: " << distilled << " uS/cm");
	CHECK(distilled > 0.4);
	CHECK(distilled < 1.5);
	// a quantity of a vessel that holds a liquid, written as a meter shows it
	bool listed = false;
	for (const lab::Quantity &q : lab::quantities_of(salt)) {
		listed = listed || q.what == "us_cm";
	}
	CHECK(listed);
	CHECK(lab::shown(lab::Quantity{ "salt", "us_cm", "g" }, 1412.6) == "1413 uS/cm");
	CHECK(lab::shown(lab::Quantity{ "salt", "us_cm", "g" }, 12856.0) == "12.86 mS/cm");
	CHECK(lab::shown(lab::Quantity{ "salt", "us_cm", "g" }, 1.234) == "1.23 uS/cm");

	// by instruments: the meter's reading, within its accuracy of the truth and on its range's resolution
	auto measured = open(bench(R"( "readings": "instruments", "seed": 11,)"));
	v = measured->view();
	const lab::instruments::ConductivityMeter meter = lab::instruments::conductivity_meter();
	std::vector<lab::instruments::Read> reads;
	for (const char *id : { "salt", "water" }) {
		const lab::ItemView i = item(v, id);
		REQUIRE(i.read_us_cm);
		double resolution = 0.0;
		lab::instruments::conductivity_reading(meter, *i.us_cm, 0.0, &resolution);
		reads.push_back(lab::instruments::Read{ std::string(id) + ": conductivity meter", *i.us_cm, *i.read_us_cm, meter.accuracy * *i.us_cm, resolution });
	}
	CHECK(lab::instruments::check_I1(reads) == "");
	CHECK_FALSE(item(v, "dry").read_us_cm);
	// I1 fires on a reading past its accuracy
	std::vector<lab::instruments::Read> past = reads;
	past[0].reading = past[0].truth * 1.02;
	CHECK(lab::instruments::check_I1(past).rfind("I1", 0) == 0);

	// recorded in the notebook, the meter's reading; not part of the world
	const std::string before = measured->digest();
	must(*measured, R"({"do": "record", "item": "salt", "what": "us_cm", "label": "KCl"})");
	v = measured->view();
	REQUIRE(v.notebook.size() == 1);
	CHECK(v.notebook[0].instrument);
	CHECK(v.notebook[0].value == *item(v, "salt").read_us_cm);
	CHECK(measured->digest() == before);
}

TEST_CASE("instruments: a conductivity meter reads to its range's resolution, within its accuracy (Stage 65)") {
	using namespace lab::instruments;
	const ConductivityMeter meter = conductivity_meter();
	CHECK(meter.accuracy == 0.005);
	double resolution = 0.0;
	CHECK(conductivity_reading(meter, 1.2345, 0.0, &resolution) == doctest::Approx(1.23));
	CHECK(resolution == 0.01);
	CHECK(conductivity_reading(meter, 141.27, 0.0, &resolution) == doctest::Approx(141.3));
	CHECK(conductivity_reading(meter, 1412.6, 0.0, &resolution) == doctest::Approx(1413.0));
	CHECK(resolution == 1.0);
	CHECK(conductivity_reading(meter, 12856.0, 0.0, &resolution) == doctest::Approx(12860.0));
	CHECK(resolution == 10.0);
	// its offset, a share of the reading
	CHECK(conductivity_reading(meter, 1000.0, 1.0) == doctest::Approx(1005.0));
	CHECK(conductivity_reading(meter, 1000.0, -1.0) == doctest::Approx(995.0));
}
