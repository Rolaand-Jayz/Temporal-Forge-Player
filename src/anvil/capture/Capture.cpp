// ANVIL capture implementation: minimal .npy v1.0 writer/reader and the disk
// capture sink. The sink writes synchronously for now; the ring/writer-thread
// upgrade lands with the substrate wave (docs/anvil/RECONCILIATION_20261005.md 3.7).
#include "anvil/capture/Capture.hpp"

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <sstream>

namespace fs = std::filesystem;

namespace anvil {

namespace {

// .npy header: magic + version + header len + python-literal dict. We emit
// minimal v1.0 headers and parse only the fields we wrote.
constexpr char kNpyMagic[] = "\x93NUMPY";

std::string dtypeStr(DType dtype) {
  return dtype == DType::F32 ? "<f4" : "|u1";
}

bool parseHeader(const std::string& header, std::vector<int64_t>& shape, DType& dtype) {
  const auto descr = header.find("'descr'");
  if (descr == std::string::npos) return false;
  const auto q1 = header.find('\'', header.find(':', descr) + 1);
  const auto q2 = header.find('\'', q1 + 1);
  if (q1 == std::string::npos || q2 == std::string::npos) return false;
  const std::string d = header.substr(q1 + 1, q2 - q1 - 1);
  if (d == "<f4" || d == "f4") dtype = DType::F32;
  else if (d == "|u1" || d == "u1") dtype = DType::U8;
  else return false;

  shape.clear();
  const auto shapePos = header.find("'shape'");
  if (shapePos == std::string::npos) return false;
  const auto open = header.find('(', shapePos);
  const auto close = header.find(')', open);
  if (open == std::string::npos || close == std::string::npos) return false;
  std::stringstream ss(header.substr(open + 1, close - open - 1));
  std::string item;
  while (std::getline(ss, item, ',')) {
    if (item.empty() || item == " ") continue;
    shape.push_back(std::strtoll(item.c_str(), nullptr, 10));
  }
  return true;
}

}  // namespace

bool writeNpy(const std::string& path, const std::vector<int64_t>& shape, DType dtype,
              const void* data) {
  std::string shapeStr = "(";
  for (size_t i = 0; i < shape.size(); ++i) {
    shapeStr += std::to_string(shape[i]);
    if (i + 1 < shape.size()) shapeStr += ", ";
  }
  if (shape.size() == 1) shapeStr += ",";
  shapeStr += ")";

  std::string dict = "{'descr': '" + dtypeStr(dtype) + "', 'fortran_order': False, 'shape': " +
                     shapeStr + ", }";
  // Header must be padded so total len (magic 6 + 2 + 2 + dict) is a multiple of 64.
  size_t base = 6 + 2 + 2 + dict.size() + 1;  // +1 for trailing newline
  size_t pad = (64 - (base % 64)) % 64;
  dict.append(pad, ' ');
  dict.push_back('\n');

  std::ofstream out(path, std::ios::binary);
  if (!out) return false;
  out.write(kNpyMagic, 6);
  out.put(static_cast<char>(1));
  out.put(static_cast<char>(0));
  const uint16_t headerLen = static_cast<uint16_t>(dict.size());
  out.put(static_cast<char>(headerLen & 0xFF));
  out.put(static_cast<char>((headerLen >> 8) & 0xFF));
  out.write(dict.data(), static_cast<std::streamsize>(dict.size()));

  size_t count = 1;
  for (int64_t s : shape) count *= static_cast<size_t>(s);
  const size_t elem = dtype == DType::F32 ? 4 : 1;
  out.write(static_cast<const char*>(data), static_cast<std::streamsize>(count * elem));
  return static_cast<bool>(out);
}

bool readNpy(const std::string& path, std::vector<int64_t>& shape, DType& dtype,
             std::vector<float>& outF32, std::vector<uint8_t>& outU8) {
  std::ifstream in(path, std::ios::binary);
  if (!in) return false;
  char magic[6] = {};
  in.read(magic, 6);
  if (std::string_view(magic, 6) != std::string_view(kNpyMagic, 6)) return false;
  char major = 0, minor = 0;
  in.get(major);
  in.get(minor);
  if (major != 1) return false;
  char lenBuf[2] = {};
  in.read(lenBuf, 2);
  const size_t headerLen = static_cast<uint8_t>(lenBuf[0]) |
                           (static_cast<uint8_t>(lenBuf[1]) << 8);
  std::string header(headerLen, ' ');
  in.read(header.data(), static_cast<std::streamsize>(headerLen));
  if (!parseHeader(header, shape, dtype)) return false;

  size_t count = 1;
  for (int64_t s : shape) count *= static_cast<size_t>(s);
  if (dtype == DType::F32) {
    outF32.resize(count);
    in.read(reinterpret_cast<char*>(outF32.data()),
            static_cast<std::streamsize>(count * 4));
    return static_cast<bool>(in);
  }
  outU8.resize(count);
  in.read(reinterpret_cast<char*>(outU8.data()), static_cast<std::streamsize>(count));
  return static_cast<bool>(in);
}

struct DiskCaptureSink::Impl {
  std::string root;
  size_t ring = 0;  // reserved for the writer-thread upgrade
  std::mutex mu;
  std::vector<std::string> index;  // record paths, for capture_index.json
};

DiskCaptureSink::DiskCaptureSink(std::string rootDir, size_t ringCapacity)
    : impl_(new Impl{std::move(rootDir), ringCapacity, {}, {}}) {}

DiskCaptureSink::~DiskCaptureSink() {
  flush();
  delete impl_;
}

void DiskCaptureSink::capture(const CapturedTensor& tensor) {
  std::lock_guard<std::mutex> lock(impl_->mu);
  std::error_code ec;
  const fs::path stageDir = fs::path(impl_->root) / tensor.stageId;
  fs::create_directories(stageDir, ec);
  std::ostringstream name;
  name << tensor.fileFrameIndex << "-" << tensor.name
       << (tensor.dtype == DType::F32 ? ".f32.npy" : ".u8.npy");
  const fs::path file = stageDir / name.str();
  const void* data = tensor.dtype == DType::F32
                         ? static_cast<const void*>(tensor.f32.data())
                         : static_cast<const void*>(tensor.u8.data());
  if (writeNpy(file.string(), tensor.shape, tensor.dtype, data)) {
    impl_->index.push_back(file.string());
  }
}

void DiskCaptureSink::flush() {
  if (!impl_ || impl_->index.empty()) return;
  std::lock_guard<std::mutex> lock(impl_->mu);
  std::ofstream out(fs::path(impl_->root) / "capture_index.json");
  out << "{\"records\": [\n";
  for (size_t i = 0; i < impl_->index.size(); ++i) {
    out << "  \"" << impl_->index[i] << "\"" << (i + 1 < impl_->index.size() ? "," : "")
        << "\n";
  }
  out << "]}\n";
  impl_->index.clear();
}

}  // namespace anvil
