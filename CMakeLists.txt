cmake_minimum_required(VERSION 3.16)
project(mod_player VERSION 0.1.0 LANGUAGES C)

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

# Compiler warnings
add_compile_options(-Wall -Wextra -Wpedantic)

# Find dependencies via pkg-config
find_package(PkgConfig REQUIRED)
pkg_check_modules(SDL2 REQUIRED IMPORTED_TARGET sdl2)
pkg_check_modules(OPENMPT REQUIRED IMPORTED_TARGET libopenmpt)
pkg_check_modules(CURL REQUIRED IMPORTED_TARGET libcurl)
pkg_check_modules(GL REQUIRED IMPORTED_TARGET gl)
pkg_check_modules(GLEW REQUIRED IMPORTED_TARGET glew)

# Collect source files
file(GLOB SOURCES src/*.c)

# Executable
add_executable(mod_player ${SOURCES})

target_include_directories(mod_player PRIVATE src)

target_link_libraries(mod_player PRIVATE
    PkgConfig::SDL2
    PkgConfig::OPENMPT
    PkgConfig::CURL
    PkgConfig::GLEW
    PkgConfig::GL
    m
)

# Install
install(TARGETS mod_player RUNTIME DESTINATION bin)
