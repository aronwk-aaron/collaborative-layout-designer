qt_add_executable(cld_app
    main.cpp
)

target_link_libraries(cld_app PRIVATE cld_ui)

set_target_properties(cld_app PROPERTIES
    OUTPUT_NAME "collaborative-layout-designer"
    WIN32_EXECUTABLE ON
    MACOSX_BUNDLE ON
)

# Headless CLI for dumping .bbm structure without requiring a GUI.
add_executable(cld_inspect cld_inspect.cpp)
target_link_libraries(cld_inspect PRIVATE cld_saveload cld_core Qt6::Core)

# Headless CLI that renders a .bbm to a PNG (uses QT_QPA_PLATFORM=offscreen).
add_executable(cld_render cld_render.cpp)
target_link_libraries(cld_render
    PRIVATE cld_rendering cld_saveload cld_parts cld_core
            Qt6::Core Qt6::Gui Qt6::Widgets)
