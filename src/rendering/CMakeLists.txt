add_library(cld_rendering STATIC
    SceneBuilder.cpp
    SceneBuilder.h
    SceneBuilderInternal.h
    SceneBuilderElectric.cpp
    SceneBuilderSidecar.cpp
    MapScene.h
)

target_include_directories(cld_rendering PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(cld_rendering
    PUBLIC
        cld_core
        cld_parts
        Qt6::Core
        Qt6::Gui
        Qt6::Widgets
)
