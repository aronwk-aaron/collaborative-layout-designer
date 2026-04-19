add_library(cld_core STATIC
    Map.cpp
    Ids.h
    Geometry.h
    Map.h
    Layer.h
    Brick.h
    Group.h
)

target_include_directories(cld_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(cld_core PUBLIC Qt6::Core Qt6::Gui)
