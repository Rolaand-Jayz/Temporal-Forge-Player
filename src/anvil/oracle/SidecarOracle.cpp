// ANVIL oracle sidecar source implementation (SidecarOracle.hpp contract).
//
// open() parses oracle_index.json with a hand-rolled minimal JSON reader
// scoped EXACTLY to the anvil.oracle.v1 schema: a top-level object holding
// "schema" (string) and "records" (array of FLAT objects whose values are
// strings, numbers, or one flat string/number "meta" object). This is not a
// general JSON parser — no arrays of arrays, no nesting beyond meta, no
// escapes beyond the common ones. Deviating input fails open() loudly
// (missing oracle data is loud, never defaulted — PROJECT DECISION).
//
// Payload geometry contracts (validated per record, mismatch => false +
// lastError):
//   flow:         (gridH*gridW*2,) f32, (dy,dx) pairs per block, block row
//                 gy*gridW+gx; gridStep/gridWidth/gridHeight from record extras
//   visibility:   (H,W) f32 weights; occluded = weight == 0
//   geometry:     (6,) f32 affine (a,b,c,d,e,f)
//   ground_truth: (3,H,W) f32 RGB
#include "anvil/oracle/SidecarOracle.hpp"
#include "anvil/capture/Capture.hpp"  // readNpy + DType (same .npy writer as capture/)

#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

namespace fs = std::filesystem;

namespace anvil {

namespace {

// ---------------------------------------------------------------------------
// Minimal JSON reader, scoped to the sidecar schema.
// ---------------------------------------------------------------------------

// A parsed record: flat scalar fields plus an optional flat meta object.
// unordered_map is fine here: records are looked up by explicit key during
// load, and iteration order never affects output.
struct SidecarRecord {
  std::string kind;
  int64_t targetIndex = 0;
  int64_t windowOffset = 0;
  bool hasWindowOffset = false;
  std::string file;
  // Remaining flat extras (gridStep, gridWidth, gridHeight, ...)
  std::unordered_map<std::string, std::string> extras;
  // Optional flat meta object (ground_truth color metadata).
  std::unordered_map<std::string, std::string> meta;
  bool hasMeta = false;
};

struct SidecarIndex {
  std::string schema;
  std::vector<SidecarRecord> records;
};

class MiniJson {
 public:
  explicit MiniJson(const std::string& text) : s_(text) {}

  bool parse(SidecarIndex& out) {
    skipWs();
    if (!expect('{')) return false;
    if (peek('}')) {
      ++pos_;
      return out.schema == "anvil.oracle.v1";  // empty index is still schema-checked
    }
    for (;;) {
      std::string key;
      if (!parseString(key)) return false;
      if (!expect(':')) return false;
      if (key == "schema") {
        if (!parseString(out.schema)) return false;
      } else if (key == "records") {
        if (!parseRecords(out.records)) return false;
      } else {
        if (!skipValue()) return false;  // tolerate unknown top-level keys
      }
      if (expect(',')) continue;
      if (expect('}')) return true;
      return false;
    }
  }

 private:

  void skipWs() {
    while (pos_ < s_.size() && (s_[pos_] == ' ' || s_[pos_] == '\t' || s_[pos_] == '\n' ||
                                s_[pos_] == '\r')) {
      ++pos_;
    }
  }
  bool expect(char c) {
    skipWs();
    if (pos_ < s_.size() && s_[pos_] == c) {
      ++pos_;
      return true;
    }
    return false;
  }
  bool peek(char c) {
    skipWs();
    return pos_ < s_.size() && s_[pos_] == c;
  }

  bool parseString(std::string& out) {
    if (!expect('"')) return false;
    out.clear();
    while (pos_ < s_.size()) {
      const char c = s_[pos_++];
      if (c == '"') return true;
      if (c == '\\') {
        if (pos_ >= s_.size()) return false;
        const char e = s_[pos_++];
        switch (e) {
          case '"': out.push_back('"'); break;
          case '\\': out.push_back('\\'); break;
          case '/': out.push_back('/'); break;
          case 'n': out.push_back('\n'); break;
          case 't': out.push_back('\t'); break;
          case 'r': out.push_back('\r'); break;
          case 'b': out.push_back('\b'); break;
          case 'f': out.push_back('\f'); break;
          case 'u': {  // keep minimal: read 4 hex digits, encode BMP as UTF-8
            if (pos_ + 4 > s_.size()) return false;
            unsigned cp = 0;
            for (int i = 0; i < 4; ++i) {
              const char h = s_[pos_++];
              cp <<= 4;
              if (h >= '0' && h <= '9') cp |= unsigned(h - '0');
              else if (h >= 'a' && h <= 'f') cp |= unsigned(h - 'a' + 10);
              else if (h >= 'A' && h <= 'F') cp |= unsigned(h - 'A' + 10);
              else return false;
            }
            if (cp < 0x80) {
              out.push_back(char(cp));
            } else if (cp < 0x800) {
              out.push_back(char(0xC0 | (cp >> 6)));
              out.push_back(char(0x80 | (cp & 0x3F)));
            } else {
              out.push_back(char(0xE0 | (cp >> 12)));
              out.push_back(char(0x80 | ((cp >> 6) & 0x3F)));
              out.push_back(char(0x80 | (cp & 0x3F)));
            }
            break;
          }
          default: return false;
        }
      } else {
        out.push_back(c);
      }
    }
    return false;
  }

  bool parseStringValue(const std::string& key, std::unordered_map<std::string, std::string>& m) {
    std::string v;
    if (!parseString(v)) return false;
    m[key] = v;
    return true;
  }

  bool parseNumberValue(const std::string& key, std::unordered_map<std::string, std::string>& m) {
    skipWs();
    const size_t start = pos_;
    if (pos_ < s_.size() && (s_[pos_] == '-' || s_[pos_] == '+')) ++pos_;
    while (pos_ < s_.size() && (std::isdigit(static_cast<unsigned char>(s_[pos_])) ||
                                s_[pos_] == '.' || s_[pos_] == 'e' || s_[pos_] == 'E' ||
                                s_[pos_] == '+' || s_[pos_] == '-')) {
      ++pos_;
    }
    if (pos_ == start) return false;
    m[key] = s_.substr(start, pos_ - start);
    return true;
  }

  // Parses a flat object whose values are strings or numbers; nested objects
  // other than "meta" are rejected (schema-scoped reader).
  bool parseFlatObject(std::unordered_map<std::string, std::string>& scalars,
                       std::unordered_map<std::string, std::string>& meta,
                       bool& hasMeta) {
    if (!expect('{')) return false;
    if (peek('}')) {
      ++pos_;  // skip ws via peek
      return true;
    }
    for (;;) {
      std::string key;
      if (!parseString(key)) return false;
      if (!expect(':')) return false;
      if (key == "meta") {
        if (!expect('{')) return false;
        if (!peek('}')) {
          for (;;) {
            std::string mk;
            if (!parseString(mk)) return false;
            if (!expect(':')) return false;
            if (peek('"')) {
              if (!parseStringValue(mk, meta)) return false;
            } else {
              if (!parseNumberValue(mk, meta)) return false;
            }
            if (expect(',')) continue;
            if (expect('}')) break;
            return false;
          }
        } else {
          ++pos_;
        }
        hasMeta = true;
      } else if (peek('"')) {
        if (!parseStringValue(key, scalars)) return false;
      } else if (peek('{') || peek('[')) {
        return false;  // schema is flat; nested structure = malformed input
      } else {
        if (!parseNumberValue(key, scalars)) return false;
      }
      if (expect(',')) continue;
      if (expect('}')) return true;
      return false;
    }
  }

  bool parseRecords(std::vector<SidecarRecord>& out) {
    if (!expect('[')) return false;
    if (peek(']')) {
      ++pos_;
      return true;
    }
    for (;;) {
      std::unordered_map<std::string, std::string> scalars;
      std::unordered_map<std::string, std::string> meta;
      bool hasMeta = false;
      if (!parseFlatObject(scalars, meta, hasMeta)) return false;
      SidecarRecord rec;
      rec.hasMeta = hasMeta;
      rec.meta = std::move(meta);
      const auto get = [&](const char* k) -> const std::string* {
        const auto it = scalars.find(k);
        return it == scalars.end() ? nullptr : &it->second;
      };
      if (const std::string* v = get("kind")) rec.kind = *v;
      if (const std::string* v = get("targetIndex"))
        rec.targetIndex = std::strtoll(v->c_str(), nullptr, 10);
      if (const std::string* v = get("windowOffset")) {
        rec.windowOffset = std::strtoll(v->c_str(), nullptr, 10);
        rec.hasWindowOffset = true;
      }
      if (const std::string* v = get("file")) rec.file = *v;
      for (const auto& kv : scalars) {
        if (kv.first != "kind" && kv.first != "targetIndex" && kv.first != "windowOffset" &&
            kv.first != "file") {
          rec.extras[kv.first] = kv.second;
        }
      }
      out.push_back(std::move(rec));
      if (expect(',')) continue;
      if (expect(']')) return true;
      return false;
    }
  }

  bool skipValue() {
    // Only used for unknown top-level keys; schema-scoped: accept string,
    // number, or flat object/array by brace matching.
    if (peek('"')) {
      std::string v;
      return parseString(v);
    }
    if (peek('{') || peek('[')) {
      const char open = s_[pos_];
      const char close = open == '{' ? '}' : ']';
      int depth = 0;
      bool inStr = false;
      while (pos_ < s_.size()) {
        const char c = s_[pos_++];
        if (inStr) {
          if (c == '\\') ++pos_;
          else if (c == '"') inStr = false;
        } else if (c == '"') {
          inStr = true;
        } else if (c == open) {
          ++depth;
        } else if (c == close) {
          if (--depth == 0) return true;
        }
      }
      return false;
    }
    return parseNumberValue("__skip", scratch_);
  }

  const std::string& s_;
  size_t pos_ = 0;
  std::unordered_map<std::string, std::string> scratch_;
};

bool readTextFile(const std::string& path, std::string& out) {
  std::ifstream in(path, std::ios::binary);
  if (!in) return false;
  std::ostringstream ss;
  ss << in.rdbuf();
  out = ss.str();
  return true;
}

}  // namespace

// ---------------------------------------------------------------------------
// SidecarOracleSource
// ---------------------------------------------------------------------------

struct SidecarOracleSource::Impl {
  SidecarIndex index;
  std::string error;
  std::string basePath;  // directory containing oracle_index.json

  void fail(const std::string& msg) { error = msg; }

  const SidecarRecord* findRecord(const std::string& kind, int64_t targetIndex,
                                  bool matchOffset, int64_t windowOffset) const {
    for (const SidecarRecord& rec : index.records) {
      if (rec.kind != kind || rec.targetIndex != targetIndex) continue;
      if (matchOffset) {
        // Records without an explicit windowOffset default to 0 (schema:
        // geometry/ground_truth records are per-target).
        const int64_t recOffset = rec.hasWindowOffset ? rec.windowOffset : 0;
        if (recOffset != windowOffset) continue;
      }
      return &rec;
    }
    return nullptr;
  }
};

SidecarOracleSource::SidecarOracleSource() : impl_(new Impl()) {}
SidecarOracleSource::~SidecarOracleSource() { delete impl_; }

const std::string& SidecarOracleSource::lastError() const { return impl_->error; }

bool SidecarOracleSource::open(const std::string& sidecarPath) {
  impl_->error.clear();
  impl_->index.records.clear();
  std::string text;
  if (!readTextFile(sidecarPath, text)) {
    impl_->fail("oracle sidecar: cannot read index file '" + sidecarPath + "'");
    return false;
  }
  MiniJson parser(text);
  if (!parser.parse(impl_->index)) {
    impl_->fail("oracle sidecar: malformed oracle_index.json '" + sidecarPath + "'");
    return false;
  }
  if (impl_->index.schema != "anvil.oracle.v1") {
    impl_->fail("oracle sidecar: unsupported schema '" + impl_->index.schema +
                "' (expected anvil.oracle.v1) in '" + sidecarPath + "'");
    return false;
  }
  impl_->basePath = fs::path(sidecarPath).parent_path().string();
  return true;
}

bool SidecarOracleSource::loadFlow(int64_t targetIndex, int windowOffset, MotionField& out) {
  impl_->error.clear();
  const SidecarRecord* rec =
      impl_->findRecord("flow", targetIndex, true, int64_t(windowOffset));
  if (!rec) {
    impl_->fail("oracle flow: no record for target=" + std::to_string(targetIndex) +
                " windowOffset=" + std::to_string(windowOffset));
    return false;
  }
  int64_t gridStep = 8, gridW = 0, gridH = 0;
  const auto extra = [&](const char* k, int64_t& v) -> bool {
    const auto it = rec->extras.find(k);
    if (it == rec->extras.end()) return false;
    v = std::strtoll(it->second.c_str(), nullptr, 10);
    return true;
  };
  if (!extra("gridStep", gridStep) || !extra("gridWidth", gridW) ||
      !extra("gridHeight", gridH) || gridW <= 0 || gridH <= 0 || gridStep <= 0) {
    impl_->fail("oracle flow: record target=" + std::to_string(targetIndex) +
                " windowOffset=" + std::to_string(windowOffset) +
                " is missing valid gridStep/gridWidth/gridHeight extras");
    return false;
  }
  std::vector<int64_t> shape;
  DType dtype = DType::F32;
  std::vector<float> data;
  std::vector<uint8_t> dataU8;
  const std::string path = (fs::path(impl_->basePath) / rec->file).string();
  if (!readNpy(path, shape, dtype, data, dataU8)) {
    impl_->fail("oracle flow: cannot read payload '" + path + "' for target=" +
                std::to_string(targetIndex) + " windowOffset=" +
                std::to_string(windowOffset));
    return false;
  }
  if (dtype != DType::F32 || shape.size() != 1 ||
      shape[0] != int64_t(gridW * gridH * 2)) {
    std::ostringstream want;
    want << "(" << gridH * gridW * 2 << ",) f32";
    impl_->fail("oracle flow: payload geometry mismatch for target=" +
                std::to_string(targetIndex) + " windowOffset=" +
                std::to_string(windowOffset) + " file='" + path + "' expected " +
                want.str());
    return false;
  }
  out = MotionField{};
  out.src = MotionField::Src::Oracle;
  out.gridStep = int(gridStep);
  out.gridWidth = int(gridW);
  out.gridHeight = int(gridH);
  out.blocks.resize(size_t(gridW * gridH));
  // Payload layout: (dy, dx) pairs per block, block index gy*gridW+gx.
  for (int64_t gy = 0; gy < gridH; ++gy) {
    for (int64_t gx = 0; gx < gridW; ++gx) {
      const size_t i = size_t(gy * gridW + gx);
      BlockVec& b = out.blocks[i];
      b.gx = int(gx);
      b.gy = int(gy);
      b.mv.y = data[i * 2 + 0];
      b.mv.x = data[i * 2 + 1];
      b.quality = 1.0f;  // oracle data: exact by definition
    }
  }
  return true;
}

bool SidecarOracleSource::loadVisibility(int64_t targetIndex, int windowOffset,
                                         Visibility& out) {
  impl_->error.clear();
  const SidecarRecord* rec =
      impl_->findRecord("visibility", targetIndex, true, int64_t(windowOffset));
  if (!rec) {
    impl_->fail("oracle visibility: no record for target=" + std::to_string(targetIndex) +
                " windowOffset=" + std::to_string(windowOffset));
    return false;
  }
  std::vector<int64_t> shape;
  DType dtype = DType::F32;
  std::vector<float> data;
  std::vector<uint8_t> dataU8;
  const std::string path = (fs::path(impl_->basePath) / rec->file).string();
  if (!readNpy(path, shape, dtype, data, dataU8)) {
    impl_->fail("oracle visibility: cannot read payload '" + path + "' for target=" +
                std::to_string(targetIndex) + " windowOffset=" +
                std::to_string(windowOffset));
    return false;
  }
  if (dtype != DType::F32 || shape.size() != 2) {
    impl_->fail("oracle visibility: payload geometry mismatch for target=" +
                std::to_string(targetIndex) + " windowOffset=" +
                std::to_string(windowOffset) + " file='" + path +
                "' expected (H,W) f32");
    return false;
  }
  out = Visibility{};
  out.src = Visibility::Src::Oracle;
  out.width = int(shape[1]);
  out.height = int(shape[0]);
  out.weight = std::move(data);
  out.occluded.resize(out.weight.size());
  for (size_t i = 0; i < out.weight.size(); ++i) {
    out.occluded[i] = out.weight[i] == 0.0f ? uint8_t(1) : uint8_t(0);
  }
  return true;
}

bool SidecarOracleSource::loadGeometry(int64_t targetIndex, SampleGeometry& out) {
  impl_->error.clear();
  const SidecarRecord* rec = impl_->findRecord("geometry", targetIndex, false, 0);
  if (!rec) {
    impl_->fail("oracle geometry: no record for target=" + std::to_string(targetIndex));
    return false;
  }
  std::vector<int64_t> shape;
  DType dtype = DType::F32;
  std::vector<float> data;
  std::vector<uint8_t> dataU8;
  const std::string path = (fs::path(impl_->basePath) / rec->file).string();
  if (!readNpy(path, shape, dtype, data, dataU8)) {
    impl_->fail("oracle geometry: cannot read payload '" + path + "' for target=" +
                std::to_string(targetIndex));
    return false;
  }
  if (dtype != DType::F32 || shape.size() != 1 || shape[0] != 6) {
    impl_->fail("oracle geometry: payload geometry mismatch for target=" +
                std::to_string(targetIndex) + " file='" + path +
                "' expected (6,) f32 affine (a,b,c,d,e,f)");
    return false;
  }
  out = SampleGeometry{};
  out.status = SampleGeometry::Status::Known;
  out.globalXform = Mat3f{data[0], data[1], data[2], data[3], data[4], data[5]};
  return true;
}

bool SidecarOracleSource::loadGroundTruth(int64_t targetIndex, SourcePlanes& out,
                                          ColorMeta& meta) {
  impl_->error.clear();
  const SidecarRecord* rec = impl_->findRecord("ground_truth", targetIndex, false, 0);
  if (!rec) {
    impl_->fail("oracle ground_truth: no record for target=" +
                std::to_string(targetIndex));
    return false;
  }
  std::vector<int64_t> shape;
  DType dtype = DType::F32;
  std::vector<float> data;
  std::vector<uint8_t> dataU8;
  const std::string path = (fs::path(impl_->basePath) / rec->file).string();
  if (!readNpy(path, shape, dtype, data, dataU8)) {
    impl_->fail("oracle ground_truth: cannot read payload '" + path + "' for target=" +
                std::to_string(targetIndex));
    return false;
  }
  if (dtype != DType::F32 || shape.size() != 3 || shape[0] != 3) {
    impl_->fail("oracle ground_truth: payload geometry mismatch for target=" +
                std::to_string(targetIndex) + " file='" + path +
                "' expected (3,H,W) f32 RGB");
    return false;
  }
  const int64_t h = shape[1];
  const int64_t w = shape[2];
  // The pinned OracleDataSource contract carries ground truth as SourcePlanes.
  // SourcePlanes is being reused here as a generic 3-plane 8-bit carrier: the
  // (3,H,W) f32 RGB channels map 1:1 to y/u/v at FULL resolution (not I420
  // chroma subsampling — no conversion is invented). Documented deviation;
  // the runner owns any color-space interpretation via ColorMeta.
  out = SourcePlanes{};
  out.width = int(w);
  out.height = int(h);
  const size_t n = size_t(w * h);
  out.y.resize(n);
  out.u.resize(n);
  out.v.resize(n);
  for (size_t i = 0; i < n; ++i) {
    const auto toU8 = [](float v) {
      return uint8_t(std::lround(std::min(255.0f, std::max(0.0f, v))));
    };
    out.y[i] = toU8(data[i]);
    out.u[i] = toU8(data[n + i]);
    out.v[i] = toU8(data[2 * n + i]);
  }
  meta = ColorMeta{};
  if (rec->hasMeta) {
    const auto get = [&](const char* k) -> const std::string* {
      const auto it = rec->meta.find(k);
      return it == rec->meta.end() ? nullptr : &it->second;
    };
    if (const std::string* v = get("matrix")) meta.matrixCoefficients = atoi(v->c_str());
    if (const std::string* v = get("primaries")) meta.primaries = atoi(v->c_str());
    if (const std::string* v = get("transfer")) meta.transfer = atoi(v->c_str());
    if (const std::string* v = get("range")) meta.range = ColorMeta::Range(atoi(v->c_str()));
    if (const std::string* v = get("chromaLocation"))
      meta.chromaLocation = atoi(v->c_str());
    if (const std::string* v = get("bitDepth")) meta.bitDepth = atoi(v->c_str());
  }
  return true;
}

}  // namespace anvil
