if(NOT DEFINED ROOT)
    message(FATAL_ERROR "ROOT is required")
endif()

cmake_path(ABSOLUTE_PATH ROOT NORMALIZE OUTPUT_VARIABLE package_root)

set(required_files
    "DEBIAN/control"
    "opt/retrovdp-studio/bin/RetroVDPStudio"
    "opt/retrovdp-studio/bin/retrovdp-cli"
    "opt/retrovdp-studio/bin/qt.conf"
    "opt/retrovdp-studio/lib/libQt6Core.so.6"
    "opt/retrovdp-studio/plugins/platforms/libqxcb.so"
    "usr/bin/RetroVDPStudio"
    "usr/bin/retrovdp-cli"
    "usr/share/applications/io.github.ciscogarciafl.RetroVDPStudio.desktop"
    "usr/share/icons/hicolor/256x256/apps/io.github.ciscogarciafl.RetroVDPStudio.png"
    "usr/share/doc/retrovdp-studio/LICENSE"
    "usr/share/doc/retrovdp-studio/NOTICE.md"
    "usr/share/doc/retrovdp-studio/THIRD_PARTY_NOTICES.md")
foreach(required_file IN LISTS required_files)
    if(NOT EXISTS "${package_root}/${required_file}")
        message(FATAL_ERROR
            "Required Debian package file is missing: ${required_file}")
    endif()
endforeach()

set(forbidden_paths
    "apprun-hooks"
    "usr/bin/qt.conf"
    "usr/lib"
    "usr/plugins"
    "usr/qml")
foreach(forbidden_path IN LISTS forbidden_paths)
    if(EXISTS "${package_root}/${forbidden_path}")
        message(FATAL_ERROR
            "Debian package uses forbidden global path: ${forbidden_path}")
    endif()
endforeach()

file(READ "${package_root}/DEBIAN/control" control)
if(NOT control MATCHES "Installed-Size: ([1-9][0-9]*)")
    message(FATAL_ERROR "Debian control file has no positive Installed-Size")
endif()
if(DEFINED EXPECTED_DEB_ARCH AND
   NOT control MATCHES "Architecture: ${EXPECTED_DEB_ARCH}([\r\n]|$)")
    message(FATAL_ERROR
        "Debian control file does not declare ${EXPECTED_DEB_ARCH}")
endif()

file(READ "${package_root}/usr/bin/RetroVDPStudio" gui_launcher)
if(NOT gui_launcher MATCHES
        "exec /opt/retrovdp-studio/bin/RetroVDPStudio")
    message(FATAL_ERROR "GUI launcher does not use the private runtime")
endif()
file(READ "${package_root}/usr/bin/retrovdp-cli" cli_launcher)
if(NOT cli_launcher MATCHES
        "exec /opt/retrovdp-studio/bin/retrovdp-cli")
    message(FATAL_ERROR "CLI launcher does not use the private runtime")
endif()

file(GLOB_RECURSE package_entries
    LIST_DIRECTORIES TRUE
    RELATIVE "${package_root}"
    "${package_root}/*")
foreach(package_entry IN LISTS package_entries)
    string(REPLACE "\\" "/" normalized_entry "${package_entry}")
    if(normalized_entry MATCHES "^DEBIAN(/|$)" OR
       normalized_entry MATCHES "^opt/retrovdp-studio(/|$)" OR
       normalized_entry MATCHES
           "^usr/bin/(RetroVDPStudio|retrovdp-cli)$" OR
       normalized_entry MATCHES
           "^usr/share/applications/io[.]github[.]ciscogarciafl[.]RetroVDPStudio[.]desktop$" OR
       normalized_entry MATCHES "^usr/share/icons/hicolor(/|$)" OR
       normalized_entry MATCHES "^usr/share/doc/retrovdp-studio(/|$)" OR
       normalized_entry STREQUAL "opt" OR
       normalized_entry STREQUAL "usr" OR
       normalized_entry STREQUAL "usr/bin" OR
       normalized_entry STREQUAL "usr/share" OR
       normalized_entry STREQUAL "usr/share/applications" OR
       normalized_entry STREQUAL "usr/share/icons" OR
       normalized_entry STREQUAL "usr/share/doc")
        continue()
    endif()
    message(FATAL_ERROR
        "Unexpected non-isolated Debian package path: ${normalized_entry}")
endforeach()

list(LENGTH package_entries entry_count)
message(STATUS
    "Audited ${entry_count} Debian entries; bundled runtime is isolated under /opt/retrovdp-studio")
