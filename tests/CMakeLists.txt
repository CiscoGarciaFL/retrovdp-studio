add_executable(test_core_validation
    test_validation.cpp
)

target_link_libraries(test_core_validation
    PRIVATE
        retrovdp_core
        Qt6::Gui
)

target_compile_definitions(test_core_validation
    PRIVATE
        RETROVDP_GOLDEN_DIR="${CMAKE_CURRENT_SOURCE_DIR}/golden"
)

# The test uses a small standalone runner and requires no Qt meta-object code.
set_target_properties(test_core_validation PROPERTIES AUTOMOC OFF)

add_test(NAME core_validation COMMAND test_core_validation)

add_test(NAME portable_dependency_validation
    COMMAND ${CMAKE_COMMAND}
        -DSOURCE_ROOT=${PROJECT_SOURCE_DIR}
        -P ${PROJECT_SOURCE_DIR}/tools/check_portable_dependencies.cmake
)

add_executable(test_image_input
    test_image_input.cpp
)

target_link_libraries(test_image_input
    PRIVATE
        retrovdp_imageio
        Qt6::Core
        Qt6::Gui
)

set_target_properties(test_image_input PROPERTIES AUTOMOC OFF)

add_test(NAME image_input_validation COMMAND test_image_input)

add_executable(test_export_formats
    test_export_formats.cpp
)

target_link_libraries(test_export_formats
    PRIVATE
        retrovdp_formats
)

target_compile_definitions(test_export_formats
    PRIVATE
        RETROVDP_GOLDEN_DIR="${CMAKE_CURRENT_SOURCE_DIR}/golden"
)

set_target_properties(test_export_formats PROPERTIES AUTOMOC OFF)

add_test(NAME export_format_validation COMMAND test_export_formats)

add_executable(test_export_writer
    test_export_writer.cpp
)

target_link_libraries(test_export_writer
    PRIVATE
        retrovdp_imageio
        Qt6::Core
        Qt6::Gui
)

set_target_properties(test_export_writer PROPERTIES AUTOMOC OFF)

add_test(NAME export_writer_validation COMMAND test_export_writer)

add_executable(test_media_timeline
    test_media_timeline.cpp
)
target_link_libraries(test_media_timeline PRIVATE retrovdp_media)
set_target_properties(test_media_timeline PROPERTIES AUTOMOC OFF)
add_test(NAME media_timeline_validation COMMAND test_media_timeline)

add_executable(test_clip_maps
    test_clip_maps.cpp
)
target_link_libraries(test_clip_maps
    PRIVATE
        retrovdp_app_support
        Qt6::Core
)
set_target_properties(test_clip_maps PROPERTIES AUTOMOC OFF)
add_test(NAME clip_map_validation COMMAND test_clip_maps)

add_executable(media_ffmpeg_fixture
    media_tool_fixture.cpp
)
target_link_libraries(media_ffmpeg_fixture PRIVATE Qt6::Core)
target_compile_definitions(media_ffmpeg_fixture PRIVATE MEDIA_TOOL_NAME="ffmpeg")
set_target_properties(media_ffmpeg_fixture PROPERTIES AUTOMOC OFF)

add_executable(media_ffprobe_fixture
    media_tool_fixture.cpp
)
target_link_libraries(media_ffprobe_fixture PRIVATE Qt6::Core)
target_compile_definitions(media_ffprobe_fixture PRIVATE MEDIA_TOOL_NAME="ffprobe")
set_target_properties(media_ffprobe_fixture PROPERTIES AUTOMOC OFF)

add_executable(test_media_tools
    test_media_tools.cpp
)
target_link_libraries(test_media_tools
    PRIVATE
        retrovdp_app_support
        Qt6::Core
)
target_compile_definitions(test_media_tools
    PRIVATE
        RETROVDP_FFMPEG_FIXTURE_PATH="$<TARGET_FILE:media_ffmpeg_fixture>"
        RETROVDP_FFPROBE_FIXTURE_PATH="$<TARGET_FILE:media_ffprobe_fixture>"
        RETROVDP_GOLDEN_DIR="${CMAKE_CURRENT_SOURCE_DIR}/golden"
        RETROVDP_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures"
)
add_dependencies(test_media_tools media_ffmpeg_fixture media_ffprobe_fixture)
set_target_properties(test_media_tools PROPERTIES AUTOMOC OFF)
add_test(NAME media_tool_discovery_validation COMMAND test_media_tools)

add_executable(test_interface_workflow
    test_interface_workflow.cpp
)

target_link_libraries(test_interface_workflow
    PRIVATE
        retrovdp_app_support
        Qt6::Core
        Qt6::Gui
        Qt6::Qml
        Qt6::Quick
        Qt6::QuickControls2
)

target_compile_definitions(test_interface_workflow
    PRIVATE
        RETROVDP_GOLDEN_DIR="${CMAKE_CURRENT_SOURCE_DIR}/golden"
        RETROVDP_QML_DIR="${PROJECT_SOURCE_DIR}/app/qml"
        RETROVDP_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures"
        RETROVDP_VERSION="${RETROVDP_VERSION}"
)

set_target_properties(test_interface_workflow PROPERTIES AUTOMOC OFF)

add_test(NAME interface_workflow_validation COMMAND test_interface_workflow)
set_tests_properties(interface_workflow_validation PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_SCALE_FACTOR=1.25"
)

add_executable(test_cli_workflow
    test_cli_workflow.cpp
)

target_link_libraries(test_cli_workflow
    PRIVATE Qt6::Core
)

target_compile_definitions(test_cli_workflow
    PRIVATE
        RETROVDP_CLI_PATH="$<TARGET_FILE:RetroVDPStudioCli>"
        RETROVDP_FFMPEG_FIXTURE_PATH="$<TARGET_FILE:media_ffmpeg_fixture>"
        RETROVDP_FFPROBE_FIXTURE_PATH="$<TARGET_FILE:media_ffprobe_fixture>"
        RETROVDP_GOLDEN_DIR="${CMAKE_CURRENT_SOURCE_DIR}/golden"
        RETROVDP_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures"
)

add_dependencies(test_cli_workflow RetroVDPStudioCli media_ffmpeg_fixture media_ffprobe_fixture)
set_target_properties(test_cli_workflow PROPERTIES AUTOMOC OFF)

add_test(NAME cli_workflow_validation COMMAND test_cli_workflow)
set_tests_properties(cli_workflow_validation PROPERTIES TIMEOUT 120)

add_executable(benchmark_core_conversion
    benchmark_conversion.cpp
)

target_link_libraries(benchmark_core_conversion
    PRIVATE
        retrovdp_core
)

set_target_properties(benchmark_core_conversion PROPERTIES AUTOMOC OFF)
