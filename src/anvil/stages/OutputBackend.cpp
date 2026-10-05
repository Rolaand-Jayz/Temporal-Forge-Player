// ANVIL stage: output backend (reconciliation 3.5, ask item 7).
// null:        declared no-output — returns a zero-filled image of the
//              reconstruction's dims (contract shape inherited from the
//              historical backend lifecycle; no scaling in v1).
// passthrough: returns the reconstruction image unchanged (working space out;
//              output conversion is a later backend's explicit job).
#include "anvil/stages/Stages.hpp"
#include "anvil/stages/StageInternal.hpp"

#include <memory>

namespace anvil {
namespace {

class NullBackend final : public OutputBackend {
 public:
  std::string name() const override { return "null"; }
  WorkingImage present(const ReconstructionResult& result) override {
    WorkingImage out;
    out.width = result.out.width;
    out.height = result.out.height;
    const size_t npix = static_cast<size_t>(out.width) *
                        static_cast<size_t>(out.height);
    out.r.assign(npix, 0.0f);
    out.g.assign(npix, 0.0f);
    out.b.assign(npix, 0.0f);
    return out;
  }
};

class PassthroughBackend final : public OutputBackend {
 public:
  std::string name() const override { return "passthrough"; }
  WorkingImage present(const ReconstructionResult& result) override {
    return result.out;
  }
};

}  // namespace

std::unique_ptr<OutputBackend> makeNullBackend() {
  return std::make_unique<NullBackend>();
}

std::unique_ptr<OutputBackend> makePassthroughBackend() {
  return std::make_unique<PassthroughBackend>();
}

}  // namespace anvil
