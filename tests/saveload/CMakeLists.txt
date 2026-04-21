add_executable(cld_saveload_tests
    RoundTripTest.cpp
    RealFixtureTest.cpp
    SidecarTest.cpp
    SetIOTest.cpp
)

target_compile_definitions(cld_saveload_tests PRIVATE
    CLD_BBM_CORPUS_DIR="${CMAKE_SOURCE_DIR}/fixtures/bbm-corpus"
)

target_link_libraries(cld_saveload_tests
    PRIVATE
        cld_saveload
        cld_core
        GTest::gtest_main
        Qt6::Core
        Qt6::Gui
)

gtest_discover_tests(cld_saveload_tests
    DISCOVERY_TIMEOUT 60
)
