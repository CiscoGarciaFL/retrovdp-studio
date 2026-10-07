add_library(retrovdp_media STATIC
    ClipMap.cpp
    MediaTimeline.cpp
)

target_include_directories(retrovdp_media
    PUBLIC
        ${PROJECT_SOURCE_DIR}/include
)

target_link_libraries(retrovdp_media PUBLIC retrovdp_core)
target_compile_features(retrovdp_media PUBLIC cxx_std_20)
set_target_properties(retrovdp_media PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)

if(MSVC)
    target_compile_options(retrovdp_media PRIVATE /W4 /permissive-)
else()
    target_compile_options(retrovdp_media PRIVATE -Wall -Wextra -Wpedantic)
endif()
