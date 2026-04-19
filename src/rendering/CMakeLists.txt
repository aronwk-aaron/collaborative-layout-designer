add_library(cld_rendering INTERFACE)
target_sources(cld_rendering INTERFACE MapScene.h)
target_include_directories(cld_rendering INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})
