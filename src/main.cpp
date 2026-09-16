#include <iostream>
#include <SDL2/SDL.h>
#include "PlayStation.h"

using namespace std;

int main(int argc, char* argv[]) {
    // Initalize SDL for now
    if (SDL_Init(SDL_INIT_VIDEO) < 0) return 1;

    PlayStation ps1;

    if (!ps1.powerOn("SCPH1001.BIN")) {
        cerr << "Failed to load BIOS ROM!" << endl;
        SDL_Quit();
        return 1;
    }

    cout << "Powering on PlayStation 1..." << endl;
    ps1.run();

    SDL_Quit();
    return 0;
}
