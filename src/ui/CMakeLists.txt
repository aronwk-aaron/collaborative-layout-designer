add_library(cld_ui STATIC
    MainWindow.cpp
    MainWindow.h
)

target_link_libraries(cld_ui
    PUBLIC
        cld_core
        cld_saveload
        cld_parts
        Qt6::Core
        Qt6::Gui
        Qt6::Widgets
)
