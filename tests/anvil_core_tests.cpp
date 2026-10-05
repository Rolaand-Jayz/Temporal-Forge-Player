// ANVIL core sanity tests: .npy round-trip, canonical config JSON stability,
// and window assembly invariants. Registered in tests/CMakeLists.txt.
#include "anvil/capture/Capture.hpp"
#include "anvil/core/Types.hpp"
#include "anvil/core/Window.hpp"
#include "anvil/runner/PipelineConfig.hpp"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>

using namespace anvil;

namespace {

int failures = 0;
#define CHECK(...)                                                     \
  do {                                                                 \
    if (!(__VA_ARGS__)) {                                              \
      std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #__VA_ARGS__); \
      ++failures;                                                      \
    }                                                                  \
  } while (0)

void testNpyRoundTripF32() {
  const std::string path =
      (std::filesystem::temp_directory_path() / "anvil_test_f32.npy").string();
  std::vector<float> data(2 * 3);
  for (size_t i = 0; i < data.size(); ++i) data[i] = static_cast<float>(i) * 0.5f;
  CHECK(writeNpy(path, {2, 3}, DType::F32, data.data()));
  std::vector<int64_t> shape;
  DType dtype = DType::F32;
  std::vector<float> out;
  std::vector<uint8_t> outU8;
  CHECK(readNpy(path, shape, dtype, out, outU8));
  CHECK(shape == std::vector<int64_t>{2, 3});
  CHECK(dtype == DType::F32);
  CHECK(out == data);
  std::filesystem::remove(path);
}

void testNpyRoundTripU8() {
  const std::string path =
      (std::filesystem::temp_directory_path() / "anvil_test_u8.npy").string();
  std::vector<uint8_t> data{1, 2, 3, 4, 5};
  CHECK(writeNpy(path, {5}, DType::U8, data.data()));
  std::vector<int64_t> shape;
  DType dtype = DType::F32;
  std::vector<float> out;
  std::vector<uint8_t> outU8;
  CHECK(readNpy(path, shape, dtype, out, outU8));
  CHECK(shape == std::vector<int64_t>{5});
  CHECK(dtype == DType::U8);
  CHECK(outU8 == data);
  std::filesystem::remove(path);
}

void testCanonicalConfigIsStable() {
  PipelineConfig cfg;
  const std::string a = cfg.canonicalJson();
  const std::string b = cfg.canonicalJson();
  CHECK(a == b);
  CHECK(a.find("\"deterministic\":true") != std::string::npos);
  // Changing a field changes the JSON (hash input sensitivity)
  cfg.past = 2;
  CHECK(cfg.canonicalJson() != a);
}

void testWindowAssemblyInvariants() {
  WindowConfig wc;
  wc.past = 2;
  wc.future = 1;
  FrameWindow w;
  for (int i = 0; i < wc.past + 1 + wc.future; ++i) {
    auto obs = std::make_shared<Observation>();
    obs->id.fileFrameIndex = static_cast<uint64_t>(100 + i);
    obs->rel = i < wc.past   ? Observation::Rel::Past
               : i == wc.past ? Observation::Rel::Center
                              : Observation::Rel::Future;
    obs->windowOffset = i - wc.past;
    w.observations.push_back(obs);
    if (obs->rel == Observation::Rel::Center) w.centerIdx = w.observations.size() - 1;
  }
  CHECK(w.observations.size() == 4);
  CHECK(w.centerIdx == 2);
  CHECK(w.center().id.fileFrameIndex == 102);
  CHECK(w.past().size() == 2);
  CHECK(w.future().size() == 1);
  CHECK(w.future()[0]->windowOffset == 1);
}

void testSideInfoDefaultsAreExplicit() {
  SideInfo side;
  CHECK(side.provenance == SideInfo::Provenance::None);
  CHECK(side.coverage == SideInfo::Coverage::Present);
  CHECK(side.mvs.empty());
  SampleGeometry geo;
  CHECK(geo.status == SampleGeometry::Status::Unknown);  // no invented jitter
}

}  // namespace

int main() {
  testNpyRoundTripF32();
  testNpyRoundTripU8();
  testCanonicalConfigIsStable();
  testWindowAssemblyInvariants();
  testSideInfoDefaultsAreExplicit();
  if (failures != 0) {
    std::fprintf(stderr, "anvil_core_tests: %d failure(s)\n", failures);
    return 1;
  }
  std::printf("anvil_core_tests: all passed\n");
  return 0;
}
