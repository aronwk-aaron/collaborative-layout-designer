add_library(cld_ui STATIC
    MainWindow.cpp
    MainWindow.h
    MapView.cpp
    MapView.h
    LayerPanel.cpp
    LayerPanel.h
    PartsBrowser.cpp
    PartsBrowser.h
    ModulesPanel.cpp
    ModulesPanel.h
)

target_link_libraries(cld_ui
    PUBLIC
        cld_core
        cld_saveload
        cld_parts
        cld_rendering
        cld_edit
        Qt6::Core
        Qt6::Gui
        Qt6::Widgets
)
