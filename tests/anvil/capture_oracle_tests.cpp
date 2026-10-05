// ANVIL capture/oracle module tests (owned by the capture+oracle worker).
//
// Covers: DiskCaptureSink bounded ring + writer thread (backpressure, no
// loss), .npy byte round-trips, SidecarOracleSource happy/failure paths, and
// the Oracle* stage twins over a stub FrameWindow.
#include "anvil/capture/Capture.hpp"
#include "anvil/oracle/OracleStages.hpp"
#include "anvil/oracle/SidecarOracle.hpp"

#include <atomic>
#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <system_error>
#include <thread>
#include <vector>
#include <unistd.h>  // getpid

namespace fs = std::filesystem;

using namespace anvil;
// SidecarOracle.hpp carries a stray global-scope forward declaration of the
// class name (outside namespace anvil), so an unqualified use is ambiguous
// under `using namespace anvil`. The header is pinned; qualify instead.
using SidecarSource = anvil::SidecarOracleSource;

namespace {

int g_failures = 0;

#define CHECK(cond)                                                           \
  do {                                                                        \
    if (!(cond)) {                                                            \
      ++g_failures;                                                           \
      std::cerr << "FAIL " << __FILE__ << ":" << __LINE__ << "  " #cond       \
                << "\n";                                                      \
    }                                                                         \
  } while (0)

// Unique per-process temp dir (pid-based; no RNG).
fs::path makeTempDir(const std::string& tag) {
  static std::atomic<int> counter{0};
  fs::path base = fs::temp_directory_path() /
                  ("anvil_capture_oracle_tests-" + std::to_string(::getpid()));
  fs::path dir = base / (tag + "-" + std::to_string(counter.fetch_add(1)));
  fs::create_directories(dir);
  return dir;
}

CapturedTensor makeTensor(uint64_t index, float value) {
  CapturedTensor t;
  t.stageId = "coarse";
  t.name = "motion.fwd";
  t.fileFrameIndex = index;
  t.shape = {4};
  t.dtype = DType::F32;
  t.f32 = {value, value, value, value};
  return t;
}

// ---------------------------------------------------------------------------
// Sidecar builder: hand-written JSON + writeNpy payloads
// ---------------------------------------------------------------------------

struct SidecarSpec {
  fs::path dir;
  std::string json;  // oracle_index.json contents
};

// Writes payload .npy files and returns the sidecar dir. `files` maps
// relative payload paths to (shape, values).
SidecarSpec buildSidecar(const std::string& tag,
                         const std::vector<std::pair<std::string, std::string>>& jsonFiles) {
  SidecarSpec spec;
  spec.dir = makeTempDir(tag);
  for (const auto& kv : jsonFiles) {
    const fs::path p = spec.dir / kv.first;
    fs::create_directories(p.parent_path());
    std::ofstream out(p, std::ios::binary);
    out << kv.second;
  }
  return spec;
}

std::string f32Npy(const std::vector<int64_t>& shape, const std::vector<float>& vals) {
  const fs::path p = makeTempDir("npytmp") / "x.npy";
  CHECK(writeNpy(p.string(), shape, DType::F32, vals.data()));
  std::ifstream in(p, std::ios::binary);
  std::string s((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  return s;
}

// A canonical happy sidecar with one full record set for target=5.
SidecarSpec buildHappySidecar(const std::string& tag) {
  const std::string flow = f32Npy({96}, std::vector<float>(96, 0.0f));
  // Non-uniform visibility so the loader's copy is observable.
  std::vector<float> vis(16);
  for (size_t i = 0; i < vis.size(); ++i) vis[i] = (i % 3 == 0) ? 0.0f : 0.75f;
  const std::string visNpy = f32Npy({4, 4}, vis);
  const std::string geo = f32Npy({6}, {0.5f, 1.5f, 2.5f, 3.5f, 4.5f, 5.5f});
  const std::string gt =
      f32Npy({3, 2, 2}, {10.f, 20.f, 30.f, 40.f, 50.f, 60.f, 70.f, 80.f, 90.f, 100.f, 110.f, 120.f});
  std::string json = R"({
  "schema": "anvil.oracle.v1",
  "records": [
    {"kind": "flow", "targetIndex": 5, "windowOffset": -1,
     "file": "flow/f.npy", "gridStep": 8, "gridWidth": 8, "gridHeight": 6},
    {"kind": "visibility", "targetIndex": 5, "windowOffset": -1,
     "file": "vis/v.npy"},
    {"kind": "geometry", "targetIndex": 5, "windowOffset": 0,
     "file": "geo/g.npy"},
    {"kind": "ground_truth", "targetIndex": 5, "windowOffset": 0,
     "file": "gt/gt.npy",
     "meta": {"matrix": 0, "primaries": 1, "transfer": 13, "range": 2,
              "bitDepth": 8}}
  ]
})";
  return buildSidecar(tag, {{"flow/f.npy", flow},
                            {"vis/v.npy", visNpy},
                            {"geo/g.npy", geo},
                            {"gt/gt.npy", gt},
                            {"oracle_index.json", json}});
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

void testSinkBackpressureNoLoss() {
  const fs::path dir = makeTempDir("sink");
  {
    DiskCaptureSink sink(dir.string(), 16);
    for (int i = 0; i < 200; ++i) {
      sink.capture(makeTensor(static_cast<uint64_t>(i), static_cast<float>(i)));
    }
    sink.flush();
  }  // destructor flushes again; must be safe
  int good = 0;
  for (int i = 0; i < 200; ++i) {
    const std::string path = (dir / "coarse" / (std::to_string(i) + "-motion.fwd.f32.npy")).string();
    std::vector<int64_t> shape;
    DType dtype = DType::F32;
    std::vector<float> data;
    std::vector<uint8_t> u8;
    if (!readNpy(path, shape, dtype, data, u8)) continue;
    if (shape.size() == 1 && shape[0] == 4 && data.size() == 4 && data[0] == static_cast<float>(i)) {
      ++good;
    } else {
      std::cerr << "sink: wrong content in " << path << "\n";
    }
  }
  CHECK(good == 200);

  // capture_index.json lists every record (writer thread ordering = capture
  // order, strict FIFO).
  std::ifstream in(dir / "capture_index.json");
  CHECK(in.good());
  std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  int lines = 0;
  size_t pos = 0;
  while ((pos = text.find(".f32.npy", pos)) != std::string::npos) {
    ++lines;
    ++pos;
  }
  CHECK(lines == 200);
  CHECK(text.find("capture_index") == std::string::npos);  // sanity: JSON is the index itself
}

void testSinkU8AndIndex() {
  const fs::path dir = makeTempDir("sinku8");
  DiskCaptureSink sink(dir.string(), 4);
  CapturedTensor t;
  t.stageId = "vis";
  t.name = "weight";
  t.fileFrameIndex = 7;
  t.shape = {2, 3};
  t.dtype = DType::U8;
  t.u8 = {1, 2, 3, 4, 5, 6};
  sink.capture(t);
  sink.flush();
  std::vector<int64_t> shape;
  DType dtype = DType::F32;
  std::vector<float> f;
  std::vector<uint8_t> u;
  CHECK(readNpy((dir / "vis" / "7-weight.u8.npy").string(), shape, dtype, f, u));
  CHECK(dtype == DType::U8 && shape == std::vector<int64_t>({2, 3}) && u.size() == 6);
}

void testNpyRoundTrip() {
  const fs::path dir = makeTempDir("roundtrip");
  // f32 2x3
  const std::vector<float> vals = {0.f, -1.5f, 3.25e8f, 1e-30f, 7.f, -0.0f};
  const fs::path p = dir / "a.npy";
  CHECK(writeNpy(p.string(), {2, 3}, DType::F32, vals.data()));
  std::vector<int64_t> shape;
  DType dtype = DType::F32;
  std::vector<float> f;
  std::vector<uint8_t> u;
  CHECK(readNpy(p.string(), shape, dtype, f, u));
  CHECK(shape == std::vector<int64_t>({2, 3}));
  CHECK(dtype == DType::F32);
  CHECK(f == vals);  // bitwise: vector<float> == compares exact floats

  // u8
  const std::vector<uint8_t> bytes = {0, 1, 255, 128};
  const fs::path q = dir / "b.npy";
  CHECK(writeNpy(q.string(), {4}, DType::U8, bytes.data()));
  CHECK(readNpy(q.string(), shape, dtype, f, u));
  CHECK(dtype == DType::U8 && u == bytes);

  // numpy cross-read (MEASURED on this host if numpy present): headers the
  // writer emits must be loadable by numpy.load.
  std::FILE* pp = ::popen("python3 -c \"import numpy;print('numpy-ok')\" 2>/dev/null", "r");
  if (pp) {
    char buf[16] = {};
    const bool hasNumpy = std::fgets(buf, sizeof buf, pp) != nullptr;
    ::pclose(pp);
    if (hasNumpy) {
      const std::string cmd =
          "python3 -c \"import numpy as np;a=np.load('" + p.string() +
          "');b=np.load('" + q.string() +
          "');import sys;sys.exit(0 if a.shape==(2,3) and a[0,1]==-1.5 and a[1,1]==7.0 and b.tolist()==[0,1,255,128] else 1)\"";
      CHECK(std::system(cmd.c_str()) == 0);
    }
  }
}

void testSidecarHappy() {
  SidecarSpec spec = buildHappySidecar("happy");
  SidecarSource src;
  CHECK(src.open((spec.dir / "oracle_index.json").string()));

  // flow: target 5, offset -1 -> 8x6 grid of zero motion, Oracle provenance
  MotionField flow;
  CHECK(src.loadFlow(5, -1, flow));
  CHECK(flow.src == MotionField::Src::Oracle);
  CHECK(flow.gridStep == 8 && flow.gridWidth == 8 && flow.gridHeight == 6);
  CHECK(flow.blocks.size() == 48);
  CHECK(flow.blocks[0].gx == 0 && flow.blocks[0].gy == 0);
  CHECK(flow.blocks[47].gx == 7 && flow.blocks[47].gy == 5);

  // visibility
  Visibility vis;
  CHECK(src.loadVisibility(5, -1, vis));
  CHECK(vis.src == Visibility::Src::Oracle);
  CHECK(vis.width == 4 && vis.height == 4 && vis.weight.size() == 16);
  CHECK(vis.occluded[0] == 1 && vis.occluded[1] == 0);
  CHECK(std::fabs(vis.weight[1] - 0.75f) < 1e-6f);

  // geometry
  SampleGeometry geo;
  CHECK(src.loadGeometry(5, geo));
  CHECK(geo.status == SampleGeometry::Status::Known);
  CHECK(geo.globalXform.a == 0.5f && geo.globalXform.e == 4.5f && geo.globalXform.f == 5.5f);

  // ground truth + meta
  SourcePlanes planes;
  ColorMeta meta;
  CHECK(src.loadGroundTruth(5, planes, meta));
  CHECK(planes.width == 2 && planes.height == 2);
  CHECK(planes.y.size() == 4 && planes.u.size() == 4 && planes.v.size() == 4);
  CHECK(planes.y[0] == 10 && planes.u[0] == 50 && planes.v[0] == 90);
  CHECK(meta.matrixCoefficients == 0 && meta.transfer == 13 &&
        meta.range == ColorMeta::Range::Full && meta.bitDepth == 8);
}

void testSidecarFailures() {
  SidecarSpec spec = buildHappySidecar("fail");

  // open: missing index file
  {
    SidecarSource src;
    CHECK(!src.open((spec.dir / "nope" / "oracle_index.json").string()));
    CHECK(!src.lastError().empty());
  }
  // open: malformed json
  {
    SidecarSpec bad = buildSidecar("badjson", {{"oracle_index.json", "{not json"}});
    SidecarSource src;
    CHECK(!src.open((bad.dir / "oracle_index.json").string()));
    CHECK(!src.lastError().empty());
  }
  // open: wrong schema
  {
    SidecarSpec bad =
        buildSidecar("badschema", {{"oracle_index.json", "{\"schema\": \"other.v9\", \"records\": []}"}});
    SidecarSource src;
    CHECK(!src.open((bad.dir / "oracle_index.json").string()));
    CHECK(!src.lastError().empty());
  }

  // loaders: happy index, then probe failure modes
  {
    SidecarSource src;
    CHECK(src.open((spec.dir / "oracle_index.json").string()));

    // wrong-shape flow payload: record says 8x6x2, payload has 95 values
    {
      SidecarSpec wrong = buildSidecar(
          "wrongshape",
          {{"flow/f.npy", f32Npy({95}, std::vector<float>(95, 0.f))},
           {"oracle_index.json",
            R"({"schema":"anvil.oracle.v1","records":[{"kind":"flow","targetIndex":5,"windowOffset":-1,"file":"flow/f.npy","gridStep":8,"gridWidth":8,"gridHeight":6}]})"}});
      SidecarSource w;
      CHECK(w.open((wrong.dir / "oracle_index.json").string()));
      MotionField flow;
      CHECK(!w.loadFlow(5, -1, flow));
      CHECK(!w.lastError().empty());
    }
    // missing payload file (record exists, file does not)
    {
      SidecarSpec missing = buildSidecar(
          "missingfile",
          {{"oracle_index.json",
            R"({"schema":"anvil.oracle.v1","records":[{"kind":"visibility","targetIndex":5,"windowOffset":-1,"file":"vis/absent.npy"}]})"}});
      SidecarSource m;
      CHECK(m.open((missing.dir / "oracle_index.json").string()));
      Visibility vis;
      CHECK(!m.loadVisibility(5, -1, vis));
      CHECK(!m.lastError().empty());
    }
    // missing record key
    {
      MotionField flow;
      CHECK(!src.loadFlow(99, -1, flow));
      CHECK(!src.lastError().empty());
      Visibility vis;
      CHECK(!src.loadVisibility(5, +1, vis));  // wrong offset
      CHECK(!src.lastError().empty());
      SampleGeometry geo;
      CHECK(!src.loadGeometry(42, geo));
      CHECK(!src.lastError().empty());
      SourcePlanes planes;
      ColorMeta meta;
      CHECK(!src.loadGroundTruth(42, planes, meta));
      CHECK(!src.lastError().empty());
    }
    // wrong dtype payload (u8 where f32 required)
    {
      const std::string u8 = [] {
        const fs::path p = makeTempDir("u8tmp") / "x.npy";
        const std::vector<uint8_t> b(96, 3);
        CHECK(writeNpy(p.string(), {96}, DType::U8, b.data()));
        std::ifstream in(p, std::ios::binary);
        return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
      }();
      SidecarSpec wrongDtype = buildSidecar(
          "wrongdtype",
          {{"flow/f.npy", u8},
           {"oracle_index.json",
            R"({"schema":"anvil.oracle.v1","records":[{"kind":"flow","targetIndex":5,"windowOffset":-1,"file":"flow/f.npy","gridStep":8,"gridWidth":8,"gridHeight":6}]})"}});
      SidecarSource w;
      CHECK(w.open((wrongDtype.dir / "oracle_index.json").string()));
      MotionField flow;
      CHECK(!w.loadFlow(5, -1, flow));
      CHECK(!w.lastError().empty());
    }
  }
}

void testOracleStages() {
  SidecarSpec spec = buildHappySidecar("stages");
  auto source = std::make_shared<SidecarSource>();
  CHECK(source->open((spec.dir / "oracle_index.json").string()));

  // Stub FrameWindow: past (offset -1) + center (offset 0), target index 5.
  auto past = std::make_shared<Observation>();
  past->id.fileFrameIndex = 4;
  past->windowOffset = -1;
  past->rel = Observation::Rel::Past;
  auto center = std::make_shared<Observation>();
  center->id.fileFrameIndex = 5;
  center->windowOffset = 0;
  center->rel = Observation::Rel::Center;
  FrameWindow window;
  window.observations = {past, center};
  window.centerIdx = 1;

  OracleCoarse coarse(source);
  CHECK(coarse.name() == "oracle");
  std::vector<MotionField> motion = coarse.run(window, NormalizedSideInfo{});
  CHECK(motion.size() == 1);  // one neighbor (past), past-then-future alignment
  CHECK(motion[0].src == MotionField::Src::Oracle);
  CHECK(motion[0].gridWidth == 8 && motion[0].gridHeight == 6);
  CHECK(motion[0].blocks.size() == 48);
  CHECK(motion[0].blocks[20].gx == 4 && motion[0].blocks[20].gy == 2);

  OracleVisibilityEstimator visStage(source);
  CHECK(visStage.name() == "oracle");
  std::vector<Visibility> vis = visStage.run(window, motion);
  CHECK(vis.size() == 1);
  CHECK(vis[0].src == Visibility::Src::Oracle);
  CHECK(vis[0].width == 4 && vis[0].weight.size() == 16);

  OracleGeometryEstimator geoStage(source);
  CHECK(geoStage.name() == "oracle");
  SampleGeometry geo = geoStage.run(window, motion);
  CHECK(geo.status == SampleGeometry::Status::Known);
  CHECK(geo.globalXform.a == 0.5f && geo.globalXform.e == 4.5f);

  // Empty window (no neighbors): coarse/visibility return empty, geometry
  // still loads for the center.
  FrameWindow empty;
  empty.observations = {center};
  empty.centerIdx = 0;
  CHECK(coarse.run(empty, NormalizedSideInfo{}).empty());
  CHECK(geoStage.run(empty, {}).status == SampleGeometry::Status::Known);

  // Missing required record -> std::runtime_error with a precise message.
  bool threw = false;
  std::string message;
  try {
    past->id.fileFrameIndex = 4;
    center->id.fileFrameIndex = 12345;  // no records for this target
    coarse.run(window, NormalizedSideInfo{});
  } catch (const std::runtime_error& e) {
    threw = true;
    message = e.what();
  }
  CHECK(threw);
  CHECK(message.find("target=12345") != std::string::npos);
  CHECK(message.find("windowOffset=-1") != std::string::npos);
}

}  // namespace

int main() {
  testSinkBackpressureNoLoss();
  testSinkU8AndIndex();
  testNpyRoundTrip();
  testSidecarHappy();
  testSidecarFailures();
  testOracleStages();
  if (g_failures == 0) {
    std::cout << "capture_oracle_tests: ALL PASS\n";
    return 0;
  }
  std::cout << "capture_oracle_tests: " << g_failures << " check(s) FAILED\n";
  return 1;
}
