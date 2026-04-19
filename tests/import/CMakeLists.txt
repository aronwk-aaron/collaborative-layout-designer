add_executable(cld_import_tests
    LDrawReaderTest.cpp
)

target_link_libraries(cld_import_tests
    PRIVATE
        cld_import
        cld_core
        GTest::gtest_main
        Qt6::Core
)

gtest_discover_tests(cld_import_tests)
