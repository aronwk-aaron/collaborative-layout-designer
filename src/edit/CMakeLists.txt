add_library(cld_edit STATIC
    EditCommands.cpp
    EditCommands.h
    ModuleCommands.cpp
    ModuleCommands.h
)

target_include_directories(cld_edit PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(cld_edit
    PUBLIC
        cld_core
        Qt6::Core
        Qt6::Widgets     # QUndoCommand lives in QtWidgets
)
