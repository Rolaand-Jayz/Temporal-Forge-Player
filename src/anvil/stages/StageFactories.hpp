// ANVIL stage factory declarations (fixed interface; stage implementations
// register by name here). The runner consumes only these factories.
// Name -> implementation map (v1):
//   sideInfoNormalizer: passthrough | densify_blocks
//   coarse:             codec | blocksearch | zero | oracle
//   refine:             none | hillclimb2
//   visibility:         unity | photo_fwbw | oracle
//   geometry:           unknown | global_xform | oracle
//   reconstruct:        center | weighted_blend
//   backend:            null | passthrough
// Oracle-backed implementations are wired by the runner when an oracle
// sidecar is configured (they live in oracle/, selected at config time).
#pragma once

#include "anvil/stages/Stages.hpp"

#include <memory>
#include <string>

namespace anvil {

std::unique_ptr<SideInfoNormalizer> createSideInfoNormalizer(const std::string& name);
std::unique_ptr<CoarseCorrespondence> createCoarse(const std::string& name);
std::unique_ptr<ResidualRefiner> createRefiner(const std::string& name);
std::unique_ptr<VisibilityEstimator> createVisibility(const std::string& name);
std::unique_ptr<SampleGeometryEstimator> createGeometry(const std::string& name);
std::unique_ptr<TemporalReconstructor> createReconstructor(const std::string& name);
std::unique_ptr<OutputBackend> createBackend(const std::string& name);

}  // namespace anvil
