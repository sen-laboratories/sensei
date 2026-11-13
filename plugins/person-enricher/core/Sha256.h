/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
 */

#pragma once

#include <array>
#include <cstdint>
#include <iomanip>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace robin {

/**
 * Compact, self-contained implementation of SHA-256 (FIPS 180-4) in C++20.
 * Used for Gravatar and Libravatar lookups (SHA-256 of trimmed, lowercase email).
 */
class Sha256 {
public:
	static std::string Compute(std::string_view input)
	{
		Sha256 ctx;
		ctx.Update(input);
		return ctx.FinalHex();
	}

	Sha256()
	{
		Reset();
	}

	void Reset()
	{
		fState[0] = 0x6a09e667;
		fState[1] = 0xbb67ae85;
		fState[2] = 0x3c6ef372;
		fState[3] = 0xa54ff53a;
		fState[4] = 0x510e527f;
		fState[5] = 0x9b05688c;
		fState[6] = 0x1f83d9ab;
		fState[7] = 0x5be0cd19;
		fCount = 0;
		fBuffer.fill(0);
	}

	void Update(std::string_view data)
	{
		for (char c : data) {
			size_t index = static_cast<size_t>(fCount % 64);
			fBuffer[index] = static_cast<uint8_t>(c);
			fCount++;
			if (index == 63) {
				_Transform(fBuffer.data());
			}
		}
	}

	std::string FinalHex()
	{
		uint64_t totalBits = fCount * 8;
		size_t index = static_cast<size_t>(fCount % 64);

		fBuffer[index++] = 0x80;
		if (index > 56) {
			while (index < 64)
				fBuffer[index++] = 0;
			_Transform(fBuffer.data());
			index = 0;
		}
		while (index < 56)
			fBuffer[index++] = 0;

		// Append bit length in big-endian
		for (int i = 7; i >= 0; i--) {
			fBuffer[56 + (7 - i)] = static_cast<uint8_t>((totalBits >> (i * 8)) & 0xFF);
		}
		_Transform(fBuffer.data());

		std::ostringstream ss;
		for (uint32_t val : fState) {
			ss << std::hex << std::setw(8) << std::setfill('0') << val;
		}
		return ss.str();
	}

private:
	static constexpr uint32_t kK[64] = {
		0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
		0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
		0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
		0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
		0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
		0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
		0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
		0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2
	};

	static inline uint32_t _RoTr(uint32_t x, uint32_t n) { return (x >> n) | (x << (32 - n)); }
	static inline uint32_t _Ch(uint32_t x, uint32_t y, uint32_t z) { return (x & y) ^ (~x & z); }
	static inline uint32_t _Maj(uint32_t x, uint32_t y, uint32_t z) { return (x & y) ^ (x & z) ^ (y & z); }
	static inline uint32_t _Sig0(uint32_t x) { return _RoTr(x, 2) ^ _RoTr(x, 13) ^ _RoTr(x, 22); }
	static inline uint32_t _Sig1(uint32_t x) { return _RoTr(x, 6) ^ _RoTr(x, 11) ^ _RoTr(x, 25); }
	static inline uint32_t _Theta0(uint32_t x) { return _RoTr(x, 7) ^ _RoTr(x, 18) ^ (x >> 3); }
	static inline uint32_t _Theta1(uint32_t x) { return _RoTr(x, 17) ^ _RoTr(x, 19) ^ (x >> 10); }

	void _Transform(const uint8_t* chunk)
	{
		uint32_t w[64];
		for (int i = 0; i < 16; i++) {
			w[i] = (static_cast<uint32_t>(chunk[i * 4]) << 24) |
				   (static_cast<uint32_t>(chunk[i * 4 + 1]) << 16) |
				   (static_cast<uint32_t>(chunk[i * 4 + 2]) << 8) |
				   (static_cast<uint32_t>(chunk[i * 4 + 3]));
		}
		for (int i = 16; i < 64; i++) {
			w[i] = _Theta1(w[i - 2]) + w[i - 7] + _Theta0(w[i - 15]) + w[i - 16];
		}

		uint32_t a = fState[0], b = fState[1], c = fState[2], d = fState[3];
		uint32_t e = fState[4], f = fState[5], g = fState[6], h = fState[7];

		for (int i = 0; i < 64; i++) {
			uint32_t t1 = h + _Sig1(e) + _Ch(e, f, g) + kK[i] + w[i];
			uint32_t t2 = _Sig0(a) + _Maj(a, b, c);
			h = g;
			g = f;
			f = e;
			e = d + t1;
			d = c;
			c = b;
			b = a;
			a = t1 + t2;
		}

		fState[0] += a;
		fState[1] += b;
		fState[2] += c;
		fState[3] += d;
		fState[4] += e;
		fState[5] += f;
		fState[6] += g;
		fState[7] += h;
	}

	std::array<uint32_t, 8> fState;
	uint64_t fCount;
	std::array<uint8_t, 64> fBuffer;
};

} // namespace robin
