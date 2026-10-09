// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: implementation of Services.h (style table assembly, resolved-style cache, colours).
// Invariants: sheet_ always compiles against the tokens; a rejected addition changes nothing.
// Callers: UiContext, widgets, the shell.
#include "r1ui/widgets/runtime/Services.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace r1ui::widgets {

namespace {

std::unique_ptr<theme::StyleSheet> compile(const std::vector<theme::StyleRuleEntry>& rules, const theme::Tokens& tokens) {
  theme::StyleSheetResult result = theme::StyleSheet::create(rules, tokens);
  if (!result.ok()) {
    std::string message = "The style table was rejected:";
    for (const std::string& e : result.errors) message += "\n  " + e;
    throw std::runtime_error(message);
  }
  return std::make_unique<theme::StyleSheet>(std::move(*result.sheet));
}

}  // namespace

Services::Services(std::shared_ptr<const theme::Tokens> tokens, TextureFactory& textures, const ServicesPaths& paths)
    : theme_(tokens),
      text_(textures, paths.fontDir),
      icons_(textures, paths.iconDirs) {
  const auto builtin = theme::builtinRules();
  rules_.assign(builtin.begin(), builtin.end());
  sheet_ = compile(rules_, *tokens);
}

void Services::addStyleRows(std::span<const theme::StyleRuleEntry> rows) {
  if (rows.empty()) return;
  const void* identity = rows.data();
  if (std::find(addedTables_.begin(), addedTables_.end(), identity) != addedTables_.end()) return;
  std::vector<theme::StyleRuleEntry> merged = rules_;
  merged.insert(merged.end(), rows.begin(), rows.end());
  std::unique_ptr<theme::StyleSheet> sheet = compile(merged, theme_.tokens());  // throws: nothing below runs
  rules_ = std::move(merged);
  sheet_ = std::move(sheet);
  addedTables_.push_back(identity);
  ++sheetRevision_;
}

theme::ResolvedStyle Services::resolve(std::string_view key, uint8_t state) {
  if (cacheThemeRevision_ != theme_.revision() || cacheSheetRevision_ != sheetRevision_) {
    cache_.clear();
    cacheThemeRevision_ = theme_.revision();
    cacheSheetRevision_ = sheetRevision_;
  }
  auto it = cache_.find(key);
  if (it == cache_.end()) it = cache_.emplace(std::string(key), std::vector<CachedState>{}).first;
  for (const CachedState& c : it->second) {
    if (c.state == state) return c.value;
  }
  std::optional<theme::ResolvedStyle> resolved = sheet_->resolve(theme_, key, state);
  if (!resolved) throw std::logic_error("unknown style key \"" + std::string(key) + "\"");
  it->second.push_back({state, std::move(*resolved)});
  return it->second.back().value;
}

render::Color Services::toRender(const theme::Color& c, double opacity) {
  const double a = std::clamp(opacity, 0.0, 1.0);
  return render::Color::fromRgba8(c.r, c.g, c.b, static_cast<uint8_t>(std::lround(c.a * a)));
}

render::Color Services::color(std::string_view token, double opacity) const {
  const auto c = theme_.color(token);
  return c ? toRender(*c, opacity) : render::Color{1.0f, 0.0f, 1.0f, 1.0f};
}

}  // namespace r1ui::widgets
