add_library(cld_saveload STATIC
    XmlPrimitives.cpp
    XmlPrimitives.h
    XmlCanonicalWriter.cpp
    XmlCanonicalWriter.h
    LayerIO.cpp
    LayerIO.h
    BbmReader.cpp
    BbmReader.h
    BbmWriter.cpp
    BbmWriter.h
    SidecarIO.cpp
    SidecarIO.h
)

target_link_libraries(cld_saveload PUBLIC cld_core Qt6::Core Qt6::Gui)
