// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: the file side of customization (slice 5.9): the versioned JSON format for a Delta, strict
//   validated parsing with a report, export, the TextStore interface with memory and file
//   implementations (atomic writes, a corrupt file kept aside), the per-user store layered under a
//   per-workspace override, import and export of a customization file, and CustomizationStorage which
//   wires a Customization to its stores (load, save, save-on-commit).
// Why: spec 06 rules 39 to 42 and decision D5: customizations are remembered per user, a workspace may
//   override them, they survive a restart and a product update, and a damaged or hostile file must
//   never change the live state or crash the application.
// Format (version 1), one object:
//   {"format":"r1ui-customization","version":1,"serial":N,
//    "edits":[{"node":ID,"hidden":bool,"label":TEXT,"rect":[x,y,w,h]}],
//    "moves":[{"node":ID,"parent":ID,"anchor":ID,"side":"end|start|before|after"}],
//    "added":[{"id":ID,"kind":"menu|section|command|separator|heading|submenu|group|spacer|button",
//              "command":ID,"label":TEXT,"hidden":bool,"rect":[x,y,w,h],
//              "parent":ID,"anchor":ID,"side":SIDE}],
//    "toolbars":[{"id":ID,"title":TEXT,"orientation":"horizontal|vertical","sizeStep":"small|medium|large","gap":N}],
//    "panels":[{"id":ID,"title":TEXT,"width":N,"height":N,"snap":bool,"grid":N}],
//    "toolbarEdits":[{"id":ID,"sizeStep":S,"gap":N}], "panelEdits":[{"id":ID,"snap":bool,"grid":N}]}
//   Unknown members are ignored. Command ids are not checked against the registry on load: an unknown
//   id is kept and shows up as a missing command in the effective layout (spec 06 rule 35).
// Rejection of the WHOLE file (nothing changes, the file is moved aside under a different name): larger
//   than kMaxFileBytes, not valid JSON (this includes invalid UTF-8, duplicate keys and nesting deeper
//   than 8), not this format, a newer version, more than kMaxNodes entries, or no usable entry in a
//   non-empty file. Single entries are skipped or repaired with an issue: a bad id, unknown kind or
//   side, a duplicate id (the first wins), a rectangle that is not finite or smaller than the minimum
//   button size, a gap or grid out of range, a label longer than kMaxLabelBytes (cut) or containing
//   control characters (replaced).
// Stores: load() reports a missing file as exists = false; save() writes a temporary file and renames
//   it over the target so a crash never leaves a half-written file; setAside() renames a corrupt file
//   to "<name>.corrupt-<n>". No function throws.
// Layering: the user store holds the editable delta, the workspace store a read-only override applied
//   on top of it (Customization::setWorkspaceDelta); only the user store is ever written.
#pragma once

#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "r1ui/commands/customize/Customization.h"

namespace r1ui::commands::customize {

inline constexpr size_t kMaxFileBytes = size_t{24} << 20;
inline constexpr const char* kFormatName = "r1ui-customization";
inline constexpr int kFormatVersion = 1;

// ---- stores -------------------------------------------------------------------------------------

struct TextLoad {
  bool exists = false;
  std::string text;
  std::string error;  // empty unless reading failed
};

class TextStore {
 public:
  virtual ~TextStore() = default;
  virtual TextLoad load() = 0;
  virtual bool save(std::string_view text, std::string& error) = 0;
  // Moves the stored content aside (a corrupt file); returns its new name, empty when there was
  // nothing to move or it failed (error says why).
  virtual std::string setAside(std::string& error) = 0;
};

class MemoryTextStore final : public TextStore {
 public:
  TextLoad load() override;
  bool save(std::string_view text, std::string& error) override;
  std::string setAside(std::string& error) override;
  const std::optional<std::string>& contents() const { return text_; }
  void setContents(std::string text) { text_ = std::move(text); }
  const std::vector<std::string>& asides() const { return asides_; }

 private:
  std::optional<std::string> text_;
  std::vector<std::string> asides_;
};

class FileTextStore final : public TextStore {
 public:
  explicit FileTextStore(std::filesystem::path path) : path_(std::move(path)) {}
  TextLoad load() override;
  bool save(std::string_view text, std::string& error) override;
  std::string setAside(std::string& error) override;
  const std::filesystem::path& path() const { return path_; }

 private:
  std::filesystem::path path_;
};

// ---- format -------------------------------------------------------------------------------------

struct ParseIssue {
  std::string where;    // "added[3]"
  std::string message;
};

struct ParseResult {
  bool ok = false;        // the file is acceptable as a whole
  std::string error;      // why it was rejected as a whole
  Delta delta;
  std::vector<ParseIssue> issues;
  size_t skipped = 0;     // entries dropped
  size_t repaired = 0;    // entries kept after a repair
};

std::string exportDelta(const Delta& delta);
ParseResult parseDelta(std::string_view json);

// ---- load, save, import ------------------------------------------------------------------------

enum class ImportMode : uint8_t { Replace, Merge };

struct LoadReport {
  bool ok = true;               // no layer was rejected and no read failed
  bool userLoaded = false;
  bool workspaceLoaded = false;
  std::string error;            // first rejection or read failure
  std::vector<ParseIssue> issues;
  std::string userKeptAside;    // new name of a corrupt user file
  std::string workspaceKeptAside;
};

// Reads the stores into `customization`. A missing file leaves that layer as it is; a failure leaves
// the live state untouched.
LoadReport loadCustomization(Customization& customization, TextStore& user, TextStore* workspace = nullptr);
bool saveCustomization(const Customization& customization, TextStore& user, std::string& error);
// Replace sets the user delta from the text; Merge lays the imported delta over the current one.
LoadReport importCustomization(Customization& customization, std::string_view json, ImportMode mode = ImportMode::Replace);
std::string exportCustomization(const Customization& customization);

// Wires a Customization to its stores: load, save, and save when an edit session is committed.
class CustomizationStorage {
 public:
  CustomizationStorage(Customization& customization, TextStore& user, TextStore* workspace = nullptr);
  ~CustomizationStorage();
  CustomizationStorage(const CustomizationStorage&) = delete;
  CustomizationStorage& operator=(const CustomizationStorage&) = delete;

  LoadReport load();
  bool save();
  const std::string& lastError() const { return lastError_; }

 private:
  Customization& customization_;
  TextStore& user_;
  TextStore* workspace_;
  std::string lastError_;
};

}  // namespace r1ui::commands::customize
