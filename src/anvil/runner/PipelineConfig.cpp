// ANVIL pipeline config: canonical JSON serialization. Field order and value
// formatting are fixed so the config hash is stable across runs and hosts.
// A matching minimal parser lands with the runner implementation; the runner
// may also accept this exact file as YAML-free JSON with the same fields.
#include "anvil/runner/PipelineConfig.hpp"

#include <sstream>

namespace anvil {

namespace {
std::string q(const std::string& s) {
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
}  // namespace

std::string PipelineConfig::canonicalJson() const {
  std::ostringstream out;
  out << "{"
      << "\"inputPath\":" << q(inputPath)
      << ",\"firstFrame\":" << firstFrame
      << ",\"lastFrame\":" << lastFrame
      << ",\"past\":" << past
      << ",\"future\":" << future
      << ",\"sideInfoNormalizer\":" << q(sideInfoNormalizer)
      << ",\"coarse\":" << q(coarse)
      << ",\"refine\":" << q(refine)
      << ",\"visibility\":" << q(visibility)
      << ",\"geometry\":" << q(geometry)
      << ",\"reconstruct\":" << q(reconstruct)
      << ",\"backend\":" << q(backend)
      << ",\"oracleSidecar\":" << q(oracleSidecar)
      << ",\"deterministic\":" << (deterministic ? "true" : "false")
      << ",\"seed\":" << seed
      << ",\"captureDir\":" << q(captureDir)
      << ",\"captureStages\":" << q(captureStages)
      << ",\"outputDir\":" << q(outputDir)
      << "}";
  return out.str();
}

}  // namespace anvil
