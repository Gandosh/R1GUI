// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: implementation of FilePathDialog.h: the listing, the path rules and the dialog.
// Invariants: the dialog state lives in a shared_ptr held by its callbacks and every widget id in it is
//   re-checked with alive() before use; the chosen callback runs once, from the dialog's result handler
//   (so after the dialog and its focus trap are gone); an overwrite needs a second press for the same
//   path; nothing here throws (file system calls use error codes).
// Callers: the preview, the creator window, the gallery, tests.
#include "r1ui/widgets/filepath/FilePathDialog.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <memory>
#include <string>
#include <system_error>

#include "r1ui/commands/Text.h"
#include "r1ui/widgets/button/Button.h"
#include "r1ui/widgets/label/Label.h"
#include "r1ui/widgets/section/Section.h"
#include "r1ui/widgets/runtime/PaintContext.h"
#include "r1ui/widgets/runtime/UiContext.h"
#include "r1ui/widgets/textinput/TextInput.h"

namespace r1ui::widgets {

namespace fs = std::filesystem;
namespace layout = core::layout;

namespace {

fs::path pathFromUtf8(std::string_view text) {
  const std::u8string wide(reinterpret_cast<const char8_t*>(text.data()), text.size());
  return fs::path(wide);
}

std::string utf8Of(const fs::path& path) {
  const std::u8string text = path.u8string();
  return std::string(reinterpret_cast<const char*>(text.data()), text.size());
}

char lower(char c) { return static_cast<char>(std::tolower(static_cast<unsigned char>(c))); }

bool sameExtension(const fs::path& file, std::string_view extension) {
  if (extension.empty()) return true;
  const std::string actual = utf8Of(file.extension());
  if (actual.size() != extension.size()) return false;
  for (size_t i = 0; i < actual.size(); ++i) {
    if (lower(actual[i]) != lower(extension[i])) return false;
  }
  return true;
}

bool isDeviceName(std::string stem) {
  for (char& c : stem) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
  if (stem == "CON" || stem == "PRN" || stem == "AUX" || stem == "NUL") return true;
  return stem.size() == 4 && (stem.rfind("COM", 0) == 0 || stem.rfind("LPT", 0) == 0) && stem[3] >= '0' && stem[3] <= '9';
}

std::string trimmed(std::string_view text) {
  size_t a = 0;
  size_t b = text.size();
  while (a < b && (text[a] == ' ' || text[a] == '\t')) ++a;
  while (b > a && (text[b - 1] == ' ' || text[b - 1] == '\t')) --b;
  return std::string(text.substr(a, b - a));
}

}  // namespace

// ---- listing and formatting -------------------------------------------------------------------------

std::vector<FileEntry> listFiles(const fs::path& folder, std::string_view extension) {
  std::vector<FileEntry> out;
  std::error_code ec;
  if (folder.empty() || !fs::is_directory(folder, ec)) return out;
  fs::directory_iterator it(folder, fs::directory_options::skip_permission_denied, ec);
  if (ec) return out;
  for (const fs::directory_iterator end; it != end && out.size() < kMaxListedFiles; it.increment(ec)) {
    if (ec) break;
    std::error_code entryEc;
    if (!it->is_regular_file(entryEc) || entryEc) continue;
    const fs::path& file = it->path();
    if (!sameExtension(file, extension)) continue;
    FileEntry entry;
    entry.name = utf8Of(file.filename());
    if (!commands::isValidUtf8(entry.name)) continue;
    const auto size = it->file_size(entryEc);
    entry.bytes = entryEc ? 0 : static_cast<uint64_t>(size);
    out.push_back(std::move(entry));
  }
  std::sort(out.begin(), out.end(), [](const FileEntry& a, const FileEntry& b) {
    return std::lexicographical_compare(a.name.begin(), a.name.end(), b.name.begin(), b.name.end(), [](char x, char y) { return lower(x) < lower(y); });
  });
  return out;
}

std::string formatFileSize(uint64_t bytes) {
  if (bytes < 1024) return std::to_string(bytes) + " B";
  const char* units[] = {"KB", "MB", "GB"};
  double value = static_cast<double>(bytes) / 1024.0;
  size_t unit = 0;
  while (value >= 1024.0 && unit < 2) {
    value /= 1024.0;
    ++unit;
  }
  char buffer[32];
  std::snprintf(buffer, sizeof buffer, "%.1f %s", value, units[unit]);
  return buffer;
}

// ---- the path rules ---------------------------------------------------------------------------------

FilePathCheck checkFilePath(const FilePathOptions& options, std::string_view typedText) {
  FilePathCheck check;
  const std::string typed = trimmed(typedText);
  if (typed.empty()) {
    check.message = options.mode == FilePathMode::Open ? "Choose a file from the list or type its path." : "Type a file name.";
    return check;
  }
  if (!commands::isValidUtf8(typed)) {
    check.message = "The path is not valid text.";
    return check;
  }
  if (typed.size() > kMaxPathChars) {
    check.message = "The path is too long (at most " + std::to_string(kMaxPathChars) + " characters).";
    return check;
  }
  for (size_t i = 0; i < typed.size(); ++i) {
    const unsigned char c = static_cast<unsigned char>(typed[i]);
    if (c < 0x20 || c == 0x7f || std::string_view("<>\"|?*").find(static_cast<char>(c)) != std::string_view::npos) {
      check.message = "The path contains a character that is not allowed in file names.";
      return check;
    }
    if (c == ':' && !(i == 1 && std::isalpha(static_cast<unsigned char>(typed[0])))) {
      check.message = "A colon is only allowed after a drive letter.";
      return check;
    }
  }
  fs::path path = pathFromUtf8(typed);
  if (!path.has_filename()) {
    check.message = "That is a folder; type a file name.";
    return check;
  }
  if (path.has_root_name() && !path.has_root_directory()) {
    check.message = "Use a full path such as C:\\folder\\name, or just a file name.";
    return check;
  }
  if (!path.is_absolute()) {
    if (path.has_root_directory()) {
      check.message = "Use a full path such as C:\\folder\\name, or just a file name.";
      return check;
    }
    if (options.folder.empty()) {
      check.message = "Type a full path.";
      return check;
    }
    path = options.folder / path;
  }
  const std::string fileName = utf8Of(path.filename());
  if (fileName.back() == '.' || fileName.back() == ' ') {
    check.message = "A file name cannot end with a dot or a space.";
    return check;
  }
  if (isDeviceName(utf8Of(path.stem()))) {
    check.message = "That name is reserved by the system.";
    return check;
  }
  if (path.extension().empty() && !options.extension.empty()) path += pathFromUtf8(options.extension);
  std::error_code ec;
  if (fs::is_directory(path, ec)) {
    check.message = "That is a folder; type a file name.";
    return check;
  }
  check.exists = fs::is_regular_file(path, ec);
  if (options.mode == FilePathMode::Open && !check.exists) {
    check.message = "There is no such file: " + utf8Of(path);
    return check;
  }
  if (options.mode == FilePathMode::Save && !check.exists) {
    const fs::path parent = path.parent_path();
    if (!parent.empty() && fs::exists(parent, ec) && !fs::is_directory(parent, ec)) {
      check.message = "The folder is a file: " + utf8Of(parent);
      return check;
    }
  }
  check.ok = true;
  check.path = std::move(path);
  return check;
}

// ---- the list widget --------------------------------------------------------------------------------

void FileListView::onAttached() {
  setFocusable(true);
  style().flexGrow = 1.0;
  style().flexShrink = 1.0;
  style().minHeight = layout::Length::px(kRowHeight * 2.0);
  style().overflow = layout::Overflow::Hidden;
}

double FileListView::viewportHeight() const { return ui().absRect(id()).h; }

void FileListView::setScroll(double offset) {
  const double maxScroll = std::max(0.0, static_cast<double>(entries_.size()) * kRowHeight - viewportHeight());
  const double clamped = std::clamp(std::isfinite(offset) ? offset : 0.0, 0.0, maxScroll);
  if (clamped == scroll_) return;
  scroll_ = clamped;
  requestPaint();
}

void FileListView::setEntries(std::vector<FileEntry> entries) {
  if (entries.size() > kMaxListedFiles) entries.resize(kMaxListedFiles);
  entries_ = std::move(entries);
  selected_ = -1;
  hover_ = -1;
  scroll_ = 0.0;
  requestPaint();
}

bool FileListView::select(int index, bool notify) {
  if (index < -1 || index >= static_cast<int>(entries_.size())) return false;
  selected_ = index;
  if (index >= 0) {
    const double top = index * kRowHeight;
    if (top < scroll_) setScroll(top);
    else if (top + kRowHeight > scroll_ + viewportHeight()) setScroll(top + kRowHeight - viewportHeight());
  }
  requestPaint();
  if (notify && index >= 0 && onSelect_) {
    const FileEntry entry = entries_[static_cast<size_t>(index)];
    onSelect_(entry);
  }
  return true;
}

core::layout::RectD FileListView::rowRect(int index) const {
  if (index < 0 || index >= static_cast<int>(entries_.size())) return {};
  const layout::Rect area = ui().absRect(id());
  const double y = area.y + index * kRowHeight - scroll_;
  if (y + kRowHeight <= area.y || y >= area.y + area.h) return {};
  return {static_cast<double>(area.x), y, static_cast<double>(area.w), kRowHeight};
}

int FileListView::rowAt(double x, double y) const {
  const layout::Rect area = ui().absRect(id());
  if (x < area.x || x >= area.x + area.w || y < area.y || y >= area.y + area.h) return -1;
  const int row = static_cast<int>((y - area.y + scroll_) / kRowHeight);
  return row >= 0 && row < static_cast<int>(entries_.size()) ? row : -1;
}

void FileListView::paint(PaintContext& ctx) {
  const layout::Rect area = ctx.rect();
  render::Painter& painter = ctx.painter();
  painter.fillRect(ctx.box(), ctx.color("panel-field"));
  painter.border(ctx.box(), render::CornerRadii::uniform(ctx.px(6.0)), ctx.hairline(), ctx.color("border"));
  painter.pushClip(ctx.box());
  const theme::TextStyle body = ctx.style("label.body").text;
  const theme::TextStyle muted = ctx.style("label.muted").text;
  const render::Color white{1.0f, 1.0f, 1.0f, 1.0f};
  if (entries_.empty()) {
    TextOptions o;
    o.align = TextAlign::Center;
    ctx.drawText("No files yet", muted, ctx.toPhysical(area.x, area.y, area.w, std::min(static_cast<double>(area.h), 2.0 * kRowHeight)), o);
  }
  const size_t first = static_cast<size_t>(std::max(0.0, scroll_ / kRowHeight));
  for (size_t i = first; i < entries_.size(); ++i) {
    const double y = area.y + static_cast<double>(i) * kRowHeight - scroll_;
    if (y >= area.y + area.h) break;
    const bool selected = static_cast<int>(i) == selected_;
    const bool hover = static_cast<int>(i) == hover_;
    if (selected) painter.fillRoundedRect(ctx.toPhysical(area.x + 3, y + 1, area.w - 6, kRowHeight - 2), render::CornerRadii::uniform(ctx.px(5.0)), ctx.color("accent"));
    else if (hover) painter.fillRoundedRect(ctx.toPhysical(area.x + 3, y + 1, area.w - 6, kRowHeight - 2), render::CornerRadii::uniform(ctx.px(5.0)), ctx.color("hover"));
    TextOptions name;
    name.color = selected ? white : ctx.color(body.color);
    name.padLeft = 10.0;
    ctx.drawText(entries_[i].name, body, ctx.toPhysical(area.x, y, std::max(0.0, area.w - 90.0), kRowHeight), name);
    TextOptions size;
    size.color = selected ? render::Color{1.0f, 1.0f, 1.0f, 0.8f} : ctx.color(muted.color);
    size.align = TextAlign::End;
    size.padRight = 10.0;
    ctx.drawText(formatFileSize(entries_[i].bytes), muted, ctx.toPhysical(area.x + area.w - 90.0, y, 90.0, kRowHeight), size);
  }
  painter.popClip();
}

void FileListView::paintOver(PaintContext& ctx) {
  if (focusVisible()) ctx.focusRing(ctx.px(4.0));
}

void FileListView::onPointerDown(Event& e) {
  if (e.button != core::events::Button::Left) return;
  ui().router().focus(id(), core::events::FocusReason::Pointer);
  e.markHandled();
  const int row = rowAt(e.x, e.y);
  if (row >= 0) select(row);
}

void FileListView::onPointerMove(Event& e) {
  const int row = rowAt(e.x, e.y);
  if (row != hover_) {
    hover_ = row;
    requestPaint();
  }
}

void FileListView::onPointerLeave(Event&) {
  if (hover_ != -1) {
    hover_ = -1;
    requestPaint();
  }
}

void FileListView::onPointerWheel(Event& e) {
  const double before = scroll_;
  setScroll(scroll_ - e.wheelY * 48.0);
  if (scroll_ != before) {
    e.markHandled();
    e.stopPropagation();
  }
}

void FileListView::onDoubleClick(Event& e) {
  const int row = rowAt(e.x, e.y);
  if (row < 0) return;
  select(row);
  e.markHandled();
  if (onActivate_) {
    const FileEntry entry = entries_[static_cast<size_t>(row)];
    onActivate_(entry);
  }
}

void FileListView::onKeyDown(Event& e) {
  using core::events::Key;
  if (entries_.empty()) return;
  const int count = static_cast<int>(entries_.size());
  switch (e.key) {
    case Key::Down: select(std::min(count - 1, selected_ + 1)); break;
    case Key::Up: select(std::max(0, selected_ < 0 ? 0 : selected_ - 1)); break;
    case Key::Home: select(0); break;
    case Key::End: select(count - 1); break;
    case Key::Enter:
      if (selected_ >= 0 && onActivate_) {
        const FileEntry entry = entries_[static_cast<size_t>(selected_)];
        onActivate_(entry);
        break;
      }
      return;
    default: return;
  }
  e.markHandled();
}

// ---- the dialog -------------------------------------------------------------------------------------

namespace {

struct DialogData {
  FilePathOptions options;
  std::function<void(const fs::path&)> chosen;
  UiContext* ui = nullptr;
  DialogHandle handle;
  core::tree::WidgetId input, list, message, accept;
  fs::path result;
  std::string armed;  // the path whose overwrite was announced
  bool done = false;
};

void say(DialogData& d, const std::string& text, bool problem) {
  if (Label* label = d.ui->objectAs<Label>(d.message)) {
    label->setText(text);
    label->setColorToken(problem ? "warning-action" : "muted");
  }
}

void setAcceptText(DialogData& d, const std::string& text) {
  if (Button* button = d.ui->objectAs<Button>(d.accept)) button->setText(text);
}

std::string typedText(DialogData& d) {
  const TextInput* input = d.ui->objectAs<TextInput>(d.input);
  return input != nullptr ? input->text() : std::string();
}

// Validates, announces an overwrite on the first press, otherwise ends the dialog with "ok".
void tryAccept(const std::shared_ptr<DialogData>& data) {
  DialogData& d = *data;
  if (d.done) return;
  const FilePathCheck check = checkFilePath(d.options, typedText(d));
  if (!check.ok) {
    say(d, check.message, true);
    return;
  }
  const std::string key = utf8Of(check.path);
  if (d.options.mode == FilePathMode::Save && check.exists && d.armed != key) {
    d.armed = key;
    setAcceptText(d, "Overwrite");
    say(d, "That file exists. Press Overwrite to replace it.", true);
    return;
  }
  if (d.options.mode == FilePathMode::Save) {
    std::error_code ec;
    const fs::path parent = check.path.parent_path();
    if (!parent.empty()) fs::create_directories(parent, ec);
    if (ec) {
      say(d, "The folder could not be created: " + ec.message(), true);
      return;
    }
  }
  d.result = check.path;
  d.done = true;
  closeDialog(*d.ui, d.handle, "ok");
}

}  // namespace

DialogHandle openFilePathDialog(UiContext& ui, FilePathOptions options, std::function<void(const fs::path&)> onChosen) {
  auto data = std::make_shared<DialogData>();
  data->ui = &ui;
  if (options.actionLabel.empty()) options.actionLabel = options.mode == FilePathMode::Open ? "Open" : "Save";
  if (options.title.empty()) options.title = options.mode == FilePathMode::Open ? "Open file" : "Save file";
  data->options = options;
  data->chosen = std::move(onChosen);
  if (options.mode == FilePathMode::Save && !options.folder.empty()) {
    std::error_code ec;
    fs::create_directories(options.folder, ec);  // the listing and the first save need it; a failure shows when saving
  }

  DialogSpec spec;
  spec.title = options.title;
  spec.description = options.description;
  spec.actions = {{"cancel", "Cancel", DialogActionKind::Neutral, false, true, true}};
  spec.owner = options.owner;
  spec.width = options.width;
  spec.onResult = [data](const DialogResult& result) {
    DialogData& d = *data;
    d.handle = {};
    if (result.action == "ok" && d.done && d.chosen) {
      const fs::path path = d.result;
      d.chosen(path);
    }
  };
  data->handle = openDialog(ui, std::move(spec));
  if (!data->handle.valid()) return {};

  const core::tree::WidgetId body = data->handle.body;
  SectionBox& column = ui.create<SectionBox>(body);
  column.style().direction = layout::FlexDirection::Column;
  column.style().gapRow = 8.0;
  column.style().flexShrink = 0.0;

  ui.create<Label>(column.id(), "Folder: " + (options.folder.empty() ? std::string("(type a full path)") : utf8Of(options.folder)), LabelRole::Muted);
  TextInput& input = ui.create<TextInput>(column.id());
  input.setPlaceholder(options.mode == FilePathMode::Open ? "File name or full path" : "File name or full path");
  input.setMaxLength(kMaxPathChars);
  input.setAccessibleName("File path");
  input.setText(options.initialName);
  data->input = input.id();
  ui.create<Label>(column.id(), options.extension.empty() ? "Files in this folder" : "Files in this folder (" + options.extension + ")", LabelRole::Caption);
  FileListView& list = ui.create<FileListView>(column.id());
  list.style().flexGrow = 0.0;
  list.style().height = layout::Length::px(150.0);
  list.setEntries(listFiles(options.folder, options.extension));
  data->list = list.id();
  Label& message = ui.create<Label>(column.id(), "", LabelRole::Muted);
  message.style().minHeight = layout::Length::px(18.0);
  data->message = message.id();
  SectionBox& footer = ui.create<SectionBox>(column.id());
  footer.style().direction = layout::FlexDirection::Row;
  footer.style().justifyContent = layout::Justify::End;
  Button& accept = ui.create<Button>(footer.id(), options.actionLabel, ButtonTone::Accent, ButtonSize::Md);
  data->accept = accept.id();

  // Any edit of the field forgets an announced overwrite; a row copies its name into the field.
  std::weak_ptr<DialogData> weak = data;
  input.setOnTextChanged([weak](std::string_view) {
    const auto d = weak.lock();
    if (!d) return;
    if (!d->armed.empty()) {
      d->armed.clear();
      setAcceptText(*d, d->options.actionLabel);
    }
    say(*d, "", false);
  });
  input.setOnCommitted([weak, field = input.id()](std::string_view) {
    const auto d = weak.lock();
    // A commit caused by leaving the field (clicking Cancel) must not accept: the focus has moved on.
    if (d && !d->done && d->ui->router().focused() == field) tryAccept(d);
  });
  list.setOnSelect([weak](const FileEntry& entry) {
    const auto d = weak.lock();
    if (!d) return;
    if (TextInput* field = d->ui->objectAs<TextInput>(d->input)) field->setText(entry.name);
  });
  list.setOnActivate([weak](const FileEntry& entry) {
    const auto d = weak.lock();
    if (!d) return;
    if (TextInput* field = d->ui->objectAs<TextInput>(d->input)) field->setText(entry.name);
    tryAccept(d);
  });
  accept.setOnClick([weak] {
    if (const auto d = weak.lock()) tryAccept(d);
  });
  ui.focusWidget(input.id(), core::events::FocusReason::Keyboard);
  input.selectAll();
  return data->handle;
}

}  // namespace r1ui::widgets
