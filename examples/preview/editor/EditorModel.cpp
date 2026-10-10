// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: implementation of EditorModel.h: the property declarations of the two sample object types,
//   the initial scene, selection and log bookkeeping.
// Invariants: items_ ids are 1.. in scene order; selection_ only holds ids of items_; the property
//   context is fed the selection in the order of selection_.
// Callers: EditorApp, tests/preview.
#include "EditorModel.h"

#include <algorithm>

namespace preview::editor {

namespace props = r1ui::props;

namespace {

const props::PropertySet<MeshObject>& meshSet() {
  static const props::PropertySet<MeshObject> set = [] {
    props::PropertySet<MeshObject> s("Mesh");
    s.category("Transform", -10);
    s.add("position", "Position", &MeshObject::position).unit("m").range(-1000.0, 1000.0).tooltip("World position");
    s.add("rotation", "Rotation", &MeshObject::rotation).unit("\xC2\xB0").range(-360.0, 360.0);
    s.add("scale", "Scale", &MeshObject::scale).range(0.001, 1000.0);
    s.category("Object");
    s.add("name", "Name", &MeshObject::name).maxLength(64);
    s.add("visible", "Visible", &MeshObject::visible);
    s.add("castShadows", "Cast shadows", &MeshObject::castShadows).advanced();
    s.category("Material");
    s.add("color", "Color", &MeshObject::color);
    s.add("roughness", "Roughness", &MeshObject::roughness).slider(0.0, 1.0).precision(2).tooltip("Surface roughness");
    s.add("shading", "Shading", &MeshObject::shading).value("flat", Shading::Flat, "Flat").value("smooth", Shading::Smooth, "Smooth").value("toon", Shading::Toon, "Toon");
    s.defaultsFrom(MeshObject{});
    return s;
  }();
  return set;
}

const props::PropertySet<LightObject>& lightSet() {
  static const props::PropertySet<LightObject> set = [] {
    props::PropertySet<LightObject> s("Light");
    s.category("Transform", -10);
    s.add("position", "Position", &LightObject::position).unit("m").range(-1000.0, 1000.0).tooltip("World position");
    s.category("Object");
    s.add("name", "Name", &LightObject::name).maxLength(64);
    s.add("visible", "Visible", &LightObject::visible);
    s.category("Light");
    s.add("intensity", "Intensity", &LightObject::intensity).range(0.0, 1.0e6).softRange(0.0, 1000.0).step(10.0).unit("lm");
    s.add("radius", "Radius", &LightObject::radius).range(0.0, 100.0);
    s.add("color", "Color", &LightObject::color);
    s.defaultsFrom(LightObject{});
    return s;
  }();
  return set;
}

props::ContextOptions optionsFor(const props::Clock* clock) {
  props::ContextOptions o;
  o.ownedUndo.clock = clock;
  return o;
}

}  // namespace

EditorModel::EditorModel(const r1ui::widgets::UiContext& ui) : clock_(ui), context_(optionsFor(&clock_)) {
  meshes_.reserve(4);
  lights_.reserve(2);
  const auto mesh = [&](const char* name, double x, double y, props::Color color, double roughness) {
    MeshObject m;
    m.name = name;
    m.position = {x, y, 0.0};
    m.color = color;
    m.roughness = roughness;
    meshes_.push_back(std::move(m));
  };
  mesh("Cube", -4.0, 2.0, {0.35f, 0.55f, 0.9f, 1.0f}, 0.35);
  mesh("Sphere", 1.5, 3.0, {0.85f, 0.45f, 0.3f, 1.0f}, 0.8);
  mesh("Cone", 5.0, -1.0, {0.4f, 0.75f, 0.45f, 1.0f}, 0.5);
  mesh("Plane", 0.0, -4.0, {0.6f, 0.6f, 0.62f, 1.0f}, 0.9);
  const auto light = [&](const char* name, double x, double y, double intensity) {
    LightObject l;
    l.name = name;
    l.position = {x, y, 6.0};
    l.intensity = intensity;
    l.color = {1.0f, 0.93f, 0.75f, 1.0f};
    lights_.push_back(std::move(l));
  };
  light("Key light", -7.0, 6.0, 250.0);
  light("Fill light", 7.0, 5.0, 80.0);
  uint64_t id = 1;
  for (size_t i = 0; i < meshes_.size(); ++i) items_.push_back({id++, ObjectKind::Mesh, i});
  for (size_t i = 0; i < lights_.size(); ++i) items_.push_back({id++, ObjectKind::Light, i});
  select({1});
}

const SceneItem* EditorModel::item(uint64_t id) const {
  const auto it = std::find_if(items_.begin(), items_.end(), [id](const SceneItem& i) { return i.id == id; });
  return it == items_.end() ? nullptr : &*it;
}

const std::string& EditorModel::nameOf(const SceneItem& item) const { return item.kind == ObjectKind::Mesh ? meshes_[item.index].name : lights_[item.index].name; }

props::Vec3 EditorModel::positionOf(const SceneItem& item) const { return item.kind == ObjectKind::Mesh ? meshes_[item.index].position : lights_[item.index].position; }

props::Color EditorModel::colorOf(const SceneItem& item) const { return item.kind == ObjectKind::Mesh ? meshes_[item.index].color : lights_[item.index].color; }

bool EditorModel::visibleOf(const SceneItem& item) const { return item.kind == ObjectKind::Mesh ? meshes_[item.index].visible : lights_[item.index].visible; }

bool EditorModel::isSelected(uint64_t id) const { return std::find(selection_.begin(), selection_.end(), id) != selection_.end(); }

void EditorModel::select(std::vector<uint64_t> ids) {
  std::vector<uint64_t> kept;
  for (const uint64_t id : ids) {
    if (item(id) != nullptr && std::find(kept.begin(), kept.end(), id) == kept.end()) kept.push_back(id);
  }
  if (kept == selection_ && context_.targetCount() == kept.size()) return;
  selection_ = std::move(kept);
  std::vector<props::Target> targets;
  for (const uint64_t id : selection_) {
    const SceneItem* it = item(id);
    if (it->kind == ObjectKind::Mesh) targets.push_back(props::targetOf(meshes_[it->index], meshSet()));
    else targets.push_back(props::targetOf(lights_[it->index], lightSet()));
  }
  context_.setSelection(targets);
  const std::vector<Slot> listeners = selectionListeners_;  // a listener may unsubscribe
  for (const Slot& slot : listeners) slot.fn();
}

EditorModel::ListenerId EditorModel::onSelectionChanged(Listener listener) {
  selectionListeners_.push_back({nextListener_, std::move(listener)});
  return nextListener_++;
}

void EditorModel::removeSelectionListener(ListenerId id) {
  std::erase_if(selectionListeners_, [id](const Slot& s) { return s.id == id; });
}

void EditorModel::frameSelection() {
  double sumX = 0.0;
  double sumY = 0.0;
  size_t count = 0;
  for (const SceneItem& item : items_) {
    if (!selection_.empty() && !isSelected(item.id)) continue;
    const r1ui::props::Vec3 p = positionOf(item);
    sumX += p.x;
    sumY += p.y;
    ++count;
  }
  viewX = count > 0 ? sumX / static_cast<double>(count) : 0.0;
  viewY = count > 0 ? sumY / static_cast<double>(count) : 0.0;
  touch();
}

void EditorModel::touch() {
  const std::vector<Slot> listeners = stateListeners_;
  for (const Slot& slot : listeners) slot.fn();
}

EditorModel::ListenerId EditorModel::onState(Listener listener) {
  stateListeners_.push_back({nextListener_, std::move(listener)});
  return nextListener_++;
}

void EditorModel::removeStateListener(ListenerId id) {
  std::erase_if(stateListeners_, [id](const Slot& s) { return s.id == id; });
}

void EditorModel::log(LogLevel level, std::string text) {
  lines_.push_back({++serial_, level, std::move(text)});
  while (lines_.size() > kMaxLogLines) lines_.pop_front();
  const std::vector<Slot> listeners = logListeners_;
  for (const Slot& slot : listeners) slot.fn();
}

EditorModel::ListenerId EditorModel::onLog(Listener listener) {
  logListeners_.push_back({nextListener_, std::move(listener)});
  return nextListener_++;
}

void EditorModel::removeLogListener(ListenerId id) {
  std::erase_if(logListeners_, [id](const Slot& s) { return s.id == id; });
}

}  // namespace preview::editor
