add_executable(bld_saveload_tests
    RoundTripTest.cpp
    RealFixtureTest.cpp
    SidecarTest.cpp
    SetIOTest.cpp
)

target_compile_definitions(bld_saveload_tests PRIVATE
    BLD_BBM_CORPUS_DIR="${CMAKE_SOURCE_DIR}/fixtures/bbm-corpus"
)

target_link_libraries(bld_saveload_tests
    PRIVATE
        bld_saveload
        bld_core
        GTest::gtest_main
        Qt6::Core
        Qt6::Gui
)

gtest_discover_tests(bld_saveload_tests
    DISCOVERY_TIMEOUT 60
)
