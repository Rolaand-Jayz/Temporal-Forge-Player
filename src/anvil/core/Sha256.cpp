// ANVIL SHA-256 implementation, written from FIPS 180-4 (sections 4.1.2,
// 4.2.2, 6.2.2). No third-party code or dependencies.
#include "anvil/core/Sha256.hpp"

#include <cstdio>
#include <vector>

namespace anvil {

namespace {
constexpr uint32_t kK[64] = {
    0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u, 0x3956c25bu,
    0x59f111f1u, 0x923f82a4u, 0xab1c5ed5u, 0xd807aa98u, 0x12835b01u,
    0x243185beu, 0x550c7dc3u, 0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u,
    0xc19bf174u, 0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu,
    0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau, 0x983e5152u,
    0xa831c66du, 0xb00327c8u, 0xbf597fc7u, 0xc6e00bf3u, 0xd5a79147u,
    0x06ca6351u, 0x14292967u, 0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu,
    0x53380d13u, 0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u,
    0xa2bfe8a1u, 0xa81a664bu, 0xc24b8b70u, 0xc76c51a3u, 0xd192e819u,
    0xd6990624u, 0xf40e3585u, 0x106aa070u, 0x19a4c116u, 0x1e376c08u,
    0x2748774cu, 0x34b0bcb5u, 0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu,
    0x682e6ff3u, 0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u,
    0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u};

inline uint32_t rotr(uint32_t x, unsigned n) { return (x >> n) | (x << (32 - n)); }

inline uint32_t loadBe32(const uint8_t* p) {
  return (static_cast<uint32_t>(p[0]) << 24) | (static_cast<uint32_t>(p[1]) << 16) |
         (static_cast<uint32_t>(p[2]) << 8) | static_cast<uint32_t>(p[3]);
}
}  // namespace

Sha256::Sha256() { reset(); }

void Sha256::reset() {
  // FIPS 180-4 5.3.3 initial hash value
  h_[0] = 0x6a09e667u; h_[1] = 0xbb67ae85u; h_[2] = 0x3c6ef372u; h_[3] = 0xa54ff53au;
  h_[4] = 0x510e527fu; h_[5] = 0x9b05688cu; h_[6] = 0x1f83d9abu; h_[7] = 0x5be0cd19u;
  blockLen_ = 0;
  totalLen_ = 0;
  finalized_ = false;
}

void Sha256::compressBlock(const uint8_t block[64]) {
  uint32_t w[64];
  for (int t = 0; t < 16; ++t) w[t] = loadBe32(block + 4 * t);
  // FIPS 180-4 6.2.2 message schedule
  for (int t = 16; t < 64; ++t) {
    const uint32_t s0 = rotr(w[t - 15], 7) ^ rotr(w[t - 15], 18) ^ (w[t - 15] >> 3);
    const uint32_t s1 = rotr(w[t - 2], 17) ^ rotr(w[t - 2], 19) ^ (w[t - 2] >> 10);
    w[t] = w[t - 16] + s0 + w[t - 7] + s1;
  }
  uint32_t a = h_[0], b = h_[1], c = h_[2], d = h_[3];
  uint32_t e = h_[4], f = h_[5], g = h_[6], hh = h_[7];
  for (int t = 0; t < 64; ++t) {
    const uint32_t S1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
    const uint32_t ch = (e & f) ^ (~e & g);
    const uint32_t temp1 = hh + S1 + ch + kK[t] + w[t];
    const uint32_t S0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
    const uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
    const uint32_t temp2 = S0 + maj;
    hh = g; g = f; f = e; e = d + temp1;
    d = c; c = b; b = a; a = temp1 + temp2;
  }
  h_[0] += a; h_[1] += b; h_[2] += c; h_[3] += d;
  h_[4] += e; h_[5] += f; h_[6] += g; h_[7] += hh;
}

void Sha256::update(const void* data, size_t len) {
  if (finalized_) return;  // hexDigest() already consumed the state
  const auto* p = static_cast<const uint8_t*>(data);
  totalLen_ += len;
  // Fill the pending block first, then consume full blocks directly from the
  // input (no copying when aligned on the 64-byte boundary).
  if (blockLen_ > 0) {
    const size_t need = 64 - blockLen_;
    const size_t take = len < need ? len : need;
    for (size_t i = 0; i < take; ++i) block_[blockLen_ + i] = p[i];
    blockLen_ += take;
    p += take;
    len -= take;
    if (blockLen_ == 64) {
      compressBlock(block_);
      blockLen_ = 0;
    }
  }
  while (len >= 64) {
    compressBlock(p);
    p += 64;
    len -= 64;
  }
  for (size_t i = 0; i < len; ++i) block_[blockLen_ + i] = p[i];
  blockLen_ += len;
}

std::string Sha256::hexDigest() {
  if (!finalized_) {
    // FIPS 180-4 5.1.1 padding: 0x80, zeros, 128-bit big-endian bit length.
    const uint64_t bitLen = totalLen_ * 8;
    uint8_t pad = 0x80;
    update(&pad, 1);
    // totalLen_ was incremented by update(); recompute padding against the
    // post-0x80 length is unnecessary: pad zeros so blockLen_ == 56.
    const uint8_t zero = 0;
    while (blockLen_ != 56) update(&zero, 1);
    uint8_t lenBytes[8];
    for (int i = 0; i < 8; ++i) lenBytes[i] = static_cast<uint8_t>(bitLen >> (56 - 8 * i));
    // Append length directly (update would bump totalLen_, which no longer
    // matters after finalized_ is set).
    for (int i = 0; i < 8; ++i) block_[56 + i] = lenBytes[i];
    compressBlock(block_);
    blockLen_ = 0;
    finalized_ = true;
  }
  static const char* kHex = "0123456789abcdef";
  std::string out;
  out.reserve(64);
  for (int i = 0; i < 8; ++i) {
    for (int shift = 28; shift >= 0; shift -= 4) {
      out.push_back(kHex[(h_[i] >> shift) & 0xfu]);
    }
  }
  return out;
}

std::string sha256(const void* data, size_t len) {
  Sha256 h;
  h.update(data, len);
  return h.hexDigest();
}

bool sha256File(const std::string& path, std::string& out, std::string& err) {
  // "rb" for exact byte semantics regardless of host locale/mode.
  std::FILE* f = std::fopen(path.c_str(), "rb");
  if (!f) {
    err = "cannot open " + path;
    return false;
  }
  Sha256 h;
  // Fixed 256 KiB chunk buffer: one allocation for the whole stream.
  std::vector<uint8_t> buf(256 * 1024);
  size_t n;
  while ((n = std::fread(buf.data(), 1, buf.size(), f)) > 0) h.update(buf.data(), n);
  const bool ioErr = std::ferror(f) != 0;
  std::fclose(f);
  if (ioErr) {
    err = "read error on " + path;
    return false;
  }
  out = h.hexDigest();
  return true;
}

}  // namespace anvil
