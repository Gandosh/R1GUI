# ui-theme: design tokens (colours now; spacing, radii and fonts follow) loaded from tokens.json.
add_library(ui-theme STATIC src/Tokens.cpp)
add_library(r1ui::theme ALIAS ui-theme)
target_include_directories(ui-theme PUBLIC include)
target_link_libraries(ui-theme PUBLIC r1ui::core PRIVATE r1ui_options)
