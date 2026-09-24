#include "PlayStation.h"
#include "Constants.h"

PlayStation::PlayStation() : cpu(&bus) {
    // Default window size scaled up for modern displays
    const int windowWidth = 1280;
    const int windowHeight = 960;

    window = SDL_CreateWindow("PlayStation 1", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, windowWidth, windowHeight, SDL_WINDOW_SHOWN);
                              
    renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
    
    // Lock the internal rendering resolution to a standard 4:3 aspect ratio
    SDL_RenderSetLogicalSize(renderer, 640, 480);
    
    // The VRAM texture represents the physical 1MB memory block
    vramTexture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ABGR1555, SDL_TEXTUREACCESS_STREAMING, Hardware::VRAM_WIDTH, Hardware::VRAM_HEIGHT);
}


PlayStation::~PlayStation() {
    if (vramTexture) SDL_DestroyTexture(vramTexture);
    if (renderer) SDL_DestroyRenderer(renderer);
    if (window) SDL_DestroyWindow(window);
}


bool PlayStation::powerOn(const std::string& biosPath, const std::string& cuePath) {
    if (!bus.loadBIOS(biosPath)) {
        return false;
    }

    if (!cuePath.empty()) {
        bus.getCDROM().mount(cuePath);
    }

    return true;
}


void PlayStation::run() {
    std::cout << "BIOS: Powering on PlayStation 1" << std::endl;
    bool running = true;
    SDL_Event event;

    while (running) {
        uint32_t frameStartTicks = SDL_GetTicks();

        // Process OS Events
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) {
                running = false;
            }

            // TODO: Expand input routing to handle Controller 2 multiplexing
            bus.getSIO().getController().handleEvent(event);
        }

        // Force SDL to update its internal controller state before the frame begins
        SDL_GameControllerUpdate();

        // Execute Hardware Frame
        // TODO: Move from a fixed per-frame block to a precise cycle-stepped scheduler
        for (int i = 0; i < Hardware::CYCLES_PER_FRAME; i++) {
            cpu.step();
        }

        // Render Output
        // Copy the raw 1MB VRAM array directly into the SDL texture
        SDL_UpdateTexture(vramTexture, nullptr, bus.getGPU().getVRAMRawPointer(), Hardware::VRAM_WIDTH * sizeof(uint16_t));

        // Define the bounding box of the active display area, stripping out the overscan garbage
        SDL_Rect sourceRectangle;
        sourceRectangle.x = bus.getGPU().getDisplayAreaX();
        sourceRectangle.y = bus.getGPU().getDisplayAreaY() + Hardware::OVERSCAN_CROP_TOP;
        sourceRectangle.w = bus.getGPU().getDisplayWidth();
        sourceRectangle.h = bus.getGPU().getDisplayHeight() - Hardware::OVERSCAN_CROP_TOP - Hardware::OVERSCAN_CROP_BOTTOM;

        // Define the destination logical window size
        SDL_Rect destRectangle;
        destRectangle.x = 0;
        destRectangle.y = 0;
        destRectangle.w = 640;
        destRectangle.h = 480;

        SDL_RenderCopy(renderer, vramTexture, &sourceRectangle, &destRectangle);
        SDL_RenderPresent(renderer);
        
        // Frame Pacing
        uint32_t frameElapsedTicks = SDL_GetTicks() - frameStartTicks;

        if (frameElapsedTicks < Hardware::MILLISECONDS_PER_FRAME) {
            SDL_Delay(Hardware::MILLISECONDS_PER_FRAME - frameElapsedTicks);
        }
    }
}
