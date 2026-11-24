// The per-pixel source (native/game/shader/optics_pixel.glsl) compiled as C++: what the shading language has
// and C++ has not, the scene's buffers, the source itself, and the frames of a sample of pixels (GAME_SPEC
// §12.2, §12.3). Included inside a namespace of its own by each arithmetic it is compiled in: float64, the
// reference (optics_reference.cpp); single precision as the GPU's (optics_single.cpp, Stage 35). Before it the
// includer gives:
//   real                       the arithmetic's number, with min, max, abs, sqrt, exp, log, pow, sin, cos, acos,
//                              asin, atan, floor, fract and clamp on it, and to_double(real)
//   OPTICS_HOST_SHAPES(scene)  the shapes' numbers as that arithmetic reads them
//   OPTICS_HOST_BETWEEN        how far a ray between two nodes may be from their cubic before a stretch is halved
//   OPTICS_HOST_NAMESPACE      the namespace it is included in
//   TRACE_NEAR, TRACE_NUDGE, TRACE_SLACK, TRACE_FINE (and TRACE_ROUGH, if not the source's own)
using uint = std::uint32_t;
struct vec2 {
	real x = 0.0, y = 0.0;
	vec2() = default;
	vec2(real a, real b) :
			x(a), y(b) {}
};
struct vec3 {
	real x = 0.0, y = 0.0, z = 0.0;
	vec3() = default;
	explicit vec3(real a) :
			x(a), y(a), z(a) {}
	vec3(real a, real b, real c) :
			x(a), y(b), z(c) {}
};
struct vec4 {
	real x = 0.0, y = 0.0, z = 0.0, w = 0.0;
	real &operator[](int i) { return i == 0 ? x : i == 1 ? y : i == 2 ? z : w; }
	real operator[](int i) const { return i == 0 ? x : i == 1 ? y : i == 2 ? z : w; }
	vec4() = default;
	explicit vec4(real a) :
			x(a), y(a), z(a), w(a) {}
	vec4(real a, real b, real c, real d) :
			x(a), y(b), z(c), w(d) {}
};
inline vec3 operator+(const vec3 &a, const vec3 &b) {
	return vec3(a.x + b.x, a.y + b.y, a.z + b.z);
}
inline vec3 operator-(const vec3 &a, const vec3 &b) {
	return vec3(a.x - b.x, a.y - b.y, a.z - b.z);
}
inline vec3 operator*(const vec3 &a, real s) {
	return vec3(a.x * s, a.y * s, a.z * s);
}
inline vec3 operator/(const vec3 &a, real s) {
	return vec3(a.x / s, a.y / s, a.z / s);
}
inline vec4 operator+(const vec4 &a, const vec4 &b) {
	return vec4(a.x + b.x, a.y + b.y, a.z + b.z, a.w + b.w);
}
inline vec4 operator*(const vec4 &a, real s) {
	return vec4(a.x * s, a.y * s, a.z * s, a.w * s);
}
inline real dot(const vec4 &a, const vec4 &b) {
	return a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
}
inline real mix(real a, real b, real t) {
	return a * (1.0 - t) + b * t;
}
inline vec3 mix(const vec3 &a, const vec3 &b, real t) {
	return a * (1.0 - t) + b * t;
}
inline vec4 mix(const vec4 &a, const vec4 &b, real t) {
	return a * (1.0 - t) + b * t;
}

// the scene being drawn (one a thread: the comparison below runs on several)
thread_local const OpticsScene *current = nullptr;
inline vec4 scene_vec(int i) {
	const float *v = &current->table[static_cast<size_t>(i) * 4];
	return vec4(v[0], v[1], v[2], v[3]);
}
inline real row_value(int i) {
	return current->rows[static_cast<size_t>(i)];
}
inline vec4 line_vec(int i) {
	const float *v = &current->lines[static_cast<size_t>(i) * 4];
	return vec4(v[0], v[1], v[2], v[3]);
}
inline vec4 light_vec(int i) {
	const float *v = &current->lights[static_cast<size_t>(i) * 4];
	return vec4(v[0], v[1], v[2], v[3]);
}
inline vec4 wall_vec(int i) {
	const float *v = &current->wall[static_cast<size_t>(i) * 4];
	return vec4(v[0], v[1], v[2], v[3]);
}

// the images things emit by (Stage 42): none in a scene with no screen, and none is read there
inline vec4 image_vec(int i) {
	const float *v = &current->images[static_cast<size_t>(i) * 4];
	return vec4(v[0], v[1], v[2], v[3]);
}
inline vec4 shape_vec(int i) {
	const auto *v = &OPTICS_HOST_SHAPES(*current)[static_cast<size_t>(i) * 4];
	return vec4(v[0], v[1], v[2], v[3]);
}
inline vec3 operator*(real s, const vec3 &a) {
	return vec3(a.x * s, a.y * s, a.z * s);
}
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wkeyword-macro"
#endif
// how many rays the pass's tracer followed on this thread (a count for the reference alone)
thread_local int64_t rays_followed = 0;
#define OPTICS_RAY_HOOK() (++rays_followed)
// what the tracer did on this thread (Stage 33; lab/optics_scene.hpp: optics_work)
thread_local int64_t work_done[8] = {};
#define OPTICS_WORK_HOOK(k) (++work_done[k])
#define OPTICS_THREAD thread_local
// a pixel's kept beams (Stage 33): the record of the pixel being read, for the shared source's kept_vec and kept_put
thread_local real *kept_now = nullptr;
inline vec4 kept_vec(int i) {
	const real *v = &kept_now[static_cast<size_t>(i) * 4];
	return vec4(v[0], v[1], v[2], v[3]);
}
inline void kept_put(int i, const vec4 &v) {
	real *to = &kept_now[static_cast<size_t>(i) * 4];
	to[0] = v.x;
	to[1] = v.y;
	to[2] = v.z;
	to[3] = v.w;
}
// the lattice of pixel corners (Stage 33): each corner's record, by its row and its place across; a corner
// never traced reads as nothing, as the GPU's cleared buffer does
using Lattice = std::vector<std::unordered_map<int, std::array<real, 16>>>;
thread_local Lattice *lattice_now = nullptr;
inline vec4 corner_vec(int i) {
	const int corner = i / 4, k = i % 4;
	const auto &row = (*lattice_now)[static_cast<size_t>(corner / 1921)];
	const auto found = row.find(corner % 1921);
	if (found == row.end()) {
		return vec4(0.0);
	}
	const real *v = &found->second[static_cast<size_t>(k) * 4];
	return vec4(v[0], v[1], v[2], v[3]);
}
inline void corner_put(int i, const vec4 &v) {
	const int corner = i / 4, k = i % 4;
	real *to = &(*lattice_now)[static_cast<size_t>(corner / 1921)][corner % 1921][static_cast<size_t>(k) * 4];
	to[0] = v.x;
	to[1] = v.y;
	to[2] = v.z;
	to[3] = v.w;
}
#define float real
// (whether a stretch of the spectrum is halved where its nodes miss the ray between them: off for O10's own test)
thread_local real beam_between = OPTICS_HOST_BETWEEN;
#define BEAM_BETWEEN beam_between
// the stretches of the spectrum the last pixel read was read in (for O10)
thread_local std::vector<real> beam_stretches;
#define OPTICS_STRETCH_HOOK(lo, hi) (beam_stretches.push_back(lo), beam_stretches.push_back(hi))
// the anchor a sample is carried from (Stage 42: a view's change; Frames::resample), its states read through it
thread_local const OpticsFramesCore *anchor_core = nullptr;
inline vec4 anchor_vec(int which, int x, int y) {
	if (anchor_core == nullptr) {
		return vec4(0.0);
	}
	double hist[4], prox[4], aux[4];
	anchor_core->state(x, y, hist, prox, aux);
	const double *v = which == 0 ? hist : which == 1 ? prox : aux;
	if (which == 3) {
		return vec4(real(prox[0] + hist[0]), real(prox[1] + hist[1]), real(prox[2] + hist[2]), real(1.0));
	}
	return vec4(real(v[0]), real(v[1]), real(v[2]), real(v[3]));
}
#include "../shader/optics_pixel.glsl"
#undef float
#if defined(__clang__)
#pragma clang diagnostic pop
#endif

static_assert(HEADER_VECS == optics_layout::HEADER_VECS && BODY_VECS == optics_layout::BODY_VECS && ROW == optics_layout::ROW && ROWS_PER_SET == optics_layout::ROWS_PER_SET &&
				LIGHT_ENTRIES == optics_layout::LIGHT_ENTRIES,
		"the per-pixel source and lab/optics_scene.hpp lay the scene out alike");
static_assert(1 + KEPT_MOST * KEPT_BEAM == optics_layout::KEPT_VECS, "a pixel's kept beams are a record of KEPT_VECS");
static_assert(TRACE_MOST == trace::MEETINGS_MOST && TRACE_TURNS == trace::TURNS_MOST, "the pass's tracer and the truth's allow a ray the same meetings and turns");

struct Use {
	explicit Use(const OpticsScene &s) {
		current = &s;
	}
	~Use() { current = nullptr; }
	Use(const Use &) = delete;
	Use &operator=(const Use &) = delete;
};

bool ready(const OpticsScene &s) {
	return s.table.size() == static_cast<size_t>(optics_layout::SIZE_VECS) * 4 && s.rows.size() % static_cast<size_t>(optics_layout::ROWS_PER_SET * optics_layout::ROW) == 0 && !s.wall.empty() &&
		   s.lines.size() == static_cast<size_t>(optics_layout::MAX_LINES) * 4 && s.lights.size() == static_cast<size_t>(optics_layout::LIGHTS * optics_layout::LIGHT_SIZE) * 4;
}

Path at(int x, int y) {
	return path_of(vec2(x + 0.5, y + 0.5), 1.0);
}

// How wide a point is, in pixels, as the pass's rays read the bodies they meet (ray_shade's layer_at).
constexpr double POINT_SIZE = 1.0e-3;

// One ray's light at one wavelength through the point p of the screen (Stage 42: the spectrum probe's sample;
// GAME_SPEC §12), by the pass's own source: where nothing bends it, the bodies as they lie at the point and the
// wall there; else the ray traced through every body to where it lands, through a grating each of its ways by
// its share. A factor of the room's light, as optics_through.
real point_light(vec2 p, real nm) {
	const Path q = path_of(p, POINT_SIZE);
	if (!beamed(q)) {
		return flat_band(q, 0.0, 0.0, 0.0, nm);
	}
	real light = 0.0;
	int ways = 1;
	for (int way = 0; way < ways; ++way) {
		ray_way = way;
		const RayMet m = ray_met(p, nm);
		ways = way == 0 ? m.ways : ways;
		const vec2 t = ray_shade(m, nm);
		light = light + (ways > 1 ? way_share(way) : 1.0) * (t.x * wall_point(vec2(m.x, m.y)) + t.y);
	}
	ray_way = 0;
	return light;
}

// ---- the frames of a sample of pixels (optics_frames.hpp; lab/optics_scene.hpp: OpticsFrames)

struct Frames final : OpticsFramesCore {
	struct State {
		PixelState s;
		// its beams as they are kept (the shared source's record: Stage 33); nothing kept at first
		std::array<real, static_cast<size_t>(1 + KEPT_MOST * KEPT_BEAM) * 4> kept{};
	};
	int x_, y_, w_, h_, step_;
	Lattice corners_ = Lattice(1082);
	std::vector<State> states_;

	Frames(int x, int y, int w, int h, int step) :
			x_(x), y_(y), w_(w), h_(h), step_(step), states_(static_cast<size_t>(w) * static_cast<size_t>(h)) {
		for (State &st : states_) {
			st.s.hist = st.s.prox = st.s.aux = vec4(0.0);
			st.s.key = vec4(-1.0); // matches no content: the first frame starts every history
		}
	}
	// the state of pixel (x, y) of the screen, if it is of the sample's rectangle
	const State *state_at(int x, int y) const {
		const int col = (x - x_) / step_, row = (y - y_) / step_;
		if (x < x_ || y < y_ || col >= w_ || row >= h_) {
			return nullptr;
		}
		return &states_[static_cast<size_t>(row) * static_cast<size_t>(w_) + static_cast<size_t>(col)];
	}

	bool frame(const OpticsScene &scene, Frame &f, std::vector<uint8_t> &restarted) override {
		if (!ready(scene)) {
			return false;
		}
		// the screen's budget for the pixels no body bends, the rays' for those read by their own rays (of the
		// whole screen's, the sample's share)
		const real scale = budget_scale(real(f.asked - f.ray_asked), real(f.budget), real(static_cast<double>(w_) * h_));
		const real ray_scale = budget_scale(real(f.ray_asked), real(std::max(optics_layout::RAY_POINTS / (static_cast<double>(step_) * step_) - to_double(RAY_POINTS_A_BEAM) * f.beams_traced, 0.0)), real(1.0));
		// a row a piece: a pixel's step touches its own state alone
		std::vector<double> asked_of(static_cast<size_t>(h_), 0.0), points_of(static_cast<size_t>(h_), 0.0), ray_asked_of(static_cast<size_t>(h_), 0.0), traced_of(static_cast<size_t>(h_), 0.0);
		std::vector<int64_t> read_of(static_cast<size_t>(h_), 0), rays_of(static_cast<size_t>(h_), 0), most_of(static_cast<size_t>(h_), 0);
		// The corners first (Stage 33; as the GPU's pass over the lattice before the pixels'): those of every pixel
		// of the sample, a row of corners a piece (a corner's record is its row's)
		std::vector<int> corner_rows;
		if (f.sharing) {
			for (int row = 0; row < h_; ++row) {
				for (int below = 0; below < 2; ++below) {
					const int y = y_ + row * step_ + below;
					if (corner_rows.empty() || corner_rows.back() < y) {
						corner_rows.push_back(y);
					}
				}
			}
		}
		std::vector<int64_t> corner_rays(corner_rows.size(), 0);
		in_pieces(static_cast<int>(corner_rows.size()), [&](int piece) {
			const Use use(scene);
			lattice_now = &corners_;
			const int64_t before = rays_followed;
			const int y = corner_rows[static_cast<size_t>(piece)];
			int last = -1;
			for (int col = 0; col < w_; ++col) {
				for (int right = 0; right < 2; ++right) {
					const int x = x_ + col * step_ + right;
					if (x > last) {
						corner_step(vec2(x, y));
						last = x;
					}
				}
			}
			corner_rays[static_cast<size_t>(piece)] = rays_followed - before;
			lattice_now = nullptr;
		});
		in_pieces(h_, [&](int row) {
			const Use use(scene);
			double &asked = asked_of[static_cast<size_t>(row)], &points = points_of[static_cast<size_t>(row)];
			for (int col = 0; col < w_; ++col) {
				const size_t i = static_cast<size_t>(row) * static_cast<size_t>(w_) + static_cast<size_t>(col);
				const int x = x_ + col * step_, y = y_ + row * step_;
				const Path q = at(x, y);
				restarted[i] = 0;
				if (q.cover <= 0.0) {
					continue; // nothing here: the pixel keeps what it had, and asks nothing
				}
				PixelState &st = states_[i].s;
				if (done(q, st.key, st.aux)) {
					st.aux.w = 0.0; // as the GPU, it does nothing; what it spent is nothing
					continue;
				}
				const double before = to_double(st.hist.w);
				const int64_t rays_before = rays_followed;
				kept_now = states_[i].kept.data();
				kept_slot = 0;
				kept_read = f.keeping;
				lattice_now = &corners_;
				lattice_on = f.sharing;
				const bool by_rays = beamed(q);
				st = step_pixel(q, st, vec2(x + 0.5, y + 0.5), f.number, by_rays ? ray_scale : scale);
				lattice_on = false;
				kept_slot = -1;
				const int64_t followed = rays_followed - rays_before;
				++read_of[static_cast<size_t>(row)];
				rays_of[static_cast<size_t>(row)] += followed;
				most_of[static_cast<size_t>(row)] = std::max(most_of[static_cast<size_t>(row)], followed);
				// a history that had points and is now one frame old (or none) started again
				restarted[i] = before > 0.0 && to_double(st.aux.y) <= 1.0 ? 1 : 0;
				asked += to_double(st.aux.z);
				ray_asked_of[static_cast<size_t>(row)] += by_rays ? to_double(st.aux.z) : 0.0;
				traced_of[static_cast<size_t>(row)] += beam_deferred ? 1.0 : 0.0;
				points += to_double(st.aux.w);
			}
		});
		f.asked = f.points = f.ray_asked = f.beams_traced = 0.0;
		for (const double a : ray_asked_of) {
			f.ray_asked += a;
		}
		for (const double a : traced_of) {
			f.beams_traced += a;
		}
		f.read = f.rays = f.most_rays = 0;
		for (int row = 0; row < h_; ++row) {
			f.asked += asked_of[static_cast<size_t>(row)];
			f.points += points_of[static_cast<size_t>(row)];
			f.read += read_of[static_cast<size_t>(row)];
			f.rays += rays_of[static_cast<size_t>(row)];
			f.most_rays = std::max(f.most_rays, most_of[static_cast<size_t>(row)]);
		}
		// (a corner is four pixels': in a sample each pixel has corners of its own, each counted as the quarter it is
		// of a pixel's cost where the pixels lie side by side)
		for (const int64_t r : corner_rays) {
			f.rays += step_ > 1 ? r / 4 : r;
		}
		return true;
	}

	void colour(const OpticsScene &scene, int x, int y, double rgb[3]) const override {
		rgb[0] = rgb[1] = rgb[2] = 0.0;
		const State *st = state_at(x, y);
		if (!ready(scene) || st == nullptr) {
			return;
		}
		rgb[0] = to_double(st->s.prox.x + st->s.hist.x);
		rgb[1] = to_double(st->s.prox.y + st->s.hist.y);
		rgb[2] = to_double(st->s.prox.z + st->s.hist.z);
	}

	void shown(const OpticsScene &scene, int x, int y, double rgba[4]) const override {
		rgba[0] = rgba[1] = rgba[2] = rgba[3] = 0.0;
		const State *st = state_at(x, y);
		if (!ready(scene) || st == nullptr) {
			return;
		}
		const Use use(scene);
		const vec4 c = lab::OPTICS_HOST_NAMESPACE::shown(at(x, y), st->s);
		rgba[0] = to_double(c.x);
		rgba[1] = to_double(c.y);
		rgba[2] = to_double(c.z);
		rgba[3] = to_double(c.w);
	}

	bool kept_fresh(const OpticsScene &scene, int x, int y, bool &holds) const override {
		holds = false;
		const State *st = state_at(x, y);
		if (!ready(scene) || st == nullptr || (x - x_) % step_ != 0 || (y - y_) % step_ != 0) {
			return true;
		}
		const Use use(scene);
		const Path q = at(x, y);
		const auto &kept = st->kept;
		const real pixel = floor(q.x) + 1920.0 * floor(q.y);
		if (!(kept[0] == q.shape.x && kept[1] == q.shape.y && kept[2] == pixel && kept[3] > 0.5)) {
			return true; // nothing kept for what it looks through now: its next read traces
		}
		holds = true;
		std::remove_const_t<std::remove_reference_t<decltype(kept)>> fresh{};
		kept_now = fresh.data();
		kept_slot = 0;
		beam_means(q, 0, 0.5, cuts_none(), 0.0, 1.0, false, 0, false); // traced, and kept: nothing read
		kept_slot = -1;
		kept_now = nullptr;
		return fresh == kept;
	}

	double spent(int x, int y) const override {
		const State *st = state_at(x, y);
		return st != nullptr ? to_double(st->s.aux.w) : 0.0;
	}

	void state(int x, int y, double hist[4], double prox[4], double aux[4]) const override {
		// (the nearest of the sample's pixels: a place past its edge reads its edge's)
		const int col = std::clamp((x - x_) / step_, 0, std::max(w_ - 1, 0)), row = std::clamp((y - y_) / step_, 0, std::max(h_ - 1, 0));
		const PixelState &s = states_[static_cast<size_t>(row) * static_cast<size_t>(w_) + static_cast<size_t>(col)].s;
		const vec4 *from[3] = { &s.hist, &s.prox, &s.aux };
		double *to[3] = { hist, prox, aux };
		for (int k = 0; k < 3; ++k) {
			to[k][0] = to_double(from[k]->x);
			to[k][1] = to_double(from[k]->y);
			to[k][2] = to_double(from[k]->z);
			to[k][3] = to_double(from[k]->w);
		}
	}

	void resample(const OpticsScene &scene, const OpticsFramesCore &anchor, const double now[4], const double from[4], bool full_key) override {
		if (!ready(scene)) {
			return;
		}
		const vec4 v_now(real(now[0]), real(now[1]), real(now[2]), real(0.0)), v_from(real(from[0]), real(from[1]), real(from[2]), real(0.0));
		in_pieces(h_, [&](int row) {
			const Use use(scene);
			anchor_core = &anchor;
			for (int col = 0; col < w_; ++col) {
				State &st = states_[static_cast<size_t>(row) * static_cast<size_t>(w_) + static_cast<size_t>(col)];
				const int x = x_ + col * step_, y = y_ + row * step_;
				const Path q = at(x, y);
				st.kept = {};
				if (q.cover <= 0.0) {
					st.s.hist = st.s.prox = st.s.aux = vec4(0.0);
					st.s.key = vec4(-3.0); // (the wall: shown again from the wall's table)
					continue;
				}
				st.s = anchor_state(vec2(x + 0.5, y + 0.5), v_now, v_from, q.key.z);
				if (full_key) {
					st.s.key = q.key;
				}
			}
			anchor_core = nullptr;
		});
	}
};
