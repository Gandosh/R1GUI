// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: the copy and paste side of PropertyContext: one value as text, and a group (a category, or a
//   sub-group of it) as "name<TAB>value" lines under a version header, with the escaping that keeps
//   tabs, newlines and backslashes inside values intact.
// Why: spec 09 rules 68-73: values are copied as text and pasted back in one undo step, singly or as a
//   whole group or category. The text is the clipboard payload, so it crosses a trust boundary: parsing
//   is strict, bounded (kMaxPasteBytes, kMaxPasteLines) and a malformed line affects only itself.
// Format: first line "r1props 1"; then one line per property: name, a tab, the escaped value (\\, \t, \n,
//   \r). Password properties are never written. Unknown names are reported and skipped on paste.
// Callers: PropertyPanel (row and category menus), hosts, tests.
#include "r1ui/props/PropertyContext.h"

namespace r1ui::props {

namespace {

constexpr std::string_view kHeader = "r1props 1";

std::string escape(std::string_view value) {
  std::string out;
  out.reserve(value.size());
  for (const char c : value) {
    switch (c) {
      case '\\': out += "\\\\"; break;
      case '\t': out += "\\t"; break;
      case '\n': out += "\\n"; break;
      case '\r': out += "\\r"; break;
      default: out += c;
    }
  }
  return out;
}

// False on a dangling or unknown escape.
bool unescape(std::string_view in, std::string& out) {
  out.clear();
  out.reserve(in.size());
  for (size_t i = 0; i < in.size(); ++i) {
    if (in[i] != '\\') {
      out += in[i];
      continue;
    }
    if (++i >= in.size()) return false;
    switch (in[i]) {
      case '\\': out += '\\'; break;
      case 't': out += '\t'; break;
      case 'n': out += '\n'; break;
      case 'r': out += '\r'; break;
      default: return false;
    }
  }
  return true;
}

EditReport refusal(EditCode code, std::string_view property) {
  EditReport r;
  r.add(code, property, -1);
  return r;
}

}  // namespace

std::optional<std::string> PropertyContext::copyValue(size_t row) const {
  if (row >= rows_.size() || targets_.empty()) return std::nullopt;
  const PropertyDescriptor& d = descriptor(row);
  if (d.meta.password) return std::nullopt;
  std::string text = formatText(d, read(row, 0));
  if (text.empty() && d.kind != ValueKind::String) return std::nullopt;
  return text;
}

EditReport PropertyContext::pasteValue(size_t row, std::string_view text) {
  if (row >= rows_.size()) return refusal(EditCode::NoSuchProperty, {});
  const ApplyMode mode{label(strings_.paste, descriptor(row)), false, true, true, false};
  return editText(row, text, -1, mode);
}

std::string PropertyContext::copyGroup(std::optional<std::string_view> category, std::optional<std::string_view> group) const {
  std::string out(kHeader);
  out += '\n';
  if (targets_.empty()) return out;
  for (size_t row = 0; row < rows_.size(); ++row) {
    const PropertyDescriptor& d = descriptor(row);
    if (!rowInScope(d, category, group) || d.meta.password) continue;
    const std::string text = formatText(d, read(row, 0));
    if (text.empty() && d.kind != ValueKind::String) continue;
    out += d.name;
    out += '\t';
    out += escape(text);
    out += '\n';
  }
  return out;
}

EditReport PropertyContext::pasteGroup(std::optional<std::string_view> category, std::optional<std::string_view> group, std::string_view text) {
  if (targets_.empty()) return refusal(EditCode::NoSelection, {});
  if (text.size() > kMaxPasteBytes) return refusal(EditCode::TooLong, {});

  // Parse every line first so a malformed payload is refused before anything is touched.
  struct Entry {
    std::string name;
    std::string value;
  };
  std::vector<Entry> entries;
  EditReport total;
  size_t pos = 0;
  size_t lines = 0;
  bool headerSeen = false;
  while (pos <= text.size()) {
    size_t end = text.find('\n', pos);
    if (end == std::string_view::npos) end = text.size();
    std::string_view line = text.substr(pos, end - pos);
    pos = end + 1;
    if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
    if (line.empty()) continue;
    if (++lines > kMaxPasteLines) return refusal(EditCode::TooLong, {});
    if (!headerSeen) {
      if (line != kHeader) return refusal(EditCode::Unparsable, {});
      headerSeen = true;
      continue;
    }
    const size_t tab = line.find('\t');
    Entry entry;
    if (tab == std::string_view::npos || tab == 0 || !unescape(line.substr(tab + 1), entry.value)) {
      total.add(EditCode::Unparsable, line.substr(0, tab), -1);
      continue;
    }
    entry.name = std::string(line.substr(0, tab));
    entries.push_back(std::move(entry));
  }
  if (!headerSeen) return refusal(EditCode::Unparsable, {});

  const std::string scope = group ? std::string(*group) : (category ? std::string(*category) : std::string());
  if (!undo_->begin(strings_.paste + (scope.empty() ? std::string("properties") : scope), false)) return refusal(EditCode::InteractionOpen, {});
  for (const Entry& entry : entries) {
    const auto row = findRow(entry.name);
    if (!row) {
      total.add(EditCode::NoSuchProperty, entry.name, -1);
      continue;
    }
    if (!rowInScope(descriptor(*row), category, group)) continue;
    const ApplyMode mode{label(strings_.paste, descriptor(*row)), false, true, true, false};
    total.merge(editText(*row, entry.value, -1, mode));
  }
  undo_->commit();
  if (!isError(total.code) && total.changed == 0) total.code = EditCode::Unchanged;
  return total;
}

}  // namespace r1ui::props
