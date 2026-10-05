// ANVIL SHA-256 (FIPS 180-4), hand-rolled: no third-party dependencies.
// Used for the run manifest replay key and output/input hashing.
// Deterministic and allocation-free in the streaming path (fixed internal
// block buffer; caller supplies the read buffer for sha256File).
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace anvil {

// Streaming SHA-256. Feed update() any number of times, then hexDigest().
class Sha256 {
 public:
  Sha256();                 // initializes to the FIPS 180-4 initial state
  void update(const void* data, size_t len);
  // Finalizes (pads) and returns the 64-character lowercase hex digest.
  // The object is left finalized; call reset() to reuse it.
  std::string hexDigest();
  void reset();

 private:
  void compressBlock(const uint8_t block[64]);

  uint32_t h_[8];
  uint8_t block_[64];       // pending partial block
  size_t blockLen_ = 0;     // bytes pending in block_ (< 64)
  uint64_t totalLen_ = 0;   // total bytes fed (for the 128-bit length pad)
  bool finalized_ = false;
};

// One-shot: 64-char lowercase hex of the SHA-256 over data[0..len).
std::string sha256(const void* data, size_t len);

// Streaming file hash. Reads in fixed-size chunks into a caller-shared
// reusable buffer (no per-chunk allocation). Returns false and sets err on
// I/O failure; out is untouched on failure.
bool sha256File(const std::string& path, std::string& out, std::string& err);

}  // namespace anvil
