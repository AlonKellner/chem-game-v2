// The user's ghost (Stage 77; docs/TDD_77_GHOST.md): salt poured by hand into the freezing point's ice bath, the bath
// stirred, its pile lowered slowly (a fraction of a pixel a frame at the room's whole view): the picture left the
// pile's old shape behind, a cone over the real one, and kept it once still. Its pixels' rays had moved as the pile's
// surface swept across them, yet the drift rule kept the corrections their old rays had found; settled, nothing asked
// for more. Held here: the play that showed it, the renderer's own frames over the bath, every pixel the truth's.
#include "lab/colour.hpp"
#include "lab/lab.hpp"
#include "lab/optics_scene.hpp"
#include "lab/play.hpp"
#include "lab/ui.hpp"

#include <doctest/doctest.h>

#include <fstream>
#include <memory>
#include <optional>
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

double eye_between(const double a[3], const double b[3]) {
	const auto xyz = [](const double c[3]) {
		return chem::optics::Xyz{ 0.4124564 * c[0] + 0.3575761 * c[1] + 0.1804375 * c[2], 0.2126729 * c[0] + 0.7151522 * c[1] + 0.0721750 * c[2], 0.0193339 * c[0] + 0.1191920 * c[1] + 0.9503041 * c[2] };
	};
	return lab::eye_difference(lab::shown(xyz(a)), lab::shown(xyz(b)));
}

} // namespace

TEST_CASE("ghost: salt poured by hand into a bath stirred, its pile lowering slowly, leaves no shape of where it was (Stage 77)") {
	std::string why;
	std::unique_ptr<lab::Ui> ui = lab::Ui::open(read("data/benches/lab.json"), why);
	REQUIRE_MESSAGE(ui != nullptr, why);
	REQUIRE_MESSAGE(ui->experiments(read("data/experiments.json"), why, [](const std::string &path) -> std::optional<std::string> { return read("data/" + path); }), why);
	lab::Play play;
	REQUIRE_MESSAGE(lab::Play::read(read("data/plays/ghost.json"), play, why), why);
	std::unique_ptr<lab::OpticsFrames> frames;
	int x0 = 0, y0 = 0, w = 0, h = 0;
	for (int k = 0; k < 5000 && play.next(*ui); ++k) {
		ui->frame(1.0 / 120.0); // (a play's frames are a 120 Hz display's, as the host plays it)
		if (!frames && ui->experiment() == "freezing_point") {
			float bx = 0, by = 0;
			REQUIRE(ui->locate("item:bath", bx, by));
			x0 = static_cast<int>(bx) - 42; // the bath's inside, as the host draws it: the frames as the GPU's
			y0 = static_cast<int>(by) - 54;
			w = 84;
			h = 108;
			frames = std::make_unique<lab::OpticsFrames>(x0, y0, w, h, 16.0, 1, true);
		}
		if (frames) {
			frames->frame(ui->optics());
		}
		if (play.shot() == "ghost-4-paused-settled") {
			break; // paused, and settled 240 frames
		}
	}
	REQUIRE(frames);
	REQUIRE(play.mark() == "paused");
	const lab::OpticsScene &scene = ui->optics();
	int apart = 0, wx = 0, wy = 0;
	double worst = 0.0;
	for (int j = 0; j < h; ++j) {
		for (int i = 0; i < w; ++i) {
			if (lab::optics_cover(scene, x0 + i, y0 + j) < lab::OPTICS_PARITY_COVER) {
				continue; // (the wall beside its rounded corners: the host draws it from its table, as O4 leaves it)
			}
			double a[3], t[3];
			frames->colour(scene, x0 + i, y0 + j, a);
			lab::optics_area_truth(scene, x0 + i, y0 + j, 3, t);
			const double e = eye_between(a, t);
			apart += e > 8.0 ? 1 : 0;
			if (e > worst) {
				worst = e;
				wx = x0 + i;
				wy = y0 + j;
			}
		}
	}
	MESSAGE(apart << " of " << w * h << " pixels of the bath visibly apart from the truth; the worst " << worst << " at " << wx << "," << wy);
	CHECK(apart <= 2); // (a pixel or two at the glass's rim, its sub-pixel edge: as anywhere)
}
