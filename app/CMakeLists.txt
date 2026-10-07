add_library(retrovdp_app_support STATIC
    AppPreferencesController.cpp
    AppPreferencesController.hpp
    ClipMapAdapter.cpp
    ClipMapAdapter.hpp
    ConversionPipeline.cpp
    ConversionPipeline.hpp
    ConversionRecipe.cpp
    ConversionRecipe.hpp
    EditorProjectController.cpp
    EditorProjectControllerSprite.cpp
    EditorProjectController.hpp
    ImageInputController.cpp
    ImageInputController.hpp
    MediaToolDiscovery.cpp
    MediaToolDiscovery.hpp
    MediaProbe.cpp
    MediaProbe.hpp
    MediaExtraction.cpp
    MediaExtraction.hpp
    MediaClipController.cpp
    MediaClipController.hpp
    MediaBatchConversion.cpp
    MediaBatchConversion.hpp
    MediaBatchController.cpp
    MediaBatchController.hpp
)

# Keep Qt's autogen subprocess count conservative on Windows developer hosts.
set_property(TARGET retrovdp_app_support PROPERTY AUTOGEN_PARALLEL 1)

target_include_directories(retrovdp_app_support
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(retrovdp_app_support
    PUBLIC
        retrovdp_core
        retrovdp_media
        retrovdp_imageio
        Qt6::Core
        Qt6::Concurrent
        Qt6::Gui
)

target_compile_features(retrovdp_app_support PUBLIC cxx_std_20)

if(MSVC)
    target_compile_options(retrovdp_app_support PRIVATE /W4 /permissive-)
else()
    target_compile_options(retrovdp_app_support PRIVATE -Wall -Wextra -Wpedantic)
endif()

qt_add_executable(RetroVDPStudio
    main.cpp
)
set_property(TARGET RetroVDPStudio PROPERTY AUTOGEN_PARALLEL 1)

set_target_properties(RetroVDPStudio PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

target_compile_definitions(RetroVDPStudio
    PRIVATE RETROVDP_VERSION="${RETROVDP_VERSION}"
)

qt_add_qml_module(RetroVDPStudio
    URI RetroVDPStudio
    VERSION 1.0
    QML_FILES
        qml/Main.qml
        qml/MediaClipWorkspace.qml
        qml/PreferencesDialog.qml
        qml/ProjectDialog.qml
        qml/PreviewPane.qml
        qml/PreviewWorkspace.qml
        qml/SettingsPanel.qml
        qml/EditorWorkspacePlaceholder.qml
        qml/EditorSidePanelPlaceholder.qml
        qml/SidePanelHost.qml
        qml/ActiveTargetSummary.qml
        qml/CharacterSetView.qml
        qml/CharacterTilingView.qml
        qml/CharacterExtractionDialog.qml
        qml/CharacterPatternPreviewDialog.qml
        qml/PatternEditor.qml
        qml/SpritePatternEditor.qml
        qml/SpritePlacementView.qml
        qml/SpriteSetView.qml
        qml/ThemedFrame.qml
        qml/ThemedGroupBox.qml
    RESOURCES
        assets/icons/RetroVDPStudio-64.png
        assets/icons/RetroVDPStudio-256.png
        assets/icons/RetroVDPStudio-watermark-512.png
)

if(WIN32)
    # The desktop application must use the Windows GUI subsystem so launching
    # it from Explorer or an installer shortcut never opens a console window.
    # RetroVDPStudioCli intentionally remains a console-subsystem executable.
    set_target_properties(RetroVDPStudio PROPERTIES WIN32_EXECUTABLE TRUE)
    set(RETROVDP_WINDOWS_ICON
        "${CMAKE_CURRENT_SOURCE_DIR}/assets/icons/RetroVDPStudio.ico"
    )
    configure_file(
        assets/windows/RetroVDPStudio.rc.in
        "${CMAKE_CURRENT_BINARY_DIR}/RetroVDPStudio.rc"
        @ONLY
    )
    target_sources(RetroVDPStudio PRIVATE
        "${CMAKE_CURRENT_BINARY_DIR}/RetroVDPStudio.rc"
    )
endif()

if(APPLE)
    set(RETROVDP_MACOS_ICON
        "${CMAKE_CURRENT_SOURCE_DIR}/assets/icons/RetroVDPStudio.icns"
    )
    set_source_files_properties("${RETROVDP_MACOS_ICON}" PROPERTIES
        MACOSX_PACKAGE_LOCATION Resources
    )
    target_sources(RetroVDPStudio PRIVATE "${RETROVDP_MACOS_ICON}")
    set_target_properties(RetroVDPStudio PROPERTIES
        MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_ICON_FILE RetroVDPStudio.icns
        MACOSX_BUNDLE_GUI_IDENTIFIER "${RETROVDP_MACOS_BUNDLE_ID}"
        MACOSX_BUNDLE_BUNDLE_NAME "RetroVDP Studio"
        MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
    )
endif()

target_link_libraries(RetroVDPStudio
    PRIVATE
        retrovdp_app_support
        Qt6::Core
        Qt6::Gui
        Qt6::Quick
        Qt6::QuickControls2
        Qt6::Widgets
)

install(TARGETS RetroVDPStudio
    BUNDLE DESTINATION .
    RUNTIME DESTINATION bin
    COMPONENT Runtime
)

set(RETROVDP_DEPLOY_TOOL_OPTIONS)
if(APPLE)
    # Preview builds use an ad-hoc signature. Stable releases replace this
    # with the protected Developer ID identity in release automation.
    list(APPEND RETROVDP_DEPLOY_TOOL_OPTIONS "-codesign=-")
endif()

qt_generate_deploy_qml_app_script(
    TARGET RetroVDPStudio
    OUTPUT_SCRIPT RETROVDP_DEPLOY_SCRIPT
    MACOS_BUNDLE_POST_BUILD
    NO_UNSUPPORTED_PLATFORM_ERROR
    DEPLOY_USER_QML_MODULES_ON_UNSUPPORTED_PLATFORM
    EXCLUDE_PLUGIN_TYPES qmltooling
    DEPLOY_TOOL_OPTIONS ${RETROVDP_DEPLOY_TOOL_OPTIONS}
)
install(SCRIPT ${RETROVDP_DEPLOY_SCRIPT} COMPONENT Runtime)

qt_add_executable(RetroVDPStudioCli
    cli_main.cpp
)
set_property(TARGET RetroVDPStudioCli PROPERTY AUTOGEN_PARALLEL 1)

if(WIN32)
    configure_file(
        assets/windows/retrovdp-cli.rc.in
        "${CMAKE_CURRENT_BINARY_DIR}/retrovdp-cli.rc"
        @ONLY
    )
    target_sources(RetroVDPStudioCli PRIVATE
        "${CMAKE_CURRENT_BINARY_DIR}/retrovdp-cli.rc"
    )
endif()

set_target_properties(RetroVDPStudioCli PROPERTIES
    OUTPUT_NAME retrovdp-cli
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    WIN32_EXECUTABLE FALSE
    MACOSX_BUNDLE FALSE
)

if(APPLE)
    set_target_properties(RetroVDPStudioCli PROPERTIES
        INSTALL_RPATH
            "@loader_path/../RetroVDPStudio.app/Contents/Frameworks"
    )
elseif(UNIX)
    set_target_properties(RetroVDPStudio RetroVDPStudioCli PROPERTIES
        INSTALL_RPATH "$ORIGIN/../lib"
    )
endif()

target_compile_definitions(RetroVDPStudioCli
    PRIVATE RETROVDP_VERSION="${RETROVDP_VERSION}"
)

target_link_libraries(RetroVDPStudioCli
    PRIVATE
        retrovdp_app_support
        Qt6::Core
        Qt6::Gui
)

install(TARGETS RetroVDPStudioCli
    RUNTIME DESTINATION bin
    COMPONENT Runtime
)

if(UNIX AND NOT APPLE)
    foreach(icon_size 16 24 32 48 64 128 256 512 1024)
        install(
            FILES "assets/icons/RetroVDPStudio-${icon_size}.png"
            DESTINATION "share/icons/hicolor/${icon_size}x${icon_size}/apps"
            RENAME "${RETROVDP_LINUX_APP_ID}.png"
            COMPONENT Runtime
        )
    endforeach()
    install(
        FILES assets/RetroVDPStudio.svg
        DESTINATION share/icons/hicolor/scalable/apps
        RENAME "${RETROVDP_LINUX_APP_ID}.svg"
        COMPONENT Runtime
    )
    configure_file(
        assets/linux/io.github.ciscogarciafl.RetroVDPStudio.desktop.in
        "${CMAKE_CURRENT_BINARY_DIR}/${RETROVDP_LINUX_APP_ID}.desktop"
        @ONLY
    )
    install(
        FILES "${CMAKE_CURRENT_BINARY_DIR}/${RETROVDP_LINUX_APP_ID}.desktop"
        DESTINATION share/applications
        COMPONENT Runtime
    )
endif()
