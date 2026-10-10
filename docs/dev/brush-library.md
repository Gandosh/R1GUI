# The brush library (ui-commands/brushes, ui-widgets/brushes; slices 5.20 to 5.23)

Developer guide to the brush library: press **B** anywhere and a popup lists the brushes as tiles; typing the first letters of a name narrows the list, and the letter shown on a tile picks it. Owner requirement (2026-10-10): "pressing B opens the brush library anywhere in the scene, so people can access their brush quickly by typing its letter." The toolkit is host neutral: it knows brushes as data (`BrushInfo`) and calls the host back with an id; the preview wires a sample set (section 9).

| Piece | Folder | Depends on | Owns |
|---|---|---|---|
| headless model | `source/ui-commands/{include/r1ui/commands,src}/brushes` | `r1ui::commands`, `r1ui::core` | `BrushInfo`, `BrushLibraryModel` (sanitising, favourites, recents, user letters, keys, badges, conflicts, `query`), `BrushLetters.h` (UTF-8, folding), `BrushState.h` (the user's file) |
| widgets | `source/ui-widgets/{include/r1ui/widgets,src}/brushes` | the widget library, `r1ui::commands` | `BrushLibraryPopup` (the overlay content), `BrushLibraryController` (command, windows, focus), `BrushThumbnailSource` (pictures), `buildGalleryBrushes` |
| sample wiring | `examples/preview/editor/EditorBrush*.{h,cpp}` | all of the above | the sample brushes, procedural pictures, the Editor's status line and viewport |
| tests | `tests/ui-commands/brushes`, `tests/ui-widgets/brushes`, `tests/preview` | | section 11 |

## 1. What the user sees and does

Press **B** (command `brush.library`, context `global`, rebindable in the hotkey editor like every command). A popup opens on the overlay layer of the window that had the key press, centred on the pointer (on the window centre when the pointer is outside), kept inside the window and shrunk to fit a small window. It holds a search field with the mode switch, category chips, the tiles and a footer. A tile shows the brush picture (its icon until the picture exists), the name, a **quick letter badge**, a star for favourites, an accent outline when it is the active brush.

| Input | Effect |
|---|---|
| a letter or digit (type-to-pick) | narrows to the brushes whose name starts with it (case and accents ignored); each remaining tile shows the **next letter** that picks it; with the option on, a single match is picked at once |
| Backspace / Ctrl+Backspace / Delete | removes one character / clears the text / clears the text |
| Enter | picks the highlighted tile |
| arrows, Home, End, PageUp, PageDown | move the highlight in the grid (also across the Recent section) |
| Tab / Shift+Tab | switches between **type to pick** and **search names** (the text is searched anywhere in the name; no hints, never picks by itself) |
| Ctrl+Left / Ctrl+Right | previous / next category chip |
| Ctrl+F | stars or unstars the highlighted brush |
| F2 | opens the **Assign letter** popover for the highlighted brush |
| Escape | closes the Assign letter popover or the tile menu first; otherwise closes the library without picking |
| B (the key that opened it) | closes the library when nothing is typed (see 4.2) |
| mouse | hover highlights, click picks, a click on the star toggles the favourite, a right click opens the tile menu (Pick, favourite, Assign letter, Clear letter), the wheel scrolls (over the chips: scrolls them), chips, the mode switch and the option are clickable, a click outside closes |

The footer shows the key hints (two lines), the number of matches and the **Pick on unique match** option (default on, saved).

While the library is open it owns the keyboard and the pointer of its window: no application command can fire (the popup is a modal overlay that holds the focus), and a click outside only closes it.

## 2. The quick letter algorithm (`BrushLibraryModel`)

**Key of a brush.** The folded letters and digits of its name, in order, spaces and punctuation skipped, at most 64 characters. "Clay Buildup" is `claybuildup`; "3D Pen" is `3dpen`; "***" has an empty key. An optional single-letter **override** (the user's, else the host's `BrushInfo::letter`) takes the first position of the key, so a brush with the override X answers to X and no longer to its own first letter.

**Folding** (`BrushLetters.h`): ASCII, Latin-1 and Latin Extended-A letters fold to lower case, Latin-1 accents are dropped (e with an acute accent types as e), Greek and Cyrillic fold to lower case; other scripts are kept as they are. CJK, kana, Hangul, Arabic, Hebrew and Armenian letters count as key characters by block. This is an approximation of Unicode case folding, not the full table (status: Approximate). Invalid UTF-8 decodes to U+FFFD, one byte at a time, and is never a key character.

**Badge** (the letters on a tile): the shortest prefix of the key that no other *enabled* brush shares, computed from the neighbours in key order. When no prefix is unique (two brushes with the same key, or one key is the start of another: "Clay" and "Clay Buildup") the badge is the whole key and the tile shows an Enter symbol: that brush is picked with Enter.

**Typing.** `query({TypeToPick, text})` keeps the enabled brushes whose key starts with the typed key. Each remaining tile reports `nextLetter` (the key character after the typed prefix), `nextUnique` (typing it leaves exactly this brush) and `exact` (its key equals the prefix). The popup draws the typed part dim and the next letter in a pill, accent filled when `nextUnique`. The **pick on unique match** rule is the pure function `pickOnUnique(result, option)`: it returns the only matching brush when the option is on.

**Order** (a pure function of the data): with nothing typed, the Recent section (up to 8, only without a category filter) and then the favourites, then the rest, alphabetical by folded name. With a prefix: the exact match first, then brushes with a letter of their own (override), then favourites, then alphabetical; the position in the host's list breaks every remaining tie. The default highlight is the active brush when nothing is typed, else the first match.

**Conflicts.** Two enabled brushes with the same non-empty key cannot be told apart by typing: `conflicts()` lists them (best tile first), `unkeyableCount()` counts the brushes whose key is empty (reachable by arrows, search and mouse only). The Assign letter popover reports before it assigns how many other brushes start with the letter (`sharingLetter`): typing it will list that many plus one.

**Disabled brushes** (`BrushInfo::enabled == false`) are shown dimmed while browsing and searching, never matched by typing, never counted in uniqueness and cannot be picked (the footer says so).

## 3. Validation of the host's data (hostile input)

`setBrushes` sanitises at the boundary and never throws: an empty or duplicate id (the first wins) and anything past 20 000 brushes is rejected with an issue; ids are cut at 128 bytes, names at 256 (an issue says so; the library shows the id for an empty name), control characters (NUL included) become spaces, invalid UTF-8 becomes U+FFFD, an icon name that is not valid is dropped, a letter override that is not exactly one letter or digit is dropped. At most 64 issues are kept. Typed text is bounded (128 bytes, 64 key characters); a query on a megabyte of text is cut at 4096 bytes.

## 4. Behaviour decisions where the brief was open

1. **Unmatched brushes are hidden**, not dimmed, in the popup (positions would be stable but 2000 dimmed tiles are not usable); `QueryRequest::hideUnmatched = false` gives the dimmed variant and is tested.
2. **B closes the library only before anything is typed** (type-to-pick mode, empty text), or at any time when the opening chord is not a plain letter or digit (F4, Ctrl+B). Reason: after the first letter B is part of a name. Consequence: a brush whose name starts with the opening key's letter ("Blob") is reached with **Shift+B** (a different chord, so it types the letter), with the search mode (Tab), with a letter override, or with the arrows. In the search mode B is text.
3. **The character produced by the opening key press is ignored**, and so are its repeats while the key is held (the platform delivers a character after the key down and repeats it). Any other key press or a key up ends the ignoring.
4. **Recent** are the last 8 picks (a pick moves a brush to the front); they appear above the list only when nothing is typed and no category is chosen. The same brush also stays in the main list.
5. **Quick letters are case insensitive and accent free; a user override replaces the first letter** (it does not add an alias), so there is never an ambiguity between "what the name says" and "what the user set".
6. **A pick is recorded and the library closes before the host's handler runs**; the host decides what "active" means and tells the model (`setActiveId`), which draws the outline. The controller itself never changes the active brush.
7. **The command toggles** when invoked through a menu, toolbar or the API while the library is open (it closes); in another window than the one it is open in, the key closes the library there (one library at a time).
8. **Type-ahead widgets keep their letters.** A widget that takes typed text (text fields, number fields, selects, and the preview's Assets grid and Actions list) swallows plain letters before the router looks, like every single-letter command (V, W, E, R). The Outliner, the viewport, the dock and floating windows do not.

## 5. Host API

```cpp
// 5.20: data and decisions
BrushLibraryModel model;                                   // single thread
model.setBrushes(std::vector<BrushInfo>{...});             // sanitised; returns accepted, rejected, issues
model.setActiveId("clay");                                 // outline in the popup; not persisted
QueryResult q = model.query({QueryMode::TypeToPick, "cl"}); // ordered tiles with hints (also used by tests and tools)
model.setUserLetter("pinch", "x");                         // LetterResult{ok, sharedWith, message}
BrushStateStorage storage(model, FileTextStore(path));     // load() at start; saves after every persisted change
storage.load();

// 5.21: the widget side
BrushLibraryController library(mainUi, services, model, [&](const std::string& id) { /* select brush id */ });
library.setThumbnails(&provider, &sink);                   // optional, section 7
auto tap = library.tapKeys(window.ui, window.keyHandler);   // for EVERY window: records which window a key came from
window.ui.setGlobalKeyHandler(tap.get());                  // (instead of the handler itself)
```

`BrushLibraryController` registers the command (`BrushLibraryOptions` changes id, label, icon, default chord, size, or turns the registration off), opens the popup in the window of the key press (menu and API invocations use the primary window), keeps one library open, restores focus (a command that moved the focus wins), closes it when the window loses activation or is destroyed, and removes the command in its destructor. Destroy it before the registry and the model.

`BrushLibraryPopup` can also be created directly as a widget (the gallery does) with `BrushPopupHooks{onPick, onClose, isOpenChord, openChordText, onDetached}`; the test accessors (`result()`, `highlight()`, `tileRect(i)`, `starRect(i)`, `chipRect(i)`, `modeRect()`, `optionRect()`, `assigning()`, `menuOpen()`, `hint()`) are public.

## 6. Keys, focus and windows

The command is an ordinary Action in the `global` context, so the existing router decides when it fires: a text field (context `text`) swallows plain letters first, any other focus (viewport, outliner, a dock tab, nothing) reaches `global`. Native floating windows have a `UiContext` each; the host wraps every window's global key handler with `tapKeys()` and the popup opens in that window's overlay layer, bounded by it (popups cannot leave their window, `docs/dev/widgets.md` section 11). While open the popup is a modal overlay without focus trap: `UiContext::onGlobalKey` withholds every key from the application's shortcuts, the focused popup takes keys and characters, Tab is not trapped (it switches the search mode), a press outside closes it and is not delivered. The host of the popup is made visible at once (it is placed before the first paint anyway) so that keys arriving in the same input batch as the opening key already reach it.

## 7. Pictures (slice 5.22)

`BrushThumbnailSource` reuses the asset browser's machinery (`thumbnailgrid/ThumbnailCache.h`): a `ThumbnailProvider` the host implements (`produce(key, size, out)` returns Ready, Pending or Failed and must return quickly), a `ThumbnailTextureSink` (`GpuThumbnailTextures` on the window's device, `MemoryThumbnailTextures` in tests), an LRU cache bounded by entries (1024) and bytes (64 MiB), and a scheduler that asks for the visible tiles first within 2.5 ms per paint (at most 16 pictures per pass, so the library opens within a frame with icons and the pictures arrive over the next few frames), then two rows on each side. A picture is identified by `BrushLibraryModel::thumbnailKey(index)` (a hash of `thumbnailKey` or the id; equal sources share a key, different ones never do) and the provider maps it back with `indexOfThumbnailKey`. The tile shows the icon until the picture exists; a Failed picture keeps the icon until `invalidate(key)`; a provider that works on other threads returns Pending and calls `notifyReady(key)` from the UI thread (a provider that stays Pending without telling keeps the popup asking for frames: do not do that). The cache belongs to the controller, so reopening the library shows the pictures at once. The popup pumps at the start of its paint, so no frame still reads a texture that gets evicted.

## 8. The file (`brushes.json`)

`{"format":"r1ui-brush-library","version":1,"pickOnUnique":true,"favourites":["id",...],"recents":["id",...],"letters":[{"brush":"id","letter":"x"},...]}`, written deterministically through `customize::TextStore` (temporary file, atomic rename). Parsing is strict (`parseBrushState`): a file over 4 MiB, invalid JSON (invalid UTF-8, duplicate keys, nesting over 6 included), another format, a missing or newer version or a member of the wrong type is rejected as a whole, changes nothing and is moved aside as `brushes.json.corrupt-N`; single entries (a bad id, a duplicate, a letter that is not one letter or digit, entries past the limits: 20 000 favourites and letters, 8 recents) are skipped with an issue. Ids of brushes the host does not list now are kept, so a brush missing for one session keeps its star and its letter. `BrushStateStorage` saves after every change of the persisted state (not after a load, a new brush list or the active brush).

## 9. The preview (slice 5.23)

`examples/preview/editor/EditorBrushes.cpp` defines 38 generic sculpt-style brushes in six categories (Sculpt, Smooth, Move, Mask, Surface, Cut) with names that share first letters on purpose (S: Standard, Smooth, Snake Hook, Slash, Slice, Scrape, Stamp, Soften, Surface Noise, Smooth Stronger), and `SampleBrushThumbnails`, a provider that draws a picture per brush by code (soft falloff, disc, ring, 3x3 dots, stroke, hatching, ring with dot, wedge; colour by category). `EditorBrushLibrary.cpp` wires the controller: the Tools menu and the main toolbar get the command, the pick sets the status line (`Brush: <name>` at the left for the pick, always at the right), the viewport label and the cursor ring (Ctrl + wheel over the viewport changes its size), the file is `%LOCALAPPDATA%\R1GUI\preview\brushes.json`, the active brush of the next start is the last pick (else Standard), and the key taps of the main window and of every floating window go through `tapKeys`. The Gallery has a Brushes page (`buildGalleryBrushes`). The hotkey editor and the Actions list show the command (a test asserts every command has a description).

## 10. Performance and idle

Measured with `ui-commands.brush_query_test` and `ui-widgets.brushes.brush_popup_test` (RelWithDebInfo): a query over 2000 brushes takes well below a millisecond (the test prints the timing and asserts under 8 ms), rebuilding the derived data for a new list of 2000 about 4 ms, two typed letters including the layout of the result 0.2 ms. The popup draws only the visible tiles (under 60 for 2000 brushes). Key to first drawn frame with 2000 brushes and pictures, measured by `ui-widgets.brushes.gallery_gpu_test` on the RTX 4080 (offscreen, the frame includes the fence wait): **0.8 ms** for every opening after the first; the very first opening of a session takes about **21 ms** (3.3 ms for the key, the router, the command, the popup and the query, 17 ms for the first frame, which rasterises the glyphs and icons of the popup and uploads 16 pictures; in the preview most glyphs of the UI sizes are in the atlas already). Opening needs no per-brush work beyond the query and the first picture batch. An open popup with all pictures present asks for no frame and starts no timer; a closed library costs nothing. The real-desktop figures (key to first drawn frame) are in the evidence of slice 5.23.

## 11. Tests

| Test | Label | Proves |
|---|---|---|
| `ui-commands.brush_letters_test` | fast | decoding of hostile bytes, folding idempotent over U+0000..U+2FFFF, keys of names with digits, symbols, Cyrillic, invalid UTF-8, NUL, 10 000 characters |
| `ui-commands.brush_library_model_test` | fast | sanitising (duplicate ids, empty names, huge names, NUL), favourites, recents, letters, conflicts, picture keys, notifications |
| `ui-commands.brush_query_test` | fast | the algorithm on the owner's names, order and tie-breaks, Recent, search, hostile text, 40 seeded random libraries (every brush reachable by typing its key, truthful hints, deterministic order), 2000-brush timing |
| `ui-commands.brush_state_test` | fast | golden file, round trip, 17 hostile files, entry repair, limits, atomic file store, corrupt file moved aside, autosave rules |
| `ui-widgets.brushes.brush_tile_layout_test` | fast | the grid geometry including zero, NaN and huge inputs |
| `ui-widgets.brushes.brush_controller_test` | fast | open and close paths, placement near edges, focus, B in a text field, the opening key's character and repeats, rebinding, native-window case and window destruction, keyboard isolation, destruction while open, 4000 random events |
| `ui-widgets.brushes.brush_popup_test` | fast | every key path, the mouse, chips, star, Assign letter, tile menu, scrolling and virtualisation with 2000 brushes, hostile brushes |
| `ui-widgets.brushes.brush_thumbnails_test` | fast | only visible tiles ask the provider, Pending/Failed, bounded cache, reuse on reopen, idle |
| `ui-widgets.brushes.gallery_test`, `gallery_gpu_test` | fast, gpu | the gallery page; both themes rendered (gallery and the live library with pictures) |
| `preview.editor`, `preview.editor_float_native` | fast, gpu | the wiring in the Editor, persistence, a floating window |

## 12. Not implemented or approximate

- Folding is not full Unicode: no decomposition of other scripts, no locale rules; keys of scripts without case are matched exactly.
- The letters come from `TextInput` characters, so a layout whose character for a key is not a letter or digit cannot narrow; dead keys and IME composition are not handled (the composed character arrives as one character).
- Favourites are shown with the outline star icon (the icon set has no filled star), in the warning colour.
- The popup does not drag, resize or remember its position; no categories editing; no brush editing.
- Disabled-brush explanations are one sentence in the footer; no per-brush tooltips for conflicts.
- The Assets grid and the Actions list of the preview keep plain letters for their type-ahead (section 4, point 8).
