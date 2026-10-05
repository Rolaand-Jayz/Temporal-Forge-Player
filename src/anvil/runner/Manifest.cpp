// ANVIL run manifest: deterministic serialization and replay-key derivation.
#include "anvil/runner/Manifest.hpp"

#include "anvil/core/Sha256.hpp"

#include <ctime>
#include <sstream>

namespace anvil {

std::string escapeJson(const std::string& s) {
  std::ostringstream out;
  out << '"';
  for (char c : s) {
    switch (c) {
      case '"': out << "\\\""; break;
      case '\\': out << "\\\\"; break;
      case '\n': out << "\\n"; break;
      default: out << c;
    }
  }
  out << '"';
  return out.str();
}

std::string computeRunId(const std::string& canonicalConfig,
                         const std::string& inputSha256,
                         const std::string& binarySha256, uint64_t seed,
                         int determinismMode, int decodeMode) {
  // Newline-separated concatenation per the manifest spec; every component
  // is unambiguous because hashes/hex contain no newlines and the trailing
  // three components are one integer each.
  std::string material = canonicalConfig;
  material += '\n';
  material += inputSha256;
  material += '\n';
  material += binarySha256;
  material += '\n';
  material += std::to_string(seed);
  material += '\n';
  material += std::to_string(determinismMode);
  material += '\n';
  material += std::to_string(decodeMode);
  return sha256(material.data(), material.size()).substr(0, 16);
}

std::string iso8601Utc(int64_t unixSeconds) {
  std::tm tm{};
  const time_t t = static_cast<time_t>(unixSeconds);
  gmtime_r(&t, &tm);  // POSIX target; the ANVIL runner is Linux-first
  char buf[32];
  const size_t n = std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &tm);
  return std::string(buf, n);
}

int determinismModeFromConfig(bool deterministic) { return deterministic ? 0 : 1; }

std::string RunManifest::toJson() const {
  std::ostringstream out;
  out << "{"
      << "\"schema\":" << escapeJson(schema)
      << ",\"runId\":" << escapeJson(runId)
      << ",\"createdAt\":" << escapeJson(createdAt)
      << ",\"git\":{\"head\":" << escapeJson(gitHead)
      << ",\"dirty\":" << (gitDirty ? "true" : "false") << "}"
      << ",\"binary\":{\"path\":" << escapeJson(binaryPath)
      << ",\"sha256\":" << escapeJson(binarySha256) << "}"
      << ",\"config\":{\"canonicalJson\":" << escapeJson(configCanonicalJson)
      << ",\"sha256\":" << escapeJson(configSha256) << "}"
      << ",\"input\":{\"path\":" << escapeJson(inputPath)
      << ",\"sha256\":" << escapeJson(inputSha256) << "}"
      << ",\"frameRange\":{\"first\":" << frameFirst
      << ",\"last\":" << frameLast
      << ",\"emitted\":" << framesEmitted
      << ",\"skippedWarmup\":" << skippedWarmup
      << ",\"skippedTrailing\":" << skippedTrailing << "}"
      // Additive field: reset boundaries in fixed occurrence order.
      << ",\"resets\":[";
  for (size_t i = 0; i < resets.size(); ++i) {
    if (i != 0) out << ',';
    out << "{\"frameIndex\":" << resets[i].frameIndex
        << ",\"cause\":" << escapeJson(resets[i].cause) << "}";
  }
  out << "]"
      << ",\"window\":{\"past\":" << past << ",\"future\":" << future << "}"
      << ",\"stages\":{"
      << "\"sideInfoNormalizer\":" << escapeJson(sideInfoNormalizer)
      << ",\"coarse\":" << escapeJson(coarse)
      << ",\"refine\":" << escapeJson(refine)
      << ",\"visibility\":" << escapeJson(visibility)
      << ",\"geometry\":" << escapeJson(geometry)
      << ",\"reconstruct\":" << escapeJson(reconstruct)
      << ",\"backend\":" << escapeJson(backend) << "}"
      << ",\"oracle\":{\"sidecar\":" << escapeJson(oracleSidecar) << ",\"arms\":[";
  for (size_t i = 0; i < oracleArms.size(); ++i) {
    if (i != 0) out << ',';
    out << escapeJson(oracleArms[i]);
  }
  out << "]}"
      << ",\"deterministic\":" << (deterministic ? "true" : "false")
      << ",\"seed\":" << seed
      << ",\"decodeMode\":" << escapeJson(decodeMode)
      << ",\"transferOverride\":" << escapeJson(transferOverride)
      << ",\"dropNeighbors\":" << escapeJson(dropNeighbors)
      << ",\"outputs\":[";
  for (size_t i = 0; i < outputs.size(); ++i) {
    if (i != 0) out << ',';
    out << "{\"file\":" << escapeJson(outputs[i].file)
        << ",\"sha256\":" << escapeJson(outputs[i].sha256) << "}";
  }
  out << "]"
      << ",\"timingsFile\":" << escapeJson(timingsFile)
      << "}";
  return out.str();
}

}  // namespace anvil
