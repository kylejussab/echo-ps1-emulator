#include <iostream>
#include <SDL2/SDL.h>

using namespace std;

int main(int argc, char* argv[]) {
    // Initalize SDL for now
    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
        cerr << "SDL could not initialize! Error: " << SDL_GetError() << endl;
        return 1;
    }

    // Emulator logic will live here

    SDL_Quit();
    return 0;
}
