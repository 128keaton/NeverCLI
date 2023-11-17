cmake_minimum_required(VERSION 3.26.0)
project(never_cli)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall") #append flags
set(EXECUTABLE_OUTPUT_PATH "${CMAKE_BINARY_DIR}/bin")
message("executable binary will be installed in ${EXECUTABLE_OUTPUT_PATH}")

#DEP: pkg-config
find_package(PkgConfig REQUIRED)

#DEP: systemd
pkg_check_modules(SYSTEMD REQUIRED IMPORTED_TARGET
    libsystemd
)

#TARGET: nvr_camera
add_executable(nvr_camera main.cpp)
target_link_libraries(nvr_camera PRIVATE PkgConfig::SYSTEMD)