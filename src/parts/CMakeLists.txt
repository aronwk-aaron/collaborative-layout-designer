add_library(cld_parts STATIC
    PartsLibrary.cpp
    PartsLibrary.h
)

target_include_directories(cld_parts PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(cld_parts PUBLIC Qt6::Core Qt6::Gui)
