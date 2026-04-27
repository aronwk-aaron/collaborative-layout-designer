add_library(cld_import STATIC
    LDrawReader.cpp
    LDrawReader.h
    LDrawColors.cpp
    LDrawColors.h
    LDrawLibrary.cpp
    LDrawLibrary.h
    LDrawMeshBuilder.cpp
    LDrawMeshBuilder.h
    LDrawMeshLoader.cpp
    LDrawMeshLoader.h
    LDrawPalette.cpp
    LDrawPalette.h
    MeshRasterize.cpp
    MeshRasterize.h
    LDrawRasterize.cpp
    LDrawRasterize.h
    StudioReader.cpp
    StudioReader.h
    LDDGeomReader.cpp
    LDDGeomReader.h
    LDDLDrawMapping.cpp
    LDDLDrawMapping.h
    LDDReader.cpp
    LDDReader.h
    LifReader.cpp
    LifReader.h
    ImportToPart.cpp
    ImportToPart.h
)

target_include_directories(cld_import PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
# StudioReader uses QZipReader from Qt's private Core headers. The public
# Qt6::Core target doesn't expose the private include path, so we pull it
# in explicitly. Using private Qt APIs is a stability tradeoff — it can
# break across Qt minor versions, but it spares us the external QuaZip /
# miniz dep for a tiny feature.
target_link_libraries(cld_import PUBLIC cld_core cld_geom Qt6::Core Qt6::Gui)
# Qt 6 layout puts private headers under
# <prefix>/include/QtCore/<version>/QtCore/private/. Locate the specific
# directory that contains the header we need, then add its parent so
# `#include <private/qzipreader_p.h>` resolves.
find_path(CLD_QZIPREADER_INCLUDE_DIR
    NAMES private/qzipreader_p.h
    PATHS
        # Start from the public Qt Core include dir and walk into the
        # version-numbered sibling. The find_path search walks sub-
        # directories automatically; NO_DEFAULT_PATH keeps us from
        # pulling in unrelated system headers.
        /usr/include/qt6/QtCore/${Qt6Core_VERSION}/QtCore
        /usr/include/qt6/QtCore
    NO_DEFAULT_PATH)
if(NOT CLD_QZIPREADER_INCLUDE_DIR)
    # Generic fallback: search every versioned sub-dir under the Core
    # include path. Reasonable on macOS / Windows Qt installer layouts.
    get_target_property(_qt_core_includes Qt6::Core INTERFACE_INCLUDE_DIRECTORIES)
    foreach(_inc IN LISTS _qt_core_includes)
        file(GLOB _candidates LIST_DIRECTORIES true "${_inc}/*/QtCore")
        foreach(_c IN LISTS _candidates)
            if(EXISTS "${_c}/private/qzipreader_p.h")
                set(CLD_QZIPREADER_INCLUDE_DIR "${_c}" CACHE INTERNAL "")
                break()
            endif()
        endforeach()
        if(CLD_QZIPREADER_INCLUDE_DIR)
            break()
        endif()
    endforeach()
endif()
if(CLD_QZIPREADER_INCLUDE_DIR)
    target_include_directories(cld_import PRIVATE "${CLD_QZIPREADER_INCLUDE_DIR}")
    # The private headers also reference <QtCore/private/qglobal_p.h>
    # etc., which resolves via the version-parent (one directory up).
    get_filename_component(_qtcore_version_parent
        "${CLD_QZIPREADER_INCLUDE_DIR}" DIRECTORY)
    target_include_directories(cld_import PRIVATE "${_qtcore_version_parent}")
    message(STATUS "cld_import: QZipReader private headers at ${CLD_QZIPREADER_INCLUDE_DIR}")
else()
    message(WARNING
        "cld_import: qzipreader_p.h not found — Studio .io import will be stubbed. "
        "Install the Qt private headers package if you want this feature.")
    target_compile_definitions(cld_import PRIVATE CLD_NO_QZIPREADER=1)
endif()
