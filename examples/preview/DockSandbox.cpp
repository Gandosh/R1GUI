// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: implementation of the docking sandbox declared in DockSandbox.h.
// Invariants: pointer capture lives in the window (it reports Up/CaptureLost on every exit path),
//   and every path out of a drag state returns to State::Idle, so no drag outlives its button.
//   Escape during a tab drag cancels it: the layout is unchanged and the tab stays where it was
//   (owner decision D9, recommendation A).
#include "DockSandbox.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <utility>

#include "r1ui/core/CheckedCast.h"

namespace {

namespace dock = r1ui::dock;
using r1ui::platform::MouseButton;
using r1ui::platform::MouseEvent;
using r1ui::render::Painter;
using r1ui::theme::ThemeId;

struct Rgba8 {
  uint8_t r = 0;
  uint8_t g = 0;
  uint8_t b = 0;
  uint8_t a = 255;
};

constexpr double kMargin = 8.0;
constexpr double kStatusHeight = 24.0;
constexpr float kLabelPixels = 11.0f;
constexpr float kStatusPixels = 12.0f;
constexpr size_t kMaxLayoutFileBytes = size_t{4} * 1024 * 1024;
constexpr size_t kMaxStatusChars = 140;

// File names and paths reach the window title, which must be UTF-8: path::string() is in the ANSI
// code page on Windows, so a non-ASCII user name would produce invalid UTF-8.
std::string utf8Of(const std::filesystem::path& path) {
  const std::u8string text = path.u8string();
  return std::string(text.begin(), text.end());
}

// ---- Panels -----------------------------------------------------------------------------

struct PanelSpec {
  const char* title;
  const char* colorToken;
};

// Nine panels with distinct token colours; index + 1 is the panel id.
constexpr PanelSpec kPanels[] = {
    {"Hierarchy", "accent"},          {"Assets", "component"},     {"Viewport", "success"},
    {"Game", "warning-action"},       {"Inspector", "error"},      {"Materials", "code-tag"},
    {"Layers", "code-attribute"},     {"Console", "success-bg"},   {"Timeline", "code-number"},
};
constexpr dock::PanelId kPanelCount = static_cast<dock::PanelId>(std::size(kPanels));

std::vector<dock::PanelInfo> panelInfos() {
  std::vector<dock::PanelInfo> infos;
  for (dock::PanelId i = 0; i < kPanelCount; ++i) infos.push_back({i + 1, kPanels[i].title, true});
  return infos;
}

// Left stack of 2, large centre stack, right stack of 3, bottom stack of 2 (9 panels).
dock::Node defaultTree() {
  using dock::Axis;
  using dock::Node;
  return Node::split(Axis::Column, {Node::split(Axis::Row, {Node::stack({1, 2}, 0, 1.2), Node::stack({3, 4}, 0, 4.0), Node::stack({5, 6, 7}, 0, 1.4)}, 4.0),
                                    Node::stack({8, 9}, 0, 1.0)});
}

dock::DockLayout makeDefaultLayout() {
  dock::DockLayoutResult result = dock::DockLayout::create(panelInfos(), {}, defaultTree());
  if (!result.ok()) throw std::runtime_error("default dock layout is invalid: " + result.error);
  return std::move(*result.layout);
}

// ---- Colours ----------------------------------------------------------------------------

Rgba8 toRgba(const r1ui::theme::Color& c) { return {c.r, c.g, c.b, 255}; }

// Linear mix toward `other` by `amount` in [0,1]; tokens are opaque here so no alpha handling.
Rgba8 mix(Rgba8 base, Rgba8 other, double amount) {
  const auto lerp = [&](uint8_t a, uint8_t b) { return static_cast<uint8_t>(std::lround(a + (b - a) * amount)); };
  return {lerp(base.r, other.r), lerp(base.g, other.g), lerp(base.b, other.b), 255};
}

constexpr Rgba8 kWhite{255, 255, 255, 255};
constexpr Rgba8 kBlack{0, 0, 0, 255};
constexpr Rgba8 kPreviewOrange{255, 192, 128, 255};  // spec 02 rule 19 tint, drawn opaque

// Rectangle edges are rounded to whole pixels (the dock's look has always been pixel exact).
r1ui::render::Rect snapped(const dock::Rect& r) {
  const double x0 = std::round(r.x);
  const double y0 = std::round(r.y);
  const double x1 = std::round(r.x + std::max(0.0, r.w));
  const double y1 = std::round(r.y + std::max(0.0, r.h));
  return {static_cast<float>(x0), static_cast<float>(y0), static_cast<float>(x1 - x0), static_cast<float>(y1 - y0)};
}

r1ui::render::Color paintColor(Rgba8 c) { return r1ui::render::Color::fromRgba8(c.r, c.g, c.b, c.a); }

void paintFill(Painter& painter, const dock::Rect& r, Rgba8 color) { painter.fillRect(snapped(r), paintColor(color)); }

// Four thin rectangles forming an outline just inside `r`.
void paintOutline(Painter& painter, const dock::Rect& r, double thickness, Rgba8 color) {
  const double t = std::min({thickness, r.w / 2.0, r.h / 2.0});
  paintFill(painter, {r.x, r.y, r.w, t}, color);
  paintFill(painter, {r.x, r.y + r.h - t, r.w, t}, color);
  paintFill(painter, {r.x, r.y + t, t, r.h - 2 * t}, color);
  paintFill(painter, {r.x + r.w - t, r.y + t, t, r.h - 2 * t}, color);
}

// Panel-name label centred vertically in `tabRect`, clipped to it; light text on dark tabs and
// dark text on light ones.
void paintLabel(Painter& painter, preview::TextEngine& text, const std::string& label, const dock::Rect& tabRect, Rgba8 background) {
  const double luminance = 0.299 * background.r + 0.587 * background.g + 0.114 * background.b;
  const Rgba8 ink = luminance > 140.0 ? Rgba8{16, 16, 16, 255} : Rgba8{255, 255, 255, 255};
  const r1ui::render::Rect box = snapped(tabRect);
  const float baseline = box.y + text.baselineInBox(kLabelPixels, box.h);
  painter.pushClip(box);
  text.draw(painter, label, kLabelPixels, 400, box.x + 6.0f, baseline, paintColor(ink));
  painter.popClip();
}

double centreAlong(const dock::Rect& r, dock::Axis axis) { return axis == dock::Axis::Row ? r.x + r.w / 2.0 : r.y + r.h / 2.0; }
double along(dock::Point p, dock::Axis axis) { return axis == dock::Axis::Row ? p.x : p.y; }

// Topmost tab under the pointer, if any.
const dock::TabLayout* tabAt(const dock::LayoutResult& layout, dock::Point p) {
  for (size_t i = layout.stacks.size(); i-- > 0;) {
    const dock::StackLayout& stack = layout.stacks[i];
    if (!stack.bounds.contains(p)) continue;
    for (const dock::TabLayout& tab : stack.tabs) {
      if (tab.rect.contains(p)) return &tab;
    }
    return nullptr;  // the topmost stack under the pointer owns it
  }
  return nullptr;
}

}  // namespace

DockSandbox::DockSandbox(r1ui::platform::Window& window, const r1ui::theme::Tokens& tokens, std::filesystem::path layoutFile)
    : window_(window), tokens_(tokens), file_(std::move(layoutFile)), layout_(makeDefaultLayout()) {
  for (const PanelSpec& spec : kPanels) {
    if (!tokens_.color(ThemeId::Dark, spec.colorToken) || !tokens_.color(ThemeId::Light, spec.colorToken)) {
      throw std::runtime_error(std::string("tokens lack the colour \"") + spec.colorToken + "\" used by the docking sandbox");
    }
  }
  for (const char* name : {"canvas", "panel-secondary", "border", "panel-focus", "warning-action"}) {
    if (!tokens_.color(ThemeId::Dark, name) || !tokens_.color(ThemeId::Light, name)) {
      throw std::runtime_error(std::string("tokens lack the colour \"") + name + "\" used by the docking sandbox");
    }
  }
  setStatus("ready");
}

dock::Rect DockSandbox::mainRect() const {
  const double width = std::max(0.0, static_cast<double>(bounds_.w) - 2 * kMargin);
  const double height = std::max(0.0, static_cast<double>(bounds_.h) - 2 * kMargin - kStatusHeight);
  return {static_cast<double>(bounds_.x) + kMargin, static_cast<double>(bounds_.y) + kMargin, width, height};
}

void DockSandbox::setStatus(std::string text) {
  if (text.size() > kMaxStatusChars) {
    size_t cut = kMaxStatusChars;
    while (cut > 0 && (static_cast<unsigned char>(text[cut]) & 0xC0u) == 0x80u) --cut;  // keep whole characters
    text.resize(cut);
  }
  status_ = std::move(text);
}

std::string DockSandbox::title() const {
  return "Docking sandbox | drag tabs/splitters, middle-click closes, Esc cancels the drag | S save, L load, R reset, Tab next | " + status_;
}

std::string DockSandbox::nameOf(dock::PanelId panel) const {
  const dock::PanelInfo* info = layout_.panel(panel);
  return info != nullptr ? info->title : "?";
}

// ---- Layout file and reset --------------------------------------------------------------

void DockSandbox::resetLayout() {
  layout_ = makeDefaultLayout();
  state_ = State::Idle;
  setStatus("layout reset to the default");
}

void DockSandbox::saveLayout() {
  const std::string json = layout_.toJson();
  std::filesystem::path temp = file_;
  temp += ".tmp";
  {
    std::ofstream out(temp, std::ios::binary | std::ios::trunc);
    out.write(json.data(), r1ui::core::checkedCast<std::streamsize>(json.size()));
    out.flush();
    if (!out) {
      setStatus("Save failed: cannot write " + utf8Of(temp));
      return;
    }
  }
  std::error_code error;
  std::filesystem::rename(temp, file_, error);
  setStatus(error ? "Save failed: error " + std::to_string(error.value()) : "saved " + utf8Of(file_.filename()) + " (" + std::to_string(json.size()) + " bytes)");
}

void DockSandbox::loadLayout() {
  std::error_code error;
  const auto size = std::filesystem::file_size(file_, error);
  if (error) {
    setStatus("Load failed: no readable " + utf8Of(file_.filename()));
    return;
  }
  if (size > kMaxLayoutFileBytes) {
    setStatus("Load failed: file is too large");
    return;
  }
  // The size check above can be stale by now (the file may have grown): read at most the cap plus
  // one byte and treat more as too large.
  std::ifstream in(file_, std::ios::binary);
  std::string text(kMaxLayoutFileBytes + 1, '\0');
  in.read(text.data(), r1ui::core::checkedCast<std::streamsize>(text.size()));
  text.resize(r1ui::core::checkedCast<size_t>(in.gcount()));
  if (text.size() > kMaxLayoutFileBytes) {
    setStatus("Load failed: file is too large");
    return;
  }
  dock::LoadResult loaded = dock::DockLayout::fromJson(text, panelInfos());
  if (!loaded.ok()) {
    setStatus("Load failed (layout kept): " + loaded.error);
    return;
  }
  layout_ = std::move(*loaded.layout);
  state_ = State::Idle;
  setStatus("loaded " + utf8Of(file_.filename()) + (loaded.droppedPanels > 0 ? " (" + std::to_string(loaded.droppedPanels) + " unknown panels dropped)" : ""));
}

// ---- Input ------------------------------------------------------------------------------

bool DockSandbox::onKey(uint32_t virtualKey) {
  namespace keys = r1ui::platform::keys;
  switch (virtualKey) {
    case keys::kS: saveLayout(); return true;
    case keys::kL: loadLayout(); return true;
    case keys::kR: resetLayout(); return true;
    case keys::kEscape:
      if (state_ != State::TabDragging) return false;
      // Cancel: nothing was changed while dragging, so leaving the drag restores the original state.
      state_ = State::Idle;
      setStatus("drag cancelled, " + nameOf(panel_) + " stays where it was");
      return true;
    default: return false;
  }
}

std::string DockSandbox::describe(const dock::DropZone& zone, dock::PanelId panel) const {
  static const char* const sides[] = {"left", "right", "top", "bottom"};
  const std::string name = nameOf(panel);
  switch (zone.kind) {
    case dock::DropKind::Float: return name + " floated";
    case dock::DropKind::SplitStack: return name + " docked " + sides[static_cast<int>(zone.side)] + " of " + nameOf(zone.stackPanel);
    case dock::DropKind::JoinStack: return name + " joined the tabs of " + nameOf(zone.stackPanel) + " at slot " + std::to_string(zone.index);
    case dock::DropKind::SplitAreaEdge: return name + " docked at the " + sides[static_cast<int>(zone.side)] + " edge";
    case dock::DropKind::FillEmptyArea: return name + " docked into the empty area";
  }
  return name;
}

void DockSandbox::finishTabDrag(const dock::DropZone& zone) {
  const dock::Status status = layout_.dock(panel_, zone);
  setStatus(status ? describe(zone, panel_) : "Drop refused: " + status.error);
  state_ = State::Idle;
}

void DockSandbox::onMove(dock::Point p) {
  pointer_ = p;
  const dock::LayoutResult layout = layout_.computeLayout(mainRect());
  switch (state_) {
    case State::TabPressed:
      if (dock::dragExceedsThreshold(press_, p, layout_.config())) state_ = State::TabDragging;
      if (state_ != State::TabDragging) break;
      [[fallthrough]];
    case State::TabDragging: zone_ = layout_.hitTestDropZone(layout, {p, panel_, grab_}); break;
    case State::SplitterDragging: {
      // The handle follows the pointer's projection on the axis (spec 05 rule 13); the model clamps.
      const auto found = std::find_if(layout.handles.begin(), layout.handles.end(), [&](const dock::HandleLayout& h) { return h.handle == handle_; });
      if (found == layout.handles.end()) {
        state_ = State::Idle;
        break;
      }
      const dock::Status status = layout_.moveSplitter(handle_, along(p, handle_.axis) - handleGrab_ - centreAlong(found->rect, handle_.axis), mainRect());
      if (!status) setStatus("Resize refused: " + status.error);
      break;
    }
    case State::Idle:
    case State::MiddlePressed: {
      const auto hit = dock::hitTestHandle(layout, p);
      hoverHandle_ = hit.has_value();
      window_.setCursor(!hit ? r1ui::platform::CursorShape::Arrow
                             : hit->handle.axis == dock::Axis::Row ? r1ui::platform::CursorShape::ResizeHorizontal
                                                                    : r1ui::platform::CursorShape::ResizeVertical);
      break;
    }
  }
}

void DockSandbox::onMouse(const MouseEvent& event) {
  const dock::Point p{event.x, event.y};
  switch (event.type) {
    case MouseEvent::Type::Move: onMove(p); return;
    case MouseEvent::Type::CaptureLost:
      state_ = State::Idle;  // the OS ended the interaction; nothing is dropped
      setStatus("interaction interrupted");
      return;
    case MouseEvent::Type::Down: break;
    case MouseEvent::Type::Up: break;
  }
  pointer_ = p;
  const dock::LayoutResult layout = layout_.computeLayout(mainRect());
  if (event.type == MouseEvent::Type::Down) {
    if (state_ != State::Idle) return;  // one interaction at a time (spec 08 rule 4)
    if (event.button == MouseButton::Left) {
      if (const auto handle = dock::hitTestHandle(layout, p)) {
        state_ = State::SplitterDragging;  // spec 05 rule 4: capture and start a resize
        handle_ = handle->handle;
        handleGrab_ = along(p, handle_.axis) - centreAlong(handle->rect, handle_.axis);
        window_.setCursor(handle_.axis == dock::Axis::Row ? r1ui::platform::CursorShape::ResizeHorizontal : r1ui::platform::CursorShape::ResizeVertical);
      } else if (const dock::TabLayout* tab = tabAt(layout, p)) {
        panel_ = tab->panel;  // spec 02 rule 1: activate on press, drag candidate registered
        press_ = p;
        grab_ = {p.x - tab->rect.x, p.y - tab->rect.y};
        layout_.activateTab(panel_);
        state_ = State::TabPressed;
      }
    } else if (event.button == MouseButton::Middle) {
      if (const dock::TabLayout* tab = tabAt(layout, p)) {
        panel_ = tab->panel;
        state_ = State::MiddlePressed;
      }
    }
    return;
  }

  // Button released: end whichever interaction that button started.
  if (event.button == MouseButton::Left) {
    if (state_ == State::TabDragging) {
      finishTabDrag(layout_.hitTestDropZone(layout, {p, panel_, grab_}));
    } else if (state_ == State::TabPressed) {
      setStatus("activated " + nameOf(panel_));
      state_ = State::Idle;
    } else if (state_ == State::SplitterDragging) {
      setStatus("resized");
      state_ = State::Idle;
    }
  } else if (event.button == MouseButton::Middle && state_ == State::MiddlePressed) {
    const dock::TabLayout* tab = tabAt(layout, p);
    if (tab != nullptr && tab->panel == panel_) {  // spec 02: release must still be over the same tab
      const dock::Status status = layout_.closePanel(panel_);
      setStatus(status ? "closed " + nameOf(panel_) : "Close refused: " + status.error);
    } else {
      setStatus("close cancelled");
    }
    state_ = State::Idle;
  }
}

// ---- Drawing ----------------------------------------------------------------------------

void DockSandbox::draw(Painter& painter, preview::TextEngine& text, ThemeId theme) {
  const auto color = [&](const char* token) { return toRgba(*tokens_.color(theme, token)); };
  const Rgba8 canvas = color("canvas");
  const Rgba8 stripColor = color("panel-secondary");
  const bool dragging = state_ == State::TabDragging;
  const dock::LayoutResult layout = layout_.computeLayout(mainRect());
  const auto panelColor = [&](dock::PanelId id) { return color(kPanels[id - 1].colorToken); };

  // Two passes: the main area (stacks, then its handles) first, floating areas (stacks, then their
  // handles) after, so a floating area always covers whatever lies under it. A handle belongs to a
  // floating area only when it lies entirely inside that area.
  const auto insideFloating = [&](const dock::Rect& r) {
    for (const dock::AreaLayout& a : layout.areas) {
      if (a.floating && a.bounds.contains({r.x, r.y}) && a.bounds.contains({r.x + r.w - 1, r.y + r.h - 1})) return true;
    }
    return false;
  };
  for (const bool floatingPass : {false, true}) {
    for (const dock::AreaLayout& area : layout.areas) {
      if (area.floating != floatingPass) continue;
      if (area.floating) {
        paintFill(painter, {area.bounds.x - 3, area.bounds.y - 3, area.bounds.w + 6, area.bounds.h + 6}, color("warning-action"));
        paintFill(painter, area.bounds, canvas);
      }
      for (const dock::StackLayout& stack : layout.stacks) {
        if (stack.area != area.id) continue;
        paintFill(painter, stack.strip, stripColor);
        // While a tab is dragged out it is not part of the strip; the neighbour shows instead.
        size_t front = 0;
        for (size_t i = 0; i < stack.tabs.size(); ++i) {
          if (stack.tabs[i].active) front = i;
        }
        if (dragging && stack.tabs[front].panel == panel_) {
          front = front + 1 < stack.tabs.size() ? front + 1 : (front > 0 ? front - 1 : front);
        }
        const bool bodyEmpty = dragging && stack.tabs.size() == 1 && stack.tabs[0].panel == panel_;
        if (!bodyEmpty) paintFill(painter, stack.body, mix(panelColor(stack.tabs[front].panel), canvas, 0.45));
        for (size_t i = 0; i < stack.tabs.size(); ++i) {
          const dock::TabLayout& tab = stack.tabs[i];
          if (dragging && tab.panel == panel_) continue;
          const bool isFront = i == front;
          const double drop = isFront ? 0.0 : 4.0;  // inactive tabs are shorter
          const Rgba8 base = panelColor(tab.panel);
          const dock::Rect face{tab.rect.x + 1, tab.rect.y + drop, std::max(0.0, tab.rect.w - 2), tab.rect.h - drop};
          const Rgba8 faceColor = isFront ? mix(base, kWhite, 0.3) : mix(base, kBlack, 0.35);
          paintFill(painter, face, faceColor);
          paintLabel(painter, text, nameOf(tab.panel), face, faceColor);
        }
      }
    }

    for (const dock::HandleLayout& h : layout.handles) {
      if (insideFloating(h.rect) != floatingPass) continue;
      const bool lit = (state_ == State::SplitterDragging && h.handle == handle_) || (state_ == State::Idle && hoverHandle_ && h.rect.contains(pointer_));
      paintFill(painter, h.rect, lit ? color("panel-focus") : color("border"));
    }
  }

  if (dragging) {
    paintOutline(painter, zone_.preview, 3.0, kPreviewOrange);
    const double width = std::min(layout_.config().maxTabWidth, 120.0);
    const dock::Rect ghost{pointer_.x - grab_.x, pointer_.y - grab_.y, width, layout_.config().tabStripHeight};
    paintFill(painter, {ghost.x - 2, ghost.y - 2, ghost.w + 4, ghost.h + 4}, kWhite);
    paintFill(painter, ghost, panelColor(panel_));
    paintLabel(painter, text, nameOf(panel_), ghost, panelColor(panel_));
  }

  const dock::Rect main = mainRect();
  const float statusTop = static_cast<float>(main.y + main.h + kMargin);
  text.draw(painter, status_, kStatusPixels, 400, static_cast<float>(main.x),
            statusTop + text.baselineInBox(kStatusPixels, static_cast<float>(kStatusHeight)), paintColor(color("muted")));
}
