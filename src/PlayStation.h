#pragma once
#include <string>
#include <SDL2/SDL.h>
#include "Bus.h"
#include "CPU.h"

class PlayStation {
public:
    PlayStation();
    ~PlayStation();


    /// @brief Initializes the hardware, loads the BIOS, and sets up the SDL window.
    /// @param biosPath The filesystem path to the PS1 BIOS binary (e.g., SCPH1001.BIN).
    /// @return True if initialization and loading succeeded, false otherwise.
    bool powerOn(const std::string& biosPath);


    /// @brief Enters the main execution loop, pumping OS events, stepping the CPU, and rendering frames.
    void run();

private:
    // Emulated Hardware
    Bus bus;
    CPU cpu;

    // SDL Host Components
    SDL_Window* window = nullptr;
    SDL_Renderer* renderer = nullptr;
    SDL_Texture* vramTexture = nullptr;
};
