// ANVIL pipeline configuration (reconciliation 3.5/3.7).
// Arms are config, not forks. The canonical JSON of this config is hashed
// into the run manifest; the replay key is (binary, config, input, seed,
// determinism, decode mode).
#pragma once

#include <cstdint>
#include <string>

namespace anvil {

struct PipelineConfig {
  // Input selection
  std::string inputPath;
  int64_t firstFrame = 0;          // inclusive, file frame index
  int64_t lastFrame = -1;          // inclusive; -1 = to end

  // Temporal window (capability C)
  int past = 0;
  int future = 0;

  // Stage selections (capability D). Names map to factories in the runner.
  std::string sideInfoNormalizer = "passthrough";  // passthrough | densify_blocks
  std::string coarse = "blocksearch";              // codec | blocksearch | zero | oracle
  std::string refine = "none";                     // none | hillclimb2
  std::string visibility = "photo_fwbw";           // unity | photo_fwbw | oracle
  std::string geometry = "unknown";                // unknown | global_xform | oracle
  std::string reconstruct = "weighted_blend";      // center | weighted_blend
  std::string backend = "passthrough";             // null | passthrough

  // Oracle sidecar (capability H); empty = no oracle anywhere
  std::string oracleSidecar;

  // Determinism / decoding
  bool deterministic = true;       // software decode, fixed threads, single-thread core
  uint64_t seed = 0;               // recorded; v1 stages are deterministic (no RNG)

  // Capture (capability G); empty = no capture
  std::string captureDir;
  std::string captureStages;       // comma-separated stage ids; empty = none

  // Output
  std::string outputDir;           // reconstructed frames written here as .npy

  // Declared transfer assumption (reconciliation 3.8): "" | bt709 | srgb |
  // linear | pq | hlg. Required (non-empty) when the input's transfer
  // metadata is unspecified; recorded in the manifest either way.
  std::string transferOverride;

  // Neighbor ablation: comma-separated integer window offsets to drop from
  // the neighbor arrays before reconstruct (e.g. "-1", "-1,1"; "" = none).
  std::string dropNeighbors;

  std::string canonicalJson() const;
};

// Minimal hand-rolled parser for EXACTLY the canonical config schema:
//   - any whitespace, any field order;
//   - unknown fields -> error; duplicate fields -> error;
//   - missing fields -> PipelineConfig{} defaults;
//   - strict types (string/int/bool; no nested values, no floats where an
//     integer is expected, no exponent notation).
// On success returns true and out holds the parsed config. On failure returns
// false with a precise message in err; out is reset to defaults.
bool parsePipelineConfig(const std::string& json, PipelineConfig& out, std::string& err);

}  // namespace anvil
