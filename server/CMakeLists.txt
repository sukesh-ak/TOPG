cmake_minimum_required(VERSION 3.20)

# vcpkg toolchain must be set before project(); only fall back to the local
# clone when the caller didn't pass -DCMAKE_TOOLCHAIN_FILE
if(NOT DEFINED CMAKE_TOOLCHAIN_FILE AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/vcpkg/scripts/buildsystems/vcpkg.cmake")
    set(CMAKE_TOOLCHAIN_FILE "${CMAKE_CURRENT_SOURCE_DIR}/vcpkg/scripts/buildsystems/vcpkg.cmake"
        CACHE STRING "Vcpkg toolchain file")
endif()

project(topg VERSION 1.0.0)

# Set C++ standard to C++17
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Find required packages
find_package(cxxopts CONFIG REQUIRED)
find_package(unofficial-uwebsockets CONFIG REQUIRED)

# Add main executable
add_executable(topg
    topgsrv.cpp
)

# Link libraries for main executable
target_link_libraries(topg PRIVATE
    cxxopts::cxxopts
    unofficial::uwebsockets::uwebsockets
)

if(MSVC)
    # Source contains UTF-8 emoji literals; keep windows.h from defining min/max macros
    target_compile_options(topg PRIVATE /utf-8)
    target_compile_definitions(topg PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    # Static vcpkg triplets link static libs against the static CRT; the exe must match
    if(VCPKG_TARGET_TRIPLET MATCHES "-static$")
        set_property(TARGET topg PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    endif()
else()
    find_package(Threads REQUIRED)
    target_link_libraries(topg PRIVATE Threads::Threads)
endif()
