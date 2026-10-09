// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: MemoryTextStore and FileTextStore (CustomizationIo.h).
// Invariants: FileTextStore::save writes "<path>.tmp" completely and renames it over the target, so a
//   reader sees the old or the new file, never half of one; load refuses files over kMaxFileBytes
//   before reading them; setAside never overwrites an existing aside file; nothing throws (all
//   std::filesystem calls use the error_code overloads).
// Callers: CustomizationStorage, hosts, tests.
#include <fstream>
#include <system_error>

#include "r1ui/commands/customize/CustomizationIo.h"

namespace r1ui::commands::customize {

TextLoad MemoryTextStore::load() {
  TextLoad result;
  if (text_) {
    result.exists = true;
    result.text = *text_;
  }
  return result;
}

bool MemoryTextStore::save(std::string_view text, std::string& error) {
  (void)error;
  text_ = std::string(text);
  return true;
}

std::string MemoryTextStore::setAside(std::string& error) {
  (void)error;
  if (!text_) return {};
  asides_.push_back(*text_);
  text_.reset();
  return "memory-aside-" + std::to_string(asides_.size());
}

TextLoad FileTextStore::load() {
  TextLoad result;
  std::error_code ec;
  const bool exists = std::filesystem::exists(path_, ec);
  if (ec) {
    result.error = "cannot access " + path_.string() + ": " + ec.message();
    return result;
  }
  if (!exists) return result;
  const auto size = std::filesystem::file_size(path_, ec);
  if (ec) {
    result.error = "cannot read " + path_.string() + ": " + ec.message();
    return result;
  }
  result.exists = true;
  if (size > kMaxFileBytes) {
    result.error = "customization file is larger than the " + std::to_string(kMaxFileBytes) + " byte limit";
    return result;
  }
  std::ifstream in(path_, std::ios::binary);
  if (!in) {
    result.exists = false;
    result.error = "cannot open " + path_.string();
    return result;
  }
  result.text.assign(static_cast<size_t>(size), '\0');
  in.read(result.text.data(), static_cast<std::streamsize>(size));
  if (!in && !in.eof()) {
    result.text.clear();
    result.exists = false;
    result.error = "read failed: " + path_.string();
    return result;
  }
  result.text.resize(static_cast<size_t>(in.gcount()));
  return result;
}

bool FileTextStore::save(std::string_view text, std::string& error) {
  std::error_code ec;
  if (path_.has_parent_path()) {
    std::filesystem::create_directories(path_.parent_path(), ec);
    if (ec) {
      error = "cannot create " + path_.parent_path().string() + ": " + ec.message();
      return false;
    }
  }
  std::filesystem::path temp = path_;
  temp += ".tmp";
  {
    std::ofstream out(temp, std::ios::binary | std::ios::trunc);
    if (!out) {
      error = "cannot write " + temp.string();
      return false;
    }
    out.write(text.data(), static_cast<std::streamsize>(text.size()));
    out.flush();
    if (!out) {
      error = "write failed: " + temp.string();
      std::filesystem::remove(temp, ec);
      return false;
    }
  }
  std::filesystem::rename(temp, path_, ec);
  if (ec) {
    error = "cannot replace " + path_.string() + ": " + ec.message();
    std::filesystem::remove(temp, ec);
    return false;
  }
  return true;
}

std::string FileTextStore::setAside(std::string& error) {
  std::error_code ec;
  if (!std::filesystem::exists(path_, ec)) return {};
  for (int n = 1; n < 1000; ++n) {
    std::filesystem::path target = path_;
    target += ".corrupt-" + std::to_string(n);
    if (std::filesystem::exists(target, ec)) continue;
    std::filesystem::rename(path_, target, ec);
    if (ec) {
      error = "cannot move " + path_.string() + " aside: " + ec.message();
      return {};
    }
    return target.string();
  }
  error = "too many corrupt copies next to " + path_.string();
  return {};
}

}  // namespace r1ui::commands::customize
