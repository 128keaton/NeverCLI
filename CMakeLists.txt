include(FetchContent)
cmake_minimum_required(VERSION 3.26)
project(never_cli)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/CMakeModules)
set(CMAKE_INSTALL_PREFIX "/usr/local")

message("When installed, executable binaries will be available in ${CMAKE_INSTALL_PREFIX}")

## DEP: pkg-config
find_package(PkgConfig REQUIRED)

## DEP: curl
find_package(CURL REQUIRED)

## DEP: spdlog
find_package(spdlog REQUIRED)

## DEP: json
FetchContent_Declare(json URL https://github.com/nlohmann/json/releases/download/v3.11.2/json.tar.xz)
FetchContent_MakeAvailable(json)

## DEP: ffmpeg
pkg_check_modules(LIBAV REQUIRED IMPORTED_TARGET
        libavdevice
        libavfilter
        libavformat
        libavcodec
        libswresample
        libswscale
        libavutil
)

## DEP: gstreamer
pkg_check_modules(GSTLIBS REQUIRED
        gobject-2.0
        gstreamer-1.0
        glib-2.0
)


## Target: nvr_record
add_executable(nvr_record nvr_record/record.cpp common.cpp common.h nvr_record/recorder.cpp nvr_record/recorder.h)
target_link_libraries(nvr_record PRIVATE CURL::libcurl PkgConfig::LIBAV -lm nlohmann_json::nlohmann_json spdlog::spdlog $<$<BOOL:${MINGW}>:ws2_32>)
install(TARGETS nvr_record DESTINATION bin)

## Target: nvr_stream
add_executable(nvr_stream common.cpp common.h nvr_stream/streamer.cpp nvr_stream/stream.cpp nvr_stream/streamer.h
        nvr_stream/janus.cpp
        nvr_stream/janus.h
)
target_link_libraries(nvr_stream PRIVATE gstreamer-1.0 gobject-2.0 glib-2.0 curl nlohmann_json::nlohmann_json spdlog::spdlog $<$<BOOL:${MINGW}>:ws2_32>)
target_include_directories(nvr_stream PRIVATE ${GSTLIBS_INCLUDE_DIRS})
target_link_directories(nvr_stream PRIVATE ${GSTLIBS_LIBRARY_DIRS})
install(TARGETS nvr_stream DESTINATION bin)


add_executable(nvr_timelapse nvr_timelapse/timelapse.cpp  common.cpp common.h)
target_link_libraries(nvr_timelapse PRIVATE CURL::libcurl PkgConfig::LIBAV -lm nlohmann_json::nlohmann_json spdlog::spdlog $<$<BOOL:${MINGW}>:ws2_32>)
install(TARGETS nvr_timelapse DESTINATION bin)

if (${CMAKE_SYSTEM_NAME} MATCHES "Linux")
    ## Target: nvr_status_manager (Linux only)
    add_executable(nvr_status_manager nvr_status_manager/nvr_status_server.cpp)
    add_executable(nvr_camera nvr_status_manager/nvr_status_client.cpp)

    #DEP: systemd
    pkg_check_modules(SYSTEMD REQUIRED IMPORTED_TARGET
            libsystemd
    )

    # Link systemd on Linux only
    target_link_libraries(nvr_status_manager PRIVATE PkgConfig::SYSTEMD)

    install(TARGETS nvr_status_manager DESTINATION bin)
endif ()
