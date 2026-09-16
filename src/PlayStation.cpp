#include "PlayStation.h"
#include "Constants.h"

PlayStation::PlayStation() : cpu(&bus) {
     // Initialize a 640x480 window (PS1 typically scales up cleanly from 320x240 or 640x480)
    window = SDL_CreateWindow("PlayStation 1",  SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 640, 480, SDL_WINDOW_SHOWN);
                              
    renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    SDL_RenderSetLogicalSize(renderer, 640, 480);
    
    // Create a texture representing a standard display frame or the VRAM slice
    vramTexture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ABGR1555, SDL_TEXTUREACCESS_STREAMING, 1024, 512);
}

PlayStation::~PlayStation() {
    if (vramTexture) SDL_DestroyTexture(vramTexture);
    if (renderer) SDL_DestroyRenderer(renderer);
    if (window) SDL_DestroyWindow(window);
}

bool PlayStation::powerOn(const std::string& biosPath) {
    if (!bus.loadBIOS(biosPath)) {
        return false;
    }
    return true;
}

void PlayStation::run() {
    bool running = true;
    SDL_Event event;

    const int cyclesPerFrame = static_cast<int>(Hardware::CPU_CLOCK_SPEED_HERTZ / Hardware::TARGET_FRAMES_PER_SECOND);
    const Uint32 targetMillisecondsPerFrame = static_cast<Uint32>(1000.0 / Hardware::TARGET_FRAMES_PER_SECOND);

    while (running) {
        Uint32 frameStartTicks = SDL_GetTicks();

        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) {
                running = false;
            }
        }

        for (int i = 0; i < cyclesPerFrame; i++) {
            cpu.step();
        }

        bus.triggerHardwareInterrupt(0x0001);

        SDL_UpdateTexture(vramTexture, nullptr, bus.getGPU().getVRAMRawPointer(), 1024 * sizeof(uint16_t));

        SDL_Rect sourceRectangle;
        sourceRectangle.x = bus.getGPU().getDisplayAreaX();
        sourceRectangle.y = bus.getGPU().getDisplayAreaY();
        sourceRectangle.w = bus.getGPU().getDisplayWidth();
        sourceRectangle.h = bus.getGPU().getDisplayHeight(); // Now properly 240 or 480

        SDL_Rect destRectangle;
        destRectangle.x = 0;
        destRectangle.y = 0;
        destRectangle.w = 640;
        destRectangle.h = 480;

        SDL_RenderCopy(renderer, vramTexture, &sourceRectangle, &destRectangle);
        SDL_RenderPresent(renderer);
        

        Uint32 frameElapsedTicks = SDL_GetTicks() - frameStartTicks;
        if (frameElapsedTicks < targetMillisecondsPerFrame) {
            SDL_Delay(targetMillisecondsPerFrame - frameElapsedTicks);
        }
    }
}
