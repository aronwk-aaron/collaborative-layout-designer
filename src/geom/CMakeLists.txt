add_library(bld_geom INTERFACE)
target_include_directories(bld_geom INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(bld_geom INTERFACE Qt6::Core Qt6::Gui)
