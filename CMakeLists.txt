include(FetchContent)
cmake_minimum_required(VERSION 3.26)
project(never_cli)

set(CMAKE_CXX_STANDARD 17)
# Don't add this line if you will try_compile with boost.
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/CMakeModules)

FetchContent_Declare(json URL https://github.com/nlohmann/json/releases/download/v3.11.2/json.tar.xz)
FetchContent_MakeAvailable(json)


find_package(PkgConfig REQUIRED)
find_package(CURL REQUIRED)
find_package(spdlog REQUIRED)


pkg_check_modules(LIBAV REQUIRED IMPORTED_TARGET
        libavdevice
        libavfilter
        libavformat
        libavcodec
        libswresample
        libswscale
        libavutil
)

add_executable(nvr_record
        nvr_record/main.cpp
        common.cpp
        common.h
        nvr_record/camera.cpp
        nvr_record/camera.h
)


target_link_libraries(nvr_record PRIVATE CURL::libcurl PkgConfig::LIBAV -lm  nlohmann_json::nlohmann_json spdlog::spdlog $<$<BOOL:${MINGW}>:ws2_32>)



pkg_check_modules(GSTLIBS REQUIRED
        gobject-2.0
        gstreamer-1.0
        glib-2.0
)

include_directories(${GSTLIBS_INCLUDE_DIRS})
link_libraries(gstreamer-1.0 gobject-2.0 glib-2.0 curl)
link_directories(${GSTLIBS_LIBRARY_DIRS})

add_executable(nvr_stream  common.cpp common.h  nvr_stream/streamer.cpp)
add_executable(nvr_service_gen nvr_service_gen/service_gen.cpp)

target_link_libraries(nvr_stream PRIVATE nlohmann_json::nlohmann_json spdlog::spdlog $<$<BOOL:${MINGW}>:ws2_32>)
