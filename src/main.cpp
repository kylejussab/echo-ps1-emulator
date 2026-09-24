#include <iostream>
#include <SDL2/SDL.h>
#include "core/PlayStation.h"

int main(int argc, char* argv[]) {
    // TODO: Parse argc/argv to accept custom BIOS paths and game ISO/CUE files dynamically
    (void)argc;
    (void)argv;


    // Initialize SDL video and event subsystems
    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
        std::cout << "FATAL: Could not initialize SDL2!" << std::endl;
        return 1;
    }

    PlayStation ps1;

    if (!ps1.powerOn("assets/SCPH1001.BIN", "games/Tetris Plus (USA)/Tetris Plus (USA).cue")) {
        std::cout << "FATAL: Failed to load BIOS ROM!" << std::endl;
        SDL_Quit();
        return 1;
    }

    ps1.run();

    SDL_Quit();
    return 0;
}
