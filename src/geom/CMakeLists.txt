add_library(cld_geom INTERFACE)
target_include_directories(cld_geom INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(cld_geom INTERFACE Qt6::Core Qt6::Gui)
