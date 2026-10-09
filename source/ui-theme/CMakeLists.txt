# ui-theme: design tokens (colours, space, radius, font, shadow, motion, widget metrics) loaded from
# tokens.json, the active Theme, and the StyleSheet that resolves widget styles from tokens.
add_library(ui-theme STATIC src/Tokens.cpp src/TokenSections.cpp src/Theme.cpp src/StyleSheet.cpp)
add_library(r1ui::theme ALIAS ui-theme)
target_include_directories(ui-theme PUBLIC include)
target_link_libraries(ui-theme PUBLIC r1ui::core PRIVATE r1ui_options)
