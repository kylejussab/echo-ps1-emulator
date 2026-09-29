cmake_minimum_required(VERSION 3.10)
project(echo-ps1-emulator)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

list(APPEND CMAKE_PREFIX_PATH "/opt/homebrew" "/usr/local")

find_package(SDL2 REQUIRED)

add_executable(echo 
    src/main.cpp

    src/cdrom/Disc.cpp
    src/cdrom/CDROM.cpp

    src/core/PlayStation.cpp
    src/core/Bus.cpp
    src/core/DMA.cpp

    src/cpu/CPU.cpp
    src/cpu/GTE.cpp

    src/gpu/GPU.cpp
    src/gpu/Rasterizer.cpp

    src/io/DualShock.cpp
    src/io/SIO.cpp

    src/mdec/MDEC.cpp
    
    src/spu/SPU.cpp)

target_include_directories(echo PRIVATE src)
target_link_libraries(echo PRIVATE SDL2::SDL2)

set_target_properties(echo PROPERTIES 
    BUILD_WITH_INSTALL_RPATH TRUE
    INSTALL_RPATH "/opt/homebrew/lib;/usr/local/lib;/Library/Frameworks"
)
