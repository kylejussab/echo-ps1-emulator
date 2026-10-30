#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include "Constants.h"
#include "PlayStation.h"

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
    displayTexture24Bit = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGB24, SDL_TEXTUREACCESS_STREAMING, Hardware::VRAM_WIDTH, Hardware::VRAM_HEIGHT);
	frameBuffer24Bit.resize(Hardware::VRAM_WIDTH * Hardware::VRAM_HEIGHT * 3);


    SDL_InitSubSystem(SDL_INIT_AUDIO);

    SDL_AudioSpec desired = {};
    desired.freq = 44100;
    desired.format = AUDIO_S16SYS;
    desired.channels = 2;
    desired.samples = 1024;
    desired.callback = nullptr;
    audioDevice = SDL_OpenAudioDevice(nullptr, 0, &desired, nullptr, 0);
    if (audioDevice) SDL_PauseAudioDevice(audioDevice, 0);
}


PlayStation::~PlayStation() {
    if (vramTexture) SDL_DestroyTexture(vramTexture);
    if (displayTexture24Bit) SDL_DestroyTexture(displayTexture24Bit);
    if (renderer) SDL_DestroyRenderer(renderer);
    if (audioDevice) SDL_CloseAudioDevice(audioDevice);
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
	bool running = true;
	SDL_Event event;
	uint64_t frameCounter = 0;

	while (running) {
		uint32_t frameStartTicks = SDL_GetTicks();

		while (SDL_PollEvent(&event)) {
			if (event.type == SDL_QUIT) {
				running = false;
			}

			/// @todo Expand input routing to handle Controller 2 multiplexing.
			bus.getSIO0().getController().handleEvent(event);
		}

		SDL_GameControllerUpdate();

		/// @todo Move from a fixed per-frame block to a precise cycle-stepped scheduler.
		for (int i = 0; i < Hardware::CYCLES_PER_FRAME; i++) {
			cpu.step();
		}

		bus.getSPU().drainOutput(audioScratch);
		if (audioDevice && !audioScratch.empty()) {
			SDL_QueueAudio(audioDevice, audioScratch.data(), static_cast<uint32_t>(audioScratch.size() * sizeof(int16_t)));

			// Hold the emulator back until the queue is down to about 50 ms, so audio and video stay in step
			constexpr uint32_t TARGET_QUEUED_BYTES = 44100 * 4 / 20;
			while (SDL_GetQueuedAudioSize(audioDevice) > TARGET_QUEUED_BYTES) {
				SDL_Delay(1);
			}
		}

		uint16_t vramWidth = bus.getGPU().getDisplayWidth();
		uint16_t vramHeight = bus.getGPU().getDisplayHeight();

		uint16_t cropTop = (vramHeight == 480) ? Hardware::OVERSCAN_CROP_480I : Hardware::OVERSCAN_CROP_240P;
		uint16_t cropBottom = (vramHeight == 480) ? Hardware::OVERSCAN_CROP_480I : Hardware::OVERSCAN_CROP_240P;

		uint16_t displayY1 = bus.getGPU().getVerticalDisplayStart();
		int jitterOffset = displayY1 - Hardware::DISPLAY_OFFSET_NTSC_V1;

		// In 480i mode, hardware V1 steps are in field lines, so the logical offset is doubled
		if (vramHeight == 480) {
			jitterOffset *= 2;
		}

		SDL_Rect sourceRectangle;
		sourceRectangle.x = bus.getGPU().getDisplayAreaX();
		// Base VRAM Y + Overscan Crop - Jitter Compensation
		sourceRectangle.y = bus.getGPU().getDisplayAreaY() + cropTop - jitterOffset;
		sourceRectangle.w = vramWidth;
		sourceRectangle.h = vramHeight - cropTop - cropBottom;

		// Prevent negative Y if jitterOffset goes wild on boot
		if (sourceRectangle.y < 0) sourceRectangle.y = 0;

		SDL_Texture* sourceTexture = vramTexture;

		if (bus.getGPU().isDisplay24Bit()) {
			// 24-bit mode: pixels are R, G, B byte triplets packed across VRAM halfwords, starting at displayAreaX (measured in halfwords)
			const uint8_t* vramBytes = reinterpret_cast<const uint8_t*>(bus.getGPU().getVRAMRawPointer());
			const size_t vramRowBytes = Hardware::VRAM_WIDTH * sizeof(uint16_t);
			const size_t startByte = static_cast<size_t>(bus.getGPU().getDisplayAreaX()) * 2;
			const size_t byteCount = static_cast<size_t>(vramWidth) * 3;
			const size_t firstChunkBytes = std::min(byteCount, vramRowBytes - startByte);

			for (size_t row = 0; row < Hardware::VRAM_HEIGHT; row++) {
				const uint8_t* sourceRow = vramBytes + row * vramRowBytes;
				uint8_t* destinationRow = frameBuffer24Bit.data() + row * Hardware::VRAM_WIDTH * 3;

				std::memcpy(destinationRow, sourceRow + startByte, firstChunkBytes);
				if (firstChunkBytes < byteCount) {
					// The row wraps around the right edge of VRAM
					std::memcpy(destinationRow + firstChunkBytes, sourceRow, byteCount - firstChunkBytes);
				}
			}

			SDL_UpdateTexture(displayTexture24Bit, nullptr, frameBuffer24Bit.data(), Hardware::VRAM_WIDTH * 3);
			sourceTexture = displayTexture24Bit;

			// The X offset was already applied while copying
			sourceRectangle.x = 0;
		} 
        else {
			SDL_UpdateTexture(vramTexture, nullptr, bus.getGPU().getVRAMRawPointer(), Hardware::VRAM_WIDTH * sizeof(uint16_t));
		}

		SDL_Rect destRectangle;

		destRectangle.x = 0;
		destRectangle.w = 640;

		// Y Scale: 240p gets line-doubled (2x), 480i is rendered 1:1 (1x)
		int scaleY = (vramHeight == 480) ? 1 : 2;

		// Scale the *cropped* height naturally to prevent stretching
		destRectangle.h = sourceRectangle.h * scaleY;

		// Center the image vertically in the 640x480 window (creates clean letterbox bars)
		destRectangle.y = (480 - destRectangle.h) / 2;

		SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
		SDL_RenderClear(renderer);

		SDL_RenderCopy(renderer, sourceTexture, &sourceRectangle, &destRectangle);
		SDL_RenderPresent(renderer);

		frameCounter++;

		uint32_t frameElapsedTicks = SDL_GetTicks() - frameStartTicks;

		if (frameElapsedTicks < Hardware::MILLISECONDS_PER_FRAME) {
			SDL_Delay(Hardware::MILLISECONDS_PER_FRAME - frameElapsedTicks);
		}
	}
}
