// Beams kept (Stage 33; GAME_SPEC §12.3, §18: O12): a pixel's beams are traced when what it looks through
// moves, kept in its record, and read from there. Held here: a kept beam is, value for value, the beam traced
// afresh, at every frame of the play a host times; the picture is the picture with nothing kept; a change of
// colour alone traces no beam.
#include "lab/optics_scene.hpp"
#include "lab/play.hpp"
#include "lab/ui.hpp"

#include <doctest/doctest.h>

#include <fstream>
#include <memory>
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

struct Traced {
	Traced() { lab::optics_reading(5); }
	~Traced() { lab::optics_reading(0); }
};

constexpr int STEP = 8;

} // namespace

TEST_CASE("kept: through the play, kept beams are the beams traced afresh (O12), the picture is the picture with none kept, and far fewer rays are followed (Stage 33)") {
	const Traced traced;
	std::string why;
	std::unique_ptr<lab::Ui> ui = lab::Ui::open(read("data/benches/lab.json"), why);
	REQUIRE_MESSAGE(ui != nullptr, why);
	lab::Play play;
	REQUIRE_MESSAGE(lab::Play::read(read("data/plays/cost.json"), play, why), why);
	// a sample every eighth pixel, and beside it a block of whole pixels over the flask's shoulder: there
	// neighbouring pixels share their corners
	lab::OpticsFrames kept(STEP / 2, STEP / 2, 1920 / STEP, 1080 / STEP, 16.0, STEP), afresh(STEP / 2, STEP / 2, 1920 / STEP, 1080 / STEP, 16.0, STEP);
	afresh.keep(false);
	afresh.share(false);
	float flask_x = 0.0f, flask_y = 0.0f;
	bool placed = false;
	int64_t held_most = 0, rays_kept = 0, rays_afresh = 0, differing = 0, rays_block = 0, rays_block_own = 0, block_differing = 0;
	std::unique_ptr<lab::OpticsFrames> shared, own;
	int frames = 0;
	for (int i = 0; i < 6000 && play.next(*ui); ++i) {
		ui->frame(1.0 / 120.0);
		if (i % 6 != 0) {
			continue;
		}
		const lab::OpticsScene &scene = ui->optics();
		kept.frame(scene);
		afresh.frame(scene);
		if (!placed && play.mark() == "settled" && ui->locate("item:flask", flask_x, flask_y)) {
			placed = true;
			shared = std::make_unique<lab::OpticsFrames>(static_cast<int>(flask_x) - 40, static_cast<int>(flask_y) - 30, 80, 60);
			own = std::make_unique<lab::OpticsFrames>(static_cast<int>(flask_x) - 40, static_cast<int>(flask_y) - 30, 80, 60);
			own->share(false);
		}
		if (placed) {
			shared->frame(scene);
			own->frame(scene);
			rays_block += shared->rays();
			rays_block_own += own->rays();
			for (int y = static_cast<int>(flask_y) - 30; y < static_cast<int>(flask_y) + 30; ++y) {
				for (int x = static_cast<int>(flask_x) - 40; x < static_cast<int>(flask_x) + 40; ++x) {
					double a[3], b[3];
					shared->colour(scene, x, y, a);
					own->colour(scene, x, y, b);
					block_differing += a[0] != b[0] || a[1] != b[1] || a[2] != b[2] ? 1 : 0;
				}
			}
		}
		++frames;
		rays_kept += kept.rays();
		rays_afresh += afresh.rays();
		int64_t held = 0;
		const std::string o12 = lab::check_O12(kept, scene, STEP / 2, STEP / 2, 1920, 1080, STEP, &held);
		CHECK_MESSAGE(o12 == "", "frame " << i << " (" << play.mark() << "): " << o12);
		held_most = std::max(held_most, held);
		for (int y = STEP / 2; y < 1080; y += STEP) {
			for (int x = STEP / 2; x < 1920; x += STEP) {
				double a[3], b[3];
				kept.colour(scene, x, y, a);
				afresh.colour(scene, x, y, b);
				differing += a[0] != b[0] || a[1] != b[1] || a[2] != b[2] ? 1 : 0;
			}
		}
		if (o12 != "") {
			break;
		}
	}
	CHECK(frames > 100);
	CHECK(held_most > 500); // (a check that held nothing checked nothing)
	CHECK(differing == 0);
	// (while things move, a pixel's beams are traced again at every frame, kept or not: what keeping saves there
	// is the pixels still settling beside what moves)
	MESSAGE("rays followed through the play: " << rays_kept << " with beams kept, " << rays_afresh << " with none");
	CHECK(rays_kept < rays_afresh);
	// and with the corners of neighbouring pixels shared: the same picture, value for value, from fewer rays
	REQUIRE(placed);
	CHECK(block_differing == 0);
	MESSAGE("rays followed over the flask's shoulder: " << rays_block << " with corners shared, " << rays_block_own << " with each pixel's own");
	CHECK(rays_block * 10 < rays_block_own * 8);
}

TEST_CASE("kept: while a bottle pours into a flask and a flask is stirred, frame after frame, a beam kept through a change of what lies in a vessel is the beam traced afresh (O12), and the rays that pass the change by are not followed again (Stage 33)") {
	const Traced traced;
	std::string why;
	std::unique_ptr<lab::Ui> ui = lab::Ui::open(read("data/benches/lab.json"), why);
	REQUIRE_MESSAGE(ui != nullptr, why);
	lab::Play play;
	REQUIRE_MESSAGE(lab::Play::read(read("data/plays/cost.json"), play, why), why);
	namespace L = lab::optics_layout;
	// the same frames twice: as the scene says, and with every change of what lies in a vessel said to be everywhere
	lab::OpticsFrames kept(STEP / 2, STEP / 2, 1920 / STEP, 1080 / STEP, 16.0, STEP), everywhere(STEP / 2, STEP / 2, 1920 / STEP, 1080 / STEP, 16.0, STEP);
	std::string mark;
	int in_mark = 0, frames = 0, changes = 0;
	int64_t rays = 0, rays_everywhere = 0, held_most = 0;
	for (int i = 0; i < 6000 && play.next(*ui); ++i) {
		ui->frame(1.0 / 120.0);
		in_mark = play.mark() == mark ? in_mark + 1 : 0;
		mark = play.mark();
		// the last of the pour, the stream running and the bottle still; and the stirring's beginning, its vortex forming
		if (!((mark == "pour" && in_mark >= 330) || (mark == "stir" && in_mark < 60))) {
			continue;
		}
		const lab::OpticsScene &scene = ui->optics();
		lab::OpticsScene wide = scene;
		bool changed = false;
		for (int b = 0; b < static_cast<int>(scene.table[0]); ++b) {
			const size_t at = static_cast<size_t>(b) * L::SHAPE_BLOCK * 4;
			changed = changed || scene.shapes[at + 33] >= scene.shapes[at + 32];
			for (std::vector<float> *shapes : { &wide.shapes }) {
				(*shapes)[at + 32] = (*shapes)[at + 36] = -1.0e30f;
				(*shapes)[at + 33] = (*shapes)[at + 37] = 1.0e30f;
			}
			wide.shapes_exact[at + 32] = wide.shapes_exact[at + 36] = -1.0e30;
			wide.shapes_exact[at + 33] = wide.shapes_exact[at + 37] = 1.0e30;
		}
		changes += changed ? 1 : 0;
		if (std::getenv("LAB_ZONES") != nullptr && in_mark % 20 == 1) {
			for (int b = 0; b < static_cast<int>(scene.table[0]); ++b) {
				const size_t at = static_cast<size_t>(b) * L::SHAPE_BLOCK * 4;
				const float *rect = &scene.table[static_cast<size_t>(L::HEADER_VECS + b * L::BODY_VECS) * 4];
				if (scene.shapes[at + 2] > 0.5f) {
					std::printf("%s %d body %d rect y %.0f..%.0f turn %.2f up (%.3f, %.3f) cos %.3f sin %.3f level %.3f: tags %.0f %.0f zone %.2f..%.2f\n", mark.c_str(), in_mark, b, rect[1], rect[1] + rect[3], scene.shapes[at + 3], scene.shapes[at + 12], scene.shapes[at + 13], scene.shapes[at + 6], scene.shapes[at + 7], scene.shapes[at + 15], scene.shapes[at + 30], scene.shapes[at + 31], scene.shapes[at + 32], scene.shapes[at + 33]);
				}
			}
		}
		kept.frame(scene);
		everywhere.frame(wide);
		++frames;
		rays += kept.rays();
		rays_everywhere += everywhere.rays();
		int64_t held = 0;
		const std::string o12 = lab::check_O12(kept, scene, STEP / 2, STEP / 2, 1920, 1080, STEP, &held);
		CHECK_MESSAGE(o12 == "", "frame " << i << " (" << mark << " " << in_mark << "): " << o12);
		held_most = std::max(held_most, held);
		if (o12 != "") {
			break;
		}
	}
	CHECK(frames > 100);
	CHECK(changes > 30); // (what lies in the vessels did change, frame after frame)
	CHECK(held_most > 300);
	MESSAGE(frames << " frames, " << changes << " of them with a change of what lies in a vessel: " << rays << " rays followed, " << rays_everywhere << " were every such change everywhere");
	CHECK(rays * 10 < rays_everywhere * 8); // (a quarter fewer: the rest are the stream, the corrections, and the rays that do pass the change)
}

TEST_CASE("kept: O12 fires where a beam is kept for what has since moved; a change of colour alone traces no beam (Stage 33)") {
	const Traced traced;
	std::string why;
	std::unique_ptr<lab::Ui> ui = lab::Ui::open(read("data/benches/lab.json"), why);
	REQUIRE_MESSAGE(ui != nullptr, why);
	lab::Play play;
	REQUIRE_MESSAGE(lab::Play::read(read("data/plays/cost.json"), play, why), why);
	for (int i = 0; i < 6000 && play.next(*ui) && play.mark() != "settled"; ++i) {
		ui->frame(1.0 / 120.0);
	}
	const lab::OpticsScene scene = ui->optics();
	namespace L = lab::optics_layout;
	lab::OpticsFrames kept(STEP / 2, STEP / 2, 1920 / STEP, 1080 / STEP, 16.0, STEP), afresh(STEP / 2, STEP / 2, 1920 / STEP, 1080 / STEP, 16.0, STEP);
	afresh.keep(false);
	afresh.share(false);
	for (int f = 0; f < 6; ++f) {
		kept.frame(scene);
		afresh.frame(scene);
	}
	int64_t held = 0;
	REQUIRE(lab::check_O12(kept, scene, STEP / 2, STEP / 2, 1920, 1080, STEP, &held) == "");
	REQUIRE(held > 500);

	SUBCASE("a liquid's level moved under the same shape tags: the kept beams are no longer the beams") {
		lab::OpticsScene moved = scene;
		bool any = false;
		for (int b = 0; b < L::MAX_BODIES; ++b) {
			double *lie = &moved.shapes_exact[static_cast<size_t>(b) * L::SHAPE_BLOCK * 4];
			if (lie[0] >= 0.0 && lie[14] > 0.5) {
				lie[15] -= 0.5; // its liquid half a centimetre lower, and nothing said
				any = true;
			}
		}
		REQUIRE(any);
		CHECK(lab::check_O12(kept, moved, STEP / 2, STEP / 2, 1920, 1080, STEP) != "");
	}

	SUBCASE("every body's colour changed: each pixel reads its fixed estimate again from the beams it kept") {
		lab::OpticsScene dyed = scene;
		const int count = static_cast<int>(dyed.table[0]);
		for (int b = 0; b < count; ++b) {
			dyed.table[static_cast<size_t>(L::HEADER_VECS + b * L::BODY_VECS + L::IDS) * 4 + 1] += 1.0f; // its content's tag
		}
		kept.frame(dyed);
		afresh.frame(dyed);
		CHECK(kept.read() == afresh.read());
		CHECK(kept.read() > 500);
		// with none kept each pixel traced its beam again, twenty rays and one; kept, only the frame's corrections
		// (but for the few whose spectrum is read in more stretches than a record holds)
		CHECK(afresh.rays() - kept.rays() >= 20 * kept.read());
		CHECK(kept.rays() <= 17 * kept.read());
		CHECK(lab::check_O12(kept, dyed, STEP / 2, STEP / 2, 1920, 1080, STEP) == "");
	}
}
