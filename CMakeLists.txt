cmake_minimum_required(VERSION 3.25)

project(CollaborativeLayoutDesigner
    VERSION 0.0.1
    DESCRIPTION "Cross-platform maintainer fork of BlueBrick"
    LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Qt 6
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt6 6.7 REQUIRED COMPONENTS Core Gui Widgets PrintSupport)
find_package(Qt6 6.7 QUIET COMPONENTS LinguistTools)
qt_standard_project_setup()

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "" FORCE)
endif()

if(MSVC)
    add_compile_options(/W4 /permissive-)
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

include(CTest)

add_subdirectory(src)

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()

# ----- Localization -----------------------------------------------------------
# Base English is implicit (Qt returns the tr() argument when no catalogue is
# loaded). Supported target languages match BlueBrick's shipped set plus a
# few we want to cover going forward. Translators populate the .ts files via
# `lupdate`; `lrelease` compiles them to .qm which the build installs next to
# the binary. See docs/TRANSLATING.md.
set(CLD_LANGUAGES fr de es it nl pt zh ja)

set(CLD_TS_FILES "")
foreach(_lang IN LISTS CLD_LANGUAGES)
    list(APPEND CLD_TS_FILES "${CMAKE_SOURCE_DIR}/translations/cld_${_lang}.ts")
endforeach()

# Only wire the binaries when every .ts file exists so a fresh clone before
# any translations are stubbed out still builds.
set(CLD_HAVE_TS TRUE)
foreach(_ts IN LISTS CLD_TS_FILES)
    if(NOT EXISTS "${_ts}")
        set(CLD_HAVE_TS FALSE)
    endif()
endforeach()
if(CLD_HAVE_TS AND TARGET cld_app AND Qt6LinguistTools_FOUND)
    qt_add_translations(cld_app
        TS_FILES ${CLD_TS_FILES}
        QM_FILES_OUTPUT_VARIABLE CLD_QM_FILES)
    install(FILES ${CLD_QM_FILES} DESTINATION translations OPTIONAL)
endif()
