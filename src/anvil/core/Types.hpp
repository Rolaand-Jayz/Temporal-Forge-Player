// ANVIL successor core data model.
// Successor of the closed FSR-era Temporal Forge line; built under the
// maintainer's 2026-10-05 directive (docs/anvil/). See
// docs/anvil/RECONCILIATION_20261005.md for the decisions encoded here.
//
// Guardrails honored by this model:
//   - Codec motion vectors are side information, never truth; provenance and
//     coverage are explicit so "no vectors" is never silently interpreted.
//   - Sample geometry is Known / Estimated / Unknown; nothing invents jitter.
//   - Consequential claims in comments carry an evidence-class label.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace anvil {

// ---------------------------------------------------------------------------
// Small math
// ---------------------------------------------------------------------------

struct Vec2f {
  float x = 0.0f;
  float y = 0.0f;
};

// Row-major 2D affine transform (a b c; d e f; 0 0 1). Used only for declared
// global transforms; never synthesized from motion vectors.
struct Mat3f {
  float a = 1.0f, b = 0.0f, c = 0.0f;
  float d = 0.0f, e = 1.0f, f = 0.0f;
};

// ---------------------------------------------------------------------------
// Planes
// ---------------------------------------------------------------------------

// Decoded source planes, 8-bit planar I420 (the only normalized decode format
// the v1 runner consumes; non-420 sources are normalized by an explicitly
// declared conversion step, never silently — see reconciliation 3.8).
struct SourcePlanes {
  int width = 0;
  int height = 0;
  std::vector<uint8_t> y;            // width*height
  std::vector<uint8_t> u, v;         // (width/2)*(height/2), 4:2:0
};

// Explicit working image: float32 planar RGB in the declared working space.
// The conversion INTO this space is an explicit, recorded boundary (reconciliation
// 3.8); metrics and captures know which space they operate in.
struct WorkingImage {
  int width = 0;
  int height = 0;
  std::vector<float> r, g, b;        // each width*height
};

// ---------------------------------------------------------------------------
// Color metadata (reconciliation 3.8)
// ---------------------------------------------------------------------------

struct ColorMeta {
  enum class Range : int { Unspecified = 0, Limited = 1, Full = 2 };
  int matrixCoefficients = -1;       // AVColorSpace value; -1 = unspecified, preserved
  int primaries = -1;                // AVColorPrimaries value
  int transfer = -1;                 // AVColorTransferCharacteristic value
  Range range = Range::Unspecified;
  int chromaLocation = -1;           // AVChromaLocation value
  int bitDepth = 8;
};

// ---------------------------------------------------------------------------
// Codec side information (reconciliation 3.9; codec audits are MEASURED FACT)
// ---------------------------------------------------------------------------

// One normalized codec motion-vector block. Units: source pixels, convention
// destination->reference ("destination_plus_motion"), matching the historical
// temporal_motion_contract.md sign boundary. No field is fabricated: fields the
// codec API does not expose (reference identity, temporal distance, skip/intra
// per vector) do not exist here.
struct AnvilMv {
  int16_t dstX = 0, dstY = 0;        // block origin, source pixels
  uint16_t w = 0, h = 0;             // block size, source pixels
  float mvX = 0.0f, mvY = 0.0f;      // destination -> reference displacement
  int8_t direction = 0;              // -1 past, +1 future, 0 unknown (API exposes
                                     // direction only; MEASURED on libavcodec 63)
};

struct SideInfo {
  enum class Provenance : int {
    None = 0,      // no side information available at all
    CodecMv = 1,   // decoded from the codec bitstream/decoder export
    External = 2,  // supplied out-of-band (e.g. sidecar), labeled, not codec
    Oracle = 3,    // injected ground truth (test arms)
  };
  enum class Coverage : int {
    // Why vectors are absent/present; "empty" must be disambiguable
    // (all coverage cells MEASURED on this stack, audit run dwfrun-015b2ece).
    Present = 0,
    IntraFrame = 1,        // codec emits no vectors for I frames
    UnsupportedCodec = 2,  // HEVC/AV1 on FFmpeg n9.0.2: no MV export exists
    HwDecodeNoExport = 3,  // VAAPI/Vulkan hw decode: measured no export
  };
  Provenance provenance = Provenance::None;
  Coverage coverage = Coverage::Present;
  std::vector<AnvilMv> mvs;
  bool bFrame = false;  // frame-level marker only; never a reference-identity claim
};

// ---------------------------------------------------------------------------
// Correspondence / visibility / geometry / reconstruction
// ---------------------------------------------------------------------------

// Dense-grid motion field: one vector per gridStep block on the working grid.
struct BlockVec {
  int gx = 0, gy = 0;                // block grid coordinates
  Vec2f mv;                          // block displacement, working pixels
  float quality = 1.0f;              // estimator-declared quality in [0,1]
};

struct MotionField {
  enum class Src : int { Oracle = 0, Codec = 1, Estimated = 2 };
  Src src = Src::Estimated;
  int gridStep = 8;
  int gridWidth = 0, gridHeight = 0; // blocks across/down
  std::vector<BlockVec> blocks;      // size gridWidth*gridHeight
  std::vector<float> fwbwError;      // forward-backward residual per block
};

struct Visibility {
  enum class Src : int { Oracle = 0, Estimated = 1 };
  Src src = Src::Estimated;
  int width = 0, height = 0;
  std::vector<float> weight;         // per-pixel confidence in [0,1]
  std::vector<uint8_t> occluded;     // per-pixel visibility flag (0 = visible)
};

struct SampleGeometry {
  enum class Status : int {
    Unknown = 0,    // default: identity sampling of the source grid; never fake
    Estimated = 1,  // produced by a declared estimator
    Known = 2,      // supplied by oracle/fixture only
  };
  Status status = Status::Unknown;
  Mat3f globalXform;                 // meaningful only for Known/Estimated
};

// Per-neighbor ablation record: which window members contributed.
struct AblationRecord {
  std::vector<int64_t> usedFrameIndices;   // contributions actually blended
  std::vector<int64_t> droppedFrameIndices; // excluded by ablation/config
};

struct ReconstructionResult {
  uint64_t fileFrameIndex = 0;
  int64_t ptsUs = 0;
  WorkingImage out;                  // working space; backend owns output conversion
  SampleGeometry geo;
  AblationRecord ablation;
};

// ---------------------------------------------------------------------------
// Observation (reconciliation 3.4)
// ---------------------------------------------------------------------------

struct FrameIdentity {
  uint64_t fileFrameIndex = 0;       // monotonic within (source, seekGeneration)
  uint32_t seekGeneration = 0;       // bumped on flush/seek; identity is the pair
  int64_t ptsUs = 0;
  int64_t dtsUs = -1;                // -1 = not present (never fabricated)
  bool valid = true;                 // false on decode errors/concealment
};

struct Observation {
  FrameIdentity id;
  SourcePlanes planes;
  ColorMeta color;
  SideInfo side;
  // Relationship to the window target; past/future comes from window assembly,
  // never inferred from motion-vector sign.
  enum class Rel : int { Past = 0, Center = 1, Future = 2 };
  Rel rel = Rel::Center;
  int windowOffset = 0;              // targetIndex - thisIndex (negative = past)
};

// ---------------------------------------------------------------------------
// Run identity (reconciliation 3.7; full manifest lives in runner/)
// ---------------------------------------------------------------------------

struct ReplayKey {
  std::string binarySha256;
  std::string configSha256;
  std::string inputSha256;
  uint64_t seed = 0;
  int determinismMode = 0;           // 0 = exact, 1 = best_effort
  int decodeMode = 0;                // 0 = software (deterministic default)
};

}  // namespace anvil
