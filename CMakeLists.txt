cmake_minimum_required(VERSION 3.10)
project(project-x1fc)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

list(APPEND CMAKE_PREFIX_PATH "/opt/homebrew" "/usr/local")

find_package(SDL2 REQUIRED)

add_executable(x1fc src/main.cpp)

target_link_libraries(x1fc PRIVATE SDL2::SDL2)

set_target_properties(x1fc PROPERTIES 
    BUILD_WITH_INSTALL_RPATH TRUE
    INSTALL_RPATH "/opt/homebrew/lib;/usr/local/lib;/Library/Frameworks"
)
