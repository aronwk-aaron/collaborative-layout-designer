add_executable(cld_parts_tests
    PartsLibraryTest.cpp
)

target_compile_definitions(cld_parts_tests PRIVATE
    CLD_PARTS_LIBRARY_ROOT="${CMAKE_SOURCE_DIR}/parts/BlueBrickParts/parts"
)

target_link_libraries(cld_parts_tests
    PRIVATE
        cld_parts
        GTest::gtest_main
        Qt6::Core
        Qt6::Gui
)

gtest_discover_tests(cld_parts_tests)
