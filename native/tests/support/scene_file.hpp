#pragma once
// An optical scene to a file and back (Stage 68): the screen's scene at a command of a play, kept by the case that
// played it (a ctest fixture, fixtures.hpp) for the cases that hold a pixel of it to the truth, which then play
// nothing. Every number of the scene, bit for bit; its glassware by kind and size (glass::find, made once a
// process). The case that writes one reads it back and holds it to the scene it wrote (test_demo_record.cpp).
#include "lab/glassware.hpp"
#include "lab/optics_scene.hpp"

#include <cstdint>
#include <cstring>
#include <string>
#include <type_traits>
#include <vector>

namespace scene_file {

namespace detail {

template <typename T>
void put(std::string &out, const T &v) {
	static_assert(std::is_trivially_copyable_v<T>);
	out.append(reinterpret_cast<const char *>(&v), sizeof v);
}

template <typename T>
void put(std::string &out, const std::vector<T> &v) {
	put(out, static_cast<uint64_t>(v.size()));
	out.append(reinterpret_cast<const char *>(v.data()), v.size() * sizeof(T));
}

struct Reader {
	const std::string &in;
	size_t at = 0;
	bool ok = true;
	template <typename T>
	void get(T &v) {
		static_assert(std::is_trivially_copyable_v<T>);
		if (at + sizeof v > in.size()) {
			ok = false;
			return;
		}
		std::memcpy(&v, in.data() + at, sizeof v);
		at += sizeof v;
	}
	template <typename T>
	void get(std::vector<T> &v) {
		uint64_t n = 0;
		get(n);
		if (!ok || at + n * sizeof(T) > in.size()) {
			ok = false;
			return;
		}
		v.resize(n);
		std::memcpy(v.data(), in.data() + at, n * sizeof(T));
		at += n * sizeof(T);
	}
};

} // namespace detail

inline constexpr uint64_t MAGIC = 0x3165637332656863ull; // "chem2sce1"

// (lab::OpticsScene's size where this was written: a member added there changes it, and this asks for it here)
template <size_t N>
struct SceneSize {
	static_assert(N == 424, "lab::OpticsScene has changed: write and read its new members here, then set its size");
};
inline constexpr SceneSize<sizeof(lab::OpticsScene)> scene_size{};

// (every member of lab::OpticsScene, in its order: one added there is one to add here, or the scene read back is
// not the scene written, and the case that writes it fails)
inline std::string write(const lab::OpticsScene &s) {
	using detail::put;
	std::string out;
	put(out, MAGIC);
	put(out, s.table);
	put(out, s.rows);
	put(out, s.lines);
	put(out, s.lights);
	put(out, s.lights_kelvin);
	put(out, s.revision);
	put(out, s.wall);
	put(out, s.wall_revision);
	put(out, s.wall_key);
	put(out, s.drawn);
	put(out, s.far_left);
	put(out, s.far_right);
	put(out, s.wall_beyond);
	put(out, s.rectangles);
	put(out, s.rectangle_bodies);
	put(out, static_cast<uint64_t>(s.vessels.size()));
	for (const lab::glass::Vessel *v : s.vessels) {
		const std::string kind = v != nullptr ? v->kind : std::string();
		put(out, static_cast<uint64_t>(kind.size()));
		out += kind;
		put(out, v != nullptr ? v->nominal_ml : 0.0);
	}
	put(out, s.shapes);
	put(out, s.shapes_revision);
	put(out, s.shapes_exact);
	put(out, s.lies);
	put(out, s.images);
	put(out, s.images_revision);
	put(out, s.images_key);
	put(out, s.image_weights);
	put(out, s.relay_pool);
	return out;
}

// false when the text is not a scene's
inline bool read(const std::string &in, lab::OpticsScene &s) {
	detail::Reader r{ in };
	uint64_t magic = 0;
	r.get(magic);
	if (magic != MAGIC) {
		return false;
	}
	r.get(s.table);
	r.get(s.rows);
	r.get(s.lines);
	r.get(s.lights);
	r.get(s.lights_kelvin);
	r.get(s.revision);
	r.get(s.wall);
	r.get(s.wall_revision);
	r.get(s.wall_key);
	r.get(s.drawn);
	r.get(s.far_left);
	r.get(s.far_right);
	r.get(s.wall_beyond);
	r.get(s.rectangles);
	r.get(s.rectangle_bodies);
	uint64_t vessels = 0;
	r.get(vessels);
	s.vessels.clear();
	for (uint64_t k = 0; r.ok && k < vessels; ++k) {
		uint64_t n = 0;
		r.get(n);
		if (!r.ok || r.at + n > in.size()) {
			return false;
		}
		const std::string kind = in.substr(r.at, n);
		r.at += n;
		double ml = 0.0;
		r.get(ml);
		s.vessels.push_back(kind.empty() ? nullptr : lab::glass::find(kind, ml));
	}
	r.get(s.shapes);
	r.get(s.shapes_revision);
	r.get(s.shapes_exact);
	r.get(s.lies);
	r.get(s.images);
	r.get(s.images_revision);
	r.get(s.images_key);
	r.get(s.image_weights);
	r.get(s.relay_pool);
	return r.ok && r.at == in.size();
}

} // namespace scene_file
