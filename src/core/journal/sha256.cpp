// Bundled SHA-256 (FIPS 180-4) implementation. Pure computation: no I/O, no exceptions.
#include "core/journal/sha256.h"

#include <array>
#include <cstdint>
#include <cstring>

namespace core::sha256 {
namespace {

// FIPS 180-4 §4.2.2: K = first 32 bits of the fractional parts of the cube roots of the first 64 primes.
constexpr std::array<std::uint32_t, 64> kK = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1,
    0x923f82a4, 0xab1c5ed5, 0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3,
    0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786,
    0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147,
    0x06ca6351, 0x14292967, 0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13,
    0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85, 0xa2bfe8a1, 0xa81a664b,
    0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a,
    0x5b9cca4f, 0x682e6ff3, 0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
    0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};

// FIPS 180-4 §5.3.3: initial hash values = first 32 bits of the fractional parts of the square roots of the first 8 primes.
constexpr std::array<std::uint32_t, 8> kInit = {
    0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
    0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};

inline std::uint32_t rotr(std::uint32_t x, unsigned n) {
  return (x >> n) | (x << (32 - n));
}

}  // namespace

std::string hex(std::string_view data) {
  const std::size_t len = data.size();
  // Message padding: 0x80, then zeros, then an 8-byte big-endian bit length; total a multiple of 64 bytes.
  const std::uint64_t bit_len = static_cast<std::uint64_t>(len) * 8u;
  std::size_t padded = len + 1;  // 0x80
  while (padded % 64 != 56) ++padded;
  padded += 8;  // bit-length field

  std::array<std::uint32_t, 8> h = kInit;

  // Compress one 64-byte block at a time.
  for (std::size_t block = 0; block < padded; block += 64) {
    std::array<std::uint8_t, 64> chunk{};
    for (std::size_t i = 0; i < 64; ++i) {
      const std::size_t pos = block + i;
      if (pos < len) {
        chunk[i] = static_cast<std::uint8_t>(data[pos]);
      } else if (pos == len) {
        chunk[i] = 0x80;
      } else if (pos >= padded - 8) {
        // Last 8 bytes: big-endian bit length.
        const unsigned shift = static_cast<unsigned>((padded - 1 - pos) * 8);
        chunk[i] = static_cast<std::uint8_t>((bit_len >> shift) & 0xFF);
      } else {
        chunk[i] = 0x00;
      }
    }

    std::array<std::uint32_t, 64> w{};
    for (std::size_t i = 0; i < 16; ++i) {
      w[i] = (static_cast<std::uint32_t>(chunk[i * 4]) << 24) |
             (static_cast<std::uint32_t>(chunk[i * 4 + 1]) << 16) |
             (static_cast<std::uint32_t>(chunk[i * 4 + 2]) << 8) |
             static_cast<std::uint32_t>(chunk[i * 4 + 3]);
    }
    for (std::size_t i = 16; i < 64; ++i) {
      const std::uint32_t s0 =
          rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
      const std::uint32_t s1 =
          rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
      w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }

    std::uint32_t a = h[0], b = h[1], c = h[2], d = h[3];
    std::uint32_t e = h[4], f = h[5], g = h[6], hh = h[7];
    for (std::size_t i = 0; i < 64; ++i) {
      const std::uint32_t s1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
      const std::uint32_t ch = (e & f) ^ (~e & g);
      const std::uint32_t temp1 = hh + s1 + ch + kK[i] + w[i];
      const std::uint32_t s0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
      const std::uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
      const std::uint32_t temp2 = s0 + maj;
      hh = g;
      g = f;
      f = e;
      e = d + temp1;
      d = c;
      c = b;
      b = a;
      a = temp1 + temp2;
    }
    h[0] += a;
    h[1] += b;
    h[2] += c;
    h[3] += d;
    h[4] += e;
    h[5] += f;
    h[6] += g;
    h[7] += hh;
  }

  static const char kHexDigits[] = "0123456789abcdef";
  std::string out;
  out.reserve(64);
  for (std::uint32_t word : h) {
    for (int shift = 28; shift >= 0; shift -= 4) {
      out.push_back(kHexDigits[(word >> shift) & 0xF]);
    }
  }
  return out;
}

}  // namespace core::sha256
