// ANVIL runner module tests (owned by the runner implementation worker).
// Scope: SHA-256 correctness, strict config parsing, replay-key stability.
// A full end-to-end pipeline run is NOT attempted here (stage/obs/oracle
// implementations land in parallel); the orchestrator runs integration.
#include "anvil/core/Sha256.hpp"
#include "anvil/runner/Manifest.hpp"
#include "anvil/runner/PipelineConfig.hpp"
#include "anvil/runner/WindowAssembler.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

using namespace anvil;  // test brevity; symbols are anvil-qualified anyway

namespace {

int g_failures = 0;

#define CHECK(cond)                                                            \
  do {                                                                         \
    if (!(cond)) {                                                             \
      std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);     \
      ++g_failures;                                                            \
    }                                                                          \
  } while (0)

#define CHECK_EQ_STR(a, b)                                                     \
  do {                                                                         \
    const std::string va_ = (a);                                               \
    const std::string vb_ = (b);                                               \
    if (va_ != vb_) {                                                          \
      std::fprintf(stderr, "FAIL %s:%d: '%s' != '%s'\n", __FILE__, __LINE__,   \
                   va_.c_str(), vb_.c_str());                                  \
      ++g_failures;                                                            \
    }                                                                          \
  } while (0)

// ---------------------------------------------------------------------------
// SHA-256
// ---------------------------------------------------------------------------

void testSha256Vectors() {
  // FIPS 180-4 / NIST known-answer vectors.
  CHECK_EQ_STR(anvil::sha256("", 0),
               "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
  CHECK_EQ_STR(anvil::sha256("abc", 3),
               "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
  // Two-block message from FIPS 180-4's example (B.2 "abcdbcde...").
  const std::string twoBlock = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
  CHECK_EQ_STR(anvil::sha256(twoBlock.data(), twoBlock.size()),
               "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
  // NIST million-'a' vector: exercises the multi-chunk streaming path.
  const std::string millionA(1'000'000, 'a');
  CHECK_EQ_STR(anvil::sha256(millionA.data(), millionA.size()),
               "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");
}

void testSha256StreamingAgreement() {
  // Deterministic LCG pattern (no RNG in the sense of runtime
  // nondeterminism: fixed seed, fixed sequence). Length deliberately straddles
  // the 64-byte block boundary (63/64/65) and the file chunk buffer shape.
  auto lcgByte = [](uint64_t& state) {
    state = state * 6364136223846793005ull + 1442695040888963407ull;
    return static_cast<unsigned char>(state >> 33);
  };
  for (size_t len : {size_t{0}, size_t{1}, size_t{55}, size_t{63}, size_t{64}, size_t{65},
                     size_t{127}, size_t{128}, size_t{129}, size_t{1000}, size_t{65537}}) {
    std::vector<unsigned char> data(len);
    uint64_t state = 0x123456789abcdefull;
    for (size_t i = 0; i < len; ++i) data[i] = lcgByte(state);

    // Reference: single-shot.
    const std::string oneShot = anvil::sha256(data.data(), len);

    // Path 2: streaming in many small updates (odd sizes cross block bounds).
    anvil::Sha256 h;
    size_t off = 0, chunk = 7;
    while (off < len) {
      const size_t n = std::min(chunk, len - off);
      h.update(data.data() + off, n);
      off += n;
      chunk = chunk * 3 + 1;  // deterministic varying chunk sizes
    }
    CHECK_EQ_STR(h.hexDigest(), oneShot);

    // Path 3: byte-at-a-time.
    anvil::Sha256 h2;
    for (size_t i = 0; i < len; ++i) h2.update(data.data() + i, 1);
    CHECK_EQ_STR(h2.hexDigest(), oneShot);

    // Path 4: reuse via reset() must reproduce the same digest.
    h2.reset();
    h2.update(data.data(), len);
    CHECK_EQ_STR(h2.hexDigest(), oneShot);
  }
}

void testSha256File() {
  const char* path = "/tmp/anvil_runner_test_sha.bin";
  {
    std::ofstream f(path, std::ios::binary);
    const std::string payload = "anvil sha256 file test payload, 48 bytes exactly!!!";
    f.write(payload.data(), static_cast<std::streamsize>(payload.size()));
  }
  std::string hex, err;
  CHECK(anvil::sha256File(path, hex, err));
  const std::string payload = "anvil sha256 file test payload, 48 bytes exactly!!!";
  CHECK_EQ_STR(hex, anvil::sha256(payload.data(), payload.size()));
  // Missing file must fail with an error, not a hash.
  std::string hex2;
  CHECK(!anvil::sha256File("/tmp/anvil_runner_test_missing_file", hex2, err));
  CHECK(!err.empty());
  std::remove(path);
}

// ---------------------------------------------------------------------------
// PipelineConfig parsing
// ---------------------------------------------------------------------------

PipelineConfig sampleConfig() {
  PipelineConfig c;
  c.inputPath = "/data/clip.mp4";
  c.firstFrame = 10;
  c.lastFrame = 199;
  c.past = 2;
  c.future = 1;
  c.sideInfoNormalizer = "densify_blocks";
  c.coarse = "codec";
  c.refine = "hillclimb2";
  c.visibility = "photo_fwbw";
  c.geometry = "global_xform";
  c.reconstruct = "weighted_blend";
  c.backend = "passthrough";
  c.oracleSidecar = "/data/oracle_index.json";
  c.deterministic = false;
  c.seed = 42;
  c.captureDir = "/tmp/capture";
  c.captureStages = "coarse,reconstruct";
  c.outputDir = "/tmp/out";
  c.transferOverride = "bt709";
  c.dropNeighbors = "-1,1";
  return c;
}

void testConfigRoundTrip() {
  const PipelineConfig c = sampleConfig();
  const std::string canonical = c.canonicalJson();
  // Field order is fixed: the two new fields come after outputDir.
  CHECK(canonical.find("\"outputDir\":\"/tmp/out\",\"transferOverride\":\"bt709\","
                       "\"dropNeighbors\":\"-1,1\"}") != std::string::npos);

  anvil::PipelineConfig parsed;
  std::string err;
  CHECK(anvil::parsePipelineConfig(canonical, parsed, err));
  CHECK_EQ_STR(err, "");
  CHECK_EQ_STR(parsed.canonicalJson(), canonical);  // round-trip byte equality
  CHECK(parsed.inputPath == c.inputPath);
  CHECK(parsed.firstFrame == 10 && parsed.lastFrame == 199);
  CHECK(parsed.past == 2 && parsed.future == 1);
  CHECK(parsed.seed == 42 && parsed.deterministic == false);
  CHECK_EQ_STR(parsed.transferOverride, "bt709");
  CHECK_EQ_STR(parsed.dropNeighbors, "-1,1");
}

void testConfigWhitespaceAndOrder() {
  // Any whitespace, any field order: parse into a shuffled, space-padded form.
  const std::string messy =
      "{\n  \"seed\" : 7 ,\n\t\"outputDir\"\t:\n \"/tmp/o\" ,\r\n"
      "  \"future\"  :3, \"transferOverride\":\"srgb\", \"dropNeighbors\":\" -2 \","
      " \"inputPath\":\"in.mp4\", \"deterministic\":true,"
      " \"sideInfoNormalizer\":\"passthrough\",\"coarse\":\"zero\",\"refine\":\"none\","
      " \"visibility\":\"unity\",\"geometry\":\"unknown\",\"reconstruct\":\"center\","
      " \"backend\":\"null\",\"oracleSidecar\":\"\",\"firstFrame\":0,\"lastFrame\":-1,"
      " \"past\":0,\"captureDir\":\"\",\"captureStages\":\"\"}\n";
  anvil::PipelineConfig parsed;
  std::string err;
  CHECK(anvil::parsePipelineConfig(messy, parsed, err));
  CHECK_EQ_STR(err, "");
  CHECK(parsed.seed == 7 && parsed.future == 3 && parsed.deterministic);
  CHECK_EQ_STR(parsed.transferOverride, "srgb");
  CHECK_EQ_STR(parsed.dropNeighbors, " -2 ");  // string values pass through verbatim
  // Re-serialization is canonical regardless of input ordering.
  CHECK_EQ_STR(parsed.canonicalJson(), parsed.canonicalJson());
  anvil::PipelineConfig expected;
  expected.seed = 7;
  expected.outputDir = "/tmp/o";
  expected.future = 3;
  expected.transferOverride = "srgb";
  expected.dropNeighbors = " -2 ";
  expected.inputPath = "in.mp4";
  expected.captureStages = "";
  expected.coarse = "zero";
  expected.visibility = "unity";
  expected.reconstruct = "center";
  expected.backend = "null";
  CHECK_EQ_STR(parsed.canonicalJson(), expected.canonicalJson());
}

void testConfigMissingFieldsDefault() {
  // Empty object: every field takes its PipelineConfig{} default.
  anvil::PipelineConfig parsed;
  std::string err;
  CHECK(anvil::parsePipelineConfig("{}", parsed, err));
  CHECK_EQ_STR(err, "");
  CHECK_EQ_STR(parsed.canonicalJson(), anvil::PipelineConfig{}.canonicalJson());
  CHECK(parsed.firstFrame == 0 && parsed.lastFrame == -1 && parsed.past == 0 &&
        parsed.future == 0 && parsed.seed == 0 && parsed.deterministic);
  // Missing fields mixed with present ones: present wins, missing default.
  CHECK(anvil::parsePipelineConfig(R"({"past": 4})", parsed, err));
  CHECK(parsed.past == 4);
  CHECK(parsed.future == 0 && parsed.seed == 0);
}

void expectParseError(const std::string& json, const std::string& tag) {
  anvil::PipelineConfig parsed;
  std::string err;
  const bool ok = anvil::parsePipelineConfig(json, parsed, err);
  if (ok || err.empty()) {
    std::fprintf(stderr, "FAIL %s:%d: expected parse error for %s\n", __FILE__, __LINE__,
                 tag.c_str());
    ++g_failures;
  }
}

void testConfigRejections() {
  expectParseError(R"({"nope": 1})", "unknown field");
  expectParseError(R"({"past": 1, "past": 2})", "duplicate field");
  expectParseError(R"({"past": "1"})", "string for integer");
  expectParseError(R"({"past": 1.5})", "float for integer");
  expectParseError(R"({"past": 1e3})", "exponent for integer");
  expectParseError(R"({"inputPath": 5})", "number for string");
  expectParseError(R"({"deterministic": "true"})", "string for bool");
  expectParseError(R"({"deterministic": 1})", "number for bool");
  expectParseError(R"({"seed": -1})", "negative seed");
  expectParseError(R"({"seed": 18446744073709551616})", "seed overflow");
  expectParseError(R"({"past": 9223372036854775808})", "int64 overflow");
  expectParseError(R"({"coarse": {"a":1}})", "nested object value");
  expectParseError(R"({"dropNeighbors": [1]})", "array value");
  expectParseError(R"(["not an object"])", "top-level array");
  expectParseError(R"({"past": 1} trailing)", "trailing content");
  expectParseError(R"({"past": })", "missing value");
  expectParseError(R"(noBraces)", "garbage input");
  // Rejections must leave the out param reset to defaults.
  anvil::PipelineConfig parsed = sampleConfig();
  std::string err;
  CHECK(!anvil::parsePipelineConfig(R"({"unknownField": true})", parsed, err));
  CHECK(parsed.past == 0 && parsed.inputPath.empty());
}

// ---------------------------------------------------------------------------
// Replay key / runId stability
// ---------------------------------------------------------------------------

void testReplayKeyStability() {
  const PipelineConfig c = sampleConfig();
  const std::string canonical = c.canonicalJson();
  const std::string inputSha(64, 'a');
  const std::string binarySha(64, 'b');

  const std::string id1 =
      anvil::computeRunId(canonical, inputSha, binarySha, c.seed, 0, 0);
  const std::string id2 =
      anvil::computeRunId(canonical, inputSha, binarySha, c.seed, 0, 0);
  CHECK_EQ_STR(id1, id2);          // same inputs -> same runId
  CHECK_EQ_STR(id1.length() == 16 ? "ok" : "bad", "ok");  // first 16 hex chars
  for (char ch : id1) {
    const bool hexOk = (ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f');
    if (!hexOk) {
      std::fprintf(stderr, "FAIL: runId is not lowercase hex: %s\n", id1.c_str());
      ++g_failures;
      break;
    }
  }
  // Changed seed -> different runId.
  const std::string idSeed =
      anvil::computeRunId(canonical, inputSha, binarySha, c.seed + 1, 0, 0);
  CHECK(idSeed != id1);
  // Changed config -> different runId.
  PipelineConfig c2 = c;
  c2.seed = 43;
  const std::string idCfg =
      anvil::computeRunId(c2.canonicalJson(), inputSha, binarySha, c.seed, 0, 0);
  CHECK(idCfg != id1);
  // Changed input/binary hashes -> different runId.
  CHECK(anvil::computeRunId(canonical, inputSha + "0", binarySha, c.seed, 0, 0) != id1);
  CHECK(anvil::computeRunId(canonical, inputSha, binarySha + "0", c.seed, 0, 0) != id1);
  // Changed determinism mode -> different runId.
  CHECK(anvil::computeRunId(canonical, inputSha, binarySha, c.seed, 1, 0) != id1);
  // determinismMode mapping: exact=0, best_effort=1.
  CHECK(anvil::determinismModeFromConfig(true) == 0);
  CHECK(anvil::determinismModeFromConfig(false) == 1);
}

void testManifestSerialization() {
  anvil::RunManifest m;
  m.runId = "0123456789abcdef";
  m.createdAt = anvil::iso8601Utc(0);
  CHECK_EQ_STR(m.createdAt, "1970-01-01T00:00:00Z");
  m.configCanonicalJson = "{}";
  m.outputs = {{"out_00000001.npy", std::string(64, 'c')}};
  const std::string json = m.toJson();
  // Fixed field order and escaping of the newline-bearing strings.
  CHECK(json.find("\"schema\":\"anvil.run_manifest.v1\"") == 1);  // after '{'
  CHECK(json.find("\"frameRange\":{\"first\":-1") != std::string::npos);
  CHECK(json.find("\"decodeMode\":\"software\"") != std::string::npos);
  CHECK(json.find("\"outputs\":[{\"file\":\"out_00000001.npy\"") != std::string::npos);
  anvil::RunManifest m2 = m;
  m2.configCanonicalJson = "line1\nline2";
  CHECK(m2.toJson().find("line1\\nline2") != std::string::npos);
}

// ---------------------------------------------------------------------------
// WindowAssembler (sliding-window state machine)
// ---------------------------------------------------------------------------

// Synthetic observation: identity only matters to the assembler; planes are
// described by extent + bit depth (the normalized 8-bit 4:2:0 model).
Observation makeObs(uint64_t index, uint32_t gen, bool valid, int w = 64, int h = 48,
                    int bitDepth = 8) {
  Observation o;
  o.id.fileFrameIndex = index;
  o.id.seekGeneration = gen;
  o.id.valid = valid;
  o.planes.width = w;
  o.planes.height = h;
  o.color.bitDepth = bitDepth;
  return o;
}

// Feed a whole synthetic stream and collect the results.
struct FedStream {
  std::vector<FrameWindow> windows;
  std::vector<ResetEvent> resets;
};

FedStream feed(WindowAssembler& a, std::vector<Observation> obsList) {
  FedStream fed;
  for (Observation& o : obsList) {
    const PushResult pr = a.push(std::move(o), 0);
    fed.windows.insert(fed.windows.end(), pr.windows.begin(), pr.windows.end());
    fed.resets.insert(fed.resets.end(), pr.resets.begin(), pr.resets.end());
  }
  return fed;
}

// (a) Warmup/trailing accounting: past=2, future=1 over 10 contiguous frames.
void testWindowWarmupTrailingAccounting() {
  WindowAssembler a(2, 1);
  std::vector<Observation> obsList;
  for (uint64_t i = 0; i < 10; ++i) obsList.push_back(makeObs(i, 0, true));
  const FedStream fed = feed(a, std::move(obsList));
  // Centers 2..8 = 7 emitted windows; frames 0,1 lack past neighbors (warmup),
  // frame 9 lacks a future neighbor (trailing).
  CHECK(fed.windows.size() == 7);
  CHECK(fed.resets.empty());
  a.endOfStream();
  CHECK(a.stats().skippedWarmup == 2);
  CHECK(a.stats().skippedTrailing == 1);
  CHECK(a.stats().skippedDiscontinuity == 0);
  for (size_t k = 0; k < fed.windows.size(); ++k) {
    const FrameWindow& w = fed.windows[k];
    CHECK(w.observations.size() == 4);
    CHECK(w.centerIdx == 2);
    CHECK(w.center().id.fileFrameIndex == 2 + k);
    for (size_t n = 0; n < w.observations.size(); ++n) {
      CHECK(w.observations[n]->id.fileFrameIndex == 2 + k - 2 + n);
    }
  }
  // Offsets are positional under contiguity (-2,-1,0,+1). Observations are
  // SHARED across sliding windows and re-stamped per emission (inherited
  // main.cpp aliasing: stages consume a window immediately, so only the
  // final window's stamps are stable once the whole stream has been fed).
  const FrameWindow& last = fed.windows.back();
  for (size_t n = 0; n < last.observations.size(); ++n) {
    CHECK(last.observations[n]->windowOffset == static_cast<int>(n) - 2);
  }
}

// (b) Invalid frame at index 3 flushes (cause=invalid), then buffering resumes.
void testWindowInvalidFrameReset() {
  WindowAssembler a(1, 0);  // span 2
  std::vector<Observation> obsList;
  for (uint64_t i = 0; i < 6; ++i) obsList.push_back(makeObs(i, 0, i != 3));
  const FedStream fed = feed(a, std::move(obsList));
  a.endOfStream();
  CHECK_EQ_STR(std::string(resetCauseName(fed.resets[0].cause)), "invalid");
  CHECK(fed.resets.size() == 1);
  CHECK(fed.resets[0].frameIndex == 3);
  // Centers 1,2 before the break (past=1 puts the center at buf[1]); center 5
  // after the resume.
  CHECK(fed.windows.size() == 3);
  CHECK(fed.windows[0].center().id.fileFrameIndex == 1);
  CHECK(fed.windows[1].center().id.fileFrameIndex == 2);
  CHECK(fed.windows[2].center().id.fileFrameIndex == 5);
  // Counts add up over the 5 valid frames (0,1,2,4,5): 3 centers + frame 2
  // discarded at the break (discontinuity) + frame 5 left at end (warmup).
  CHECK(a.stats().skippedDiscontinuity == 1);
  CHECK(a.stats().skippedWarmup == 1);
  CHECK(a.stats().skippedTrailing == 0);
}

// (c) Frame-index jump (frame 3 missing) flushes with cause=discontinuity.
void testWindowIndexDiscontinuityReset() {
  WindowAssembler a(1, 0);  // span 2
  std::vector<Observation> obsList;
  for (uint64_t i : {0ull, 1ull, 2ull, 4ull, 5ull, 6ull}) {
    obsList.push_back(makeObs(i, 0, true));
  }
  const FedStream fed = feed(a, std::move(obsList));
  a.endOfStream();
  CHECK(fed.resets.size() == 1);
  CHECK_EQ_STR(std::string(resetCauseName(fed.resets[0].cause)), "discontinuity");
  CHECK(fed.resets[0].frameIndex == 4);
  // Centers 1,2 before the jump; centers 5,6 after the restart.
  CHECK(fed.windows.size() == 4);
  CHECK(fed.windows[0].center().id.fileFrameIndex == 1);
  CHECK(fed.windows[1].center().id.fileFrameIndex == 2);
  CHECK(fed.windows[2].center().id.fileFrameIndex == 5);
  CHECK(fed.windows[3].center().id.fileFrameIndex == 6);
  // Counts add up over the 6 present frames: 4 centers + frame 2 discarded
  // at the jump + frame 6 left at end (warmup).
  CHECK(a.stats().skippedDiscontinuity == 1);
  CHECK(a.stats().skippedWarmup == 1);
  CHECK(a.stats().skippedTrailing == 0);
}

// (d) Seek-generation change flushes with cause=seek; indices may restart.
void testWindowSeekGenerationReset() {
  WindowAssembler a(0, 0);  // span 1: every valid frame is a center
  std::vector<Observation> obsList = {
      makeObs(0, 0, true), makeObs(1, 0, true),
      makeObs(0, 1, true), makeObs(1, 1, true)};  // seek between index 1 and 0
  FedStream fed;
  // feed() passes generation 0; push the generation change by hand.
  for (size_t k = 0; k < obsList.size(); ++k) {
    const uint32_t gen = k < 2 ? 0 : 1;
    const PushResult pr = a.push(std::move(obsList[k]), gen);
    fed.windows.insert(fed.windows.end(), pr.windows.begin(), pr.windows.end());
    fed.resets.insert(fed.resets.end(), pr.resets.begin(), pr.resets.end());
  }
  a.endOfStream();
  CHECK(fed.resets.size() == 1);
  CHECK_EQ_STR(std::string(resetCauseName(fed.resets[0].cause)), "seek");
  CHECK(fed.resets[0].frameIndex == 0);
  CHECK(fed.windows.size() == 4);
  CHECK(fed.windows[2].center().id.fileFrameIndex == 0);  // post-seek restart
  CHECK(a.stats().skippedDiscontinuity == 0);
  CHECK(a.stats().skippedWarmup == 0);
  CHECK(a.stats().skippedTrailing == 0);
  CHECK(a.generation() == 1);
  // The very first push adopts the source generation without an event (the
  // old runner's pre-buffer generation flush was a no-op on an empty buffer).
  WindowAssembler b(0, 0);
  const PushResult pr = b.push(makeObs(5, 7, true), 7);
  CHECK(pr.resets.empty());
  CHECK(pr.windows.size() == 1);
  CHECK(b.generation() == 7);
}

// (e) Mid-stream dimension/format change is a Dimensions reset boundary with
// precise detail (the runner fails the run loudly on this cause).
void testWindowDimensionChangeReset() {
  WindowAssembler a(0, 0);
  std::vector<Observation> obsList = {
      makeObs(0, 0, true, 64, 48), makeObs(1, 0, true, 64, 48),
      makeObs(2, 0, true, 32, 24)};  // mid-stream dimension change
  const FedStream fed = feed(a, std::move(obsList));
  a.endOfStream();
  CHECK(fed.resets.size() == 1);
  CHECK_EQ_STR(std::string(resetCauseName(fed.resets[0].cause)), "dimensions");
  CHECK(fed.resets[0].frameIndex == 2);
  CHECK(fed.resets[0].detail.find("64x48x8") != std::string::npos);
  CHECK(fed.resets[0].detail.find("32x24x8") != std::string::npos);
  CHECK(fed.windows.size() == 2);  // frames 0,1 only; frame 2 never buffered
  CHECK(a.buffered() == 0);
  // Pixel-format class axis (bit depth) participates too.
  WindowAssembler b(0, 0);
  const FedStream fedB = feed(b, {makeObs(0, 0, true, 64, 48, 8),
                                  makeObs(1, 0, true, 64, 48, 10)});
  CHECK(fedB.resets.size() == 1);
  CHECK_EQ_STR(std::string(resetCauseName(fedB.resets[0].cause)), "dimensions");
}

// (f) A clean contiguous run emits no reset events at all.
void testWindowCleanRunNoResets() {
  WindowAssembler a(2, 1);
  std::vector<Observation> obsList;
  for (uint64_t i = 0; i < 10; ++i) obsList.push_back(makeObs(i, 0, true));
  const FedStream fed = feed(a, std::move(obsList));
  a.endOfStream();
  CHECK(fed.resets.empty());
  CHECK(fed.windows.size() == 7);
  CHECK(a.stats().skippedWarmup == 2 && a.stats().skippedTrailing == 1);
  CHECK(a.stats().skippedDiscontinuity == 0);
}

void testWindowResetCauseNames() {
  CHECK_EQ_STR(std::string(resetCauseName(ResetCause::Invalid)), "invalid");
  CHECK_EQ_STR(std::string(resetCauseName(ResetCause::Discontinuity)), "discontinuity");
  CHECK_EQ_STR(std::string(resetCauseName(ResetCause::Seek)), "seek");
  CHECK_EQ_STR(std::string(resetCauseName(ResetCause::Dimensions)), "dimensions");
}

// The manifest's additive "resets" array serializes in fixed position and
// order; a default (clean) manifest carries an empty array.
void testManifestResetsSerialization() {
  anvil::RunManifest m;
  m.resets = {{3, "invalid"}, {7, "dimensions"}};
  const std::string json = m.toJson();
  CHECK(json.find("\"skippedTrailing\":0},\"resets\":[{\"frameIndex\":3,"
                  "\"cause\":\"invalid\"},{\"frameIndex\":7,\"cause\":\"dimensions\"}]"
                  ",\"window\":{") != std::string::npos);
  anvil::RunManifest clean;
  CHECK(clean.toJson().find("\"resets\":[]") != std::string::npos);
}

}  // namespace

int main() {
  testSha256Vectors();
  testSha256StreamingAgreement();
  testSha256File();
  testConfigRoundTrip();
  testConfigWhitespaceAndOrder();
  testConfigMissingFieldsDefault();
  testConfigRejections();
  testReplayKeyStability();
  testManifestSerialization();
  testWindowWarmupTrailingAccounting();
  testWindowInvalidFrameReset();
  testWindowIndexDiscontinuityReset();
  testWindowSeekGenerationReset();
  testWindowDimensionChangeReset();
  testWindowCleanRunNoResets();
  testWindowResetCauseNames();
  testManifestResetsSerialization();
  if (g_failures != 0) {
    std::fprintf(stderr, "anvil_runner_tests: %d check(s) FAILED\n", g_failures);
    return 1;
  }
  std::fprintf(stdout, "anvil_runner_tests: all checks passed\n");
  return 0;
}
