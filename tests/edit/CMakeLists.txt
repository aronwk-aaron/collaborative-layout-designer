add_executable(cld_edit_tests
    EditCommandsTest.cpp
    ModuleCommandsTest.cpp
    LabelCommandsTest.cpp
    LayerCommandsTest.cpp
    AreaCommandsTest.cpp
)

target_link_libraries(cld_edit_tests
    PRIVATE
        cld_edit
        cld_core
        GTest::gtest_main
        Qt6::Core
        Qt6::Widgets
)

gtest_discover_tests(cld_edit_tests
    DISCOVERY_TIMEOUT 60
)
