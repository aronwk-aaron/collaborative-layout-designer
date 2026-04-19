add_library(cld_import STATIC
    LDrawReader.cpp
    LDrawReader.h
)

target_include_directories(cld_import PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(cld_import PUBLIC cld_core Qt6::Core)
