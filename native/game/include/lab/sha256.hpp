#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

// SHA-256 (FIPS 180-4), for the state digest (GAME_SPEC §13): the same bytes
// give the same digest on every platform.
namespace lab {

class Sha256 {
public:
	void update(const void *data, size_t size);
	void update_u64(uint64_t v); // little-endian
	void update_i64(int64_t v) { update_u64(static_cast<uint64_t>(v)); }
	void update_double(double v); // its IEEE-754 bits
	std::string hex(); // finishes; 64 lower-case hex digits

private:
	void block(const uint8_t *p);
	std::array<uint32_t, 8> h_{ 0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au, 0x510e527fu, 0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u };
	std::array<uint8_t, 64> buffer_{};
	size_t fill_ = 0;
	uint64_t bits_ = 0;
};

} // namespace lab
