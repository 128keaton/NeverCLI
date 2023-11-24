include(FetchContent)
cmake_minimum_required(VERSION 3.26.0)
project(never_cli)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall")

#DEP: pkg-config
find_package(PkgConfig REQUIRED)
## DEP: curl
find_package(CURL REQUIRED)

## DEP: spdlog
find_package(spdlog REQUIRED)

## DEP: json
FetchContent_Declare(json URL https://github.com/nlohmann/json/releases/download/v3.11.2/json.tar.xz)
FetchContent_MakeAvailable(json)
#DEP: ffmpeg
pkg_check_modules(LIBAV REQUIRED IMPORTED_TARGET
  	libavdevice
        libavfilter
        libavformat
        libavcodec
        libswresample
        libswscale
        libavutil
)

add_executable(nvr_timelapse timelapse.cpp  ../common.cpp ../common.h)
target_link_libraries(nvr_timelapse PRIVATE CURL::libcurl PkgConfig::LIBAV -lm nlohmann_json::nlohmann_json spdlog::spdlog $<$<BOOL:${MINGW}>:ws2_32>)
