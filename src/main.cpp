#include <iostream>
#include <SDL2/SDL.h>
#include "Constants.h"
#include "Bus.h"
#include "CPU.h"

using namespace std;

int main(int argc, char* argv[]) {
    // Initalize SDL for now
    if (SDL_Init(SDL_INIT_VIDEO) < 0) return 1;

    // Power on the motherboard
    Bus bus;

    if(!bus.loadBIOS("SCPH1001.BIN")){
        SDL_Quit();
        return 1;
    }

    CPU cpu(&bus);

    while(true) {
        cpu.step();
    }

    SDL_Quit();
    return 0;
}
