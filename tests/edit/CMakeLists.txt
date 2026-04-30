add_executable(bld_edit_tests
    EditCommandsTest.cpp
    ModuleCommandsTest.cpp
    LabelCommandsTest.cpp
    LayerCommandsTest.cpp
    AreaCommandsTest.cpp
)

target_link_libraries(bld_edit_tests
    PRIVATE
        bld_edit
        bld_core
        GTest::gtest_main
        Qt6::Core
        Qt6::Widgets
)

gtest_discover_tests(bld_edit_tests
    DISCOVERY_TIMEOUT 60
)
