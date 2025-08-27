cmake_minimum_required(VERSION 3.20)
project(topg VERSION 1.0.0)

# Set C++ standard to C++17
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_TOOLCHAIN_FILE "vcpkg/scripts/buildsystems/vcpkg.cmake" 
  CACHE STRING "Vcpkg toolchain file")

# Find required packages
find_package(fmt CONFIG REQUIRED)

# Define macro for httplib SSL support
find_package(RapidJSON CONFIG REQUIRED)
find_package(cxxopts CONFIG REQUIRED)

# Find uWebSockets package
find_package(unofficial-uwebsockets CONFIG REQUIRED)

# Add main executable
add_executable(topg
    topgsrv.cpp
)

# Link libraries for main executable
target_link_libraries(topg PRIVATE
    fmt::fmt
    unofficial::uwebsockets::uwebsockets
)

