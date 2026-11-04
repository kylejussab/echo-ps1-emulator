#include <algorithm>
#include <cstring>
#include "Constants.h"
#include "VideoOutput.h"
#include "../gpu/GPU.h"

VideoOutput::VideoOutput() {
    frameBuffer24Bit.resize(Hardware::VRAM_WIDTH * Hardware::VRAM_HEIGHT * 3);
}


DisplayFrame VideoOutput::capture(GPU& gpu) {
    DisplayFrame frame;

    uint16_t vramWidth = gpu.getDisplayWidth();
    uint16_t vramHeight = gpu.getDisplayHeight();

    uint16_t cropTop = (vramHeight == 480) ? Hardware::OVERSCAN_CROP_480I : Hardware::OVERSCAN_CROP_240P;
    uint16_t cropBottom = (vramHeight == 480) ? Hardware::OVERSCAN_CROP_480I : Hardware::OVERSCAN_CROP_240P;

    uint16_t displayY1 = gpu.getVerticalDisplayStart();
    int jitterOffset = displayY1 - Hardware::DISPLAY_OFFSET_NTSC_V1;

    // In 480i mode, hardware V1 steps are in field lines, so the logical offset is doubled
    if (vramHeight == 480) {
        jitterOffset *= 2;
    }

    frame.sourceX = gpu.getDisplayAreaX();
    // Base VRAM Y + Overscan Crop - Jitter Compensation
    frame.sourceY = gpu.getDisplayAreaY() + cropTop - jitterOffset;
    frame.sourceWidth = vramWidth;
    frame.sourceHeight = vramHeight - cropTop - cropBottom;

    // Prevent negative Y if jitterOffset goes wild on boot
    if (frame.sourceY < 0) frame.sourceY = 0;

    if (gpu.isDisplay24Bit()) {
        // 24-bit mode: pixels are R, G, B byte triplets packed across VRAM halfwords, starting at displayAreaX (measured in halfwords)
        const uint8_t* vramBytes = reinterpret_cast<const uint8_t*>(gpu.getVRAMRawPointer());
        const size_t vramRowBytes = Hardware::VRAM_WIDTH * sizeof(uint16_t);
        const size_t startByte = static_cast<size_t>(gpu.getDisplayAreaX()) * 2;
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

        frame.pixels = frameBuffer24Bit.data();
        frame.pitchBytes = Hardware::VRAM_WIDTH * 3;
        frame.is24Bit = true;

        // The X offset was already applied while copying
        frame.sourceX = 0;
    }
    else {
        frame.pixels = reinterpret_cast<const uint8_t*>(gpu.getVRAMRawPointer());
        frame.pitchBytes = Hardware::VRAM_WIDTH * sizeof(uint16_t);
        frame.is24Bit = false;
    }

    frame.destinationX = 0;
    frame.destinationWidth = 640;

    // Y Scale: 240p gets line-doubled (2x), 480i is rendered 1:1 (1x)
    int scaleY = (vramHeight == 480) ? 1 : 2;

    // Scale the *cropped* height naturally to prevent stretching
    frame.destinationHeight = frame.sourceHeight * scaleY;

    // Center the image vertically in the 640x480 space (creates clean letterbox bars)
    frame.destinationY = (480 - frame.destinationHeight) / 2;

    return frame;
}
