qt_add_executable(cld_app
    main.cpp
)

target_link_libraries(cld_app PRIVATE cld_ui)

set_target_properties(cld_app PROPERTIES
    OUTPUT_NAME "collaborative-layout-designer"
    WIN32_EXECUTABLE ON
    MACOSX_BUNDLE ON
)
