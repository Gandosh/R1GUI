// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: key validation, the steady clock, the memory and file layout stores and the bounded file
//   helpers (see LayoutStore.h for the contract).
// Invariants: every key is validated before it touches a path, so a store never leaves its root;
//   all file operations use error codes (nothing throws); writes go through a temporary file and a
//   replace so a crash or a full disk leaves the previous layout readable.
#include "r1ui/dock/LayoutStore.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <fstream>
#include <iterator>
#include <system_error>

namespace r1ui::dock {

namespace {

constexpr std::string_view kSuffix = ".layout.json";

bool keyChar(unsigned char c) { return std::isalnum(c) != 0 || c == ' ' || c == '_' || c == '-' || c == '.' || c == '(' || c == ')'; }

bool isDeviceName(std::string_view key) {
  std::string base(key.substr(0, key.find('.')));
  while (!base.empty() && base.back() == ' ') base.pop_back();
  std::transform(base.begin(), base.end(), base.begin(), [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
  if (base == "CON" || base == "PRN" || base == "AUX" || base == "NUL") return true;
  return base.size() == 4 && (base.rfind("COM", 0) == 0 || base.rfind("LPT", 0) == 0) && base[3] >= '1' && base[3] <= '9';
}

std::filesystem::path blobPath(const std::filesystem::path& root, std::string_view scope, std::string_view key) {
  return root / std::string(scope) / (std::string(key) + std::string(kSuffix));
}

}  // namespace

uint64_t SteadyClock::nowMs() const {
  return static_cast<uint64_t>(
      std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
}

std::string keyProblem(std::string_view key) {
  if (key.empty()) return "the name is empty";
  if (key.size() > kMaxKeyBytes) return "the name is longer than " + std::to_string(kMaxKeyBytes) + " characters";
  for (unsigned char c : key) {
    if (!keyChar(c)) return "the name contains a character that cannot be used in a file name";
  }
  if (key.front() == ' ' || key.front() == '.' || key.back() == ' ' || key.back() == '.') {
    return "the name cannot start or end with a space or a dot";
  }
  if (key.find("..") != std::string_view::npos) return "the name cannot contain \"..\"";
  if (isDeviceName(key)) return "the name is reserved by the operating system";
  return {};
}

std::string keyFromName(std::string_view name) {
  std::string key;
  bool lastUnderscore = false;
  for (unsigned char c : name) {
    const bool ok = keyChar(c);
    if (!ok) {
      if (!lastUnderscore) key.push_back('_');
      lastUnderscore = true;
      continue;
    }
    lastUnderscore = false;
    key.push_back(static_cast<char>(c));
  }
  while (key.find("..") != std::string::npos) key.replace(key.find(".."), 2, ".");
  const auto trim = [&]() {
    while (!key.empty() && (key.front() == ' ' || key.front() == '.' || key.front() == '_')) key.erase(key.begin());
    while (!key.empty() && (key.back() == ' ' || key.back() == '.' || key.back() == '_')) key.pop_back();
  };
  trim();  // leading '_' is reserved for internal keys
  if (key.size() > kMaxKeyBytes) key.resize(kMaxKeyBytes);
  trim();
  if (!keyProblem(key).empty()) return {};
  return key;
}

// ---- files ---------------------------------------------------------------------------------

std::optional<std::string> readBoundedFile(const std::filesystem::path& path) {
  std::error_code ec;
  const auto size = std::filesystem::file_size(path, ec);
  if (ec || size > kMaxLayoutBytes) return std::nullopt;
  std::ifstream in(path, std::ios::binary);
  if (!in) return std::nullopt;
  std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  if (text.size() > kMaxLayoutBytes) return std::nullopt;
  return text;
}

Status writeFileAtomic(const std::filesystem::path& path, std::string_view text) {
  if (text.size() > kMaxLayoutBytes) return Status::failure("layout is larger than the storage limit");
  std::error_code ec;
  std::filesystem::create_directories(path.parent_path(), ec);
  if (ec) return Status::failure("cannot create the layout folder: " + ec.message());
  std::filesystem::path temp = path;
  temp += ".tmp";
  {
    std::ofstream out(temp, std::ios::binary | std::ios::trunc);
    if (!out) return Status::failure("cannot write the layout file");
    out.write(text.data(), static_cast<std::streamsize>(text.size()));
    out.flush();
    if (!out) {
      out.close();
      std::filesystem::remove(temp, ec);
      return Status::failure("cannot write the layout file (disk full?)");
    }
  }
  std::filesystem::rename(temp, path, ec);
  if (ec) {
    std::error_code ignored;
    std::filesystem::remove(temp, ignored);
    return Status::failure("cannot replace the layout file: " + ec.message());
  }
  return Status::success();
}

// ---- memory store ---------------------------------------------------------------------------------

std::vector<std::string> MemoryLayoutStore::list(std::string_view scope) const {
  std::vector<std::string> keys;
  const auto it = data_.find(std::string(scope));
  if (it == data_.end()) return keys;
  for (const auto& entry : it->second) {
    if (keys.size() >= kMaxStoredLayouts) break;
    keys.push_back(entry.first);
  }
  return keys;
}

std::optional<std::string> MemoryLayoutStore::read(std::string_view scope, std::string_view key) const {
  const auto it = data_.find(std::string(scope));
  if (it == data_.end()) return std::nullopt;
  const auto blob = it->second.find(std::string(key));
  if (blob == it->second.end()) return std::nullopt;
  return blob->second;
}

Status MemoryLayoutStore::write(std::string_view scope, std::string_view key, std::string_view text) {
  if (std::string p = keyProblem(scope); !p.empty()) return Status::failure("scope: " + p);
  if (std::string p = keyProblem(key); !p.empty()) return Status::failure(p);
  if (failWrites_) return Status::failure("the store is read-only");
  if (text.size() > kMaxLayoutBytes) return Status::failure("layout is larger than the storage limit");
  auto& scoped = data_[std::string(scope)];
  if (scoped.size() >= kMaxStoredLayouts && scoped.count(std::string(key)) == 0) return Status::failure("too many stored layouts");
  scoped[std::string(key)] = std::string(text);
  ++writes_;
  return Status::success();
}

Status MemoryLayoutStore::remove(std::string_view scope, std::string_view key) {
  if (failWrites_) return Status::failure("the store is read-only");
  const auto it = data_.find(std::string(scope));
  if (it == data_.end() || it->second.erase(std::string(key)) == 0) return Status::failure("no such layout");
  return Status::success();
}

Status MemoryLayoutStore::rename(std::string_view scope, std::string_view from, std::string_view to) {
  if (std::string p = keyProblem(to); !p.empty()) return Status::failure(p);
  if (failWrites_) return Status::failure("the store is read-only");
  const auto it = data_.find(std::string(scope));
  if (it == data_.end() || it->second.count(std::string(from)) == 0) return Status::failure("no such layout");
  if (it->second.count(std::string(to)) != 0) return Status::failure("a layout with that name already exists");
  it->second[std::string(to)] = std::move(it->second[std::string(from)]);
  it->second.erase(std::string(from));
  return Status::success();
}

bool MemoryLayoutStore::exists(std::string_view scope, std::string_view key) const { return read(scope, key).has_value(); }

// ---- file store ------------------------------------------------------------------------------------

std::vector<std::string> FileLayoutStore::list(std::string_view scope) const {
  std::vector<std::string> keys;
  if (!keyProblem(scope).empty()) return keys;
  std::error_code ec;
  std::filesystem::directory_iterator it(root_ / std::string(scope), ec);
  if (ec) return keys;
  for (const auto& entry : it) {
    std::error_code fileEc;
    if (!entry.is_regular_file(fileEc)) continue;
    const std::string name = entry.path().filename().string();
    if (name.size() <= kSuffix.size() || name.compare(name.size() - kSuffix.size(), kSuffix.size(), kSuffix) != 0) continue;
    const std::string key = name.substr(0, name.size() - kSuffix.size());
    if (keyProblem(key).empty()) keys.push_back(key);
  }
  std::sort(keys.begin(), keys.end());
  if (keys.size() > kMaxStoredLayouts) keys.resize(kMaxStoredLayouts);
  return keys;
}

std::optional<std::string> FileLayoutStore::read(std::string_view scope, std::string_view key) const {
  if (!keyProblem(scope).empty() || !keyProblem(key).empty()) return std::nullopt;
  return readBoundedFile(blobPath(root_, scope, key));
}

Status FileLayoutStore::write(std::string_view scope, std::string_view key, std::string_view text) {
  if (std::string p = keyProblem(scope); !p.empty()) return Status::failure("scope: " + p);
  if (std::string p = keyProblem(key); !p.empty()) return Status::failure(p);
  return writeFileAtomic(blobPath(root_, scope, key), text);
}

Status FileLayoutStore::remove(std::string_view scope, std::string_view key) {
  if (!keyProblem(scope).empty() || !keyProblem(key).empty()) return Status::failure("invalid layout name");
  std::error_code ec;
  if (!std::filesystem::remove(blobPath(root_, scope, key), ec) || ec) return Status::failure("no such layout");
  return Status::success();
}

Status FileLayoutStore::rename(std::string_view scope, std::string_view from, std::string_view to) {
  if (!keyProblem(scope).empty() || !keyProblem(from).empty()) return Status::failure("invalid layout name");
  if (std::string p = keyProblem(to); !p.empty()) return Status::failure(p);
  const std::filesystem::path source = blobPath(root_, scope, from);
  const std::filesystem::path target = blobPath(root_, scope, to);
  std::error_code ec;
  if (!std::filesystem::exists(source, ec)) return Status::failure("no such layout");
  if (std::filesystem::exists(target, ec)) return Status::failure("a layout with that name already exists");
  std::filesystem::rename(source, target, ec);
  return ec ? Status::failure("cannot rename the layout file: " + ec.message()) : Status::success();
}

bool FileLayoutStore::exists(std::string_view scope, std::string_view key) const {
  if (!keyProblem(scope).empty() || !keyProblem(key).empty()) return false;
  std::error_code ec;
  return std::filesystem::is_regular_file(blobPath(root_, scope, key), ec);
}

std::optional<std::filesystem::path> FileLayoutStore::pathOf(std::string_view scope, std::string_view key) const {
  if (!keyProblem(scope).empty() || !keyProblem(key).empty()) return std::nullopt;
  return blobPath(root_, scope, key);
}

}  // namespace r1ui::dock
