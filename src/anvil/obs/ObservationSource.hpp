// ANVIL observation source: wraps the historical src/media demux/decode shell
// (reused in place per reconciliation 3.2/3.6) into the successor Observation
// model. Qt-free, FSR-free.
#pragma once

#include "anvil/core/Types.hpp"

#include <memory>
#include <string>

namespace anvil {

class ObservationSource {
 public:
  virtual ~ObservationSource() = default;
  // Pull the next decoded observation in display order. false = end of stream
  // or unrecoverable decode failure (check lastError()).
  virtual bool next(Observation& out) = 0;
  // Seek generation: bumped on every flush/seek; frame identity is the pair
  // (fileFrameIndex, seekGeneration).
  virtual uint32_t generation() const = 0;
  virtual const std::string& lastError() const = 0;
};

// File-backed source. requestCodecMv forces software decode with
// AV_CODEC_FLAG2_EXPORT_MVS (hardware paths measurably export no MVs —
// audit dwfrun-015b2ece, codec side-information stream).
class FileObservationSource : public ObservationSource {
 public:
  FileObservationSource(const std::string& path, bool requestCodecMv);
  ~FileObservationSource() override;
  bool next(Observation& out) override;
  uint32_t generation() const override;
  const std::string& lastError() const override;

 private:
  struct Impl;
  Impl* impl_;
};

// Explicit decode->working-space conversion boundary (reconciliation 3.8).
// v1 working space (PROJECT DECISION): display-referred planar float RGB in
// [0,1] at source resolution; matrix from ColorMeta (601/709/2020), range from
// ColorMeta; the recorded transfer is NOT linearized in v1 but is DECLARED
// (recorded in manifests/captures). If color.transfer is unspecified,
// transferOverride must name the assumption explicitly ("bt709" | "srgb" |
// "linear" | "pq" | "hlg"); otherwise this returns false and sets errorOut —
// unknown-transfer content never silently enters reconstruction.
bool convertToWorking(const SourcePlanes& planes, const ColorMeta& color,
                      const std::string& transferOverride, WorkingImage& out,
                      std::string& errorOut, std::string& declaredSpaceOut);

}  // namespace anvil
