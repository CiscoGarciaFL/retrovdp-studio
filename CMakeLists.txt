cmake_minimum_required(VERSION 3.24)

# This is the single release-version source. CMake's project() command accepts
# only the numeric core, so keep the complete semantic version here and derive
# PROJECT_VERSION from it.
set(RETROVDP_VERSION "0.1.0-beta.11")
if(NOT RETROVDP_VERSION MATCHES
        "^([0-9]+)\\.([0-9]+)\\.([0-9]+)(-[0-9A-Za-z.-]+)?$")
    message(FATAL_ERROR
        "RETROVDP_VERSION is not a valid semantic version: "
        "${RETROVDP_VERSION}")
endif()
set(RETROVDP_VERSION_CORE
    "${CMAKE_MATCH_1}.${CMAKE_MATCH_2}.${CMAKE_MATCH_3}")

if(CMAKE_HOST_APPLE AND NOT DEFINED CMAKE_OSX_DEPLOYMENT_TARGET)
    # Qt 6.10 supports macOS 13 and newer. Keep this explicit so both native
    # architecture packages have the same compatibility floor.
    set(CMAKE_OSX_DEPLOYMENT_TARGET "13.0" CACHE STRING
        "Minimum supported macOS release")
endif()

project(RetroVDPStudio
    VERSION ${RETROVDP_VERSION_CORE}
    DESCRIPTION "Cross-platform graphics workspace for classic video display processors"
    LANGUAGES CXX
)

set(RETROVDP_ORGANIZATION "CiscoGarciaFL")
set(RETROVDP_SETTINGS_ID "RetroVDPStudio")
set(RETROVDP_MACOS_BUNDLE_ID "io.github.ciscogarciafl.RetroVDPStudio")
set(RETROVDP_LINUX_APP_ID "io.github.ciscogarciafl.RetroVDPStudio")
# Frozen for the RetroVDP Studio product line. This identifies the Windows
# installer family; never replace it for later beta, RC, or stable packages.
set(RETROVDP_WINDOWS_INSTALLER_ID
    "{9D53324F-AE3A-42FC-96C6-0B2256798410}")

option(RETROVDP_RELEASE_BUILD
    "Enable strict release packaging inputs and metadata" OFF)
if(RETROVDP_RELEASE_BUILD)
    set(RETROVDP_LICENSE_SOURCE_DIR
        "${CMAKE_CURRENT_SOURCE_DIR}/packaging/licenses")
    set(RETROVDP_LICENSE_DIR "${CMAKE_BINARY_DIR}/release-licenses")
    file(MAKE_DIRECTORY "${RETROVDP_LICENSE_DIR}")
    foreach(license_file IN ITEMS LGPL-3.0.txt GPL-3.0.txt)
        configure_file(
            "${RETROVDP_LICENSE_SOURCE_DIR}/${license_file}"
            "${RETROVDP_LICENSE_DIR}/${license_file}"
            COPYONLY
        )
    endforeach()
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# The legacy Release build used whole-program optimization. Enable the
# equivalent portable IPO/LTO mode when the active compiler supports it so
# cross-translation-unit helpers can be inlined in production builds.
include(CheckIPOSupported)
check_ipo_supported(RESULT RETROVDP_IPO_SUPPORTED)
if(RETROVDP_IPO_SUPPORTED)
    set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE ON)
endif()

find_package(Qt6 6.8 REQUIRED COMPONENTS Concurrent Core Gui Quick QuickControls2 Widgets)
qt_standard_project_setup(REQUIRES 6.8)

add_subdirectory(src/core)
add_subdirectory(src/media)
add_subdirectory(src/formats)
add_subdirectory(src/imageio)
add_subdirectory(app)

install(FILES
    LICENSE
    NOTICE.md
    docs/THIRD_PARTY_NOTICES.md
    DESTINATION .
    COMPONENT Runtime
)
if(RETROVDP_RELEASE_BUILD)
    install(DIRECTORY "${RETROVDP_LICENSE_DIR}/"
        DESTINATION LICENSES
        COMPONENT Runtime
    )
endif()

set(CPACK_PACKAGE_NAME "RetroVDPStudio")
set(CPACK_PACKAGE_VENDOR "CiscoGarciaFL")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "${PROJECT_DESCRIPTION}")
set(CPACK_PACKAGE_HOMEPAGE_URL
    "https://github.com/CiscoGarciaFL/retrovdp-studio")
set(CPACK_PACKAGE_VERSION "${RETROVDP_VERSION}")
set(CPACK_PACKAGE_INSTALL_DIRECTORY "RetroVDPStudio")
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE")
set(CPACK_MONOLITHIC_INSTALL ON)

if(WIN32)
    set(CPACK_GENERATOR "ZIP;NSIS")
    set(CPACK_NSIS_DISPLAY_NAME "RetroVDP Studio")
    set(CPACK_NSIS_PACKAGE_NAME "RetroVDP Studio")
    set(CPACK_NSIS_INSTALL_ROOT "$LOCALAPPDATA")
    set(CPACK_NSIS_MUI_ICON
        "${CMAKE_CURRENT_SOURCE_DIR}/app/assets/icons/RetroVDPStudio.ico")
    set(CPACK_NSIS_MUI_UNIICON
        "${CMAKE_CURRENT_SOURCE_DIR}/app/assets/icons/RetroVDPStudio.ico")
    set(CPACK_NSIS_ENABLE_UNINSTALL_BEFORE_INSTALL ON)
    set(CPACK_NSIS_MODIFY_PATH OFF)
    set(CPACK_PACKAGE_INSTALL_REGISTRY_KEY
        "RetroVDPStudio-${RETROVDP_WINDOWS_INSTALLER_ID}")
    set(CPACK_PACKAGE_EXECUTABLES "RetroVDPStudio;RetroVDP Studio")
    set(CPACK_CREATE_DESKTOP_LINKS RetroVDPStudio)
elseif(APPLE)
    set(CPACK_GENERATOR "DragNDrop")
else()
    set(CPACK_GENERATOR "DEB")
    set(CPACK_DEBIAN_PACKAGE_NAME "retrovdp-studio")
    set(CPACK_DEBIAN_PACKAGE_MAINTAINER "Cisco Garcia")
    set(CPACK_DEBIAN_PACKAGE_SECTION "graphics")
    set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
    string(REPLACE "-" "~" CPACK_DEBIAN_PACKAGE_VERSION
        "${RETROVDP_VERSION}")
endif()

include(CPack)

include(CTest)
if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
