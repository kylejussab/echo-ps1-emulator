cmake_minimum_required(VERSION 3.10)
project(echo-ps1-emulator)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Link-time optimization: lets the compiler inline across .cpp files (e.g. Bus::advanceClock into the CPU loop)
include(CheckIPOSupported)
check_ipo_supported(RESULT ipo_supported OUTPUT ipo_error)
if(ipo_supported)
    set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE ON)
else()
    message(STATUS "LTO not supported, skipping: ${ipo_error}")
endif()

list(APPEND CMAKE_PREFIX_PATH "/opt/homebrew" "/usr/local")

find_package(SDL2 REQUIRED)

# Everything that makes up the emulated console. Both the emulator and the headless test runner link this.
add_library(echo_core STATIC
    src/cdrom/Disc.cpp
    src/cdrom/CDROM.cpp

    src/core/Bus.cpp
    src/core/DMA.cpp
    src/core/Timers.cpp

    src/cpu/CPU.cpp
    src/cpu/GTE.cpp

    src/gpu/GPU.cpp
    src/gpu/Rasterizer.cpp

    src/io/DualShock.cpp
    src/io/SIO.cpp

    src/mdec/MDEC.cpp

    src/spu/SPU.cpp)

target_include_directories(echo_core PUBLIC src)
target_link_libraries(echo_core PUBLIC SDL2::SDL2)

# The emulator itself (window, input, frame pacing)
add_executable(echo
    src/main.cpp
    src/core/PlayStation.cpp)
target_link_libraries(echo PRIVATE echo_core)

# Windowless runner used by ps1-unit-tester/run_tests.py
add_executable(echo-headless ps1-unit-tester/runner/echo_headless.cpp)
target_link_libraries(echo-headless PRIVATE echo_core)

# Component tests: every ps1-unit-tester/echo-tests/*.cpp becomes its own executable (echo-test-<name>) linked against echo_core.
# They are not part of the normal build; run_tests.py builds them through the `echo-tests` target and runs them.
# Adding a test is just adding a .cpp file there (CMake notices on the next build).
file(GLOB ECHO_UNIT_TEST_SOURCES CONFIGURE_DEPENDS ps1-unit-tester/echo-tests/*.cpp)
set(ECHO_UNIT_TEST_TARGETS)
foreach(unitTestSource ${ECHO_UNIT_TEST_SOURCES})
    get_filename_component(unitTestName ${unitTestSource} NAME_WE)
    add_executable(echo-test-${unitTestName} EXCLUDE_FROM_ALL ${unitTestSource})
    target_link_libraries(echo-test-${unitTestName} PRIVATE echo_core)
    set_target_properties(echo-test-${unitTestName} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/echo-tests
        BUILD_WITH_INSTALL_RPATH TRUE
        INSTALL_RPATH "/opt/homebrew/lib;/usr/local/lib;/Library/Frameworks"
    )
    list(APPEND ECHO_UNIT_TEST_TARGETS echo-test-${unitTestName})
endforeach()
add_custom_target(echo-tests)
if(ECHO_UNIT_TEST_TARGETS)
    add_dependencies(echo-tests ${ECHO_UNIT_TEST_TARGETS})
endif()

set_target_properties(echo echo-headless PROPERTIES
    BUILD_WITH_INSTALL_RPATH TRUE
    INSTALL_RPATH "/opt/homebrew/lib;/usr/local/lib;/Library/Frameworks"
)
