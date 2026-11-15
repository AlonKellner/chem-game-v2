#include "lab/sha256.hpp"

#include <bit>
#include <cstring>

namespace lab {

namespace {

constexpr uint32_t K[64] = {
	0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5, 0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe,
	0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da, 0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7,
	0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967, 0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85, 0xa2bfe8a1, 0xa81a664b,
	0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070, 0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
	0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2
};

} // namespace

void Sha256::block(const uint8_t *p) {
	uint32_t w[64];
	for (int i = 0; i < 16; ++i) {
		w[i] = static_cast<uint32_t>(p[4 * i]) << 24 | static_cast<uint32_t>(p[4 * i + 1]) << 16 | static_cast<uint32_t>(p[4 * i + 2]) << 8 | static_cast<uint32_t>(p[4 * i + 3]);
	}
	for (int i = 16; i < 64; ++i) {
		const uint32_t s0 = std::rotr(w[i - 15], 7) ^ std::rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
		const uint32_t s1 = std::rotr(w[i - 2], 17) ^ std::rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
		w[i] = w[i - 16] + s0 + w[i - 7] + s1;
	}
	uint32_t a = h_[0], b = h_[1], c = h_[2], d = h_[3], e = h_[4], f = h_[5], g = h_[6], h = h_[7];
	for (int i = 0; i < 64; ++i) {
		const uint32_t s1 = std::rotr(e, 6) ^ std::rotr(e, 11) ^ std::rotr(e, 25);
		const uint32_t ch = (e & f) ^ (~e & g);
		const uint32_t t1 = h + s1 + ch + K[i] + w[i];
		const uint32_t s0 = std::rotr(a, 2) ^ std::rotr(a, 13) ^ std::rotr(a, 22);
		const uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
		const uint32_t t2 = s0 + maj;
		h = g;
		g = f;
		f = e;
		e = d + t1;
		d = c;
		c = b;
		b = a;
		a = t1 + t2;
	}
	h_[0] += a;
	h_[1] += b;
	h_[2] += c;
	h_[3] += d;
	h_[4] += e;
	h_[5] += f;
	h_[6] += g;
	h_[7] += h;
}

void Sha256::update(const void *data, size_t size) {
	const auto *p = static_cast<const uint8_t *>(data);
	bits_ += static_cast<uint64_t>(size) * 8;
	while (size > 0) {
		const size_t take = size < 64 - fill_ ? size : 64 - fill_;
		std::memcpy(buffer_.data() + fill_, p, take);
		fill_ += take;
		p += take;
		size -= take;
		if (fill_ == 64) {
			block(buffer_.data());
			fill_ = 0;
		}
	}
}

void Sha256::update_u64(uint64_t v) {
	uint8_t bytes[8];
	for (int i = 0; i < 8; ++i) {
		bytes[i] = static_cast<uint8_t>(v >> (8 * i));
	}
	update(bytes, 8);
}

void Sha256::update_double(double v) {
	update_u64(std::bit_cast<uint64_t>(v));
}

std::string Sha256::hex() {
	const uint64_t bits = bits_;
	const uint8_t one = 0x80;
	update(&one, 1);
	const uint8_t zero = 0;
	while (fill_ != 56) {
		update(&zero, 1);
	}
	uint8_t length[8];
	for (int i = 0; i < 8; ++i) {
		length[i] = static_cast<uint8_t>(bits >> (56 - 8 * i));
	}
	update(length, 8);
	static const char digits[] = "0123456789abcdef";
	std::string out;
	for (const uint32_t word : h_) {
		for (int shift = 28; shift >= 0; shift -= 4) {
			out += digits[(word >> shift) & 0xf];
		}
	}
	return out;
}

} // namespace lab
