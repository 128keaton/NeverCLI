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

pkg_check_modules(LIBAV REQUIRED IMPORTED_TARGET
        libavdevice
        libavfilter
        libavformat
        libavcodec
        libswresample
        libswscale
        libavutil
)

add_executable(never_cli
        src/main.cpp
        src/common.cpp
        src/common.h
        src/camera.cpp
        src/camera.h
)



include_directories(${CURL_INCLUDE_DIR})

target_link_libraries(never_cli PRIVATE ${CURL_LIBRARIES})
target_link_libraries(never_cli PUBLIC "-lm")
target_link_libraries(never_cli PRIVATE PkgConfig::LIBAV)
target_link_libraries(never_cli PRIVATE nlohmann_json::nlohmann_json)