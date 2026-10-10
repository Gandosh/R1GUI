// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: the tests of the in-toolkit file path dialog: the path rules (empty, hostile characters, invalid
//   UTF-8, 10 000 characters, device names, drive-less roots, extension appended, relative names resolved
//   in the default folder, directories, missing files), the listing (extension filter, case-insensitive
//   order, missing folder), size formatting, and the dialog itself with synthetic input (save a new name,
//   the overwrite needs a second Enter, open a missing file is refused with a message, a row copies its
//   name, Enter on a row chooses it, Escape never calls back, the callback runs once).
// Callers: CTest (label fast).
#include <filesystem>
#include <fstream>
#include <string>

#include "TestSupport.h"
#include "r1ui/widgets/filepath/FilePathDialog.h"
#include "r1ui/widgets/label/Label.h"
#include "r1ui/widgets/textinput/TextInput.h"

namespace {

using namespace r1ui::widgets;
namespace fs = std::filesystem;
using r1ui::core::events::Key;
using r1ui::core::tree::WidgetId;

struct TempFolder {
  TempFolder() {
    static int counter = 0;
    path = fs::temp_directory_path() / ("r1ui-filepath-test-" + std::to_string(++counter) + "-" + std::to_string(reinterpret_cast<uintptr_t>(this) & 0xfffff));
    std::error_code ec;
    fs::remove_all(path, ec);
    fs::create_directories(path, ec);
  }
  ~TempFolder() {
    std::error_code ec;
    fs::remove_all(path, ec);
  }
  void write(const std::string& name, const std::string& text = "x") const { std::ofstream(path / name, std::ios::binary) << text; }
  fs::path path;
};

FilePathOptions options(FilePathMode mode, const fs::path& folder) {
  FilePathOptions o;
  o.mode = mode;
  o.folder = folder;
  o.extension = ".r1mn";
  return o;
}

void testPathRules() {
  TempFolder dir;
  dir.write("exists.r1mn");
  fs::create_directories(dir.path / "sub");
  FilePathOptions save = options(FilePathMode::Save, dir.path);
  FilePathOptions open = options(FilePathMode::Open, dir.path);

  FilePathCheck c = checkFilePath(save, "");
  R1_EXPECT(!c.ok && !c.message.empty());
  R1_EXPECT(!checkFilePath(save, "   \t ").ok);
  c = checkFilePath(save, "new menu");
  R1_EXPECT(c.ok && !c.exists && c.path == dir.path / "new menu.r1mn");   // relative name, extension appended
  c = checkFilePath(save, "new.MENU");
  R1_EXPECT(c.ok && c.path.filename() == "new.MENU");                    // another extension is kept
  c = checkFilePath(save, "exists");
  R1_EXPECT(c.ok && c.exists);
  c = checkFilePath(save, (dir.path / "abs").string());
  R1_EXPECT(c.ok && c.path == dir.path / "abs.r1mn");
  R1_EXPECT(checkFilePath(save, "sub").ok);                               // "sub" gets the extension: sub.r1mn is a new file
  R1_EXPECT(!checkFilePath(save, "sub/").ok);                             // a trailing separator names a folder
  fs::create_directories(dir.path / "taken.r1mn");
  R1_EXPECT(!checkFilePath(save, "taken").ok && !checkFilePath(save, "taken.r1mn").ok);  // an existing folder is not a file

  for (const char* bad : {"a<b", "a>b", "a\"b", "a|b", "a?b", "a*b", "a\x01" "b", "x:y", "C:", "CON", "nul", "COM1.r1mn", "lpt9", "name."}) {
    R1_EXPECT(!checkFilePath(save, bad).ok);
  }
  R1_EXPECT(checkFilePath(save, "name ").ok);                             // trailing blanks are trimmed first
  R1_EXPECT(!checkFilePath(save, "C:rel\\x").ok);                         // drive-relative paths are not accepted
  R1_EXPECT(!checkFilePath(save, "\\rooted\\x").ok);
  R1_EXPECT(!checkFilePath(save, std::string("bad\xff\xfe.r1mn")).ok);    // invalid UTF-8
  R1_EXPECT(!checkFilePath(save, std::string(10000, 'a')).ok);            // far over the limit
  R1_EXPECT(!checkFilePath(save, std::string(kMaxPathChars + 1, 'a')).ok);
  R1_EXPECT(checkFilePath(save, std::string(60, 'a')).ok);

  // Open: needs an existing regular file.
  R1_EXPECT(checkFilePath(open, "exists").ok && checkFilePath(open, "exists").exists);
  R1_EXPECT(!checkFilePath(open, "missing").ok);
  R1_EXPECT(!checkFilePath(open, "sub").ok);
  R1_EXPECT(!checkFilePath(open, "").ok);
  FilePathOptions noFolder = options(FilePathMode::Save, {});
  R1_EXPECT(!checkFilePath(noFolder, "relative").ok && checkFilePath(noFolder, (dir.path / "abs").string()).ok);
  FilePathOptions anyExtension = options(FilePathMode::Save, dir.path);
  anyExtension.extension.clear();
  R1_EXPECT(checkFilePath(anyExtension, "plain").path.filename() == "plain");
}

void testListing() {
  TempFolder dir;
  dir.write("b.r1mn", "12345");
  dir.write("A.r1mn");
  dir.write("c.R1MN");
  dir.write("note.txt");
  fs::create_directories(dir.path / "folder.r1mn");  // a folder named like a menu file is not listed
  const std::vector<FileEntry> files = listFiles(dir.path, ".r1mn");
  R1_EXPECT(files.size() == 3 && files[0].name == "A.r1mn" && files[1].name == "b.r1mn" && files[2].name == "c.R1MN" && files[1].bytes == 5);
  R1_EXPECT(listFiles(dir.path, "").size() == 4);
  R1_EXPECT(listFiles(dir.path / "missing", ".r1mn").empty() && listFiles({}, ".r1mn").empty());
  R1_EXPECT(formatFileSize(0) == "0 B" && formatFileSize(1023) == "1023 B" && formatFileSize(1536) == "1.5 KB" && formatFileSize(3u * 1024u * 1024u) == "3.0 MB");
}

// ---- the dialog ---------------------------------------------------------------------------------------

struct DialogRig {
  DialogRig() : t(900, 700) {}
  void settle() {
    for (int i = 0; i < 3; ++i) {
      t.ui.setTime(t.ui.now() + 50);
      t.ui.tick();
      t.layout();
    }
  }
  void key(Key k) {
    t.ui.keyDown(k);
    t.ui.keyUp(k);
    settle();
  }
  void type(const std::string& text) {
    t.ui.keyDown(static_cast<Key>('A'), r1ui::core::events::Mod::kCtrl);
    t.ui.keyUp(static_cast<Key>('A'), r1ui::core::events::Mod::kCtrl);
    for (const char c : text) t.ui.textInput(static_cast<char32_t>(static_cast<unsigned char>(c)));
    settle();
  }
  template <class T>
  T* find() {
    T* found = nullptr;
    t.ui.tree().forEachDescendant(t.ui.root(), [&](WidgetId id) {
      if (found == nullptr) found = t.ui.objectAs<T>(id);
    }, true);
    return found;
  }
  std::string messageText() {
    std::string text;
    t.ui.tree().forEachDescendant(t.ui.root(), [&](WidgetId id) {
      if (Label* label = t.ui.objectAs<Label>(id); label != nullptr && label->text().find("exists") != std::string::npos) text = label->text();
    }, true);
    return text;
  }
  r1test::TestUi t;
};

void testSaveDialog() {
  TempFolder dir;
  dir.write("old.r1mn");
  DialogRig r;
  int calls = 0;
  fs::path chosen;
  FilePathOptions o = options(FilePathMode::Save, dir.path / "fresh");  // a folder that does not exist yet
  o.initialName = "suggestion";
  const DialogHandle handle = openFilePathDialog(r.t.ui, o, [&](const fs::path& p) {
    ++calls;
    chosen = p;
  });
  r.settle();
  R1_EXPECT(handle.valid() && isDialogOpen(r.t.ui, handle));
  TextInput* input = r.find<TextInput>();
  R1_EXPECT(input != nullptr && input->text() == "suggestion" && r.t.ui.router().focused() == input->id());
  r.type("my menu");
  R1_EXPECT(input->text() == "my menu");
  r.key(Key::Enter);
  R1_EXPECT(calls == 1 && chosen == dir.path / "fresh" / "my menu.r1mn" && !isDialogOpen(r.t.ui, handle));
  R1_EXPECT(fs::is_directory(dir.path / "fresh"));  // the folder was created for the save
  R1_EXPECT(r.t.ui.inputFaults() == 0);
}

void testOverwriteNeedsSecondPress() {
  TempFolder dir;
  dir.write("old.r1mn");
  DialogRig r;
  int calls = 0;
  const DialogHandle handle = openFilePathDialog(r.t.ui, options(FilePathMode::Save, dir.path), [&](const fs::path&) { ++calls; });
  r.settle();
  r.type("old");
  r.key(Key::Enter);
  R1_EXPECT(calls == 0 && isDialogOpen(r.t.ui, handle));
  R1_EXPECT(!r.messageText().empty());                         // "That file exists. Press Overwrite..."
  r.type("old2");                                               // editing forgets the announcement
  r.key(Key::Enter);
  R1_EXPECT(calls == 1 && !isDialogOpen(r.t.ui, handle));
  // Same file again: announce, then accept.
  calls = 0;
  const DialogHandle again = openFilePathDialog(r.t.ui, options(FilePathMode::Save, dir.path), [&](const fs::path&) { ++calls; });
  r.settle();
  r.type("old");
  r.key(Key::Enter);
  r.key(Key::Enter);
  R1_EXPECT(calls == 1 && !isDialogOpen(r.t.ui, again));
}

void testOpenDialog() {
  TempFolder dir;
  dir.write("alpha.r1mn", "one");
  dir.write("beta.r1mn", "two");
  DialogRig r;
  int calls = 0;
  fs::path chosen;
  const DialogHandle handle = openFilePathDialog(r.t.ui, options(FilePathMode::Open, dir.path), [&](const fs::path& p) {
    ++calls;
    chosen = p;
  });
  r.settle();
  FileListView* list = r.find<FileListView>();
  TextInput* input = r.find<TextInput>();
  R1_EXPECT(list != nullptr && input != nullptr && list->entries().size() == 2);
  r.type("nothing");
  r.key(Key::Enter);
  R1_EXPECT(calls == 0 && isDialogOpen(r.t.ui, handle));                       // a missing file is refused, the dialog stays
  // A click on the second row copies its name into the field.
  const auto row = list->rowRect(1);
  R1_EXPECT(row.w > 0);
  r.t.ui.pointerMove(row.x + 20, row.y + 10);
  r.t.ui.pointerDown(row.x + 20, row.y + 10);
  r.t.ui.pointerUp(row.x + 20, row.y + 10);
  r.settle();
  R1_EXPECT(input->text() == "beta.r1mn" && list->selected() == 1);
  // Enter on the focused list chooses the row.
  r.key(Key::Up);
  R1_EXPECT(list->selected() == 0 && input->text() == "alpha.r1mn");
  r.key(Key::Enter);
  R1_EXPECT(calls == 1 && chosen == dir.path / "alpha.r1mn" && !isDialogOpen(r.t.ui, handle));
}

void testEscapeAndHostile() {
  TempFolder dir;
  DialogRig r;
  int calls = 0;
  const DialogHandle handle = openFilePathDialog(r.t.ui, options(FilePathMode::Save, dir.path), [&](const fs::path&) { ++calls; });
  r.settle();
  r.type(std::string(2000, 'q'));                // the field limits the length; a long text is refused or cut, never accepted blindly
  TextInput* input = r.find<TextInput>();
  R1_EXPECT(input != nullptr && input->text().size() <= kMaxPathChars);
  r.type("a|b");
  r.key(Key::Enter);
  R1_EXPECT(calls == 0 && isDialogOpen(r.t.ui, handle));
  r.key(Key::Escape);
  R1_EXPECT(calls == 0 && !isDialogOpen(r.t.ui, handle));
  R1_EXPECT(r.t.ui.overlays().stack().empty());

  // Destroying the owner's context state while the dialog is open: the dialog closes with the overlays.
  const DialogHandle open = openFilePathDialog(r.t.ui, options(FilePathMode::Open, dir.path), [&](const fs::path&) { ++calls; });
  r.settle();
  r.t.ui.overlays().closeAll();
  r.settle();
  R1_EXPECT(open.valid() && !isDialogOpen(r.t.ui, open) && calls == 0);
}

}  // namespace

int main() {
  testPathRules();
  testListing();
  testSaveDialog();
  testOverwriteNeedsSecondPress();
  testOpenDialog();
  testEscapeAndHostile();
  return r1test::finish();
}
