include(FetchContent)
cmake_minimum_required(VERSION 3.26)
project(never_cli)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/CMakeModules)


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
target_link_libraries(nvr_record PRIVATE CURL::libcurl PkgConfig::LIBAV -lm  nlohmann_json::nlohmann_json spdlog::spdlog $<$<BOOL:${MINGW}>:ws2_32>)

## Target: nvr_service_gen
add_executable(nvr_service_gen nvr_service_gen/service_gen.cpp)

## Target: nvr_stream
add_executable(nvr_stream  common.cpp common.h  nvr_stream/streamer.cpp nvr_stream/stream.cpp nvr_stream/streamer.h
        nvr_stream/janus.cpp
        nvr_stream/janus.h)
target_link_libraries(nvr_stream PRIVATE gstreamer-1.0 gobject-2.0 glib-2.0 curl nlohmann_json::nlohmann_json spdlog::spdlog $<$<BOOL:${MINGW}>:ws2_32>)
target_include_directories(nvr_stream PRIVATE ${GSTLIBS_INCLUDE_DIRS})
target_link_directories(nvr_stream PRIVATE ${GSTLIBS_LIBRARY_DIRS})