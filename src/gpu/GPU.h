#pragma once
#include <cstdint>
#include <vector>
#include <iostream>
#include "../Constants.h"
#include "VRAM.h"
#include "Rasterizer.h"

/// @brief Emulates the PS1 Graphics Processing Unit, handling display timing, VRAM management, and rendering primitives.
class GPU {
public:
    GPU();

    bool consumeInterruptRequest() {
        if (interruptRequestFired) {
            interruptRequestFired = false;
            return true;
        }
        return false;
    }
    
    /// @brief Reads the next data word from the GP0 port.
	/// @return The next pixel pair from an in-progress VRAM-to-CPU transfer, or 0 if none is pending.
    uint32_t readGP0();


    /// @brief Reads the GPU status register via the GP1 port.
	/// @return The current status register, after side effects (FIFO-ready bits, odd/even frame toggle) needed to satisfy BIOS polling.
    uint32_t readGP1();


    /// @brief Writes a command or data word to the GP0 port (drawing commands and VRAM transfers).
	/// @param value The 32-bit word being written.
    void writeGP0(uint32_t value);


    /// @brief Writes a control word to the GP1 port (display control and GPU/command-buffer reset).
	/// @param value The 32-bit word being written.
    void writeGP1(uint32_t value);


    /// @brief Toggles the odd/even frame bit (Bit 31) in the Status Register during VBlank.
    void toggleFrameBit() { gpuStatusRegister ^= 0x80000000; }


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
private:
    // Core Hardware State
    VRAM       vram;
    Rasterizer rasterizer;
    uint32_t   gpuStatusRegister = Hardware::GPU_DEFAULT_STATUS;
    uint8_t    DMADirection = 0;



    bool interruptRequestFired = false;


    
    uint32_t gpuReadLatch = 0;


    // Command Parser & FIFO
    uint32_t currentCommand = 0;
    int      wordsRemaining = 0;
    int      parametersRemaining = 0;


    // CPU to VRAM (0xA0) / VRAM to CPU (0xC0)
    uint16_t transferX = 0, transferY = 0;
    uint16_t transferWidth = 0, transferHeight = 0;
    int      transferXCursor = 0, transferYCursor = 0;
    
    uint16_t readTransferX = 0, readTransferY = 0;
    uint16_t readTransferWidth = 0, readTransferHeight = 0;
    int      readTransferXCursor = 0, readTransferYCursor = 0;
    int      vramReadWordsRemaining = 0;


    // Rectangles (0x60-0x7F)
    bool     rectangleTextured = false;
    bool     rectangleSemiTransparent = false;
    bool     rectangleRawTexture = false;
    uint8_t  rectangleSizeMode = 0; 
    int      rectangleParametersExpected = 0;
    int16_t  rectangleX = 0, rectangleY = 0;
    uint16_t rectangleWidth = 0, rectangleHeight = 0;
    uint8_t  rectangleTextureCoordinateU = 0, rectangleTextureCoordinateV = 0;
    uint16_t rectangleColorLookupTableX = 0, rectangleColorLookupTableY = 0;


    // Unified Polygon State
    Vertex primitiveVertices[4];
    int    primitiveVertexCount = 0;
    int    primitiveCurrentVertexIndex = 0;


    // Shared Primitive Attributes
    bool primitiveIsTextured = false;
    bool primitiveIsSemiTransparent = false;
    bool primitiveIsRawTexture = false;
    bool primitiveIsGouraud = false;


    // Shared Color Lookup Table (CLUT) 
    uint16_t primitiveColorLookupTableX = 0;
    uint16_t primitiveColorLookupTableY = 0;


    // Specifically for Gouraud parsing order
    enum class GouraudPolygonWordRole { Color, Position, Texture };
    std::vector<GouraudPolygonWordRole> gouraudPolygonWordSequence;
    int gouraudPolygonWordCursor = 0;


    // GP0 Entry Points
    void continuePendingCommand(uint32_t value);
    void beginNewCommand(uint32_t value);

    
    // GP0 Word Handlers
    void handleFillRectangleWord(uint32_t value);
    void handleMonotoneQuadWord(uint32_t value);
    void handleCopyCPUToVRAMWord(uint32_t value);
    void handleCopyVRAMToCPUParameters(uint32_t value);
    void handleRectangleWord(uint32_t value);
    void handleTexturedPolygonWord(uint32_t value);
    void handleGouraudPolygonWord(uint32_t value);
    void finishRectangleSetup();


    // Misc
    void updateDMARequestBit();
};