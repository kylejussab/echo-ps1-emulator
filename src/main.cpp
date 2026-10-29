#include <iostream>
#include <string>
#include <SDL2/SDL.h>
#include "core/PlayStation.h"

int main(int argc, char* argv[]) {
    std::string biosPath = "assets/SCPH1001.BIN";
    std::string cuePath = "games/Resident Evil 2 (USA) (Disc 1)/Resident Evil 2 (USA) (Disc 1).cue";

    //Crash Bandicoot (USA)/Crash Bandicoot (USA).cue
    //CTR - Crash Team Racing (USA)/CTR - Crash Team Racing (USA).cue
    //Rayman (USA)/Rayman (USA).cue
    //Tetris Plus (USA)/Tetris Plus (USA).cue
    //Silent Hill (USA)/Silent Hill (USA).cue
    //Spyro the Dragon (USA)/Spyro the Dragon (USA).cue
    //Tomb Raider (USA)/Tomb Raider (USA).cue
    //Resident Evil 2 (USA) (Disc 1)/Resident Evil 2 (USA) (Disc 1).cue
    //Resident Evil 2 (USA) (Disc 2)/Resident Evil 2 (USA) (Disc 2).cue

    // Initialize SDL video and event subsystems
    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
        std::cout << "FATAL: Could not initialize SDL2! Error: " << SDL_GetError() << std::endl;
        return 1;
    }

    PlayStation ps1;

    if (!ps1.powerOn(biosPath, cuePath)) {
        std::cout << "FATAL: Failed to power on the console! Check BIOS and CUE paths." << std::endl;
        SDL_Quit();
        return 1;
    }

    ps1.run();

    SDL_Quit();
    return 0;
}
