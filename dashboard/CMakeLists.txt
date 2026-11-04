# Echo: the dashboard and the emulator in one SDL3 app.
# Pulled into the build from the repo root with add_subdirectory(dashboard).
# C++ standard, build type, compile_commands and CMAKE_PREFIX_PATH are inherited from the root CMakeLists.

find_package(SDL3 REQUIRED CONFIG)
find_package(SDL3_ttf REQUIRED CONFIG)
find_package(SDL3_image REQUIRED)

add_executable(Echo
    src/main.cpp
    src/app/Dashboard.cpp
    src/app/GameAudio.cpp
    src/app/Preferences.cpp
    src/input/ControllerInput.cpp
    src/library/GameScanner.cpp
    src/library/DiscReader.cpp
    src/library/GameDatabase.cpp
    src/gfx/GPUTexture.cpp
    src/gfx/GameView.cpp
    src/ui/Tile.cpp
    src/ui/TextTile.cpp
    src/ui/IconTile.cpp
    src/ui/RingTile.cpp
    src/ui/GameTile.cpp
    src/ui/widgets/WidgetTile.cpp
    src/ui/widgets/ControllerWidget.cpp
    src/gfx/Renderer.cpp
    src/library/GameLibrary.cpp
    src/ui/CollectionGrid.cpp
    src/ui/RecentlyPlayedRow.cpp
    src/ui/EmptyState.cpp
    src/ui/TabFader.cpp
    src/app/FocusManager.cpp
    src/ui/WidgetRow.cpp
    src/ui/NavBar.cpp
    src/app/UserData.cpp
    src/ui/MenuTile.cpp
    src/ui/GameBackground.cpp)

# CMAKE_CURRENT_SOURCE_DIR, not CMAKE_SOURCE_DIR: the latter points at the repo root (the emulator's src).
# Linking echo_core also puts the emulator's src on the include path, so emulator headers are included as "core/PlayStation.h" and so on.
target_include_directories(Echo PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(Echo PRIVATE echo_core SDL3::SDL3 SDL3_ttf::SDL3_ttf SDL3_image::SDL3_image)

# Copy the runtime assets next to the executable (build/dashboard/)
file(COPY shaders fonts data icons boxart backgrounds DESTINATION ${CMAKE_CURRENT_BINARY_DIR})

# The emulator's BIOS, next to the executable
file(COPY ${CMAKE_SOURCE_DIR}/assets/SCPH1001.BIN DESTINATION ${CMAKE_CURRENT_BINARY_DIR}/assets)
