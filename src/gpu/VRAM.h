#pragma once
#include <cstdint>
#include <vector>
#include "../core/Constants.h"

class VRAM {
public:
    VRAM() { data.resize(Hardware::VRAM_WIDTH * Hardware::VRAM_HEIGHT, 0); }


    /// @brief Reads a 16-bit pixel from VRAM at (x, y) with hardware coordinate wrapping.
    /// @param x Horizontal pixel coordinate (0-1023). Automatically wraps on overflow.
    /// @param y Vertical pixel coordinate (0-511). Automatically wraps on overflow.
    /// @return 16-bit BGR555 color value stored at the coordinates.
    inline uint16_t readPixel(uint32_t x, uint32_t y) const {
        uint32_t wrappedX = x & (Hardware::VRAM_WIDTH - 1);
        uint32_t wrappedY = y & (Hardware::VRAM_HEIGHT - 1);
        return data[wrappedY * Hardware::VRAM_WIDTH + wrappedX];
    }


    /// @brief Writes a 16-bit pixel to VRAM at (x, y) with hardware coordinate wrapping.
    /// @param x Horizontal pixel coordinate (0-1023). Automatically wraps on overflow.
    /// @param y Vertical pixel coordinate (0-511). Automatically wraps on overflow.
    /// @param color 16-bit BGR555 color value to write.
    inline void writePixel(uint32_t x, uint32_t y, uint16_t color) {
        uint32_t wrappedX = x & (Hardware::VRAM_WIDTH - 1);
        uint32_t wrappedY = y & (Hardware::VRAM_HEIGHT - 1);
        data[wrappedY * Hardware::VRAM_WIDTH + wrappedX] = color;
    }


    /// @brief Gets a direct read-only pointer to the raw VRAM buffer.
    /// @return Pointer to contiguous 16-bit pixel array (1024x512 elements).
    const uint16_t* getRawData() const { return data.data(); }


    /// @brief Sets the top-left offset in VRAM where the display controller samples video output.
    /// @param x Top-left X coordinate in VRAM (even-aligned per PS1 hardware specs).
    /// @param y Top-left Y coordinate in VRAM.
    void setDisplayArea(uint16_t x, uint16_t y) {
        displayAreaX = x & (Hardware::VRAM_WIDTH - 2); // PS1 requires even-aligned X
        displayAreaY = y & (Hardware::VRAM_HEIGHT - 1);
    }


    /// @brief Sets the active display output resolution.
    /// @param width Display width in pixels (e.g. 256, 320, 368, 512, 640).
    /// @param height Display height in pixels (e.g. 240, 480).
    void setDisplayDimensions(uint16_t width, uint16_t height) {
        displayWidth = width;
        displayHeight = height;
    }


    /// @brief Gets the current display start X coordinate in VRAM.
    uint16_t getDisplayAreaX() const { return displayAreaX; }


    /// @brief Gets the current display start Y coordinate in VRAM.
    uint16_t getDisplayAreaY() const { return displayAreaY; }


    /// @brief Gets the current display frame width in pixels.
    uint16_t getDisplayWidth() const { return displayWidth; }


    /// @brief Gets the current display frame height in pixels.
    uint16_t getDisplayHeight() const { return displayHeight; }

private:
    std::vector<uint16_t> data;

    uint16_t displayAreaX = 0;
    uint16_t displayAreaY = 0;
    uint16_t displayWidth = Hardware::GPU_DEFAULT_DISPLAY_WIDTH;
    uint16_t displayHeight = Hardware::GPU_DEFAULT_DISPLAY_HEIGHT;
};