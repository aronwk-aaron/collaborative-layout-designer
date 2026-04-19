add_executable(cld_saveload_tests
    RoundTripTest.cpp
)

target_link_libraries(cld_saveload_tests
    PRIVATE
        cld_saveload
        cld_core
        GTest::gtest_main
        Qt6::Core
        Qt6::Gui
)

gtest_discover_tests(cld_saveload_tests)
