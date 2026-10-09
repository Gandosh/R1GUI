// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: the load, save and import glue around the stores (CustomizationIo.h): loadCustomization,
//   saveCustomization, importCustomization, exportCustomization and CustomizationStorage. The format is
//   written by CustomizationExport.cpp and read by CustomizationParse.cpp.
// Invariants: a rejected or unreadable layer changes nothing (the live state is kept) and a corrupt file is
//   moved aside under a different name; the workspace layer is only read.
// Callers: hosts, tests.
#include "r1ui/commands/customize/CustomizationIo.h"

namespace r1ui::commands::customize {

// ---- load, save, import -------------------------------------------------------------------------

namespace {

void note(LoadReport& report, const std::string& source, const ParseResult& parsed) {
  for (const ParseIssue& i : parsed.issues) report.issues.push_back({source + ": " + i.where, i.message});
}

// Reads one layer; returns the parsed delta when the layer was acceptable.
std::optional<Delta> readLayer(TextStore& store, const char* name, LoadReport& report, std::string& keptAside) {
  const TextLoad loaded = store.load();
  if (!loaded.error.empty()) {
    report.ok = false;
    if (report.error.empty()) report.error = std::string(name) + ": " + loaded.error;
    if (loaded.exists) {
      std::string moveError;
      keptAside = store.setAside(moveError);
    }
    return std::nullopt;
  }
  if (!loaded.exists) return std::nullopt;
  ParseResult parsed = parseDelta(loaded.text);
  note(report, name, parsed);
  if (!parsed.ok) {
    report.ok = false;
    if (report.error.empty()) report.error = std::string(name) + " file rejected: " + parsed.error;
    std::string moveError;
    keptAside = store.setAside(moveError);
    return std::nullopt;
  }
  return std::move(parsed.delta);
}

}  // namespace

LoadReport loadCustomization(Customization& customization, TextStore& user, TextStore* workspace) {
  LoadReport report;
  if (auto delta = readLayer(user, "user", report, report.userKeptAside)) {
    customization.setUserDelta(std::move(*delta));
    report.userLoaded = true;
  }
  if (workspace != nullptr) {
    if (auto delta = readLayer(*workspace, "workspace", report, report.workspaceKeptAside)) {
      customization.setWorkspaceDelta(std::move(*delta));
      report.workspaceLoaded = true;
    }
  }
  return report;
}

bool saveCustomization(const Customization& customization, TextStore& user, std::string& error) {
  return user.save(exportDelta(customization.userDelta()), error);
}

LoadReport importCustomization(Customization& customization, std::string_view json, ImportMode mode) {
  LoadReport report;
  ParseResult parsed = parseDelta(json);
  note(report, "import", parsed);
  if (!parsed.ok) {
    report.ok = false;
    report.error = "import rejected: " + parsed.error;
    return report;
  }
  if (mode == ImportMode::Merge) {
    customization.setUserDelta(mergeDeltas(customization.userDelta(), parsed.delta));
  } else {
    customization.setUserDelta(std::move(parsed.delta));
  }
  report.userLoaded = true;
  return report;
}

std::string exportCustomization(const Customization& customization) { return exportDelta(customization.userDelta()); }

CustomizationStorage::CustomizationStorage(Customization& customization, TextStore& user, TextStore* workspace)
    : customization_(customization), user_(user), workspace_(workspace) {
  customization_.setOnCommit([this] { save(); });
}

CustomizationStorage::~CustomizationStorage() { customization_.setOnCommit({}); }

LoadReport CustomizationStorage::load() {
  LoadReport report = loadCustomization(customization_, user_, workspace_);
  lastError_ = report.error;
  return report;
}

bool CustomizationStorage::save() {
  std::string error;
  const bool ok = saveCustomization(customization_, user_, error);
  lastError_ = ok ? std::string() : error;
  return ok;
}

}  // namespace r1ui::commands::customize
