// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: readTextFile and writeTextFileAtomic (TextFile.h).
// Invariants: a file over the limit is never read; the target is replaced only after the temporary file
//   was written and flushed completely; the temporary file never survives a failure.
// Callers: CustomMenuIo, workspace files and folder, tests.
#include "r1ui/commands/custommenu/TextFile.h"

#include <fstream>
#include <system_error>

namespace r1ui::commands::custommenu {

bool readTextFile(const std::filesystem::path& path, size_t maxBytes, std::string& text, std::string& error) {
  text.clear();
  std::error_code ec;
  const bool isFile = std::filesystem::is_regular_file(path, ec);
  if (ec || !isFile) {
    error = "cannot find the file " + path.string();
    return false;
  }
  const auto size = std::filesystem::file_size(path, ec);
  if (ec) {
    error = "cannot read " + path.string() + ": " + ec.message();
    return false;
  }
  if (size > maxBytes) {
    error = "the file is larger than the " + std::to_string(maxBytes) + " byte limit";
    return false;
  }
  std::ifstream in(path, std::ios::binary);
  if (!in) {
    error = "cannot open " + path.string();
    return false;
  }
  text.assign(static_cast<size_t>(size), '\0');
  in.read(text.data(), static_cast<std::streamsize>(size));
  if (!in && !in.eof()) {
    text.clear();
    error = "reading failed: " + path.string();
    return false;
  }
  text.resize(static_cast<size_t>(in.gcount()));
  return true;
}

bool writeTextFileAtomic(const std::filesystem::path& path, std::string_view text, std::string& error) {
  std::error_code ec;
  if (path.has_parent_path()) {
    std::filesystem::create_directories(path.parent_path(), ec);
    if (ec) {
      error = "cannot create " + path.parent_path().string() + ": " + ec.message();
      return false;
    }
  }
  std::filesystem::path temp = path;
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
      out.close();
      std::filesystem::remove(temp, ec);
      error = "writing failed: " + temp.string();
      return false;
    }
  }
  std::filesystem::rename(temp, path, ec);
  if (ec) {
    const std::string why = ec.message();
    std::filesystem::remove(temp, ec);
    error = "cannot replace " + path.string() + ": " + why;
    return false;
  }
  return true;
}

}  // namespace r1ui::commands::custommenu
