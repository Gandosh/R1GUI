// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: EditorModel, the sample document of the preview's Editor screen: a handful of scene objects
//   (meshes and lights) with declared properties, the selection that feeds the generated property
//   panel (mixed values, undo, reset), a bounded console log and the state the sample commands
//   change (active tool, grid).
// Why: the Editor screen needs real data behind its panels; the property context, undo stack and
//   selection must exist once and outlive every panel widget, because panels are recreated when a tab
//   moves into or out of a native window.
// Callers: EditorApp (owner), the panel widgets (Outliner, Inspector, Viewport, Console), commands.
//   Calls: ui-props (PropertySet, PropertyContext, PanelState, UndoStack).
// Lifetime: objects are never added or removed after construction (the property context keeps raw
//   pointers to them); listeners are removed by their owners in onDetached. UI thread only.
#pragma once

#include <cstdint>
#include <deque>
#include <functional>
#include <string>
#include <vector>

#include "r1ui/props/PanelState.h"
#include "r1ui/props/PropertyContext.h"
#include "r1ui/props/PropertySet.h"
#include "r1ui/widgets/props/PropertyPanel.h"
#include "r1ui/widgets/runtime/UiContext.h"

namespace preview::editor {

enum class Shading { Flat, Smooth, Toon };

struct MeshObject {
  std::string name = "Mesh";
  bool visible = true;
  r1ui::props::Vec3 position{};
  r1ui::props::Vec3 rotation{};
  r1ui::props::Vec3 scale{1.0, 1.0, 1.0};
  r1ui::props::Color color{0.7f, 0.7f, 0.7f, 1.0f};
  double roughness = 0.5;
  Shading shading = Shading::Smooth;
  bool castShadows = true;
};

struct LightObject {
  std::string name = "Light";
  bool visible = true;
  r1ui::props::Vec3 position{};
  r1ui::props::Color color{1.0f, 1.0f, 1.0f, 1.0f};
  double intensity = 100.0;
  double radius = 1.0;
};

enum class ObjectKind : uint8_t { Mesh, Light };

// One row of the outliner / one shape of the viewport.
struct SceneItem {
  uint64_t id = 0;  // stable, non-zero; the outliner's node id
  ObjectKind kind = ObjectKind::Mesh;
  size_t index = 0;  // into meshes_ / lights_
};

enum class LogLevel : uint8_t { Info, Warning, Error };

struct LogLine {
  uint64_t serial = 0;
  LogLevel level = LogLevel::Info;
  std::string text;
};

class EditorModel {
 public:
  static constexpr size_t kMaxLogLines = 500;

  explicit EditorModel(const r1ui::widgets::UiContext& ui);
  EditorModel(const EditorModel&) = delete;
  EditorModel& operator=(const EditorModel&) = delete;

  // ---- properties and undo ----
  r1ui::props::PropertyContext& context() { return context_; }
  r1ui::props::PanelState& panelState() { return panelState_; }

  // ---- scene and selection ----
  const std::vector<SceneItem>& items() const { return items_; }
  const SceneItem* item(uint64_t id) const;
  const MeshObject& mesh(size_t index) const { return meshes_[index]; }
  const LightObject& light(size_t index) const { return lights_[index]; }
  const std::string& nameOf(const SceneItem& item) const;
  // Object position in the scene plane (x, y), the viewport draws and drags with it.
  r1ui::props::Vec3 positionOf(const SceneItem& item) const;
  r1ui::props::Color colorOf(const SceneItem& item) const;
  bool visibleOf(const SceneItem& item) const;
  const std::vector<uint64_t>& selection() const { return selection_; }
  bool isSelected(uint64_t id) const;
  // Replaces the selection (unknown ids are ignored) and feeds the property context.
  void select(std::vector<uint64_t> ids);

  using Listener = std::function<void()>;
  using ListenerId = uint32_t;
  ListenerId onSelectionChanged(Listener listener);
  void removeSelectionListener(ListenerId id);

  // ---- state the sample commands change ----
  std::string tool = "tool.select";
  bool showGrid = true;
  bool showLights = true;
  // Tells the views that tool, grid or light display changed (commands call it after changing them).
  void touch();
  ListenerId onState(Listener listener);
  void removeStateListener(ListenerId id);

  // ---- console ----
  void log(LogLevel level, std::string text);
  void info(std::string text) { log(LogLevel::Info, std::move(text)); }
  const std::deque<LogLine>& lines() const { return lines_; }
  uint64_t logSerial() const { return serial_; }
  ListenerId onLog(Listener listener);
  void removeLogListener(ListenerId id);

 private:
  struct Slot {
    ListenerId id = 0;
    Listener fn;
  };

  r1ui::widgets::PropsUiClock clock_;
  std::vector<MeshObject> meshes_;
  std::vector<LightObject> lights_;
  std::vector<SceneItem> items_;
  std::vector<uint64_t> selection_;
  r1ui::props::PropertyContext context_;
  r1ui::props::PanelState panelState_;
  std::vector<Slot> selectionListeners_;
  std::vector<Slot> logListeners_;
  std::vector<Slot> stateListeners_;
  ListenerId nextListener_ = 1;
  std::deque<LogLine> lines_;
  uint64_t serial_ = 0;
};

}  // namespace preview::editor
