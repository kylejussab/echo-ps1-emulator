#pragma once

#include <string>
#include <vector>
#include <SDL3/SDL.h>
#include "Bus.h"
#include "VideoOutput.h"
#include "../cpu/CPU.h"

/// @brief The emulated console and nothing else: no window, no renderer, no audio device, no event loop, no frame pacing.
/// A host (the standalone window today, the dashboard later) drives it one frame at a time and decides what to do with the picture and sound.
class PlayStation {
public:
    PlayStation();


    /// @brief Loads the BIOS and optionally mounts a disc.
    /// @param biosPath The filesystem path to the PS1 BIOS binary (e.g., SCPH1001.BIN).
    /// @param cuePath The filesystem path to the disc's .cue file, or empty for no disc.
    /// @return True if the BIOS loaded (and the disc mounted, if given), false otherwise.
    bool powerOn(const std::string& biosPath, const std::string& cuePath = "");


    /// @brief Runs one frame's worth of CPU work. Nothing else: no events, no drawing, no waiting.
    void runFrame();


    /// @brief Forwards a host SDL event (gamepad hotplug, Guide button) to the emulated controller.
    /// @param event The event to forward.
    void handleEvent(const SDL_Event& event);


    /// @brief Collects the audio the SPU produced since the last call.
    /// @param output Receives interleaved stereo 16-bit samples at 44100 Hz. The host owns this buffer and reuses it each frame.
    void drainAudio(std::vector<int16_t>& output);


    /// @brief Describes the picture for the current frame. Call after runFrame().
    /// @return The frame description. Its pixel pointer stays valid until the next call to this or to runFrame().
    DisplayFrame getDisplayFrame();

private:
    // Emulated Hardware
    Bus bus;
    CPU cpu;
    VideoOutput videoOutput;
};
