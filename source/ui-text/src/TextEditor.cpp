// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: TextEditor implementation (see r1ui/text/TextEditor.h for the full behavior contract).
// Structure: every mutation funnels through replaceRange() (text + history + caret snapping) or
//   through the small caret setters, so the invariants hold after each public call:
//     text valid UTF-8, size <= maxBytes unless lowered afterwards, caret/anchor <= size and on
//     grapheme boundaries, layout flagged stale whenever displayText() changes.
// Failure behavior: nothing here throws to callers except std::bad_alloc from std::string
//   growth, which propagates with the editor still in its previous consistent state because
//   buffers are built before they are committed.
#include "r1ui/text/TextEditor.h"

#include <algorithm>
#include <cmath>
#include <utility>

#include "EditHistory.h"
#include "r1ui/text/Grapheme.h"
#include "r1ui/text/Utf8.h"

namespace r1ui::text {

namespace {

constexpr std::size_t kMaxPreeditBytes = 4096;

bool isDroppedInSingleLine(char32_t cp) {
  return cp < 0x20 || (cp >= 0x7F && cp <= 0x9F) || cp == 0x2028 || cp == 0x2029;
}

// Sanitizes text for a single-line field (see TextEditor.h) and stops once `limit` bytes were
// produced, so a huge clipboard string costs only as much as the room available.
std::string sanitizeSingleLine(std::string_view in, std::size_t limit) {
  std::string out;
  out.reserve(std::min(in.size(), limit));
  for (std::size_t pos = 0; pos < in.size() && out.size() < limit;) {
    const DecodedCodePoint d = decodeUtf8(in, pos);
    if (!isDroppedInSingleLine(d.codePoint)) appendUtf8(out, d.codePoint);
    pos += d.length;
  }
  return out;
}

// Cuts `s` to at most `maxBytes` at a grapheme boundary.
void cutToBoundary(std::string& s, std::size_t maxBytes) {
  if (s.size() <= maxBytes) return;
  s.resize(prevGraphemeBoundary(s, maxBytes + 1));
}

std::size_t snapForward(const std::string& text, std::size_t offset) {
  offset = std::min(offset, text.size());
  return isGraphemeBoundary(text, offset) ? offset : nextGraphemeBoundary(text, offset);
}

std::size_t snapNearest(const std::string& text, std::size_t offset) {
  offset = std::min(offset, text.size());
  if (isGraphemeBoundary(text, offset)) return offset;
  const std::size_t before = prevGraphemeBoundary(text, offset);
  const std::size_t after = nextGraphemeBoundary(text, offset);
  return offset - before < after - offset ? before : after;
}

}  // namespace

struct TextEditor::State {
  EditorConfig config;
  std::string text;
  std::size_t caret = 0;
  std::size_t anchor = 0;
  std::size_t revision = 0;
  EditHistory history;
  ClipboardCallbacks clipboard;

  bool composing = false;
  std::string preedit;
  std::size_t preeditCursor = 0;
  mutable std::string display;
  mutable bool displayDirty = true;

  CaretMap map;
  bool layoutOk = false;
  float scroll = 0;

  void textChanged() {
    ++revision;
    displayDirty = true;
    layoutOk = false;
  }
  void preeditChanged() {
    displayDirty = true;
    layoutOk = false;
  }
  void cancelPreedit() {
    if (!composing) return;
    composing = false;
    preedit.clear();
    preeditCursor = 0;
    preeditChanged();
  }
  TextRange selection() const { return TextRange{std::min(anchor, caret), std::max(anchor, caret)}; }
  std::size_t room(std::size_t replaced) const {
    const std::size_t after = text.size() - replaced;
    return config.maxBytes > after ? config.maxBytes - after : 0;
  }

  // Replaces [begin, end) by `ins`, records the step, and places the caret after the insertion
  // (snapped forward when the splice merged it into a cluster).
  void replaceRange(std::size_t begin, std::size_t end, std::string ins, EditKind kind) {
    EditRecord rec;
    rec.offset = begin;
    rec.removed = text.substr(begin, end - begin);
    rec.inserted = ins;
    rec.caretBefore = caret;
    rec.anchorBefore = anchor;
    rec.kind = kind;
    text.replace(begin, end - begin, ins);
    caret = anchor = snapForward(text, begin + ins.size());
    rec.caretAfter = caret;
    rec.anchorAfter = anchor;
    history.record(std::move(rec));
    textChanged();
  }

  bool deleteRange(std::size_t begin, std::size_t end, EditKind kind) {
    if (config.readOnly || begin >= end) return false;
    cancelPreedit();
    replaceRange(begin, end, std::string(), kind);
    return true;
  }

  // Applies a history record's text change; `forward` redoes, otherwise undoes.
  bool applyRecord(const EditRecord& rec, bool forward) {
    const std::string& from = forward ? rec.removed : rec.inserted;
    const std::string& to = forward ? rec.inserted : rec.removed;
    if (rec.offset > text.size() || from.size() > text.size() - rec.offset) return false;
    text.replace(rec.offset, from.size(), to);
    caret = snapNearest(text, forward ? rec.caretAfter : rec.caretBefore);
    anchor = snapNearest(text, forward ? rec.anchorAfter : rec.anchorBefore);
    textChanged();
    return true;
  }
};

TextEditor::TextEditor(const EditorConfig& config) : s_(std::make_unique<State>()) {
  s_->config = config;
  s_->config.maxBytes = std::clamp<std::size_t>(config.maxBytes, 1, kMaxAllowedBytes);
}
TextEditor::~TextEditor() = default;
TextEditor::TextEditor(TextEditor&&) noexcept = default;
TextEditor& TextEditor::operator=(TextEditor&&) noexcept = default;

// ---- State ---------------------------------------------------------------------------------

const std::string& TextEditor::text() const { return s_->text; }
std::size_t TextEditor::caret() const { return s_->caret; }
std::size_t TextEditor::anchor() const { return s_->anchor; }
TextRange TextEditor::selection() const { return s_->selection(); }
bool TextEditor::hasSelection() const { return s_->caret != s_->anchor; }
std::size_t TextEditor::revision() const { return s_->revision; }
bool TextEditor::readOnly() const { return s_->config.readOnly; }
void TextEditor::setReadOnly(bool value) {
  if (value) s_->cancelPreedit();
  s_->config.readOnly = value;
}
std::size_t TextEditor::maxBytes() const { return s_->config.maxBytes; }

// History records were validated against the old limit, so replaying them could regrow the text
// past the new one; dropping the history keeps the size invariant simple and absolute.
void TextEditor::setMaxBytes(std::size_t bytes) {
  s_->config.maxBytes = std::clamp<std::size_t>(bytes, 1, kMaxAllowedBytes);
  s_->history.clear();
}

void TextEditor::setText(std::string_view utf8) {
  State& s = *s_;
  std::string clean = sanitizeSingleLine(utf8, s.config.maxBytes + 4);
  cutToBoundary(clean, s.config.maxBytes);
  s.cancelPreedit();
  s.text = std::move(clean);
  s.caret = s.anchor = s.text.size();
  s.history.clear();
  s.textChanged();
}

// ---- Editing -------------------------------------------------------------------------------

std::size_t TextEditor::insertText(std::string_view utf8, InsertSource source) {
  State& s = *s_;
  if (s.config.readOnly) return 0;
  s.cancelPreedit();
  const TextRange sel = s.selection();
  const std::size_t avail = s.room(sel.end - sel.begin);
  std::string clean = sanitizeSingleLine(utf8, avail + 4);
  cutToBoundary(clean, avail);
  if (clean.empty()) return 0;

  const std::size_t taken = clean.size();
  if (source == InsertSource::Paste) s.history.breakGroup();
  s.replaceRange(sel.begin, sel.end, std::move(clean),
                 source == InsertSource::Paste ? EditKind::Other : EditKind::Typing);
  if (source == InsertSource::Paste) s.history.breakGroup();
  return taken;
}

bool TextEditor::deleteBackward() {
  State& s = *s_;
  if (s.config.readOnly) return false;
  if (s.caret != s.anchor) {
    s.history.breakGroup();
    const TextRange sel = s.selection();
    return s.deleteRange(sel.begin, sel.end, EditKind::Other);
  }
  if (s.caret == 0) return false;
  return s.deleteRange(prevGraphemeBoundary(s.text, s.caret), s.caret, EditKind::DeleteBackward);
}

bool TextEditor::deleteForward() {
  State& s = *s_;
  if (s.config.readOnly) return false;
  if (s.caret != s.anchor) {
    s.history.breakGroup();
    const TextRange sel = s.selection();
    return s.deleteRange(sel.begin, sel.end, EditKind::Other);
  }
  if (s.caret >= s.text.size()) return false;
  return s.deleteRange(s.caret, nextGraphemeBoundary(s.text, s.caret), EditKind::DeleteForward);
}

bool TextEditor::deleteWordBackward() {
  State& s = *s_;
  if (s.config.readOnly) return false;
  s.history.breakGroup();
  const TextRange sel = s.selection();
  if (!sel.empty()) return s.deleteRange(sel.begin, sel.end, EditKind::Other);
  return s.deleteRange(prevWordBoundary(s.text, s.caret), s.caret, EditKind::Other);
}

bool TextEditor::deleteWordForward() {
  State& s = *s_;
  if (s.config.readOnly) return false;
  s.history.breakGroup();
  const TextRange sel = s.selection();
  if (!sel.empty()) return s.deleteRange(sel.begin, sel.end, EditKind::Other);
  return s.deleteRange(s.caret, nextWordBoundary(s.text, s.caret), EditKind::Other);
}

// ---- Caret and selection ---------------------------------------------------------------------

void TextEditor::move(Motion motion, bool extend) {
  State& s = *s_;
  s.cancelPreedit();
  s.history.breakGroup();
  const TextRange sel = s.selection();
  std::size_t target = s.caret;
  switch (motion) {
    case Motion::Left:
      target = (!extend && !sel.empty()) ? sel.begin : prevGraphemeBoundary(s.text, s.caret);
      break;
    case Motion::Right:
      target = (!extend && !sel.empty()) ? sel.end : nextGraphemeBoundary(s.text, s.caret);
      break;
    case Motion::WordLeft:
      target = prevWordBoundary(s.text, s.caret);
      break;
    case Motion::WordRight:
      target = nextWordBoundary(s.text, s.caret);
      break;
    case Motion::LineStart:
      target = 0;
      break;
    case Motion::LineEnd:
      target = s.text.size();
      break;
  }
  s.caret = target;
  if (!extend) s.anchor = target;
}

void TextEditor::selectAll() {
  State& s = *s_;
  s.cancelPreedit();
  s.history.breakGroup();
  s.anchor = 0;
  s.caret = s.text.size();
}

void TextEditor::selectWordAt(std::size_t offset) {
  State& s = *s_;
  s.cancelPreedit();
  s.history.breakGroup();
  const auto [begin, end] = wordRangeAt(s.text, offset);
  s.anchor = begin;
  s.caret = end;
}

void TextEditor::pointerPress(std::size_t offset, int clickCount, bool extend) {
  State& s = *s_;
  if (clickCount >= 3) {
    selectAll();
  } else if (clickCount == 2) {
    selectWordAt(offset);
  } else {
    setSelection(extend ? s.anchor : offset, offset);
  }
}

void TextEditor::setSelection(std::size_t anchor, std::size_t caret) {
  State& s = *s_;
  s.cancelPreedit();
  s.history.breakGroup();
  s.anchor = snapNearest(s.text, anchor);
  s.caret = snapNearest(s.text, caret);
}

// ---- Clipboard -------------------------------------------------------------------------------

void TextEditor::setClipboard(ClipboardCallbacks callbacks) { s_->clipboard = std::move(callbacks); }

bool TextEditor::copy() {
  State& s = *s_;
  if (!s.clipboard.write || s.caret == s.anchor) return false;
  const TextRange sel = s.selection();
  const std::string selected = s.text.substr(sel.begin, sel.end - sel.begin);
  s.history.breakGroup();
  s.clipboard.write(selected);
  return true;
}

bool TextEditor::cut() {
  State& s = *s_;
  if (s.config.readOnly || !copy()) return false;
  const TextRange sel = s.selection();
  return s.deleteRange(sel.begin, sel.end, EditKind::Other);
}

bool TextEditor::paste() {
  State& s = *s_;
  if (s.config.readOnly || !s.clipboard.read) return false;
  const std::optional<std::string> clip = s.clipboard.read();
  if (!clip) return false;
  return insertText(*clip, InsertSource::Paste) > 0;
}

// ---- Undo ------------------------------------------------------------------------------------

bool TextEditor::undo() {
  State& s = *s_;
  if (s.config.readOnly || !s.history.canUndo()) return false;
  s.cancelPreedit();
  if (!s.applyRecord(s.history.takeUndo(), false)) {
    s.history.clear();
    return false;
  }
  return true;
}

bool TextEditor::redo() {
  State& s = *s_;
  if (s.config.readOnly || !s.history.canRedo()) return false;
  s.cancelPreedit();
  if (!s.applyRecord(s.history.takeRedo(), true)) {
    s.history.clear();
    return false;
  }
  return true;
}

bool TextEditor::canUndo() const { return !s_->config.readOnly && s_->history.canUndo(); }
bool TextEditor::canRedo() const { return !s_->config.readOnly && s_->history.canRedo(); }
void TextEditor::breakUndoGroup() { s_->history.breakGroup(); }

// ---- Composition -----------------------------------------------------------------------------

bool TextEditor::setPreedit(std::string_view utf8, std::size_t cursorInPreedit) {
  State& s = *s_;
  if (s.config.readOnly) return false;
  std::string clean = sanitizeSingleLine(utf8, kMaxPreeditBytes + 4);
  if (clean.empty()) {
    s.cancelPreedit();
    return true;
  }
  if (!s.composing && s.caret != s.anchor) {
    s.history.breakGroup();
    const TextRange sel = s.selection();
    s.deleteRange(sel.begin, sel.end, EditKind::Other);
  }
  cutToBoundary(clean, std::min(kMaxPreeditBytes, s.room(0)));
  if (clean.empty()) {
    s.cancelPreedit();
    return true;
  }
  const std::size_t cursor = std::min(cursorInPreedit, clean.size());
  s.preeditCursor = isGraphemeBoundary(clean, cursor) ? cursor : prevGraphemeBoundary(clean, cursor);
  s.preedit = std::move(clean);
  s.composing = true;
  s.history.breakGroup();
  s.preeditChanged();
  return true;
}

bool TextEditor::commitPreedit() {
  State& s = *s_;
  if (!s.composing) return false;
  std::string committed = std::move(s.preedit);
  s.composing = false;
  s.preedit.clear();
  s.preeditCursor = 0;
  s.preeditChanged();
  s.history.breakGroup();
  insertText(committed, InsertSource::Typing);
  return true;
}

void TextEditor::cancelPreedit() { s_->cancelPreedit(); }
bool TextEditor::composing() const { return s_->composing; }

const std::string& TextEditor::displayText() const {
  const State& s = *s_;
  if (!s.composing) return s.text;
  if (s.displayDirty) {
    s.display = s.text.substr(0, s.caret) + s.preedit + s.text.substr(s.caret);
    s.displayDirty = false;
  }
  return s.display;
}

std::optional<TextRange> TextEditor::preeditRange() const {
  const State& s = *s_;
  if (!s.composing) return std::nullopt;
  return TextRange{s.caret, s.caret + s.preedit.size()};
}

std::size_t TextEditor::displayCaret() const {
  const State& s = *s_;
  return s.composing ? s.caret + s.preeditCursor : s.caret;
}

// ---- Layout, caret geometry, scrolling ---------------------------------------------------------

Status TextEditor::relayout(const Font& font, float pixelSize, const ShapeOptions& options) {
  State& s = *s_;
  s.layoutOk = false;
  const std::string& shown = displayText();
  Result<ShapedRun> run = shapeText(font, pixelSize, shown, options);
  if (!run.ok()) return run.error();
  s.map = CaretMap::build(run.value(), shown);
  s.layoutOk = true;
  return {};
}

bool TextEditor::layoutCurrent() const { return s_->layoutOk; }
float TextEditor::textWidth() const { return s_->layoutOk ? s_->map.width() : 0.0f; }

std::optional<float> TextEditor::caretX() const {
  if (!s_->layoutOk) return std::nullopt;
  return s_->map.xForOffset(displayCaret());
}

std::optional<std::pair<float, float>> TextEditor::selectionX() const {
  const State& s = *s_;
  if (!s.layoutOk || s.caret == s.anchor) return std::nullopt;
  const TextRange sel = s.selection();
  return std::make_pair(s.map.xForOffset(sel.begin), s.map.xForOffset(sel.end));
}

std::size_t TextEditor::hitTest(float x) const {
  const State& s = *s_;
  if (!s.layoutOk) return s.caret;
  const std::size_t shown = s.map.offsetForX(x);
  std::size_t offset = s.caret;
  if (!s.composing || shown <= s.caret) {
    offset = shown;
  } else if (shown >= s.caret + s.preedit.size()) {
    offset = shown - s.preedit.size();
  }
  // A boundary of the display text is not always one of text() (a preedit can join a cluster).
  return snapForward(s.text, offset);
}

float TextEditor::scrollX() const { return s_->scroll; }

float TextEditor::ensureCaretVisible(float fieldWidth, float margin) {
  State& s = *s_;
  if (!s.layoutOk || !std::isfinite(fieldWidth) || fieldWidth <= 0.0f) return s.scroll;
  margin = std::isfinite(margin) ? std::clamp(margin, 0.0f, fieldWidth * 0.5f) : 0.0f;
  const float x = s.map.xForOffset(displayCaret());
  float scroll = s.scroll;
  if (x - scroll < margin) scroll = x - margin;
  if (x - scroll > fieldWidth - margin) scroll = x - (fieldWidth - margin);
  const float maxScroll = std::max(0.0f, s.map.width() + margin - fieldWidth);
  s.scroll = std::clamp(scroll, 0.0f, maxScroll);
  return s.scroll;
}

}  // namespace r1ui::text
