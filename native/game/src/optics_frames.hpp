// Internal to the lab layer: the renderer's own frames (lab/optics_scene.hpp: OpticsFrames), behind one face for
// the per-pixel source compiled in float64 (the reference, optics_reference.cpp) and compiled whole in single
// precision as the GPU runs it (optics_single.cpp; GAME_SPEC §12.3, Stage 35).
#pragma once

#include "lab/optics_scene.hpp"

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <thread>
#include <vector>

namespace lab {

// The reference's loops over pixels and rays are run in pieces on a few threads: `piece(i)` for each i of `count`
// pieces, each done whole by one thread. A piece is the same piece on every machine and results are joined in
// the pieces' order, so a sum comes out the same however many threads there are. The scene in use is each
// thread's own (shader::current): a piece names its own.
template <typename Piece>
void in_pieces(int count, const Piece &piece) {
	// half the machine's threads, six at most (measured on six fast cores and six slow ones: 20 s on one thread,
	// 7.6 s on four, 14 s on twelve); or LAB_THREADS, where several such loops run side by side (ctest)
	static const int most = [] {
		const char *asked = std::getenv("LAB_THREADS");
		const int n = asked != nullptr ? std::atoi(asked) : static_cast<int>(std::min(6u, std::thread::hardware_concurrency() / 2));
		return n < 1 ? 1 : n > 64 ? 64 : n;
	}();
	const int threads = std::max(1, std::min(count, most));
	if (threads == 1) {
		for (int i = 0; i < count; ++i) {
			piece(i);
		}
		return;
	}
	std::atomic<int> next{ 0 };
	std::vector<std::thread> workers;
	for (int t = 0; t < threads; ++t) {
		workers.emplace_back([&] {
			for (int i = next.fetch_add(1); i < count; i = next.fetch_add(1)) {
				piece(i);
			}
		});
	}
	for (std::thread &w : workers) {
		w.join();
	}
}

// The pixels of a sample and their histories, stepped a frame at a time by the per-pixel source in one
// arithmetic. What goes in and out is in double whatever the arithmetic is.
struct OpticsFramesCore {
	// a frame's settings in; its totals out (the next frame's budgets are scaled by them)
	struct Frame {
		int number = 0;
		double budget = 16.0;
		bool keeping = true, sharing = true;
		double asked = 0.0, ray_asked = 0.0, beams_traced = 0.0, points = 0.0;
		int64_t read = 0, rays = 0, most_rays = 0;
	};
	virtual ~OpticsFramesCore() = default;
	// (false, and nothing done, where the scene is not one the source can read)
	virtual bool frame(const OpticsScene &scene, Frame &f, std::vector<uint8_t> &restarted) = 0;
	virtual void colour(const OpticsScene &scene, int x, int y, double rgb[3]) const = 0;
	virtual void shown(const OpticsScene &scene, int x, int y, double rgba[4]) const = 0;
	virtual bool kept_fresh(const OpticsScene &scene, int x, int y, bool &holds) const = 0;
	virtual double spent(int x, int y) const = 0;
};
// (`step`: every step-th pixel across and down, from (x, y); w and h are then how many across and down)
OpticsFramesCore *optics_frames_exact(int x, int y, int w, int h, int step);
OpticsFramesCore *optics_frames_single(int x, int y, int w, int h, int step);

} // namespace lab
