#include <iostream>
#include <string>
#include <vector>
#include <SDL3/SDL.h>
#include "core/Constants.h"
#include "core/PlayStation.h"

namespace {

/// @brief Temporary host for the standalone emulator: owns the window, the textures that show a DisplayFrame, the audio device, and frame pacing.
/// Step 3 deletes this: the dashboard's Renderer and single event loop take over all of these jobs.
class WindowHost {
public:
    WindowHost() {
        window = SDL_CreateWindow("Echo", 1280, 960, 0);
        if (window) renderer = SDL_CreateRenderer(window, nullptr);
        if (!window || !renderer) {
            std::cerr << "FATAL: could not create the window or renderer: " << SDL_GetError() << std::endl;
            return;
        }

        // Lock the internal rendering resolution to a standard 4:3 aspect ratio
        SDL_SetRenderLogicalPresentation(renderer, 640, 480, SDL_LOGICAL_PRESENTATION_LETTERBOX);

        texture15Bit = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ABGR1555, SDL_TEXTUREACCESS_STREAMING, Hardware::VRAM_WIDTH, Hardware::VRAM_HEIGHT);
        texture24Bit = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGB24, SDL_TEXTUREACCESS_STREAMING, Hardware::VRAM_WIDTH, Hardware::VRAM_HEIGHT);
        if (!texture15Bit || !texture24Bit) {
            std::cerr << "FATAL: could not create the display textures: " << SDL_GetError() << std::endl;
            return;
        }

        // SDL3 smooths scaled textures by default. Keep the sharp pixels SDL2 gave us.
        SDL_SetTextureScaleMode(texture15Bit, SDL_SCALEMODE_NEAREST);
        SDL_SetTextureScaleMode(texture24Bit, SDL_SCALEMODE_NEAREST);

        // PS1 VRAM bit 15 is the mask bit, not alpha, so don't let SDL blend with it
        SDL_SetTextureBlendMode(texture15Bit, SDL_BLENDMODE_NONE);
        SDL_SetTextureBlendMode(texture24Bit, SDL_BLENDMODE_NONE);

        SDL_InitSubSystem(SDL_INIT_AUDIO);

        // 1024 frame device buffer. Has to be set before the device opens.
        SDL_SetHint(SDL_HINT_AUDIO_DEVICE_SAMPLE_FRAMES, "1024");

        SDL_AudioSpec desired = {};
        desired.freq = 44100;
        desired.format = SDL_AUDIO_S16;
        desired.channels = 2;
        audioStream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &desired, nullptr, nullptr);

        // Streams start paused
        if (audioStream) SDL_ResumeAudioStreamDevice(audioStream);
        else std::cerr << "WARNING: no audio: " << SDL_GetError() << std::endl;
    }


    ~WindowHost() {
        if (audioStream) SDL_DestroyAudioStream(audioStream); // Also closes the audio device it opened
        if (texture15Bit) SDL_DestroyTexture(texture15Bit);
        if (texture24Bit) SDL_DestroyTexture(texture24Bit);
        if (renderer) SDL_DestroyRenderer(renderer);
        if (window) SDL_DestroyWindow(window);
    }


    WindowHost(const WindowHost&) = delete;
    WindowHost& operator=(const WindowHost&) = delete;


    bool isReady() const { return window && renderer && texture15Bit && texture24Bit; }


    /// @brief Queues the frame's audio, then holds the caller back until the queue is down to about 50 ms so audio and video stay in step.
    void queueAudio(const std::vector<int16_t>& samples) {
        if (!audioStream || samples.empty()) return;

        SDL_PutAudioStreamData(audioStream, samples.data(), static_cast<int>(samples.size() * sizeof(int16_t)));

        constexpr int TARGET_QUEUED_BYTES = 44100 * 4 / 20;
        while (SDL_GetAudioStreamQueued(audioStream) > TARGET_QUEUED_BYTES) {
            SDL_Delay(1);
        }
    }


    /// @brief Uploads the frame's pixels to the matching texture and draws the picture into the 640x480 letterboxed output.
    void present(const DisplayFrame& frame) {
        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
        SDL_RenderClear(renderer);

        if (frame.pixels) {
            SDL_Texture* texture = frame.is24Bit ? texture24Bit : texture15Bit;
            SDL_UpdateTexture(texture, nullptr, frame.pixels, frame.pitchBytes);

            // SDL3 draws textures with float rectangles
            const SDL_FRect source = { static_cast<float>(frame.sourceX), static_cast<float>(frame.sourceY),
                                       static_cast<float>(frame.sourceWidth), static_cast<float>(frame.sourceHeight) };
            const SDL_FRect destination = { static_cast<float>(frame.destinationX), static_cast<float>(frame.destinationY),
                                            static_cast<float>(frame.destinationWidth), static_cast<float>(frame.destinationHeight) };
            SDL_RenderTexture(renderer, texture, &source, &destination);
        }

        SDL_RenderPresent(renderer);
    }

private:
    SDL_Window* window = nullptr;
    SDL_Renderer* renderer = nullptr;
    SDL_Texture* texture15Bit = nullptr;
    SDL_Texture* texture24Bit = nullptr;
    SDL_AudioStream* audioStream = nullptr;
};


/// @brief The standalone emulator's frame loop. Returns when the window is closed.
void runWindowed(PlayStation& ps1) {
    WindowHost host;
    if (!host.isReady()) {
        return;
    }

    bool running = true;
    SDL_Event event;
    std::vector<int16_t> audioScratch;

    while (running) {
        uint32_t frameStartTicks = static_cast<uint32_t>(SDL_GetTicks());

        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_EVENT_QUIT) {
                running = false;
            }

            ps1.handleEvent(event);
        }

        SDL_UpdateGamepads();

        ps1.runFrame();

        ps1.drainAudio(audioScratch);
        host.queueAudio(audioScratch);

        host.present(ps1.getDisplayFrame());

        uint32_t frameElapsedTicks = static_cast<uint32_t>(SDL_GetTicks()) - frameStartTicks;

        if (frameElapsedTicks < Hardware::MILLISECONDS_PER_FRAME) {
            SDL_Delay(Hardware::MILLISECONDS_PER_FRAME - frameElapsedTicks);
        }
    }
}

} // namespace


int main(int argc, char* argv[]) {
    std::string biosPath = "assets/SCPH1001.BIN";
    std::string cuePath = "games/CTR - Crash Team Racing (USA)/CTR - Crash Team Racing (USA).cue";

    //Crash Bandicoot (USA)/Crash Bandicoot (USA).cue
    //Crash Bandicoot 2 - Cortex Strikes Back (USA)/Crash Bandicoot 2 - Cortex Strikes Back (USA).cue
    //CTR - Crash Team Racing (USA)/CTR - Crash Team Racing (USA).cue
    //Rayman (USA)/Rayman (USA).cue
    //Tetris Plus (USA)/Tetris Plus (USA).cue
    //Silent Hill (USA)/Silent Hill (USA).cue
    //Spyro the Dragon (USA)/Spyro the Dragon (USA).cue
    //Tomb Raider (USA)/Tomb Raider (USA).cue
    //Resident Evil 2 (USA) (Disc 1)/Resident Evil 2 (USA) (Disc 1).cue
    //Resident Evil 2 (USA) (Disc 2)/Resident Evil 2 (USA) (Disc 2).cue

    //Tekken 3 (USA)/Tekken 3 (USA).cue
    //Gran Turismo 2 (USA) (Arcade Mode)/Gran Turismo 2 (USA) (Arcade Mode).cue

    // Command line overrides, used by the dashboard to launch a game. With no arguments the defaults above are used.
    for (int i = 1; i < argc; i++) {
        std::string argument = argv[i];

        if (argument == "--game" && i + 1 < argc) {
            cuePath = argv[++i];
        }
        else if (argument == "--bios" && i + 1 < argc) {
            biosPath = argv[++i];
        }
        else {
            std::cout << "Unknown or incomplete argument: " << argument << std::endl;
            return 1;
        }
    }
    
    // Initialize SDL video and event subsystems
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        std::cout << "FATAL: Could not initialize SDL3! Error: " << SDL_GetError() << std::endl;
        return 1;
    }

    int exitCode = 0;

    // The console lives inside this block so it (and the controller, window and audio it uses) is destroyed BEFORE SDL_Quit()
    {
        PlayStation ps1;

        if (!ps1.powerOn(biosPath, cuePath)) {
            std::cout << "FATAL: Failed to power on the console! Check BIOS and CUE paths." << std::endl;
            exitCode = 1;
        }
        else {
            runWindowed(ps1);
        }
    }

    SDL_Quit();
    return exitCode;
}
