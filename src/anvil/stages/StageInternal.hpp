// ANVIL stages: internal maker declarations. The group .cpp files define the
// concrete implementations in anonymous namespaces and expose them to
// StageFactories.cpp through these makers only (keeps the pinned public
// declarations in Stages.hpp/StageFactories.hpp untouched).
#pragma once

#include "anvil/stages/Stages.hpp"

#include <memory>

namespace anvil {

std::unique_ptr<SideInfoNormalizer> makePassthroughNormalizer();
std::unique_ptr<SideInfoNormalizer> makeDensifyBlocksNormalizer();

std::unique_ptr<CoarseCorrespondence> makeZeroCoarse();
std::unique_ptr<CoarseCorrespondence> makeCodecCoarse();
std::unique_ptr<CoarseCorrespondence> makeBlockSearchCoarse();

std::unique_ptr<ResidualRefiner> makeNoneRefiner();
std::unique_ptr<ResidualRefiner> makeHillClimb2Refiner();

std::unique_ptr<VisibilityEstimator> makeUnityVisibility();
std::unique_ptr<VisibilityEstimator> makePhotoFwBwVisibility();

std::unique_ptr<SampleGeometryEstimator> makeUnknownGeometry();
std::unique_ptr<SampleGeometryEstimator> makeGlobalXformGeometry();

std::unique_ptr<TemporalReconstructor> makeCenterReconstructor();
std::unique_ptr<TemporalReconstructor> makeWeightedBlendReconstructor();

std::unique_ptr<OutputBackend> makeNullBackend();
std::unique_ptr<OutputBackend> makePassthroughBackend();

}  // namespace anvil
