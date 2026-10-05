// ANVIL stage factories (pinned declarations in StageFactories.hpp).
// Convention: an unknown name returns nullptr. Oracle-backed twins
// ("oracle" entries in the name map) are NOT created here — they are wired
// by the runner from oracle/ at config time (StageFactories.hpp header
// comment), so those names also resolve to nullptr from this factory set.
#include "anvil/stages/StageFactories.hpp"
#include "anvil/stages/StageInternal.hpp"

#include <memory>
#include <string>

namespace anvil {

std::unique_ptr<SideInfoNormalizer> createSideInfoNormalizer(const std::string& name) {
  if (name == "passthrough") return makePassthroughNormalizer();
  if (name == "densify_blocks") return makeDensifyBlocksNormalizer();
  return nullptr;
}

std::unique_ptr<CoarseCorrespondence> createCoarse(const std::string& name) {
  if (name == "codec") return makeCodecCoarse();
  if (name == "blocksearch") return makeBlockSearchCoarse();
  if (name == "zero") return makeZeroCoarse();
  return nullptr;
}

std::unique_ptr<ResidualRefiner> createRefiner(const std::string& name) {
  if (name == "none") return makeNoneRefiner();
  if (name == "hillclimb2") return makeHillClimb2Refiner();
  return nullptr;
}

std::unique_ptr<VisibilityEstimator> createVisibility(const std::string& name) {
  if (name == "unity") return makeUnityVisibility();
  if (name == "photo_fwbw") return makePhotoFwBwVisibility();
  return nullptr;
}

std::unique_ptr<SampleGeometryEstimator> createGeometry(const std::string& name) {
  if (name == "unknown") return makeUnknownGeometry();
  if (name == "global_xform") return makeGlobalXformGeometry();
  return nullptr;
}

std::unique_ptr<TemporalReconstructor> createReconstructor(const std::string& name) {
  if (name == "center") return makeCenterReconstructor();
  if (name == "weighted_blend") return makeWeightedBlendReconstructor();
  return nullptr;
}

std::unique_ptr<OutputBackend> createBackend(const std::string& name) {
  if (name == "null") return makeNullBackend();
  if (name == "passthrough") return makePassthroughBackend();
  return nullptr;
}

}  // namespace anvil
