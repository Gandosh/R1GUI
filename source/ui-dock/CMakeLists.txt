# ui-dock: headless dock model (tab stacks, splits, floating areas), its geometry, drop-zone
# hit testing, layout persistence (schema versions 1 and 2) and the layout manager (named layouts,
# auto-save, import and export). No Vulkan or OS types; depends only on ui-core.
add_library(ui-dock STATIC
  src/DockTree.cpp
  src/DockOps.cpp
  src/DockOpsMore.cpp
  src/DockGeometry.cpp
  src/DockMonitors.cpp
  src/DockSerialize.cpp
  src/DockSerializeWrite.cpp
  src/LayoutStore.cpp
  src/LayoutManager.cpp
  src/LayoutManagerAutosave.cpp)
add_library(r1ui::dock ALIAS ui-dock)
target_include_directories(ui-dock PUBLIC include PRIVATE src)
target_link_libraries(ui-dock PUBLIC r1ui::core PRIVATE r1ui_options)
