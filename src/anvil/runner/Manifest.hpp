// ANVIL run manifest: pure manifest/runId helpers (reconciliation 3.7).
// Split from main.cpp so unit tests can pin the replay-key derivation and
// the manifest serialization. I/O (git probing, /proc/self/exe, file
// hashing) stays in the runner main; everything here is deterministic.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace anvil {

// JSON string escaping used by every ANVIL writer (canonical config and
// manifest): only '"', '\\', and '\n' are escaped; other bytes pass through.
std::string escapeJson(const std::string& s);

// Replay key digest (reconciliation 3.7): first 16 hex characters of
//   sha256(canonicalConfig + '\n' + inputSha256 + '\n' + binarySha256 + '\n'
//          + seed + '\n' + determinismMode + '\n' + decodeMode)
// with seed in decimal, determinismMode/decodeMode as integers.
std::string computeRunId(const std::string& canonicalConfig,
                         const std::string& inputSha256,
                         const std::string& binarySha256, uint64_t seed,
                         int determinismMode, int decodeMode);

// "YYYY-MM-DDTHH:MM:SSZ" UTC.
std::string iso8601Utc(int64_t unixSeconds);

// determinismMode: 0 = exact, 1 = best_effort (ReplayKey convention).
int determinismModeFromConfig(bool deterministic);

struct ManifestOutputRecord {
  std::string file;     // file name relative to the output dir
  std::string sha256;   // lowercase hex of the file bytes
};

// One reset boundary observed during window assembly (WindowAssembler).
// Additive anvil.run_manifest.v1 field; a clean run carries an empty array.
struct ManifestResetRecord {
  int64_t frameIndex = -1;  // fileFrameIndex of the observation that broke the run
  std::string cause;        // "invalid" | "discontinuity" | "seek" | "dimensions"
};

struct RunManifest {
  std::string schema = "anvil.run_manifest.v1";
  std::string runId;
  std::string createdAt;          // ISO8601 UTC
  // git
  std::string gitHead;            // "" when not a git worktree (recorded, not hidden)
  bool gitDirty = false;
  // binary
  std::string binaryPath;
  std::string binarySha256;
  // config
  std::string configCanonicalJson;
  std::string configSha256;
  // input
  std::string inputPath;
  std::string inputSha256;
  // frame accounting
  int64_t frameFirst = -1;        // first emitted center's fileFrameIndex; -1 = none
  int64_t frameLast = -1;
  int64_t framesEmitted = 0;
  int64_t skippedWarmup = 0;      // windows lacking past neighbors
  int64_t skippedTrailing = 0;    // windows lacking future neighbors
  std::vector<ManifestResetRecord> resets;  // ADDITIVE v1 field, fixed occurrence order
  // window
  int past = 0;
  int future = 0;
  // stage selections (implementation names as run)
  std::string sideInfoNormalizer;
  std::string coarse;
  std::string refine;
  std::string visibility;
  std::string geometry;
  std::string reconstruct;
  std::string backend;
  // oracle
  std::string oracleSidecar;
  std::vector<std::string> oracleArms;  // stages consuming oracle, fixed order
  // determinism
  bool deterministic = true;
  uint64_t seed = 0;
  std::string decodeMode = "software";  // v1: software always (deterministic default)
  // color/ablation declarations
  std::string transferOverride;
  std::string dropNeighbors;
  // outputs
  std::vector<ManifestOutputRecord> outputs;
  std::string timingsFile;        // "timings.json", relative to output dir

  // Serializes with a FIXED field order (manifest bytes are compared
  // byte-wise against a re-run's manifest in exact-replay audits).
  std::string toJson() const;
};

}  // namespace anvil
