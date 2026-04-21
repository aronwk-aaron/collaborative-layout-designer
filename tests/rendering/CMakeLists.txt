add_executable(cld_rendering_tests
    RenderSmokeTest.cpp
    RenderGoldensTest.cpp
)

target_compile_definitions(cld_rendering_tests PRIVATE
    CLD_BBM_CORPUS_DIR="${CMAKE_SOURCE_DIR}/fixtures/bbm-corpus"
    CLD_PARTS_LIBRARY_ROOT="${CMAKE_SOURCE_DIR}/parts/BlueBrickParts/parts"
    CLD_RENDER_GOLDENS_DIR="${CMAKE_SOURCE_DIR}/fixtures/render-goldens"
)

target_link_libraries(cld_rendering_tests
    PRIVATE
        cld_rendering
        cld_saveload
        cld_parts
        cld_core
        GTest::gtest_main
        Qt6::Core
        Qt6::Gui
        Qt6::Widgets
)

gtest_discover_tests(cld_rendering_tests
    PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
)
