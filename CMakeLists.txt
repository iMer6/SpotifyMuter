cmake_minimum_required(VERSION 3.10)

# Project name
project(SpotifyMuter CXX)

# Executable file name
add_executable(SpotifyMuter WIN32 SpotifyMuter.cpp)

# Win32 libraries
target_link_libraries(SpotifyMuter PRIVATE
    user32
    shell32
    ole32
    uuid
)

# Warnings
target_compile_options(SpotifyMuter PRIVATE
    -Wall
    -Wextra
    -Wconversion
    -Wdouble-promotion
    -Wpedantic
    -Wshadow
    -Wformat=2
    -fdiagnostics-color=always
)

# Optimisation and compression
set(CMAKE_CXX_FLAGS_MINSIZEREL "-Os -s")