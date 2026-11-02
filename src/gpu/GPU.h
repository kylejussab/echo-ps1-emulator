#pragma once

#include <cstdint>
#include <iostream>
#include <vector>
#include "Rasterizer.h"
#include "VRAM.h"
#include "../core/Constants.h"


/// @brief Emulates the PS1 Graphics Processing Unit, handling display timing, VRAM management, and rendering primitives.
class GPU {
public:
    GPU();
    
    
    /// @brief Reads the next data word from the GP0 port.
	/// @return The next pixel pair from an in-progress VRAM-to-CPU transfer, or 0 if none is pending.
    uint32_t readGP0();


    /// @brief Writes a command or data word to the GP0 port (drawing commands and VRAM transfers).
	/// @param value The 32-bit word being written.
    void writeGP0(uint32_t value);


    /// @brief Reads the GPU status register via the GP1 port.
	/// @return The current status register, after side effects (FIFO-ready bits, odd/even frame toggle) needed to satisfy BIOS polling.
    uint32_t readGP1();


    /// @brief Writes a control word to the GP1 port (display control and GPU/command-buffer reset).
	/// @param value The 32-bit word being written.
    void writeGP1(uint32_t value);


    /// @brief Drains the GPU's outstanding draw work. Called once per CPU instruction.
    /// @param cycles The number of CPU cycles that have passed.
    void advanceClock(int cycles);


    /// @brief Checks and clears the GPU interrupt request flag.
    /// @return True if an interrupt was pending and cleared, false otherwise.
    bool consumeInterruptRequest();


    /// @brief Toggles the odd/even frame bit (Bit 31) in the Status Register during VBlank.
    void toggleFrameBit();


    /// @brief Gets a direct read-only pointer to the raw VRAM buffer.
	/// @return Pointer to contiguous 16-bit pixel array (1024x512 elements).
    const uint16_t* getVRAMRawPointer() const { return vram.getRawData(); }


    /// @brief Gets the X coordinate of the active display area in VRAM.
    /// @return The starting X coordinate (0-1023).
    uint16_t getDisplayAreaX() const { return vram.getDisplayAreaX(); }
    
    /// @brief Gets the Y coordinate of the active display area in VRAM.
    /// @return The starting Y coordinate (0-511).
    uint16_t getDisplayAreaY() const { return vram.getDisplayAreaY(); }
   
    /// @brief Gets the configured width of the active display area.
    /// @return The display width in pixels.
    uint16_t getDisplayWidth() const { return vram.getDisplayWidth(); }
   
    /// @brief Gets the configured height of the active display area.
    /// @return The display height in pixels.
    uint16_t getDisplayHeight() const { return vram.getDisplayHeight(); }


    /// @brief Gets the configured start of the vertical display range.
    /// @return The start line of the display (0-511).
    uint16_t getVerticalDisplayStart() const { return verticalDisplayRangeY1; }


    /// @brief Gets the configured end of the vertical display range.
    /// @return The end line of the display (0-511).
    uint16_t getVerticalDisplayEnd() const { return verticalDisplayRangeY2; }


    // @brief Checks if the display is configured for 24-bit color mode.
    /// @return True if 24-bit mode is enabled (Bit 21 of GPUSTAT), false for 15-bit.
    bool isDisplay24Bit() const { return (gpuStatusRegister & 0x00200000) != 0; }

private:
    VRAM vram;
    Rasterizer rasterizer;
    uint32_t gpuStatusRegister = Hardware::GPU_DEFAULT_STATUS;
    int64_t busyCycles = 0; // CPU cycles of drawing still outstanding
    bool interruptRequestFired = false;

    uint16_t horizontalDisplayRangeX1 = 0;
    uint16_t horizontalDisplayRangeX2 = 0;
    uint16_t verticalDisplayRangeY1 = 0;
    uint16_t verticalDisplayRangeY2 = 0;
    bool forceSetMaskBit = false;
    bool preserveMaskedPixels = false;

    /// @todo GPUSTAT must mirror GP0(E1) bits 0-10, and bit 15 when allowed.
    bool textureDisableAllowed = false;
    
    uint8_t DMADirection = 0;
    uint32_t gpuReadLatch = 0;

    uint32_t currentCommand = 0;
    int wordsRemaining = 0;
    int parametersRemaining = 0;


    // CPU to VRAM (0xA0) / VRAM to CPU (0xC0)
    uint16_t transferX = 0, transferY = 0;
    uint16_t transferWidth = 0, transferHeight = 0;
    int transferXCursor = 0, transferYCursor = 0;
    
    uint16_t readTransferX = 0, readTransferY = 0;
    uint16_t readTransferWidth = 0, readTransferHeight = 0;
    int readTransferXCursor = 0, readTransferYCursor = 0;
    int vramReadWordsRemaining = 0;

    // Shared Attributes & CLUT
    bool primitiveIsTextured = false;
    bool primitiveIsSemiTransparent = false;
    bool primitiveIsRawTexture = false;
    bool primitiveIsGouraud = false;
    uint16_t primitiveColorLookupTableX = 0;
    uint16_t primitiveColorLookupTableY = 0;

    // Polygons
    Rasterizer::Vertex primitiveVertices[4];
    int primitiveVertexCount = 0;
    int primitiveCurrentVertexIndex = 0;

    // Gouraud Parsing Order
    enum class GouraudPolygonWordRole { Color, Position, Texture };
    std::vector<GouraudPolygonWordRole> gouraudPolygonWordSequence;
    int gouraudPolygonWordCursor = 0;

    // Rectangles (0x60-0x7F)
    bool rectangleTextured = false;
    bool rectangleSemiTransparent = false;
    bool rectangleRawTexture = false;
    uint8_t rectangleSizeMode = 0; 
    int rectangleParametersExpected = 0;
    int16_t rectangleX = 0, rectangleY = 0;
    uint16_t rectangleWidth = 0, rectangleHeight = 0;
    uint8_t rectangleTextureCoordinateU = 0, rectangleTextureCoordinateV = 0;
    uint16_t rectangleColorLookupTableX = 0, rectangleColorLookupTableY = 0;

    // Lines
    bool lineIsPolyline = false;
    bool lineIsGouraud = false;
    bool lineIsSemiTransparent = false;
    Rasterizer::Vertex lineVertices[2];
    int lineWordStep = 0;


    /// @brief Parses the next word for a multi-word command currently in progress.
    /// @param value The 32-bit word from the GP0 port.
    void continuePendingCommand(uint32_t value);


    /// @brief Initiates a new GPU command from the GP0 port.
    /// @param value The 32-bit command word.
    void beginNewCommand(uint32_t value);


    /// @brief Parses parameters for line rendering (0x40-0x5F).
    /// @param value The 32-bit parameter word.
    void handleLineWord(uint32_t value);

    
    /// @brief Parses parameters for the VRAM Block Fill (0x02) command.
    /// @param value The 32-bit parameter word.
    void handleFillRectangleWord(uint32_t value);


    /// @brief Parses parameters for flat-shaded polygons (0x20-0x2B).
    /// @param value The 32-bit parameter word.
    void handleMonotonePolygonWord(uint32_t value);


    /// @brief Parses parameters and transfers data for the CPU-to-VRAM (0xA0) command.
    /// @param value The 32-bit data or parameter word.
    void handleCopyCPUToVRAMWord(uint32_t value);


    /// @brief Parses parameters for the VRAM-to-CPU (0xC0) command.
    /// @param value The 32-bit parameter word.
    void handleCopyVRAMToCPUParameters(uint32_t value);


    /// @brief Parses parameters for the VRAM-to-VRAM (0x80) copy command.
    /// @param value The 32-bit parameter word.
    void handleCopyVRAMToVRAMWord(uint32_t value);


    /// @brief Parses parameters for untextured and textured rectangles (0x60-0x7F).
    /// @param value The 32-bit parameter word.
    void handleRectangleWord(uint32_t value);


    /// @brief Parses parameters for textured, flat-shaded polygons (0x2C-0x2F).
    /// @param value The 32-bit parameter word.
    void handleTexturedPolygonWord(uint32_t value);


    /// @brief Parses parameters for Gouraud-shaded polygons (0x30-0x3F).
    /// @param value The 32-bit parameter word.
    void handleGouraudPolygonWord(uint32_t value);


    /// @brief Finalizes rectangle state and dispatches the draw call to the Rasterizer.
    void finishRectangleSetup();


    /// @brief Updates the DMA request bit in the GPU Status Register based on current FIFO state and DMA direction.
    void updateDMARequestBit();


    /// @brief Accumulates the CPU cycle cost of recent drawing operations from the Rasterizer.
    void chargeDrawCost();


    /// @brief Sign-extends an 11-bit GP0 vertex coordinate (-1024 to +1023) to a standard 16-bit signed integer.
    /// @param value The raw 32-bit word containing the 11-bit coordinate in its lowest bits.
    /// @return The sign-extended 16-bit coordinate.
    static int16_t signExtend11(uint32_t value) { return static_cast<int16_t>(static_cast<int32_t>(value << 21) >> 21);}
};