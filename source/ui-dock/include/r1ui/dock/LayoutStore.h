// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: the storage abstraction of the layout manager: ILayoutStore (named text blobs grouped by a
//   scope, which is the application mode), the clock interface, the in-memory store used by tests
//   and tools, the file store (one directory per scope, one "<key>.layout.json" per layout, atomic
//   replace on write) and the key validation every store applies.
// Why: spec 04 wants layouts per editor instance, user-visible lists and safe writes; keeping the
//   disk behind an interface lets the manager be tested headless with injected failures and a fake
//   clock, and lets an application keep layouts somewhere else (a project folder, a database).
// Callers: LayoutManager, tests, the preview. Calls: std::filesystem (FileLayoutStore only).
// Keys: a scope or key is 1..64 printable ASCII characters (letters, digits, space, "_-.()"), does
//   not start or end with a space or dot, has no "..", and is not a Windows device name, so it can
//   be a file name and can never escape the store's directory. Keys starting with '_' are internal
//   (the auto-saved active layout, layouts kept aside); the manager hides them from user lists.
// Failure behavior: no method throws for bad input or I/O trouble; failures are returned. Reads are
//   bounded (kMaxLayoutBytes) and a missing, oversized or unreadable blob reads as nullopt.
// Threading: UI thread only (no internal locking); I/O is synchronous and small.
#pragma once

#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "r1ui/dock/DockTypes.h"

namespace r1ui::dock {

inline constexpr size_t kMaxLayoutBytes = size_t{4} * 1024 * 1024;
inline constexpr size_t kMaxStoredLayouts = 1000;
inline constexpr size_t kMaxKeyBytes = 64;

// Monotonic milliseconds. Injected so tests control time.
class IClock {
 public:
  virtual ~IClock() = default;
  virtual uint64_t nowMs() const = 0;
};

class SteadyClock final : public IClock {
 public:
  uint64_t nowMs() const override;
};

// Empty string when `key` is a legal scope or layout key, else the reason it is not.
std::string keyProblem(std::string_view key);
// Derives a legal key from a free-form display name (illegal characters become '_'); empty when
// nothing usable is left (the name is empty or only punctuation).
std::string keyFromName(std::string_view name);

class ILayoutStore {
 public:
  virtual ~ILayoutStore() = default;
  // Keys in `scope`, sorted, at most kMaxStoredLayouts.
  virtual std::vector<std::string> list(std::string_view scope) const = 0;
  // The blob, or nullopt when it does not exist, is unreadable or exceeds kMaxLayoutBytes.
  virtual std::optional<std::string> read(std::string_view scope, std::string_view key) const = 0;
  // Creates or replaces atomically: a failed write leaves the old blob intact.
  virtual Status write(std::string_view scope, std::string_view key, std::string_view text) = 0;
  virtual Status remove(std::string_view scope, std::string_view key) = 0;
  // Fails when `from` is missing or `to` exists.
  virtual Status rename(std::string_view scope, std::string_view from, std::string_view to) = 0;
  virtual bool exists(std::string_view scope, std::string_view key) const = 0;
  // The file behind a blob, when there is one (used to refuse importing a layout onto itself).
  virtual std::optional<std::filesystem::path> pathOf(std::string_view scope, std::string_view key) const {
    (void)scope;
    (void)key;
    return std::nullopt;
  }
};

class MemoryLayoutStore final : public ILayoutStore {
 public:
  std::vector<std::string> list(std::string_view scope) const override;
  std::optional<std::string> read(std::string_view scope, std::string_view key) const override;
  Status write(std::string_view scope, std::string_view key, std::string_view text) override;
  Status remove(std::string_view scope, std::string_view key) override;
  Status rename(std::string_view scope, std::string_view from, std::string_view to) override;
  bool exists(std::string_view scope, std::string_view key) const override;

  // Test hooks: make every write or remove fail, count successful writes.
  void setFailWrites(bool fail) { failWrites_ = fail; }
  size_t writeCount() const { return writes_; }

 private:
  std::map<std::string, std::map<std::string, std::string>> data_;
  bool failWrites_ = false;
  size_t writes_ = 0;
};

class FileLayoutStore final : public ILayoutStore {
 public:
  explicit FileLayoutStore(std::filesystem::path root) : root_(std::move(root)) {}
  std::vector<std::string> list(std::string_view scope) const override;
  std::optional<std::string> read(std::string_view scope, std::string_view key) const override;
  Status write(std::string_view scope, std::string_view key, std::string_view text) override;
  Status remove(std::string_view scope, std::string_view key) override;
  Status rename(std::string_view scope, std::string_view from, std::string_view to) override;
  bool exists(std::string_view scope, std::string_view key) const override;
  std::optional<std::filesystem::path> pathOf(std::string_view scope, std::string_view key) const override;

 private:
  std::filesystem::path root_;
};

// Reads a whole file of at most kMaxLayoutBytes; nullopt when it is missing, unreadable or larger.
std::optional<std::string> readBoundedFile(const std::filesystem::path& path);
// Writes `text` to `path` through a temporary file in the same directory and an atomic replace.
Status writeFileAtomic(const std::filesystem::path& path, std::string_view text);

}  // namespace r1ui::dock
