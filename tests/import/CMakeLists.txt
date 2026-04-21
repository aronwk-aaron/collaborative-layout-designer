add_executable(cld_import_tests
    LDrawReaderTest.cpp
    StudioReaderTest.cpp
    LDDReaderTest.cpp
)

target_link_libraries(cld_import_tests
    PRIVATE
        cld_import
        cld_core
        GTest::gtest_main
        Qt6::Core
)

# Studio/LDD tests construct synthetic ZIPs in temp dirs using Qt's
# private QZipWriter — same approach the reader side uses. cld_import's
# CMakeLists.txt already located the private header dir and cached it
# in CLD_QZIPREADER_INCLUDE_DIR; reuse it here so the tests can include
# <private/qzipwriter_p.h>.
if(CLD_QZIPREADER_INCLUDE_DIR)
    target_include_directories(cld_import_tests PRIVATE
        "${CLD_QZIPREADER_INCLUDE_DIR}")
    get_filename_component(_qtcore_private_parent
        "${CLD_QZIPREADER_INCLUDE_DIR}" DIRECTORY)
    target_include_directories(cld_import_tests PRIVATE
        "${_qtcore_private_parent}")
endif()

gtest_discover_tests(cld_import_tests)
