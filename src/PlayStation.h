#pragma once
#include <string>
#include <SDL2/SDL.h>
#include "Bus.h"
#include "CPU.h"

class PlayStation {
private:
    Bus bus;
    CPU cpu;

    // SDL Window Components
    SDL_Window* window = nullptr;
    SDL_Renderer* renderer = nullptr;
    SDL_Texture* vramTexture = nullptr;

public:
    PlayStation();
    ~PlayStation();
    bool powerOn(const std::string& biosPath);
    void run();
};
