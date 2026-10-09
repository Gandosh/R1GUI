# ui-core: framework-neutral primitives shared by every toolkit module.
# r1ui_options is PUBLIC so dependants (and their tests) inherit the warning policy.
# Subsystems: json (Json, JsonWriter), tree (widget arena), layout (flexbox), events (routing),
# invalidation (dirty tracking and damage).
add_library(ui-core STATIC
  src/Json.cpp
  src/JsonWriter.cpp
  src/tree/WidgetTree.cpp
  src/layout/Style.cpp
  src/layout/LayoutEngine.cpp
  src/layout/FlexContainer.cpp
  src/layout/FlexLayout.cpp
  src/events/TreeQueries.cpp
  src/events/Router.cpp
  src/events/RouterFocus.cpp
  src/invalidation/DamageList.cpp
  src/invalidation/Invalidator.cpp)
add_library(r1ui::core ALIAS ui-core)
target_include_directories(ui-core PUBLIC include)
target_compile_features(ui-core PUBLIC cxx_std_20)
target_link_libraries(ui-core PUBLIC r1ui_options)
