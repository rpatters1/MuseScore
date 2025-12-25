# SPDX-License-Identifier: GPL-3.0-only
# MuseScore-Studio-CLA-applies
#
# MuseScore Studio
# .mnx Import Module (Experimental)

declare_module(iex_mnx)

include(FetchContent)

set(mnxdom_BUILD_TESTING OFF CACHE BOOL "Do not build tests for mnx")
FetchContent_Declare(
    mnxdom
    GIT_REPOSITORY https://github.com/rpatters1/mnxdom.git
    GIT_TAG        dev/musescore-prep
)

FetchContent_MakeAvailable(mnxdom)

set(MODULE_SRC
    ${CMAKE_CURRENT_LIST_DIR}/mnxmodule.cpp
    ${CMAKE_CURRENT_LIST_DIR}/mnxmodule.h
    ${CMAKE_CURRENT_LIST_DIR}/internal/mnxexporter.cpp
    ${CMAKE_CURRENT_LIST_DIR}/internal/mnxexporter.h
    ${CMAKE_CURRENT_LIST_DIR}/internal/mnximporter.cpp
    ${CMAKE_CURRENT_LIST_DIR}/internal/mnximporter.h
    ${CMAKE_CURRENT_LIST_DIR}/internal/mnxtypesconv.cpp
    ${CMAKE_CURRENT_LIST_DIR}/internal/mnxtypesconv.h
    ${CMAKE_CURRENT_LIST_DIR}/internal/notationmnxreader.cpp
    ${CMAKE_CURRENT_LIST_DIR}/internal/notationmnxreader.h
    ${CMAKE_CURRENT_LIST_DIR}/internal/notationmnxwriter.cpp
    ${CMAKE_CURRENT_LIST_DIR}/internal/notationmnxwriter.h
)

set(MODULE_LINK
    mnxdom            # Link MnxDom library
    engraving         # MuseScore expects importers to link to "engraving" usually
)

setup_module()
