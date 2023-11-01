cmake_minimum_required(VERSION 3.26)
project(never_cli)

set(CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/CMakeModules)
find_package(LIBAV REQUIRED)

add_executable(never_cli main.cpp)

if(LIBAV_FOUND)
    message("Found FFMPEG/LibAV libraries")
    include_directories(${LIBAV_INCLUDE_DIR})
    target_link_libraries(never_cli ${LIBAV_LIBRARIES})
else()
    message("Can't find libavcodec, libavformat or libavutil. Add them!")
endif()

target_link_libraries(never_cli "-lm")

set(CMAKE_CXX_STANDARD 17)


set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -g -std=c99")