// ANVIL pipeline config: canonical JSON serialization and the matching
// strict parser. Field order and value formatting are fixed so the config
// hash is stable across runs and hosts (replay key component).
#include "anvil/runner/PipelineConfig.hpp"

#include <cctype>
#include <cerrno>
#include <cstdlib>
#include <limits>
#include <sstream>
#include <string>
#include <vector>

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

// ---------------------------------------------------------------------------
// Strict parser for the canonical schema. One JSON value flavor per field:
// string, signed 64-bit integer, unsigned 64-bit integer, or bool.
// ---------------------------------------------------------------------------

struct Parser {
  const std::string& s;
  size_t i = 0;
  std::string err;

  explicit Parser(const std::string& text) : s(text) {}

  bool fail(const std::string& msg) {
    if (err.empty()) {
      err = "parsePipelineConfig: " + msg + " at offset " + std::to_string(i);
    }
    return false;
  }

  void skipWs() {
    while (i < s.size() && (s[i] == ' ' || s[i] == '\t' || s[i] == '\n' || s[i] == '\r')) ++i;
  }

  bool expect(char c) {
    skipWs();
    if (i >= s.size() || s[i] != c) return fail(std::string("expected '") + c + "'");
    ++i;
    return true;
  }

  // Parses a JSON string literal with standard escapes into out.
  bool parseString(std::string& out) {
    skipWs();
    if (i >= s.size() || s[i] != '"') return fail("expected string literal");
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
          // BMP code unit; surrogate pairs are rejected (the canonical
          // schema only ever escapes ", \, and \n, so this is a door for
          // hand-written configs, not a general JSON gateway).
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
          i += 0;  // (already advanced per digit)
          if (code >= 0xD800u && code <= 0xDFFFu) return fail("surrogate \\u escapes unsupported");
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

  // Integer literal: optional '-', digits only; '.' and exponent are
  // rejected so the value type is unambiguous (strict-typing requirement).
  // allowNegative=false parses into the full unsigned 64-bit range (seed).
  bool parseInt(const std::string& key, long long& out, bool allowNegative) {
    skipWs();
    bool neg = false;
    if (i < s.size() && s[i] == '-') {
      if (!allowNegative) return fail("field '" + key + "' expects a non-negative integer");
      neg = true;
      ++i;
    }
    if (i >= s.size() || !std::isdigit(static_cast<unsigned char>(s[i]))) {
      return fail("field '" + key + "' expects an integer");
    }
    unsigned long long mag = 0;
    // Negative magnitudes are capped at 2^63; positive values at INT64_MAX
    // when a signed field is parsed, or the full uint64 range for seed.
    const unsigned long long limit =
        neg ? 9223372036854775808ull
            : (allowNegative ? 9223372036854775807ull : 18446744073709551615ull);
    while (i < s.size() && std::isdigit(static_cast<unsigned char>(s[i]))) {
      const unsigned digit = static_cast<unsigned>(s[i] - '0');
      if (mag > (limit - digit) / 10) {
        return fail("integer out of range in field '" + key + "'");
      }
      mag = mag * 10 + digit;
      ++i;
    }
    if (i < s.size() && (s[i] == '.' || s[i] == 'e' || s[i] == 'E')) {
      return fail("field '" + key + "' expects an integer, got a float");
    }
    out = neg ? -static_cast<long long>(mag) : static_cast<long long>(mag);
    return true;
  }

  bool parseBool(const std::string& key, bool& out) {
    skipWs();
    if (s.compare(i, 4, "true") == 0) {
      i += 4;
      out = true;
      return true;
    }
    if (s.compare(i, 5, "false") == 0) {
      i += 5;
      out = false;
      return true;
    }
    return fail("field '" + key + "' expects true or false");
  }
};

enum class FieldType { Str, I64, U64, Bool };

struct FieldSpec {
  const char* name;
  FieldType type;
};
}  // namespace

bool parsePipelineConfig(const std::string& json, PipelineConfig& out, std::string& err) {
  out = PipelineConfig{};  // defaults for missing fields
  err.clear();
  Parser p(json);
  if (!p.expect('{')) {
    err = p.err;
    return false;
  }
  std::vector<bool> seen;  // parallel to the field table below
  static const FieldSpec kFields[] = {
      {"inputPath", FieldType::Str},         {"firstFrame", FieldType::I64},
      {"lastFrame", FieldType::I64},         {"past", FieldType::I64},
      {"future", FieldType::I64},            {"sideInfoNormalizer", FieldType::Str},
      {"coarse", FieldType::Str},            {"refine", FieldType::Str},
      {"visibility", FieldType::Str},        {"geometry", FieldType::Str},
      {"reconstruct", FieldType::Str},       {"backend", FieldType::Str},
      {"oracleSidecar", FieldType::Str},     {"deterministic", FieldType::Bool},
      {"seed", FieldType::U64},              {"captureDir", FieldType::Str},
      {"captureStages", FieldType::Str},     {"outputDir", FieldType::Str},
      {"transferOverride", FieldType::Str},  {"dropNeighbors", FieldType::Str},
  };
  seen.resize(sizeof(kFields) / sizeof(kFields[0]), false);

  auto index = [](const std::string& key) -> int {
    for (int k = 0; k < static_cast<int>(sizeof(kFields) / sizeof(kFields[0])); ++k) {
      if (key == kFields[k].name) return k;
    }
    return -1;
  };

  p.skipWs();
  if (p.i < p.s.size() && p.s[p.i] == '}') {
    ++p.i;
  } else {
    while (true) {
      std::string key;
      if (!p.parseString(key)) break;
      const int idx = index(key);
      if (idx < 0) {
        p.fail("unknown field '" + key + "'");
        break;
      }
      if (seen[static_cast<size_t>(idx)]) {
        p.fail("duplicate field '" + key + "'");
        break;
      }
      seen[static_cast<size_t>(idx)] = true;
      if (!p.expect(':')) break;
      const bool ok = [&]() -> bool {
        switch (kFields[static_cast<size_t>(idx)].type) {
          case FieldType::Str: {
            std::string v;
            if (!p.parseString(v)) return false;
            switch (idx) {
              case 0: out.inputPath = v; break;
              case 5: out.sideInfoNormalizer = v; break;
              case 6: out.coarse = v; break;
              case 7: out.refine = v; break;
              case 8: out.visibility = v; break;
              case 9: out.geometry = v; break;
              case 10: out.reconstruct = v; break;
              case 11: out.backend = v; break;
              case 12: out.oracleSidecar = v; break;
              case 15: out.captureDir = v; break;
              case 16: out.captureStages = v; break;
              case 17: out.outputDir = v; break;
              case 18: out.transferOverride = v; break;
              case 19: out.dropNeighbors = v; break;
              default: return p.fail("internal field mapping error");
            }
            return true;
          }
          case FieldType::I64: {
            long long v = 0;
            if (!p.parseInt(key, v, true)) return false;
            switch (idx) {
              case 1: out.firstFrame = v; break;
              case 2: out.lastFrame = v; break;
              case 3: out.past = static_cast<int>(v); break;
              case 4: out.future = static_cast<int>(v); break;
              default: return p.fail("internal field mapping error");
            }
            return true;
          }
          case FieldType::U64: {
            long long v = 0;
            if (!p.parseInt(key, v, /*allowNegative=*/false)) return false;
            out.seed = static_cast<uint64_t>(v);
            return true;
          }
          case FieldType::Bool: {
            bool v = false;
            if (!p.parseBool(key, v)) return false;
            out.deterministic = v;
            return true;
          }
        }
        return p.fail("internal field mapping error");
      }();
      if (!ok) break;
      p.skipWs();
      if (p.i < p.s.size() && p.s[p.i] == ',') {
        ++p.i;
        continue;
      }
      if (p.i < p.s.size() && p.s[p.i] == '}') {
        ++p.i;
        break;
      }
      p.fail("expected ',' or '}'");
      break;
    }
  }
  if (p.err.empty()) {
    p.skipWs();
    if (p.i != p.s.size()) p.fail("trailing content after top-level object");
  }
  if (!p.err.empty()) {
    out = PipelineConfig{};
    err = p.err;
    return false;
  }
  return true;
}

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
      << ",\"transferOverride\":" << q(transferOverride)
      << ",\"dropNeighbors\":" << q(dropNeighbors)
      << "}";
  return out.str();
}

}  // namespace anvil
