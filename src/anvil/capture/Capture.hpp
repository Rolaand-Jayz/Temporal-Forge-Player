// ANVIL stage capture (reconciliation 3.7): per-(stage, frame) intermediate
// dumps with provenance sidecars. Formats: .npy (numeric exact, hand-rolled
// writer — no new dependencies), .png (8-bit display proxies, only when a
// PNG encoder is already linked), .raw + .json sidecar otherwise.
// Every record names its stage, frame identity, and payload shape/dtype.
#pragma once

#include "anvil/core/Types.hpp"

#include <string>
#include <vector>

namespace anvil {

enum class DType : int { F32 = 0, U8 = 1 };

struct CapturedTensor {
  std::string stageId;         // e.g. "coarse", "reconstruct"
  std::string name;            // e.g. "motion.fwd", "visibility.weight"
  uint64_t fileFrameIndex = 0;
  int64_t ptsUs = 0;
  std::vector<int64_t> shape;  // row-major
  DType dtype = DType::F32;
  std::vector<float> f32;
  std::vector<uint8_t> u8;
};

// Interface so tests can inject an in-memory sink; the disk sink drains a
// bounded ring from a writer thread.
class CaptureSink {
 public:
  virtual ~CaptureSink() = default;
  virtual void capture(const CapturedTensor& tensor) = 0;
  virtual void flush() {}
};

// Writes <dir>/<stageId>/<fileFrameIndex>-<name>.npy plus a per-run
// capture_index.json recording every emitted record.
class DiskCaptureSink : public CaptureSink {
 public:
  DiskCaptureSink(std::string rootDir, size_t ringCapacity);
  ~DiskCaptureSink() override;
  void capture(const CapturedTensor& tensor) override;
  void flush() override;

 private:
  struct Impl;
  Impl* impl_;
};

// Minimal .npy v1.0 writer/reader (float32/uint8, C order, little-endian).
// No external dependencies; tested round-trip in tests/anvil.
bool writeNpy(const std::string& path, const std::vector<int64_t>& shape, DType dtype,
              const void* data);
bool readNpy(const std::string& path, std::vector<int64_t>& shape, DType& dtype,
             std::vector<float>& outF32, std::vector<uint8_t>& outU8);

}  // namespace anvil
