#pragma once

#include <cstdint>
#include <vector>

class GPU;

/// @brief Describes "the picture for this frame": where the pixels are, how to read them, and which part of them to show.
/// Pure data with no window or SDL types, so anything (an SDL texture today, the dashboard Renderer later) can present it.
/// The pixel buffer is always Hardware::VRAM_WIDTH x Hardware::VRAM_HEIGHT.
struct DisplayFrame {
    /// @brief First byte of the pixel buffer, or null if there is nothing to show. Valid until the next capture or until the GPU runs again.
    const uint8_t* pixels = nullptr;

    /// @brief Bytes per row of the pixel buffer.
    int pitchBytes = 0;

    /// @brief True if the buffer holds 24-bit RGB triplets, false if it holds PS1 15-bit VRAM halfwords.
    bool is24Bit = false;

    /// @brief The region of the pixel buffer that contains the picture, in pixels.
    int sourceX = 0;
    int sourceY = 0;
    int sourceWidth = 0;
    int sourceHeight = 0;

    /// @brief Where the picture goes in a 640x480 (4:3) output space, letterboxed and vertically centered.
    int destinationX = 0;
    int destinationY = 0;
    int destinationWidth = 640;
    int destinationHeight = 480;
};


/// @brief Turns the GPU's display registers and VRAM into a DisplayFrame: overscan crop, scanline jitter compensation,
/// 24-bit conversion, and the 4:3 destination rectangle. It does no drawing.
class VideoOutput {
public:
    VideoOutput();

    /// @brief Builds the picture for the current frame.
    /// @param gpu The console's GPU.
    /// @return The frame description. For 15-bit modes it points straight at VRAM, for 24-bit modes at this object's own converted buffer.
    DisplayFrame capture(GPU& gpu);

private:
    std::vector<uint8_t> frameBuffer24Bit;
};
