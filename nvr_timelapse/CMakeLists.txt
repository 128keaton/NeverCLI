cmake_minimum_required(VERSION 3.26.0)
project(never_cli)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall")

#DEP: pkg-config
find_package(PkgConfig REQUIRED)

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

add_executable(nvr_timelapse main.cpp)
target_link_libraries(nvr_timelapse PkgConfig::LIBAV)
