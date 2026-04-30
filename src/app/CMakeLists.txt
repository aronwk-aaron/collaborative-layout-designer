qt_add_executable(bld_app
    main.cpp
)

target_link_libraries(bld_app PRIVATE bld_ui)

set_target_properties(bld_app PROPERTIES
    OUTPUT_NAME "brick-layout-designer"
    WIN32_EXECUTABLE ON
    MACOSX_BUNDLE ON
)

# Drop the Qt runtime next to the built .exe so double-clicking the
# dev binary works without putting Qt's bin/ on PATH. windeployqt is
# idempotent and incremental, so re-running it on every build is fine
# (and matches what packaging does in CI).
if(WIN32)
    find_program(WINDEPLOYQT_EXE windeployqt HINTS "${Qt6_DIR}/../../../bin")
    if(WINDEPLOYQT_EXE)
        add_custom_command(TARGET bld_app POST_BUILD
            COMMAND "${WINDEPLOYQT_EXE}"
                    --no-translations
                    --no-compiler-runtime
                    "$<TARGET_FILE:bld_app>"
            VERBATIM)
    endif()
endif()

# Headless CLI for dumping .bbm structure without requiring a GUI.
add_executable(bld_inspect bld_inspect.cpp)
target_link_libraries(bld_inspect PRIVATE bld_saveload bld_core Qt6::Core)

# Headless CLI that renders a .bbm to a PNG (uses QT_QPA_PLATFORM=offscreen).
add_executable(bld_render bld_render.cpp)
target_link_libraries(bld_render
    PRIVATE bld_rendering bld_saveload bld_parts bld_core
            Qt6::Core Qt6::Gui Qt6::Widgets)

# LDraw .ldr/.dat/.mpd -> .bbm importer.
add_executable(bld_ldraw_import bld_ldraw_import.cpp)
target_link_libraries(bld_ldraw_import
    PRIVATE bld_import bld_saveload bld_core Qt6::Core)

# External format (.ldr / .io / .lxf / .lxfml) -> top-down sprite PNG/GIF.
# Wraps the same render pipeline the UI's Tools → Import uses so CI
# pipelines and batch conversions can reach the sprite output without
# launching the desktop app.
add_executable(bld_sprite_gen bld_sprite_gen.cpp)
target_link_libraries(bld_sprite_gen
    PRIVATE bld_import bld_rendering bld_edit bld_parts bld_core
            Qt6::Core Qt6::Gui Qt6::Widgets)
