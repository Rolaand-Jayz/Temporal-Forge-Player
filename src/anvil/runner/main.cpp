// ANVIL offline runner entry point (Qt-free, FSR-free).
// Full implementation (docs/anvil/RECONCILIATION_20261005.md 3.5/3.7):
//   anvil_runner --config pipeline.json   run the configured pipeline
//   anvil_runner --replay manifest.json   exact replay against a run manifest
// Exit codes: 0 ok; 1 replay output mismatch; 2 usage/config error;
// 3 provenance mismatch; 4 runtime failure.
#include "anvil/capture/Capture.hpp"
#include "anvil/core/Sha256.hpp"
#include "anvil/core/Types.hpp"
#include "anvil/core/Window.hpp"
#include "anvil/obs/ObservationSource.hpp"
#include "anvil/oracle/Oracle.hpp"
#include "anvil/oracle/OracleStages.hpp"
#include "anvil/oracle/SidecarOracle.hpp"
#include "anvil/runner/Manifest.hpp"
#include "anvil/runner/PipelineConfig.hpp"
#include "anvil/runner/WindowAssembler.hpp"
#include "anvil/stages/StageFactories.hpp"
#include "anvil/stages/Stages.hpp"

#include <unistd.h>

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <stdexcept>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace fs = std::filesystem;

namespace anvil {
namespace {

constexpr int kExitOk = 0;
constexpr int kExitReplayDiff = 1;
constexpr int kExitUsage = 2;
constexpr int kExitProvenance = 3;
constexpr int kExitRuntime = 4;

int usage() {
  std::fprintf(stderr,
               "anvil_runner: Temporal Forge / ANVIL successor offline runner.\n"
               "Usage:\n"
               "  anvil_runner --config <pipeline.json>\n"
               "  anvil_runner --replay <run_manifest.json>\n"
               "Exit codes: 0 ok; 1 replay mismatch; 2 usage/config; 3 provenance; 4 runtime.\n");
  return kExitUsage;
}

// ---------------------------------------------------------------------------
// Small I/O helpers
// ---------------------------------------------------------------------------

bool readWholeFile(const std::string& path, std::string& out, std::string& err) {
  std::ifstream in(path, std::ios::binary);
  if (!in) {
    err = "cannot open " + path;
    return false;
  }
  std::ostringstream ss;
  ss << in.rdbuf();
  if (in.bad()) {
    err = "read error on " + path;
    return false;
  }
  out = ss.str();
  return true;
}

bool writeWholeFile(const std::string& path, const std::string& content, std::string& err) {
  std::ofstream outF(path, std::ios::binary | std::ios::trunc);
  if (!outF) {
    err = "cannot write " + path;
    return false;
  }
  outF << content;
  outF.flush();
  if (!outF) {
    err = "write error on " + path;
    return false;
  }
  return true;
}

std::string currentBinaryPath() {
  char buf[4096];
  const ssize_t n = ::readlink("/proc/self/exe", buf, sizeof(buf) - 1);
  if (n <= 0) return "";  // recorded as empty; hashing will fail loudly below
  buf[n] = '\0';
  return std::string(buf, static_cast<size_t>(n));
}

// Best-effort provenance: runs from the caller's cwd; empty head means "not
// a git worktree" and is RECORDED as such, never fabricated.
std::string gitHead() {
  std::FILE* p = ::popen("git rev-parse HEAD 2>/dev/null", "r");
  if (!p) return "";
  char buf[128];
  std::string head;
  while (std::fgets(buf, sizeof(buf), p) != nullptr) head += buf;
  ::pclose(p);
  while (!head.empty() && (head.back() == '\n' || head.back() == '\r')) head.pop_back();
  return head;
}

bool gitDirty() {
  std::FILE* p = ::popen("git status --porcelain 2>/dev/null", "r");
  if (!p) return false;
  char buf[4096];
  bool dirty = std::fgets(buf, sizeof(buf), p) != nullptr;
  ::pclose(p);
  return dirty;
}

int64_t nowUnixSeconds() {
  return std::chrono::duration_cast<std::chrono::seconds>(
             std::chrono::system_clock::now().time_since_epoch())
      .count();
}

template <typename F>
auto timed(std::vector<double>& ms, F&& f) -> decltype(f()) {
  const auto t0 = std::chrono::steady_clock::now();
  auto r = f();
  const auto t1 = std::chrono::steady_clock::now();
  ms.push_back(std::chrono::duration<double, std::milli>(t1 - t0).count());
  return r;
}

// ---------------------------------------------------------------------------
// Minimal JSON value parser — used ONLY to read a run manifest back in
// --replay mode. Accepts standard JSON; object key order is preserved
// (lookup is by key, never by iteration order, so this is not an
// output-affecting container).
// ---------------------------------------------------------------------------

struct JValue {
  enum class T { Null, Bool, Number, String, Array, Object };
  T t = T::Null;
  bool b = false;
  std::string raw;  // number token text (int64/uint64-safe)
  std::string str;
  std::vector<JValue> arr;
  std::vector<std::pair<std::string, JValue>> obj;  // order-preserving

  const JValue* find(const std::string& key) const {
    if (t != T::Object) return nullptr;
    for (const auto& kv : obj) {
      if (kv.first == key) return &kv.second;
    }
    return nullptr;
  }
};

struct JParser {
  const std::string& s;
  size_t i = 0;
  std::string err;

  explicit JParser(const std::string& text) : s(text) {}

  bool fail(const std::string& msg) {
    if (err.empty()) err = "manifest JSON: " + msg + " at offset " + std::to_string(i);
    return false;
  }

  void skipWs() {
    while (i < s.size() &&
           (s[i] == ' ' || s[i] == '\t' || s[i] == '\n' || s[i] == '\r')) ++i;
  }

  bool parseString(std::string& out) {
    skipWs();
    if (i >= s.size() || s[i] != '"') return fail("expected string");
    ++i;
    out.clear();
    while (true) {
      if (i >= s.size()) return fail("unterminated string");
      const char c = s[i++];
      if (c == '"') return true;
      if (c != '\\') {
        out.push_back(c);
        continue;
      }
      if (i >= s.size()) return fail("unterminated escape");
      const char e = s[i++];
      switch (e) {
        case '"': out.push_back('"'); break;
        case '\\': out.push_back('\\'); break;
        case '/': out.push_back('/'); break;
        case 'b': out.push_back('\b'); break;
        case 'f': out.push_back('\f'); break;
        case 'n': out.push_back('\n'); break;
        case 'r': out.push_back('\r'); break;
        case 't': out.push_back('\t'); break;
        case 'u': {
          if (i + 4 > s.size()) return fail("truncated \\u escape");
          unsigned code = 0;
          for (int k = 0; k < 4; ++k) {
            const char h = s[i++];
            code <<= 4;
            if (h >= '0' && h <= '9') code |= static_cast<unsigned>(h - '0');
            else if (h >= 'a' && h <= 'f') code |= static_cast<unsigned>(h - 'a' + 10);
            else if (h >= 'A' && h <= 'F') code |= static_cast<unsigned>(h - 'A' + 10);
            else return fail("bad hex digit in \\u escape");
          }
          if (code >= 0xD800u && code <= 0xDFFFu) return fail("surrogate escapes unsupported");
          if (code < 0x80u) {
            out.push_back(static_cast<char>(code));
          } else if (code < 0x800u) {
            out.push_back(static_cast<char>(0xC0u | (code >> 6)));
            out.push_back(static_cast<char>(0x80u | (code & 0x3Fu)));
          } else {
            out.push_back(static_cast<char>(0xE0u | (code >> 12)));
            out.push_back(static_cast<char>(0x80u | ((code >> 6) & 0x3Fu)));
            out.push_back(static_cast<char>(0x80u | (code & 0x3Fu)));
          }
          break;
        }
        default: return fail("unknown escape");
      }
    }
  }

  bool parseValue(JValue& v) {
    skipWs();
    if (i >= s.size()) return fail("unexpected end of input");
    const char c = s[i];
    if (c == '{') {
      v.t = JValue::T::Object;
      ++i;
      skipWs();
      if (i < s.size() && s[i] == '}') {
        ++i;
        return true;
      }
      while (true) {
        std::string key;
        if (!parseString(key)) return false;
        skipWs();
        if (i >= s.size() || s[i] != ':') return fail("expected ':'");
        ++i;
        JValue child;
        if (!parseValue(child)) return false;
        v.obj.emplace_back(std::move(key), std::move(child));
        skipWs();
        if (i < s.size() && s[i] == ',') {
          ++i;
          continue;
        }
        if (i < s.size() && s[i] == '}') {
          ++i;
          return true;
        }
        return fail("expected ',' or '}' in object");
      }
    }
    if (c == '[') {
      v.t = JValue::T::Array;
      ++i;
      skipWs();
      if (i < s.size() && s[i] == ']') {
        ++i;
        return true;
      }
      while (true) {
        JValue child;
        if (!parseValue(child)) return false;
        v.arr.push_back(std::move(child));
        skipWs();
        if (i < s.size() && s[i] == ',') {
          ++i;
          continue;
        }
        if (i < s.size() && s[i] == ']') {
          ++i;
          return true;
        }
        return fail("expected ',' or ']' in array");
      }
    }
    if (c == '"') {
      v.t = JValue::T::String;
      return parseString(v.str);
    }
    if (s.compare(i, 4, "true") == 0) {
      v.t = JValue::T::Bool;
      v.b = true;
      i += 4;
      return true;
    }
    if (s.compare(i, 5, "false") == 0) {
      v.t = JValue::T::Bool;
      v.b = false;
      i += 5;
      return true;
    }
    if (s.compare(i, 4, "null") == 0) {
      v.t = JValue::T::Null;
      i += 4;
      return true;
    }
    // Number: keep the raw token (int64/uint64 values must not lose bits).
    const size_t start = i;
    if (i < s.size() && (s[i] == '-' || s[i] == '+')) ++i;
    while (i < s.size() &&
           (std::isdigit(static_cast<unsigned char>(s[i])) || s[i] == '.' ||
            s[i] == 'e' || s[i] == 'E' || s[i] == '-' || s[i] == '+')) ++i;
    if (i == start) return fail("unexpected character");
    v.t = JValue::T::Number;
    v.raw = s.substr(start, i - start);
    return true;
  }
};

// ---------------------------------------------------------------------------
// Ablation / capture helpers
// ---------------------------------------------------------------------------

// Neighbor-array index for a window offset: past members occupy indices
// [0, past) with offsets -past..-1; future members [past, past+future) with
// offsets +1..+future (offset 0 is the center, never a neighbor).
int neighborArrayIndex(int offset, int past, int future, std::string& err) {
  if (offset == 0 || offset < -past || offset > future) {
    err = "dropNeighbors offset " + std::to_string(offset) +
          " is outside the window (past=" + std::to_string(past) +
          ", future=" + std::to_string(future) + ")";
    return -1;
  }
  return offset < 0 ? offset + past : past + offset - 1;
}

bool parseCommaList(const std::string& spec, std::vector<std::string>& out,
                    std::string& err) {
  size_t pos = 0;
  while (pos <= spec.size()) {
    const size_t comma = spec.find(',', pos);
    const std::string token =
        spec.substr(pos, comma == std::string::npos ? std::string::npos : comma - pos);
    size_t b = token.find_first_not_of(" \t");
    size_t e = token.find_last_not_of(" \t");
    if (b == std::string::npos) {
      err = "empty entry in comma-separated list";
      return false;
    }
    out.push_back(token.substr(b, e - b + 1));
    if (comma == std::string::npos) break;
    pos = comma + 1;
  }
  return true;
}

bool parseDropOffsets(const std::string& spec, std::vector<int>& out, std::string& err) {
  std::vector<std::string> tokens;
  if (!parseCommaList(spec, tokens, err)) {
    err = "dropNeighbors: " + err;
    return false;
  }
  for (const std::string& tok : tokens) {
    size_t k = 0;
    bool neg = false;
    if (!tok.empty() && tok[0] == '-') {
      neg = true;
      k = 1;
    }
    if (k >= tok.size()) {
      err = "dropNeighbors: bad integer token '" + tok + "'";
      return false;
    }
    long val = 0;
    for (; k < tok.size(); ++k) {
      if (tok[k] < '0' || tok[k] > '9') {
        err = "dropNeighbors: bad integer token '" + tok + "'";
        return false;
      }
      val = val * 10 + (tok[k] - '0');
    }
    out.push_back(static_cast<int>(neg ? -val : val));
  }
  std::sort(out.begin(), out.end());
  for (size_t k = 1; k < out.size(); ++k) {
    if (out[k] == out[k - 1]) {
      err = "dropNeighbors: duplicate offset " + std::to_string(out[k]);
      return false;
    }
  }
  return true;
}

const char* kCaptureStageIds[] = {"coarse",  "refine", "visibility",
                                  "geometry", "reconstruct", "backend"};

bool parseCaptureStages(const std::string& spec, std::vector<std::string>& out,
                        std::string& err) {
  if (!parseCommaList(spec, out, err)) {
    err = "captureStages: " + err;
    return false;
  }
  for (const std::string& id : out) {
    bool known = false;
    for (const char* k : kCaptureStageIds) known = known || id == k;
    if (!known) {
      err = "captureStages: unknown stage id '" + id + "'";
      return false;
    }
  }
  return true;
}

// ---------------------------------------------------------------------------
// Pipeline run (shared by --config and --replay)
// ---------------------------------------------------------------------------

struct Provenance {
  std::string binaryPath;
  std::string binarySha256;
  std::string gitHead;
  bool gitDirty = false;
  std::string inputPath;
  std::string inputSha256;
};

// Runs the configured pipeline into outputDir. Returns kExitOk,
// kExitUsage (invalid stage/capture/ablation selections) or kExitRuntime.
// On success `manifest` holds the fully assembled run manifest (the caller
// decides where to persist it).
int runPipeline(const PipelineConfig& cfg, const Provenance& prov,
                const std::string& outputDir, RunManifest& manifest,
                std::string& failErr) {
  // --- config validation -----------------------------------------------
  // Oracle-backed stage twins are wired HERE (not in StageFactories): when
  // an oracle sidecar is configured, any stage selection "oracle" binds to
  // the oracle twin over that source (StageFactories.hpp contract).
  std::shared_ptr<OracleDataSource> oracleSource;
  const bool wantsOracle = cfg.coarse == "oracle" || cfg.visibility == "oracle" ||
                           cfg.geometry == "oracle";
  if (!cfg.oracleSidecar.empty()) {
    auto src = std::make_shared<SidecarOracleSource>();
    if (!src->open(cfg.oracleSidecar)) {
      failErr = "oracle sidecar: " + src->lastError();
      return kExitRuntime;
    }
    oracleSource = std::move(src);
  }
  if (wantsOracle && !oracleSource) {
    failErr =
        "stage selection 'oracle' requires a non-empty oracleSidecar "
        "(loud, never a silent estimator fallback)";
    return kExitUsage;
  }
  auto normalizer = createSideInfoNormalizer(cfg.sideInfoNormalizer);
  auto coarseStage = cfg.coarse == "oracle"
                         ? std::make_unique<OracleCoarse>(oracleSource)
                         : createCoarse(cfg.coarse);
  auto refiner = createRefiner(cfg.refine);
  auto visibilityStage = cfg.visibility == "oracle"
                             ? std::make_unique<OracleVisibilityEstimator>(oracleSource)
                             : createVisibility(cfg.visibility);
  auto geometryStage = cfg.geometry == "oracle"
                           ? std::make_unique<OracleGeometryEstimator>(oracleSource)
                           : createGeometry(cfg.geometry);
  auto reconstructor = createReconstructor(cfg.reconstruct);
  auto backend = createBackend(cfg.backend);
  // Unknown stage selections are config errors, not runtime errors: they
  // are detectable before any frame is decoded.
  if (!normalizer || !coarseStage || !refiner || !visibilityStage || !geometryStage ||
      !reconstructor || !backend) {
    std::ostringstream ss;
    ss << "unknown stage selection:";
    if (!normalizer) ss << " sideInfoNormalizer=" << cfg.sideInfoNormalizer;
    if (!coarseStage) ss << " coarse=" << cfg.coarse;
    if (!refiner) ss << " refine=" << cfg.refine;
    if (!visibilityStage) ss << " visibility=" << cfg.visibility;
    if (!geometryStage) ss << " geometry=" << cfg.geometry;
    if (!reconstructor) ss << " reconstruct=" << cfg.reconstruct;
    if (!backend) ss << " backend=" << cfg.backend;
    failErr = ss.str();
    return kExitUsage;
  }

  std::vector<int> dropOffsets;
  if (!cfg.dropNeighbors.empty() &&
      !parseDropOffsets(cfg.dropNeighbors, dropOffsets, failErr)) {
    return kExitUsage;
  }
  for (int off : dropOffsets) {
    if (neighborArrayIndex(off, cfg.past, cfg.future, failErr) < 0) return kExitUsage;
  }

  std::vector<std::string> captureIds;
  if (!cfg.captureStages.empty() && !parseCaptureStages(cfg.captureStages, captureIds, failErr)) {
    return kExitUsage;
  }
  const bool captureOn = !cfg.captureDir.empty() && !captureIds.empty();

  // --- output / capture dirs --------------------------------------------
  std::error_code ec;
  fs::create_directories(outputDir, ec);
  if (!fs::is_directory(outputDir)) {
    failErr = "cannot create output directory " + outputDir + ": " + ec.message();
    return kExitRuntime;
  }
  std::unique_ptr<DiskCaptureSink> captureSink;
  if (captureOn) {
    fs::create_directories(cfg.captureDir, ec);
    if (!fs::is_directory(cfg.captureDir)) {
      failErr = "cannot create capture directory " + cfg.captureDir + ": " + ec.message();
      return kExitRuntime;
    }
    captureSink = std::make_unique<DiskCaptureSink>(cfg.captureDir, size_t{1024});
  }
  auto wantCapture = [&](const std::string& id) {
    return captureSink && std::find(captureIds.begin(), captureIds.end(), id) != captureIds.end();
  };

  // --- stage chain: created ONCE up front, reset on discontinuities ------
  auto resetStages = [&]() {
    normalizer->onReset();
    coarseStage->onReset();
    refiner->onReset();
    visibilityStage->onReset();
    geometryStage->onReset();
    reconstructor->onReset();
    backend->onReset();
  };

  // Per-stage host timings, fixed key order (deterministic file layout).
  std::vector<std::pair<const char*, std::vector<double>>> timings = {
      {"sideInfoNormalizer", {}}, {"coarse", {}},           {"refine", {}},
      {"visibility", {}},         {"geometry", {}},         {"reconstruct", {}},
      {"backend", {}}};
  auto timingFor = [&](const std::string& stageId) -> std::vector<double>& {
    for (auto& kv : timings) {
      if (kv.first == stageId) return kv.second;
    }
    return timings.back().second;  // unreachable: stage ids are fixed above
  };

  // --- observation source -------------------------------------------------
  // Codec MVs are requested only when the coarse arm consumes them; this
  // forces software decode (hw paths measurably export no MVs — audit
  // dwfrun-015b2ece).
  FileObservationSource source(cfg.inputPath, cfg.coarse == "codec");

  // --- sliding window assembly ---------------------------------------------
  // WindowAssembler owns the buffer state machine: a window is emitted only
  // when the full [past+center+future] span of contiguous valid observations
  // is buffered; invalid frames, index discontinuities, seek-generation
  // changes, and mid-stream dimension/format changes surface as ResetEvents.
  // The runner stages the buffer MUTABLE observations (windowOffset is
  // stamped per emission); stages always receive them as const.
  WindowAssembler assembler(cfg.past, cfg.future);
  std::vector<ManifestResetRecord> resets;

  int64_t framesEmitted = 0;
  int64_t firstEmitted = -1;
  int64_t lastEmitted = -1;
  std::vector<ManifestOutputRecord> outputs;

  auto captureMotionFields = [&](const std::string& stageId,
                                 const std::vector<MotionField>& fields,
                                 const FrameWindow& window) {
    if (!wantCapture(stageId)) return;
    for (size_t n = 0; n < fields.size(); ++n) {
      const MotionField& mf = fields[n];
      const int off = n < static_cast<size_t>(cfg.past)
                          ? static_cast<int>(n) - cfg.past
                          : static_cast<int>(n) - cfg.past + 1;
      CapturedTensor t;
      t.stageId = stageId;
      t.fileFrameIndex = window.center().id.fileFrameIndex;
      t.name = "motion_dydx_off" + std::to_string(off);
      t.shape = {2 * static_cast<int64_t>(mf.gridHeight) * mf.gridWidth};
      t.f32.reserve(mf.blocks.size() * 2);
      for (const BlockVec& b : mf.blocks) {  // fixed grid order
        t.f32.push_back(b.mv.y);
        t.f32.push_back(b.mv.x);
      }
      captureSink->capture(t);
    }
  };
  auto captureVisibility = [&](const std::vector<Visibility>& vis,
                               const FrameWindow& window) {
    if (!wantCapture("visibility")) return;
    for (size_t n = 0; n < vis.size(); ++n) {
      const int off = n < static_cast<size_t>(cfg.past)
                          ? static_cast<int>(n) - cfg.past
                          : static_cast<int>(n) - cfg.past + 1;
      CapturedTensor t;
      t.stageId = "visibility";
      t.fileFrameIndex = window.center().id.fileFrameIndex;
      t.name = "weight_off" + std::to_string(off);
      t.shape = {vis[n].height, vis[n].width};
      t.f32 = vis[n].weight;
      captureSink->capture(t);
    }
  };
  auto captureGeometry = [&](const SampleGeometry& geo, const FrameWindow& window) {
    if (!wantCapture("geometry")) return;
    CapturedTensor t;
    t.stageId = "geometry";
    t.fileFrameIndex = window.center().id.fileFrameIndex;
    t.name = "xform";
    t.shape = {6};
    t.f32 = {geo.globalXform.a, geo.globalXform.b, geo.globalXform.c,
             geo.globalXform.d, geo.globalXform.e, geo.globalXform.f};
    captureSink->capture(t);
  };
  auto captureOut = [&](const std::string& stageId, const WorkingImage& img,
                        const FrameWindow& window) {
    if (!wantCapture(stageId)) return;
    CapturedTensor t;
    t.stageId = stageId;
    t.fileFrameIndex = window.center().id.fileFrameIndex;
    t.name = "out";
    t.shape = {3, img.height, img.width};
    t.f32.reserve(img.r.size() * 3);
    t.f32.insert(t.f32.end(), img.r.begin(), img.r.end());
    t.f32.insert(t.f32.end(), img.g.begin(), img.g.end());
    t.f32.insert(t.f32.end(), img.b.begin(), img.b.end());
    captureSink->capture(t);
  };

  Observation obs;
  while (source.next(obs)) {
    // Range filtering stays outside the assembler, exactly as before: frames
    // below firstFrame were never buffered, so the assembler's boundaries on
    // them were no-ops on an always-empty pre-range buffer (frame indices are
    // monotonic within one seek generation, so a pre-range frame can never
    // follow a buffered one without a generation change in between).
    if (obs.id.fileFrameIndex < static_cast<uint64_t>(cfg.firstFrame)) continue;
    if (cfg.lastFrame >= 0 &&
        obs.id.fileFrameIndex > static_cast<uint64_t>(cfg.lastFrame)) {
      break;  // sequential display order: everything past the range is out
    }
    const PushResult pr = assembler.push(std::move(obs), source.generation());
    for (const ResetEvent& ev : pr.resets) {
      if (ev.cause == ResetCause::Dimensions) {
        // v1 has no rescale path: silent mixed-dimension output is
        // forbidden — fail the run loudly (exit 4).
        failErr = "frame " + std::to_string(ev.frameIndex) +
                  ": mid-stream plane dimension/pixel-format change" +
                  (ev.detail.empty() ? "" : " (" + ev.detail + ")") +
                  " — v1 has no rescale path, refusing to emit "
                  "mixed-dimension output";
        return kExitRuntime;
      }
      // Generation/index/invalid boundaries: flush stage history and
      // continue (observable in the manifest's additive "resets" array).
      resets.push_back({static_cast<int64_t>(ev.frameIndex), resetCauseName(ev.cause)});
      resetStages();
    }
    if (pr.windows.empty()) continue;
    const FrameWindow& window = pr.windows.front();

    // Stage chain in DAG order. Stage failures (e.g. a missing oracle record)
    // arrive as exceptions and fail the run loudly — never degraded silently.
    std::vector<MotionField> refinedFields;
    std::vector<Visibility> vis;
    SampleGeometry geo;
    try {
      NormalizedSideInfo sideSeed;
      sideSeed.side = window.center().side;  // raw center side info is the seed
      NormalizedSideInfo side = timed(timingFor("sideInfoNormalizer"),
                                      [&] { return normalizer->run(window, sideSeed); });
      std::vector<MotionField> coarseFields = timed(
          timingFor("coarse"), [&] { return coarseStage->run(window, side); });
      captureMotionFields("coarse", coarseFields, window);
      refinedFields = timed(
          timingFor("refine"), [&] { return refiner->run(window, coarseFields); });
      captureMotionFields("refine", refinedFields, window);
      vis = timed(
          timingFor("visibility"), [&] { return visibilityStage->run(window, refinedFields); });
      captureVisibility(vis, window);
      geo = timed(timingFor("geometry"),
                  [&] { return geometryStage->run(window, refinedFields); });
      captureGeometry(geo, window);
    } catch (const std::exception& e) {
      failErr = "frame " + std::to_string(window.center().id.fileFrameIndex) +
                ": stage chain: " + e.what();
      return kExitRuntime;
    }

    // Explicit decode->working-space boundary (reconciliation 3.8). A
    // failure here fails the run loudly with the error string: no silent
    // transfer assumption is ever made.
    StageBundle bundle;
    std::string convErr;
    std::string declaredSpace;
    if (!convertToWorking(window.center().planes, window.center().color,
                          cfg.transferOverride, bundle.center, convErr, declaredSpace)) {
      failErr = "frame " + std::to_string(window.center().id.fileFrameIndex) +
                " (center): " + convErr;
      return kExitRuntime;
    }
    bundle.neighbors.reserve(window.observations.size() - 1);
    for (size_t k = 0; k < window.observations.size(); ++k) {
      if (k == window.centerIdx) continue;
      WorkingImage img;
      if (!convertToWorking(window.observations[k]->planes, window.observations[k]->color,
                            cfg.transferOverride, img, convErr, declaredSpace)) {
        failErr = "frame " +
                  std::to_string(window.observations[k]->id.fileFrameIndex) +
                  " (neighbor): " + convErr;
        return kExitRuntime;
      }
      bundle.neighbors.push_back(std::move(img));
      bundle.neighborFrameIndices.push_back(
          static_cast<int64_t>(window.observations[k]->id.fileFrameIndex));
    }
    bundle.neighborMotion = refinedFields;
    bundle.neighborVisibility = std::move(vis);
    bundle.geometry = geo;

    // --- neighbor ablation BEFORE reconstruct ------------------------------
    // Dropped members are removed from all four neighbor arrays; their
    // indices are recorded in the ablation record. Captures above were
    // taken before ablation (they document stage outputs, not the arm).
    std::vector<int64_t> droppedIndices;
    std::vector<size_t> dropArrayIdx;
    for (int off : dropOffsets) {
      const int arrIdx = neighborArrayIndex(off, cfg.past, cfg.future, failErr);
      // Past neighbors sit at window observation index == array index; the
      // center occupies index `past`, so future neighbors shift by one.
      const int obsIdx = arrIdx < cfg.past ? arrIdx : arrIdx + 1;
      dropArrayIdx.push_back(static_cast<size_t>(arrIdx));
      droppedIndices.push_back(
          static_cast<int64_t>(window.observations[static_cast<size_t>(obsIdx)]->id.fileFrameIndex));
    }
    std::sort(dropArrayIdx.rbegin(), dropArrayIdx.rend());
    for (size_t k : dropArrayIdx) {
      bundle.neighbors.erase(bundle.neighbors.begin() + static_cast<long>(k));
      bundle.neighborFrameIndices.erase(bundle.neighborFrameIndices.begin() +
                                        static_cast<long>(k));
      bundle.neighborMotion.erase(bundle.neighborMotion.begin() + static_cast<long>(k));
      bundle.neighborVisibility.erase(bundle.neighborVisibility.begin() +
                                      static_cast<long>(k));
    }

    ReconstructionResult result;
    WorkingImage presented;
    try {
      result = timed(timingFor("reconstruct"),
                     [&] { return reconstructor->run(window, bundle); });
      result.ablation.droppedFrameIndices = droppedIndices;
      captureOut("reconstruct", result.out, window);

      presented = timed(timingFor("backend"), [&] { return backend->present(result); });
      captureOut("backend", presented, window);
    } catch (const std::exception& e) {
      failErr = "frame " + std::to_string(window.center().id.fileFrameIndex) +
                ": reconstruct/backend: " + e.what();
      return kExitRuntime;
    }

    // --- output frame -------------------------------------------------------
    char name[32];
    std::snprintf(name, sizeof(name), "out_%08llu.npy",
                  static_cast<unsigned long long>(window.center().id.fileFrameIndex));
    std::vector<float> stack;
    stack.reserve(presented.r.size() * 3);
    stack.insert(stack.end(), presented.r.begin(), presented.r.end());
    stack.insert(stack.end(), presented.g.begin(), presented.g.end());
    stack.insert(stack.end(), presented.b.begin(), presented.b.end());
    const std::vector<int64_t> shape = {3, presented.height, presented.width};
    const std::string outPath = outputDir + "/" + name;
    if (!writeNpy(outPath, shape, DType::F32, stack.data())) {
      failErr = "failed to write " + outPath;
      return kExitRuntime;
    }
    std::string outSha;
    if (!sha256File(outPath, outSha, convErr)) {
      failErr = "failed to hash output " + outPath + ": " + convErr;
      return kExitRuntime;
    }
    outputs.push_back({name, outSha});
    if (firstEmitted < 0) {
      firstEmitted = static_cast<int64_t>(window.center().id.fileFrameIndex);
    }
    lastEmitted = static_cast<int64_t>(window.center().id.fileFrameIndex);
    ++framesEmitted;
  }
  const std::string srcErr = source.lastError();
  assembler.endOfStream();  // trailing accounting for the final (partial) run
  if (!srcErr.empty()) {
    failErr = "observation source: " + srcErr;
    return kExitRuntime;
  }
  if (captureSink) captureSink->flush();

  // --- timings.json (fixed key order; %.3f ms) -----------------------------
  {
    std::ostringstream ts;
    ts << "{";
    bool firstKey = true;
    for (const auto& kv : timings) {
      if (!firstKey) ts << ",";
      firstKey = false;
      ts << "\"" << kv.first << "\":[";
      for (size_t k = 0; k < kv.second.size(); ++k) {
        if (k != 0) ts << ",";
        char num[32];
        std::snprintf(num, sizeof(num), "%.3f", kv.second[k]);
        ts << num;
      }
      ts << "]";
    }
    ts << "}";
    std::string werr;
    if (!writeWholeFile(outputDir + "/timings.json", ts.str(), werr)) {
      failErr = werr;
      return kExitRuntime;
    }
  }

  // --- run manifest ---------------------------------------------------------
  const std::string canonical = cfg.canonicalJson();
  manifest = RunManifest{};
  manifest.runId = computeRunId(canonical, prov.inputSha256, prov.binarySha256, cfg.seed,
                                determinismModeFromConfig(cfg.deterministic),
                                /*decodeMode=*/0);
  manifest.createdAt = iso8601Utc(nowUnixSeconds());
  manifest.gitHead = prov.gitHead;
  manifest.gitDirty = prov.gitDirty;
  manifest.binaryPath = prov.binaryPath;
  manifest.binarySha256 = prov.binarySha256;
  manifest.configCanonicalJson = canonical;
  manifest.configSha256 = sha256(canonical.data(), canonical.size());
  manifest.inputPath = prov.inputPath;
  manifest.inputSha256 = prov.inputSha256;
  manifest.frameFirst = firstEmitted;
  manifest.frameLast = lastEmitted;
  manifest.framesEmitted = framesEmitted;
  manifest.skippedWarmup = assembler.stats().skippedWarmup;
  manifest.skippedTrailing = assembler.stats().skippedTrailing;
  manifest.resets = std::move(resets);
  manifest.past = cfg.past;
  manifest.future = cfg.future;
  // Record the implementation name each stage reports (what actually ran);
  // the config selections remain auditable via canonicalJson.
  manifest.sideInfoNormalizer = normalizer->name();
  manifest.coarse = coarseStage->name();
  manifest.refine = refiner->name();
  manifest.visibility = visibilityStage->name();
  manifest.geometry = geometryStage->name();
  manifest.reconstruct = reconstructor->name();
  manifest.backend = backend->name();
  manifest.oracleSidecar = cfg.oracleSidecar;
  if (cfg.coarse == "oracle") manifest.oracleArms.push_back("coarse");
  if (cfg.visibility == "oracle") manifest.oracleArms.push_back("visibility");
  if (cfg.geometry == "oracle") manifest.oracleArms.push_back("geometry");
  manifest.deterministic = cfg.deterministic;
  manifest.seed = cfg.seed;
  manifest.decodeMode = "software";  // v1 deterministic default (PROJECT DECISION)
  manifest.transferOverride = cfg.transferOverride;
  manifest.dropNeighbors = cfg.dropNeighbors;
  manifest.outputs = std::move(outputs);
  manifest.timingsFile = "timings.json";
  return kExitOk;
}

// ---------------------------------------------------------------------------
// --replay
// ---------------------------------------------------------------------------

int replayManifest(const std::string& manifestPath) {
  std::string text, err;
  if (!readWholeFile(manifestPath, text, err)) {
    std::fprintf(stderr, "anvil_runner: %s\n", err.c_str());
    return kExitUsage;
  }
  JParser p(text);
  JValue root;
  if (!p.parseValue(root) || !p.err.empty()) {
    std::fprintf(stderr, "anvil_runner: %s\n", p.err.c_str());
    return kExitUsage;
  }
  const JValue* schema = root.find("schema");
  const JValue* runIdV = root.find("runId");
  const JValue* binary = root.find("binary");
  const JValue* config = root.find("config");
  const JValue* input = root.find("input");
  const JValue* outputs = root.find("outputs");
  if (!schema || schema->t != JValue::T::String || !runIdV || !binary || !config || !input ||
      !outputs || outputs->t != JValue::T::Array) {
    std::fprintf(stderr, "anvil_runner: manifest %s is missing required fields\n",
                 manifestPath.c_str());
    return kExitUsage;
  }
  if (schema->str != "anvil.run_manifest.v1") {
    std::fprintf(stderr, "anvil_runner: unsupported manifest schema '%s'\n",
                 schema->str.c_str());
    return kExitUsage;
  }
  const JValue* binSha = binary->find("sha256");
  const JValue* cfgJson = config->find("canonicalJson");
  const JValue* inputPathV = input->find("path");
  const JValue* inputSha = input->find("sha256");
  if (!binSha || !cfgJson || !inputPathV || !inputSha) {
    std::fprintf(stderr, "anvil_runner: manifest %s is missing provenance fields\n",
                 manifestPath.c_str());
    return kExitUsage;
  }

  // --- re-serialize config from the manifest's canonicalJson ---------------
  PipelineConfig cfg;
  std::string cfgErr;
  if (!parsePipelineConfig(cfgJson->str, cfg, cfgErr)) {
    std::fprintf(stderr,
                 "anvil_runner: provenance mismatch — manifest config canonicalJson does "
                 "not parse: %s\n",
                 cfgErr.c_str());
    return kExitProvenance;
  }
  const std::string reserialized = cfg.canonicalJson();
  if (reserialized != cfgJson->str) {
    std::fprintf(stderr,
                 "anvil_runner: provenance mismatch — config re-serialization differs from "
                 "the manifest's canonicalJson\n");
    return kExitProvenance;
  }
  const std::string configSha = sha256(reserialized.data(), reserialized.size());
  // configSha is implied by the byte-equality check above; the explicit
  // hash is only used if the canonicalJson ever diverges from the manifest.
  (void)configSha;

  // --- re-hash the CURRENT binary ------------------------------------------
  const std::string binaryPath = currentBinaryPath();
  std::string binarySha;
  if (binaryPath.empty() || !sha256File(binaryPath, binarySha, err)) {
    std::fprintf(stderr, "anvil_runner: cannot hash current binary: %s\n", err.c_str());
    return kExitProvenance;
  }
  if (binarySha != binSha->str) {
    std::fprintf(stderr,
                 "anvil_runner: provenance mismatch — binary hash differs (manifest %s, "
                 "current %s at %s)\n",
                 binSha->str.c_str(), binarySha.c_str(), binaryPath.c_str());
    return kExitProvenance;
  }

  // --- re-hash the input file ------------------------------------------------
  std::string inputShaNow;
  if (!sha256File(inputPathV->str, inputShaNow, err)) {
    std::fprintf(stderr, "anvil_runner: provenance mismatch — input file unreadable: %s\n",
                 err.c_str());
    return kExitProvenance;
  }
  if (inputShaNow != inputSha->str) {
    std::fprintf(stderr,
                 "anvil_runner: provenance mismatch — input %s hash differs (manifest %s, "
                 "current %s)\n",
                 inputPathV->str.c_str(), inputSha->str.c_str(), inputShaNow.c_str());
    return kExitProvenance;
  }

  // --- aggregate replay key ----------------------------------------------------
  const std::string recomputedRunId =
      computeRunId(reserialized, inputShaNow, binarySha, cfg.seed,
                   determinismModeFromConfig(cfg.deterministic), /*decodeMode=*/0);
  if (recomputedRunId != runIdV->str) {
    std::fprintf(stderr,
                 "anvil_runner: provenance mismatch — replay key differs (manifest runId "
                 "%s, recomputed %s)\n",
                 runIdV->str.c_str(), recomputedRunId.c_str());
    return kExitProvenance;
  }

  // --- re-run into a fresh temp output dir ------------------------------------
  std::error_code ec;
  const std::string tempDir =
      (fs::temp_directory_path(ec) / ("anvil_replay_" + std::to_string(::getpid()))).string();
  fs::remove_all(tempDir, ec);
  Provenance prov;
  prov.binaryPath = binaryPath;
  prov.binarySha256 = binarySha;
  prov.gitHead = gitHead();
  prov.gitDirty = gitDirty();
  prov.inputPath = inputPathV->str;
  prov.inputSha256 = inputShaNow;
  RunManifest rerunManifest;
  const int runCode = runPipeline(cfg, prov, tempDir, rerunManifest, err);
  if (runCode != kExitOk) {
    fs::remove_all(tempDir, ec);
    std::fprintf(stderr, "anvil_runner: replay run failed (exit %d): %s\n", runCode,
                 err.c_str());
    return runCode;
  }

  // --- byte-compare outputs ----------------------------------------------------
  std::vector<std::string> differing;
  for (const ManifestOutputRecord& rec : rerunManifest.outputs) {
    std::string nowSha;
    if (!sha256File(tempDir + "/" + rec.file, nowSha, err)) {
      differing.push_back(rec.file + " (unreadable)");
      continue;
    }
    bool matched = false;
    for (const JValue& entry : outputs->arr) {
      const JValue* f = entry.find("file");
      const JValue* h = entry.find("sha256");
      if (f && h && f->str == rec.file) {
        if (h->str != nowSha) differing.push_back(rec.file + " (sha256 differs)");
        matched = true;
        break;
      }
    }
    if (!matched) differing.push_back(rec.file + " (not present in manifest)");
  }
  for (const JValue& entry : outputs->arr) {
    const JValue* f = entry.find("file");
    if (!f) continue;
    bool produced = false;
    for (const ManifestOutputRecord& rec : rerunManifest.outputs) produced = produced || rec.file == f->str;
    if (!produced) differing.push_back(f->str + " (not produced by replay run)");
  }
  fs::remove_all(tempDir, ec);
  if (!differing.empty()) {
    std::fprintf(stderr, "anvil_runner: replay mismatch — %zu differing frame(s):\n",
                 differing.size());
    for (const std::string& d : differing) std::fprintf(stderr, "  %s\n", d.c_str());
    return kExitReplayDiff;
  }
  return kExitOk;
}

}  // namespace
}  // namespace anvil

int main(int argc, char** argv) {
  using namespace anvil;
  if (argc != 3) return usage();
  const std::string mode = argv[1];
  const std::string path = argv[2];

  if (mode == "--replay") return replayManifest(path);

  if (mode != "--config") return usage();

  // --- --config mode ---------------------------------------------------------
  std::string text, err;
  if (!readWholeFile(path, text, err)) {
    std::fprintf(stderr, "anvil_runner: %s\n", err.c_str());
    return kExitUsage;
  }
  PipelineConfig cfg;
  if (!parsePipelineConfig(text, cfg, err)) {
    std::fprintf(stderr, "anvil_runner: config error: %s\n", err.c_str());
    return kExitUsage;
  }
  if (cfg.inputPath.empty() || cfg.outputDir.empty()) {
    std::fprintf(stderr,
                 "anvil_runner: config error: inputPath and outputDir must be non-empty\n");
    return kExitUsage;
  }

  Provenance prov;
  prov.binaryPath = currentBinaryPath();
  if (!sha256File(prov.binaryPath, prov.binarySha256, err)) {
    std::fprintf(stderr, "anvil_runner: cannot hash current binary: %s\n", err.c_str());
    return kExitProvenance;
  }
  if (!sha256File(cfg.inputPath, prov.inputSha256, err)) {
    std::fprintf(stderr, "anvil_runner: cannot hash input %s: %s\n", cfg.inputPath.c_str(),
                 err.c_str());
    return kExitUsage;
  }
  prov.gitHead = gitHead();
  prov.gitDirty = gitDirty();
  prov.inputPath = cfg.inputPath;

  RunManifest manifest;
  const int code = runPipeline(cfg, prov, cfg.outputDir, manifest, err);
  if (code != kExitOk) {
    std::fprintf(stderr, "anvil_runner: %s\n", err.c_str());
    return code;
  }
  if (!writeWholeFile(cfg.outputDir + "/run_manifest.json", manifest.toJson(), err)) {
    std::fprintf(stderr, "anvil_runner: %s\n", err.c_str());
    return kExitRuntime;
  }
  std::fprintf(stdout,
               "anvil_runner: run %s complete — %lld frame(s) emitted "
               "(warmup skipped %lld, trailing skipped %lld), outputs in %s\n",
               manifest.runId.c_str(), static_cast<long long>(manifest.framesEmitted),
               static_cast<long long>(manifest.skippedWarmup),
               static_cast<long long>(manifest.skippedTrailing), cfg.outputDir.c_str());
  return kExitOk;
}
